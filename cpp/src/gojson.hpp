// JSON decoded into a struct the way the Go port's callers use encoding/json.
//
// The payload is read as a tree and needs none of this. Everything else the Go
// port reads, the rate table, the palette, the layout, herdr's reply, a
// transcript line and the offsets, it unmarshals into a struct and then tests
// `Unmarshal(...) != nil`. Two rules follow, and they are what this carries:
//
//   a value of the wrong type is an error, and the error does not stop the
//   decode. So ONE wrong type anywhere rejects the whole file, even where the
//   field it hit is one the caller ignores
//
//   null leaves a plain value as it was and resets a pointer, a map or a slice
//
// PARITY IS HELD ON THE INPUTS THIS PROGRAM IS REALLY HANDED, not on every
// input Go accepts. Keys are matched exactly, where Go also matches a key
// differing only in case; nothing that writes these files does that.
// cpp/README.md lists the differences and how each was measured.
#pragma once

#include <simdjson.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace infobot::gojson {

template <class T>
using Map = std::map<std::string, T, std::less<>>;

class Decode {
   public:
    // Whether Unmarshal would have returned nil.
    [[nodiscard]] bool ok() const { return ok_; }

    // Each value in `object` under `field`, in document order, so the last one
    // wins as it does in Go.
    template <class Visit>
    void fields(simdjson::dom::object object, std::string_view field, Visit&& visit) {
        for (const auto entry : object) {
            if (entry.key == field) {
                std::invoke(visit, entry.value);
            }
        }
    }

    // A struct-typed value: an object is decoded into it, null leaves it, and
    // anything else is a type error.
    template <class Visit>
    void as_struct(simdjson::dom::element value, Visit&& visit) {
        simdjson::dom::object object;
        if (value.get(object) == simdjson::SUCCESS) {
            std::invoke(visit, object);
        } else if (!value.is_null()) {
            ok_ = false;
        }
    }

    void string_into(simdjson::dom::element value, std::string& out);
    void float_into(simdjson::dom::element value, double& out);
    void int_into(simdjson::dom::element value, std::int64_t& out);
    void bool_into(simdjson::dom::element value, bool& out);

    // A pointer field: null resets it.
    void optional_float_into(simdjson::dom::element value, std::optional<double>& out);
    void optional_int_into(simdjson::dom::element value,
                           std::optional<std::int64_t>& out);

    // A map field. Null resets it to nil. An object creates it if nil and sets
    // each key from a fresh zero value, which is what Go does per entry.
    template <class T, class DecodeValue>
    void map_into(simdjson::dom::element value,
                  std::optional<Map<T>>& out,
                  DecodeValue&& decode_value) {
        if (value.is_null()) {
            out.reset();
            return;
        }
        simdjson::dom::object object;
        if (value.get(object) != simdjson::SUCCESS) {
            ok_ = false;
            return;
        }
        if (!out) {
            out.emplace();
        }
        for (const auto entry : object) {
            T fresh{};
            std::invoke(decode_value, *this, entry.value, fresh);
            out->insert_or_assign(std::string(entry.key), std::move(fresh));
        }
    }

    // A slice field. Null resets it to nil, and an array replaces it.
    template <class T, class DecodeValue>
    void slice_into(simdjson::dom::element value,
                    std::optional<std::vector<T>>& out,
                    DecodeValue&& decode_value) {
        if (value.is_null()) {
            out.reset();
            return;
        }
        simdjson::dom::array array;
        if (value.get(array) != simdjson::SUCCESS) {
            ok_ = false;
            return;
        }
        out.emplace();
        for (const auto element : array) {
            std::invoke(decode_value, *this, element, out->emplace_back());
        }
    }

   private:
    bool ok_ = true;
};

}  // namespace infobot::gojson
