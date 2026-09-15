#include "palette.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <initializer_list>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

#include "environment.hpp"
#include "payload.hpp"
#include "render.hpp"
#include "support.hpp"

namespace palette = infobot::palette;
namespace payload = infobot::payload;
namespace render = infobot::render;
namespace test = infobot::test;

namespace {

using Colour = std::array<int, 3>;

constexpr Colour green_rgb = {43, 255, 158};
constexpr int five_hours = 5 * 3600;
constexpr int seven_days = 7 * 86400;

const std::regex& fg_code() {
    static const std::regex pattern(R"(\x1b\[(?:1;)?38;2;(\d+);(\d+);(\d+))");
    return pattern;
}

Colour colour_of(const std::smatch& match) {
    return {std::stoi(match[1].str()),
            std::stoi(match[2].str()),
            std::stoi(match[3].str())};
}

// The first foreground colour in a rendered span.
Colour first_fg(const std::string& text) {
    std::smatch match;
    const bool found = std::regex_search(text, match, fg_code());
    INFO("no foreground colour in ", text);
    REQUIRE(found);
    return colour_of(match);
}

// Every foreground colour in a bar, in order.
std::vector<Colour> cell_colours(const std::string& text) {
    std::vector<Colour> out;
    for (auto it = std::sregex_iterator(text.begin(), text.end(), fg_code());
         it != std::sregex_iterator();
         ++it) {
        out.push_back(colour_of(*it));
    }
    return out;
}

std::size_t occurrences(std::string_view text, std::string_view needle) {
    std::size_t count = 0;
    for (auto at = text.find(needle); at != std::string_view::npos;
         at = text.find(needle, at + needle.size())) {
        ++count;
    }
    return count;
}

render::Renderer colourful() {
    return {render::Setup{.palette = palette::seed(),
                          .margin = render::default_margin,
                          .plain = false,
                          .env = nullptr},
            test::clock()};
}

std::string context_json(double pct) {
    return std::format(R"({{"context_window_size":200000,"used_percentage":{}}})", pct);
}

// The context segment with no bar, so its one colour is the printed number's.
std::string context(double pct) {
    const payload::Document doc(context_json(pct));
    return colourful().context_segment(doc.root(), 0);
}

// rampAt samples the ramp at pct through the printed NUMBER rather than through
// a bar cell. A cell is coloured for the percentage IT stands for, so the one
// cell of a one-cell bar is always 100% whatever the bar's own reading; the
// number is coloured from the real percentage, which is the thing under test.
Colour ramp_at(double pct) { return first_fg(context(pct)); }

// A limit segment for a window with this much spent, resetting this many
// seconds from the clock, reading on and compact off.
struct Spend {
    double pct;
    std::int64_t resets_in;
    int span = five_hours;
};

std::string limit(Spend spend) {
    const payload::Document doc(
        std::format(R"({{"used_percentage":{},"resets_at":{}}})",
                    spend.pct,
                    test::at(spend.resets_in)));
    return colourful().limit_segment("w", doc.root(), spend.span, false, true);
}

// The Go suite's isolate: a home that holds nothing, and config and state
// directories of scratch, so a render reaches no real file.
class Isolated {
   public:
    Isolated() {
        env_.home = "/home/me";
        env_.xdg_config_home = config_.path();
        env_.xdg_state_home = state_.path();
    }

    [[nodiscard]] std::vector<std::string> build(std::string_view json,
                                                 int width) const {
        const payload::Document doc(json);
        const render::Renderer renderer(render::Setup{.palette = palette::seed(),
                                                      .margin = render::default_margin,
                                                      .plain = false,
                                                      .env = &env_},
                                        test::clock());
        return renderer.build(doc.root(), width);
    }

