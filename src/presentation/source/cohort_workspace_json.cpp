#include "biocore/presentation/cohort_workspace_json.hpp"

#include <charconv>
#include <cstdint>
#include <map>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace biocore::presentation {
namespace {


struct JsonValue final {
    using Object = std::map<std::string, JsonValue, std::less<>>;
    using Array = std::vector<JsonValue>;
    std::variant<std::nullptr_t, std::string, std::int64_t, Object, Array> value;
};

class JsonParser final {
public:
    explicit JsonParser(const std::string_view text) : text_{text} {}

    [[nodiscard]] JsonValue parse() {
        skip_space();
        JsonValue result = parse_value(0U);
        skip_space();
        if (position_ != text_.size()) fail("Trailing cohort JSON data");
        return result;
    }

private:
    static constexpr std::size_t maximum_depth = 12U;
    static constexpr std::size_t maximum_collection = 512U;

    [[noreturn]] void fail(const char* message) const {
        throw std::invalid_argument{message};
    }

    void skip_space() {
        while (position_ < text_.size() &&
               (text_[position_] == ' ' || text_[position_] == '\n' ||
                text_[position_] == '\r' || text_[position_] == '\t')) {
            ++position_;
        }
    }

    [[nodiscard]] bool consume(const char expected) {
        if (position_ < text_.size() && text_[position_] == expected) {
            ++position_;
            return true;
        }
        return false;
    }

    void require(const char expected) {
        if (!consume(expected)) fail("Malformed cohort JSON");
    }

    [[nodiscard]] bool literal(const std::string_view value) {
        if (text_.substr(position_, value.size()) != value) return false;
        position_ += value.size();
        return true;
    }

    [[nodiscard]] std::uint32_t hex4() {
        if (position_ + 4U > text_.size()) fail("Truncated cohort JSON Unicode escape");
        std::uint32_t result = 0U;
        for (std::size_t index = 0U; index < 4U; ++index) {
            const char character = text_[position_++];
            result <<= 4U;
            if (character >= '0' && character <= '9') {
                result |= static_cast<std::uint32_t>(character - '0');
            } else if (character >= 'a' && character <= 'f') {
                result |= static_cast<std::uint32_t>(character - 'a' + 10);
            } else if (character >= 'A' && character <= 'F') {
                result |= static_cast<std::uint32_t>(character - 'A' + 10);
            } else {
                fail("Invalid cohort JSON Unicode escape");
            }
        }
        return result;
    }

