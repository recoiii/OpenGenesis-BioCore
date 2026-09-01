#include "biocore/pipeline_protocol/pipeline_document_codec.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace biocore::pipeline_protocol {
namespace {

constexpr std::size_t maximum_json_depth = 16U;
constexpr std::size_t maximum_steps = 256U;
constexpr std::size_t maximum_dependencies = 64U;
constexpr std::size_t maximum_text_length = 32U * 1024U;

struct JsonValue;
using JsonArray = std::vector<JsonValue>;
using JsonObject = std::map<std::string, JsonValue, std::less<>>;

struct JsonValue final {
    using Storage = std::variant<std::nullptr_t, std::string, std::int64_t, double, JsonArray, JsonObject>;
    Storage storage;
};

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
            throw std::invalid_argument("Pipeline JSON is not valid UTF-8");
        }
        if (index + length > value.size()) {
            throw std::invalid_argument("Pipeline JSON ends inside a UTF-8 sequence");
        }
        for (std::size_t offset = 1U; offset < length; ++offset) {
            const auto continuation = static_cast<unsigned char>(value[index + offset]);
            if ((continuation & 0xC0U) != 0x80U) {
                throw std::invalid_argument("Pipeline JSON is not valid UTF-8");
            }
            code_point = (code_point << 6U) | (continuation & 0x3FU);
        }
        if ((length == 3U && code_point < 0x800U) ||
            (length == 4U && code_point < 0x10000U) || code_point > 0x10FFFFU ||
            (code_point >= 0xD800U && code_point <= 0xDFFFU)) {
            throw std::invalid_argument("Pipeline JSON contains an invalid UTF-8 code point");
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
    throw std::invalid_argument("Pipeline JSON escape contains a non-hexadecimal digit");
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
        throw std::invalid_argument("Pipeline JSON Unicode escape is outside the valid range");
    }
}

class Parser final {
public:
    explicit Parser(const std::string_view input) : input_{input} {}

    [[nodiscard]] JsonValue parse_document() {
        skip_space();
        JsonValue value = parse_value(0U);
        skip_space();
        if (position_ != input_.size()) {
            throw std::invalid_argument("Pipeline JSON contains trailing data");
        }
        return value;
    }

private:
    [[nodiscard]] JsonValue parse_value(const std::size_t depth) {
        if (depth > maximum_json_depth) {
            throw std::invalid_argument("Pipeline JSON nesting exceeds the supported depth");
        }
        if (position_ >= input_.size()) {
            throw std::invalid_argument("Pipeline JSON ended before a value");
        }
        switch (input_[position_]) {
            case '{': return JsonValue{parse_object(depth + 1U)};
            case '[': return JsonValue{parse_array(depth + 1U)};
            case '"': return JsonValue{parse_string()};
            default: break;
        }
        if (input_.substr(position_, 4U) == "null") {
            position_ += 4U;
            return JsonValue{nullptr};
        }
        if (input_.substr(position_, 4U) == "true" || input_.substr(position_, 5U) == "false") {
            throw std::invalid_argument("Pipeline JSON booleans are not supported by schema v1");
        }
        return JsonValue{parse_number()};
    }

    [[nodiscard]] JsonObject parse_object(const std::size_t depth) {
        expect('{');
        skip_space();
        JsonObject object;
        if (peek('}')) {
            ++position_;
            return object;
        }
        for (;;) {
            skip_space();
            if (!peek('"')) {
                throw std::invalid_argument("Pipeline JSON object key must be a string");
            }
            std::string key = parse_string();
            skip_space();
            expect(':');
            skip_space();
            const auto [iterator, inserted] = object.emplace(
                std::move(key), parse_value(depth)
            );
            static_cast<void>(iterator);
            if (!inserted) {
                throw std::invalid_argument("Pipeline JSON contains a duplicate field");
            }
            skip_space();
            if (peek('}')) {
                ++position_;
                return object;
            }
            expect(',');
        }
    }

