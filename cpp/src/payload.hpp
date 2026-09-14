// The session JSON Claude Code puts on stdin.
//
// EVERY FIELD IS OPTIONAL and is treated as optional. rate_limits appears only
// for subscribers and only after the first API response, either window can be
// absent on its own, and used_percentage can be null early in a session. So the
// payload is read through views onto the parsed tree rather than into a struct,
// and an absent field and a zero field stay distinguishable. Zero is a claim,
// absence is not.
#pragma once

#include <simdjson.h>

#include <optional>
#include <string_view>

namespace infobot::payload {

// One JSON object. An absent Map answers like an empty one, so a caller never
// has to check before reaching through it.
//
// A Map is a view. It and every string it returns are valid only while the
// Document it came from is alive.
class Map {
   public:
    Map() = default;
    explicit Map(simdjson::dom::object object);

    // Whether this is an object at all, as opposed to absent.
    [[nodiscard]] bool present() const;

    // The nested object at key, or an absent Map when it is missing or is not
    // one.
    [[nodiscard]] Map obj(std::string_view key) const;

    // The string at key, or empty when it is missing or is not one.
    [[nodiscard]] std::string_view str(std::string_view key) const;

    // The number at key, when there is one. Any JSON number reads as a double,
    // as it does in Go; anything else is absent data rather than a zero.
    [[nodiscard]] std::optional<double> num(std::string_view key) const;

    // The number at key or zero, for fields that are summed rather than
    // displayed. A token count that is not there contributes nothing, where a
    // percentage that is not there drops a segment.
    [[nodiscard]] double count(std::string_view key) const;

    // Whether key holds anything at all, null included.
    [[nodiscard]] bool has(std::string_view key) const;

   private:
    // The LAST value under key. A repeated key is valid JSON, and Go's decoder
    // keeps the last one it reads, so the first match would disagree with it.
    [[nodiscard]] std::optional<simdjson::dom::element> find(
        std::string_view key) const;

    std::optional<simdjson::dom::object> object_;
};

// A parsed payload. It owns the parser the tree lives in, so it can be neither
// copied nor moved while a Map points into it.
class Document {
   public:
    // The root is present only when the text parses and is a JSON object,
    // which is the same test Go's Main makes before rendering anything.
    explicit Document(std::string_view json);
    ~Document() = default;

    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;
    Document(Document&&) = delete;
    Document& operator=(Document&&) = delete;

    [[nodiscard]] Map root() const;

   private:
    simdjson::dom::parser parser_;
    Map root_;
};

}  // namespace infobot::payload