   private:
    test::Scratch config_;
    test::Scratch state_;
    infobot::Environment env_;
};

bool same(palette::Rgb a, palette::Rgb b) {
    return a.r == b.r && a.g == b.g && a.b == b.b;
}

bool same(const palette::Palette& a, const palette::Palette& b) {
    bool stops = true;
    for (std::size_t i = 0; i < a.pace.size(); ++i) {
        stops = stops && a.pace[i].at == b.pace[i].at &&
                same(a.pace[i].colour, b.pace[i].colour);
    }
    return stops && same(a.green, b.green) && same(a.yellow, b.yellow) &&
           same(a.red, b.red) && same(a.backdrop, b.backdrop) &&
           same(a.alarm_fg, b.alarm_fg) && same(a.alarm_bg, b.alarm_bg) &&
           a.path == b.path && a.empty == b.empty && a.separator == b.separator &&
           a.dim == b.dim;
}

std::string palette_file(const std::string& colours) {
    return std::format(R"({{"taken":"g0bl1n","source":"theme","colours":{}}})",
                       colours);
}

}  // namespace

// COVERS: FR-6.1 | property
//
// Two straight lines, green to yellow from 0 to 75 and yellow to red from 75 to
// 90, so the colour moves fastest exactly where a glance needs to tell 80 from
// 88.
TEST_CASE("the ramp is two straight lines") {
    constexpr double pivot = 75;
    constexpr double below_alarm = 89.999;
    constexpr double first_leg_middle = 37.5;
    constexpr double second_leg_middle = 82.5;
    constexpr Colour yellow = {255, 212, 38};
    constexpr Colour red = {255, 46, 110};
    CHECK(ramp_at(0) == green_rgb);
    CHECK(ramp_at(pivot) == yellow);
    // Red arrives at the alarm boundary, not before it.
    CHECK(ramp_at(below_alarm) == red);
    // The second leg is steeper: 75 to 90 covers the same colour distance as
    // 0 to 75.
    CHECK(ramp_at(first_leg_middle) != ramp_at(second_leg_middle));
}

// COVERS: FR-6.2, FR-6.2a | property
//
// At 90 the foreground is the red the ramp arrives at and the background is the
// TERMINAL'S OWN, so the boundary has nothing to show. At 100 it is pale yellow
// on deep red. Bold is on across the whole band.
TEST_CASE("the alarm fades in rather than switching on") {
    constexpr double alarm_at = 90;
    constexpr double middle = 95;
    constexpr double top = 100;
    CHECK(context(alarm_at).contains("\033[1;38;2;255;46;110;48;2;13;10;32m"));
    CHECK(context(top).contains("\033[1;38;2;255;232;92;48;2;115;21;50m"));
    // Bold is on across the whole band and cannot fade.
    for (const double pct : {alarm_at, middle, top}) {
        CAPTURE(pct);
        CHECK(context(pct).contains("\033[1;38;2;"));
    }
}

// COVERS: FR-6.2a | edge
//
// Nothing jumps at the boundary: the last cell below the alarm and the first
// cell in it are the same foreground.
TEST_CASE("the alarm boundary has nothing to show") {
    constexpr double below_alarm = 89.999;
    constexpr double alarm_at = 90;
    CHECK(ramp_at(below_alarm) == ramp_at(alarm_at));
}

// COVERS: FR-6.2b | property
//
// The band is the top tenth of the bar, so its resolution is the cell count: 11
// cells of 103, 5 of 40, and 1 at BAR_MIN, where it is a step rather than a
// fade. The gauges are fixed at 10 cells so their band is 2.
TEST_CASE("the alarm's resolution is the cell count") {
    struct Case {
        int cells;
        std::size_t want;
    };
    constexpr double full = 100;
    for (const auto c : {
             Case{.cells = 103, .want = 11},
             Case{.cells = 40, .want = 5},
             Case{.cells = 10, .want = 2},
             Case{.cells = 8, .want = 1},
         }) {
        CAPTURE(c.cells);
        CHECK(occurrences(colourful().bar(full, c.cells, ""), "\033[1;38;2;") ==
              c.want);
    }
}

