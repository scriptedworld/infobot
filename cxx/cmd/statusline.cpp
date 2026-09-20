// The status line's entry point. Claude Code runs it through bin/infobot with
// the session payload on stdin; everything it does is render::statusline.
#include <unistd.h>

#include "render.hpp"

int main() { return infobot::render::statusline(STDIN_FILENO, STDOUT_FILENO); }
