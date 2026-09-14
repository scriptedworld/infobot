// What the process environment says, read once.
//
// The Go port calls os.Getenv wherever it needs a value and its tests move the
// environment with t.Setenv. Here it is read once at the entry point and handed
// down, so a test states the environment it means instead of mutating the
// process's, which setenv cannot do safely.
#pragma once

#include <string>
#include <string_view>

namespace infobot {

struct Environment {
    // $HOME. Empty is what os.UserHomeDir reports as an error, and every Go
    // caller treats that error as "no home".
    std::string home;
    std::string xdg_config_home;
    std::string xdg_state_home;
    // NO_COLOR set to anything at all.
    bool plain = false;
    std::string tmux;
    std::string tmux_pane;
    std::string herdr_pane_id;
    std::string herdr_bin_path;

    [[nodiscard]] static Environment from_process();

    // ~/.config/infobot/<name>, following XDG_CONFIG_HOME. Empty when neither
    // it nor a home can be found.
    [[nodiscard]] std::string config_file(std::string_view name) const;

    // ~/.local/state/infobot, following XDG_STATE_HOME. Empty as above.
    [[nodiscard]] std::string state_dir() const;

    // ~/.claude/projects, where the transcripts are. Empty with no home.
    [[nodiscard]] std::string projects() const;
};

}  // namespace infobot