// COVERS: FR-6.3 | property
//
// Each filled cell is coloured for the percentage IT stands for, not for the
// bar's total, so the fade is a fixed property of the bar and only its length
// moves.
TEST_CASE("each cell is coloured for its own percentage") {
    constexpr double half_pct = 50;
    constexpr double full_pct = 100;
    constexpr int cells = 20;
    const auto half = cell_colours(colourful().bar(half_pct, cells, ""));
    const auto full = cell_colours(colourful().bar(full_pct, cells, ""));
    REQUIRE(half.size() >= 2);
    REQUIRE(full.size() >= 2);
    // The first cell means 5% in both, so it is the same colour in both.
    CHECK(half[0] == full[0]);
}

// COVERS: FR-6.5, FR-6.6 | property
//
// One escape per RUN of cells sharing a style, and every run opens with a reset
// BEFORE the style: the alarm carries bold and a background, so a bare colour
// change after it leaves both switched on for the rest of the line.
TEST_CASE("runs share one escape and open with a reset") {
    constexpr double half = 50;
    constexpr double alarmed = 95;
    constexpr int cells = 40;
    // A tint paints every filled cell one colour, so the whole fill is ONE run
    // and the escapes are the fill, the track, and the closing reset. The fade
    // is the opposite case and gives most cells their own shade, which is what
    // it is for; the sharing is what makes the tinted gauge cheap.
    const std::string tinted = colourful().bar(half, cells, "\033[38;2;1;2;3m");
    CHECK(occurrences(tinted, "\033[38;2;1;2;3m") == 1);
    CHECK(occurrences(tinted, "\033[38;2;12;90;102m") == 1);

    // Every style change opens with a reset BEFORE the style, because the alarm
    // carries bold and a background as well as a colour and a bare colour
    // change after it leaves both switched on for the rest of the line.
    for (const auto& bar : {tinted,
                            colourful().bar(half, cells, ""),
                            colourful().bar(alarmed, cells, "")}) {
        CAPTURE(bar);
        for (const std::string glyph : {"▰", "▱"}) {
            CHECK_FALSE(bar.contains(glyph + "\033[38;2;"));
            CHECK_FALSE(bar.contains(glyph + "\033[1;38;2;"));
        }
    }
}

// COVERS: FR-6.10, FR-6.12 | property
//
// The empty cells carry an outline glyph and NO background, and the rail is
// drawn in that same colour rather than one of its own.
TEST_CASE("empty cells and the rail share one colour and no background") {
    constexpr double pct = 10;
    constexpr int cells = 20;
    constexpr int width = 120;
    const std::string bar = colourful().bar(pct, cells, "");
    CHECK(bar.contains("\033[38;2;12;90;102m▱"));
    CHECK_FALSE(bar.contains("48;2;12;90;102"));

    const Isolated isolated;
    const auto rows = isolated.build(
        R"({"context_window":{"context_window_size":200000,"used_percentage":20},)"
        R"("rate_limits":{"five_hour":{"used_percentage":30}}})",
        width);
    REQUIRE_FALSE(rows.empty());
    CHECK(rows[0].starts_with("\033[38;2;12;90;102m"));
}

// COVERS: FR-6.11 | property
TEST_CASE("the path carries the desktop accent") {
    constexpr int width = 120;
    const Isolated isolated;
    const auto rows =
        isolated.build(R"({"workspace":{"current_dir":"/home/me/x"}})", width);
    REQUIRE_FALSE(rows.empty());
    CHECK(rows[0].contains("\033[38;2;255;43;214m"));
}

// COVERS: FR-6.13 | property
//
// A word present to be scanned past is dimmed; the figures beside it are not.
TEST_CASE("only connective words are dimmed") {
    constexpr double pct = 40;
    constexpr int cells = 10;
    const payload::Document doc(context_json(pct));
    CHECK_FALSE(colourful()
                    .context_segment(doc.root(), cells)
                    .contains("\033[38;2;120;130;135m"));
}

