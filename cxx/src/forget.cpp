#include "forget.hpp"

#include "platform.hpp"

namespace infobot::forget {

int run(int input, int /*log*/) noexcept {
    platform::drain(input);
    return 0;
}

}  // namespace infobot::forget
