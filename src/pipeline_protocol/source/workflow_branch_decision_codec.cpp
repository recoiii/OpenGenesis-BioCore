#include "biocore/pipeline_protocol/workflow_branch_decision_codec.hpp"

#include <charconv>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace biocore::pipeline_protocol {
namespace {

using biocore::domain::WorkflowBranchDecision;
using biocore::domain::WorkflowBranchDecisionSnapshot;
using biocore::domain::WorkflowId;
using biocore::domain::WorkflowNodeId;

struct JsonValue;
using JsonArray = std::vector<JsonValue>;
using JsonObject = std::map<std::string, JsonValue, std::less<>>;
struct JsonValue final {
    using Storage = std::variant<std::nullptr_t, std::string, std::uint64_t, bool, JsonArray, JsonObject>;
    Storage storage;
};

class Parser final {
public:
    explicit Parser(const std::string_view input) : input_{input} {}

    [[nodiscard]] JsonValue parse_document() {
        skip();
        JsonValue value = parse_value();
        skip();
        if (position_ != input_.size()) {
            throw std::invalid_argument("Branch decision JSON contains trailing content");
        }
        return value;
    }

private:
    [[nodiscard]] JsonValue parse_value() {
        skip();
        if (position_ >= input_.size()) {
            throw std::invalid_argument("Branch decision JSON ended unexpectedly");
        }
        if (input_[position_] == '{') return JsonValue{parse_object()};
        if (input_[position_] == '[') return JsonValue{parse_array()};
        if (input_[position_] == '"') return JsonValue{parse_string()};
        if (input_.substr(position_, 4U) == "true") {
            position_ += 4U;
            return JsonValue{true};
        }
        if (input_.substr(position_, 5U) == "false") {
            position_ += 5U;
            return JsonValue{false};
        }
        if (input_.substr(position_, 4U) == "null") {
            position_ += 4U;
            return JsonValue{nullptr};
        }
        if (input_[position_] >= '0' && input_[position_] <= '9') {
            return JsonValue{parse_unsigned()};
        }
        throw std::invalid_argument("Branch decision JSON value is invalid");
    }

    [[nodiscard]] JsonObject parse_object() {
        consume('{');
        JsonObject result;
        skip();
        if (consume_if('}')) return result;
        while (true) {
            const std::string key = parse_string();
            consume(':');
            if (!result.emplace(key, parse_value()).second) {
                throw std::invalid_argument("Branch decision JSON contains duplicate fields");
            }
            if (consume_if('}')) break;
            consume(',');
        }
        return result;
    }

    [[nodiscard]] JsonArray parse_array() {
        consume('[');
        JsonArray result;
        skip();
        if (consume_if(']')) return result;
        while (true) {
            result.push_back(parse_value());
            if (consume_if(']')) break;
            consume(',');
        }
        return result;
    }

    [[nodiscard]] std::string parse_string() {
        consume('"');
        std::string result;
        while (position_ < input_.size()) {
            const char value = input_[position_++];
            if (value == '"') return result;
            if (static_cast<unsigned char>(value) < 0x20U) {
                throw std::invalid_argument("Branch decision JSON string contains control characters");
            }
            if (value != '\\') {
                result.push_back(value);
                continue;
            }
            if (position_ >= input_.size()) {
                throw std::invalid_argument("Branch decision JSON string escape is incomplete");
            }
            const char escaped = input_[position_++];
            switch (escaped) {
                case '"': result.push_back('"'); break;
                case '\\': result.push_back('\\'); break;
                case '/': result.push_back('/'); break;
                case 'b': result.push_back('\b'); break;
                case 'f': result.push_back('\f'); break;
                case 'n': result.push_back('\n'); break;
                case 'r': result.push_back('\r'); break;
                case 't': result.push_back('\t'); break;
                default:
                    throw std::invalid_argument("Branch decision JSON string escape is invalid");
            }
        }
        throw std::invalid_argument("Branch decision JSON string is unterminated");
    }

    [[nodiscard]] std::uint64_t parse_unsigned() {
        const std::size_t begin = position_;
        if (input_[position_] == '0') {
            ++position_;
            if (position_ < input_.size() &&
                input_[position_] >= '0' && input_[position_] <= '9') {
                throw std::invalid_argument("Branch decision JSON number has a leading zero");
            }
        } else {
            while (position_ < input_.size() &&
                   input_[position_] >= '0' && input_[position_] <= '9') {
                ++position_;
            }
        }
        std::uint64_t result = 0U;
        const auto [end, error] = std::from_chars(
            input_.data() + begin,
            input_.data() + position_,
            result
        );
        if (error != std::errc{} || end != input_.data() + position_) {
            throw std::invalid_argument("Branch decision JSON integer is invalid");
        }
        return result;
    }

    void consume(const char expected) {
        skip();
        if (position_ >= input_.size() || input_[position_] != expected) {
            throw std::invalid_argument("Branch decision JSON punctuation is invalid");
        }
        ++position_;
    }

    [[nodiscard]] bool consume_if(const char expected) {
        skip();
        if (position_ < input_.size() && input_[position_] == expected) {
            ++position_;
            return true;
        }
        return false;
    }

    void skip() {
        while (position_ < input_.size()) {
            const char value = input_[position_];
            if (value != ' ' && value != '\n' && value != '\r' && value != '\t') break;
            ++position_;
        }
    }