// COVERS: FR-7.1, FR-7.2, FR-7.3 | property
//
// The gauge's LENGTH is the spend and its COLOUR is the verdict, and the verdict
// is where the window is projected to LAND. Landing exactly full is green,
// because that is the best outcome available rather than an alarm.
TEST_CASE("pace colours the gauge by where it lands") {
    constexpr double on_rate_pct = 50;
    constexpr double hot_pct = 90;
    constexpr double cold_pct = 5;
    constexpr std::int64_t halfway = five_hours / 2;
    // Half the window elapsed, half of it spent: lands exactly full.
    const std::string on_rate = limit({.pct = on_rate_pct, .resets_in = halfway});
    // Same elapsed, far more spent: empties early.
    const std::string hot = limit({.pct = hot_pct, .resets_in = halfway});
    // Same elapsed, barely touched: goes unspent.
    const std::string cold = limit({.pct = cold_pct, .resets_in = halfway});

    const Colour green = first_fg(on_rate);
    const Colour warm = first_fg(hot);
    const Colour blue = first_fg(cold);
    // Named rather than negated inline, so the invariant reads forwards and the
    // check reads as its absence.
    const bool diverges = warm[0] > green[0] && green[0] > blue[0];
    CAPTURE(hot);
    CAPTURE(on_rate);
    CAPTURE(cold);
    CHECK(diverges);
    // The LENGTH still says the spend, whatever the colour says.
    constexpr std::size_t spent_cells = 9;
    CHECK(occurrences(hot, "▰") == spent_cells);
}

// COVERS: FR-7.4, FR-7.5, FR-7.6 | property
//
// The verdict fades in against GREEN rather than switching on, and the fade is
// a fraction of the window, so the seven day window matures at the same point
// in its own life instead of after an afternoon.
TEST_CASE("the verdict fades in against green") {
    constexpr double early_pct = 20;
    constexpr double late_pct = 90;
    constexpr std::int64_t five_minutes = 300;
    // The same overspend, judged early and late in the window.
    const Colour early =
        first_fg(limit({.pct = early_pct, .resets_in = five_hours - five_minutes}));
    const Colour late = first_fg(limit({.pct = late_pct, .resets_in = five_minutes}));

    // Early is close to green because almost nothing can be said yet.
    CHECK(early == green_rgb);
    CHECK(late[0] > early[0]);

    // A fraction, not a duration: the same position in each window judges the
    // same overspend identically.
    constexpr double overspent = 80;
    const Colour five_hour =
        first_fg(limit({.pct = overspent, .resets_in = five_hours / 2}));
    const Colour seven_day = first_fg(
        limit({.pct = overspent, .resets_in = seven_days / 2, .span = seven_days}));
    CHECK(five_hour == seven_day);
}

// COVERS: FR-7.7 | edge
//
// Beyond the ends the colour clamps: once it will not last, by how much stops
// changing what to do about it.
TEST_CASE("pace clamps beyond the ends of the scale") {
    constexpr double bad_pct = 200;
    constexpr double worse_pct = 400;
    constexpr std::int64_t a_minute = 60;
    // Both project far past the top stop of 150, at the same window position,
    // so both clamp to the same red.
    CHECK(first_fg(limit({.pct = bad_pct, .resets_in = a_minute})) ==
          first_fg(limit({.pct = worse_pct, .resets_in = a_minute})));
}

// COVERS: FR-7.8 | negative
//
// With no reset time there is no window position, so the gauge falls back to
// meaning what the context meter's colour means.
TEST_CASE("a window with no reset falls back to the consumption ramp") {
    constexpr double pct = 50;
    const payload::Document doc(R"({"used_percentage":50})");
    const Colour no_reset =
        first_fg(colourful().limit_segment("w", doc.root(), five_hours, false, true));
    CHECK(no_reset == ramp_at(pct));
}

// COVERS: FR-7.11 | edge
//
// The pace arithmetic cannot divide by nothing and cannot run backwards. A
// reset further out than the window is long reads as the start, and a
// projection made a moment in is large rather than infinite.
TEST_CASE("the pace arithmetic survives its edges") {
    struct Case {
        const char* name;
        std::int64_t resets_in;
    };
    constexpr double pct = 50;
    constexpr int channel_max = 255;
    constexpr std::int64_t nine_hours = 32400;
    for (const auto c : {
             Case{.name = "a moment into the window", .resets_in = five_hours - 1},
             Case{.name = "a reset further out than the window is long",
                  .resets_in = nine_hours},
             Case{.name = "a reset one second away", .resets_in = 1},
         }) {
        CAPTURE(c.name);
        const std::string got = limit({.pct = pct, .resets_in = c.resets_in});
        REQUIRE_FALSE(got.empty());
        for (const int channel : first_fg(got)) {
            CHECK(channel >= 0);
            CHECK(channel <= channel_max);
        }
    }
}

