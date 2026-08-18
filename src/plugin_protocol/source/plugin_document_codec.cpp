#include "biocore/plugin_protocol/plugin_document_codec.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace biocore::plugin_protocol {
namespace {

constexpr std::size_t maximum_json_depth = 16U;
constexpr std::size_t maximum_modules = 128U;
constexpr std::size_t maximum_entrypoints = 8U;
constexpr std::size_t maximum_text_length = 32U * 1024U;

struct JsonValue;
using JsonArray = std::vector<JsonValue>;
using JsonObject = std::map<std::string, JsonValue, std::less<>>;

struct JsonValue final {
    using Storage = std::variant<std::nullptr_t, std::string, std::int64_t, bool, JsonArray, JsonObject>;
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
            throw std::invalid_argument("Plugin manifest is not valid UTF-8");
        }
        if (index + length > value.size()) {
            throw std::invalid_argument("Plugin manifest ends inside a UTF-8 sequence");
        }
        for (std::size_t offset = 1U; offset < length; ++offset) {
            const auto continuation = static_cast<unsigned char>(value[index + offset]);
            if ((continuation & 0xC0U) != 0x80U) {
                throw std::invalid_argument("Plugin manifest is not valid UTF-8");
            }
            code_point = (code_point << 6U) | (continuation & 0x3FU);
        }
        if ((length == 3U && code_point < 0x800U) ||
            (length == 4U && code_point < 0x10000U) || code_point > 0x10FFFFU ||
            (code_point >= 0xD800U && code_point <= 0xDFFFU)) {
            throw std::invalid_argument("Plugin manifest contains an invalid UTF-8 code point");
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
    throw std::invalid_argument("Plugin manifest Unicode escape is invalid");
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
        throw std::invalid_argument("Plugin manifest Unicode escape is outside the valid range");
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
            throw std::invalid_argument("Plugin manifest contains trailing data");
        }
        return value;
    }

private:
    [[nodiscard]] JsonValue parse_value(const std::size_t depth) {
        if (depth > maximum_json_depth) {
            throw std::invalid_argument("Plugin manifest nesting exceeds the supported depth");
        }
        if (position_ >= input_.size()) {
            throw std::invalid_argument("Plugin manifest ended before a value");
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
        if (input_.substr(position_, 4U) == "true") {
            position_ += 4U;
            return JsonValue{true};
        }
        if (input_.substr(position_, 5U) == "false") {
            position_ += 5U;
            return JsonValue{false};
        }
        return JsonValue{parse_integer()};
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
                throw std::invalid_argument("Plugin manifest object key must be a string");
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
                throw std::invalid_argument("Plugin manifest contains a duplicate field");
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

    [[nodiscard]] std::int64_t parse_integer() {
        const std::size_t begin = position_;
        if (peek('-')) ++position_;
        if (position_ >= input_.size() || input_[position_] < '0' || input_[position_] > '9') {
            throw std::invalid_argument("Plugin manifest number is invalid");
        }
        if (input_[position_] == '0') {
            ++position_;
        } else {
            while (position_ < input_.size() && input_[position_] >= '0' &&
                   input_[position_] <= '9') {
                ++position_;
            }
        }
        if (peek('.') || peek('e') || peek('E')) {
            throw std::invalid_argument("Plugin manifest schema v1 only supports integer numbers");
        }
        const std::string_view token = input_.substr(begin, position_ - begin);
        std::int64_t value = 0;
        const auto [end, error] = std::from_chars(
            token.data(), token.data() + token.size(), value
        );
        if (error != std::errc{} || end != token.data() + token.size()) {
            throw std::invalid_argument("Plugin manifest integer is outside the supported range");
        }
        return value;
    }

    [[nodiscard]] std::string parse_string() {
        expect('"');
        std::string output;
        while (position_ < input_.size()) {
            const auto character = static_cast<unsigned char>(input_[position_++]);
            if (character == '"') {
                if (output.size() > maximum_text_length) {
                    throw std::invalid_argument("Plugin manifest string exceeds the maximum length");
                }
                return output;
            }
            if (character < 0x20U) {
                throw std::invalid_argument("Plugin manifest string contains a control character");
            }
            if (character != '\\') {
                output.push_back(static_cast<char>(character));
                continue;
            }
            if (position_ >= input_.size()) {
                throw std::invalid_argument("Plugin manifest escape is incomplete");
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
                            throw std::invalid_argument("Plugin manifest high surrogate lacks a pair");
                        }
                        position_ += 2U;
                        const std::uint32_t second = parse_hex_quad();
                        if (second < 0xDC00U || second > 0xDFFFU) {
                            throw std::invalid_argument("Plugin manifest surrogate pair is invalid");
                        }
                        first = 0x10000U + ((first - 0xD800U) << 10U) +
                                (second - 0xDC00U);
                    } else if (first >= 0xDC00U && first <= 0xDFFFU) {
                        throw std::invalid_argument("Plugin manifest contains an unpaired low surrogate");
                    }
                    append_utf8(output, first);
                    break;
                }
                default:
                    throw std::invalid_argument("Plugin manifest contains an unsupported escape");
            }
        }
        throw std::invalid_argument("Plugin manifest string is unterminated");
    }

    [[nodiscard]] std::uint32_t parse_hex_quad() {
        if (position_ + 4U > input_.size()) {
            throw std::invalid_argument("Plugin manifest Unicode escape is incomplete");
        }
        std::uint32_t value = 0U;
        for (int index = 0; index < 4; ++index) {
            const char digit = input_[position_++];
            if (!is_hex(digit)) {
                throw std::invalid_argument("Plugin manifest Unicode escape is invalid");
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
            throw std::invalid_argument("Plugin manifest JSON syntax is invalid");
        }
        ++position_;
    }

    [[nodiscard]] bool peek(const char expected) const noexcept {
        return position_ < input_.size() && input_[position_] == expected;
    }

    std::string_view input_;
    std::size_t position_{0U};
};

