#include "render.hpp"

#include <doctest/doctest.h>
#include <unistd.h>

#include <array>
#include <cstdint>
#include <format>
#include <initializer_list>
#include <limits>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

#include "environment.hpp"
#include "files.hpp"
#include "palette.hpp"
#include "payload.hpp"
#include "support.hpp"
#include "width.hpp"

namespace render = infobot::render;
namespace palette = infobot::palette;
namespace payload = infobot::payload;
namespace test = infobot::test;
namespace width = infobot::width;

namespace {

constexpr std::int64_t minute = 60;
constexpr std::int64_t hour = 3600;
constexpr std::int64_t day = 86400;
constexpr std::string_view session = "a5e58a4d-0000-4000-8000-000000000000";

// Every escape the renderer writes, which is SGR and nothing else.
const std::regex& ansi() {
    static const std::regex pattern("\033\\[[0-9;]*m");
    return pattern;
}

// The context window at a percentage of 200k.
std::string window(double pct) {
    return std::format(R"({{"context_window_size":200000,"used_percentage":{}}})", pct);
}

// A rate limit window, with a reset time when resets_at is not 0.
std::string limit(double pct, double resets_at) {
    if (resets_at == 0) {
        return std::format(R"({{"used_percentage":{}}})", pct);
    }
    return std::format(R"({{"used_percentage":{},"resets_at":{}}})", pct, resets_at);
}

std::string repeat(std::string_view unit, int count) {
    std::string out;
    for (int i = 0; i < count; ++i) {
        out.append(unit);
    }
    return out;
}

std::string joined(const std::vector<std::string>& rows) {
    std::string out;
    for (const std::string& row : rows) {
        out.append(row).push_back('\n');
    }
    return out;
}

// The cells of every bar in text, filled and empty.
int cells(std::string_view text) {
    int count = 0;
    for (const std::string_view glyph :
         {std::string_view("▰"), std::string_view("▱")}) {
        for (auto at = text.find(glyph); at != std::string_view::npos;
             at = text.find(glyph, at + glyph.size())) {
            ++count;
        }
    }
    return count;
}

render::Renderer renderer(bool plain) {
    return {{.palette = palette::seed(),
             .margin = render::default_margin,
             .plain = plain,
             .env = nullptr},
            test::clock()};
}

// isolate: HOME and the state and config directories are scratch, so a render
// reads no real transcript, rate table or palette and leaves no offsets naming a
// session that never existed. The host variables stay empty, so the width is
// whatever the caller passes.
class Isolated {
   public:
    // home is what paths are shown relative to, and names no transcripts. Empty
    // is a scratch home, for a render whose paths are not asserted on.
    explicit Isolated(std::string_view home) {
        env_.home = home.empty() ? scratch_home_.path() : std::string(home);
        env_.xdg_config_home = config_.path();
        env_.xdg_state_home = state_.path();
    }

    [[nodiscard]] std::vector<std::string> build(std::string_view json,
                                                 int columns,
                                                 bool plain) const {
        const payload::Document doc(json);
        const render::Renderer rows({.palette = palette::seed(),
                                     .margin = render::default_margin,
                                     .plain = plain,
                                     .env = &env_},
                                    test::clock());
        return rows.build(doc.root(), columns);
    }

    [[nodiscard]] const infobot::Environment& env() const { return env_; }

   private:
    test::Scratch scratch_home_;
    test::Scratch config_;
    test::Scratch state_;
    infobot::Environment env_;
};

struct Ran {
    int code = 0;
    std::string out;
};

// statusline over two pipes: input written and closed, then the rows read back.
Ran run(std::string_view input, const infobot::Environment& env) {
    std::array<int, 2> in{};
    std::array<int, 2> out{};
    REQUIRE(::pipe(in.data()) == 0);
    REQUIRE(::pipe(out.data()) == 0);
    REQUIRE(infobot::files::write_all(in[1], input));
    ::close(in[1]);
    const int code =
        render::statusline({.input = in[0], .output = out[1]}, env, test::clock());
    ::close(out[1]);
    const std::optional<std::string> written = infobot::files::read_all(out[0]);
    ::close(in[0]);
    ::close(out[0]);
    return {.code = code, .out = written.value_or("")};
}

}  // namespace

