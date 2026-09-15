#include "render.hpp"

#include <simdjson.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <initializer_list>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "environment.hpp"
#include "files.hpp"
#include "gojson.hpp"
#include "host.hpp"
#include "num.hpp"
#include "palette.hpp"
#include "payload.hpp"
#include "pricing.hpp"
#include "state.hpp"
#include "status_file.hpp"
#include "usage.hpp"
#include "width.hpp"

namespace infobot::render {

namespace {

constexpr std::string_view brain = "🧠";
constexpr std::string_view hourglass = "⏳";
constexpr std::string_view calendar = "📅";
constexpr std::string_view money = "💵";
constexpr std::string_view bullseye = "🎯";
constexpr std::string_view root_glyph = "⌂";

// Powerline's thin separator. A plain bar is the fallback wherever the glyph is
// missing, one column either way.
constexpr std::string_view separator_glyph = "\uE0B1";

constexpr std::string_view rail_top = "╭─ ";
constexpr std::string_view rail_mid = "├─ ";
constexpr std::string_view rail_end = "╰─ ";
constexpr std::string_view rail_one = "╶─ ";

// The glyphs Claude Code draws in its own compaction meter.
constexpr std::string_view filled = "▰";
constexpr std::string_view empty = "▱";

// The width below which the context bar is dropped rather than clamped to.
constexpr int bar_min = 8;
// The rate limit gauges are a fixed ten cells rather than a share of the slack.
constexpr int window_cells = 10;
// The width at which the percentage is printed beside each gauge.
constexpr int reading_min = 80;
// Clear space between the meter row and the cost pushed to its right.
constexpr int cost_gutter = 3;
// The most a layout file may set the margin to.
constexpr std::int64_t margin_max = 40;

constexpr std::size_t session_shown = 8;

constexpr double hundred = 100;
constexpr double pivot = 75.0;
constexpr double alarm_at = 90.0;
constexpr double alarm_top = 100.0;

// How much of a window must elapse before its verdict shows in full, and the
// floor under the divisor, which is arithmetic only.
constexpr double pace_confident = 0.6;
constexpr double pace_min_elapsed = 0.01;

constexpr double nanos_per_second = 1e9;
constexpr std::int64_t seconds_per_day = 86400;
constexpr std::int64_t seconds_per_hour = 3600;
constexpr std::int64_t seconds_per_minute = 60;
constexpr double thousand = 1e3;
constexpr double million = 1e6;
// 2^63, the first double past int64's range.
constexpr double int64_bound = 9223372036854775808.0;

std::string repeat(std::string_view unit, int count) {
    std::string out;
    for (int i = 0; i < count; ++i) {
        out.append(unit);
    }
    return out;
}

// The non-empty parts with one space between, so a dropped bar takes its
// separating space with it.
std::string join(std::initializer_list<std::string_view> parts) {
    std::string out;
    for (const std::string_view part : parts) {
        if (part.empty()) {
            continue;
        }
        if (!out.empty()) {
            out.push_back(' ');
        }
        out.append(part);
    }
    return out;
}

std::string join_with(const std::vector<std::string>& parts, std::string_view between) {
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            out.append(between);
        }
        out.append(parts[i]);
    }
    return out;
}

std::string model_part(const payload::Map& data) {
    std::string_view model = data.obj("model").str("display_name");
    if (model.empty()) {
        model = data.obj("model").str("id");
    }
    if (model.empty()) {
        return {};
    }
    const std::string_view effort = data.obj("effort").str("level");
    if (!effort.empty()) {
        return std::string(model) + " - " + std::string(effort);
    }
    return std::string(model);
}

// The first eight bytes of the session id, which is what the harness shows.
std::string session_part(const payload::Map& data) {
    const std::string_view id = data.str("session_id");
    if (id.empty()) {
        return {};
    }
    return "⟨" + std::string(id.substr(0, session_shown)) + "⟩";
}

// When a window resets and how long it is.
struct Reset {
    double at = 0;
    int span = 0;
};