    static void append_utf8(std::string& output, const std::uint32_t codepoint) {
        if (codepoint <= 0x7fU) {
            output.push_back(static_cast<char>(codepoint));
        } else if (codepoint <= 0x7ffU) {
            output.push_back(static_cast<char>(0xc0U | (codepoint >> 6U)));
            output.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
        } else if (codepoint <= 0xffffU) {
            output.push_back(static_cast<char>(0xe0U | (codepoint >> 12U)));
            output.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3fU)));
            output.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
        } else if (codepoint <= 0x10ffffU) {
            output.push_back(static_cast<char>(0xf0U | (codepoint >> 18U)));
            output.push_back(static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3fU)));
            output.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3fU)));
            output.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
        } else {
            throw std::invalid_argument{"Invalid cohort JSON Unicode codepoint"};
        }
    }

    [[nodiscard]] std::string parse_string() {
        require('"');
        std::string output;
        while (position_ < text_.size()) {
            const char character = text_[position_++];
            if (character == '"') return output;
            if (static_cast<unsigned char>(character) < 0x20U) {
                fail("Control character in cohort JSON string");
            }
            if (character != '\\') {
                output.push_back(character);
                continue;
            }
            if (position_ >= text_.size()) fail("Truncated cohort JSON escape");
            const char escaped = text_[position_++];
            switch (escaped) {
                case '"': output.push_back('"'); break;
                case '\\': output.push_back('\\'); break;
                case '/': output.push_back('/'); break;
                case 'b': output.push_back('\b'); break;
                case 'f': output.push_back('\f'); break;
                case 'n': output.push_back('\n'); break;
                case 'r': output.push_back('\r'); break;
                case 't': output.push_back('\t'); break;
                case 'u': {
                    std::uint32_t codepoint = hex4();
                    if (codepoint >= 0xd800U && codepoint <= 0xdbffU) {
                        if (position_ + 2U > text_.size() ||
                            text_[position_] != '\\' || text_[position_ + 1U] != 'u') {
                            fail("Unpaired cohort JSON high surrogate");
                        }
                        position_ += 2U;
                        const std::uint32_t low = hex4();
                        if (low < 0xdc00U || low > 0xdfffU) {
                            fail("Invalid cohort JSON surrogate pair");
                        }
                        codepoint = 0x10000U +
                            ((codepoint - 0xd800U) << 10U) + (low - 0xdc00U);
                    } else if (codepoint >= 0xdc00U && codepoint <= 0xdfffU) {
                        fail("Unpaired cohort JSON low surrogate");
                    }
                    append_utf8(output, codepoint);
                    break;
                }
                default: fail("Unsupported cohort JSON escape");
            }
        }
        fail("Unterminated cohort JSON string");
    }

    [[nodiscard]] std::int64_t parse_integer() {
        const std::size_t start = position_;
        if (consume('-') && position_ >= text_.size()) fail("Invalid cohort JSON integer");
        if (consume('0')) {
            if (position_ < text_.size() &&
                text_[position_] >= '0' && text_[position_] <= '9') {
                fail("Cohort JSON integer has a leading zero");
            }
        } else {
            if (position_ >= text_.size() ||
                text_[position_] < '1' || text_[position_] > '9') {
                fail("Invalid cohort JSON integer");
            }
            while (position_ < text_.size() &&
                   text_[position_] >= '0' && text_[position_] <= '9') {
                ++position_;
            }
        }
        if (position_ < text_.size() &&
            (text_[position_] == '.' || text_[position_] == 'e' ||
             text_[position_] == 'E')) {
            fail("Cohort JSON numbers must be integers");
        }
        std::int64_t result{};
        const auto parsed = std::from_chars(
            text_.data() + start, text_.data() + position_, result
        );
        if (parsed.ec != std::errc{} || parsed.ptr != text_.data() + position_) {
            fail("Cohort JSON integer is out of range");
        }
        return result;
    }

    [[nodiscard]] JsonValue parse_array(const std::size_t depth) {
        require('[');
        skip_space();
        JsonValue::Array values;
        if (consume(']')) return JsonValue{std::move(values)};
        for (;;) {
            if (values.size() >= maximum_collection) fail("Cohort JSON array is too large");
            values.push_back(parse_value(depth + 1U));
            skip_space();
            if (consume(']')) break;
            require(',');
            skip_space();
        }
        return JsonValue{std::move(values)};
    }

    [[nodiscard]] JsonValue parse_object(const std::size_t depth) {
        require('{');
        skip_space();
        JsonValue::Object values;
        if (consume('}')) return JsonValue{std::move(values)};
        for (;;) {
            if (values.size() >= maximum_collection) fail("Cohort JSON object is too large");
            const std::string key = parse_string();
            if (values.contains(key)) fail("Duplicate cohort JSON object key");
            skip_space();
            require(':');
            skip_space();
            values.emplace(key, parse_value(depth + 1U));
            skip_space();
            if (consume('}')) break;
            require(',');
            skip_space();
        }
        return JsonValue{std::move(values)};
    }

    [[nodiscard]] JsonValue parse_value(const std::size_t depth) {
        if (depth > maximum_depth) fail("Cohort JSON nesting is too deep");
        skip_space();
        if (position_ >= text_.size()) fail("Cohort JSON value is missing");
        const char character = text_[position_];
        if (character == '"') return JsonValue{parse_string()};
        if (character == '{') return parse_object(depth);
        if (character == '[') return parse_array(depth);
        if (character == '-' || (character >= '0' && character <= '9')) {
            return JsonValue{parse_integer()};
        }
        if (literal("null")) return JsonValue{nullptr};
        fail("Unsupported cohort JSON value");
    }

    std::string_view text_;
    std::size_t position_{0U};
};