// COVERS: FR-6.9 | property
TEST_CASE("tokens are compact") {
    struct Case {
        double in;
        std::string_view want;
    };
    for (const auto c : {
             Case{.in = 0, .want = "0"},
             Case{.in = 999, .want = "999"},
             Case{.in = 1000, .want = "1k"},
             Case{.in = 21000, .want = "21k"},
             Case{.in = 142000, .want = "142k"},
             Case{.in = 999999, .want = "1000k"},
             Case{.in = 1000000, .want = "1.0M"},
             Case{.in = 2700000, .want = "2.7M"},
         }) {
        CAPTURE(c.in);
        CHECK(render::tokens(c.in) == c.want);
    }
}

// COVERS: FR-7.9 | property
TEST_CASE("countdown is the largest two non-zero units") {
    struct Case {
        std::int64_t in;
        std::string_view want;
    };
    const auto rows = renderer(false);
    for (const auto c : {
             Case{.in = (3 * hour) + (30 * minute), .want = "3h30m"},
             Case{.in = (3 * hour) + (5 * minute), .want = "3h05m"},
             Case{.in = 45 * minute, .want = "45m"},
             Case{.in = (2 * day) + (5 * hour), .want = "2d5h"},
             Case{.in = 7 * day, .want = "7d0h"},
             Case{.in = 90, .want = "1m"},
         }) {
        CAPTURE(c.in);
        CHECK(rows.countdown(test::at(c.in)) == c.want);
    }
}

// COVERS: FR-2.4 | negative
//
// A countdown reading "0m" suggests a reset is imminent when it has already
// happened and the number is simply stale.
TEST_CASE("countdown is empty when missing or past") {
    const auto rows = renderer(false);
    for (const double in : {0.0, test::at(-1), test::at(-day)}) {
        CAPTURE(in);
        CHECK(rows.countdown(in).empty());
    }
}

// COVERS: FR-3.9 | property
//
// Escapes cost nothing and east-asian wide glyphs cost two. len() is wrong in
// both directions and both errors run toward a row too wide for the line.
TEST_CASE("visible width measures what the terminal draws") {
    struct Case {
        std::string_view in;
        int want;
    };
    for (const auto c : {
             Case{.in = "", .want = 0},
             Case{.in = "abc", .want = 3},
             Case{.in = "\033[0mabc\033[0m", .want = 3},
             Case{.in = "\033[38;2;1;2;3mx\033[0m", .want = 1},
             Case{.in = "🧠", .want = 2},
             Case{.in = "🧠 x", .want = 4},
             // ambiguous, counted as one each
             Case{.in = "▰▱", .want = 2},
             // the rail
             Case{.in = "╭─ ", .want = 3},
             Case{.in = "⟨abcd⟩", .want = 6},
         }) {
        CAPTURE(c.in);
        CHECK(width::visible(c.in) == c.want);
    }
}

// COVERS: FR-6.7, FR-6.8 | property
TEST_CASE("bar is filled and empty parallelograms only") {
    struct Case {
        double pct;
        int cells;
        std::string_view want;
    };
    const auto rows = renderer(true);
    for (const auto c : {
             Case{.pct = 0, .cells = 10, .want = "▱▱▱▱▱▱▱▱▱▱"},
             Case{.pct = 50, .cells = 10, .want = "▰▰▰▰▰▱▱▱▱▱"},
             Case{.pct = 100, .cells = 10, .want = "▰▰▰▰▰▰▰▰▰▰"},
             // below 0 renders all empty
             Case{.pct = -20, .cells = 10, .want = "▱▱▱▱▱▱▱▱▱▱"},
             // above 100 renders all filled
             Case{.pct = 140, .cells = 10, .want = "▰▰▰▰▰▰▰▰▰▰"},
         }) {
        CAPTURE(c.pct);
        CHECK(rows.bar(c.pct, c.cells, "") == c.want);
    }
}

// COVERS: FR-6.8 | edge
TEST_CASE("bar of no cells renders nothing") {
    constexpr double half = 50;
    const auto rows = renderer(false);
    for (const int none : {0, -1}) {
        CAPTURE(none);
        CHECK(rows.bar(half, none, "").empty());
    }
}

// COVERS: FR-3.8 | property
//
// NO_COLOR strips EVERY escape, the separator and the rail included, so the
// output is either coloured or clean and never half of each.
TEST_CASE("NO_COLOR leaves no escape anywhere") {
    constexpr double context = 48;
    constexpr double five = 34;
    constexpr int columns = 120;
    const Isolated home("/tmp");
    const std::string data =
        std::format(R"({{"session_id":"{}","model":{{"display_name":"Opus 5"}},)"
                    R"("workspace":{{"current_dir":"/tmp/x"}},"context_window":{},)"
                    R"("rate_limits":{{"five_hour":{}}}}})",
                    session,
                    window(context),
                    limit(five, test::at(hour)));
    for (const std::string& row : home.build(data, columns, true)) {
        CAPTURE(row);
        CHECK_FALSE(row.contains('\033'));
    }
}