// COVERS: FR-7.10 | property
TEST_CASE("the window lengths come from the field names") {
    constexpr int width = 200;
    const Isolated isolated;
    const auto rows = isolated.build(
        std::format(R"({{"rate_limits":{{)"
                    R"("five_hour":{{"used_percentage":50,"resets_at":{}}},)"
                    R"("seven_day":{{"used_percentage":50,"resets_at":{}}}}}}})",
                    test::at(five_hours / 2),
                    test::at(seven_days / 2)),
        width);
    REQUIRE_FALSE(rows.empty());
    // Both are at the same point in their own window, so both read the same
    // verdict despite the durations differing by a factor of thirty-three.
    const std::string last =
        std::regex_replace(rows.back(), std::regex("\x1b\\[[0-9;]*m"), "");
    CAPTURE(last);
    CHECK(occurrences(last, "📅") == 1);
    CHECK(occurrences(last, "⏳") == 1);
}

// COVERS: FR-6.11 | negative
//
// The colours follow the desktop's palette file, and a machine that has not
// themed one still matches the terminals: no path, a missing file, and a file
// that is not a palette are each the D1C3 Goblin seed rather than a failure,
// because a status line that fails shows nothing at all.
TEST_CASE("a missing or malformed palette file is the seed") {
    const test::Scratch dir;
    const palette::Palette seed = palette::seed();
    CHECK(same(palette::load(""), seed));
    CHECK(same(palette::load(dir.file("absent/palette.json")), seed));
    for (const std::string& body : {
             std::string("not json"),
             std::string("[1,2]"),
             std::string("null"),
             std::string(R"({"taken":"x"})"),
             palette_file("{}"),
             palette_file("null"),
             palette_file(R"("#FF0000")"),
             // One value of the wrong type anywhere rejects the whole file, as
             // Go's Unmarshal does, even beside a good entry.
             palette_file(R"({"green":"#FF0000","red":7})"),
             std::string(R"({"taken":5,"colours":{"green":"#FF0000"}})"),
         }) {
        CAPTURE(body);
        CHECK(same(palette::load(dir.write("palette.json", body)), seed));
    }
}

// COVERS: FR-6.11 | positive
//
// Every named colour is read from the file. The four emitted directly become
// escapes, and the pace stops after the first two follow the ramp colours
// rather than being named again.
TEST_CASE("a palette file overlays every named colour") {
    const test::Scratch dir;
    const std::string path = dir.write(
        "palette.json",
        palette_file(
            R"({"green":"#010203","yellow":"#040506","red":"#070809",)"
            R"("backdrop":"#0A0B0C","alarm_fg":"#0D0E0F","alarm_bg":"#101112",)"
            R"("pace_low":"#131415","pace_under":"#161718",)"
            R"("path":"#191A1B","empty":"#1C1D1E","separator":"#1F2021",)"
            R"("dim":"#222324","unknown":"#FFFFFF"})"));
    const palette::Palette got = palette::load(path);
    const auto rgb = [](int r, int g, int b) {
        return palette::Rgb{.r = r, .g = g, .b = b};
    };
    CHECK(same(got.green, rgb(1, 2, 3)));
    CHECK(same(got.yellow, rgb(4, 5, 6)));
    CHECK(same(got.red, rgb(7, 8, 9)));
    CHECK(same(got.backdrop, rgb(10, 11, 12)));
    CHECK(same(got.alarm_fg, rgb(13, 14, 15)));
    CHECK(same(got.alarm_bg, rgb(16, 17, 18)));
    CHECK(same(got.pace[0].colour, rgb(19, 20, 21)));
    CHECK(same(got.pace[1].colour, rgb(22, 23, 24)));
    CHECK(same(got.pace[2].colour, got.green));
    CHECK(same(got.pace[3].colour, got.yellow));
    CHECK(same(got.pace[4].colour, got.red));
    CHECK(got.path == "\033[38;2;25;26;27m");
    CHECK(got.empty == "\033[38;2;28;29;30m");
    CHECK(got.separator == "\033[38;2;31;32;33m");
    CHECK(got.dim == "\033[38;2;34;35;36m");
}

// COVERS: FR-6.11 | edge
//
// A bad entry is rejected on its own rather than poisoning the file: one
// mistyped colour keeps its seed value and every other colour still loads.
TEST_CASE("one bad entry keeps its seed while the rest load") {
    const test::Scratch dir;
    const palette::Palette seed = palette::seed();
    for (const std::string_view bad : {"FF0000",
                                       "xFF0000",
                                       "#FF00",
                                       "#FF000000",
                                       "#GG0000",
                                       "#11GG33",
                                       "#1122GG",
                                       "#0x1122",
                                       "#1_2233",
                                       ""}) {
        CAPTURE(bad);
        const palette::Palette got = palette::load(dir.write(
            "palette.json",
            palette_file(std::format(
                R"({{"green":"{}","path":"{}","red":"#FFFFFF"}})", bad, bad))));
        CHECK(same(got.green, seed.green));
        CHECK(got.path == seed.path);
        CHECK(same(got.red, palette::Rgb{.r = 255, .g = 255, .b = 255}));
        CHECK(same(got.pace[4].colour, got.red));
    }
}

// COVERS: FR-6.11 | positive
//
// Hex digits are read in either case, the way the palette the desktop uses is
// written.
TEST_CASE("hex is read in either case") {
    const palette::Parsed parsed = palette::parse_hex("#AbCdEf");
    CHECK(parsed.ok);
    CHECK(same(parsed.colour, palette::Rgb{.r = 171, .g = 205, .b = 239}));
    const palette::Parsed bad = palette::parse_hex("#1g2233");
    CHECK_FALSE(bad.ok);
    CHECK(same(bad.colour, palette::Rgb{}));
}

// COVERS: FR-6.11 | edge
//
// Go reads each channel with Sscanf's %02x, which takes a sign and skips a
// space inside its two columns. A colour Go accepts is accepted here, so one
// palette file themes both builds alike. Measured against Go 1.27.1 with
// .ephemera/cpp/agent-palette/hexprobe.
TEST_CASE("hex accepts what Go's scanner accepts") {
    struct Case {
        std::string_view text;
        palette::Rgb want;
    };
    for (const auto& c : {
             Case{.text = "#-1-1-1", .want = {.r = -1, .g = -1, .b = -1}},
             Case{.text = "#+1+1+1", .want = {.r = 1, .g = 1, .b = 1}},
             Case{.text = "# 1 1 1", .want = {.r = 1, .g = 1, .b = 1}},
         }) {
        CAPTURE(c.text);
        const palette::Parsed parsed = palette::parse_hex(c.text);
        CHECK(parsed.ok);
        CHECK(same(parsed.colour, c.want));
    }
}

// COVERS: FR-6.3 | property
//
// A mix holds its fraction between the two ends and rounds each channel ties to
// even, the rule the bar's cell colours depend on.
TEST_CASE("mix clamps its fraction and rounds ties to even") {
    constexpr palette::Rgb from{.r = 0, .g = 10, .b = 255};
    constexpr palette::Rgb to{.r = 1, .g = 13, .b = 0};
    constexpr double beyond = 1.5;
    CHECK(same(palette::mix(from, to, -1), from));
    CHECK(same(palette::mix(from, to, beyond), to));
    // 0.5, 11.5 and 127.5 break to 0, 12 and 128.
    constexpr double halfway = 0.5;
    CHECK(
        same(palette::mix(from, to, halfway), palette::Rgb{.r = 0, .g = 12, .b = 128}));
}
