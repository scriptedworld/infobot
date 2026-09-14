#include "environment.hpp"

#include <cstdlib>
#include <string>
#include <string_view>

#include "gopath.hpp"

namespace infobot {

namespace {

std::string variable(const char* name) {
    const char* value = std::getenv(name);
    return value == nullptr ? std::string{} : std::string(value);
}

}  // namespace

Environment Environment::from_process() {
    return {
        .home = variable("HOME"),
        .xdg_config_home = variable("XDG_CONFIG_HOME"),
        .xdg_state_home = variable("XDG_STATE_HOME"),
        .plain = !variable("NO_COLOR").empty(),
        .tmux = variable("TMUX"),
        .tmux_pane = variable("TMUX_PANE"),
        .herdr_pane_id = variable("HERDR_PANE_ID"),
        .herdr_bin_path = variable("HERDR_BIN_PATH"),
    };
}

std::string Environment::config_file(std::string_view name) const {
    std::string root = xdg_config_home;
    if (root.empty()) {
        if (home.empty()) {
            return {};
        }
        root = gopath::join({home, ".config"});
    }
    return gopath::join({root, "infobot", name});
}

std::string Environment::state_dir() const {
    std::string root = xdg_state_home;
    if (root.empty()) {
        if (home.empty()) {
            return {};
        }
        root = gopath::join({home, ".local", "state"});
    }
    return gopath::join({root, "infobot"});
}

std::string Environment::projects() const {
    if (home.empty()) {
        return {};
    }
    return gopath::join({home, ".claude", "projects"});
}

}  // namespace infobot
