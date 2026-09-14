#include "host.hpp"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <simdjson.h>
#include <spawn.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "environment.hpp"
#include "files.hpp"
#include "gojson.hpp"
#include "gotext.hpp"

namespace infobot::host {

namespace {

using Clock = std::chrono::steady_clock;

// A hung multiplexer must not hang a line that renders on every event.
constexpr std::chrono::milliseconds ask_timeout{2000};
// How long the pipe is drained after a kill before it is closed regardless.
// Without it the bound is the grandchild.
constexpr std::chrono::milliseconds wait_delay{250};

// How much of herdr's reported width is not drawable, set from measurement.
constexpr int herdr_trim = 10;

// The walk up the process tree to a terminal is three or four hops; anything
// longer is a loop or a surprise.
constexpr int ancestor_limit = 16;

constexpr std::size_t read_chunk = 4096;
constexpr int decimal = 10;

class Pipe {
   public:
    Pipe() { ok_ = ::pipe2(ends_.data(), O_CLOEXEC) == 0; }
    ~Pipe() {
        close_read();
        close_write();
    }
    Pipe(const Pipe&) = delete;
    Pipe& operator=(const Pipe&) = delete;
    Pipe(Pipe&&) = delete;
    Pipe& operator=(Pipe&&) = delete;

    [[nodiscard]] bool ok() const { return ok_; }
    [[nodiscard]] int read_end() const { return ends_[0]; }
    [[nodiscard]] int write_end() const { return ends_[1]; }
    void close_read() { close_end(0); }
    void close_write() { close_end(1); }

   private:
    void close_end(std::size_t which) {
        if (ends_.at(which) >= 0) {
            ::close(ends_.at(which));
            ends_.at(which) = -1;
        }
    }
    std::array<int, 2> ends_{-1, -1};
    bool ok_ = false;
};

// Reads what is available before the deadline. False once the deadline has
// passed; `open` goes false at end of file.
bool drain(int fd, Clock::time_point deadline, std::string& out, bool& open) {
    std::array<char, read_chunk> buffer{};
    while (open) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - Clock::now());
        if (left.count() <= 0) {
            return false;
        }
        pollfd watch{.fd = fd, .events = POLLIN, .revents = 0};
        if (::poll(&watch, 1, static_cast<int>(left.count())) <= 0) {
            continue;
        }
        const ssize_t got = ::read(fd, buffer.data(), buffer.size());
        if (got <= 0) {
            open = false;
        } else {
            out.append(buffer.data(), static_cast<std::size_t>(got));
        }
    }
    return true;
}

// Waits for the child until the deadline. nullopt when it is still running.
std::optional<int> reap(pid_t pid, Clock::time_point deadline) {
    constexpr std::chrono::milliseconds step{1};
    for (;;) {
        int status = 0;
        const pid_t done = ::waitpid(pid, &status, WNOHANG);
        if (done == pid) {
            return status;
        }
        if (done < 0 || Clock::now() >= deadline) {
            return done < 0 ? std::optional<int>{status} : std::nullopt;
        }
        ::usleep(static_cast<useconds_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(step).count()));
    }
}