[[nodiscard]] const JsonObject& require_object(
    const JsonValue& value,
    const std::string_view field
) {
    if (const auto* object = std::get_if<JsonObject>(&value.storage)) return *object;
    throw std::invalid_argument(std::string{field} + " must be a JSON object");
}

[[nodiscard]] const JsonArray& require_array(
    const JsonValue& value,
    const std::string_view field
) {
    if (const auto* array = std::get_if<JsonArray>(&value.storage)) return *array;
    throw std::invalid_argument(std::string{field} + " must be a JSON array");
}

[[nodiscard]] const JsonValue& require_field(
    const JsonObject& object,
    const std::string_view field
) {
    const auto iterator = object.find(field);
    if (iterator == object.end()) {
        throw std::invalid_argument("Plugin manifest is missing field: " + std::string{field});
    }
    return iterator->second;
}

[[nodiscard]] std::string require_string(
    const JsonObject& object,
    const std::string_view field
) {
    const JsonValue& value = require_field(object, field);
    if (const auto* text = std::get_if<std::string>(&value.storage)) return *text;
    throw std::invalid_argument("Plugin manifest field must be a string: " + std::string{field});
}

[[nodiscard]] std::int64_t require_integer(
    const JsonObject& object,
    const std::string_view field
) {
    const JsonValue& value = require_field(object, field);
    if (const auto* integer = std::get_if<std::int64_t>(&value.storage)) return *integer;
    throw std::invalid_argument("Plugin manifest field must be an integer: " + std::string{field});
}

[[nodiscard]] bool require_bool(
    const JsonObject& object,
    const std::string_view field
) {
    const JsonValue& value = require_field(object, field);
    if (const auto* boolean = std::get_if<bool>(&value.storage)) return *boolean;
    throw std::invalid_argument("Plugin manifest field must be a boolean: " + std::string{field});
}

