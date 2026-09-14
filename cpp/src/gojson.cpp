#include "gojson.hpp"

#include <simdjson.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace infobot::gojson {

void Decode::string_into(simdjson::dom::element value, std::string& out) {
    std::string_view text;
    if (value.get(text) == simdjson::SUCCESS) {
        out = text;
    } else if (!value.is_null()) {
        ok_ = false;
    }
}

void Decode::float_into(simdjson::dom::element value, double& out) {
    double number = 0;
    if (value.get_double().get(number) == simdjson::SUCCESS) {
        out = number;
    } else if (!value.is_null()) {
        ok_ = false;
    }
}

// INT64 is simdjson's word for an integer literal that fits. A literal with a
// fraction or an exponent arrives as DOUBLE and one past int64 as UINT64, and Go
// refuses both for an int.
void Decode::int_into(simdjson::dom::element value, std::int64_t& out) {
    std::int64_t number = 0;
    if (value.type() == simdjson::dom::element_type::INT64 &&
        value.get(number) == simdjson::SUCCESS) {
        out = number;
    } else if (!value.is_null()) {
        ok_ = false;
    }
}

void Decode::bool_into(simdjson::dom::element value, bool& out) {
    bool flag = false;
    if (value.get(flag) == simdjson::SUCCESS) {
        out = flag;
    } else if (!value.is_null()) {
        ok_ = false;
    }
}

void Decode::optional_float_into(simdjson::dom::element value,
                                 std::optional<double>& out) {
    if (value.is_null()) {
        out.reset();
        return;
    }
    double number = 0;
    if (value.get_double().get(number) == simdjson::SUCCESS) {
        out = number;
    } else {
        ok_ = false;
    }
}

void Decode::optional_int_into(simdjson::dom::element value,
                               std::optional<std::int64_t>& out) {
    if (value.is_null()) {
        out.reset();
        return;
    }
    std::int64_t number = 0;
    int_into(value, number);
    if (value.type() == simdjson::dom::element_type::INT64) {
        out = number;
    }
}

}  // namespace infobot::gojson
