// The status line's entry point, and nothing else. The program lives in the
// library, where a test can reach it and a checker can read it.
#include <unistd.h>

#include <chrono>

#include "environment.hpp"
#include "render.hpp"

int main() {
    return infobot::render::statusline(STDIN_FILENO,
                                       STDOUT_FILENO,
                                       infobot::Environment::from_process(),
                                       std::chrono::system_clock::now());
}
