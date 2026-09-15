#include "host.hpp"

#include <doctest/doctest.h>
#include <sys/resource.h>

#include <chrono>
#include <csignal>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <string_view>

#include "environment.hpp"
#include "support.hpp"

namespace host = infobot::host;
using infobot::Environment;
using infobot::test::Scratch;

namespace {

using Clock = std::chrono::steady_clock;

// FR-3.11's bound is two seconds plus a short delay. Five is Go's own margin
// for it, and a host that was not bounded sleeps ten.
constexpr std::chrono::seconds bound{5};

// An executable at scratch/name running body under /bin/sh.
std::string script(const Scratch& scratch, std::string_view name, std::string_view body) {
    const std::string path = scratch.write(name, "#!/bin/sh\n" + std::string(body));
    std::filesystem::permissions(path, std::filesystem::perms::owner_all);
    return path;
}

// A host that prints reply on stdout, so the width routes are exercised
// without a multiplexer.
std::string replying(const Scratch& scratch, std::string_view reply) {
    return script(scratch, "host", "printf '%s\\n' '" + std::string(reply) + "'\n");
}

// The PATH this test process was started with.
std::string process_path() {
    return infobot::from_process().path;
}

// An environment whose only setting is where hosts are looked for.
Environment searching(std::string_view path) {
    Environment env;
    env.path = path;
    return env;
}

// An environment naming a herdr pane and the host that answers for it.
Environment herdr(std::string_view pane, std::string_view binary) {
    Environment env;
    env.herdr_pane_id = pane;
    env.herdr_bin_path = binary;
    env.path = process_path();
    return env;
}

// An environment claiming a tmux socket, naming a pane in it, and looking for
// tmux on search_path.
Environment tmux(std::string_view pane, std::string_view search_path) {
    Environment env;
    env.tmux = "/tmp/tmux-1000/default,1,0";
    env.tmux_pane = pane;
    env.path = search_path;
    return env;
}

// A `tmux` in its own scratch directory that records its arguments and replies
// with a width, and the directory to search for it. The reply is the same
// whatever it is asked, so an assertion is about the QUESTION: the bug this
// guards was a well-formed reply about the wrong pane.
std::string fake_tmux(const Scratch& scratch, std::string_view reply) {
    (void)script(scratch,
                 "bin/tmux",
                 "printf '%s\\n' \"$*\" > " + scratch.file("argv") + "\nprintf '%s\\n' " +
                     std::string(reply) + "\n");
    return scratch.path() + "/bin";
}

// A layout reply of two panes, w4:p1 at 99 and w4:p2 at 96, in a tab 195 wide.
std::string zoomed_layout(std::string_view focused) {
    return R"({"result":{"layout":{
      "area":{"width":195},
      "focused_pane_id":")" +
           std::string(focused) + R"(",
      "zoomed":true,
      "panes":[{"pane_id":"w4:p1","rect":{"width":99}},
               {"pane_id":"w4:p2","rect":{"width":96}}]}}})";
}

}  // namespace

// COVERS: FR-3.3, FR-3.4 | negative
//
// With no multiplexer, the answer is the TERMINAL or unknown, and never the
// fabricated 80 a library would hand back. Believing that 80 would truncate a
// 223-column pane, which is worse than not adapting at all.
//
// This asserts the fabrication is absent rather than asserting 0: tty_width
// reaches the terminal through an ancestor that still holds it, so a real width
// here is a pass. 24 is checked with it, because a library returning the 80x24
// pair would produce both.
TEST_CASE("no host means the terminal or unknown but never eighty") {
    constexpr std::int64_t fabricated_columns = 80;
    constexpr std::int64_t fabricated_rows = 24;
    const std::int64_t got = host::terminal_width(Environment{});
    CHECK(got != fabricated_columns);
    CHECK(got != fabricated_rows);
    CHECK(got >= 0);
}