    [[nodiscard]] JsonArray parse_array(const std::size_t depth) {
        expect('[');
        skip_space();
        JsonArray array;
        if (peek(']')) {
            ++position_;
            return array;
        }
        for (;;) {
            skip_space();
            array.push_back(parse_value(depth));
            skip_space();
            if (peek(']')) {
                ++position_;
                return array;
            }
            expect(',');
        }
    }

    [[nodiscard]] JsonValue::Storage parse_number() {
        const std::size_t begin = position_;
        if (peek('-')) ++position_;
        if (position_ >= input_.size() || input_[position_] < '0' || input_[position_] > '9') {
            throw std::invalid_argument("Pipeline JSON number is invalid");
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
                throw std::invalid_argument("Pipeline JSON fraction is invalid");
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
                throw std::invalid_argument("Pipeline JSON exponent is invalid");
            }
        }
        const std::string_view token = input_.substr(begin, position_ - begin);
        if (!floating) {
            std::int64_t integer = 0;
            const auto [end, error] = std::from_chars(
                token.data(), token.data() + token.size(), integer
            );
            if (error != std::errc{} || end != token.data() + token.size()) {
                throw std::invalid_argument("Pipeline JSON integer is outside the supported range");
            }
            return integer;
        }
        double number = 0.0;
        const auto [end, error] = std::from_chars(
            token.data(), token.data() + token.size(), number, std::chars_format::general
        );
        if (error != std::errc{} || end != token.data() + token.size() ||
            !std::isfinite(number)) {
            throw std::invalid_argument("Pipeline JSON number is invalid or non-finite");
        }
        return number;
    }

    [[nodiscard]] std::string parse_string() {
        expect('"');
        std::string output;
        while (position_ < input_.size()) {
            const auto character = static_cast<unsigned char>(input_[position_++]);
            if (character == '"') {
                if (output.size() > maximum_text_length) {
                    throw std::invalid_argument("Pipeline JSON string exceeds the maximum length");
                }
                return output;
            }
            if (character < 0x20U) {
                throw std::invalid_argument("Pipeline JSON string contains an unescaped control character");
            }
            if (character != '\\') {
                output.push_back(static_cast<char>(character));
                continue;
            }
            if (position_ >= input_.size()) {
                throw std::invalid_argument("Pipeline JSON escape is incomplete");
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
                            throw std::invalid_argument("Pipeline JSON high surrogate lacks a pair");
                        }
                        position_ += 2U;
                        const std::uint32_t second = parse_hex_quad();
                        if (second < 0xDC00U || second > 0xDFFFU) {
                            throw std::invalid_argument("Pipeline JSON surrogate pair is invalid");
                        }
                        first = 0x10000U + ((first - 0xD800U) << 10U) +
                                (second - 0xDC00U);
                    } else if (first >= 0xDC00U && first <= 0xDFFFU) {
                        throw std::invalid_argument("Pipeline JSON contains an unpaired low surrogate");
                    }
                    append_utf8(output, first);
                    break;
                }
                default:
                    throw std::invalid_argument("Pipeline JSON contains an unsupported escape");
            }
        }
        throw std::invalid_argument("Pipeline JSON string is unterminated");
    }

    [[nodiscard]] std::uint32_t parse_hex_quad() {
        if (position_ + 4U > input_.size()) {
            throw std::invalid_argument("Pipeline JSON Unicode escape is incomplete");
        }
        std::uint32_t value = 0U;
        for (int index = 0; index < 4; ++index) {
            const char digit = input_[position_++];
            if (!is_hex(digit)) {
                throw std::invalid_argument("Pipeline JSON Unicode escape is invalid");
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
            throw std::invalid_argument("Pipeline JSON syntax is invalid");
        }
        ++position_;
    }

    [[nodiscard]] bool peek(const char expected) const noexcept {
        return position_ < input_.size() && input_[position_] == expected;
    }

    std::string_view input_;
    std::size_t position_{0U};
};

