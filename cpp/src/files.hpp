// Reading a file, and reading one a line at a time, without holding it whole.
//
// THE ZIG PORT'S MEMORY DEFECT IS WHY THIS EXISTS AS ITS OWN PIECE. It read a
// transcript's whole delta into one allocation, so a cold read of a 6.3MB file
// held 368MB resident. Lines here come out of one bounded buffer that grows
// only as far as the longest single line.
#pragma once

#include <simdjson.h>
#include <sys/types.h>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace infobot::files {

// The whole of a small file, the rate table or a palette. nullopt when it
// cannot be opened or read.
[[nodiscard]] std::optional<std::string> read(const std::string& path);

// Everything readable from a descriptor, to its end. nullopt on a read error.
[[nodiscard]] std::optional<std::string> read_all(int fd);

// All of body to a descriptor. False when any of it could not be written.
[[nodiscard]] bool write_all(int fd, std::string_view body);

// os.MkdirAll: every missing directory on the way to path, created with mode
// before the umask. An existing directory is left as it is. False when path
// cannot be made a directory.
[[nodiscard]] bool make_directories(const std::string& path, ::mode_t mode);

// os.WriteFile: path truncated or created with mode before the umask, then
// written. False on any failure.
[[nodiscard]] bool write(const std::string& path, std::string_view body, ::mode_t mode);

// An open file read line by line from a given offset.
//
// Every line handed out is followed in memory by at least SIMDJSON_PADDING
// readable bytes, so simdjson can parse it where it lies instead of copying it.
class Lines {
   public:
    // The file at path, from offset. good() is false when it cannot be opened
    // or the offset cannot be reached.
    Lines(const std::string& path, std::size_t offset);
    ~Lines();

    Lines(const Lines&) = delete;
    Lines& operator=(const Lines&) = delete;
    Lines(Lines&&) = delete;
    Lines& operator=(Lines&&) = delete;

    [[nodiscard]] bool good() const;

    // The next line ending in a newline, the newline included. nullopt at the
    // end of the file, at a read error, and at a last line with no newline,
    // which is what bufio.Reader.ReadBytes reports as an error.
    [[nodiscard]] std::optional<std::string_view> next();

    // After next() has returned nullopt: the unterminated tail, if any. This is
    // what bufio.Scanner hands out as a final line and ReadBytes does not.
    [[nodiscard]] std::string_view tail() const;

   private:
    // Reads more of the file onto the end of the buffer. False at the end or on
    // an error.
    bool fill();

    int fd_ = -1;
    bool good_ = false;
    std::vector<char> buffer_;
    std::size_t begin_ = 0;
    std::size_t end_ = 0;
};

}  // namespace infobot::files
