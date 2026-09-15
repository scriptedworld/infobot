#include "usage.hpp"

#include <simdjson.h>
#include <sys/stat.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "environment.hpp"
#include "files.hpp"
#include "gojson.hpp"
#include "gopath.hpp"
#include "payload.hpp"

namespace infobot::usage {

namespace {

// How far into a transcript to look for the session it belongs to. A transcript
// opens with bookkeeping that carries no session: measured on one opened by a
// clear, the field first appears on record 18.
constexpr int claim_lines = 40;

// bufio.Scanner's ceiling in the Go port. A longer line stops the search.
constexpr std::size_t claim_line_max = 16UZ * 1024UZ * 1024UZ;

constexpr std::array<std::string_view, 5> counted = {
    "input_tokens",
    "output_tokens",
    "cache_read_input_tokens",
    "ephemeral_5m_input_tokens",
    "ephemeral_1h_input_tokens",
};

// The session a transcript line claims, if it parses as a record carrying one.
// An unpadded line is copied by simdjson before it is parsed.
std::optional<std::string> claimed(simdjson::dom::parser& parser,
                                   std::string_view line,
                                   bool padded) {
    simdjson::dom::element root;
    if (parser.parse(line.data(), line.size(), !padded).get(root) != simdjson::SUCCESS) {
        return std::nullopt;
    }
    gojson::Decode decode;
    std::string session;
    decode.as_struct(root, [&](simdjson::dom::object record) {
        decode.fields(record, "session_id", [&](simdjson::dom::element value) {
            decode.string_into(value, session);
        });
    });
    if (!decode.ok() || session.empty()) {
        return std::nullopt;
    }
    return session;
}

// One line for the origin search, as bufio.Scanner hands lines out: without its
// newline or a carriage return before it, and including an unterminated last
// line, which ReadBytes would not hand out at all.
struct ScannedLine {
    std::string_view text;
    // Whether simdjson's padding follows text where it lies. The tail of the
    // file has no newline, so nothing guarantees what follows it.
    bool padded = false;
    bool last = false;
};

std::optional<ScannedLine> scanner_line(files::Lines& lines) {
    ScannedLine line;
    if (const auto whole = lines.next()) {
        line.text = *whole;
        line.padded = true;
    } else {
        line.text = lines.tail();
        line.last = true;
        if (line.text.empty()) {
            return std::nullopt;
        }
    }
    if (line.text.ends_with('\n')) {
        line.text.remove_suffix(1);
    }
    if (line.text.ends_with('\r')) {
        line.text.remove_suffix(1);
    }
    return line;
}

// The session a transcript belongs to: its recorded origin, else its own name.
std::string origin_of(const std::string& path) {
    files::Lines lines(path, 0);
    simdjson::dom::parser parser;
    for (int read = 0; read < claim_lines; ++read) {
        const auto line = scanner_line(lines);
        if (!line || line->text.size() > claim_line_max) {
            break;
        }
        if (auto session = claimed(parser, line->text, line->padded)) {
            return *session;
        }
        if (line->last) {
            break;
        }
    }
    return gopath::stem(path);
}

// A transcript line as the Go port's record struct reads it: message.model and
// message.usage, and whether the decode as a whole succeeded.
//
// usage is a map[string]any, and a repeated key decodes INTO the map the first
// one made rather than replacing it, so each object is a layer over the ones
// before and a key reads from the last layer holding it. Null resets the map.
struct UsageRecord {
    std::string model;
    std::vector<simdjson::dom::object> usage;
    bool ok = false;
};

void decode_message(gojson::Decode& decode, simdjson::dom::object fields, UsageRecord& out) {
    decode.fields(fields, "model", [&](simdjson::dom::element value) {
        decode.string_into(value, out.model);
    });
    // map[string]any takes any object. Null resets it, and anything else is a
    // type error.
    decode.fields(fields, "usage", [&](simdjson::dom::element value) {
        simdjson::dom::object object;
        if (value.get(object) == simdjson::SUCCESS) {
            out.usage.push_back(object);
        } else if (value.is_null()) {
            out.usage.clear();
        } else {
            decode.as_struct(value, [](simdjson::dom::object) {});
        }
    });
}

UsageRecord decode_record(simdjson::dom::element root) {
    gojson::Decode decode;
    UsageRecord record;
    decode.as_struct(root, [&](simdjson::dom::object top) {
        decode.fields(top, "message", [&](simdjson::dom::element message) {
            decode.as_struct(message, [&](simdjson::dom::object fields) {
                decode_message(decode, fields, record);
            });
        });
    });
    record.ok = decode.ok();
    return record;
}

// The layer a key reads from: the last one holding it, or an absent Map.
payload::Map holding(const std::vector<simdjson::dom::object>& layers, std::string_view key) {
    const auto found = std::ranges::find_if(layers.rbegin(), layers.rend(), [&](auto layer) {
        return payload::Map(layer).has(key);
    });
    return found == layers.rend() ? payload::Map{} : payload::Map(*found);
}

// The counted fields of a usage map added into a model's totals. A field is
// read from the top level where it is present, null included, and from the
// nested cache_creation object where it is not.
void add_usage(gojson::Map<double>& into, const std::vector<simdjson::dom::object>& layers) {
    const payload::Map creation = holding(layers, "cache_creation").obj("cache_creation");
    for (const std::string_view field : counted) {
        const payload::Map top = holding(layers, field);
        const payload::Map& source = top.present() ? top : creation;
        // Only numbers are added. Anything else is ignored rather than
        // coerced, and does not abandon the record.
        if (const auto number = source.num(field)) {
            into[std::string(field)] += *number;
        }
    }
}

// One record's usage added into found, ignoring any line that is not usage.
void take(Totals& found, simdjson::dom::parser& parser, std::string_view line) {
    simdjson::dom::element root;
    if (parser.parse(line.data(), line.size(), false).get(root) != simdjson::SUCCESS) {
        return;
    }
    const UsageRecord record = decode_record(root);
    const bool empty = std::ranges::all_of(
        record.usage, [](simdjson::dom::object layer) { return layer.size() == 0; });
    if (!record.ok || empty) {
        return;
    }
    // A record naming no model is counted under a placeholder no rate table
    // answers to, so it flags the total incomplete rather than being priced at
    // a neighbour's rate. The entry exists even when nothing in it counts,
    // which is also what flags an unpriced model.
    add_usage(found[record.model.empty() ? std::string("?") : record.model], record.usage);
}

void merge(Totals& into, const Totals& more) {
    for (const auto& [model, fields] : more) {
        auto& target = into[model];
        for (const auto& [field, value] : fields) {
            target[field] += value;
        }
    }
}

struct Scanned {
    Totals found;
    std::uint64_t consumed = 0;
};

// Usage from start onward, and how many bytes of it were whole lines. The
// offset advances only over lines that arrived complete.
Scanned scan(const std::string& path, std::uint64_t start) {
    Scanned out;
    files::Lines lines(path, start);
    simdjson::dom::parser parser;
    while (const auto line = lines.next()) {
        out.consumed += line->size();
        take(out.found, parser, *line);
    }
    return out;
}

struct FileState {
    std::int64_t size = 0;
    Totals totals;
};

using Offsets = gojson::Map<FileState>;

void decode_count(gojson::Decode& d, simdjson::dom::element value, double& out) {
    d.float_into(value, out);
}

void decode_fields(gojson::Decode& d, simdjson::dom::element value, gojson::Map<double>& out) {
    std::optional<gojson::Map<double>> held;
    d.map_into(value, held, decode_count);
    out = held.value_or(gojson::Map<double>{});
}

void decode_file_state(gojson::Decode& d, simdjson::dom::element value, FileState& state) {
    d.as_struct(value, [&](simdjson::dom::object object) {
        d.fields(object, "size", [&](simdjson::dom::element size) {
            d.int_into(size, state.size);
        });
        d.fields(object, "totals", [&](simdjson::dom::element totals) {
            std::optional<Totals> decoded(std::move(state.totals));
            d.map_into(totals, decoded, decode_fields);
            state.totals = decoded.value_or(Totals{});
        });
    });
}

std::optional<Offsets> decode_offsets(simdjson::dom::element root) {
    gojson::Decode decode;
    std::optional<Offsets> files;
    decode.as_struct(root, [&](simdjson::dom::object top) {
        decode.fields(top, "files", [&](simdjson::dom::element value) {
            decode.map_into(value, files, decode_file_state);
        });
    });
    if (!decode.ok() || !files) {
        return std::nullopt;
    }
    return files;
}

Offsets load(const std::string& path) {
    if (path.empty()) {
        return {};
    }
    const auto raw = files::read(path);
    if (!raw) {
        return {};
    }
    simdjson::dom::parser parser;
    simdjson::dom::element root;
    if (parser.parse(*raw).get(root) != simdjson::SUCCESS) {
        return {};
    }
    return decode_offsets(root).value_or(Offsets{});
}

void quote(std::string& out, std::string_view text) {
    constexpr std::string_view hex = "0123456789abcdef";
    constexpr unsigned char control_end = 0x20;
    constexpr int nibble = 4;
    constexpr unsigned char low_nibble = 0x0F;
    out.push_back('"');
    for (const char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        if (c == '"' || c == '\\') {
            out.push_back('\\');
            out.push_back(c);
        } else if (byte < control_end) {
            out.append("\\u00");
            out.push_back(hex[byte >> nibble]);
            out.push_back(hex[byte & low_nibble]);
        } else {
            out.push_back(c);
        }
    }
    out.push_back('"');
}

// The shortest spelling that reads back as the same double, which is what the
// offsets need: they are read back by this program and by nothing else.
void number(std::string& out, double value) {
    constexpr std::size_t longest = 32;
    std::array<char, longest> text{};
    const auto written = std::to_chars(text.data(), text.data() + text.size(), value);
    out.append(text.data(), written.ptr);
}

std::string encode(const Offsets& offsets) {
    std::string out = R"({"files":{)";
    bool first_file = true;
    for (const auto& [path, state] : offsets) {
        out.append(first_file ? "" : ",");
        first_file = false;
        quote(out, path);
        out.append(R"(:{"size":)");
        out.append(std::to_string(state.size));
        out.append(R"(,"totals":{)");
        bool first_model = true;
        for (const auto& [model, fields] : state.totals) {
            out.append(first_model ? "" : ",");
            first_model = false;
            quote(out, model);
            out.append(":{");
            bool first_field = true;
            for (const auto& [field, value] : fields) {
                out.append(first_field ? "" : ",");
                first_field = false;
                quote(out, field);
                out.push_back(':');
                number(out, value);
            }
            out.push_back('}');
        }
        out.append("}}");
    }
    out.append("}}");
    return out;
}

// Best effort. Offsets that cannot be written cost a re-sum, which is slow
// rather than wrong.
void save(const std::string& path, const Offsets& offsets) {
    if (path.empty()) {
        return;
    }
    // OWNER ONLY: byte positions into this user's own transcripts, read by
    // nothing but this program. The modes apply to what is created, as
    // os.MkdirAll and os.WriteFile apply them, and leave an existing directory
    // as it is.
    constexpr ::mode_t directory_mode = 0750;
    constexpr ::mode_t file_mode = 0600;
    if (!files::make_directories(gopath::dir(path), directory_mode)) {
        return;
    }
    (void)files::write(path, encode(offsets), file_mode);
}

std::optional<std::int64_t> size_of(const std::string& path) {
    struct stat info{};
    if (::stat(path.c_str(), &info) != 0) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(info.st_size);
}

}  // namespace

std::vector<std::string> transcripts(std::string_view session,
                                     std::string_view root,
                                     const Environment& env) {
    if (session.empty()) {
        return {};
    }
    const std::string base = root.empty() ? projects(env) : std::string(root);
    const std::string file = std::string(session) + ".jsonl";
    const std::vector<std::string> named =
        gopath::glob(gopath::join({base, "*", file}));

    // Scoped to the project the session belongs to: a clear opens its new
    // transcript beside the old one, so the search never leaves that directory.
    std::vector<std::string> pool;
    std::string origin(session);
    if (!named.empty()) {
        pool = gopath::glob(gopath::join({gopath::dir(named[0]), "*.jsonl"}));
        origin = origin_of(named[0]);
    } else {
        pool = gopath::glob(gopath::join({base, "*", "*.jsonl"}));
    }

    // The origin is read before the name is compared, as the Go port orders
    // it, so the cost measured here is the cost of the same algorithm.
    std::vector<std::string> found;
    std::ranges::copy_if(pool, std::back_inserter(found), [&](const std::string& path) {
        return origin_of(path) == origin || gopath::stem(path) == origin;
    });
    std::vector<std::string> subagents;
    for (const std::string& path : found) {
        const std::vector<std::string> matched = gopath::glob(gopath::join(
            {gopath::dir(path), gopath::stem(path), "subagents", "*.jsonl"}));
        subagents.insert(subagents.end(), matched.begin(), matched.end());
    }
    found.insert(found.end(), subagents.begin(), subagents.end());
    return found;
}

std::string offset_path(std::string_view session, const Environment& env) {
    const std::string dir = state_dir(env);
    if (dir.empty()) {
        return {};
    }
    return gopath::join({dir, std::string(session) + ".json"});
}

Totals sum(std::string_view session, std::string_view root, const Environment& env) {
    const std::string offsets_file = offset_path(session, env);
    const Offsets known = load(offsets_file);
    Offsets seen;
    bool changed = false;

    for (const std::string& path : transcripts(session, root, env)) {
        const auto size = size_of(path);
        if (!size) {
            continue;
        }
        const auto previous_entry = known.find(path);
        const FileState previous =
            previous_entry == known.end() ? FileState{} : previous_entry->second;
        if (previous.size == *size) {
            seen.insert_or_assign(path, previous);
            continue;
        }
        // A file that shrank was rotated or replaced, so its offset means
        // nothing against the new one and it is read from the start.
        std::int64_t start = 0;
        Totals so_far;
        if (*size > previous.size) {
            start = previous.size;
            so_far = previous.totals;
        }
        const Scanned read = scan(path, static_cast<std::uint64_t>(start));
        merge(so_far, read.found);
        seen.insert_or_assign(
            path,
            FileState{.size = start + static_cast<std::int64_t>(read.consumed),
                      .totals = std::move(so_far)});
        changed = true;
    }

    if (changed || seen.size() != known.size()) {
        save(offsets_file, seen);
    }
    Totals summed;
    for (const auto& [path, state] : seen) {
        merge(summed, state.totals);
    }
    return summed;
}

}  // namespace infobot::usage
