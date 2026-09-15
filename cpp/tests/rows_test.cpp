#include <doctest/doctest.h>
#include <unistd.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "environment.hpp"
#include "files.hpp"
#include "palette.hpp"
#include "payload.hpp"
#include "render.hpp"
#include "support.hpp"
#include "width.hpp"

namespace render = infobot::render;
namespace palette = infobot::palette;
namespace payload = infobot::payload;
namespace test = infobot::test;
namespace width = infobot::width;

namespace {

constexpr std::int64_t hour = 3600;
constexpr std::int64_t day = 86400;

// Powerline's thin separator, which the renderer draws between parts.
constexpr std::string_view powerline = "\uE0B1";

// Both rate limit windows, the five hour at 34% and the seven day at 62%.
std::string limits() {
    return std::format(R"({{"five_hour":{{"used_percentage":34,"resets_at":{}}},)"
                       R"("seven_day":{{"used_percentage":62,"resets_at":{}}}}})",
                       test::at(hour),
                       test::at(day));
}

// full is a payload with everything present, for tests about where things go
// rather than about whether they appear. workspace replaces the paths.
std::string full(std::string_view workspace) {
    return std::format(
        R"({{"session_id":"a5e58a4d-2a4e-4774-aa6c-1e7745721df6",)"
        R"("model":{{"display_name":"Opus 5","id":"claude-opus-5"}},)"
        R"("effort":{{"level":"high"}},"workspace":{},)"
        R"("context_window":{{"context_window_size":200000,"used_percentage":48}},)"
        R"("rate_limits":{}}})",
        workspace,
        limits());
}

std::string full() {
    return full(
        R"({"current_dir":"/home/me/.projects/demo/bin","project_dir":"/home/me/.projects/demo"})");
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

// NO_COLOR, with config and state in scratch and the host variables empty, so
// the width is whatever the caller passes or whatever the terminal says.
infobot::Environment environment(const std::string& home,
                                 const test::Scratch& config,
                                 const test::Scratch& state) {
    infobot::Environment env;
    env.home = home;
    env.xdg_config_home = config.path();
    env.xdg_state_home = state.path();
    env.plain = true;
    return env;
}

// The rows for a payload with HOME, state and config in scratch, plain, and
// paths shown relative to /home/me.
std::vector<std::string> rows(std::string_view json, int columns) {
    const test::Scratch state;
    const test::Scratch config;
    const infobot::Environment env = environment("/home/me", config, state);
    const payload::Document doc(json);
    const render::Renderer renderer({.palette = palette::seed(),
                                     .margin = render::default_margin,
                                     .plain = true,
                                     .env = &env},
                                    test::clock());
    return renderer.build(doc.root(), columns);
}

// statusline over two pipes: input written and closed, then the rows read back.
std::string run(std::string_view input, const infobot::Environment& env) {
    std::array<int, 2> in{};
    std::array<int, 2> out{};
    REQUIRE(::pipe(in.data()) == 0);
    REQUIRE(::pipe(out.data()) == 0);
    REQUIRE(infobot::files::write_all(in[1], input));
    ::close(in[1]);
    CHECK(render::statusline({.input = in[0], .output = out[1]}, env, test::clock()) ==
          0);
    ::close(out[1]);
    const std::optional<std::string> written = infobot::files::read_all(out[0]);
    ::close(in[0]);
    ::close(out[0]);
    return written.value_or("");
}

// The value of a top-level key in the state file, or empty.
std::string field(std::string_view file, const std::string& key) {
    const std::string name = std::format(R"("{}": )", key);
    for (std::size_t start = 0; start < file.size();) {
        std::size_t end = file.find('\n', start);
        if (end == std::string_view::npos) {
            end = file.size();
        }
        const std::string_view line = file.substr(start, end - start);
        if (line.starts_with(name)) {
            return std::string(line.substr(name.size()));
        }
        start = end + 1;
    }
    return {};
}

// The whole percent the row prints before "% consumed", or empty.
std::string percent_in_row(std::string_view row) {
    const std::size_t cut = row.find("% consumed");
    if (cut == std::string_view::npos) {
        return {};
    }
    const std::size_t before = row.substr(0, cut).find_last_of("( ");
    const std::size_t start = before == std::string_view::npos ? 0 : before + 1;
    return std::string(row.substr(start, cut - start));
}

}  // namespace

// COVERS: FR-1.1 | positive
//
// Reads the session JSON on standard input and writes COMPLETE rows to standard
// output. Complete is the requirement; the count is a rendering decision.
TEST_CASE("statusline writes complete rows") {
    const test::Scratch home;
    const test::Scratch state;
    const test::Scratch config;
    const infobot::Environment env = environment(home.path(), config, state);
    const std::string got =
        run(R"({"session_id":"a5e58a4d-0000-4000-8000-000000000000",)"
            R"("model":{"display_name":"Opus 5"},)"
            R"("context_window":{"context_window_size":200000,"used_percentage":48}})",
            env);
    CAPTURE(got);
    CHECK(got.ends_with('\n'));
    CHECK_FALSE(got.starts_with('\n'));
    CHECK_FALSE(got.contains("\n\n"));
    CHECK(got.contains("Opus 5"));
    CHECK(got.contains("consumed"));
}

// COVERS: FR-5.4, FR-5.13 | property
//
// The session id CLOSES the identity row, which puts the bar between two fixed
// things. Each segment opens with a fixed marker, so which meter is which is
// read from the shape rather than from the numbers.
TEST_CASE("the row shape is markers and a closing session id") {
    constexpr int columns = 200;
    const auto got = rows(full(), columns);
    REQUIRE_FALSE(got.empty());
    CHECK(got[0].ends_with("⟨a5e58a4d⟩"));
    std::string all;
    for (const std::string& row : got) {
        all += row + "\n";
    }
    for (const std::string_view marker : {"🧠", "⏳", "📅"}) {
        CAPTURE(marker);
        CHECK(all.contains(marker));
    }
}

// COVERS: FR-1.11a | property
//
// The bar and the file come through ONE measurement. Two formulas would let a
// bar reading 56% sit beside a file saying something else, and a reader has no
// way to tell which is wrong.
TEST_CASE("the bar and the file agree because they share a measurement") {
    for (const std::string_view given : {
             // A stated percentage.
             R"({"context_window_size":200000,"used_percentage":48})",
             // Counts alone, so the percentage is derived.
             R"({"context_window_size":200000,"current_usage":{"input_tokens":50000}})",
             // Both, disagreeing: the payload's percentage is the authority.
             R"({"context_window_size":200000,"used_percentage":48,)"
             R"("current_usage":{"input_tokens":10}})",
         }) {
        CAPTURE(given);
        const test::Scratch home;
        const test::Scratch state;
        const test::Scratch config;
        const infobot::Environment env = environment(home.path(), config, state);
        const std::string out = run(
            std::format(R"({{"session_id":"agree","context_window":{}}})", given), env);

        const std::string file =
            test::slurp(state.path() + "/infobot/agree.status.yaml");
        const std::string on_disk = field(file, "context_percent");
        const std::string on_screen = percent_in_row(out);
        CAPTURE(file);
        CAPTURE(out);
        CHECK_FALSE(on_disk.empty());
        CHECK_FALSE(on_screen.empty());
        // The row prints whole percent and the file keeps a decimal, so they
        // agree when the file rounds to what the row shows.
        CHECK(on_disk.starts_with(on_screen));
    }
}

// COVERS: FR-5.2 | property
//
// The context segment rides the identity row when that row can hold it with no
// bar at all, and moves to the meter row when it cannot. Truncation eats the
// end of the row, which is the session id, so relocating costs less.
TEST_CASE("the context relocates rather than overflowing") {
    constexpr int wide_columns = 200;
    constexpr int narrow_columns = 100;
    constexpr int depth = 9;
    const auto wide = rows(full(), wide_columns);
    REQUIRE_FALSE(wide.empty());
    CHECK(wide[0].contains("🧠"));

    std::string deep;
    for (int i = 0; i < depth; ++i) {
        deep += "deeply/";
    }
    const auto narrow =
        rows(full(std::format(R"({{"current_dir":"/home/me/{}leaf"}})", deep)),
             narrow_columns);
    REQUIRE(narrow.size() == 2);
    CHECK_FALSE(narrow[0].contains("🧠"));
    CHECK(narrow[1].contains("🧠"));
}

// COVERS: FR-5.3 | property
//
// The context bar takes every column the identity row has left once everything
// else is placed, so a wider pane spends all of it on the bar.
TEST_CASE("the bar takes what the row has left") {
    constexpr std::array widths = {140, 180, 220};
    constexpr int step = 40;
    // A short path, so the context stays on row one at every width tested and
    // this measures the bar's growth rather than FR-5.2's relocation.
    const std::string short_path = full(R"({"current_dir":"/home/me/x"})");

    int previous = 0;
    for (const int columns : widths) {
        CAPTURE(columns);
        const auto got = rows(short_path, columns);
        REQUIRE_FALSE(got.empty());
        REQUIRE(got[0].contains("🧠"));
        const int count = cells(got[0]);
        // Every extra column of pane becomes a cell, so the growth tracks the
        // width exactly rather than lagging it.
        if (previous != 0) {
            CHECK(count - previous == step);
        }
        previous = count;
    }
}

// COVERS: FR-5.5 | property
//
// The rate limit gauges are a FIXED ten cells rather than a share of the slack,
// so the row below does not move under them as the bar above grows.
TEST_CASE("the gauges are fixed at ten cells") {
    constexpr std::array widths = {120, 160, 200, 250};
    constexpr int two_gauges = 20;
    // No context window, so the meter row carries the two gauges and nothing
    // else and the count is theirs alone.
    const std::string windows = std::format(R"({{"rate_limits":{}}})", limits());
    for (const int columns : widths) {
        CAPTURE(columns);
        const auto got = rows(windows, columns);
        REQUIRE_FALSE(got.empty());
        CHECK(cells(got.back()) == two_gauges);
    }
}

// COVERS: FR-5.10 | property
//
// Parts within a row are divided by a separator costing exactly one column,
// whatever glyph the font provides, so a font without it shifts nothing.
TEST_CASE("the separator costs exactly one column") {
    constexpr int columns = 200;
    // The powerline glyph and the plain bar it falls back to are one column
    // each. Measured through the renderer's own width function, which is what
    // the fitting uses.
    for (const std::string_view glyph :
         {powerline, std::string_view("│"), std::string_view("|")}) {
        CAPTURE(glyph);
        CHECK(width::visible(glyph) == 1);
    }
    // And in a row, each separator adds exactly the glyph plus its two spaces.
    const auto one = rows(R"({"model":{"display_name":"Opus 5"}})", columns);
    const auto two = rows(R"({"model":{"display_name":"Opus 5"},)"
                          R"("workspace":{"current_dir":"/home/me/x"}})",
                          columns);
    REQUIRE_FALSE(one.empty());
    REQUIRE_FALSE(two.empty());
    const int added = width::visible(two[0]) - width::visible(one[0]);
    const std::string between = std::format(" {} ", powerline);
    CHECK(added == width::visible(between) + width::visible("~/x"));
}

// COVERS: FR-5.11 | property
//
// The rail is part of what a row costs. A bar sized against the parts alone
// overflows by exactly the rail, which is three columns.
TEST_CASE("the rail is counted in the row's cost") {
    constexpr int columns = 200;
    constexpr int rail_columns = 3;
    constexpr std::array<std::string_view, 2> leads = {"╭─ ", "╰─ "};
    const auto got = rows(full(), columns);
    REQUIRE(got.size() == leads.size());
    for (std::size_t i = 0; i < got.size(); ++i) {
        CAPTURE(got[i]);
        REQUIRE(got[i].starts_with(leads.at(i)));
        REQUIRE(width::visible(leads.at(i)) == rail_columns);
        // The row fits INCLUDING its rail, which is the thing a fit that
        // forgets the rail gets wrong by exactly three.
        CHECK(width::visible(got[i]) <= columns - render::default_margin);
    }
}

// COVERS: FR-1.2 | negative
//
// Neither end of the pipe failing is worth more than an empty status line: an
// input that cannot be read writes nothing, and an output that cannot be
// written stops there. Both still exit 0.
TEST_CASE("statusline exits zero when its descriptors fail") {
    constexpr int closed = -1;
    const test::Scratch home;
    const test::Scratch state;
    const test::Scratch config;
    const infobot::Environment env = environment(home.path(), config, state);

    std::array<int, 2> out{};
    REQUIRE(::pipe(out.data()) == 0);
    CHECK(render::statusline({.input = closed, .output = out[1]}, env, test::clock()) ==
          0);
    ::close(out[1]);
    CHECK(infobot::files::read_all(out[0]).value_or("unread").empty());
    ::close(out[0]);

    std::array<int, 2> in{};
    REQUIRE(::pipe(in.data()) == 0);
    REQUIRE(infobot::files::write_all(in[1], full()));
    ::close(in[1]);
    CHECK(render::statusline({.input = in[0], .output = closed}, env, test::clock()) ==
          0);
    ::close(in[0]);
}