std::optional<pid_t> spawn(const std::vector<std::string>& args, const Pipe& out) {
    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (const std::string& arg : args) {
        argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);

    posix_spawn_file_actions_t actions{};
    ::posix_spawn_file_actions_init(&actions);
    ::posix_spawn_file_actions_addopen(
        &actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    ::posix_spawn_file_actions_adddup2(&actions, out.write_end(), STDOUT_FILENO);
    ::posix_spawn_file_actions_addopen(
        &actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    pid_t pid = 0;
    const int failed =
        ::posix_spawnp(&pid, argv[0], &actions, nullptr, argv.data(), ::environ);
    ::posix_spawn_file_actions_destroy(&actions);
    if (failed != 0) {
        return std::nullopt;
    }
    return pid;
}

// The pane id and width of one pane in herdr's layout.
struct Pane {
    std::string pane_id;
    std::int64_t width = 0;
};

struct Layout {
    std::int64_t area_width = 0;
    std::string focused_pane_id;
    bool zoomed = false;
    std::optional<std::vector<Pane>> panes;
};

void decode_pane(gojson::Decode& d, simdjson::dom::element value, Pane& pane) {
    d.as_struct(value, [&](simdjson::dom::object object) {
        d.fields(object, "pane_id", [&](auto v) { d.string_into(v, pane.pane_id); });
        d.fields(object, "rect", [&](auto rect) {
            d.as_struct(rect, [&](simdjson::dom::object r) {
                d.fields(r, "width", [&](auto w) { d.int_into(w, pane.width); });
            });
        });
    });
}

void decode_layout(gojson::Decode& d, simdjson::dom::object object, Layout& layout) {
    d.fields(object, "area", [&](auto area) {
        d.as_struct(area, [&](simdjson::dom::object a) {
            d.fields(a, "width", [&](auto w) { d.int_into(w, layout.area_width); });
        });
    });
    d.fields(object, "focused_pane_id", [&](auto v) {
        d.string_into(v, layout.focused_pane_id);
    });
    d.fields(object, "zoomed", [&](auto v) { d.bool_into(v, layout.zoomed); });
    d.fields(
        object, "panes", [&](auto v) { d.slice_into(v, layout.panes, decode_pane); });
}

std::optional<Layout> parse_layout(std::string_view reply) {
    simdjson::dom::parser parser;
    simdjson::dom::element root;
    if (parser.parse(reply.data(), reply.size()).get(root) != simdjson::SUCCESS) {
        return std::nullopt;
    }
    gojson::Decode d;
    Layout layout;
    d.as_struct(root, [&](simdjson::dom::object top) {
        d.fields(top, "result", [&](auto result) {
            d.as_struct(result, [&](simdjson::dom::object r) {
                d.fields(r, "layout", [&](auto value) {
                    d.as_struct(value, [&](simdjson::dom::object l) {
                        decode_layout(d, l, layout);
                    });
                });
            });
        });
    });
    if (!d.ok()) {
        return std::nullopt;
    }
    return layout;
}

// The pts a process holds on a standard descriptor, or empty.
std::string tty_of(pid_t pid) {
    const std::string base = "/proc/" + std::to_string(pid) + "/fd/";
    for (const char* fd : {"0", "1", "2"}) {
        constexpr std::size_t link_max = 4096;
        std::array<char, link_max> link{};
        const std::string path = base + fd;
        const ssize_t length = ::readlink(path.c_str(), link.data(), link.size());
        if (length <= 0) {
            continue;
        }
        const std::string_view target(link.data(), static_cast<std::size_t>(length));
        if (target.starts_with("/dev/pts/")) {
            return std::string(target);
        }
    }
    return {};
}

// The parent pid out of /proc/<pid>/stat, or 0. Parsed from the LAST ')',
// because the process name before it may itself contain spaces and parentheses.
pid_t parent_of(pid_t pid) {
    const auto raw = files::read("/proc/" + std::to_string(pid) + "/stat");
    if (!raw) {
        return 0;
    }
    const std::size_t closing = raw->rfind(')');
    if (closing == std::string::npos) {
        return 0;
    }
    std::string_view rest = std::string_view(*raw).substr(closing + 1);
    // After the name: state, then the parent.
    for (int field = 0; field < 2; ++field) {
        const std::size_t start = rest.find_first_not_of(" \t\n");
        if (start == std::string_view::npos) {
            return 0;
        }
        rest.remove_prefix(start);
        const std::size_t end = rest.find_first_of(" \t\n");
        if (field == 1) {
            pid_t parent = 0;
            const std::string_view token = rest.substr(0, end);
            const auto parsed =
                std::from_chars(token.data(), token.data() + token.size(), parent);
            return parsed.ec == std::errc{} && parsed.ptr == token.data() + token.size()
                       ? parent
                       : 0;
        }
        rest.remove_prefix(end == std::string_view::npos ? rest.size() : end);
    }
    return 0;
}

// A terminal's width by TIOCGWINSZ, opened O_NOCTTY so this process never
// acquires it as a controlling terminal.
int winsize_columns(const std::string& path) {
    const int fd = ::open(path.c_str(), O_RDONLY | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        return 0;
    }
    winsize size{};
    const int failed = ::ioctl(fd, TIOCGWINSZ, &size);
    ::close(fd);
    return failed != 0 ? 0 : size.ws_col;
}

}  // namespace

std::string ask(std::initializer_list<std::string_view> argv) {
    const std::vector<std::string> args(argv.begin(), argv.end());
    Pipe out;
    if (!out.ok() || args.empty()) {
        return {};
    }
    const auto pid = spawn(args, out);
    out.close_write();
    if (!pid) {
        return {};
    }
    const Clock::time_point deadline = Clock::now() + ask_timeout;
    std::string output;
    bool open = true;
    const bool in_time = drain(out.read_end(), deadline, output, open);
    const auto status = in_time ? reap(*pid, deadline) : std::nullopt;
    if (!status) {
        ::kill(*pid, SIGKILL);
        std::string ignored;
        (void)drain(out.read_end(), Clock::now() + wait_delay, ignored, open);
        int reaped = 0;
        ::waitpid(*pid, &reaped, 0);
        return {};
    }
    if (!WIFEXITED(*status) || WEXITSTATUS(*status) != 0) {
        return {};
    }
    return std::string(gotext::trim_space(output));
}

int atoi(std::string_view text) {
    if (text.empty()) {
        return 0;
    }
    int total = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') {
            return 0;
        }
        total = total * decimal + (c - '0');
    }
    return total;
}