[[nodiscard]] std::optional<std::string> optional_string(
    const JsonObject& object,
    const std::string_view field
) {
    const auto iterator = object.find(field);
    if (iterator == object.end() || std::holds_alternative<std::nullptr_t>(iterator->second.storage)) {
        return std::nullopt;
    }
    if (const auto* text = std::get_if<std::string>(&iterator->second.storage)) return *text;
    throw std::invalid_argument("Plugin manifest optional field must be a string or null: " + std::string{field});
}

[[nodiscard]] std::vector<std::string> parse_string_array(
    const JsonValue& value,
    const std::string_view field,
    const std::size_t maximum_count
) {
    const JsonArray& array = require_array(value, field);
    if (array.size() > maximum_count) {
        throw std::invalid_argument(std::string{field} + " contains too many values");
    }
    std::vector<std::string> result;
    result.reserve(array.size());
    for (const JsonValue& entry : array) {
        const auto* text = std::get_if<std::string>(&entry.storage);
        if (text == nullptr) throw std::invalid_argument(std::string{field} + " values must be strings");
        result.push_back(*text);
    }
    return result;
}

void require_only_fields(
    const JsonObject& object,
    const std::initializer_list<std::string_view> allowed
) {
    for (const auto& [name, value] : object) {
        static_cast<void>(value);
        if (std::ranges::find(allowed, name) == allowed.end()) {
            throw std::invalid_argument("Plugin manifest contains unknown field: " + name);
        }
    }
}

[[nodiscard]] std::vector<PluginEntrypointDocument> parse_entrypoints(
    const JsonValue& value
) {
    const JsonObject& object = require_object(value, "Plugin module entrypoints");
    if (object.empty() || object.size() > maximum_entrypoints) {
        throw std::invalid_argument("Plugin module entrypoint count is invalid");
    }
    std::vector<PluginEntrypointDocument> result;
    result.reserve(object.size());
    for (const auto& [platform, entrypoint_value] : object) {
        const auto* path = std::get_if<std::string>(&entrypoint_value.storage);
        if (path == nullptr) {
            throw std::invalid_argument("Plugin entrypoint path must be a string");
        }
        result.push_back(PluginEntrypointDocument{platform, *path});
    }
    return result;
}

[[nodiscard]] std::vector<PluginParameterDocument> parse_parameters(const JsonValue& value) {
    constexpr std::size_t maximum_parameters = 128U;
    const JsonArray& array = require_array(value, "Plugin parameters");
    if (array.size() > maximum_parameters) throw std::invalid_argument("Plugin parameter count is invalid");
    std::vector<PluginParameterDocument> result;
    result.reserve(array.size());
    for (const JsonValue& entry : array) {
        const JsonObject& object = require_object(entry, "Plugin parameter");
        require_only_fields(object, {"name", "type", "required", "default", "minimum", "maximum", "enumValues"});
        result.push_back(PluginParameterDocument{
            .name = require_string(object, "name"),
            .type = require_string(object, "type"),
            .required = require_bool(object, "required"),
            .default_value = optional_string(object, "default"),
            .minimum = optional_string(object, "minimum"),
            .maximum = optional_string(object, "maximum"),
            .enum_values = parse_string_array(require_field(object, "enumValues"), "Plugin parameter enumValues", 128U),
        });
    }
    return result;
}

[[nodiscard]] std::vector<PluginInputPortDocument> parse_inputs(const JsonValue& value) {
    constexpr std::size_t maximum_ports = 64U;
    const JsonArray& array = require_array(value, "Plugin inputs");
    if (array.size() > maximum_ports) throw std::invalid_argument("Plugin input port count is invalid");
    std::vector<PluginInputPortDocument> result;
    result.reserve(array.size());
    for (const JsonValue& entry : array) {
        const JsonObject& object = require_object(entry, "Plugin input port");
        require_only_fields(object, {"name", "required", "acceptedFileTypes"});
        result.push_back(PluginInputPortDocument{
            .name = require_string(object, "name"),
            .required = require_bool(object, "required"),
            .accepted_file_types = parse_string_array(require_field(object, "acceptedFileTypes"), "Plugin input acceptedFileTypes", 64U),
        });
    }
    return result;
}

