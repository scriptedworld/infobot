#include "environment.hpp"

#include <unistd.h>

#include <string>
#include <string_view>

#include "gopath.hpp"

namespace infobot {

namespace {

// The value of name in the environment block, read directly rather than through
// getenv, which is not thread safe. Nothing here runs a second thread and
// nothing sets a variable, but reading the block needs no such argument.
std::string variable(std::string_view name) {
    for (char** entry = ::environ; entry != nullptr && *entry != nullptr; ++entry) {
        const std::string_view pair(*entry);
        if (pair.size() > name.size() && pair.starts_with(name) &&
            pair[name.size()] == '=') {
            return std::string(pair.substr(name.size() + 1));
        }
    }
    return {};
}

}  // namespace

Environment from_process() {
    return {
        .home = variable("HOME"),
        .xdg_config_home = variable("XDG_CONFIG_HOME"),
        .xdg_state_home = variable("XDG_STATE_HOME"),
        .plain = !variable("NO_COLOR").empty(),
        .tmux = variable("TMUX"),
        .tmux_pane = variable("TMUX_PANE"),
        .herdr_pane_id = variable("HERDR_PANE_ID"),
        .herdr_bin_path = variable("HERDR_BIN_PATH"),
        .path = variable("PATH"),
    };
}

std::string config_file(const Environment& env, std::string_view name) {
    std::string root = env.xdg_config_home;
    if (root.empty()) {
        if (env.home.empty()) {
            return {};
        }
        root = gopath::join({env.home, ".config"});
    }
    return gopath::join({root, "infobot", name});
}

std::string state_dir(const Environment& env) {
    std::string root = env.xdg_state_home;
    if (root.empty()) {
        if (env.home.empty()) {
            return {};
        }
        root = gopath::join({env.home, ".local", "state"});
    }
    return gopath::join({root, "infobot"});
}

std::string projects(const Environment& env) {
    if (env.home.empty()) {
        return {};
    }
    return gopath::join({env.home, ".claude", "projects"});
}

}  // namespace infobot
