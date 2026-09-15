#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <format>
#include <initializer_list>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "environment.hpp"
#include "palette.hpp"
#include "payload.hpp"
#include "render.hpp"
#include "support.hpp"

namespace render = infobot::render;
namespace palette = infobot::palette;
namespace payload = infobot::payload;
namespace test = infobot::test;

namespace {

constexpr std::int64_t hour = 3600;
constexpr std::int64_t day = 86400;
constexpr std::string_view cost_session = "cost-0000-4000-8000-000000000000";

// The five hour window at 34%, alone.
std::string one_window() {
    return std::format(R"({{"five_hour":{{"used_percentage":34,"resets_at":{}}}}})",
                       test::at(hour));
}

// Both windows, so the meter row is wide enough that the cost has to compete
// for the slack rather than always fitting.
std::string both_windows() {
    return std::format(R"({{"five_hour":{{"used_percentage":34,"resets_at":{}}},)"
                       R"("seven_day":{{"used_percentage":62,"resets_at":{}}}}})",
                       test::at(hour),
                       test::at(day));
}

// The session with a model, the context at 20% and the given rate limits.
std::string with_limits(std::string_view limits) {
    return std::format(
        R"({{"session_id":"{}","model":{{"display_name":"Opus 5"}},)"
        R"("context_window":{{"context_window_size":200000,"used_percentage":20}},)"
        R"("rate_limits":{}}})",
        cost_session,
        limits);
}

std::string usage_line(std::string_view fields) {
    return std::format(R"({{"message":{{"model":"claude-opus-5","usage":{{{}}}}}}})"
                       "\n",
                       fields);
}

// priced sets up a home whose transcripts and rate table are known, so the
// money on the row is arithmetic rather than whatever this machine happens to
// hold.
//
// usage counts are chosen to make the figures round: 10M input at $5/M is $50,
// and 100M cache reads at a tenth of $5/M is another $50.
class Priced {
   public:
    explicit Priced(std::string_view records) {
        env_.home = home_.path();
        env_.xdg_config_home = config_.path();
        env_.xdg_state_home = state_.path();
        env_.plain = true;
        (void)home_.write(std::format(".claude/projects/-p/{}.jsonl", cost_session),
                          records);
    }

    [[nodiscard]] std::vector<std::string> build(std::string_view json,
                                                 int columns) const {
        const payload::Document doc(json);
        return renderer().build(doc.root(), columns);
    }

    [[nodiscard]] std::string joined(std::string_view json, int columns) const {
        std::string out;
        for (const std::string& row : build(json, columns)) {
            out.append(row).push_back('\n');
        }
        return out;
    }

    [[nodiscard]] render::Renderer renderer() const {
        return {{.palette = palette::seed(),
                 .margin = render::default_margin,
                 .plain = true,
                 .env = &env_},
                test::clock()};
    }

   private:
    test::Scratch home_;
    test::Scratch config_;
    test::Scratch state_;
    infobot::Environment env_;
};

// form is which of the three shapes the cost took at a width.
enum class Form : std::uint8_t { dropped, shortened, whole };

Form cost_form(const Priced& fixture, std::string_view json, int columns) {
    const std::string got = fixture.joined(json, columns);
    if (got.contains("saved")) {
        return Form::whole;
    }
    if (got.contains("💵")) {
        return Form::shortened;
    }
    return Form::dropped;
}

// money pulls the dollar figure that follows the note marker.
std::string money(std::string_view row) {
    constexpr std::string_view marker = "💵 ";
    const std::size_t cut = row.find(marker);
    if (cut == std::string_view::npos) {
        return {};
    }
    const std::string_view rest = row.substr(cut + marker.size());
    return std::string(rest.substr(0, rest.find_first_of(" \n")));
}

constexpr int widest = 240;
constexpr int narrowest = 40;

}  // namespace

// COVERS: FR-8.1 | property
//
// The figure is a COUNTERFACTUAL and not a bill: what the same tokens would
// have cost through the API at list rates. 10M input tokens at Opus 5's $5 per
// million is $50, and that is the number, not a share of a subscription.
TEST_CASE("the cost is the list rate counterfactual") {
    constexpr int columns = 200;
    const Priced fixture(usage_line(R"("input_tokens":10000000)"));
    const std::string got = fixture.joined(with_limits(one_window()), columns);
    CAPTURE(got);
    CHECK(got.contains("💵 $50"));
}

// COVERS: FR-8.18 | negative
//
// The saving is shown only when there IS one. A session that wrote cache blocks
// and never read them back spent MORE than it would have with no caching, so
// the figure is negative and the clause is dropped rather than printed as a
// loss. The total is still shown.
TEST_CASE("the saving is dropped when it is negative") {
    constexpr int columns = 220;
    {
        // A 1h write costs double the plain input rate and is never read back.
        const Priced loss(usage_line(R"("ephemeral_1h_input_tokens":10000000)"));
        const std::string got = loss.joined(with_limits(one_window()), columns);
        CAPTURE(got);
        CHECK_FALSE(got.contains("saved"));
        CHECK(got.contains("💵"));
    }
    {
        // And it IS shown when reads make it positive.
        const Priced gain(usage_line(R"("cache_read_input_tokens":100000000)"));
        const std::string got = gain.joined(with_limits(one_window()), columns);
        CAPTURE(got);
        CHECK(got.contains("🎯"));
        CHECK(got.contains("saved"));
    }
}

