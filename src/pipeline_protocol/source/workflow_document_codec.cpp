#include "biocore/pipeline_protocol/workflow_document_codec.hpp"

#include <charconv>
#include <cstdint>
#include <initializer_list>
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

using biocore::domain::Workflow;
using biocore::domain::WorkflowEdge;
using biocore::domain::WorkflowId;
using biocore::domain::WorkflowInputDeclaration;
using biocore::domain::WorkflowNode;
using biocore::domain::WorkflowNodeId;
using biocore::domain::WorkflowOutputDeclaration;
using biocore::domain::WorkflowParameters;

constexpr std::size_t maximum_json_depth = 32U;

struct JsonValue;
using JsonArray = std::vector<JsonValue>;
using JsonObject = std::map<std::string, JsonValue, std::less<>>;

struct JsonValue final {
    using Storage = std::variant<std::nullptr_t, std::string, std::uint64_t, bool, JsonArray, JsonObject>;
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
            throw std::invalid_argument("Workflow JSON is not valid UTF-8");
        }
        if (index + length > value.size()) {
            throw std::invalid_argument("Workflow JSON ends inside a UTF-8 sequence");
        }
        for (std::size_t offset = 1U; offset < length; ++offset) {
            const auto continuation = static_cast<unsigned char>(value[index + offset]);
            if ((continuation & 0xC0U) != 0x80U) {
                throw std::invalid_argument("Workflow JSON is not valid UTF-8");
            }
            code_point = (code_point << 6U) | (continuation & 0x3FU);
        }
        if ((length == 3U && code_point < 0x800U) ||
            (length == 4U && code_point < 0x10000U) ||
            code_point > 0x10FFFFU ||
            (code_point >= 0xD800U && code_point <= 0xDFFFU)) {
            throw std::invalid_argument("Workflow JSON contains an invalid UTF-8 code point");
        }
        index += length;
    }
}

