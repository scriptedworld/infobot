// The two rows, composed from segments that each render themselves.
//
// Claude Code renders each printed line as its own row. The context window is
// the meter watched while working, so it rides the top row with the model and
// the path, and its bar takes every column the row has left. The rate limit
// windows are the infrequent detail and go below, with the cost pushed to the
// right of them.
#pragma once

#include <chrono>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "environment.hpp"
#include "palette.hpp"
#include "payload.hpp"

namespace infobot::render {

using Instant = std::chrono::sys_time<std::chrono::nanoseconds>;

// What Claude Code keeps for itself, so the pane width is not the budget. 3 is
// the compiled default and ~/.config/infobot/layout.json overrides it.
inline constexpr int default_margin = 3;

// The margin from a layout file, or the default when the file is missing,
// malformed, or sets something outside 0 to 40.
[[nodiscard]] int load_margin(const std::string& path);

// Everything a render reads that is not the payload.
struct Setup {
    palette::Palette palette;
    int margin = default_margin;
    // NO_COLOR: every escape dropped, the separator and the rail included.
    bool plain = false;
    // Where home is, for paths shown relative to it and for the transcripts.
    const Environment* env = nullptr;
};

// The window lengths, from the payload's own field names.
inline constexpr int five_hour = 5 * 3600;
inline constexpr int seven_day = 7 * 86400;

class Renderer {
   public:
    Renderer(Setup setup, Instant now);

    // The rows for a payload at a pane width, 0 meaning unknown.
    [[nodiscard]] std::vector<std::string> build(const payload::Map& data,
                                                 int width) const;

    // A proportional bar whose fill fades cell by cell along the ramp, or is
    // painted one tint when tint is not empty.
    [[nodiscard]] std::string bar(double pct, int cells, std::string_view tint) const;

    // Used/total and a percentage with a bar of cells, or nothing when the
    // numbers are not there. cells of 0 drops the bar.
    [[nodiscard]] std::string context_segment(const payload::Map& context_window,
                                              int cells) const;

    // A rate limit gauge and its countdown.
    [[nodiscard]] std::string limit_segment(std::string_view label,
                                            const payload::Map& window,
                                            int span,
                                            bool compact,
                                            bool reading) const;

    // The time until reset, as the largest two non-zero units. Empty when
    // missing or already past.
    [[nodiscard]] std::string countdown(double resets_at) const;

    // The rows hung off a left rail.
    [[nodiscard]] std::vector<std::string> rail(std::vector<std::string> rows) const;

    // The escape for a percentage on the consumption ramp, alarm included.
    [[nodiscard]] std::string ramp(double pct) const;

    // The escape for a window's pace verdict, faded toward green by how much of
    // the window has elapsed.
    [[nodiscard]] std::string pace_tint(double pct, double elapsed) const;

    // The cost at full length and shortened, from one reading of the
    // transcripts. Both empty when nothing could be priced.
    [[nodiscard]] std::pair<std::string, std::string> cost_forms(
        const payload::Map& data) const;

   private:
    [[nodiscard]] std::string separator() const;
    [[nodiscard]] std::string tinted(std::string_view text,
                                     std::string_view escape) const;
    [[nodiscard]] std::string colour(double pct, std::string_view text) const;
    [[nodiscard]] std::string alarm(double pct) const;
    [[nodiscard]] palette::Rgb pace_rgb(double projected) const;
    [[nodiscard]] double seconds() const;
    [[nodiscard]] int row_width(const std::vector<std::string>& parts) const;
    [[nodiscard]] std::string fitted(const payload::Map& context_window,
                                     const std::vector<std::string>& others,
                                     std::size_t at,
                                     int width) const;
    [[nodiscard]] std::vector<std::vector<std::string>> compose(
        const payload::Map& data, int width, bool compact) const;
    [[nodiscard]] std::vector<std::string> path_parts(const payload::Map& data) const;
    [[nodiscard]] std::vector<std::string> align_cost(std::vector<std::string> lines,
                                                      const payload::Map& data,
                                                      int width) const;

    Setup setup_;
    Instant now_;
};

// Compact token counts: 142000 is 142k, 1000000 is 1.0M.
[[nodiscard]] std::string tokens(double n);

// path shown relative to home when it is inside it.
[[nodiscard]] std::string home_relative(std::string_view path, std::string_view home);

// What the statusline prints for the text on its stdin: the configured palette
// and margin picked up, the pane width asked of the host, and no rows at all
// for input that is not a JSON object.
[[nodiscard]] std::vector<std::string> rows_for(std::string_view input,
                                                const Environment& env,
                                                Instant now);

// The whole statusline: the payload read from one descriptor to its end, the
// rows written to another, stopping at the first failed write so a torn line
// is a missing one. Returns 0 whatever it is given, because a status line that
// fails shows nothing at all.
int statusline(int input, int output, const Environment& env, Instant now);

}  // namespace infobot::render