// COVERS: FR-5.1 | property
//
// An opening corner with no closing corner under it reads as a block that
// failed to finish, so a lone row gets the stub instead.
TEST_CASE("rail corners match the row count") {
    constexpr double context = 20;
    constexpr double five = 34;
    constexpr int columns = 120;
    const Isolated home("/tmp");

    const auto one =
        home.build(std::format(R"({{"session_id":"{}"}})", session), columns, true);
    REQUIRE(one.size() == 1);
    CHECK(one[0].starts_with("╶─ "));

    const auto two = home.build(
        std::format(
            R"({{"session_id":"{}","context_window":{},"rate_limits":{{"five_hour":{}}}}})",
            session,
            window(context),
            limit(five, 0)),
        columns,
        true);
    REQUIRE(two.size() == 2);
    CHECK(two[0].starts_with("╭─ "));
    CHECK(two[1].starts_with("╰─ "));
}

// COVERS: FR-1.5 | negative
//
// A row with no parts in it is omitted rather than printed blank. Early in a
// session the context and rate-limit fields are all absent.
TEST_CASE("an empty row is omitted") {
    constexpr int columns = 120;
    const Isolated home("/tmp");
    const auto rows =
        home.build(std::format(R"({{"session_id":"{}"}})", session), columns, false);
    CHECK(rows.size() == 1);
}

// COVERS: FR-1.3 | negative
//
// A segment whose data is absent is DROPPED, never rendered as zero, because
// zero is a claim and absence is not.
TEST_CASE("absent data is dropped not zeroed") {
    constexpr int columns = 120;
    const Isolated home("/tmp");
    const std::string all = joined(home.build(
        std::format(R"({{"session_id":"{}","model":{{"display_name":"Opus 5"}}}})",
                    session),
        columns,
        true));
    for (const std::string_view absent : {"0%", "0/", "🧠", "⏳", "📅"}) {
        CAPTURE(absent);
        CHECK_FALSE(all.contains(absent));
    }
}

// COVERS: FR-2.13 | property
TEST_CASE("unknown payload fields change nothing") {
    constexpr double context = 48;
    constexpr int columns = 120;
    const Isolated home("/tmp");
    const std::string base = std::format(
        R"("session_id":"{}","model":{{"display_name":"Opus 5"}},"context_window":{})",
        session,
        window(context));
    const auto before = home.build("{" + base + "}", columns, true);
    const auto after =
        home.build("{" + base + R"(,"something_claude_code_added_later":{"x":1})" + "}",
                   columns,
                   true);
    CHECK(joined(before) == joined(after));
}

// COVERS: FR-2.9, FR-2.10, FR-2.11 | property
TEST_CASE("the identity row shows paths and session") {
    constexpr int columns = 200;
    const Isolated home("/home/me");
    const auto rows =
        home.build(R"({"session_id":"a5e58a4d-2a4e-4774-aa6c-1e7745721df6",)"
                   R"("model":{"display_name":"Opus 5"},"effort":{"level":"high"},)"
                   R"("workspace":{"current_dir":"/home/me/.projects/demo/bin",)"
                   R"("project_dir":"/home/me/.projects/demo"}})",
                   columns,
                   true);
    REQUIRE_FALSE(rows.empty());
    const std::string& row = rows[0];
    CAPTURE(row);
    CHECK(row.contains("Opus 5 - high"));
    CHECK(row.contains("~/.projects/demo/bin"));
    CHECK(row.contains("⌂ ~/.projects/demo"));
    CHECK(row.contains("⟨a5e58a4d⟩"));
}

// COVERS: FR-2.9 | negative
//
// Repeating the root is noise on the common case, a session started where the
// work is.
TEST_CASE("the project root is hidden when it matches the working directory") {
    constexpr int columns = 200;
    const Isolated home("/home/me");
    const auto rows =
        home.build(R"({"model":{"display_name":"Opus 5"},)"
                   R"("workspace":{"current_dir":"/home/me/.projects/demo",)"
                   R"("project_dir":"/home/me/.projects/demo"}})",
                   columns,
                   true);
    REQUIRE_FALSE(rows.empty());
    CHECK_FALSE(rows[0].contains("⌂"));
}