// How far through the window now is, when that is knowable at all.
std::optional<double> elapsed_fraction(Reset reset, double now) {
    if (reset.at == 0 || reset.span == 0) {
        return std::nullopt;
    }
    const double remaining = reset.at - now;
    if (remaining <= 0) {
        return std::nullopt;
    }
    return num::clamp(1 - (remaining / static_cast<double>(reset.span)), 0, 1);
}

}  // namespace

int load_margin(const std::string& path) {
    if (path.empty()) {
        return default_margin;
    }
    const auto raw = files::read(path);
    if (!raw) {
        return default_margin;
    }
    simdjson::dom::parser parser;
    simdjson::dom::element root;
    if (parser.parse(*raw).get(root) != simdjson::SUCCESS) {
        return default_margin;
    }
    gojson::Decode decode;
    std::optional<std::int64_t> margin;
    decode.as_struct(root, [&](simdjson::dom::object top) {
        decode.fields(
            top, "margin", [&](auto v) { decode.optional_int_into(v, margin); });
    });
    // ABSENT AND ZERO DIFFER: a file setting 0 says the host reserves nothing.
    if (!decode.ok() || !margin || *margin < 0 || *margin > margin_max) {
        return default_margin;
    }
    return static_cast<int>(*margin);
}

std::string tokens(double n) {
    if (n >= million) {
        return std::format("{:.1f}M", n / million);
    }
    if (n >= thousand) {
        return std::format("{:.0f}k", n / thousand);
    }
    return std::format("{:.0f}", n);
}

std::string home_relative(std::string_view path, std::string_view home) {
    if (path == home) {
        return "~";
    }
    if (!home.empty() && path.size() > home.size() && path.starts_with(home) &&
        path[home.size()] == '/') {
        return "~" + std::string(path.substr(home.size()));
    }
    return std::string(path);
}

Renderer::Renderer(Setup setup, Instant now) : setup_(std::move(setup)), now_(now) {}

double Renderer::seconds() const {
    return static_cast<double>(now_.time_since_epoch().count()) / nanos_per_second;
}

std::string Renderer::separator() const {
    if (setup_.plain) {
        return " " + std::string(separator_glyph) + " ";
    }
    return " " + setup_.palette.separator + std::string(separator_glyph) +
           std::string(palette::reset) + " ";
}

std::string Renderer::tinted(std::string_view text, std::string_view escape) const {
    if (setup_.plain) {
        return std::string(text);
    }
    return std::string(escape) + std::string(text) + std::string(palette::reset);
}

std::string Renderer::alarm(double pct) const {
    const double into = num::clamp((pct - alarm_at) / (alarm_top - alarm_at), 0, 1);
    const palette::Rgb front =
        palette::mix(setup_.palette.red, setup_.palette.alarm_fg, into);
    const palette::Rgb back =
        palette::mix(setup_.palette.backdrop, setup_.palette.alarm_bg, into);
    return std::format("\033[1;38;2;{};{};{};48;2;{};{};{}m",
                       front.r,
                       front.g,
                       front.b,
                       back.r,
                       back.g,
                       back.b);
}

std::string Renderer::ramp(double pct) const {
    if (pct >= alarm_at) {
        return alarm(pct);
    }
    const palette::Palette& p = setup_.palette;
    if (pct <= pivot) {
        return palette::fg(palette::mix(p.green, p.yellow, pct / pivot));
    }
    return palette::fg(palette::mix(p.yellow, p.red, (pct - pivot) / (alarm_at - pivot)));
}

std::string Renderer::colour(double pct, std::string_view text) const {
    if (text.empty() || setup_.plain) {
        return std::string(text);
    }
    return ramp(pct) + std::string(text) + std::string(palette::reset);
}

palette::Rgb Renderer::pace_rgb(double projected) const {
    const auto& stops = setup_.palette.pace;
    palette::PaceStop low = stops[0];
    for (std::size_t i = 1; i < stops.size(); ++i) {
        const palette::PaceStop& high = stops[i];
        if (projected <= high.at) {
            return palette::mix(
                low.colour, high.colour, (projected - low.at) / (high.at - low.at));
        }
        low = high;
    }
    return stops.back().colour;
}

