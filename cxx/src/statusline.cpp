#include "statusline.hpp"

#include "platform.hpp"

namespace infobot::statusline {

int run(int input, int /*output*/) noexcept {
    platform::drain(input);
    return 0;
}

}  // namespace infobot::statusline
