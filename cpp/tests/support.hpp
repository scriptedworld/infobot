// What the suites share: a scratch directory and the fixed clock.
//
// NOTHING HERE STANDS IN FOR THE PROGRAM. A test writes real files into a real
// directory and hands the code under test a real Environment naming it, which
// is how the Go suite moves HOME and XDG_STATE_HOME with t.Setenv, without
// mutating the process environment.
#pragma once

#include <chrono>
#include <string>
#include <string_view>

namespace infobot::test {

// A directory created fresh under the system temporary directory and removed,
// with everything in it, when this goes out of scope.
class Scratch {
public:
    Scratch();
    ~Scratch();

    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;
    Scratch(Scratch&&) = delete;
    Scratch& operator=(Scratch&&) = delete;

    [[nodiscard]] const std::string& path() const;

    // path/relative, with its parent directories created.
    [[nodiscard]] std::string file(std::string_view relative) const;

    // Writes body to path/relative, creating parents, and returns the path.
    std::string write(std::string_view relative, std::string_view body) const;

private:
    std::string path_;
};

using Instant = std::chrono::sys_time<std::chrono::nanoseconds>;

// 1,800,000,000 seconds after the epoch, the Go suite's fixed instant, so a
// countdown is asserted as a string rather than as a shape.
inline constexpr std::int64_t clock_seconds = 1'800'000'000;

[[nodiscard]] Instant clock();

// The clock plus a number of seconds, as the epoch value a payload carries.
[[nodiscard]] double at(std::int64_t seconds);

// The whole of a file, or empty when it cannot be read.
[[nodiscard]] std::string slurp(const std::string& path);

}  // namespace infobot::test