// COVERS: FR-3.3 | positive
//
// With nothing owning the pane the terminal is the answer, whichever terminal
// the runner has. tty_width walks the real /proc from this process, so what
// holds everywhere is that it returns, is not negative, and is what the whole
// route answers when no host is named.
TEST_CASE("with no host the terminal width is the tty walk") {
    const std::int64_t walked = host::tty_width();
    CHECK(walked >= 0);
    CHECK(host::terminal_width(Environment{}) == walked);
}

// COVERS: FR-3.3 | negative
//
// tmux is asked only when TMUX says this process is inside it. A pane id
// without the socket is not enough to ask.
TEST_CASE("tmux is not asked when TMUX is empty") {
    const Scratch scratch;
    const std::string bin = fake_tmux(scratch, "257");
    Environment env = tmux("%5", bin);
    env.tmux.clear();
    CHECK(host::tmux_width(Environment{}) == 0);
    CHECK(host::tmux_width(env) == 0);
    CHECK(infobot::test::slurp(scratch.file("argv")).empty());
}

// COVERS: FR-3.7 | regression
//
// An untargeted `display-message` answers for the ACTIVE pane of the current
// client, not the pane that asked. Measured 2026-09-01 from pane %5 at 257
// columns with a 60-column %6 focused: the untargeted form said 60. The
// assertion is on the argv, because a reply about the wrong pane is well
// formed and only the question distinguishes the two.
TEST_CASE("tmux is asked about the calling pane not the active one") {
    constexpr std::int64_t want = 257;
    const Scratch scratch;
    const Environment env = tmux("%5", fake_tmux(scratch, "257"));
    CHECK(host::terminal_width(env) == want);
    CHECK(infobot::test::slurp(scratch.file("argv")) ==
          "display-message -p -t %5 #{pane_width}\n");
}

// COVERS: FR-3.3 | edge
//
// Without TMUX_PANE there is no better question than the old one, so the
// untargeted form stays as the fallback rather than the route going unknown.
TEST_CASE("tmux without a pane id still asks") {
    constexpr std::int64_t want = 180;
    const Scratch scratch;
    const Environment env = tmux("", fake_tmux(scratch, "180"));
    CHECK(host::terminal_width(env) == want);
    CHECK(infobot::test::slurp(scratch.file("argv")) ==
          "display-message -p #{pane_width}\n");
}

// COVERS: FR-3.3 | property
//
// The hosts are tried INNERMOST FIRST. tmux inside a herdr pane draws the line
// in the tmux pane, so tmux answers whenever it is there and herdr, the
// outer host, is never run.
TEST_CASE("tmux is asked innermost first") {
    constexpr std::int64_t want = 150;
    const Scratch scratch;
    Environment env = tmux("%1", fake_tmux(scratch, "150"));
    const std::string calls = scratch.file("herdr-calls");
    env.herdr_pane_id = "w1:p1";
    env.herdr_bin_path = script(scratch, "herdr", "echo call >> " + calls + "\n");
    CHECK(host::terminal_width(env) == want);
    CHECK(infobot::test::slurp(calls).empty());
}

// COVERS: FR-3.4 | negative
//
// A HOST THAT IS PRESENT BUT SILENT MUST NOT FALL THROUGH TO THE TERMINAL. The
// terminal behind a pane is WIDER than the pane, so answering with it builds a
// row past the edge and the host cuts the tail on every render.
TEST_CASE("a present host that cannot answer does not borrow the terminal") {
    const Scratch scratch;
    const Environment claimed = tmux("%99", scratch.path());
    CHECK(host::tmux_width(claimed) == 0);
    CHECK(host::terminal_width(claimed) == 0);

    const Environment failing =
        herdr("w1:p1", script(scratch, "host", "printf '%s\\n' 120\nexit 3\n"));
    CHECK(host::herdr_width(failing) == 0);
    CHECK(host::terminal_width(failing) == 0);
}

// COVERS: FR-3.7 | property
//
// A ZOOMED PANE'S RECTANGLE IS THE UNZOOMED ONE. The tab's area is the width to
// use, and the zoomed pane is the focused one.
TEST_CASE("zoomed pane is fitted to the tab area") {
    constexpr std::int64_t want = 185;
    const Scratch scratch;
    const Environment env = herdr("w4:p1", replying(scratch, zoomed_layout("w4:p1")));
    CHECK(host::terminal_width(env) == want);
}

