#include "host.hpp"

#include <fcntl.h>
#include <poll.h>
#include <simdjson.h>
#include <spawn.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <csignal>
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
#include "gopath.hpp"
#include "gotext.hpp"

namespace infobot::host {

namespace {

using Clock = std::chrono::steady_clock;

// A hung multiplexer must not hang a line that renders on every event.
constexpr std::chrono::milliseconds ask_timeout{2000};
// Go's WaitDelay: how long the pipes are waited on once the process has exited.
// Without it the bound is the grandchild.
constexpr std::chrono::milliseconds wait_delay{250};
// How often a wait on pipes looks at whether the process has exited.
constexpr std::chrono::milliseconds poll_step{5};

// How much of herdr's reported width is not drawable, set from measurement.
constexpr std::int64_t herdr_trim = 10;

// The walk up the process tree to a terminal is three or four hops; anything
// longer is a loop or a surprise.
constexpr int ancestor_limit = 16;

constexpr std::size_t read_chunk = 4096;
constexpr std::uint64_t decimal = 10;

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

// findExecutable: a regular file this process may execute.
bool executable(const std::string& path) {
    struct stat info {};
    if (::stat(path.c_str(), &info) != 0 || S_ISDIR(info.st_mode)) {
        return false;
    }
    return ::faccessat(AT_FDCWD, path.c_str(), X_OK, AT_EACCESS) == 0;
}

std::optional<pid_t> spawn(const std::string& program,
                           const std::vector<std::string>& args,
                           int out,
                           int err) {
    // posix_spawn takes char* const[], and does not write through it.
    std::vector<char*> argv(args.size() + 1, nullptr);
    std::ranges::transform(args, argv.begin(), [](const std::string& arg) {
        return const_cast<char*>(arg.c_str());
    });
    posix_spawn_file_actions_t actions{};
    ::posix_spawn_file_actions_init(&actions);
    ::posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    ::posix_spawn_file_actions_adddup2(&actions, out, STDOUT_FILENO);
    ::posix_spawn_file_actions_adddup2(&actions, err, STDERR_FILENO);
    pid_t pid = 0;
    const int failed =
        ::posix_spawn(&pid, program.c_str(), &actions, nullptr, argv.data(), ::environ);
    ::posix_spawn_file_actions_destroy(&actions);
    if (failed != 0) {
        return std::nullopt;
    }
    return pid;
}

// The child's stdout and stderr, each read until it closes. stderr is read and
// dropped: Go's Output captures it through a pipe of its own, so a grandchild
// holding stderr bounds the wait exactly as one holding stdout does.
struct Streams {
    int out;
    int err;
    bool out_open = true;
    bool err_open = true;
    std::string output;
};

void read_ready(int fd, bool& open, std::string* into) {
    std::array<char, read_chunk> buffer{};
    const ssize_t got = ::read(fd, buffer.data(), buffer.size());
    if (got <= 0) {
        open = false;
    } else if (into != nullptr) {
        into->append(buffer.data(), static_cast<std::size_t>(got));
    }
}

// One round of waiting on whichever streams are still open, for at most wait.
void pump(Streams& streams, std::chrono::milliseconds wait) {
    std::array<pollfd, 2> watch{{
        {.fd = streams.out_open ? streams.out : -1, .events = POLLIN, .revents = 0},
        {.fd = streams.err_open ? streams.err : -1, .events = POLLIN, .revents = 0},
    }};
    if (::poll(watch.data(), watch.size(), static_cast<int>(wait.count())) <= 0) {
        return;
    }
    if (watch[0].revents != 0) {
        read_ready(streams.out, streams.out_open, &streams.output);
    }
    if (watch[1].revents != 0) {
        read_ready(streams.err, streams.err_open, nullptr);
    }
}

// The child's wait status if it has exited. A waitpid that fails reads as an
// exit that is not a success.
std::optional<int> exited(pid_t pid) {
    constexpr int not_a_success = -1;
    int status = 0;
    const pid_t done = ::waitpid(pid, &status, WNOHANG);
    if (done == 0) {
        return std::nullopt;
    }
    return done == pid ? status : not_a_success;
}

// cmd.Output under a context deadline and a WaitDelay: the stdout of a child
// that exits 0 with both streams closed, and nothing in every other case.
std::string collect(pid_t pid, Streams& streams) {
    const Clock::time_point deadline = Clock::now() + ask_timeout;
    std::optional<int> status;
    Clock::time_point ended{};
    for (;;) {
        if (!status) {
            status = exited(pid);
            ended = Clock::now();
        }
        if (status && !streams.out_open && !streams.err_open) {
            break;
        }
        const Clock::time_point now = Clock::now();
        if (status && now >= ended + wait_delay) {
            return {};
        }
        if (!status && now >= deadline) {
            ::kill(pid, SIGKILL);
            int reaped = 0;
            ::waitpid(pid, &reaped, 0);
            return {};
        }
        pump(streams, poll_step);
    }
    if (!WIFEXITED(*status) || WEXITSTATUS(*status) != 0) {
        return {};
    }
    return std::string(gotext::trim_space(streams.output));
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

// `{"width": N}`, the shape of both a pane's rect and the tab's area.
void decode_width(gojson::Decode& d, simdjson::dom::element value, std::int64_t& width) {
    d.as_struct(value, [&](simdjson::dom::object object) {
        d.fields(object, "width", [&](auto w) { d.int_into(w, width); });
    });
}

void decode_pane(gojson::Decode& d, simdjson::dom::element value, Pane& pane) {
    d.as_struct(value, [&](simdjson::dom::object object) {
        d.fields(object, "pane_id", [&](auto v) { d.string_into(v, pane.pane_id); });
        d.fields(object, "rect", [&](auto rect) { decode_width(d, rect, pane.width); });
    });
}

void decode_layout(gojson::Decode& d, simdjson::dom::element value, Layout& layout) {
    d.as_struct(value, [&](simdjson::dom::object object) {
        d.fields(object, "area", [&](auto area) {
            decode_width(d, area, layout.area_width);
        });
        d.fields(object, "focused_pane_id", [&](auto v) {
            d.string_into(v, layout.focused_pane_id);
        });
        d.fields(object, "zoomed", [&](auto v) { d.bool_into(v, layout.zoomed); });
        d.fields(object, "panes", [&](auto v) {
            d.slice_into(v, layout.panes, decode_pane);
        });
    });
}

void decode_result(gojson::Decode& d, simdjson::dom::element value, Layout& layout) {
    d.as_struct(value, [&](simdjson::dom::object object) {
        d.fields(object, "layout", [&](auto v) { decode_layout(d, v, layout); });
    });
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
        d.fields(top, "result", [&](auto result) { decode_result(d, result, layout); });
    });
    if (!d.ok()) {
        return std::nullopt;
    }
    return layout;
}

// The pts a process holds on a standard descriptor, or empty.
std::string tty_of(pid_t pid, std::string_view proc) {
    const std::string base = gopath::join({proc, std::to_string(pid), "fd"});
    for (const std::string_view fd : {"0", "1", "2"}) {
        constexpr std::size_t link_max = 4096;
        std::array<char, link_max> link{};
        const std::string path = gopath::join({base, fd});
        const ssize_t length = ::readlink(path.c_str(), link.data(), link.size());
        const std::string_view target(link.data(),
                                      length > 0 ? static_cast<std::size_t>(length) : 0);
        if (target.starts_with("/dev/pts/")) {
            return std::string(target);
        }
    }
    return {};
}

// The field after the next run of whitespace in rest, advancing past it.
std::string_view next_field(std::string_view& rest) {
    const std::size_t start = rest.find_first_not_of(" \t\n");
    if (start == std::string_view::npos) {
        rest = {};
        return {};
    }
    rest.remove_prefix(start);
    const std::size_t end = std::min(rest.find_first_of(" \t\n"), rest.size());
    const std::string_view field = rest.substr(0, end);
    rest.remove_prefix(end);
    return field;
}

// The parent pid out of <proc>/<pid>/stat, or 0. Parsed from the LAST ')',
// because the process name before it may itself contain spaces and parentheses.
pid_t parent_of(pid_t pid, std::string_view proc) {
    const auto raw = files::read(gopath::join({proc, std::to_string(pid), "stat"}));
    if (!raw) {
        return 0;
    }
    const std::size_t closing = raw->rfind(')');
    if (closing == std::string::npos) {
        return 0;
    }
    std::string_view rest = std::string_view(*raw).substr(closing + 1);
    // After the name: state, then the parent.
    (void)next_field(rest);
    const std::string_view token = next_field(rest);
    pid_t parent = 0;
    const auto parsed = std::from_chars(token.data(), token.data() + token.size(), parent);
    const bool whole = parsed.ec == std::errc{} && parsed.ptr == token.data() + token.size();
    return whole ? parent : 0;
}

// A terminal's width by TIOCGWINSZ, opened O_NOCTTY so this process never
// acquires it as a controlling terminal.
std::int64_t winsize_columns(const std::string& path) {
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

std::string look_path(std::string_view name, const Environment& env) {
    if (name.empty()) {
        return {};
    }
    if (name.find('/') != std::string_view::npos) {
        const std::string path(name);
        return executable(path) ? path : std::string{};
    }
    std::string_view rest = env.path;
    while (!rest.empty()) {
        const std::size_t colon = std::min(rest.find(':'), rest.size());
        const std::string_view dir = rest.substr(0, colon);
        rest.remove_prefix(colon == rest.size() ? colon : colon + 1);
        const std::string path = gopath::join({dir.empty() ? "." : dir, name});
        if (executable(path)) {
            // ErrDot: a match found through a relative directory is refused.
            return path.starts_with('/') ? path : std::string{};
        }
    }
    return {};
}

std::string ask(std::initializer_list<std::string_view> argv, const Environment& env) {
    if (argv.size() == 0) {
        return {};
    }
    const std::vector<std::string> args(argv.begin(), argv.end());
    // exec.Command looks up a bare name and runs a path with a slash as given.
    const std::string program = args[0].find('/') == std::string::npos
                                    ? look_path(args[0], env)
                                    : args[0];
    Pipe out;
    Pipe err;
    if (program.empty() || !out.ok() || !err.ok()) {
        return {};
    }
    const auto pid = spawn(program, args, out.write_end(), err.write_end());
    out.close_write();
    err.close_write();
    if (!pid) {
        return {};
    }
    Streams streams{.out = out.read_end(),
                    .err = err.read_end(),
                    .out_open = true,
                    .err_open = true,
                    .output = {}};
    return collect(*pid, streams);
}

std::int64_t atoi(std::string_view text) {
    if (text.empty()) {
        return 0;
    }
    std::uint64_t total = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') {
            return 0;
        }
        total = (total * decimal) + static_cast<std::uint64_t>(c - '0');
    }
    return static_cast<std::int64_t>(total);
}