// COVERS: FR-3.5, FR-3.6 | property
//
// The width a row is fitted to is the pane less the three columns Claude Code
// keeps, and what exceeds it is cut rather than wrapped.
// Below about 70 columns two rate limit windows cannot be made to fit even
// once their gauges are given up for the percentages, so the row runs over and
// the terminal cuts it. Measured on the Python this replaced: at width 60 its
// meter row was the same 65 columns against the same 57 budget. That is FR-3.6
// working, not a fitting bug, so the fitting assertion starts where fitting is
// achievable.
TEST_CASE("rows fit inside the budget") {
    constexpr double context = 48;
    constexpr double five = 34;
    constexpr double seven = 62;
    constexpr std::array widths = {80, 120, 191, 223};
    const Isolated home("/home/me");
    const std::string data = std::format(
        R"({{"session_id":"a5e58a4d-2a4e-4774-aa6c-1e7745721df6",)"
        R"("model":{{"display_name":"Opus 5"}},)"
        R"("workspace":{{"current_dir":"/home/me/.projects/demo"}},)"
        R"("context_window":{},"rate_limits":{{"five_hour":{},"seven_day":{}}}}})",
        window(context),
        limit(five, test::at(hour)),
        limit(seven, test::at(day)));
    for (const int columns : widths) {
        for (const std::string& row : home.build(data, columns, false)) {
            CAPTURE(columns);
            CAPTURE(row);
            CHECK(width::visible(row) <= columns - render::default_margin);
        }
    }
}

namespace {

// A path long enough to leave the context bar no room at narrow widths.
std::string deep_path_payload() {
    constexpr int depth = 8;
    constexpr double context = 48;
    return std::format(R"({{"model":{{"display_name":"Opus 5"}},)"
                       R"("workspace":{{"current_dir":"/home/me/{}leaf"}},)"
                       R"("context_window":{}}})",
                       repeat("deeply/", depth),
                       window(context));
}

}  // namespace

// COVERS: FR-3.6, FR-5.12 | edge
//
// The bar is DROPPED rather than clamped when the row has no room for it, and
// the counts stay either way.
TEST_CASE("the bar is dropped not clamped when there is no room") {
    constexpr int columns = 40;
    const Isolated home("/home/me");
    const std::string all = joined(home.build(deep_path_payload(), columns, true));
    CAPTURE(all);
    CHECK(cells(all) == 0);
    CHECK(all.contains("consumed"));
}

// COVERS: FR-3.6 | property
//
// The bar shrinks with the pane and is dropped whole below BAR_MIN rather than
// clamped to it. Clamping overflows, which costs the whole row to save a bar
// that at 12% a cell was not saying much. Cell counts measured against the
// Python this replaced, at the widths where the two must agree.
TEST_CASE("the bar shrinks with the pane then goes whole") {
    struct Case {
        int width;
        int cells;
    };
    const Isolated home("/home/me");
    for (const auto c : {
             Case{.width = 40, .cells = 0},
             Case{.width = 45, .cells = 12},
             Case{.width = 50, .cells = 17},
             Case{.width = 60, .cells = 27},
             Case{.width = 70, .cells = 37},
         }) {
        CAPTURE(c.width);
        CHECK(cells(joined(home.build(deep_path_payload(), c.width, true))) == c.cells);
    }
}

// COVERS: FR-3.3, FR-5.12 | edge
//
// With the width unknown there is NO BAR AT ALL. A bar is a claim about how
// much room there is, and with nothing to fit against, any length is a guess
// the host then truncates. The numbers say the same thing at a known cost, so
// the row stays short and left-aligned.
TEST_CASE("an unknown width draws no bar") {
    constexpr double context = 50;
    const Isolated home("/home/me");
    const auto rows =
        home.build(std::format(R"({{"context_window":{}}})", window(context)), 0, true);
    REQUIRE_FALSE(rows.empty());
    CAPTURE(rows[0]);
    CHECK(cells(rows[0]) == 0);
    CHECK(rows[0].contains("50%"));
}

// COVERS: FR-2.2 | property
//
// rate_limits carries a percentage and a reset time and NOTHING ELSE, so no
// token counts appear there.
TEST_CASE("the limit segment shows no token counts") {
    constexpr double five = 34;
    const payload::Document doc(limit(five, test::at(hour)));
    const std::string got = renderer(true).limit_segment(
        "⏳ 5hr", doc.root(), render::five_hour, false, true);
    CAPTURE(got);
    CHECK_FALSE(got.contains('/'));
    CHECK_FALSE(got.contains("consumed"));
    CHECK(got.contains("1h00m"));
}