int usable(int reported) { return reported <= herdr_trim ? 0 : reported - herdr_trim; }

int tmux_width(const Environment& env) {
    if (env.tmux.empty()) {
        return 0;
    }
    // TARGET THE CALLING PANE. Untargeted, display-message answers for the
    // active pane of the client, which in a split is whichever pane happens to
    // have focus.
    if (!env.tmux_pane.empty()) {
        return atoi(ask(
            {"tmux", "display-message", "-p", "-t", env.tmux_pane, "#{pane_width}"}));
    }
    return atoi(ask({"tmux", "display-message", "-p", "#{pane_width}"}));
}

int herdr_layout_width(std::string_view reply, std::string_view pane_id) {
    const auto layout = parse_layout(reply);
    if (!layout || !layout->panes) {
        return 0;
    }
    const std::vector<Pane>& panes = *layout->panes;
    std::optional<std::size_t> index;
    for (std::size_t i = 0; i < panes.size(); ++i) {
        if (panes[i].pane_id == pane_id) {
            index = i;
        }
    }
    // A tab of exactly one pane answers whatever id it carries: there is only
    // one rectangle it could be.
    if (!index) {
        if (panes.size() != 1) {
            return 0;
        }
        index = 0;
    }
    const Pane& pane = panes[*index];
    // A zoomed pane's rectangle is the unzoomed one, so the tab's area is the
    // width it is drawn at.
    if (layout->zoomed && pane.pane_id == layout->focused_pane_id) {
        return usable(static_cast<int>(layout->area_width));
    }
    return usable(static_cast<int>(pane.width));
}

int herdr_width(const Environment& env) {
    if (env.herdr_pane_id.empty()) {
        return 0;
    }
    const std::string binary =
        env.herdr_bin_path.empty() ? "herdr" : env.herdr_bin_path;
    const std::string answer = ask({binary, "pane", "layout", "--current"});
    if (answer.empty()) {
        return 0;
    }
    return herdr_layout_width(answer, env.herdr_pane_id);
}

int tty_width() {
    pid_t pid = ::getpid();
    for (int hop = 0; hop < ancestor_limit; ++hop) {
        if (const std::string path = tty_of(pid); !path.empty()) {
            if (const int columns = winsize_columns(path); columns != 0) {
                return columns;
            }
        }
        const pid_t parent = parent_of(pid);
        // 1 is init and 0 a failed read. Neither has a terminal worth asking.
        if (parent <= 1) {
            return 0;
        }
        pid = parent;
    }
    return 0;
}

int terminal_width(const Environment& env) {
    if (const int w = tmux_width(env); w != 0) {
        return w;
    }
    if (const int w = herdr_width(env); w != 0) {
        return w;
    }
    // A HOST THAT IS PRESENT BUT DID NOT ANSWER STAYS UNKNOWN. The terminal
    // behind a pane is wider than the pane, so falling through to it here would
    // build every row past the edge.
    if (!env.tmux.empty() || !env.herdr_pane_id.empty()) {
        return 0;
    }
    return tty_width();
}

}  // namespace infobot::host
