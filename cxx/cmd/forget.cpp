// The SessionEnd hook's entry point, reached through bin/forget-session;
// everything it does is forget::run.
#include "forget.hpp"

#include <unistd.h>

int main() { return infobot::forget::run(STDIN_FILENO, STDERR_FILENO); }