[[nodiscard]] const JsonObject& require_object(const JsonValue& value, const std::string_view field) {
    if (const auto* object = std::get_if<JsonObject>(&value.storage)) return *object;
    throw std::invalid_argument(std::string{field} + " must be a JSON object");
}

[[nodiscard]] const JsonArray& require_array(const JsonValue& value, const std::string_view field) {
    if (const auto* array = std::get_if<JsonArray>(&value.storage)) return *array;
    throw std::invalid_argument(std::string{field} + " must be a JSON array");
}

[[nodiscard]] const JsonValue& require_field(
    const JsonObject& object,
    const std::string_view field
) {
    const auto iterator = object.find(field);
    if (iterator == object.end()) {
        throw std::invalid_argument("Pipeline JSON is missing field: " + std::string{field});
    }
    return iterator->second;
}

[[nodiscard]] std::string require_string(
    const JsonObject& object,
    const std::string_view field
) {
    const JsonValue& value = require_field(object, field);
    if (const auto* text = std::get_if<std::string>(&value.storage)) return *text;
    throw std::invalid_argument("Pipeline JSON field must be a string: " + std::string{field});
}

[[nodiscard]] std::int64_t require_integer(
    const JsonObject& object,
    const std::string_view field
) {
    const JsonValue& value = require_field(object, field);
    if (const auto* integer = std::get_if<std::int64_t>(&value.storage)) return *integer;
    throw std::invalid_argument("Pipeline JSON field must be an integer: " + std::string{field});
}

[[nodiscard]] std::uint32_t require_uint32(const JsonObject& object, const std::string_view field) {
    const std::int64_t value = require_integer(object, field);
    if (value < 0 || value > static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max())) {
        throw std::invalid_argument("Pipeline JSON field is outside uint32 range: " + std::string{field});
    }
    return static_cast<std::uint32_t>(value);
}

[[nodiscard]] double require_number(const JsonObject& object, const std::string_view field) {
    const JsonValue& value = require_field(object, field);
    if (const auto* number = std::get_if<double>(&value.storage)) return *number;
    if (const auto* integer = std::get_if<std::int64_t>(&value.storage)) {
        return static_cast<double>(*integer);
    }
    throw std::invalid_argument("Pipeline JSON field must be numeric: " + std::string{field});
}

void require_only_fields(
    const JsonObject& object,
    const std::initializer_list<std::string_view> allowed
) {
    for (const auto& [name, value] : object) {
        static_cast<void>(value);
        if (std::ranges::find(allowed, name) == allowed.end()) {
            throw std::invalid_argument("Pipeline JSON contains unknown field: " + name);
        }
    }
}

[[nodiscard]] std::uint32_t require_schema_version(
    const JsonObject& object,
    const std::uint32_t expected
) {
    const std::int64_t version = require_integer(object, "schemaVersion");
    if (version != static_cast<std::int64_t>(expected)) {
        throw std::invalid_argument("Pipeline JSON schema version is unsupported");
    }
    return static_cast<std::uint32_t>(version);
}

[[nodiscard]] std::vector<std::string> parse_dependencies(const JsonValue& value) {
    const JsonArray& array = require_array(value, "dependsOn");
    if (array.size() > maximum_dependencies) {
        throw std::invalid_argument("Pipeline step has too many dependencies");
    }
    std::vector<std::string> dependencies;
    dependencies.reserve(array.size());
    for (const JsonValue& entry : array) {
        if (const auto* text = std::get_if<std::string>(&entry.storage)) {
            dependencies.push_back(*text);
        } else {
            throw std::invalid_argument("Pipeline dependency must be a string");
        }
    }
    return dependencies;
}