// COVERS: FR-3.7 | edge
//
// Zooming the NEIGHBOUR moves focused_pane_id to it and leaves this pane
// hidden, which is the case where the unzoomed rectangle is right because it is
// what unzooming restores.
TEST_CASE("unfocused pane under zoom keeps its own rectangle") {
    constexpr std::int64_t want = 89;
    const Scratch scratch;
    const Environment env = herdr("w4:p1", replying(scratch, zoomed_layout("w4:p2")));
    CHECK(host::terminal_width(env) == want);
}

// COVERS: FR-3.3 | edge
//
// herdr is looked for by name on PATH when HERDR_BIN_PATH does not pin it.
TEST_CASE("herdr is found on the path when no binary is named") {
    constexpr std::int64_t want = 185;
    const Scratch scratch;
    (void)script(scratch,
                 "bin/herdr",
                 "printf '%s\\n' '" + zoomed_layout("w4:p1") + "'\n");
    Environment env = herdr("w4:p1", "");
    env.path = scratch.path() + "/bin";
    CHECK(host::herdr_width(env) == want);
}

// COVERS: FR-3.3 | edge
//
// A tab holding exactly one pane answers whatever id that pane carries. It
// covers an id in the environment that no longer names the pane the process
// sits in, and it is the one case where not matching costs nothing.
TEST_CASE("lone pane answers even when the id does not match") {
    constexpr std::int64_t want = 110;
    const Scratch scratch;
    const Environment env = herdr("stale:id", replying(scratch, R"({"result":{"layout":{
      "area":{"width":120},
      "focused_pane_id":"w1:p1",
      "zoomed":false,
      "panes":[{"pane_id":"w1:p1","rect":{"width":120}}]}}})"));
    CHECK(host::terminal_width(env) == want);
}

// COVERS: FR-3.3 | negative
//
// Every other mismatch is reported unknown rather than guessed at.
TEST_CASE("mismatched id among several panes is unknown") {
    const Scratch scratch;
    const Environment env = herdr("stale:id", replying(scratch, R"({"result":{"layout":{
      "area":{"width":195},
      "focused_pane_id":"w1:p1",
      "zoomed":false,
      "panes":[{"pane_id":"w1:p1","rect":{"width":99}},
               {"pane_id":"w1:p2","rect":{"width":96}}]}}})"));
    CHECK(host::terminal_width(env) == 0);
}

// COVERS: FR-3.3 | negative
TEST_CASE("unreadable host reply is unknown") {
    for (const std::string_view reply : {"", "not json", "{}", R"({"result":{}})"}) {
        CAPTURE(reply);
        const Scratch scratch;
        CHECK(host::terminal_width(herdr("w1:p1", replying(scratch, reply))) == 0);
    }
}

// COVERS: FR-3.3 | negative
//
// herdr's reply is read the way encoding/json reads it into the Go port's
// struct: one value of the wrong type anywhere is an Unmarshal error, and an
// error is unknown even where the width itself was readable. Every reply here
// names a lone pane 120 wide that would otherwise answer 110.
TEST_CASE("a herdr reply Go fails to unmarshal is unknown") {
    const Environment env = herdr("w1:p1", "");
    for (const std::string_view reply : {
             R"({"result":{"layout":{"area":{"width":1.5},)"
             R"("panes":[{"pane_id":"w1:p1","rect":{"width":120}}]}}})",
             R"({"result":{"layout":{"zoomed":"yes",)"
             R"("panes":[{"pane_id":"w1:p1","rect":{"width":120}}]}}})",
             R"({"result":{"layout":{)"
             R"("panes":[{"pane_id":"w1:p1","rect":{"width":"120"}}]}}})",
             R"({"result":{"layout":{"panes":{"pane_id":"w1:p1"}}}})",
             R"({"result":{"layout":{)"
             R"("panes":[{"pane_id":"w1:p1","rect":{"width":120}}]}}} x)",
         }) {
        CAPTURE(reply);
        CHECK(host::herdr_layout_width(reply, env) == 0);
    }
}