std::string Renderer::pace_tint(double pct, double elapsed) const {
    const double projected = pct / std::max(elapsed, pace_min_elapsed);
    // Squared, so the verdict stays quiet through the middle of the window and
    // arrives late.
    double trust = std::min(1.0, elapsed / pace_confident);
    trust *= trust;
    return palette::fg(palette::mix(setup_.palette.green, pace_rgb(projected), trust));
}

std::string Renderer::bar(double pct, int cells, std::string_view tint) const {
    pct = num::clamp(pct, 0, hundred);
    if (cells <= 0) {
        return {};
    }
    const int full = num::round_int(pct / hundred * static_cast<double>(cells));
    if (setup_.plain) {
        return repeat(filled, full) + repeat(empty, cells - full);
    }
    // One escape per RUN of cells sharing a style, each opened with a reset,
    // because the alarm style carries bold and a background a bare colour
    // change would leave switched on.
    std::string out;
    std::string held;
    for (int i = 0; i < cells; ++i) {
        std::string style = setup_.palette.empty;
        std::string_view glyph = empty;
        if (i < full) {
            glyph = filled;
            style = tint.empty() ? ramp(static_cast<double>(i + 1) /
                                        static_cast<double>(cells) * hundred)
                                 : std::string(tint);
        }
        if (style != held) {
            out.append(palette::reset);
            out.append(style);
            held = std::move(style);
        }
        out.append(glyph);
    }
    out.append(palette::reset);
    return out;
}

std::vector<std::string> Renderer::rail(std::vector<std::string> rows) const {
    for (std::size_t i = 0; i < rows.size(); ++i) {
        std::string_view lead = rail_mid;
        if (rows.size() == 1) {
            lead = rail_one;
        } else if (i == 0) {
            lead = rail_top;
        } else if (i == rows.size() - 1) {
            lead = rail_end;
        }
        const std::string framed = setup_.plain ? std::string(lead)
                                                : setup_.palette.empty + std::string(lead) +
                                                      std::string(palette::reset);
        rows[i] = framed + rows[i];
    }
    return rows;
}

std::string Renderer::countdown(double resets_at) const {
    if (resets_at == 0) {
        return {};
    }
    const double left = resets_at - seconds();
    // Go converts NaN or out of range to the minimum int64 on amd64, which reads
    // as already past.
    if (std::isnan(left) || left <= -int64_bound || left >= int64_bound) {
        return {};
    }
    const auto remaining = static_cast<std::int64_t>(left);
    if (remaining <= 0) {
        return {};
    }
    const std::int64_t days = remaining / seconds_per_day;
    const std::int64_t hours = (remaining % seconds_per_day) / seconds_per_hour;
    const std::int64_t minutes = (remaining % seconds_per_hour) / seconds_per_minute;
    if (days != 0) {
        return std::format("{}d{}h", days, hours);
    }
    if (hours != 0) {
        return std::format("{}h{:02}m", hours, minutes);
    }
    return std::format("{}m", minutes);
}

std::string Renderer::context_segment(const payload::Map& context_window,
                                      int cells) const {
    const auto window = state::figures(context_window);
    if (!window) {
        return {};
    }
    const std::string counts = std::format("{}/{} ({:.0f}% consumed)",
                                           tokens(window->used),
                                           tokens(window->size),
                                           window->percent);
    return join(
        {brain, bar(window->percent, cells, ""), colour(window->percent, counts)});
}

