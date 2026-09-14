#include "files.hpp"

#include <fcntl.h>
#include <simdjson.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gopath.hpp"

namespace infobot::files {

namespace {

// The Go port reads through a 256KB buffered reader, so this starts the same
// size and grows only for a line longer than it.
constexpr std::size_t chunk = 256UZ * 1024UZ;

}  // namespace

std::optional<std::string> read(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return std::nullopt;
    }
    std::string content{std::istreambuf_iterator<char>(stream),
                        std::istreambuf_iterator<char>()};
    if (stream.bad()) {
        return std::nullopt;
    }
    return content;
}

std::optional<std::string> read_all(int fd) {
    std::string content;
    std::vector<char> buffer(chunk);
    for (;;) {
        const ssize_t got = ::read(fd, buffer.data(), buffer.size());
        if (got == 0) {
            return content;
        }
        if (got < 0) {
            return std::nullopt;
        }
        content.append(buffer.data(), static_cast<std::size_t>(got));
    }
}

bool write_all(int fd, std::string_view body) {
    std::size_t written = 0;
    while (written < body.size()) {
        const ssize_t got = ::write(fd, body.data() + written, body.size() - written);
        if (got <= 0) {
            return false;
        }
        written += static_cast<std::size_t>(got);
    }
    return true;
}

bool make_directories(const std::string& path, ::mode_t mode) {
    struct stat info{};
    if (::stat(path.c_str(), &info) == 0) {
        return S_ISDIR(info.st_mode);
    }
    const std::string parent = gopath::dir(path);
    if (parent != path && !make_directories(parent, mode)) {
        return false;
    }
    if (::mkdir(path.c_str(), mode) == 0) {
        return true;
    }
    // Lost a race with another render creating it, which is success.
    return ::stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

bool write(const std::string& path, std::string_view body, ::mode_t mode) {
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, mode);
    if (fd < 0) {
        return false;
    }
    const bool whole = write_all(fd, body);
    return (::close(fd) == 0) && whole;
}

Lines::Lines(const std::string& path, std::size_t offset)
    : fd_(::open(path.c_str(), O_RDONLY | O_CLOEXEC)) {
    if (fd_ < 0) {
        return;
    }
    good_ = ::lseek(fd_, static_cast<off_t>(offset), SEEK_SET) >= 0;
    buffer_.resize(chunk + simdjson::SIMDJSON_PADDING);
}

Lines::~Lines() {
    if (fd_ >= 0) {
        ::close(fd_);
    }
}

bool Lines::good() const { return good_; }

bool Lines::fill() {
    // Slide what is left to the front, and grow only when a single line has
    // outrun the whole buffer.
    if (begin_ > 0) {
        std::copy(buffer_.begin() + static_cast<std::ptrdiff_t>(begin_),
                  buffer_.begin() + static_cast<std::ptrdiff_t>(end_),
                  buffer_.begin());
        end_ -= begin_;
        begin_ = 0;
    }
    if (buffer_.size() - end_ < chunk + simdjson::SIMDJSON_PADDING) {
        buffer_.resize(end_ + chunk + simdjson::SIMDJSON_PADDING);
    }
    const ssize_t got = ::read(fd_, buffer_.data() + end_, chunk);
    if (got <= 0) {
        return false;
    }
    end_ += static_cast<std::size_t>(got);
    return true;
}

std::optional<std::string_view> Lines::next() {
    if (!good_) {
        return std::nullopt;
    }
    std::size_t searched = begin_;
    for (;;) {
        const std::string_view held(buffer_.data() + searched, end_ - searched);
        const std::size_t newline = held.find('\n');
        if (newline != std::string_view::npos) {
            const std::size_t start = begin_;
            begin_ = searched + newline + 1;
            return std::string_view(buffer_.data() + start, begin_ - start);
        }
        searched = end_;
        const std::size_t before = begin_;
        if (!fill()) {
            return std::nullopt;
        }
        searched -= before;
    }
}

std::string_view Lines::tail() const {
    return {buffer_.data() + begin_, end_ - begin_};
}

}  // namespace infobot::files