[[nodiscard]] std::vector<PipelineStepDocument> parse_steps(const JsonValue& value) {
    const JsonArray& array = require_array(value, "steps");
    if (array.empty() || array.size() > maximum_steps) {
        throw std::invalid_argument("Pipeline step count is invalid");
    }
    std::vector<PipelineStepDocument> steps;
    steps.reserve(array.size());
    for (const JsonValue& entry : array) {
        const JsonObject& object = require_object(entry, "Pipeline step");
        require_only_fields(object, {"id", "module", "pluginVersion", "dependsOn", "weight"});
        steps.push_back(PipelineStepDocument{
            .id = require_string(object, "id"),
            .module_id = require_string(object, "module"),
            .plugin_version = require_string(object, "pluginVersion"),
            .depends_on = parse_dependencies(require_field(object, "dependsOn")),
            .weight = require_number(object, "weight"),
        });
    }
    return steps;
}

[[nodiscard]] std::vector<ExecutionParameterDocument> parse_execution_parameters(const JsonValue& value) {
    const JsonArray& array = require_array(value, "Execution-plan parameters");
    if (array.size() > 128U) throw std::invalid_argument("Execution-plan parameter count is invalid");
    std::vector<ExecutionParameterDocument> result; result.reserve(array.size());
    for (const JsonValue& entry : array) {
        const JsonObject& object = require_object(entry, "Execution-plan parameter");
        require_only_fields(object, {"name", "type", "value"});
        result.push_back({require_string(object, "name"), require_string(object, "type"), require_string(object, "value")});
    }
    return result;
}

[[nodiscard]] std::vector<ExecutionInputBindingDocument> parse_execution_inputs(const JsonValue& value) {
    const JsonArray& array = require_array(value, "Execution-plan inputs");
    if (array.size() > 64U) throw std::invalid_argument("Execution-plan input count is invalid");
    std::vector<ExecutionInputBindingDocument> result; result.reserve(array.size());
    for (const JsonValue& entry : array) {
        const JsonObject& object = require_object(entry, "Execution-plan input");
        require_only_fields(object, {"port", "sourceKind", "sourceId", "fileType", "relativeProjectPath"});
        result.push_back({require_string(object, "port"), require_string(object, "sourceKind"), require_string(object, "sourceId"), require_string(object, "fileType"), require_string(object, "relativeProjectPath")});
    }
    return result;
}

[[nodiscard]] std::vector<ExecutionOutputBindingDocument> parse_execution_outputs(const JsonValue& value) {
    const JsonArray& array = require_array(value, "Execution-plan outputs");
    if (array.size() > 64U) throw std::invalid_argument("Execution-plan output count is invalid");
    std::vector<ExecutionOutputBindingDocument> result; result.reserve(array.size());
    for (const JsonValue& entry : array) {
        const JsonObject& object = require_object(entry, "Execution-plan output");
        require_only_fields(object, {"port", "fileType", "relativeProjectPath"});
        result.push_back({require_string(object, "port"), require_string(object, "fileType"), require_string(object, "relativeProjectPath")});
    }
    return result;
}

[[nodiscard]] std::vector<ExecutionPlanStepDocument> parse_execution_steps(
    const JsonValue& value
) {
    const JsonArray& array = require_array(value, "steps");
    if (array.empty() || array.size() > maximum_steps) {
        throw std::invalid_argument("Execution-plan step count is invalid");
    }
    std::vector<ExecutionPlanStepDocument> steps;
    steps.reserve(array.size());
    for (const JsonValue& entry : array) {
        const JsonObject& object = require_object(entry, "Execution-plan step");
        require_only_fields(
            object,
            {"id", "module", "pluginId", "pluginVersion", "pluginManifestVersion",
             "pluginApiVersion", "moduleType", "pluginRoot", "executable", "dependsOn",
             "weight", "parameters", "inputs", "outputs"}
        );
        steps.push_back(ExecutionPlanStepDocument{
            .id = require_string(object, "id"),
            .module_id = require_string(object, "module"),
            .plugin_id = require_string(object, "pluginId"),
            .plugin_version = require_string(object, "pluginVersion"),
            .plugin_manifest_version = require_uint32(object, "pluginManifestVersion"),
            .plugin_api_version = require_string(object, "pluginApiVersion"),
            .module_type = require_string(object, "moduleType"),
            .plugin_root_path = require_string(object, "pluginRoot"),
            .executable_path = require_string(object, "executable"),
            .depends_on = parse_dependencies(require_field(object, "dependsOn")),
            .weight = require_number(object, "weight"),
            .parameters = parse_execution_parameters(require_field(object, "parameters")),
            .inputs = parse_execution_inputs(require_field(object, "inputs")),
            .outputs = parse_execution_outputs(require_field(object, "outputs")),
        });
    }
    return steps;
}

