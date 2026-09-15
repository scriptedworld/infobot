#include "environment.hpp"

#include <doctest/doctest.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <cstdlib>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "files.hpp"
#include "support.hpp"

using infobot::Environment;
using infobot::test::Scratch;

namespace {

// An Environment naming only these, built by assignment so every other field
// keeps its default.
Environment with(std::string home, std::string config, std::string state) {
    Environment env;
    env.home = std::move(home);
    env.xdg_config_home = std::move(config);
    env.xdg_state_home = std::move(state);
    return env;
}

// The environment this process was started with, read from /proc rather than
// through environ, so it is a second reading rather than the same one twice.
// The first of a repeated name is kept, as getenv and Go's syscall.Getenv keep
// it.
std::map<std::string, std::string, std::less<>> initial_environment() {
    const std::string block = infobot::files::read("/proc/self/environ").value_or("");
    std::map<std::string, std::string, std::less<>> vars;
    std::string_view rest(block);
    while (!rest.empty()) {
        const std::size_t end = rest.find('\0');
        const std::string_view pair = rest.substr(0, end);
        const std::size_t equals = pair.find('=');
        if (equals != std::string_view::npos) {
            vars.emplace(std::string(pair.substr(0, equals)),
                         std::string(pair.substr(equals + 1)));
        }
        rest.remove_prefix(end == std::string_view::npos ? rest.size() : end + 1);
    }
    return vars;
}

std::string value_of(const std::map<std::string, std::string, std::less<>>& vars,
                     std::string_view name) {
    const auto found = vars.find(name);
    return found == vars.end() ? std::string{} : found->second;
}

}  // namespace

// COVERS: FR-8.15 | positive
//
// The rate table lives under XDG_CONFIG_HOME when it is set, and under
// ~/.config when it is not, which is pricing.TablePath.
TEST_CASE("config_file follows XDG_CONFIG_HOME, then home") {
    CHECK(infobot::config_file(with("/h", "/x", ""), "pricing.json") ==
          "/x/infobot/pricing.json");
    CHECK(infobot::config_file(with("", "/x/", ""), "pricing.json") ==
          "/x/infobot/pricing.json");
    CHECK(infobot::config_file(with("/h", "", ""), "pricing.json") ==
          "/h/.config/infobot/pricing.json");
    // Go's TablePath takes the variable as it is, relative or not.
    CHECK(infobot::config_file(with("/h", "rel", ""), "palette.json") ==
          "rel/infobot/palette.json");
}

// COVERS: FR-8.16 | negative
//
// With neither a config home nor a home there is no path, and an empty path
// reads as a missing table, which falls back to the seed.
TEST_CASE("config_file with no home is empty") {
    CHECK(infobot::config_file(with("", "", ""), "pricing.json").empty());
}

// COVERS: FR-4.4 | property
//
// The offsets follow XDG_STATE_HOME, so a test moves them with the environment
// it hands over and never writes beside the real ones.
TEST_CASE("state_dir follows XDG_STATE_HOME, then home") {
    CHECK(infobot::state_dir(with("/h", "", "/s")) == "/s/infobot");
    CHECK(infobot::state_dir(with("", "", "/s")) == "/s/infobot");
    CHECK(infobot::state_dir(with("/h", "/x", "")) == "/h/.local/state/infobot");
    CHECK(infobot::state_dir(with("", "/x", "")).empty());
}

// COVERS: FR-8.2 | positive
//
// The transcripts are under ~/.claude/projects, which has no XDG variable, and
// with no home there is nowhere to look.
TEST_CASE("projects is under home or nowhere") {
    CHECK(infobot::projects(with("/h", "/x", "/s")) == "/h/.claude/projects");
    CHECK(infobot::projects(with("/h/", "", "")) == "/h/.claude/projects");
    CHECK(infobot::projects(with("", "/x", "/s")).empty());
}

// COVERS: FR-4.4 | property
//
// from_process reads the environment the process was started with. Nothing
// about this process's environment is assumed: each field is held against a
// second reading of the same block, taken from /proc.
TEST_CASE("from_process reads the environment the process started with") {
    const auto vars = initial_environment();
    const Environment env = infobot::from_process();
    CHECK(env.home == value_of(vars, "HOME"));
    CHECK(env.xdg_config_home == value_of(vars, "XDG_CONFIG_HOME"));
    CHECK(env.xdg_state_home == value_of(vars, "XDG_STATE_HOME"));
    CHECK(env.plain == !value_of(vars, "NO_COLOR").empty());
    CHECK(env.tmux == value_of(vars, "TMUX"));
    CHECK(env.tmux_pane == value_of(vars, "TMUX_PANE"));
    CHECK(env.herdr_pane_id == value_of(vars, "HERDR_PANE_ID"));
    CHECK(env.herdr_bin_path == value_of(vars, "HERDR_BIN_PATH"));
    CHECK(env.path == value_of(vars, "PATH"));
}

namespace {

// The case above, run in a fresh copy of this binary whose environment is
// exactly env. The environment of this process is never changed; a child is
// started with the one the case needs. The exit status is doctest's.
int run_in_environment(const std::string& out, std::span<std::string> env) {
    std::vector<std::string> args{
        "infobot-tests",
        "--test-case=from_process reads the environment the process started with",
        "--out=" + out,
    };
    // The null-terminated arrays of char* that exec takes, pointing into both.
    std::vector<char*> argv(args.size() + 1, nullptr);
    for (std::size_t i = 0; i < args.size(); ++i) {
        argv[i] = args[i].data();
    }
    std::vector<char*> envp(env.size() + 1, nullptr);
    for (std::size_t i = 0; i < env.size(); ++i) {
        envp[i] = env[i].data();
    }
    pid_t pid = 0;
    if (::posix_spawn(
            &pid, "/proc/self/exe", nullptr, nullptr, argv.data(), envp.data()) != 0) {
        return -1;
    }
    int status = 0;
    if (::waitpid(pid, &status, 0) != pid || !WIFEXITED(status)) {
        return -1;
    }
    return WEXITSTATUS(status);
}

}  // namespace

// COVERS: FR-4.4 | edge
//
// The same reading, in an environment built to catch a lookup by prefix: a
// bare name with no `=`, a longer name that starts with a wanted one, a wanted
// name set twice, where the first is the one Go's syscall.Getenv keeps, and a
// value that is empty or itself holds `=`.
TEST_CASE("from_process reads a stated environment") {
    const Scratch scratch;
    const std::string out = scratch.file("child.txt");
    std::vector<std::string> env{
        "HOME",
        "HOMEBREW_PREFIX=/brew",
        "HOME=/first",
        "HOME=/second",
        "TMUX_PANE=%1",
        "NO_COLOR=1",
        "XDG_STATE_HOME=",
        "PATH=/bin:/usr/bin",
        "HERDR_BIN_PATH=/a=b",
    };
    const int status = run_in_environment(out, env);
    const std::string report = infobot::test::slurp(out);
    CAPTURE(report);
    CHECK(status == 0);
    // A filter that matched nothing also exits 0, so the case is seen to run.
    CHECK(report.find(" 1 passed | 0 failed") != std::string::npos);
}
