#include "support.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace infobot::test {

Scratch::Scratch() {
    std::string pattern =
        (std::filesystem::temp_directory_path() / "infobot-test-XXXXXX").string();
    if (::mkdtemp(pattern.data()) == nullptr) {
        throw std::runtime_error("mkdtemp failed for " + pattern);
    }
    path_ = pattern;
}

Scratch::~Scratch() {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
}

const std::string& Scratch::path() const {
    return path_;
}

std::string Scratch::file(std::string_view relative) const {
    const std::filesystem::path full = std::filesystem::path(path_) / relative;
    std::filesystem::create_directories(full.parent_path());
    return full.string();
}

std::string Scratch::write(std::string_view relative, std::string_view body) const {
    const std::string full = file(relative);
    std::ofstream stream(full, std::ios::binary | std::ios::trunc);
    stream.write(body.data(), static_cast<std::streamsize>(body.size()));
    return full;
}

Instant clock() {
    return Instant(std::chrono::seconds(clock_seconds));
}

double at(std::int64_t seconds) {
    return static_cast<double>(clock_seconds + seconds);
}

std::string slurp(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

}  // namespace infobot::test
