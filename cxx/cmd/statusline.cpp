// The status line's entry point. Claude Code runs it through bin/infobot with
// the session payload on stdin; everything it does is statusline::run.
#include "statusline.hpp"

#include <unistd.h>

int main() { return infobot::statusline::run(STDIN_FILENO, STDOUT_FILENO); }
