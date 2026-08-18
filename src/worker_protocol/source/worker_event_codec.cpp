#include "biocore/worker_protocol/worker_event_codec.hpp"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

namespace biocore::worker_protocol {
namespace {

using JsonValue = std::variant<std::nullptr_t, std::string, std::int64_t, double>;
using JsonObject = std::map<std::string, JsonValue, std::less<>>;

void validate_utf8(const std::string_view value) {
    std::size_t index = 0U;
    while (index < value.size()) {
        const auto first = static_cast<unsigned char>(value[index]);
        if (first <= 0x7FU) {
            ++index;
            continue;
        }

        std::size_t length = 0U;
        std::uint32_t code_point = 0U;
        if (first >= 0xC2U && first <= 0xDFU) {
            length = 2U;
            code_point = first & 0x1FU;
        } else if (first >= 0xE0U && first <= 0xEFU) {
            length = 3U;
            code_point = first & 0x0FU;
        } else if (first >= 0xF0U && first <= 0xF4U) {
            length = 4U;
            code_point = first & 0x07U;
        } else {
            throw std::invalid_argument("Worker event JSON is not valid UTF-8");
        }
        if (index + length > value.size()) {
            throw std::invalid_argument("Worker event JSON ends inside a UTF-8 sequence");
        }
        for (std::size_t offset = 1U; offset < length; ++offset) {
            const auto continuation = static_cast<unsigned char>(value[index + offset]);
            if ((continuation & 0xC0U) != 0x80U) {
                throw std::invalid_argument("Worker event JSON is not valid UTF-8");
            }
            code_point = (code_point << 6U) | (continuation & 0x3FU);
        }
        if ((length == 3U && code_point < 0x800U) ||
            (length == 4U && code_point < 0x10000U) ||
            code_point > 0x10FFFFU ||
            (code_point >= 0xD800U && code_point <= 0xDFFFU)) {
            throw std::invalid_argument("Worker event JSON contains an invalid UTF-8 code point");
        }
        index += length;
    }
}

[[nodiscard]] bool is_hex(const char value) noexcept {
    return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') ||
           (value >= 'A' && value <= 'F');
}

[[nodiscard]] std::uint32_t hex_value(const char value) {
    if (value >= '0' && value <= '9') return static_cast<std::uint32_t>(value - '0');
    if (value >= 'a' && value <= 'f') return 10U + static_cast<std::uint32_t>(value - 'a');
    if (value >= 'A' && value <= 'F') return 10U + static_cast<std::uint32_t>(value - 'A');
    throw std::invalid_argument("JSON escape contains a non-hexadecimal digit");
}

void append_utf8(std::string& output, const std::uint32_t code_point) {
    if (code_point <= 0x7FU) {
        output.push_back(static_cast<char>(code_point));
    } else if (code_point <= 0x7FFU) {
        output.push_back(static_cast<char>(0xC0U | (code_point >> 6U)));
        output.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    } else if (code_point <= 0xFFFFU) {
        output.push_back(static_cast<char>(0xE0U | (code_point >> 12U)));
        output.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    } else if (code_point <= 0x10FFFFU) {
        output.push_back(static_cast<char>(0xF0U | (code_point >> 18U)));
        output.push_back(static_cast<char>(0x80U | ((code_point >> 12U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    } else {
        throw std::invalid_argument("JSON Unicode escape is outside the valid range");
    }
}

class Parser final {
public:
    explicit Parser(const std::string_view input) : input_{input} {}

    [[nodiscard]] JsonObject parse_object() {
        skip_space();
        expect('{');
        skip_space();

        JsonObject object;
        if (peek('}')) {
            ++position_;
            finish_document();
            return object;
        }

        for (;;) {
            skip_space();
            const std::string key = parse_string();
            skip_space();
            expect(':');
            skip_space();
            const auto [iterator, inserted] = object.emplace(key, parse_value());
            static_cast<void>(iterator);
            if (!inserted) {
                throw std::invalid_argument("Worker event JSON contains a duplicate field");
            }
            skip_space();
            if (peek('}')) {
                ++position_;
                finish_document();
                return object;
            }
            expect(',');
        }
    }

private:
    [[nodiscard]] JsonValue parse_value() {
        if (position_ >= input_.size()) {
            throw std::invalid_argument("Worker event JSON ended before a value");
        }
        if (input_[position_] == '"') {
            return parse_string();
        }
        if (input_.substr(position_, 4U) == "null") {
            position_ += 4U;
            return nullptr;
        }
        if (input_[position_] == '{' || input_[position_] == '[' ||
            input_[position_] == 't' || input_[position_] == 'f') {
            throw std::invalid_argument("Worker event JSON contains an unsupported value type");
        }
        return parse_number();
    }

    [[nodiscard]] JsonValue parse_number() {
        const std::size_t begin = position_;
        if (peek('-')) ++position_;
        if (position_ >= input_.size() || input_[position_] < '0' || input_[position_] > '9') {
            throw std::invalid_argument("Worker event JSON number is invalid");
        }
        if (input_[position_] == '0') {
            ++position_;
        } else {
            while (position_ < input_.size() && input_[position_] >= '0' &&
                   input_[position_] <= '9') {
                ++position_;
            }
        }

        bool floating = false;
        if (peek('.')) {
            floating = true;
            ++position_;
            const std::size_t fraction_begin = position_;
            while (position_ < input_.size() && input_[position_] >= '0' &&
                   input_[position_] <= '9') {
                ++position_;
            }
            if (fraction_begin == position_) {
                throw std::invalid_argument("Worker event JSON fraction is invalid");
            }
        }
        if (peek('e') || peek('E')) {
            floating = true;
            ++position_;
            if (peek('+') || peek('-')) ++position_;
            const std::size_t exponent_begin = position_;
            while (position_ < input_.size() && input_[position_] >= '0' &&
                   input_[position_] <= '9') {
                ++position_;
            }
            if (exponent_begin == position_) {
                throw std::invalid_argument("Worker event JSON exponent is invalid");
            }
        }

        const std::string_view token = input_.substr(begin, position_ - begin);
        if (!floating) {
            std::int64_t value = 0;
            const auto [end, error] = std::from_chars(token.data(), token.data() + token.size(), value);
            if (error != std::errc{} || end != token.data() + token.size()) {
                throw std::invalid_argument("Worker event JSON integer is outside the supported range");
            }
            return value;
        }

        double value = 0.0;
        const auto [end, error] = std::from_chars(
            token.data(), token.data() + token.size(), value, std::chars_format::general
        );
        if (error != std::errc{} || end != token.data() + token.size() || !std::isfinite(value)) {
            throw std::invalid_argument("Worker event JSON number is invalid or non-finite");
        }
        return value;
    }

    [[nodiscard]] std::string parse_string() {
        expect('"');
        std::string output;
        while (position_ < input_.size()) {
            const unsigned char character = static_cast<unsigned char>(input_[position_++]);
            if (character == '"') {
                return output;
            }
            if (character < 0x20U) {
                throw std::invalid_argument("Worker event JSON string contains an unescaped control character");
            }
            if (character != '\\') {
                output.push_back(static_cast<char>(character));
                continue;
            }
            if (position_ >= input_.size()) {
                throw std::invalid_argument("Worker event JSON escape is incomplete");
            }
            const char escape = input_[position_++];
            switch (escape) {
                case '"': output.push_back('"'); break;
                case '\\': output.push_back('\\'); break;
                case '/': output.push_back('/'); break;
                case 'b': output.push_back('\b'); break;
                case 'f': output.push_back('\f'); break;
                case 'n': output.push_back('\n'); break;
                case 'r': output.push_back('\r'); break;
                case 't': output.push_back('\t'); break;
                case 'u': {
                    std::uint32_t first = parse_hex_quad();
                    if (first >= 0xD800U && first <= 0xDBFFU) {
                        if (position_ + 2U > input_.size() || input_[position_] != '\\' ||
                            input_[position_ + 1U] != 'u') {
                            throw std::invalid_argument("Worker event JSON high surrogate lacks a pair");
                        }
                        position_ += 2U;
                        const std::uint32_t second = parse_hex_quad();
                        if (second < 0xDC00U || second > 0xDFFFU) {
                            throw std::invalid_argument("Worker event JSON surrogate pair is invalid");
                        }
                        first = 0x10000U + ((first - 0xD800U) << 10U) + (second - 0xDC00U);
                    } else if (first >= 0xDC00U && first <= 0xDFFFU) {
                        throw std::invalid_argument("Worker event JSON contains an unpaired low surrogate");
                    }
                    append_utf8(output, first);
                    break;
                }
                default:
                    throw std::invalid_argument("Worker event JSON contains an unsupported escape");
            }
        }
        throw std::invalid_argument("Worker event JSON string is unterminated");
    }

    [[nodiscard]] std::uint32_t parse_hex_quad() {
        if (position_ + 4U > input_.size()) {
            throw std::invalid_argument("Worker event JSON Unicode escape is incomplete");
        }
        std::uint32_t value = 0U;
        for (int index = 0; index < 4; ++index) {
            const char digit = input_[position_++];
            if (!is_hex(digit)) {
                throw std::invalid_argument("Worker event JSON Unicode escape is invalid");
            }
            value = (value << 4U) | hex_value(digit);
        }
        return value;
    }

    void skip_space() {
        while (position_ < input_.size() &&
               (input_[position_] == ' ' || input_[position_] == '\t' ||
                input_[position_] == '\r' || input_[position_] == '\n')) {
            ++position_;
        }
    }

    void expect(const char expected) {
        if (position_ >= input_.size() || input_[position_] != expected) {
            throw std::invalid_argument("Worker event JSON syntax is invalid");
        }
        ++position_;
    }

    [[nodiscard]] bool peek(const char expected) const noexcept {
        return position_ < input_.size() && input_[position_] == expected;
    }

    void finish_document() {
        skip_space();
        if (position_ != input_.size()) {
            throw std::invalid_argument("Worker event JSON contains trailing data");
        }
    }

    std::string_view input_;
    std::size_t position_{0U};
};

[[nodiscard]] std::string escape_json(const std::string_view value) {
    static constexpr char hexadecimal[] = "0123456789abcdef";
    std::string output;
    output.reserve(value.size() + 8U);
    for (const char raw_character : value) {
        const auto character = static_cast<unsigned char>(raw_character);
        switch (character) {
            case '"': output += "\\\""; break;
            case '\\': output += "\\\\"; break;
            case '\b': output += "\\b"; break;
            case '\f': output += "\\f"; break;
            case '\n': output += "\\n"; break;
            case '\r': output += "\\r"; break;
            case '\t': output += "\\t"; break;
            default:
                if (character < 0x20U) {
                    output += "\\u00";
                    output.push_back(hexadecimal[(character >> 4U) & 0x0FU]);
                    output.push_back(hexadecimal[character & 0x0FU]);
                } else {
                    output.push_back(static_cast<char>(character));
                }
        }
    }
    return output;
}

void append_string_field(
    std::ostringstream& output,
    const std::string_view name,
    const std::string_view value
) {
    output << ",\"" << name << "\":\"" << escape_json(value) << '"';
}

[[nodiscard]] const JsonValue& require_field(
    const JsonObject& object,
    const std::string_view name
) {
    const auto iterator = object.find(name);
    if (iterator == object.end()) {
        throw std::invalid_argument("Worker event JSON is missing field: " + std::string{name});
    }
    return iterator->second;
}

[[nodiscard]] std::string require_string(
    const JsonObject& object,
    const std::string_view name
) {
    const JsonValue& value = require_field(object, name);
    if (const auto* text = std::get_if<std::string>(&value)) {
        return *text;
    }
    throw std::invalid_argument("Worker event JSON field must be a string: " + std::string{name});
}

[[nodiscard]] std::int64_t require_integer(
    const JsonObject& object,
    const std::string_view name
) {
    const JsonValue& value = require_field(object, name);
    if (const auto* integer = std::get_if<std::int64_t>(&value)) {
        return *integer;
    }
    throw std::invalid_argument("Worker event JSON field must be an integer: " + std::string{name});
}

[[nodiscard]] double require_number(const JsonObject& object, const std::string_view name) {
    const JsonValue& value = require_field(object, name);
    if (const auto* number = std::get_if<double>(&value)) return *number;
    if (const auto* integer = std::get_if<std::int64_t>(&value)) {
        return static_cast<double>(*integer);
    }
    throw std::invalid_argument("Worker event JSON field must be numeric: " + std::string{name});
}

[[nodiscard]] std::optional<std::string> optional_string(
    const JsonObject& object,
    const std::string_view name
) {
    const auto iterator = object.find(name);
    if (iterator == object.end() || std::holds_alternative<std::nullptr_t>(iterator->second)) {
        return std::nullopt;
    }
    if (const auto* text = std::get_if<std::string>(&iterator->second)) return *text;
    throw std::invalid_argument("Worker event JSON optional field must be string or null: " + std::string{name});
}

void require_only_fields(
    const JsonObject& object,
    const std::initializer_list<std::string_view> allowed
) {
    for (const auto& [name, value] : object) {
        static_cast<void>(value);
        bool found = false;
        for (const std::string_view candidate : allowed) {
            if (name == candidate) {
                found = true;
                break;
            }
        }
        if (!found) {
            throw std::invalid_argument("Worker event JSON contains unknown field: " + name);
        }
    }
}

[[nodiscard]] std::uint64_t to_sequence(const std::int64_t value) {
    if (value <= 0) {
        throw std::invalid_argument("Worker event sequence must be positive");
    }
    return static_cast<std::uint64_t>(value);
}

[[nodiscard]] std::uint32_t to_protocol_version(const std::int64_t value) {
    if (value < 0 || value > static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max())) {
        throw std::invalid_argument("Worker event protocol version is outside the supported range");
    }
    return static_cast<std::uint32_t>(value);
}

}  // namespace

std::string serialize_worker_event(const WorkerEvent& event) {
    validate_worker_event(event);

    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << "{\"protocolVersion\":" << event.protocol_version;
    append_string_field(output, "type", to_string(event.type));
    append_string_field(output, "jobId", event.job_id);
    output << ",\"jobRevision\":" << event.job_revision
           << ",\"sequence\":" << event.sequence;
    append_string_field(output, "timestampUtc", event.timestamp_utc);

    switch (event.type) {
        case MessageType::progress:
            output << ",\"progress\":" << std::setprecision(17) << *event.progress;
            if (event.active_step_id.has_value()) {
                append_string_field(output, "activeStepId", *event.active_step_id);
            }
            break;
        case MessageType::log:
            append_string_field(output, "level", to_string(*event.log_level));
            append_string_field(output, "component", *event.component);
            append_string_field(output, "message", *event.message);
            break;
        case MessageType::artifact:
            append_string_field(output, "stepId", *event.artifact_step_id);
            append_string_field(output, "outputPort", *event.artifact_output_port);
            append_string_field(output, "pluginId", *event.artifact_plugin_id);
            append_string_field(output, "pluginVersion", *event.artifact_plugin_version);
            append_string_field(output, "moduleId", *event.artifact_module_id);
            append_string_field(output, "fileType", *event.artifact_file_type);
            append_string_field(output, "relativeProjectPath", *event.artifact_relative_project_path);
            break;
        case MessageType::completed:
            output << ",\"exitCode\":" << *event.exit_code;
            break;
        case MessageType::failed:
            output << ",\"exitCode\":" << *event.exit_code;
            append_string_field(output, "message", *event.message);
            break;
        case MessageType::ready:
        case MessageType::heartbeat:
            break;
        case MessageType::cancel:
        case MessageType::shutdown:
        case MessageType::ping:
        case MessageType::pong:
            throw std::invalid_argument("Control messages cannot be serialized as worker events");
    }
    output << '}';

    std::string result = output.str();
    if (result.size() > maximum_event_line_bytes) {
        throw std::invalid_argument("Serialized worker event exceeds the maximum line size");
    }
    return result;
}

WorkerEvent parse_worker_event(const std::string_view json_line) {
    if (json_line.empty()) {
        throw std::invalid_argument("Worker event line must not be blank");
    }
    if (json_line.size() > maximum_event_line_bytes) {
        throw std::invalid_argument("Worker event line exceeds the maximum size");
    }
    if (json_line.find('\0') != std::string_view::npos) {
        throw std::invalid_argument("Worker event line must not contain NUL characters");
    }
    validate_utf8(json_line);

    const JsonObject object = Parser{json_line}.parse_object();
    const std::string type_text = require_string(object, "type");
    const auto type = message_type_from_string(type_text);
    if (!type.has_value()) {
        throw std::invalid_argument("Worker event type is unknown");
    }

    WorkerEvent event{
        .protocol_version = to_protocol_version(require_integer(object, "protocolVersion")),
        .type = *type,
        .job_id = require_string(object, "jobId"),
        .job_revision = require_integer(object, "jobRevision"),
        .sequence = to_sequence(require_integer(object, "sequence")),
        .timestamp_utc = require_string(object, "timestampUtc"),
        .progress = std::nullopt,
        .active_step_id = std::nullopt,
        .log_level = std::nullopt,
        .component = std::nullopt,
        .message = std::nullopt,
        .artifact_step_id = std::nullopt,
        .artifact_output_port = std::nullopt,
        .artifact_plugin_id = std::nullopt,
        .artifact_plugin_version = std::nullopt,
        .artifact_module_id = std::nullopt,
        .artifact_file_type = std::nullopt,
        .artifact_relative_project_path = std::nullopt,
        .exit_code = std::nullopt,
    };

    const std::initializer_list<std::string_view> base_fields{
        "protocolVersion", "type", "jobId", "jobRevision", "sequence", "timestampUtc"
    };
    switch (event.type) {
        case MessageType::ready:
        case MessageType::heartbeat:
            require_only_fields(object, base_fields);
            break;
        case MessageType::progress:
            require_only_fields(object, {
                "protocolVersion", "type", "jobId", "jobRevision", "sequence",
                "timestampUtc", "progress", "activeStepId"
            });
            event.progress = require_number(object, "progress");
            event.active_step_id = optional_string(object, "activeStepId");
            break;
        case MessageType::log: {
            require_only_fields(object, {
                "protocolVersion", "type", "jobId", "jobRevision", "sequence",
                "timestampUtc", "level", "component", "message"
            });
            const auto level = worker_log_level_from_string(require_string(object, "level"));
            if (!level.has_value()) {
                throw std::invalid_argument("Worker log level is unknown");
            }
            event.log_level = *level;
            event.component = require_string(object, "component");
            event.message = require_string(object, "message");
            break;
        }
        case MessageType::artifact:
            require_only_fields(object, {
                "protocolVersion", "type", "jobId", "jobRevision", "sequence",
                "timestampUtc", "stepId", "outputPort", "pluginId", "pluginVersion",
                "moduleId", "fileType", "relativeProjectPath"
            });
            event.artifact_step_id = require_string(object, "stepId");
            event.artifact_output_port = require_string(object, "outputPort");
            event.artifact_plugin_id = require_string(object, "pluginId");
            event.artifact_plugin_version = require_string(object, "pluginVersion");
            event.artifact_module_id = require_string(object, "moduleId");
            event.artifact_file_type = require_string(object, "fileType");
            event.artifact_relative_project_path = require_string(object, "relativeProjectPath");
            break;
        case MessageType::completed:
            require_only_fields(object, {
                "protocolVersion", "type", "jobId", "jobRevision", "sequence",
                "timestampUtc", "exitCode"
            });
            event.exit_code = require_integer(object, "exitCode");
            break;
        case MessageType::failed:
            require_only_fields(object, {
                "protocolVersion", "type", "jobId", "jobRevision", "sequence",
                "timestampUtc", "exitCode", "message"
            });
            event.exit_code = require_integer(object, "exitCode");
            event.message = require_string(object, "message");
            break;
        case MessageType::cancel:
        case MessageType::shutdown:
        case MessageType::ping:
        case MessageType::pong:
            throw std::invalid_argument("Worker stdout contains a control message");
    }

    validate_worker_event(event);
    return event;
}

}  // namespace biocore::worker_protocol