[[nodiscard]] const JsonValue::Object& as_object(
    const JsonValue& value,
    const char* what
) {
    const auto* result = std::get_if<JsonValue::Object>(&value.value);
    if (result == nullptr) throw std::invalid_argument{std::string{what} + " must be an object"};
    return *result;
}

[[nodiscard]] const JsonValue::Array& as_array(
    const JsonValue& value,
    const char* what
) {
    const auto* result = std::get_if<JsonValue::Array>(&value.value);
    if (result == nullptr) throw std::invalid_argument{std::string{what} + " must be an array"};
    return *result;
}

[[nodiscard]] const JsonValue& required(
    const JsonValue::Object& object,
    const std::string_view key
) {
    const auto found = object.find(key);
    if (found == object.end()) {
        throw std::invalid_argument{"Missing cohort JSON field: " + std::string{key}};
    }
    return found->second;
}

[[nodiscard]] std::string string_value(
    const JsonValue& value,
    const char* what,
    const std::size_t maximum
) {
    const auto* result = std::get_if<std::string>(&value.value);
    if (result == nullptr || result->empty() || result->size() > maximum) {
        throw std::invalid_argument{std::string{what} + " must be a non-empty bounded string"};
    }
    return *result;
}

void reject_unknown(
    const JsonValue::Object& object,
    const std::vector<std::string_view>& allowed
) {
    for (const auto& [key, unused] : object) {
        static_cast<void>(unused);
        if (std::ranges::find(allowed, key) == allowed.end()) {
            throw std::invalid_argument{"Unknown cohort JSON field: " + key};
        }
    }
}

[[nodiscard]] std::optional<std::string> optional_string(
    const JsonValue::Object& object,
    const std::string_view key,
    const std::size_t maximum
) {
    const auto found = object.find(key);
    if (found == object.end() ||
        std::holds_alternative<std::nullptr_t>(found->second.value)) {
        return std::nullopt;
    }
    return string_value(found->second, "optional cohort string", maximum);
}

[[nodiscard]] application::CohortMemberDraft parse_member(
    const JsonValue& value
) {
    const auto& object = as_object(value, "cohort member");
    reject_unknown(
        object,
        {"sampleId", "biologicalUnitId", "group", "disposition", "exclusionReason"}
    );
    const std::string group_text =
        string_value(required(object, "group"), "group", 32U);
    const auto group = application::cohort_group_from_string(group_text);
    if (!group.has_value()) {
        throw std::invalid_argument{"group must be case, control, or unassigned"};
    }

    application::CohortMemberDisposition disposition =
        application::CohortMemberDisposition::included;
    if (const auto found = object.find("disposition"); found != object.end()) {
        const std::string text = string_value(found->second, "disposition", 32U);
        const auto parsed = application::cohort_member_disposition_from_string(text);
        if (!parsed.has_value()) {
            throw std::invalid_argument{"disposition must be included or excluded"};
        }
        disposition = *parsed;
    }

    return application::CohortMemberDraft{
        .sample_id = string_value(required(object, "sampleId"), "sampleId", 128U),
        .biological_unit_id = string_value(
            required(object, "biologicalUnitId"), "biologicalUnitId", 128U
        ),
        .group = *group,
        .disposition = disposition,
        .exclusion_reason = optional_string(object, "exclusionReason", 512U),
    };
}

[[nodiscard]] std::vector<application::CohortMemberDraft> parse_members(
    const JsonValue& value
) {
    const auto& values = as_array(value, "members");
    if (values.empty() || values.size() > 100U) {
        throw std::invalid_argument{"members must contain between 1 and 100 entries"};
    }
    std::vector<application::CohortMemberDraft> result;
    result.reserve(values.size());
    for (const auto& item : values) result.push_back(parse_member(item));
    return result;
}