[[nodiscard]] std::vector<PluginOutputPortDocument> parse_outputs(const JsonValue& value) {
    constexpr std::size_t maximum_ports = 64U;
    const JsonArray& array = require_array(value, "Plugin outputs");
    if (array.size() > maximum_ports) throw std::invalid_argument("Plugin output port count is invalid");
    std::vector<PluginOutputPortDocument> result;
    result.reserve(array.size());
    for (const JsonValue& entry : array) {
        const JsonObject& object = require_object(entry, "Plugin output port");
        require_only_fields(object, {"name", "fileType"});
        result.push_back(PluginOutputPortDocument{
            .name = require_string(object, "name"),
            .file_type = require_string(object, "fileType"),
        });
    }
    return result;
}

[[nodiscard]] std::vector<PluginModuleDocument> parse_modules(
    const JsonValue& value,
    const std::uint32_t manifest_version
) {
    const JsonArray& array = require_array(value, "Plugin modules");
    if (array.empty() || array.size() > maximum_modules) {
        throw std::invalid_argument("Plugin module count is invalid");
    }
    std::vector<PluginModuleDocument> modules;
    modules.reserve(array.size());
    for (const JsonValue& entry : array) {
        const JsonObject& object = require_object(entry, "Plugin module");
        if (manifest_version == 1U) {
            require_only_fields(object, {"id", "type", "entrypoints"});
            modules.push_back(PluginModuleDocument{
                .id = require_string(object, "id"),
                .type = require_string(object, "type"),
                .entrypoints = parse_entrypoints(require_field(object, "entrypoints")),
                .parameters = {}, .inputs = {}, .outputs = {},
            });
        } else {
            require_only_fields(object, {"id", "type", "entrypoints", "parameters", "inputs", "outputs"});
            modules.push_back(PluginModuleDocument{
                .id = require_string(object, "id"),
                .type = require_string(object, "type"),
                .entrypoints = parse_entrypoints(require_field(object, "entrypoints")),
                .parameters = parse_parameters(require_field(object, "parameters")),
                .inputs = parse_inputs(require_field(object, "inputs")),
                .outputs = parse_outputs(require_field(object, "outputs")),
            });
        }
    }
    return modules;
}