// COVERS: FR-1.3 | negative
TEST_CASE("the limit segment is dropped without a percentage") {
    const auto rows = renderer(false);
    const payload::Document empty("{}");
    const payload::Document reset_only(
        std::format(R"({{"resets_at":{}}})", test::at(hour)));
    for (const payload::Map& w : {payload::Map{}, empty.root(), reset_only.root()}) {
        CHECK(rows.limit_segment("⏳ 5hr", w, render::five_hour, false, true).empty());
    }
}

// COVERS: FR-5.6 | property
//
// A meter row over budget gives up its gauges for the percentages they were
// drawing before anything is cut.
TEST_CASE("compact gauges replace bars when the row is tight") {
    constexpr double five = 34;
    const auto rows = renderer(true);
    const payload::Document doc(limit(five, 0));
    const std::string wide =
        rows.limit_segment("⏳ 5hr", doc.root(), render::five_hour, false, true);
    const std::string tight =
        rows.limit_segment("⏳ 5hr", doc.root(), render::five_hour, true, true);
    CAPTURE(wide);
    CAPTURE(tight);
    CHECK(wide.contains("▰"));
    CHECK_FALSE(tight.contains("▰"));
    CHECK(tight.contains("34%"));
    CHECK(width::visible(tight) < width::visible(wide));
}

// COVERS: FR-1.4 | regression
//
// A failure confined to one segment costs only that segment. The Python this
// replaced wrapped the whole render in a bare except, so a malformed resets_at
// blanked BOTH rows; golden/malformed-resets.txt captured that. Here the field
// is absent data and the rest of the line survives.
TEST_CASE("one bad field costs only its segment") {
    constexpr int columns = 120;
    const Isolated home("/home/me");
    const auto rows = home.build(
        R"({"model":{"display_name":"Opus 5"},)"
        R"("rate_limits":{"five_hour":{"used_percentage":11,"resets_at":"BAD-ISO"}}})",
        columns,
        true);
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].contains("Opus 5"));
    CHECK(rows[1].contains("⏳ 5hr"));
    // The countdown is what could not be read, so it is what goes.
    CAPTURE(rows[1]);
    CHECK_FALSE((rows[1].contains('h') && rows[1].contains('m')));
}

// COVERS: FR-1.2 | negative
TEST_CASE("statusline exits zero whatever it is given") {
    const Isolated home("");
    for (const std::string_view in :
         {"", "not json at all", "[]", "null", "{}", R"({"session_id":123})"}) {
        CAPTURE(in);
        const Ran ran = run(in, home.env());
        CHECK(ran.code == 0);
        CHECK_FALSE(ran.out.contains("panic"));
    }
}

// COVERS: FR-6.4 | property
//
// The last filled cell carries the same colour as the number printed beside it,
// because they mean the same thing.
TEST_CASE("the last filled cell matches the number's colour") {
    constexpr int bar_cells = 50;
    const payload::Document doc(
        R"({"context_window_size":200000,"used_percentage":48})");
    const std::string segment = renderer(false).context_segment(doc.root(), bar_cells);
    std::vector<std::string> codes;
    for (auto it = std::sregex_iterator(segment.begin(), segment.end(), ansi());
         it != std::sregex_iterator();
         ++it) {
        codes.push_back(it->str());
    }
    REQUIRE(codes.size() >= 2);
    // The escape opening the counts is the last distinct colour in the string,
    // and the bar's final filled cell carries the one before the empty track.
    REQUIRE(segment.contains("consumed"));
    const std::string& counts_colour = codes[codes.size() - 2];
    CHECK(segment.contains(counts_colour));
}

// COVERS: FR-2.4 | edge
//
// A reset time too far off to be a number of seconds is no countdown. Go's
// int64 conversion of NaN or an out-of-range double gives the minimum int64 on
// amd64, which reads as already past.
TEST_CASE("countdown is empty for a reset time that is not a number of seconds") {
    constexpr double far = 1e30;
    const auto rows = renderer(false);
    for (const double in : {std::numeric_limits<double>::quiet_NaN(), far, -far}) {
        CAPTURE(in);
        CHECK(rows.countdown(in).empty());
    }
}

