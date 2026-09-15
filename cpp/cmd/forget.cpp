// The SessionEnd hook's entry point, and nothing else. The cleanup lives in the
// library, where a test can reach it and a checker can read it.
#include <unistd.h>

#include "environment.hpp"
#include "forget.hpp"

int main() {
    return infobot::forget::run({.input = STDIN_FILENO, .log = STDERR_FILENO},
                                infobot::from_process());
}