void validate_document_size_and_encoding(const std::string_view json) {
    if (json.empty() || json.size() > maximum_pipeline_document_bytes) {
        throw std::invalid_argument("Pipeline JSON document size is invalid");
    }
    if (json.find('\0') != std::string_view::npos) {
        throw std::invalid_argument("Pipeline JSON must not contain NUL characters");
    }
    validate_utf8(json);
}

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

void append_string(std::ostringstream& output, const std::string_view value) {
    output << '"' << escape_json(value) << '"';
}

void append_pipeline_steps(std::ostringstream& output, const std::vector<PipelineStepDocument>& steps) {
    output << '[';
    for (std::size_t index = 0U; index < steps.size(); ++index) {
        if (index != 0U) output << ',';
        const PipelineStepDocument& step = steps[index];
        output << "{\"id\":";
        append_string(output, step.id);
        output << ",\"module\":";
        append_string(output, step.module_id);
        output << ",\"pluginVersion\":";
        append_string(output, step.plugin_version);
        output << ",\"dependsOn\":[";
        for (std::size_t dependency = 0U; dependency < step.depends_on.size(); ++dependency) {
            if (dependency != 0U) output << ',';
            append_string(output, step.depends_on[dependency]);
        }
        output << "],\"weight\":" << std::setprecision(17) << step.weight << '}';
    }
    output << ']';
}

void append_execution_steps(
    std::ostringstream& output,
    const std::vector<ExecutionPlanStepDocument>& steps
) {
    output << '[';
    for (std::size_t index = 0U; index < steps.size(); ++index) {
        if (index != 0U) output << ',';
        const ExecutionPlanStepDocument& step = steps[index];
        output << "{\"id\":"; append_string(output, step.id);
        output << ",\"module\":"; append_string(output, step.module_id);
        output << ",\"pluginId\":"; append_string(output, step.plugin_id);
        output << ",\"pluginVersion\":"; append_string(output, step.plugin_version);
        output << ",\"pluginManifestVersion\":" << step.plugin_manifest_version;
        output << ",\"pluginApiVersion\":"; append_string(output, step.plugin_api_version);
        output << ",\"moduleType\":"; append_string(output, step.module_type);
        output << ",\"pluginRoot\":"; append_string(output, step.plugin_root_path);
        output << ",\"executable\":"; append_string(output, step.executable_path);
        output << ",\"dependsOn\":[";
        for (std::size_t dependency = 0U; dependency < step.depends_on.size(); ++dependency) {
            if (dependency != 0U) output << ',';
            append_string(output, step.depends_on[dependency]);
        }
        output << "],\"weight\":" << std::setprecision(17) << step.weight;
        output << ",\"parameters\":[";
        for (std::size_t i = 0U; i < step.parameters.size(); ++i) {
            if (i != 0U) output << ',';
            output << "{\"name\":"; append_string(output, step.parameters[i].name);
            output << ",\"type\":"; append_string(output, step.parameters[i].type);
            output << ",\"value\":"; append_string(output, step.parameters[i].value);
            output << '}';
        }
        output << "],\"inputs\":[";
        for (std::size_t i = 0U; i < step.inputs.size(); ++i) {
            if (i != 0U) output << ',';
            const auto& input = step.inputs[i];
            output << "{\"port\":"; append_string(output, input.port);
            output << ",\"sourceKind\":"; append_string(output, input.source_kind);
            output << ",\"sourceId\":"; append_string(output, input.source_id);
            output << ",\"fileType\":"; append_string(output, input.file_type);
            output << ",\"relativeProjectPath\":"; append_string(output, input.relative_project_path);
            output << '}';
        }
        output << "],\"outputs\":[";
        for (std::size_t i = 0U; i < step.outputs.size(); ++i) {
            if (i != 0U) output << ',';
            const auto& out = step.outputs[i];
            output << "{\"port\":"; append_string(output, out.port);
            output << ",\"fileType\":"; append_string(output, out.file_type);
            output << ",\"relativeProjectPath\":"; append_string(output, out.relative_project_path);
            output << '}';
        }
        output << "]}";
    }
    output << ']';
}

