#include "platform.hpp"

#include <sys/types.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <string>

namespace infobot::platform {

// One branch here is never taken and cannot be: gcov marks the call to ::read
// with a throw edge, and a C function does not throw. The file carries 10
// branches, so it reads 90% with every reachable one taken.
std::string read_all(int descriptor) {
    constexpr std::size_t chunk = 4096;
    std::array<char, chunk> buffer{};
    std::string held;
    for (;;) {
        const ssize_t got = ::read(descriptor, buffer.data(), buffer.size());
        if (got > 0) {
            held.append(buffer.data(), static_cast<std::size_t>(got));
            continue;
        }
        if (got < 0 && errno == EINTR) {
            continue;
        }
        return held;
    }
}

}  // namespace infobot::platform
