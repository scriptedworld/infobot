// The status line's entry point, and nothing else. The program lives in the
// library, where a test can reach it and a checker can read it.
#include <unistd.h>

#include <chrono>

#include "environment.hpp"
#include "render.hpp"

int main() {
    return infobot::render::statusline({.input = STDIN_FILENO, .output = STDOUT_FILENO},
                                       infobot::from_process(),
                                       std::chrono::system_clock::now());
}