// COVERS: FR-5.8 | property
//
// The cost goes on the METER row rather than the identity row, because the
// identity row has already given its slack to the context bar. With no meter
// row there is nowhere for it that is not somewhere else's space.
TEST_CASE("the cost rides the meter row and is dropped without one") {
    constexpr int columns = 220;
    const Priced fixture(usage_line(R"("input_tokens":10000000)"));
    const auto got = fixture.build(with_limits(one_window()), columns);
    REQUIRE(got.size() == 2);
    CHECK_FALSE(got[0].contains("💵"));
    CHECK(got[1].contains("💵"));

    // No rate limits, so no meter row, so nowhere for it.
    const auto lone = fixture.build(
        std::format(R"({{"session_id":"{}","model":{{"display_name":"Opus 5"}}}})",
                    cost_session),
        columns);
    REQUIRE(lone.size() == 1);
    CHECK_FALSE(lone[0].contains("💵"));
}

// COVERS: FR-5.7 | property
//
// It gives up DETAIL before it gives up the row, in order: the full form, then
// the total alone, then nothing. The assertion is the ordering across every
// width rather than three magic numbers, because which width crosses which
// boundary depends on how wide the meter row already is.
TEST_CASE("the cost shortens then drops as the row narrows") {
    const Priced fixture(usage_line(R"("cache_read_input_tokens":100000000)"));
    const std::string data = with_limits(both_windows());

    std::map<Form, int> seen;
    Form previous = Form::whole;
    for (int columns = widest; columns >= narrowest; --columns) {
        const Form got = cost_form(fixture, data, columns);
        seen.try_emplace(got, columns);
        // It gives up detail before it gives up the row, and never recovers a
        // form it has already surrendered as the pane keeps shrinking.
        CAPTURE(columns);
        CHECK(got <= previous);
        previous = got;
    }

    for (const Form shape : {Form::whole, Form::shortened, Form::dropped}) {
        CAPTURE(static_cast<int>(shape));
        CHECK(seen.contains(shape));
    }
    CHECK(seen[Form::whole] > seen[Form::shortened]);
    CHECK(seen[Form::shortened] > seen[Form::dropped]);
}

// COVERS: FR-5.7 | edge
TEST_CASE("the cost keeps three columns of clear space") {
    constexpr int from = 120;
    constexpr int step = 4;
    constexpr std::size_t gutter = 3;
    const Priced fixture(usage_line(R"("input_tokens":10000000)"));
    const std::string data = with_limits(one_window());
    for (int columns = from; columns <= widest; columns += step) {
        const auto got = fixture.build(data, columns);
        REQUIRE_FALSE(got.empty());
        const std::string_view meters = got.back();
        const std::size_t cut = meters.find("💵");
        if (cut == std::string_view::npos) {
            continue;
        }
        const std::string_view before = meters.substr(0, cut);
        const std::size_t kept = before.find_last_not_of(' ') + 1;
        CAPTURE(columns);
        CHECK(before.size() - kept >= gutter);
    }
}

// COVERS: FR-5.9 | property
//
// Both cost forms come from ONE reading of the transcripts. Deciding which of
// two strings fits must not double the only expensive thing on the row, and the
// consequence a test can see is that the two forms carry the SAME total: a
// second read would advance the offsets and the shortened form would price only
// what the first read had not already counted.
TEST_CASE("both cost forms carry the same total") {
    const Priced fixture(usage_line(R"("cache_read_input_tokens":100000000)"));
    const std::string data = with_limits(both_windows());

    std::string wide;
    std::string narrow;
    for (int columns = widest; columns >= narrowest; --columns) {
        const std::string got = fixture.joined(data, columns);
        if (wide.empty() && got.contains("saved")) {
            wide = got;
        }
        if (narrow.empty() && got.contains("💵") && !got.contains("saved")) {
            narrow = got;
        }
    }
    REQUIRE_FALSE(wide.empty());
    REQUIRE_FALSE(narrow.empty());
    CAPTURE(wide);
    CAPTURE(narrow);
    CHECK_FALSE(money(wide).empty());
    CHECK(money(wide) == money(narrow));
}

// COVERS: FR-8.12 | property
//
// A model the rate table has no rate for is left out and the figure carries a
// trailing plus, because it is a floor rather than a total.
TEST_CASE("an unpriced model leaves the total a floor") {
    constexpr int columns = 220;
    const Priced fixture(usage_line(R"("input_tokens":10000000)") +
                         R"({"message":{"model":"some-model-nobody-priced",)"
                         R"("usage":{"input_tokens":10000000}}})"
                         "\n");
    const std::string got = fixture.joined(with_limits(one_window()), columns);
    CAPTURE(got);
    CHECK(got.contains("💵 $50.00+"));
}

// COVERS: FR-3.3, FR-5.7 | edge
//
// With no width to align against, the cost JOINS the meter row rather than
// being dropped. Pushing it right needs a right edge to push it to, so it is
// carried inline, separated the way the meters are, and in its full form.
TEST_CASE("with the width unknown the cost joins the meter row") {
    const Priced fixture(usage_line(R"("cache_read_input_tokens":100000000)"));
    const auto got = fixture.build(with_limits(one_window()), 0);
    REQUIRE(got.size() == 2);
    CAPTURE(got[1]);
    CHECK(got[1].contains(" \uE0B1 💵 "));
    CHECK(got[1].contains("saved"));
    CHECK_FALSE(got[1].contains("   "));
}

// COVERS: FR-5.8 | edge
//
// A meter row with nothing priceable beside it carries no cost at any width:
// the fixture's transcript names a different session.
TEST_CASE("a session with no transcript carries no cost") {
    constexpr int columns = 220;
    const Priced fixture(usage_line(R"("input_tokens":10000000)"));
    const std::string other =
        std::format(R"({{"session_id":"elsewhere","rate_limits":{}}})", one_window());
    for (const int width : {0, columns}) {
        CAPTURE(width);
        const auto got = fixture.build(other, width);
        REQUIRE(got.size() == 2);
        CHECK_FALSE(got[1].contains("💵"));
    }
}