void require_serializable_text(const std::string_view value, const std::string_view field) {
    if (value.empty() || value.size() > maximum_text_length ||
        value.find('\0') != std::string_view::npos) {
        throw std::invalid_argument(std::string{field} + " is invalid");
    }
    validate_utf8(value);
}

void require_serializable_pipeline_steps(const std::vector<PipelineStepDocument>& steps) {
    if (steps.empty() || steps.size() > maximum_steps) {
        throw std::invalid_argument("Pipeline document step count is invalid");
    }
    for (const PipelineStepDocument& step : steps) {
        require_serializable_text(step.id, "Pipeline document step id");
        require_serializable_text(step.module_id, "Pipeline document module id");
        require_serializable_text(step.plugin_version, "Pipeline document plugin version");
        if (!std::isfinite(step.weight) || step.weight <= 0.0 ||
            step.depends_on.size() > maximum_dependencies) {
            throw std::invalid_argument("Pipeline document step is invalid");
        }
        for (const std::string& dependency : step.depends_on) {
            require_serializable_text(dependency, "Pipeline document dependency id");
        }
    }
}

void require_serializable_execution_steps(
    const std::vector<ExecutionPlanStepDocument>& steps
) {
    if (steps.empty() || steps.size() > maximum_steps) {
        throw std::invalid_argument("Execution-plan document step count is invalid");
    }
    for (const ExecutionPlanStepDocument& step : steps) {
        require_serializable_text(step.id, "Execution-plan step id");
        require_serializable_text(step.module_id, "Execution-plan module id");
        require_serializable_text(step.plugin_id, "Execution-plan plugin id");
        require_serializable_text(step.plugin_version, "Execution-plan plugin version");
        if (step.plugin_manifest_version == 0U) {
            throw std::invalid_argument("Execution-plan plugin manifest version is invalid");
        }
        require_serializable_text(step.plugin_api_version, "Execution-plan plugin API version");
        require_serializable_text(step.module_type, "Execution-plan module type");
        require_serializable_text(step.plugin_root_path, "Execution-plan plugin root");
        require_serializable_text(step.executable_path, "Execution-plan executable");
        if (!std::isfinite(step.weight) || step.weight <= 0.0 ||
            step.depends_on.size() > maximum_dependencies) {
            throw std::invalid_argument("Execution-plan document step is invalid");
        }
        for (const std::string& dependency : step.depends_on) {
            require_serializable_text(dependency, "Execution-plan dependency id");
        }
        for (const auto& parameter : step.parameters) {
            require_serializable_text(parameter.name, "Execution-plan parameter name");
            require_serializable_text(parameter.type, "Execution-plan parameter type");
            if (parameter.value.size() > 4096U || parameter.value.find('\0') != std::string::npos) {
                throw std::invalid_argument("Execution-plan parameter value is invalid");
            }
        }
        for (const auto& input : step.inputs) {
            require_serializable_text(input.port, "Execution-plan input port");
            require_serializable_text(input.source_kind, "Execution-plan input source kind");
            require_serializable_text(input.source_id, "Execution-plan input source id");
            require_serializable_text(input.file_type, "Execution-plan input file type");
            require_serializable_text(input.relative_project_path, "Execution-plan input relative path");
        }
        for (const auto& output_binding : step.outputs) {
            require_serializable_text(output_binding.port, "Execution-plan output port");
            require_serializable_text(output_binding.file_type, "Execution-plan output file type");
            require_serializable_text(output_binding.relative_project_path, "Execution-plan output relative path");
        }
    }
}