// COVERS: FR-3.3 | edge
//
// A reply with no panes to pick from is unknown however it says so: absent,
// null, or an empty list. A document that is null throughout decodes to the
// same nothing.
TEST_CASE("a herdr reply with no panes is unknown") {
    const Environment env = herdr("w1:p1", "");
    for (const std::string_view reply : {
             "null",
             R"({"result":{"layout":{"area":{"width":195}}}})",
             R"({"result":{"layout":{"area":{"width":195},"panes":null}}})",
             R"({"result":{"layout":{"area":{"width":195},"panes":[]}}})",
             R"({"result":null})",
         }) {
        CAPTURE(reply);
        CHECK(host::herdr_layout_width(reply, env) == 0);
    }
}

// COVERS: FR-3.7 | edge
//
// A pane listed twice answers with its LAST rectangle, because the Go loop
// keeps overwriting its index rather than stopping at the first match. A zoomed
// focused pane with no area reported is drawn at 0 columns, which is unknown.
TEST_CASE("herdr reply edges resolve as the Go loop resolves them") {
    constexpr std::int64_t last_rectangle = 40;
    const Environment env = herdr("w1:p1", "");
    CHECK(host::herdr_layout_width(R"({"result":{"layout":{"panes":[)"
                                   R"({"pane_id":"w1:p1","rect":{"width":99}},)"
                                   R"({"pane_id":"w1:p1","rect":{"width":50}}]}}})",
                                   env) == last_rectangle);
    CHECK(host::herdr_layout_width(
              R"({"result":{"layout":{"focused_pane_id":"w1:p1","zoomed":true,)"
              R"("panes":[{"pane_id":"w1:p1","rect":{"width":99}}]}}})",
              env) == 0);
}

// COVERS: FR-1.8 | property
//
// ONE SUBPROCESS PER RENDER AT MOST, and only to ask a host how wide the pane
// is. The route that answers runs and the others do not: a herdr binary named
// without a pane id is never run, and one with a pane id is run exactly once.
TEST_CASE("at most one subprocess and only for the width") {
    constexpr std::int64_t want = 110;
    const Scratch scratch;
    const std::string calls = scratch.file("calls");
    const std::string counter =
        script(scratch,
               "host",
               "echo call >> " + calls + "\nprintf '%s\\n' '" +
                   R"({"result":{"layout":{"area":{"width":120},)"
                   R"("focused_pane_id":"w1:p1","zoomed":false,)"
                   R"("panes":[{"pane_id":"w1:p1","rect":{"width":120}}]}}})" + "'\n");

    CHECK(host::terminal_width(herdr("", counter)) >= 0);
    CHECK(infobot::test::slurp(calls).empty());

    CHECK(host::terminal_width(herdr("w1:p1", counter)) == want);
    CHECK(infobot::test::slurp(calls) == "call\n");
}

// COVERS: FR-3.3 | positive
//
// What a host prints is trimmed as strings.TrimSpace trims it, Unicode white
// space included, so a reply with a trailing newline still reads as a number.
TEST_CASE("ask returns the host's stdout trimmed") {
    const Scratch scratch;
    const Environment path = searching(process_path());
    CHECK(host::ask({script(scratch, "host", "printf '  137 \\n\\n'\n")}, path) == "137");
    CHECK(host::ask({script(scratch, "nbsp", "printf '\\302\\240137\\302\\205\\n'\n")},
                    path) == "137");
    CHECK(host::ask({script(scratch, "args", "printf '%s|' \"$@\"\n"), "a b", "c"},
                    path) == "a b|c|");
}

