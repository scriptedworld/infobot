#include <doctest/doctest.h>

#include <chrono>
#include <cstdint>
#include <format>
#include <initializer_list>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

#include "environment.hpp"
#include "palette.hpp"
#include "payload.hpp"
#include "render.hpp"
#include "support.hpp"
#include "width.hpp"

namespace palette = infobot::palette;
namespace payload = infobot::payload;
namespace render = infobot::render;
namespace test = infobot::test;

namespace {

constexpr int five_hours = 5 * 3600;
constexpr int wide = 200;

render::Renderer renderer(bool plain, render::Instant now = test::clock()) {
    return {render::Setup{.palette = palette::seed(),
                          .margin = render::default_margin,
                          .plain = plain,
                          .env = nullptr},
            now};
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

    [[nodiscard]] std::vector<std::string> build(
        std::string_view json, bool plain, render::Instant now = test::clock()) const {
        const payload::Document doc(json);
        const render::Renderer at(render::Setup{.palette = palette::seed(),
                                                .margin = render::default_margin,
                                                .plain = plain,
                                                .env = &env_},
                                  now);
        return at.build(doc.root(), wide);
    }

   private:
    test::Scratch config_;
    test::Scratch state_;
    infobot::Environment env_;
};

std::string joined(const std::vector<std::string>& rows) {
    std::string out;
    for (const auto& row : rows) {
        if (!out.empty()) {
            out.push_back('\n');
        }
        out += row;
    }
    return out;
}

// A payload carrying every field the status line reads.
std::string full() {
    constexpr std::int64_t an_hour = 3600;
    constexpr std::int64_t a_day = 86400;
    return std::format(
        R"({{"session_id":"a5e58a4d-2a4e-4774-aa6c-1e7745721df6",)"
        R"("model":{{"display_name":"Opus 5","id":"claude-opus-5"}},)"
        R"("effort":{{"level":"high"}},)"
        R"("workspace":{{"current_dir":"/home/me/.projects/demo/bin",)"
        R"("project_dir":"/home/me/.projects/demo"}},)"
        R"("context_window":{{"context_window_size":200000,"used_percentage":48}},)"
        R"("rate_limits":{{)"
        R"("five_hour":{{"used_percentage":34,"resets_at":{}}},)"
        R"("seven_day":{{"used_percentage":62,"resets_at":{}}}}}}})",
        test::at(an_hour),
        test::at(a_day));
}

}  // namespace

// COVERS: FR-2.1 | property
//
// resets_at is a NUMERIC EPOCH rather than a timestamp string, which is what
// the running Claude Code sends. A string is not a timestamp in another
// spelling, it is absent data, and it costs its own countdown and nothing else.
TEST_CASE("resets_at is a numeric epoch") {
    constexpr std::int64_t an_hour = 3600;
    const payload::Document numeric(
        std::format(R"({{"used_percentage":34,"resets_at":{}}})", test::at(an_hour)));
    CHECK(renderer(true)
              .limit_segment("5hr", numeric.root(), five_hours, false, true)
              .contains("1h00m"));

    for (const std::string_view spelled :
         {R"("2026-08-28T12:00:00Z")", R"("4102444800")", "true", "null"}) {
        CAPTURE(spelled);
        const payload::Document doc(
            std::format(R"({{"used_percentage":34,"resets_at":{}}})", spelled));
        const std::string got =
            renderer(true).limit_segment("5hr", doc.root(), five_hours, false, true);
        REQUIRE_FALSE(got.empty());
        CHECK(got.substr(std::string_view("5hr").size()).find_first_of("hm") ==
              std::string::npos);
    }
}

// COVERS: FR-2.3 | property
//
// A percentage the status line prints is computed by the SAME formula as the
// percentage the payload supplies, so the two halves of a segment agree. Where
// the payload states one, that is the one printed, rather than a second figure
// derived from the counts.
TEST_CASE("the printed percentage is the payload's own") {
    // Counts that would give 25% if recomputed, beside a stated 48%.
    const payload::Document doc(R"({"context_window_size":200000,"used_percentage":48,)"
                                R"("current_usage":{"input_tokens":50000}})");
    const std::string got = renderer(true).context_segment(doc.root(), 0);
    CAPTURE(got);
    CHECK(got.contains("(48% consumed)"));
    CHECK_FALSE(got.contains("25%"));
}