[[nodiscard]] std::uint32_t hex_value(const char value) {
    if (value >= '0' && value <= '9') return static_cast<std::uint32_t>(value - '0');
    if (value >= 'a' && value <= 'f') return 10U + static_cast<std::uint32_t>(value - 'a');
    if (value >= 'A' && value <= 'F') return 10U + static_cast<std::uint32_t>(value - 'A');
    throw std::invalid_argument("Workflow JSON escape contains a non-hexadecimal digit");
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
    } else {
        output.push_back(static_cast<char>(0xF0U | (code_point >> 18U)));
        output.push_back(static_cast<char>(0x80U | ((code_point >> 12U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    }
}

class Parser final {
public:
    explicit Parser(const std::string_view input) : input_{input} {}

    [[nodiscard]] JsonValue parse_document() {
        skip_whitespace();
        JsonValue value = parse_value(0U);
        skip_whitespace();
        if (position_ != input_.size()) {
            throw std::invalid_argument("Workflow JSON contains trailing content");
        }
        return value;
    }

private:
    [[nodiscard]] JsonValue parse_value(const std::size_t depth) {
        if (depth > maximum_json_depth) {
            throw std::invalid_argument("Workflow JSON exceeds the maximum nesting depth");
        }
        skip_whitespace();
        if (position_ >= input_.size()) {
            throw std::invalid_argument("Workflow JSON ended unexpectedly");
        }
        switch (input_[position_]) {
            case '{': return JsonValue{parse_object(depth + 1U)};
            case '[': return JsonValue{parse_array(depth + 1U)};
            case '"': return JsonValue{parse_string()};
            case 't':
                consume_literal("true");
                return JsonValue{true};
            case 'f':
                consume_literal("false");
                return JsonValue{false};
            case 'n':
                consume_literal("null");
                return JsonValue{nullptr};
            default:
                if (input_[position_] >= '0' && input_[position_] <= '9') {
                    return JsonValue{parse_unsigned_integer()};
                }
                throw std::invalid_argument("Workflow JSON value is invalid");
        }
    }

    [[nodiscard]] JsonObject parse_object(const std::size_t depth) {
        consume('{');
        JsonObject object;
        skip_whitespace();
        if (consume_if('}')) return object;
        while (true) {
            skip_whitespace();
            if (position_ >= input_.size() || input_[position_] != '"') {
                throw std::invalid_argument("Workflow JSON object key must be a string");
            }
            std::string key = parse_string();
            skip_whitespace();
            consume(':');
            JsonValue value = parse_value(depth);
            if (!object.emplace(std::move(key), std::move(value)).second) {
                throw std::invalid_argument("Workflow JSON object contains a duplicate field");
            }
            skip_whitespace();
            if (consume_if('}')) break;
            consume(',');
        }
        return object;
    }

    [[nodiscard]] JsonArray parse_array(const std::size_t depth) {
        consume('[');
        JsonArray array;
        skip_whitespace();
        if (consume_if(']')) return array;
        while (true) {
            array.push_back(parse_value(depth));
            skip_whitespace();
            if (consume_if(']')) break;
            consume(',');
        }
        return array;
    }

    [[nodiscard]] std::string parse_string() {
        consume('"');
        std::string result;
        while (position_ < input_.size()) {
            const char character = input_[position_++];
            if (character == '"') return result;
            if (static_cast<unsigned char>(character) < 0x20U) {
                throw std::invalid_argument("Workflow JSON string contains a control character");
            }
            if (character != '\\') {
                result.push_back(character);
                continue;
            }
            if (position_ >= input_.size()) {
                throw std::invalid_argument("Workflow JSON string escape is incomplete");
            }
            const char escape = input_[position_++];
            switch (escape) {
                case '"': result.push_back('"'); break;
                case '\\': result.push_back('\\'); break;
                case '/': result.push_back('/'); break;
                case 'b': result.push_back('\b'); break;
                case 'f': result.push_back('\f'); break;
                case 'n': result.push_back('\n'); break;
                case 'r': result.push_back('\r'); break;
                case 't': result.push_back('\t'); break;
                case 'u': {
                    std::uint32_t code_point = parse_hex_quad();
                    if (code_point >= 0xD800U && code_point <= 0xDBFFU) {
                        if (position_ + 2U > input_.size() ||
                            input_[position_] != '\\' || input_[position_ + 1U] != 'u') {
                            throw std::invalid_argument("Workflow JSON contains an incomplete surrogate pair");
                        }
                        position_ += 2U;
                        const std::uint32_t low = parse_hex_quad();
                        if (low < 0xDC00U || low > 0xDFFFU) {
                            throw std::invalid_argument("Workflow JSON contains an invalid surrogate pair");
                        }
                        code_point = 0x10000U + ((code_point - 0xD800U) << 10U) +
                                     (low - 0xDC00U);
                    } else if (code_point >= 0xDC00U && code_point <= 0xDFFFU) {
                        throw std::invalid_argument("Workflow JSON contains an unpaired low surrogate");
                    }
                    append_utf8(result, code_point);
                    break;
                }
                default:
                    throw std::invalid_argument("Workflow JSON string escape is invalid");
            }
        }
        throw std::invalid_argument("Workflow JSON string is unterminated");
    }

    [[nodiscard]] std::uint32_t parse_hex_quad() {
        if (position_ + 4U > input_.size()) {
            throw std::invalid_argument("Workflow JSON unicode escape is incomplete");
        }
        std::uint32_t value = 0U;
        for (std::size_t index = 0U; index < 4U; ++index) {
            value = (value << 4U) | hex_value(input_[position_++]);
        }
        return value;
    }

    [[nodiscard]] std::uint64_t parse_unsigned_integer() {
        const std::size_t begin = position_;
        if (input_[position_] == '0') {
            ++position_;
            if (position_ < input_.size() &&
                input_[position_] >= '0' && input_[position_] <= '9') {
                throw std::invalid_argument("Workflow JSON number contains a leading zero");
            }
        } else {
            while (position_ < input_.size() &&
                   input_[position_] >= '0' && input_[position_] <= '9') {
                ++position_;
            }
        }
        std::uint64_t value = 0U;
        const auto [end, error] = std::from_chars(
            input_.data() + begin, input_.data() + position_, value
        );
        if (error != std::errc{} || end != input_.data() + position_) {
            throw std::invalid_argument("Workflow JSON integer is invalid");
        }
        return value;
    }

    void consume_literal(const std::string_view literal) {
        if (input_.substr(position_, literal.size()) != literal) {
            throw std::invalid_argument("Workflow JSON literal is invalid");
        }
        position_ += literal.size();
    }

    void consume(const char expected) {
        skip_whitespace();
        if (position_ >= input_.size() || input_[position_] != expected) {
            throw std::invalid_argument("Workflow JSON punctuation is invalid");
        }
        ++position_;
    }

    [[nodiscard]] bool consume_if(const char expected) {
        skip_whitespace();
        if (position_ < input_.size() && input_[position_] == expected) {
            ++position_;
            return true;
        }
        return false;
    }

    void skip_whitespace() {
        while (position_ < input_.size()) {
            const char value = input_[position_];
            if (value != ' ' && value != '\t' && value != '\n' && value != '\r') break;
            ++position_;
        }
    }

    std::string_view input_;
    std::size_t position_{0U};
};

void validate_document(const std::string_view json) {
    if (json.empty() || json.size() > maximum_workflow_document_bytes) {
        throw std::invalid_argument("Workflow document size is invalid");
    }
    validate_utf8(json);
}

[[nodiscard]] const JsonObject& require_object(
    const JsonValue& value,
    const std::string_view field
) {
    const auto* object = std::get_if<JsonObject>(&value.storage);
    if (object == nullptr) {
        throw std::invalid_argument(std::string{field} + " must be an object");
    }
    return *object;
}

[[nodiscard]] const JsonArray& require_array(
    const JsonValue& value,
    const std::string_view field
) {
    const auto* array = std::get_if<JsonArray>(&value.storage);
    if (array == nullptr) {
        throw std::invalid_argument(std::string{field} + " must be an array");
    }
    return *array;
}

[[nodiscard]] const JsonValue& require_field(
    const JsonObject& object,
    const std::string_view name
) {
    const auto iterator = object.find(name);
    if (iterator == object.end()) {
        throw std::invalid_argument(
            "Workflow document is missing a required field: " + std::string{name}
        );
    }
    return iterator->second;
}

[[nodiscard]] std::string require_string(
    const JsonObject& object,
    const std::string_view name
) {
    const auto* value = std::get_if<std::string>(&require_field(object, name).storage);
    if (value == nullptr) {
        throw std::invalid_argument("Workflow field must be a string: " + std::string{name});
    }
    return *value;
}

[[nodiscard]] std::string optional_string(
    const JsonObject& object,
    const std::string_view name
) {
    const auto iterator = object.find(name);
    if (iterator == object.end()) return {};
    const auto* value = std::get_if<std::string>(&iterator->second.storage);
    if (value == nullptr) {
        throw std::invalid_argument("Workflow field must be a string: " + std::string{name});
    }
    return *value;
}

[[nodiscard]] bool require_bool(
    const JsonObject& object,
    const std::string_view name
) {
    const auto* value = std::get_if<bool>(&require_field(object, name).storage);
    if (value == nullptr) {
        throw std::invalid_argument("Workflow field must be a boolean: " + std::string{name});
    }
    return *value;
}

[[nodiscard]] std::uint32_t require_schema_version(const JsonObject& object) {
    const auto* value = std::get_if<std::uint64_t>(&require_field(object, "schemaVersion").storage);
    if (value == nullptr || *value > std::numeric_limits<std::uint32_t>::max()) {
        throw std::invalid_argument("Workflow schema version is invalid");
    }
    return static_cast<std::uint32_t>(*value);
}

void require_only_fields(
    const JsonObject& object,
    const std::initializer_list<std::string_view> allowed
) {
    for (const auto& [key, value] : object) {
        static_cast<void>(value);
        bool found = false;
        for (const std::string_view candidate : allowed) {
            if (key == candidate) {
                found = true;
                break;
            }
        }
        if (!found) {
            throw std::invalid_argument("Workflow document contains an unknown field: " + key);
        }
    }
}

[[nodiscard]] std::vector<WorkflowInputDeclaration> parse_inputs(const JsonValue& value) {
    const JsonArray& array = require_array(value, "Workflow node inputs");
    std::vector<WorkflowInputDeclaration> result;
    result.reserve(array.size());
    for (const JsonValue& element : array) {
        const JsonObject& object = require_object(element, "Workflow input");
        require_only_fields(object, {"name", "artifactType", "required"});
        result.emplace_back(
            require_string(object, "name"),
            require_string(object, "artifactType"),
            require_bool(object, "required")
        );
    }
    return result;
}

[[nodiscard]] std::vector<WorkflowOutputDeclaration> parse_outputs(const JsonValue& value) {
    const JsonArray& array = require_array(value, "Workflow node outputs");
    std::vector<WorkflowOutputDeclaration> result;
    result.reserve(array.size());
    for (const JsonValue& element : array) {
        const JsonObject& object = require_object(element, "Workflow output");
        require_only_fields(object, {"name", "artifactType"});
        result.emplace_back(
            require_string(object, "name"),
            require_string(object, "artifactType")
        );
    }
    return result;
}

[[nodiscard]] WorkflowParameters parse_parameters(const JsonValue& value) {
    const JsonObject& object = require_object(value, "Workflow node parameters");
    WorkflowParameters result;
    for (const auto& [key, json_value] : object) {
        const auto* text = std::get_if<std::string>(&json_value.storage);
        if (text == nullptr) {
            throw std::invalid_argument("Workflow parameter values must be strings");
        }
        result.emplace(key, *text);
    }
    return result;
}

[[nodiscard]] std::vector<WorkflowNode> parse_nodes(const JsonValue& value) {
    const JsonArray& array = require_array(value, "Workflow nodes");
    std::vector<WorkflowNode> result;
    result.reserve(array.size());
    for (const JsonValue& element : array) {
        const JsonObject& object = require_object(element, "Workflow node");
        require_only_fields(
            object,
            {"id", "label", "moduleId", "pluginVersion", "inputs", "outputs", "parameters"}
        );
        result.emplace_back(
            WorkflowNodeId{require_string(object, "id")},
            require_string(object, "label"),
            require_string(object, "moduleId"),
            require_string(object, "pluginVersion"),
            parse_inputs(require_field(object, "inputs")),
            parse_outputs(require_field(object, "outputs")),
            parse_parameters(require_field(object, "parameters"))
        );
    }
    return result;
}

[[nodiscard]] std::vector<WorkflowEdge> parse_edges(const JsonValue& value) {
    const JsonArray& array = require_array(value, "Workflow edges");
    std::vector<WorkflowEdge> result;
    result.reserve(array.size());
    for (const JsonValue& element : array) {
        const JsonObject& object = require_object(element, "Workflow edge");
        require_only_fields(
            object,
            {"sourceNode", "sourceOutput", "targetNode", "targetInput"}
        );
        result.emplace_back(
            WorkflowNodeId{require_string(object, "sourceNode")},
            require_string(object, "sourceOutput"),
            WorkflowNodeId{require_string(object, "targetNode")},
            require_string(object, "targetInput")
        );
    }
    return result;
}

void append_string(std::ostringstream& output, const std::string_view value) {
    validate_utf8(value);
    output << '"';
    constexpr char hex[] = "0123456789abcdef";
    for (const unsigned char character : value) {
        switch (character) {
            case '"': output << "\\\""; break;
            case '\\': output << "\\\\"; break;
            case '\b': output << "\\b"; break;
            case '\f': output << "\\f"; break;
            case '\n': output << "\\n"; break;
            case '\r': output << "\\r"; break;
            case '\t': output << "\\t"; break;
            default:
                if (character < 0x20U) {
                    output << "\\u00" << hex[(character >> 4U) & 0x0FU]
                           << hex[character & 0x0FU];
                } else {
                    output << static_cast<char>(character);
                }
        }
    }
    output << '"';
}

void append_inputs(
    std::ostringstream& output,
    const std::vector<WorkflowInputDeclaration>& inputs
) {
    output << '[';
    for (std::size_t index = 0U; index < inputs.size(); ++index) {
        if (index != 0U) output << ',';
        const WorkflowInputDeclaration& input = inputs[index];
        output << "{\"name\":";
        append_string(output, input.name());
        output << ",\"artifactType\":";
        append_string(output, input.artifact_type());
        output << ",\"required\":" << (input.required() ? "true" : "false") << '}';
    }
    output << ']';
}

void append_outputs(
    std::ostringstream& output,
    const std::vector<WorkflowOutputDeclaration>& outputs
) {
    output << '[';
    for (std::size_t index = 0U; index < outputs.size(); ++index) {
        if (index != 0U) output << ',';
        const WorkflowOutputDeclaration& declaration = outputs[index];
        output << "{\"name\":";
        append_string(output, declaration.name());
        output << ",\"artifactType\":";
        append_string(output, declaration.artifact_type());
        output << '}';
    }
    output << ']';
}

void append_parameters(std::ostringstream& output, const WorkflowParameters& parameters) {
    output << '{';
    bool first = true;
    for (const auto& [name, value] : parameters) {
        if (!first) output << ',';
        first = false;
        append_string(output, name);
        output << ':';
        append_string(output, value);
    }
    output << '}';
}

void append_nodes(std::ostringstream& output, const std::vector<WorkflowNode>& nodes) {
    output << '[';
    for (std::size_t index = 0U; index < nodes.size(); ++index) {
        if (index != 0U) output << ',';
        const WorkflowNode& node = nodes[index];
        output << "{\"id\":";
        append_string(output, node.id().value());
        output << ",\"label\":";
        append_string(output, node.label());
        output << ",\"moduleId\":";
        append_string(output, node.module_id());
        output << ",\"pluginVersion\":";
        append_string(output, node.plugin_version());
        output << ",\"inputs\":";
        append_inputs(output, node.inputs());
        output << ",\"outputs\":";
        append_outputs(output, node.outputs());
        output << ",\"parameters\":";
        append_parameters(output, node.parameters());
        output << '}';
    }
    output << ']';
}

void append_edges(std::ostringstream& output, const std::vector<WorkflowEdge>& edges) {
    output << '[';
    for (std::size_t index = 0U; index < edges.size(); ++index) {
        if (index != 0U) output << ',';
        const WorkflowEdge& edge = edges[index];
        output << "{\"sourceNode\":";
        append_string(output, edge.source_node_id().value());
        output << ",\"sourceOutput\":";
        append_string(output, edge.source_output());
        output << ",\"targetNode\":";
        append_string(output, edge.target_node_id().value());
        output << ",\"targetInput\":";
        append_string(output, edge.target_input());
        output << '}';
    }
    output << ']';
}

}  // namespace

Workflow parse_workflow_document(const std::string_view json) {
    validate_document(json);
    const JsonValue root_value = Parser{json}.parse_document();
    const JsonObject& root = require_object(root_value, "Workflow");
    require_only_fields(root, {"schemaVersion", "id", "name", "description", "nodes", "edges"});

    return Workflow{
        require_schema_version(root),
        WorkflowId{require_string(root, "id")},
        require_string(root, "name"),
        optional_string(root, "description"),
        parse_nodes(require_field(root, "nodes")),
        parse_edges(require_field(root, "edges")),
    };
}

std::string serialize_workflow_document(const Workflow& workflow) {
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << "{\"schemaVersion\":" << workflow.schema_version() << ",\"id\":";
    append_string(output, workflow.id().value());
    output << ",\"name\":";
    append_string(output, workflow.name());
    output << ",\"description\":";
    append_string(output, workflow.description());
    output << ",\"nodes\":";
    append_nodes(output, workflow.nodes());
    output << ",\"edges\":";
    append_edges(output, workflow.edges());
    output << '}';

    std::string result = output.str();
    if (result.size() > maximum_workflow_document_bytes) {
        throw std::invalid_argument("Serialized workflow document exceeds the maximum size");
    }
    return result;
}

}  // namespace biocore::pipeline_protocol