    std::string_view input_;
    std::size_t position_{0U};
};

[[nodiscard]] const JsonObject& object_of(const JsonValue& value) {
    const auto* object = std::get_if<JsonObject>(&value.storage);
    if (object == nullptr) {
        throw std::invalid_argument("Branch decision JSON value must be an object");
    }
    return *object;
}

[[nodiscard]] const JsonValue& field(
    const JsonObject& object,
    const std::string_view name
) {
    const auto iterator = object.find(name);
    if (iterator == object.end()) {
        throw std::invalid_argument("Branch decision JSON is missing a required field");
    }
    return iterator->second;
}

[[nodiscard]] std::string string_field(
    const JsonObject& object,
    const std::string_view name
) {
    const auto* value = std::get_if<std::string>(&field(object, name).storage);
    if (value == nullptr) {
        throw std::invalid_argument("Branch decision JSON field must be a string");
    }
    return *value;
}

void require_only(
    const JsonObject& object,
    const std::initializer_list<std::string_view> allowed
) {
    for (const auto& [key, value] : object) {
        static_cast<void>(value);
        bool known = false;
        for (const auto candidate : allowed) {
            if (key == candidate) {
                known = true;
                break;
            }
        }
        if (!known) {
            throw std::invalid_argument("Branch decision JSON contains an unknown field");
        }
    }
}

void append_string(std::ostringstream& output, const std::string_view value) {
    output << '"';
    for (const char character : value) {
        switch (character) {
            case '"': output << "\\\""; break;
            case '\\': output << "\\\\"; break;
            case '\b': output << "\\b"; break;
            case '\f': output << "\\f"; break;
            case '\n': output << "\\n"; break;
            case '\r': output << "\\r"; break;
            case '\t': output << "\\t"; break;
            default:
                if (static_cast<unsigned char>(character) < 0x20U) {
                    throw std::invalid_argument(
                        "Branch decision string contains unsupported control characters"
                    );
                }
                output << character;
        }
    }
    output << '"';
}

}  // namespace

std::string serialize_workflow_branch_decision_snapshot(
    const WorkflowBranchDecisionSnapshot& snapshot
) {
    std::ostringstream output;
    output << "{\"schemaVersion\":" << snapshot.schema_version() << ",\"workflowId\":";
    append_string(output, snapshot.workflow_id().value());
    output << ",\"decisions\":[";
    for (std::size_t index = 0U; index < snapshot.decisions().size(); ++index) {
        if (index != 0U) output << ',';
        const WorkflowBranchDecision& decision = snapshot.decisions()[index];
        output << "{\"nodeId\":";
        append_string(output, decision.node_id.value());
        output << ",\"state\":";
        append_string(output, biocore::domain::to_string(decision.state));
        output << ",\"reason\":";
        append_string(output, biocore::domain::to_string(decision.reason));
        output << ",\"conditionResult\":";
        if (decision.condition_result.has_value()) {
            output << (*decision.condition_result ? "true" : "false");
        } else {
            output << "null";
        }
        output << '}';
    }
    output << "]}";
    std::string result = output.str();
    if (result.size() > maximum_workflow_branch_decision_document_bytes) {
        throw std::invalid_argument("Branch decision JSON exceeds the maximum document size");
    }
    return result;
}

WorkflowBranchDecisionSnapshot parse_workflow_branch_decision_snapshot(
    const std::string_view json
) {
    if (json.empty() || json.size() > maximum_workflow_branch_decision_document_bytes) {
        throw std::invalid_argument("Branch decision JSON size is invalid");
    }

    const JsonValue root_value = Parser{json}.parse_document();
    const JsonObject& root = object_of(root_value);
    require_only(root, {"schemaVersion", "workflowId", "decisions"});

    const auto* version = std::get_if<std::uint64_t>(&field(root, "schemaVersion").storage);
    if (version == nullptr || *version > std::numeric_limits<std::uint32_t>::max()) {
        throw std::invalid_argument("Branch decision schema version is invalid");
    }

    const auto* decisions_value = std::get_if<JsonArray>(&field(root, "decisions").storage);
    if (decisions_value == nullptr) {
        throw std::invalid_argument("Branch decision decisions field must be an array");
    }

    std::vector<WorkflowBranchDecision> decisions;
    decisions.reserve(decisions_value->size());
    for (const JsonValue& value : *decisions_value) {
        const JsonObject& object = object_of(value);
        require_only(object, {"nodeId", "state", "reason", "conditionResult"});

        const auto state = biocore::domain::workflow_branch_decision_state_from_string(
            string_field(object, "state")
        );
        const auto reason = biocore::domain::workflow_branch_decision_reason_from_string(
            string_field(object, "reason")
        );
        if (!state.has_value() || !reason.has_value()) {
            throw std::invalid_argument("Branch decision state or reason is invalid");
        }

        std::optional<bool> condition_result;
        const JsonValue& result_value = field(object, "conditionResult");
        if (const auto* boolean = std::get_if<bool>(&result_value.storage)) {
            condition_result = *boolean;
        } else if (!std::holds_alternative<std::nullptr_t>(result_value.storage)) {
            throw std::invalid_argument("Branch decision conditionResult is invalid");
        }

        decisions.push_back(WorkflowBranchDecision{
            WorkflowNodeId{string_field(object, "nodeId")},
            *state,
            *reason,
            condition_result,
        });
    }

    return WorkflowBranchDecisionSnapshot{
        static_cast<std::uint32_t>(*version),
        WorkflowId{string_field(root, "workflowId")},
        std::move(decisions),
    };
}

}  // namespace biocore::pipeline_protocol
