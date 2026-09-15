#include "forget.hpp"

#include <sys/stat.h>


#include <array>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "environment.hpp"
#include "files.hpp"
#include "goquote.hpp"
#include "payload.hpp"
#include "status_file.hpp"
#include "usage.hpp"

namespace infobot::forget {

bool named(std::string_view session) {
    if (session.empty() || session.find("..") != std::string_view::npos) {
        return false;
    }
    return session.find_first_of("/\\") == std::string_view::npos;
}

std::vector<std::string> remove(std::string_view session, const Environment& env) {
    std::vector<std::string> removed;
    const std::array<std::string, 2> paths = {status_file::path(session, env),
                                              usage::offset_path(session, env)};
    for (const std::string& path : paths) {
        struct stat info {};
        if (path.empty() || ::stat(path.c_str(), &info) != 0) {
            continue;
        }
        if (std::remove(path.c_str()) == 0) {
            removed.push_back(path);
        }
    }
    return removed;
}

int run(Streams streams, const Environment& env) {
    const auto raw = files::read_all(streams.input);
    if (!raw) {
        return 0;
    }
    const payload::Document document(*raw);
    if (!document.root().present()) {
        return 0;
    }
    // The log is where a failure would be reported, so a failed write to it has
    // nowhere to go and is dropped on purpose.
    const std::string_view session = document.root().str("session_id");
    if (!named(session)) {
        (void)files::write_all(
            streams.log, "forget-session: refused session id " + goquote::quote(session) + "\n");
        return 0;
    }
    const std::vector<std::string> gone = remove(session, env);
    if (gone.empty()) {
        (void)files::write_all(streams.log, "forget-session: " + std::string(session) +
                                                " had nothing to remove\n");
        return 0;
    }
    std::string line = "forget-session: " + std::string(session) + " removed";
    for (const std::string& path : gone) {
        line += " " + path;
    }
    (void)files::write_all(streams.log, line + "\n");
    return 0;
}

}  // namespace infobot::forget