std::string Renderer::limit_segment(std::string_view label,
                                    const payload::Map& window,
                                    int span,
                                    bool compact,
                                    bool reading) const {
    if (!window.present()) {
        return {};
    }
    const auto pct = window.num("used_percentage");
    if (!pct) {
        return {};
    }
    const double resets_at = window.count("resets_at");
    // Concern where it can be worked out, raw spend where it cannot.
    std::string tint = ramp(*pct);
    if (const auto elapsed = elapsed_fraction({.at = resets_at, .span = span}, seconds())) {
        tint = pace_tint(*pct, *elapsed);
    }
    const std::string number = tinted(std::format("@{:.0f}%", *pct), tint);
    std::string gauge = bar(*pct, window_cells, tint);
    if (reading) {
        gauge += "  " + number;
    }
    if (compact) {
        gauge = number;
    }
    return join({label, gauge, countdown(resets_at)});
}

int Renderer::row_width(const std::vector<std::string>& parts) const {
    return width::visible(join_with(parts, separator())) + width::visible(rail_top);
}

std::string Renderer::fitted(const payload::Map& context_window,
                             int width,
                             const std::vector<std::string>& others,
                             std::size_t at) const {
    std::string bare = context_segment(context_window, 0);
    if (width == 0) {
        // No bar when the width is unknown: any length is a guess the host
        // then truncates.
        return bare;
    }
    std::vector<std::string> row(others.begin(),
                                 others.begin() + static_cast<std::ptrdiff_t>(at));
    row.push_back(bare);
    row.insert(
        row.end(), others.begin() + static_cast<std::ptrdiff_t>(at), others.end());
    // The bar brings the space that separates it from the counts.
    const int spare = width - setup_.margin - row_width(row) - 1;
    if (spare < bar_min) {
        return bare;
    }
    return context_segment(context_window, spare);
}

std::vector<std::string> Renderer::path_parts(const payload::Map& data) const {
    const payload::Map workspace = data.obj("workspace");
    const std::string_view cwd = workspace.str("current_dir");
    const std::string_view project = workspace.str("project_dir");
    const std::string_view home =
        setup_.env == nullptr ? std::string_view{} : std::string_view(setup_.env->home);
    std::vector<std::string> parts;
    if (!cwd.empty()) {
        parts.push_back(tinted(home_relative(cwd, home), setup_.palette.path));
    }
    if (!project.empty() && project != cwd) {
        // The marker sits outside the colour the path carries.
        parts.push_back(std::string(root_glyph) + " " +
                        tinted(home_relative(project, home), setup_.palette.path));
    }
    return parts;
}

std::vector<std::vector<std::string>> Renderer::compose(const payload::Map& data,
                                                        int width,
                                                        bool compact) const {
    std::vector<std::string> identity;
    if (std::string model = model_part(data); !model.empty()) {
        identity.push_back(std::move(model));
    }
    for (std::string& part : path_parts(data)) {
        if (!part.empty()) {
            identity.push_back(std::move(part));
        }
    }
    std::vector<std::string> tail;
    if (std::string session = session_part(data); !session.empty()) {
        tail.push_back(std::move(session));
    }

    const payload::Map limits = data.obj("rate_limits");
    const bool reading = width >= reading_min;
    std::vector<std::string> meters;
    const std::string five = std::string(hourglass) + " 5hr";
    const std::string seven = std::string(calendar) + " 7d";
    for (const auto& [label, key, span] :
         {std::tuple{std::string_view(five), std::string_view("five_hour"), five_hour},
          std::tuple{
              std::string_view(seven), std::string_view("seven_day"), seven_day}}) {
        std::string segment =
            limit_segment(label, limits.obj(key), span, compact, reading);
        if (!segment.empty()) {
            meters.push_back(std::move(segment));
        }
    }

    // The context segment rides the identity row only if that row can hold it
    // with no bar at all, and otherwise opens the meter row rather than being
    // truncated.
    const payload::Map context_window = data.obj("context_window");
    const std::string bare = context_segment(context_window, 0);
    if (!bare.empty()) {
        std::vector<std::string> full = identity;
        full.push_back(bare);
        full.insert(full.end(), tail.begin(), tail.end());
        if (width != 0 && row_width(full) > width - setup_.margin) {
            meters.insert(meters.begin(), fitted(context_window, width, meters, 0));
        } else {
            std::vector<std::string> others = identity;
            others.insert(others.end(), tail.begin(), tail.end());
            identity.push_back(fitted(context_window, width, others, identity.size()));
        }
    }
    identity.insert(identity.end(), tail.begin(), tail.end());
    return {std::move(identity), std::move(meters)};
}