void validate_document(const std::string_view json) {
    if (json.empty() || json.size() > maximum_plugin_manifest_bytes) {
        throw std::invalid_argument("Plugin manifest document size is invalid");
    }
    if (json.find('\0') != std::string_view::npos) {
        throw std::invalid_argument("Plugin manifest must not contain NUL characters");
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

void require_text(const std::string_view value, const std::string_view field) {
    if (value.empty() || value.size() > maximum_text_length ||
        value.find('\0') != std::string_view::npos) {
        throw std::invalid_argument(std::string{field} + " is invalid");
    }
    validate_utf8(value);
}

void append_string(std::ostringstream& output, const std::string_view value) {
    output << '"' << escape_json(value) << '"';
}

}  // namespace

PluginManifestDocument parse_plugin_manifest_document(const std::string_view json) {
    validate_document(json);
    const JsonValue root_value = Parser{json}.parse_document();
    const JsonObject& root = require_object(root_value, "Plugin manifest");
    require_only_fields(
        root,
        {"manifestVersion", "id", "name", "version", "apiVersion", "publisher", "modules"}
    );
    const std::int64_t version = require_integer(root, "manifestVersion");
    if (version < static_cast<std::int64_t>(minimum_plugin_manifest_version) ||
        version > static_cast<std::int64_t>(current_plugin_manifest_version)) {
        throw std::invalid_argument("Plugin manifest version is unsupported");
    }
    return PluginManifestDocument{
        .manifest_version = static_cast<std::uint32_t>(version),
        .id = require_string(root, "id"),
        .name = require_string(root, "name"),
        .version = require_string(root, "version"),
        .api_version = require_string(root, "apiVersion"),
        .publisher = require_string(root, "publisher"),
        .modules = parse_modules(require_field(root, "modules"), static_cast<std::uint32_t>(version)),
    };
}

std::string serialize_plugin_manifest_document(const PluginManifestDocument& document) {
    if (document.manifest_version < minimum_plugin_manifest_version ||
        document.manifest_version > current_plugin_manifest_version ||
        document.modules.empty() || document.modules.size() > maximum_modules) {
        throw std::invalid_argument("Plugin manifest document is invalid");
    }
    require_text(document.id, "Plugin id");
    require_text(document.name, "Plugin name");
    require_text(document.version, "Plugin version");
    require_text(document.api_version, "Plugin API version");
    require_text(document.publisher, "Plugin publisher");

    std::ostringstream output;
    output << "{\"manifestVersion\":" << document.manifest_version << ",\"id\":";
    append_string(output, document.id);
    output << ",\"name\":";
    append_string(output, document.name);
    output << ",\"version\":";
    append_string(output, document.version);
    output << ",\"apiVersion\":";
    append_string(output, document.api_version);
    output << ",\"publisher\":";
    append_string(output, document.publisher);
    output << ",\"modules\":[";
    for (std::size_t module_index = 0U; module_index < document.modules.size(); ++module_index) {
        if (module_index != 0U) output << ',';
        const PluginModuleDocument& module = document.modules[module_index];
        require_text(module.id, "Plugin module id");
        require_text(module.type, "Plugin module type");
        if (document.manifest_version == 1U &&
            (!module.parameters.empty() || !module.inputs.empty() || !module.outputs.empty())) {
            throw std::invalid_argument("Plugin manifest v1 cannot contain I/O contracts");
        }
        if (module.entrypoints.empty() || module.entrypoints.size() > maximum_entrypoints) {
            throw std::invalid_argument("Plugin module entrypoints are invalid");
        }
        output << "{\"id\":";
        append_string(output, module.id);
        output << ",\"type\":";
        append_string(output, module.type);
        output << ",\"entrypoints\":{";
        for (std::size_t entry_index = 0U; entry_index < module.entrypoints.size(); ++entry_index) {
            if (entry_index != 0U) output << ',';
            const PluginEntrypointDocument& entrypoint = module.entrypoints[entry_index];
            require_text(entrypoint.platform, "Plugin platform");
            require_text(entrypoint.relative_path, "Plugin entrypoint path");
            append_string(output, entrypoint.platform);
            output << ':';
            append_string(output, entrypoint.relative_path);
        }
        output << '}';
        if (document.manifest_version >= 2U) {
            output << ",\"parameters\":[";
            for (std::size_t parameter_index = 0U; parameter_index < module.parameters.size(); ++parameter_index) {
                if (parameter_index != 0U) output << ',';
                const auto& parameter = module.parameters[parameter_index];
                require_text(parameter.name, "Plugin parameter name");
                require_text(parameter.type, "Plugin parameter type");
                output << "{\"name\":"; append_string(output, parameter.name);
                output << ",\"type\":"; append_string(output, parameter.type);
                output << ",\"required\":" << (parameter.required ? "true" : "false");
                output << ",\"default\":";
                if (parameter.default_value.has_value()) append_string(output, *parameter.default_value); else output << "null";
                output << ",\"minimum\":";
                if (parameter.minimum.has_value()) append_string(output, *parameter.minimum); else output << "null";
                output << ",\"maximum\":";
                if (parameter.maximum.has_value()) append_string(output, *parameter.maximum); else output << "null";
                output << ",\"enumValues\":[";
                for (std::size_t enum_index = 0U; enum_index < parameter.enum_values.size(); ++enum_index) {
                    if (enum_index != 0U) output << ',';
                    append_string(output, parameter.enum_values[enum_index]);
                }
                output << "]}";
            }
            output << "],\"inputs\":[";
            for (std::size_t input_index = 0U; input_index < module.inputs.size(); ++input_index) {
                if (input_index != 0U) output << ',';
                const auto& input = module.inputs[input_index];
                output << "{\"name\":"; append_string(output, input.name);
                output << ",\"required\":" << (input.required ? "true" : "false");
                output << ",\"acceptedFileTypes\":[";
                for (std::size_t type_index = 0U; type_index < input.accepted_file_types.size(); ++type_index) {
                    if (type_index != 0U) output << ',';
                    append_string(output, input.accepted_file_types[type_index]);
                }
                output << "]}";
            }
            output << "],\"outputs\":[";
            for (std::size_t output_index = 0U; output_index < module.outputs.size(); ++output_index) {
                if (output_index != 0U) output << ',';
                const auto& port = module.outputs[output_index];
                output << "{\"name\":"; append_string(output, port.name);
                output << ",\"fileType\":"; append_string(output, port.file_type);
                output << '}';
            }
            output << ']';
        }
        output << '}';
    }
    output << "]}";
    std::string result = output.str();
    if (result.size() > maximum_plugin_manifest_bytes) {
        throw std::invalid_argument("Serialized plugin manifest exceeds the maximum size");
    }
    return result;
}


PluginInvocationDocument parse_plugin_invocation_document(const std::string_view json) {
    if (json.empty() || json.size() > maximum_plugin_invocation_bytes ||
        json.find('\0') != std::string_view::npos) {
        throw std::invalid_argument("Plugin invocation document size is invalid");
    }
    validate_utf8(json);
    const JsonValue root_value = Parser{json}.parse_document();
    const JsonObject& root = require_object(root_value, "Plugin invocation");
    require_only_fields(root, {"schemaVersion", "jobId", "jobRevision", "stepId", "moduleId", "parameters", "inputs", "outputs"});
    const auto version = require_integer(root, "schemaVersion");
    if (version != static_cast<std::int64_t>(current_plugin_invocation_schema_version)) {
        throw std::invalid_argument("Plugin invocation schema version is unsupported");
    }

    std::vector<PluginInvocationParameterDocument> parameters;
    const JsonArray& parameter_array = require_array(require_field(root, "parameters"), "Plugin invocation parameters");
    if (parameter_array.size() > 128U) throw std::invalid_argument("Plugin invocation parameter count is invalid");
    parameters.reserve(parameter_array.size());
    for (const JsonValue& entry : parameter_array) {
        const JsonObject& object = require_object(entry, "Plugin invocation parameter");
        require_only_fields(object, {"name", "type", "value"});
        parameters.push_back(PluginInvocationParameterDocument{
            .name = require_string(object, "name"),
            .type = require_string(object, "type"),
            .value = require_string(object, "value"),
        });
    }

    std::vector<PluginInvocationInputDocument> inputs;
    const JsonArray& input_array = require_array(require_field(root, "inputs"), "Plugin invocation inputs");
    if (input_array.size() > 64U) throw std::invalid_argument("Plugin invocation input count is invalid");
    inputs.reserve(input_array.size());
    for (const JsonValue& entry : input_array) {
        const JsonObject& object = require_object(entry, "Plugin invocation input");
        require_only_fields(object, {"port", "sourceKind", "sourceId", "fileType", "path"});
        inputs.push_back(PluginInvocationInputDocument{
            .port = require_string(object, "port"),
            .source_kind = require_string(object, "sourceKind"),
            .source_id = require_string(object, "sourceId"),
            .file_type = require_string(object, "fileType"),
            .path = require_string(object, "path"),
        });
    }

    std::vector<PluginInvocationOutputDocument> outputs;
    const JsonArray& output_array = require_array(require_field(root, "outputs"), "Plugin invocation outputs");
    if (output_array.size() > 64U) throw std::invalid_argument("Plugin invocation output count is invalid");
    outputs.reserve(output_array.size());
    for (const JsonValue& entry : output_array) {
        const JsonObject& object = require_object(entry, "Plugin invocation output");
        require_only_fields(object, {"port", "fileType", "path"});
        outputs.push_back(PluginInvocationOutputDocument{
            .port = require_string(object, "port"),
            .file_type = require_string(object, "fileType"),
            .path = require_string(object, "path"),
        });
    }

    return PluginInvocationDocument{
        .schema_version = static_cast<std::uint32_t>(version),
        .job_id = require_string(root, "jobId"),
        .job_revision = require_integer(root, "jobRevision"),
        .step_id = require_string(root, "stepId"),
        .module_id = require_string(root, "moduleId"),
        .parameters = std::move(parameters),
        .inputs = std::move(inputs),
        .outputs = std::move(outputs),
    };
}

std::string serialize_plugin_invocation_document(const PluginInvocationDocument& document) {
    if (document.schema_version != current_plugin_invocation_schema_version ||
        document.job_revision < 0 || document.parameters.size() > 128U ||
        document.inputs.size() > 64U || document.outputs.size() > 64U) {
        throw std::invalid_argument("Plugin invocation document is invalid");
    }
    require_text(document.job_id, "Plugin invocation job id");
    require_text(document.step_id, "Plugin invocation step id");
    require_text(document.module_id, "Plugin invocation module id");

    std::ostringstream output;
    output << "{\"schemaVersion\":" << document.schema_version << ",\"jobId\":";
    append_string(output, document.job_id);
    output << ",\"jobRevision\":" << document.job_revision << ",\"stepId\":";
    append_string(output, document.step_id);
    output << ",\"moduleId\":";
    append_string(output, document.module_id);
    output << ",\"parameters\":[";
    for (std::size_t index = 0U; index < document.parameters.size(); ++index) {
        if (index != 0U) output << ',';
        const auto& parameter = document.parameters[index];
        require_text(parameter.name, "Plugin invocation parameter name");
        require_text(parameter.type, "Plugin invocation parameter type");
        if (parameter.value.size() > maximum_text_length || parameter.value.find('\0') != std::string::npos) {
            throw std::invalid_argument("Plugin invocation parameter value is invalid");
        }
        output << "{\"name\":"; append_string(output, parameter.name);
        output << ",\"type\":"; append_string(output, parameter.type);
        output << ",\"value\":"; append_string(output, parameter.value);
        output << '}';
    }
    output << "],\"inputs\":[";
    for (std::size_t index = 0U; index < document.inputs.size(); ++index) {
        if (index != 0U) output << ',';
        const auto& input = document.inputs[index];
        require_text(input.port, "Plugin invocation input port");
        require_text(input.source_kind, "Plugin invocation input source kind");
        require_text(input.source_id, "Plugin invocation input source id");
        require_text(input.file_type, "Plugin invocation input file type");
        require_text(input.path, "Plugin invocation input path");
        output << "{\"port\":"; append_string(output, input.port);
        output << ",\"sourceKind\":"; append_string(output, input.source_kind);
        output << ",\"sourceId\":"; append_string(output, input.source_id);
        output << ",\"fileType\":"; append_string(output, input.file_type);
        output << ",\"path\":"; append_string(output, input.path);
        output << '}';
    }
    output << "],\"outputs\":[";
    for (std::size_t index = 0U; index < document.outputs.size(); ++index) {
        if (index != 0U) output << ',';
        const auto& port = document.outputs[index];
        require_text(port.port, "Plugin invocation output port");
        require_text(port.file_type, "Plugin invocation output file type");
        require_text(port.path, "Plugin invocation output path");
        output << "{\"port\":"; append_string(output, port.port);
        output << ",\"fileType\":"; append_string(output, port.file_type);
        output << ",\"path\":"; append_string(output, port.path);
        output << '}';
    }
    output << "]}";
    std::string result = output.str();
    if (result.size() > maximum_plugin_invocation_bytes) {
        throw std::invalid_argument("Serialized plugin invocation exceeds the maximum size");
    }
    validate_utf8(result);
    return result;
}

}  // namespace biocore::plugin_protocol