std::int64_t usable(std::int64_t reported) {
    return reported <= herdr_trim ? 0 : reported - herdr_trim;
}

std::int64_t tmux_width(const Environment& env) {
    if (env.tmux.empty()) {
        return 0;
    }
    // TARGET THE CALLING PANE. Untargeted, display-message answers for the
    // active pane of the client, which in a split is whichever pane happens to
    // have focus.
    if (!env.tmux_pane.empty()) {
        return atoi(ask({"tmux", "display-message", "-p", "-t", env.tmux_pane, "#{pane_width}"},
                        env));
    }
    return atoi(ask({"tmux", "display-message", "-p", "#{pane_width}"}, env));
}

std::int64_t herdr_layout_width(std::string_view reply, const Environment& env) {
    const auto layout = parse_layout(reply);
    if (!layout || !layout->panes) {
        return 0;
    }
    const std::vector<Pane>& panes = *layout->panes;
    std::optional<std::size_t> index;
    for (std::size_t i = 0; i < panes.size(); ++i) {
        if (panes[i].pane_id == env.herdr_pane_id) {
            index = i;
        }
    }
    // A tab of exactly one pane answers whatever id it carries: there is only
    // one rectangle it could be.
    if (!index && panes.size() == 1) {
        index = 0;
    }
    if (!index) {
        return 0;
    }
    const Pane& pane = panes[*index];
    // A zoomed pane's rectangle is the unzoomed one, so the tab's area is the
    // width it is drawn at.
    if (layout->zoomed && pane.pane_id == layout->focused_pane_id) {
        return usable(layout->area_width);
    }
    return usable(pane.width);
}

std::int64_t herdr_width(const Environment& env) {
    if (env.herdr_pane_id.empty()) {
        return 0;
    }
    const std::string binary = env.herdr_bin_path.empty() ? "herdr" : env.herdr_bin_path;
    const std::string answer = ask({binary, "pane", "layout", "--current"}, env);
    return answer.empty() ? 0 : herdr_layout_width(answer, env);
}

std::int64_t tty_width_from(pid_t start, std::string_view proc) {
    pid_t pid = start;
    for (int hop = 0; hop < ancestor_limit; ++hop) {
        const std::string path = tty_of(pid, proc);
        if (const std::int64_t columns = path.empty() ? 0 : winsize_columns(path);
            columns != 0) {
            return columns;
        }
        const pid_t parent = parent_of(pid, proc);
        // 1 is init and 0 a failed read. Neither has a terminal worth asking.
        if (parent <= 1) {
            return 0;
        }
        pid = parent;
    }
    return 0;
}

std::int64_t tty_width() {
    return tty_width_from(::getpid(), "/proc");
}

std::int64_t terminal_width(const Environment& env) {
    if (const std::int64_t w = tmux_width(env); w != 0) {
        return w;
    }
    if (const std::int64_t w = herdr_width(env); w != 0) {
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