// COVERS: FR-3.3 | negative
//
// A host that could not be asked answers empty, which is unknown: one that is
// not there, one that exits non-zero whatever it printed, one killed by a
// signal, one marked executable that is not a program, and a question with no
// program in it.
TEST_CASE("ask is empty when the host could not be asked") {
    const Scratch scratch;
    const Environment path = searching(process_path());
    CHECK(host::ask({scratch.file("absent")}, path).empty());
    CHECK(host::ask({"infobot-no-such-host-on-any-path"}, path).empty());
    CHECK(host::ask({script(scratch, "fails", "printf '%s\\n' 137\nexit 1\n")}, path)
              .empty());
    CHECK(host::ask({script(scratch, "killed", "printf '%s\\n' 137\nkill -9 $$\n")}, path)
              .empty());
    const std::string garbage = scratch.write("garbage", "not a program\n");
    std::filesystem::permissions(garbage, std::filesystem::perms::owner_all);
    CHECK(host::ask({garbage}, path).empty());
    CHECK(host::ask({}, path).empty());
}

// COVERS: FR-3.11 | edge
//
// A host that does not answer costs a BOUNDED wait and then counts as unknown.
// The host here is a shell whose backgrounded child holds stdout while the
// shell waits on it, so killing the shell is not enough.
TEST_CASE("a hung host is bounded and counts as unknown") {
    const Scratch scratch;
    const Environment env = herdr("w1:p1", script(scratch, "host", "sleep 10 &\nwait\n"));
    const auto start = Clock::now();
    const std::int64_t got = host::terminal_width(env);
    const auto elapsed = Clock::now() - start;
    CHECK(got == 0);
    CHECK(elapsed < bound);
}

// COVERS: FR-3.11 | edge
//
// A host that closes stdout and keeps running has not answered either. End of
// file on the pipe is not an exit, so the wait on the process is bounded by the
// same deadline.
TEST_CASE("a host that closes stdout and keeps running is bounded") {
    const Scratch scratch;
    const std::string closing =
        script(scratch, "host", "printf '%s\\n' 137\nexec >&-\nexec 2>&-\nsleep 10\n");
    const auto start = Clock::now();
    const std::string got = host::ask({closing}, searching(process_path()));
    const auto elapsed = Clock::now() - start;
    CHECK(got.empty());
    CHECK(elapsed < bound);
}

// COVERS: FR-3.11 | regression
//
// Go's exec.Cmd.Output captures stderr through a pipe of its own, and WaitDelay
// bounds that pipe as it bounds stdout: a host that exits 0 while a child it
// left behind still holds STDERR returns ErrWaitDelay after 250ms, and ask
// answers empty. Measured 2026-09-14 by running the Go port's ask against this
// exact script: "" in 250ms. The same holds for a child holding stdout.
TEST_CASE("a child holding a stream after a clean exit is unknown as in Go") {
    constexpr std::chrono::milliseconds quick{1500};
    const Scratch scratch;
    const Environment path = searching(process_path());
    for (const std::string_view redirect : {">/dev/null", "2>/dev/null"}) {
        CAPTURE(redirect);
        const std::string holding = script(
            scratch, "host", "printf '%s\\n' 137\nsleep 10 " + std::string(redirect) + " &\n");
        const auto start = Clock::now();
        CHECK(host::ask({holding}, path).empty());
        CHECK(Clock::now() - start < quick);
    }
}

// COVERS: FR-3.11 | edge
//
// A child whose exit cannot be collected, because this process has told the
// kernel to reap its children itself, is an exit that is not a success.
TEST_CASE("a host whose exit cannot be collected is unknown") {
    const Scratch scratch;
    const std::string quiet = script(scratch, "host", "printf '%s\\n' 137\n");
    const auto previous = std::signal(SIGCHLD, SIG_IGN);
    const std::string got = host::ask({quiet}, searching(process_path()));
    (void)std::signal(SIGCHLD, previous);
    CHECK(got.empty());
}

// COVERS: FR-3.3 | negative
//
// A process with no descriptors left cannot open the pipes a host would answer
// through, and that is unknown rather than a crash.
TEST_CASE("a host cannot be asked with no descriptors left") {
    const Scratch scratch;
    const std::string host_path = replying(scratch, "137");
    rlimit before{};
    REQUIRE(::getrlimit(RLIMIT_NOFILE, &before) == 0);
    rlimit none = before;
    none.rlim_cur = 0;
    REQUIRE(::setrlimit(RLIMIT_NOFILE, &none) == 0);
    const std::string got = host::ask({host_path}, searching(process_path()));
    REQUIRE(::setrlimit(RLIMIT_NOFILE, &before) == 0);
    CHECK(got.empty());
}