// COVERS: FR-2.8 | property
TEST_CASE("the model is display name then id with effort appended") {
    struct Case {
        const char* name;
        const char* data;
        std::string_view want;
    };
    const Isolated isolated;
    for (const auto& c : {
             Case{.name = "display name wins",
                  .data = R"({"model":{"display_name":"Opus 5","id":"claude-opus-5"}})",
                  .want = "Opus 5"},
             Case{.name = "id is the fallback",
                  .data = R"({"model":{"id":"claude-opus-5"}})",
                  .want = "claude-opus-5"},
             Case{
                 .name = "effort is appended",
                 .data =
                     R"({"model":{"display_name":"Opus 5"},"effort":{"level":"high"}})",
                 .want = "Opus 5 - high"},
             Case{.name = "no effort, no suffix",
                  .data = R"({"model":{"display_name":"Opus 5"}})",
                  .want = "Opus 5"},
         }) {
        CAPTURE(c.name);
        const auto got = isolated.build(c.data, true);
        REQUIRE_FALSE(got.empty());
        CAPTURE(got[0]);
        CHECK(got[0].contains(c.want));
        if (c.want == "Opus 5") {
            CHECK_FALSE(got[0].contains("Opus 5 -"));
        }
    }
}

// COVERS: FR-2.12 | property
//
// The project root is MARKED as a root rather than left to read as a second
// path, and the marker sits OUTSIDE the colour the path carries, so what is
// tinted is the path and not the annotation.
TEST_CASE("the project root marker sits outside the colour") {
    const Isolated isolated;
    const auto got =
        isolated.build(R"({"workspace":{"current_dir":"/home/me/.projects/demo/bin",)"
                       R"("project_dir":"/home/me/.projects/demo"}})",
                       false);
    REQUIRE_FALSE(got.empty());
    CAPTURE(got[0]);
    CHECK(got[0].contains("⌂ \033[38;2;255;43;214m"));
}

// COVERS: FR-3.1 | property
//
// Colour is 24-bit. Every escape carries three channels rather than a palette
// index, so the ramp is continuous instead of stepped.
TEST_CASE("every colour is twenty-four bit") {
    const Isolated isolated;
    const std::string text = joined(isolated.build(full(), false));
    const std::regex ansi("\x1b\\[[0-9;]*m");
    for (auto it = std::sregex_iterator(text.begin(), text.end(), ansi);
         it != std::sregex_iterator();
         ++it) {
        const std::string code = it->str();
        if (code == palette::reset) {
            continue;
        }
        CAPTURE(code);
        CHECK((code.contains("38;2;") || code.contains("48;2;")));
        // A 256-palette escape is 38;5;N, which is what Claude Code falls back
        // to under $TMUX when the environment is not set.
        CHECK_FALSE(code.contains("38;5;"));
        CHECK_FALSE(code.contains("48;5;"));
    }
}

// COVERS: FR-3.2 | property
//
// Refresh is time-based as well as event-based, which is Claude Code's
// `refreshInterval` and lives in silo's settings.json. INFOBOT'S HALF of that
// is being safe to call on a timer: a render with no new data must cost nothing
// and change nothing, or a periodic refresh accumulates state or drifts.
TEST_CASE("repeated renders are stable") {
    constexpr int repeats = 5;
    const Isolated isolated;
    const std::string data = full();
    const std::string first = joined(isolated.build(data, true));
    for (int i = 0; i < repeats; ++i) {
        CAPTURE(i);
        REQUIRE(joined(isolated.build(data, true)) == first);
    }
    // And the countdown moves with the clock rather than with the call count,
    // which is what makes a timed refresh worth doing.
    constexpr std::chrono::minutes half_an_hour{30};
    const std::string later =
        joined(isolated.build(data, true, test::clock() + half_an_hour));
    CHECK(later != first);
}

// COVERS: FR-3.10 | property
//
// Ambiguous-width characters are counted as ONE column, which is what kitty
// draws them as. A terminal treating ambiguous as wide would draw every bar at
// twice its measured width.
TEST_CASE("ambiguous width counts as one") {
    for (const std::string_view glyph : {
             "▰",
             "▱",  // the bar's own glyphs
             "─",
             "╭",
             "├",
             "╰",
             "╵",  // the rail
             // Written as an escape rather than as the glyph: it is invisible in
             // most editors and a raw copy of it is easily lost, which is how this
             // line first asserted the width of an empty string.
             "\uE0B1",  // the powerline separator, private use
             "⌂",    // the project root marker
         }) {
        CAPTURE(glyph);
        CHECK(infobot::width::visible(glyph) == 1);
    }
    // Against a genuinely wide glyph, so this is not asserting that everything
    // is one column.
    CHECK(infobot::width::visible("🧠") == 2);
}
