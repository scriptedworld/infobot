#include "platform.hpp"

#include <sys/types.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstddef>

namespace infobot::platform {

void drain(int descriptor) noexcept {
    constexpr std::size_t chunk = 4096;
    std::array<char, chunk> buffer{};
    for (;;) {
        const ssize_t got = ::read(descriptor, buffer.data(), buffer.size());
        if (got > 0 || (got < 0 && errno == EINTR)) {
            continue;
        }
        return;
    }
}

}  // namespace infobot::platform
