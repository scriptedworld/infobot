#include "status_file.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <format>
#include <jsoncons/basic_json.hpp>
#include <jsoncons_ext/jsonschema/jsonschema.hpp>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>

#include "environment.hpp"
#include "files.hpp"
#include "gopath.hpp"
#include "num.hpp"
#include "payload.hpp"
#include "state.hpp"
#include "status_schema.hpp"

namespace infobot::status_file {

namespace {

// ---- yaml.v3's double-quoted scalar ------------------------------------------
//
// yaml_emitter_write_double_quoted_scalar, emitterc.go:1761, with the emitter
// set to unicode and to an unbounded width, which is how yaml.v3's Encoder
// leaves it. The input is always valid UTF-8 here, because it came out of a
// JSON parser, so the byte arithmetic below never reads past a sequence.

constexpr unsigned char utf8_2 = 0xC2;
constexpr unsigned char utf8_surrogate = 0xED;
constexpr unsigned char utf8_private = 0xEE;
constexpr unsigned char utf8_bmp_top = 0xEF;
constexpr unsigned char utf8_e2 = 0xE2;
constexpr unsigned char ascii_space = 0x20;
constexpr unsigned char ascii_tilde = 0x7E;
constexpr unsigned char nbsp_second = 0xA0;
constexpr unsigned char bom_second = 0xBB;
constexpr unsigned char bom_third = 0xBF;
constexpr unsigned char nonchar_second = 0xBF;
constexpr unsigned char nonchar_fffe = 0xBE;
constexpr unsigned char nel_second = 0x85;
constexpr unsigned char line_second = 0x80;
constexpr unsigned char ls_third = 0xA8;
constexpr unsigned char ps_third = 0xA9;

unsigned char at(std::string_view text, std::size_t i) {
    return i < text.size() ? static_cast<unsigned char>(text[i]) : 0;
}

// is_printable, yamlprivateh.go:85. Note what it leaves out: TAB, DEL, the C1
// controls, U+FEFF, U+FFFE, U+FFFF, and every four-byte character.
bool printable(std::string_view text, std::size_t i) {
    const unsigned char b = at(text, i);
    const unsigned char next = at(text, i + 1);
    const unsigned char third = at(text, i + 2);
    if (b == '\n' || (b >= ascii_space && b <= ascii_tilde)) {
        return true;
    }
    if (b == utf8_2) {
        return next >= nbsp_second;
    }
    if (b > utf8_2 && b < utf8_surrogate) {
        return true;
    }
    if (b == utf8_surrogate) {
        return next < nbsp_second;
    }
    if (b == utf8_private) {
        return true;
    }
    if (b == utf8_bmp_top) {
        const bool bom = next == bom_second && third == bom_third;
        const bool nonchar =
            next == nonchar_second && (third == nonchar_fffe || third == nonchar_second);
        return !bom && !nonchar;
    }
    return false;
}

// is_break, yamlprivateh.go:124: CR, LF, NEL, LS, PS.
bool line_break(std::string_view text, std::size_t i) {
    const unsigned char b = at(text, i);
    if (b == '\r' || b == '\n') {
        return true;
    }
    if (b == utf8_2) {
        return at(text, i + 1) == nel_second;
    }
    if (b == utf8_e2 && at(text, i + 1) == line_second) {
        return at(text, i + 2) == ls_third || at(text, i + 2) == ps_third;
    }
    return false;
}

// is_bom, yamlprivateh.go:103, which tests the first three bytes of the WHOLE
// value whatever position it is asked about. A value opening with a byte-order
// mark therefore has every character escaped.
bool opens_with_bom(std::string_view text) {
    return at(text, 0) == utf8_bmp_top && at(text, 1) == bom_second &&
           at(text, 2) == bom_third;
}

// width, yamlprivateh.go:181, by the lead byte alone.
std::size_t sequence_width(unsigned char lead) {
    constexpr unsigned char top1 = 0x80;
    constexpr unsigned char top3 = 0xE0;
    constexpr unsigned char top4 = 0xF0;
    constexpr unsigned char top5 = 0xF8;
    constexpr unsigned char lead2 = 0xC0;
    if ((lead & top1) == 0) {
        return 1;
    }
    if ((lead & top3) == lead2) {
        return 2;
    }
    if ((lead & top4) == top3) {
        return 3;
    }
    return (lead & top5) == top4 ? 4 : 0;
}

struct Rune {
    char32_t value;
    std::size_t width;
};

// The decode the emitter does before an escape: lead byte masked by width and
// continuation bytes shifted in, without validating either.
Rune rune_at(std::string_view text, std::size_t i) {
    constexpr std::array<unsigned char, 5> lead_mask = {0, 0x7F, 0x1F, 0x0F, 0x07};
    constexpr unsigned char payload = 0x3F;
    constexpr int shift = 6;
    const std::size_t width = sequence_width(at(text, i));
    char32_t value = at(text, i) & lead_mask.at(width);
    for (std::size_t k = 1; k < width; ++k) {
        value = (value << shift) | (at(text, i + k) & payload);
    }
    return {.value = value, .width = width == 0 ? 1 : width};
}

// The escape for one rune, after the backslash.
void escape_rune(std::string& out, char32_t rune) {
    switch (rune) {
        case U'\0': out.push_back('0'); return;
        case U'\a': out.push_back('a'); return;
        case U'\b': out.push_back('b'); return;
        case U'\t': out.push_back('t'); return;
        case U'\n': out.push_back('n'); return;
        case U'\v': out.push_back('v'); return;
        case U'\f': out.push_back('f'); return;
        case U'\r': out.push_back('r'); return;
        case U'\x1B': out.push_back('e'); return;
        case U'"': out.push_back('"'); return;
        case U'\\': out.push_back('\\'); return;
        case U'\u0085': out.push_back('N'); return;
        case U'\u00A0': out.push_back('_'); return;
        case U'\u2028': out.push_back('L'); return;
        case U'\u2029': out.push_back('P'); return;
        default: break;
    }
    constexpr char32_t byte_max = 0xFF;
    constexpr char32_t bmp_max = 0xFFFF;
    if (rune <= byte_max) {
        out.append(std::format("x{:02X}", static_cast<std::uint32_t>(rune)));
    } else if (rune <= bmp_max) {
        out.append(std::format("u{:04X}", static_cast<std::uint32_t>(rune)));
    } else {
        out.append(std::format("U{:08X}", static_cast<std::uint32_t>(rune)));
    }
}

// Digits d1 d2 ... dn meaning d1.d2...dn x 10^exponent, written positionally
// with .0 when the value is whole.
std::string place_point(const std::string& digits, int exponent) {
    const auto length = static_cast<int>(digits.size());
    if (exponent < 0) {
        return "0." + std::string(static_cast<std::size_t>(-exponent - 1), '0') + digits;
    }
    const int whole = exponent + 1;
    if (whole >= length) {
        return digits + std::string(static_cast<std::size_t>(whole - length), '0') + ".0";
    }
    return digits.substr(0, static_cast<std::size_t>(whole)) + "." +
           digits.substr(static_cast<std::size_t>(whole));
}

// ---- the schema --------------------------------------------------------------

using Json = jsoncons::json;

const jsoncons::jsonschema::json_schema<Json>& schema() {
    static const jsoncons::jsonschema::json_schema<Json> compiled =
        jsoncons::jsonschema::make_json_schema(Json::parse(schema_text));
    return compiled;
}

Json as_json(const Fields& fields) {
    Json object(jsoncons::json_object_arg);
    for (const auto& [key, value] : fields) {
        if (const auto* text = std::get_if<std::string>(&value)) {
            object.insert_or_assign(key, *text);
        } else if (const auto* integer = std::get_if<std::int64_t>(&value)) {
            object.insert_or_assign(key, *integer);
        } else {
            object.insert_or_assign(key, std::get<double>(value));
        }
    }
    return object;
}

// ---- the atomic write ----------------------------------------------------------

constexpr ::mode_t directory_mode = 0755;
constexpr ::mode_t file_mode = 0644;

// A temporary beside the target, synced, made readable, and renamed into place.
// Beside, because a rename across filesystems is a copy and not atomic.
bool replace(const std::string& target, std::string_view body) {
    std::string temporary =
        gopath::join({gopath::dir(target), "." + gopath::base(target) + ".XXXXXX"});
    const int fd = ::mkstemp(temporary.data());
    if (fd < 0) {
        return false;
    }
    bool ok = files::write_all(fd, body) && ::fsync(fd) == 0;
    ok = (::close(fd) == 0) && ok;
    ok = ok && ::chmod(temporary.c_str(), file_mode) == 0;
    ok = ok && ::rename(temporary.c_str(), target.c_str()) == 0;
    if (!ok) {
        ::unlink(temporary.c_str());
    }
    return ok;
}

}  // namespace

std::string path(std::string_view session, const Environment& env) {
    const std::string dir = state_dir(env);
    if (dir.empty()) {
        return {};
    }
    return gopath::join({dir, std::string(session) + ".status.yaml"});
}

std::int64_t tokens(double count) {
    // 2^63, the first double at or past int64's range.
    constexpr double bound = 9223372036854775808.0;
    if (!(count > 0)) {
        return 0;
    }
    if (count >= bound) {
        return std::numeric_limits<std::int64_t>::max();
    }
    return static_cast<std::int64_t>(count);
}

std::string timestamp(Instant now) {
    const auto seconds =
        std::chrono::floor<std::chrono::seconds>(now).time_since_epoch().count();
    const auto when = static_cast<std::time_t>(seconds);
    std::tm local{};
    if (::localtime_r(&when, &local) == nullptr) {
        return {};
    }
    constexpr std::size_t longest = 32;
    std::array<char, longest> text{};
    const std::size_t length = std::strftime(text.data(), text.size(), "%Y-%m-%dT%H:%M:%S%z", &local);
    std::string out(text.data(), length);
    // %z gives +hhmm; Go's -07:00 layout gives +hh:mm.
    constexpr std::size_t minutes = 2;
    if (out.size() > minutes) {
        out.insert(out.size() - minutes, ":");
    }
    return out;
}

// strconv.FormatFloat(v, 'f', -1, 64): the SHORTEST digits that read back as
// the same double, placed positionally. std::to_chars in fixed notation is not
// that: past 2^53 it prints the exact binary value's digits, so 1e300 came out
// with three hundred of them. The shortest digits come from scientific notation
// and the point is placed here.
std::optional<std::string> float_text(double value) {
    if (!std::isfinite(value)) {
        return std::nullopt;
    }
    constexpr std::size_t longest = 32;
    std::array<char, longest> text{};
    const auto written = std::to_chars(
        text.data(), text.data() + text.size(), value, std::chars_format::scientific);
    const std::string_view scientific(text.data(), written.ptr);
    const std::size_t e = scientific.find('e');
    std::string_view mantissa = scientific.substr(0, e);
    std::string out;
    if (mantissa.starts_with('-')) {
        out.push_back('-');
        mantissa.remove_prefix(1);
    }
    std::string digits;
    for (const char c : mantissa) {
        if (c != '.') {
            digits.push_back(c);
        }
    }
    const std::string_view exponent_text = scientific.substr(e + 1);
    int exponent = 0;
    std::from_chars(exponent_text.data() + (exponent_text.starts_with('+') ? 1 : 0),
                    exponent_text.data() + exponent_text.size(),
                    exponent);
    out.append(place_point(digits, exponent));
    return out;
}

std::string quote(std::string_view text) {
    std::string out = "\"";
    const bool bom = opens_with_bom(text);
    for (std::size_t i = 0; i < text.size();) {
        const unsigned char b = at(text, i);
        const bool escaped =
            !printable(text, i) || bom || line_break(text, i) || b == '"' || b == '\\';
        const Rune rune = rune_at(text, i);
        if (escaped) {
            out.push_back('\\');
            escape_rune(out, rune.value);
        } else {
            out.append(text.substr(i, rune.width));
        }
        i += rune.width;
    }
    out.push_back('"');
    return out;
}

std::optional<std::string> encode(const Fields& fields) {
    std::string out;
    for (const auto& [key, value] : fields) {
        out.append(quote(key));
        out.append(": ");
        if (const auto* text = std::get_if<std::string>(&value)) {
            out.append(quote(*text));
        } else if (const auto* integer = std::get_if<std::int64_t>(&value)) {
            out.append(std::to_string(*integer));
        } else {
            const auto spelled = float_text(std::get<double>(value));
            if (!spelled) {
                return std::nullopt;
            }
            out.append(*spelled);
        }
        out.push_back('\n');
    }
    return out;
}

Fields fields(const payload::Map& data, std::string_view session, Instant now) {
    Fields out;
    out.insert_or_assign("session", std::string(session));
    out.insert_or_assign("written", timestamp(now));
    if (const std::string_view cwd = data.obj("workspace").str("current_dir"); !cwd.empty()) {
        out.insert_or_assign("cwd", std::string(cwd));
    }
    std::string_view model = data.obj("model").str("display_name");
    if (model.empty()) {
        model = data.obj("model").str("id");
    }
    if (!model.empty()) {
        out.insert_or_assign("model", std::string(model));
    }
    if (const std::string_view effort = data.obj("effort").str("level"); !effort.empty()) {
        out.insert_or_assign("effort", std::string(effort));
    }
    const auto window = state::figures(data.obj("context_window"));
    if (!window) {
        return out;
    }
    const std::int64_t used = tokens(window->used);
    const std::int64_t size = tokens(window->size);
    out.insert_or_assign("context_used", used);
    out.insert_or_assign("context_size", size);
    out.insert_or_assign("context_remaining", size > used ? size - used : 0);
    out.insert_or_assign("context_percent", num::round_to(window->percent, 1));
    return out;
}

bool valid(const Fields& fields) {
    return schema().is_valid(as_json(fields));
}

void write(const payload::Map& data, const Environment& env, Instant now) {
    const std::string_view session = data.str("session_id");
    if (session.empty()) {
        return;
    }
    const std::string target = path(session, env);
    if (target.empty() || !files::make_directories(gopath::dir(target), directory_mode)) {
        return;
    }
    const Fields values = fields(data, session, now);
    if (!valid(values)) {
        return;
    }
    if (const auto body = encode(values)) {
        (void)replace(target, *body);
    }
}

}  // namespace infobot::status_file