// COVERS: FR-3.3 | property
//
// A name with no slash is looked for on PATH as exec.LookPath looks: an empty
// entry is the working directory, a directory or a file that is not executable
// is passed over, and a match found through a relative directory is refused, as
// Go refuses it with ErrDot.
TEST_CASE("look_path finds a host as exec.LookPath does") {
    const Scratch scratch;
    const std::string found = script(scratch, "abs/probe", "exit 0\n");
    (void)scratch.write("plain/probe", "not executable\n");
    std::filesystem::create_directories(scratch.path() + "/dir/probe");
    const std::string abs = scratch.path() + "/abs";
    CHECK(host::look_path("probe", searching(abs)) == found);
    CHECK(host::look_path("probe", searching(scratch.path() + "/dir:" + scratch.path() + "/plain:" +
                                       abs)) == found);
    CHECK(host::look_path("probe", searching("")).empty());
    CHECK(host::look_path("", searching(abs)).empty());
    CHECK(host::look_path(found, searching("")).empty() == false);
    CHECK(host::look_path(scratch.path() + "/plain/probe", searching("")).empty());

    const std::string relative =
        std::filesystem::relative(abs, std::filesystem::current_path()).string();
    CHECK(host::look_path("probe", searching(relative)).empty());
    CHECK(host::look_path("infobot-no-such-host-on-any-path", searching(":")).empty());
}

// COVERS: FR-3.7 | property
//
// herdr's rectangle less the ten columns that cannot be drawn into, and never
// below unknown. A width read too small wastes a few columns; one read too
// large truncates on every render.
TEST_CASE("usable trims herdr's rectangle to what can be drawn") {
    struct Case {
        std::int64_t in;
        std::int64_t want;
    };
    for (const auto c : {
             Case{.in = 195, .want = 185},
             Case{.in = 11, .want = 1},
             Case{.in = 10, .want = 0},
             Case{.in = 0, .want = 0},
             Case{.in = -5, .want = 0},
         }) {
        CAPTURE(c.in);
        CHECK(host::usable(c.in) == c.want);
    }
}

// COVERS: FR-3.3 | negative
//
// A tmux reply is decimal digits and nothing else, or it is unknown. Nothing
// is trimmed, signed or partially read here: ask has already trimmed, and a
// reply with anything else in it is not a width.
TEST_CASE("atoi reads decimal digits and nothing else") {
    struct Case {
        std::string_view in;
        std::int64_t want;
    };
    for (const auto c : {
             Case{.in = "257", .want = 257},
             Case{.in = "0", .want = 0},
             Case{.in = "007", .want = 7},
             Case{.in = "", .want = 0},
             Case{.in = " 257", .want = 0},
             Case{.in = "257\n", .want = 0},
             Case{.in = "-1", .want = 0},
             Case{.in = "+1", .want = 0},
             Case{.in = "25a", .want = 0},
             Case{.in = "２", .want = 0},
         }) {
        CAPTURE(c.in);
        CHECK(host::atoi(c.in) == c.want);
    }
}

// COVERS: FR-3.3 | edge
//
// Go's int is 64 bits, so a width past INT_MAX is read as the number it is:
// atoi("2147483658") is 2147483658 and a herdr rectangle of that width is that
// less ten. Measured 2026-09-14 against the Go port's atoi and against
// encoding/json decoding the reply. A width that wraps to a small number is a
// guess, which FR-3.3 forbids.
TEST_CASE("a width past INT_MAX is read as Go's 64-bit int reads it") {
    constexpr std::int64_t reported = 2'147'483'658;
    constexpr std::int64_t trimmed = 2'147'483'648;
    CHECK(host::atoi("2147483658") == reported);
    const Environment env = herdr("w1:p1", "");
    CHECK(host::herdr_layout_width(
              R"({"result":{"layout":{)"
              R"("panes":[{"pane_id":"w1:p1","rect":{"width":2147483658}}]}}})",
              env) == trimmed);
}