void require_serialized_size(const std::string& result) {
    if (result.size() > maximum_pipeline_document_bytes) {
        throw std::invalid_argument("Serialized pipeline document exceeds the maximum size");
    }
}

}  // namespace

PipelineDefinitionDocument parse_pipeline_definition_document(const std::string_view json) {
    validate_document_size_and_encoding(json);
    const JsonValue root_value = Parser{json}.parse_document();
    const JsonObject& root = require_object(root_value, "Pipeline definition");
    require_only_fields(root, {"schemaVersion", "id", "name", "version", "steps"});
    return PipelineDefinitionDocument{
        .schema_version = require_schema_version(root, current_pipeline_definition_schema_version),
        .id = require_string(root, "id"),
        .name = require_string(root, "name"),
        .version = require_string(root, "version"),
        .steps = parse_steps(require_field(root, "steps")),
    };
}

std::string serialize_pipeline_definition_document(
    const PipelineDefinitionDocument& document
) {
    if (document.schema_version != current_pipeline_definition_schema_version) {
        throw std::invalid_argument("Pipeline definition document is invalid");
    }
    require_serializable_text(document.id, "Pipeline definition id");
    require_serializable_text(document.name, "Pipeline definition name");
    require_serializable_text(document.version, "Pipeline definition version");
    require_serializable_pipeline_steps(document.steps);
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << "{\"schemaVersion\":" << document.schema_version << ",\"id\":";
    append_string(output, document.id);
    output << ",\"name\":";
    append_string(output, document.name);
    output << ",\"version\":";
    append_string(output, document.version);
    output << ",\"steps\":";
    append_pipeline_steps(output, document.steps);
    output << '}';
    std::string result = output.str();
    require_serialized_size(result);
    return result;
}

ExecutionPlanDocument parse_execution_plan_document(const std::string_view json) {
    validate_document_size_and_encoding(json);
    const JsonValue root_value = Parser{json}.parse_document();
    const JsonObject& root = require_object(root_value, "Execution plan");
    require_only_fields(
        root,
        {"schemaVersion", "jobId", "jobRevision", "pipelineId", "pipelineVersion", "steps"}
    );
    const std::int64_t revision = require_integer(root, "jobRevision");
    if (revision < 0) {
        throw std::invalid_argument("Execution plan job revision must not be negative");
    }
    return ExecutionPlanDocument{
        .schema_version = require_schema_version(root, current_execution_plan_schema_version),
        .job_id = require_string(root, "jobId"),
        .job_revision = revision,
        .pipeline_id = require_string(root, "pipelineId"),
        .pipeline_version = require_string(root, "pipelineVersion"),
        .steps = parse_execution_steps(require_field(root, "steps")),
    };
}

std::string serialize_execution_plan_document(const ExecutionPlanDocument& document) {
    if (document.schema_version != current_execution_plan_schema_version || document.job_revision < 0) {
        throw std::invalid_argument("Execution plan document is invalid");
    }
    require_serializable_text(document.job_id, "Execution plan job id");
    require_serializable_text(document.pipeline_id, "Execution plan pipeline id");
    require_serializable_text(document.pipeline_version, "Execution plan pipeline version");
    require_serializable_execution_steps(document.steps);
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << "{\"schemaVersion\":" << document.schema_version << ",\"jobId\":";
    append_string(output, document.job_id);
    output << ",\"jobRevision\":" << document.job_revision << ",\"pipelineId\":";
    append_string(output, document.pipeline_id);
    output << ",\"pipelineVersion\":";
    append_string(output, document.pipeline_version);
    output << ",\"steps\":";
    append_execution_steps(output, document.steps);
    output << '}';
    std::string result = output.str();
    require_serialized_size(result);
    return result;
}

}  // namespace biocore::pipeline_protocol