std::pair<std::string, std::string> Renderer::cost_forms(
    const payload::Map& data) const {
    if (setup_.env == nullptr) {
        return {};
    }
    const Environment& env = *setup_.env;
    const auto figures = pricing::price(usage::sum(data.str("session_id"), "", env),
                                        pricing::load(config_file(env, "pricing.json")));
    if (!figures) {
        return {};
    }
    // A trailing plus says a model had no rate, so the figure is a floor.
    std::string total = std::string(money) + " " + pricing::money(figures->spent);
    if (!figures->complete) {
        total += "+";
    }
    if (figures->saved <= 0) {
        return {total, total};
    }
    std::string full = total + separator() + std::string(bullseye) + " " +
                       tinted("saved", setup_.palette.dim) + " " +
                       pricing::money(figures->saved);
    return {std::move(full), std::move(total)};
}

std::vector<std::string> Renderer::align_cost(std::vector<std::string> lines,
                                              const payload::Map& data,
                                              int width) const {
    if (lines.size() < 2) {
        return lines;
    }
    std::string& last = lines.back();
    const auto [long_form, short_form] = cost_forms(data);
    // With no width to align against, the cost joins the row rather than being
    // dropped: pushing it right needs a right edge.
    if (width == 0) {
        if (!long_form.empty()) {
            last += separator() + long_form;
        }
        return lines;
    }
    const int room = width - setup_.margin - width::visible(last);
    for (const std::string& segment : {long_form, short_form}) {
        const int gap = room - width::visible(segment);
        if (!segment.empty() && gap >= cost_gutter) {
            last += std::string(static_cast<std::size_t>(gap), ' ') + segment;
            break;
        }
    }
    return lines;
}

std::vector<std::string> Renderer::build(const payload::Map& data, int width) const {
    // An unknown width takes the compact form outright.
    auto rows = compose(data, width, width == 0);
    // A meter row over budget gives up its gauges for the percentages they
    // draw, measured after composing because the context segment may have
    // moved onto it.
    if (width != 0 && !rows[1].empty() && row_width(rows[1]) > width - setup_.margin) {
        rows = compose(data, width, true);
    }
    std::vector<std::string> joined;
    for (const auto& row : rows) {
        if (!row.empty()) {
            joined.push_back(join_with(row, separator()));
        }
    }
    return align_cost(rail(std::move(joined)), data, width);
}

std::vector<std::string> rows_for(std::string_view input,
                                  const Environment& env,
                                  Instant now) {
    Setup setup{.palette = palette::load(config_file(env, "palette.json")),
                .margin = load_margin(config_file(env, "layout.json")),
                .plain = env.plain,
                .env = &env};
    const payload::Document document(input);
    if (!document.root().present()) {
        return {};
    }
    // Before the render, so a payload the bar cannot draw still leaves its
    // numbers on disk.
    status_file::write(document.root(), env, now);
    const Renderer renderer(std::move(setup), now);
    // A width past int's range is not a pane anybody has; it is held at the
    // largest int rather than wrapped, which is the one place this port and
    // Go's 64-bit int would part.
    const std::int64_t width = std::min<std::int64_t>(host::terminal_width(env),
                                                      std::numeric_limits<int>::max());
    return renderer.build(document.root(), static_cast<int>(width));
}

int statusline(Streams streams, const Environment& env, Instant now) {
    const auto raw = files::read_all(streams.input);
    if (!raw) {
        return 0;
    }
    // all_of stops at the first write that fails.
    (void)std::ranges::all_of(rows_for(*raw, env, now), [&](const std::string& row) {
        return files::write_all(streams.output, row + "\n");
    });
    return 0;
}

}  // namespace infobot::render