// COVERS: FR-2.4, FR-7.8 | negative
//
// A window whose reset has already passed keeps its gauge and loses only its
// countdown, and with no position left in the window it is tinted as the
// consumption ramp would tint it.
TEST_CASE("a window past its reset keeps its gauge and drops its countdown") {
    constexpr double five = 34;
    const auto rows = renderer(false);
    const payload::Document past(limit(five, test::at(-hour)));
    const payload::Document none(limit(five, 0));
    const std::string got =
        rows.limit_segment("⏳ 5hr", past.root(), render::five_hour, false, true);
    CHECK(got ==
          rows.limit_segment("⏳ 5hr", none.root(), render::five_hour, false, true));
    CHECK(got.contains(rows.ramp(five)));
}

// COVERS: FR-2.10 | property
//
// Inside home means home itself or a path under it at a directory boundary, so
// a sibling sharing home's leading characters is shown whole.
TEST_CASE("a path is shown relative to home only when it is inside it") {
    struct Case {
        std::string_view path;
        std::string_view home;
        std::string_view want;
    };
    for (const auto c : {
             Case{.path = "/home/me", .home = "/home/me", .want = "~"},
             Case{.path = "/home/me/x", .home = "/home/me", .want = "~/x"},
             Case{.path = "/home/me/", .home = "/home/me", .want = "~/"},
             Case{.path = "/home/meadow", .home = "/home/me", .want = "/home/meadow"},
             Case{.path = "/elsewhere", .home = "/home/me", .want = "/elsewhere"},
             Case{.path = "/home/me", .home = "", .want = "/home/me"},
             Case{.path = "", .home = "", .want = "~"},
         }) {
        CAPTURE(c.path);
        CAPTURE(c.home);
        CHECK(render::home_relative(c.path, c.home) == c.want);
    }
}

// COVERS: FR-2.10, FR-1.3 | edge
//
// With no home to be relative to, a path is shown whole, and with no home to
// find transcripts under there is no cost to price. Go's Main passes an empty
// home when os.UserHomeDir fails.
TEST_CASE("with no home the path is shown whole and no cost is priced") {
    constexpr double five = 34;
    constexpr int columns = 200;
    const payload::Document doc(
        std::format(R"({{"session_id":"{}","workspace":{{"current_dir":"/home/me/x"}},)"
                    R"("rate_limits":{{"five_hour":{}}}}})",
                    session,
                    limit(five, test::at(hour))));
    const auto rows = renderer(true).build(doc.root(), columns);
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].contains("/home/me/x"));
    CHECK_FALSE(rows[0].contains('~'));
    CHECK_FALSE(rows[1].contains("💵"));
}

// COVERS: FR-5.1 | property
//
// A tee for every row between the corners, and the rail drawn in the empty
// cells' colour when colour is on.
TEST_CASE("rows between the corners hang off a tee") {
    const auto plain = renderer(true).rail({"a", "b", "c"});
    REQUIRE(plain.size() == 3);
    CHECK(plain[0] == "╭─ a");
    CHECK(plain[1] == "├─ b");
    CHECK(plain[2] == "╰─ c");
    CHECK(renderer(true).rail({}).empty());

    const auto coloured = renderer(false).rail({"a"});
    REQUIRE(coloured.size() == 1);
    CHECK(coloured[0] ==
          palette::seed().empty + "╶─ " + std::string(palette::reset) + "a");
}

// COVERS: FR-3.5 | property
//
// The reserve is three columns unless ~/.config/infobot/layout.json says
// otherwise. A file setting 0 says the host reserves nothing, so absent and
// zero differ; anything outside 0 to 40, or anything that is not an integer,
// leaves the default.
TEST_CASE("the margin comes from the layout file or is three") {
    struct Case {
        std::string_view body;
        int want;
    };
    const test::Scratch config;
    CHECK(render::load_margin("") == render::default_margin);
    CHECK(render::load_margin(config.file("missing.json")) == render::default_margin);
    for (const auto c : {
             Case{.body = R"({"margin":8})", .want = 8},
             Case{.body = R"({"margin":0})", .want = 0},
             Case{.body = R"({"margin":40})", .want = 40},
             Case{.body = R"({"margin":41})", .want = 3},
             Case{.body = R"({"margin":-1})", .want = 3},
             Case{.body = R"({"margin":null})", .want = 3},
             Case{.body = R"({"margin":"8"})", .want = 3},
             Case{.body = R"({"margin":8.5})", .want = 3},
             Case{.body = R"({})", .want = 3},
             Case{.body = R"([8])", .want = 3},
             Case{.body = "not json", .want = 3},
         }) {
        CAPTURE(c.body);
        CHECK(render::load_margin(config.write("layout.json", c.body)) == c.want);
    }
}
