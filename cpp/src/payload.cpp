#include "payload.hpp"

#include <simdjson.h>

#include <optional>
#include <string>
#include <string_view>

namespace infobot::payload {

Map::Map(simdjson::dom::object object) : object_(object) {}

bool Map::present() const { return object_.has_value(); }

std::optional<simdjson::dom::element> Map::find(std::string_view key) const {
    if (!object_) {
        return std::nullopt;
    }
    std::optional<simdjson::dom::element> found;
    for (const auto field : *object_) {
        if (field.key == key) {
            found = field.value;
        }
    }
    return found;
}

Map Map::obj(std::string_view key) const {
    simdjson::dom::object nested;
    if (auto value = find(key); value && value->get(nested) == simdjson::SUCCESS) {
        return Map(nested);
    }
    return {};
}

std::string_view Map::str(std::string_view key) const {
    std::string_view text;
    if (auto value = find(key); value && value->get(text) == simdjson::SUCCESS) {
        return text;
    }
    return {};
}

std::optional<double> Map::num(std::string_view key) const {
    // get_double converts an integer too, which is what Go's decoder does with
    // every JSON number it puts into an interface, and refuses anything that is
    // not a number.
    auto value = find(key);
    double number = 0;
    if (!value || value->get_double().get(number) != simdjson::SUCCESS) {
        return std::nullopt;
    }
    return number;
}

double Map::count(std::string_view key) const { return num(key).value_or(0.0); }

bool Map::has(std::string_view key) const { return find(key).has_value(); }

Document::Document(std::string_view json) {
    simdjson::dom::object object;
    if (parser_.parse(json.data(), json.size()).get(object) == simdjson::SUCCESS) {
        root_ = Map(object);
    }
}

Map Document::root() const { return root_; }

}  // namespace infobot::payload