[[nodiscard]] std::string quote(const std::string_view value) {
    std::string out{"\""};
    out.reserve(value.size() + 2U);
    for (const char raw_character : value) {
        const auto character = static_cast<unsigned char>(raw_character);
        switch (character) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (character < 0x20U) {
                    static constexpr char hex[] = "0123456789abcdef";
                    out += "\\u00";
                    out.push_back(hex[(character >> 4U) & 0x0fU]);
                    out.push_back(hex[character & 0x0fU]);
                } else {
                    out.push_back(static_cast<char>(character));
                }
        }
    }
    out.push_back('"');
    return out;
}

[[nodiscard]] std::string optional_string(
    const std::optional<std::string>& value
) {
    return value.has_value() ? quote(*value) : "null";
}

[[nodiscard]] std::string render_member(
    const application::CohortMemberSnapshot& member
) {
    return "{\"sampleId\":" + quote(member.sample_id) +
           ",\"displayName\":" + quote(member.sample_display_name) +
           ",\"sampleGroupMetadata\":" + quote(member.sample_group_metadata) +
           ",\"biologicalUnitId\":" + quote(member.biological_unit_id) +
           ",\"group\":" + quote(application::to_string(member.group)) +
           ",\"disposition\":" + quote(application::to_string(member.disposition)) +
           ",\"exclusionReason\":" + optional_string(member.exclusion_reason) + "}";
}

}  // namespace

std::string render_cohort_definition(
    const application::CohortDefinition& cohort
) {
    std::string body =
        "{\"projectId\":" + quote(cohort.project_id) +
        ",\"cohortId\":" + quote(cohort.cohort_id) +
        ",\"name\":" + quote(cohort.name) +
        ",\"currentRevision\":" + std::to_string(cohort.current_revision) +
        ",\"createdAtUtc\":" + quote(cohort.created_at_utc) +
        ",\"updatedAtUtc\":" + quote(cohort.updated_at_utc) +
        ",\"revision\":{\"revision\":" +
        std::to_string(cohort.revision.revision) +
        ",\"parentRevision\":";
    if (cohort.revision.parent_revision.has_value()) {
        body += std::to_string(*cohort.revision.parent_revision);
    } else {
        body += "null";
    }
    body += ",\"createdAtUtc\":" + quote(cohort.revision.created_at_utc) +
            ",\"members\":[";
    for (std::size_t index = 0U; index < cohort.revision.members.size(); ++index) {
        if (index != 0U) body += ',';
        body += render_member(cohort.revision.members[index]);
    }
    body += "]}}";
    return body;
}

std::string render_cohort_list(
    const std::vector<application::CohortDefinition>& cohorts
) {
    std::string body{"{\"cohorts\":["};
    for (std::size_t index = 0U; index < cohorts.size(); ++index) {
        if (index != 0U) body += ',';
        body += render_cohort_definition(cohorts[index]);
    }
    body += "]}";
    return body;
}

ParsedCreateCohortRequest parse_create_cohort_request(
    const std::string_view body
) {
    const auto parsed = JsonParser{body}.parse();
    const auto& object = as_object(parsed, "create cohort request");
    reject_unknown(object, {"name", "members"});
    return {
        .name = string_value(required(object, "name"), "name", 256U),
        .members = parse_members(required(object, "members")),
    };
}

ParsedReviseCohortRequest parse_revise_cohort_request(
    const std::string_view body
) {
    const auto parsed = JsonParser{body}.parse();
    const auto& object = as_object(parsed, "revise cohort request");
    reject_unknown(object, {"expectedRevision", "members"});
    const auto* revision = std::get_if<std::int64_t>(
        &required(object, "expectedRevision").value
    );
    if (revision == nullptr || *revision <= 0 ||
        static_cast<std::uint64_t>(*revision) > 0xffffffffULL) {
        throw std::invalid_argument{
            "expectedRevision must be a positive 32-bit integer"
        };
    }
    return {
        .expected_revision = static_cast<std::uint32_t>(*revision),
        .members = parse_members(required(object, "members")),
    };
}


}  // namespace biocore::presentation
