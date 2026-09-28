#include "biocore/presentation/project_workspace_json.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "biocore/application/batch_plan.hpp"
#include "biocore/application/batch_results.hpp"
#include "biocore/domain/job_priority.hpp"
#include "biocore/domain/job_status.hpp"
#include "biocore/domain/project_sample_binding.hpp"
#include "biocore/domain/storage_mode.hpp"

namespace biocore::presentation {
namespace {

[[nodiscard]] std::string escape_json(const std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size());
    constexpr std::array<char, 16> hexadecimal{
        '0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f'};
    for (const char raw : value) {
        const auto character = static_cast<unsigned char>(raw);
        switch (character) {
            case '"': escaped += "\\\""; break;
            case '\\': escaped += "\\\\"; break;
            case '\b': escaped += "\\b"; break;
            case '\f': escaped += "\\f"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default:
                if (character < 0x20U) {
                    escaped += "\\u00";
                    escaped += hexadecimal[(character >> 4U) & 0x0fU];
                    escaped += hexadecimal[character & 0x0fU];
                } else {
                    escaped += static_cast<char>(character);
                }
                break;
        }
    }
    return escaped;
}

[[nodiscard]] std::string quote(const std::string_view value) {
    return "\"" + escape_json(value) + "\"";
}

[[nodiscard]] std::string optional_string(const std::optional<std::string>& value) {
    return value.has_value() ? quote(*value) : "null";
}

[[nodiscard]] std::string bool_json(const bool value) { return value ? "true" : "false"; }

[[nodiscard]] std::string reference_status_name(
    const application::SampleBindingReferenceStatus status
) {
    switch (status) {
        case application::SampleBindingReferenceStatus::not_requested: return "not_requested";
        case application::SampleBindingReferenceStatus::not_declared_by_format: return "not_declared_by_format";
        case application::SampleBindingReferenceStatus::verified: return "verified";
        case application::SampleBindingReferenceStatus::mismatch: return "mismatch";
        case application::SampleBindingReferenceStatus::evidence_unavailable: return "evidence_unavailable";
    }
    return "evidence_unavailable";
}

[[nodiscard]] std::string metric_kind_name(const application::BatchQcMetricValueKind kind) {
    switch (kind) {
        case application::BatchQcMetricValueKind::integer: return "integer";
        case application::BatchQcMetricValueKind::number: return "number";
        case application::BatchQcMetricValueKind::boolean: return "boolean";
        case application::BatchQcMetricValueKind::string: return "string";
        case application::BatchQcMetricValueKind::null_value: return "null";
    }
    return "string";
}

[[nodiscard]] std::string render_binding(const domain::ProjectSampleBinding& binding) {
    return "{\"layout\":" + quote(domain::to_string(binding.layout())) +
           ",\"primaryFileId\":" + quote(binding.primary_file_id()) +
           ",\"secondaryFileId\":" + optional_string(binding.secondary_file_id()) +
           ",\"referenceFileId\":" + optional_string(binding.reference_file_id()) + "}";
}

[[nodiscard]] std::string render_execution_sample(
    const application::BatchExecutionSampleView& sample
) {
    return "{\"sampleId\":" + quote(sample.sample_id) +
           ",\"jobId\":" + quote(sample.job_id) +
           ",\"status\":" + quote(domain::to_string(sample.status)) +
           ",\"progress\":" + std::to_string(sample.progress) +
           ",\"attemptNumber\":" + std::to_string(sample.attempt_number) +
           ",\"attemptMode\":" + quote(application::to_string(sample.attempt_mode)) +
           ",\"parentJobId\":" + optional_string(sample.parent_job_id) + "}";
}

[[nodiscard]] std::string render_artifact(const application::BatchResultArtifactLink& artifact) {
    return "{\"sampleId\":" + quote(artifact.sample_id) +
           ",\"jobId\":" + quote(artifact.job_id) +
           ",\"attemptNumber\":" + std::to_string(artifact.attempt_number) +
           ",\"attemptMode\":" + quote(application::to_string(artifact.attempt_mode)) +
           ",\"managedFileId\":" + quote(artifact.managed_file_id) +
           ",\"stepId\":" + quote(artifact.step_id) +
           ",\"outputPort\":" + quote(artifact.output_port) +
           ",\"moduleId\":" + quote(artifact.module_id) +
           ",\"pluginVersion\":" + quote(artifact.plugin_version) +
           ",\"fileType\":" + quote(artifact.file_type) +
           ",\"relativeProjectPath\":" + quote(artifact.relative_project_path) +
           ",\"sizeBytes\":" + std::to_string(artifact.size_bytes) +
           ",\"sha256\":" + optional_string(artifact.sha256) + "}";
}

struct JsonValue final {
    using Array = std::vector<JsonValue>;
    using Object = std::map<std::string, JsonValue, std::less<>>;
    std::variant<std::nullptr_t, bool, std::int64_t, std::string, Array, Object> value{nullptr};
};

void append_utf8(std::string& output, const std::uint32_t codepoint) {
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
        throw std::invalid_argument{"JSON Unicode codepoint is invalid"};
    }
}

class JsonParser final {
public:
    explicit JsonParser(const std::string_view text) : text_{text} {}

    [[nodiscard]] JsonValue parse() {
        skip_space();
        JsonValue result = parse_value(0U);
        skip_space();
        if (position_ != text_.size()) fail("Trailing JSON data");
        return result;
    }

private:
    static constexpr std::size_t maximum_depth = 16U;
    static constexpr std::size_t maximum_collection = 20000U;

    [[noreturn]] void fail(const std::string_view message) const {
        throw std::invalid_argument{std::string{message}};
    }

    void skip_space() {
        while (position_ < text_.size()) {
            const char c = text_[position_];
            if (c != ' ' && c != '\n' && c != '\r' && c != '\t') break;
            ++position_;
        }
    }

    [[nodiscard]] bool consume(const char c) {
        if (position_ < text_.size() && text_[position_] == c) {
            ++position_;
            return true;
        }
        return false;
    }

    void require(const char c) {
        if (!consume(c)) fail("Malformed JSON");
    }

    [[nodiscard]] bool literal(const std::string_view value) {
        if (text_.substr(position_, value.size()) != value) return false;
        position_ += value.size();
        return true;
    }

    [[nodiscard]] std::uint32_t hex4() {
        if (position_ + 4U > text_.size()) fail("Truncated JSON Unicode escape");
        std::uint32_t result = 0U;
        for (std::size_t index = 0U; index < 4U; ++index) {
            const char c = text_[position_++];
            result <<= 4U;
            if (c >= '0' && c <= '9') result |= static_cast<std::uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') result |= static_cast<std::uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') result |= static_cast<std::uint32_t>(c - 'A' + 10);
            else fail("Invalid JSON Unicode escape");
        }
        return result;
    }

    [[nodiscard]] std::string parse_string() {
        require('"');
        std::string output;
        while (position_ < text_.size()) {
            const char c = text_[position_++];
            if (c == '"') return output;
            if (static_cast<unsigned char>(c) < 0x20U) fail("Control character in JSON string");
            if (c != '\\') {
                output.push_back(c);
                continue;
            }
            if (position_ >= text_.size()) fail("Truncated JSON escape");
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
                        if (position_ + 2U > text_.size() || text_[position_] != '\\' || text_[position_ + 1U] != 'u') {
                            fail("Unpaired JSON high surrogate");
                        }
                        position_ += 2U;
                        const std::uint32_t low = hex4();
                        if (low < 0xdc00U || low > 0xdfffU) fail("Invalid JSON surrogate pair");
                        codepoint = 0x10000U + ((codepoint - 0xd800U) << 10U) + (low - 0xdc00U);
                    } else if (codepoint >= 0xdc00U && codepoint <= 0xdfffU) {
                        fail("Unpaired JSON low surrogate");
                    }
                    append_utf8(output, codepoint);
                    break;
                }
                default: fail("Unsupported JSON escape");
            }
        }
        fail("Unterminated JSON string");
    }

    [[nodiscard]] std::int64_t parse_integer() {
        const std::size_t start = position_;
        if (consume('-')) {
            if (position_ >= text_.size()) fail("Invalid JSON integer");
        }
        if (consume('0')) {
            if (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9') {
                fail("JSON integer has a leading zero");
            }
        } else {
            if (position_ >= text_.size() || text_[position_] < '1' || text_[position_] > '9') {
                fail("Invalid JSON integer");
            }
            while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9') ++position_;
        }
        if (position_ < text_.size() && (text_[position_] == '.' || text_[position_] == 'e' || text_[position_] == 'E')) {
            fail("Workspace JSON numbers must be integers");
        }
        std::int64_t value = 0;
        const auto parsed = std::from_chars(text_.data() + start, text_.data() + position_, value);
        if (parsed.ec != std::errc{} || parsed.ptr != text_.data() + position_) fail("JSON integer is out of range");
        return value;
    }

    [[nodiscard]] JsonValue parse_array(const std::size_t depth) {
        require('[');
        skip_space();
        JsonValue::Array values;
        if (consume(']')) return JsonValue{std::move(values)};
        for (;;) {
            if (values.size() >= maximum_collection) fail("JSON array is too large");
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
            if (values.size() >= maximum_collection) fail("JSON object is too large");
            const std::string key = parse_string();
            if (values.contains(key)) fail("Duplicate JSON object key");
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
        if (depth > maximum_depth) fail("JSON nesting is too deep");
        skip_space();
        if (position_ >= text_.size()) fail("JSON value is missing");
        const char c = text_[position_];
        if (c == '"') return JsonValue{parse_string()};
        if (c == '{') return parse_object(depth);
        if (c == '[') return parse_array(depth);
        if (c == '-' || (c >= '0' && c <= '9')) return JsonValue{parse_integer()};
        if (literal("true")) return JsonValue{true};
        if (literal("false")) return JsonValue{false};
        if (literal("null")) return JsonValue{nullptr};
        fail("Unsupported JSON value");
    }

    std::string_view text_;
    std::size_t position_{0U};
};

[[nodiscard]] const JsonValue::Object& object(const JsonValue& value, const std::string_view what) {
    const auto* result = std::get_if<JsonValue::Object>(&value.value);
    if (result == nullptr) throw std::invalid_argument{std::string{what} + " must be an object"};
    return *result;
}

[[nodiscard]] const JsonValue::Array& array(const JsonValue& value, const std::string_view what) {
    const auto* result = std::get_if<JsonValue::Array>(&value.value);
    if (result == nullptr) throw std::invalid_argument{std::string{what} + " must be an array"};
    return *result;
}

[[nodiscard]] const JsonValue& required(const JsonValue::Object& value, const std::string_view key) {
    const auto found = value.find(key);
    if (found == value.end()) throw std::invalid_argument{"Missing workspace JSON field: " + std::string{key}};
    return found->second;
}

[[nodiscard]] std::string string_value(const JsonValue& value, const std::string_view what, const std::size_t maximum = 4096U) {
    const auto* result = std::get_if<std::string>(&value.value);
    if (result == nullptr || result->size() > maximum) throw std::invalid_argument{std::string{what} + " must be a bounded string"};
    return *result;
}

[[nodiscard]] std::optional<std::string> optional_string_value(
    const JsonValue::Object& value,
    const std::string_view key,
    const std::size_t maximum = 4096U
) {
    const auto found = value.find(key);
    if (found == value.end() || std::holds_alternative<std::nullptr_t>(found->second.value)) return std::nullopt;
    return string_value(found->second, key, maximum);
}

[[nodiscard]] std::int64_t integer_value(const JsonValue& value, const std::string_view what) {
    const auto* result = std::get_if<std::int64_t>(&value.value);
    if (result == nullptr) throw std::invalid_argument{std::string{what} + " must be an integer"};
    return *result;
}

[[nodiscard]] std::vector<std::string> string_array(
    const JsonValue& value,
    const std::string_view what,
    const std::size_t maximum_items
) {
    const auto& values = array(value, what);
    if (values.size() > maximum_items) throw std::invalid_argument{std::string{what} + " contains too many items"};
    std::vector<std::string> result;
    result.reserve(values.size());
    for (const auto& item : values) result.push_back(string_value(item, what, 256U));
    return result;
}

void reject_unknown(const JsonValue::Object& value, const std::vector<std::string_view>& allowed) {
    for (const auto& [key, unused] : value) {
        static_cast<void>(unused);
        if (std::ranges::find(allowed, key) == allowed.end()) {
            throw std::invalid_argument{"Unknown workspace JSON field: " + key};
        }
    }
}

[[nodiscard]] domain::JobPriority priority_from_object(const JsonValue::Object& root) {
    const auto found = root.find("priority");
    if (found == root.end()) return domain::JobPriority::normal;
    const auto parsed = domain::job_priority_from_string(string_value(found->second, "priority", 16U));
    if (!parsed.has_value()) throw std::invalid_argument{"priority must be low, normal, or high"};
    return *parsed;
}

}  // namespace

std::string render_project_workspace_snapshot(const application::ProjectWorkspaceSnapshot& snapshot) {
    std::string body = "{\"project\":{\"id\":" + quote(snapshot.project.id()) +
                       ",\"name\":" + quote(snapshot.project.name()) +
                       ",\"rootPath\":" + quote(snapshot.project.root_path()) +
                       ",\"createdAtUtc\":" + quote(snapshot.project.created_at_utc()) +
                       ",\"updatedAtUtc\":" + quote(snapshot.project.updated_at_utc()) + "}";
    if (snapshot.research.has_value()) {
        body += ",\"research\":{\"description\":" + quote(snapshot.research->description()) +
                ",\"organism\":" + quote(snapshot.research->organism()) +
                ",\"revision\":" + std::to_string(snapshot.research->revision()) +
                ",\"updatedAtUtc\":" + quote(snapshot.research->updated_at_utc()) + "}";
    } else {
        body += ",\"research\":null";
    }
    body += ",\"samples\":[";
    for (std::size_t index = 0U; index < snapshot.samples.size(); ++index) {
        if (index != 0U) body += ',';
        const auto& value = snapshot.samples[index];
        body += "{\"sampleId\":" + quote(value.sample.sample_id()) +
                ",\"displayName\":" + quote(value.sample.display_name()) +
                ",\"group\":" + quote(value.sample.group_label()) +
                ",\"bindingComplete\":" + bool_json(value.binding_complete) +
                ",\"binding\":" + (value.binding.has_value() ? render_binding(*value.binding) : "null") +
                ",\"issues\":[";
        for (std::size_t issue = 0U; issue < value.issues.size(); ++issue) {
            if (issue != 0U) body += ',';
            body += "{\"code\":" + quote(value.issues[issue].code) +
                    ",\"message\":" + quote(value.issues[issue].message) + "}";
        }
        body += "]}";
    }
    body += "],\"files\":[";
    for (std::size_t index = 0U; index < snapshot.files.size(); ++index) {
        if (index != 0U) body += ',';
        const auto& file = snapshot.files[index];
        body += "{\"id\":" + quote(file.file_id) +
                ",\"displayName\":" + quote(file.display_name) +
                ",\"fileType\":" + quote(file.file_type) +
                ",\"storageMode\":" + quote(domain::to_string(file.storage_mode)) +
                ",\"sizeBytes\":" + std::to_string(file.size_bytes) +
                ",\"orphaned\":" + bool_json(file.orphaned) + "}";
    }
    body += "],\"batches\":[";
    for (std::size_t index = 0U; index < snapshot.batches.size(); ++index) {
        if (index != 0U) body += ',';
        const auto& batch = snapshot.batches[index];
        body += "{\"planId\":" + quote(batch.plan_id) +
                ",\"templateId\":" + quote(batch.template_id) +
                ",\"templateVersion\":" + quote(batch.template_version) +
                ",\"approvedAtUtc\":" + quote(batch.approved_at_utc) +
                ",\"includedSamples\":" + std::to_string(batch.included_samples) +
                ",\"excludedSamples\":" + std::to_string(batch.excluded_samples) +
                ",\"recoveryAttention\":" + std::to_string(batch.recovery_attention) +
                ",\"execution\":" + (batch.execution.has_value() ? render_batch_execution_snapshot(*batch.execution) : "null") + "}";
    }
    body += "]}";
    return body;
}

std::string render_sample_import_preview(const application::SampleImportPreview& preview) {
    std::string body = "{\"valid\":" + bool_json(preview.valid()) + ",\"samples\":[";
    const auto& samples = preview.samples();
    for (std::size_t index = 0U; index < samples.size(); ++index) {
        if (index != 0U) body += ',';
        body += "{\"sampleId\":" + quote(samples[index].sample_id()) +
                ",\"displayName\":" + quote(samples[index].display_name()) +
                ",\"group\":" + quote(samples[index].group_label()) + "}";
    }
    body += "],\"issues\":[";
    const auto& issues = preview.issues();
    for (std::size_t index = 0U; index < issues.size(); ++index) {
        if (index != 0U) body += ',';
        body += "{\"line\":" + std::to_string(issues[index].line) +
                ",\"code\":" + quote(issues[index].code) +
                ",\"message\":" + quote(issues[index].message) + "}";
    }
    body += "]}";
    return body;
}

std::string render_sample_binding_preview(const application::SampleBindingPreview& preview) {
    std::string body = "{\"valid\":" + bool_json(preview.valid()) +
                       ",\"referenceStatus\":" + quote(reference_status_name(preview.reference_status)) +
                       ",\"binding\":" + render_binding(preview.binding) + ",\"issues\":[";
    for (std::size_t index = 0U; index < preview.issues.size(); ++index) {
        if (index != 0U) body += ',';
        const auto& issue = preview.issues[index];
        body += "{\"severity\":" + quote(issue.severity == application::SampleBindingIssueSeverity::blocker ? "blocker" : "warning") +
                ",\"code\":" + quote(issue.code) + ",\"message\":" + quote(issue.message) + "}";
    }
    body += "]}";
    return body;
}

std::string render_batch_plan_preview(const application::BatchPlanPreview& preview) {
    std::string body = "{\"planId\":" + quote(preview.plan_id) +
                       ",\"projectId\":" + quote(preview.project_id) +
                       ",\"templateId\":" + quote(preview.template_id) +
                       ",\"templateVersion\":" + quote(preview.template_version) +
                       ",\"globallyValid\":" + bool_json(preview.globally_valid()) +
                       ",\"issues\":[";
    for (std::size_t index = 0U; index < preview.issues.size(); ++index) {
        if (index != 0U) body += ',';
        body += "{\"severity\":" + quote(preview.issues[index].severity == application::BatchPlanIssueSeverity::blocker ? "blocker" : "warning") +
                ",\"code\":" + quote(preview.issues[index].code) +
                ",\"message\":" + quote(preview.issues[index].message) + "}";
    }
    body += "],\"samples\":[";
    for (std::size_t index = 0U; index < preview.samples.size(); ++index) {
        if (index != 0U) body += ',';
        const auto& sample = preview.samples[index];
        body += "{\"sampleId\":" + quote(sample.sample_id) +
                ",\"workflowId\":" + quote(sample.workflow_id) +
                ",\"valid\":" + bool_json(sample.valid()) +
                ",\"nodeCount\":" + std::to_string(sample.nodes.size()) +
                ",\"issues\":[";
        for (std::size_t issue = 0U; issue < sample.issues.size(); ++issue) {
            if (issue != 0U) body += ',';
            body += "{\"severity\":" + quote(sample.issues[issue].severity == application::BatchPlanIssueSeverity::blocker ? "blocker" : "warning") +
                    ",\"code\":" + quote(sample.issues[issue].code) +
                    ",\"message\":" + quote(sample.issues[issue].message) + "}";
        }
        body += "]}";
    }
    body += "]}";
    return body;
}

std::string render_approved_batch_plan(const application::ApprovedBatchPlan& plan) {
    std::string body = "{\"planId\":" + quote(plan.plan_id) +
                       ",\"projectId\":" + quote(plan.project_id) +
                       ",\"templateId\":" + quote(plan.template_id) +
                       ",\"templateVersion\":" + quote(plan.template_version) +
                       ",\"approvedAtUtc\":" + quote(plan.approved_at_utc) +
                       ",\"samples\":[";
    for (std::size_t index = 0U; index < plan.samples.size(); ++index) {
        if (index != 0U) body += ',';
        body += "{\"sampleId\":" + quote(plan.samples[index].sample_id) +
                ",\"disposition\":" + quote(application::to_string(plan.samples[index].disposition)) + "}";
    }
    body += "]}";
    return body;
}

std::string render_batch_execution_snapshot(const application::BatchExecutionSnapshot& snapshot) {
    std::string body = "{\"planId\":" + quote(snapshot.plan_id) +
                       ",\"state\":" + quote(application::to_string(snapshot.state)) +
                       ",\"maximumConcurrentJobs\":" + std::to_string(snapshot.maximum_concurrent_jobs) +
                       ",\"cancellationRequested\":" + bool_json(snapshot.cancellation_requested) +
                       ",\"completedCount\":" + std::to_string(snapshot.completed_count) +
                       ",\"failedCount\":" + std::to_string(snapshot.failed_count) +
                       ",\"cancelledCount\":" + std::to_string(snapshot.cancelled_count) +
                       ",\"interruptedCount\":" + std::to_string(snapshot.interrupted_count) +
                       ",\"activeCount\":" + std::to_string(snapshot.active_count) +
                       ",\"samples\":[";
    for (std::size_t index = 0U; index < snapshot.samples.size(); ++index) {
        if (index != 0U) body += ',';
        body += render_execution_sample(snapshot.samples[index]);
    }
    body += "]}";
    return body;
}

std::string render_batch_recovery_inspection(const application::BatchRecoveryInspection& inspection) {
    std::string body = "{\"planId\":" + quote(inspection.plan_id) + ",\"samples\":[";
    for (std::size_t index = 0U; index < inspection.samples.size(); ++index) {
        if (index != 0U) body += ',';
        const auto& sample = inspection.samples[index];
        body += "{\"sampleId\":" + quote(sample.sample_id) +
                ",\"jobId\":" + quote(sample.job_id) +
                ",\"attemptNumber\":" + std::to_string(sample.attempt_number) +
                ",\"action\":" + quote(application::to_string(sample.action)) +
                ",\"reason\":" + quote(sample.reason) + "}";
    }
    body += "]}";
    return body;
}

std::string render_batch_results_overview(const application::BatchResultsOverview& overview) {
    std::string body = "{\"planId\":" + quote(overview.plan_id) +
                       ",\"projectId\":" + quote(overview.project_id) +
                       ",\"templateId\":" + quote(overview.template_id) +
                       ",\"templateVersion\":" + quote(overview.template_version) +
                       ",\"samples\":[";
    for (std::size_t index = 0U; index < overview.samples.size(); ++index) {
        if (index != 0U) body += ',';
        const auto& sample = overview.samples[index];
        body += "{\"sampleId\":" + quote(sample.sample_id) +
                ",\"disposition\":" + quote(application::to_string(sample.disposition)) +
                ",\"state\":" + quote(application::to_string(sample.state)) +
                ",\"latestJobId\":" + optional_string(sample.latest_job_id) +
                ",\"latestJobStatus\":" + (sample.latest_job_status.has_value() ? quote(domain::to_string(*sample.latest_job_status)) : "null") +
                ",\"latestAttemptNumber\":" + std::to_string(sample.latest_attempt_number) +
                ",\"latestAttemptMode\":" + (sample.latest_attempt_mode.has_value() ? quote(application::to_string(*sample.latest_attempt_mode)) : "null") +
                ",\"artifacts\":[";
        for (std::size_t artifact = 0U; artifact < sample.artifacts.size(); ++artifact) {
            if (artifact != 0U) body += ',';
            body += render_artifact(sample.artifacts[artifact]);
        }
        body += "],\"qcSummaries\":[";
        for (std::size_t qc = 0U; qc < sample.qc_summaries.size(); ++qc) {
            if (qc != 0U) body += ',';
            const auto& summary = sample.qc_summaries[qc];
            body += "{\"nodeId\":" + quote(summary.node_id) +
                    ",\"moduleId\":" + quote(summary.module_id) +
                    ",\"pluginVersion\":" + quote(summary.plugin_version) +
                    ",\"metricSchemaVersion\":" + std::to_string(summary.metric_schema_version) +
                    ",\"stable\":" + bool_json(summary.stable) +
                    ",\"metrics\":[";
            for (std::size_t metric = 0U; metric < summary.metrics.size(); ++metric) {
                if (metric != 0U) body += ',';
                body += "{\"key\":" + quote(summary.metrics[metric].key) +
                        ",\"kind\":" + quote(metric_kind_name(summary.metrics[metric].kind)) +
                        ",\"value\":" + optional_string(summary.metrics[metric].value) + "}";
            }
            body += "]}";
        }
        body += "]}";
    }
    body += "],\"qcComparisons\":[";
    for (std::size_t index = 0U; index < overview.qc_comparisons.size(); ++index) {
        if (index != 0U) body += ',';
        const auto& comparison = overview.qc_comparisons[index];
        body += "{\"nodeId\":" + quote(comparison.node_id) +
                ",\"moduleId\":" + quote(comparison.module_id) +
                ",\"state\":" + quote(application::to_string(comparison.state)) +
                ",\"reason\":" + quote(comparison.reason) +
                ",\"sampleIds\":[";
        for (std::size_t sample = 0U; sample < comparison.sample_ids.size(); ++sample) {
            if (sample != 0U) body += ',';
            body += quote(comparison.sample_ids[sample]);
        }
        body += "]}";
    }
    body += "],\"issues\":[";
    for (std::size_t index = 0U; index < overview.issues.size(); ++index) {
        if (index != 0U) body += ',';
        body += "{\"sampleId\":" + quote(overview.issues[index].sample_id) +
                ",\"code\":" + quote(overview.issues[index].code) +
                ",\"message\":" + quote(overview.issues[index].message) + "}";
    }
    body += "]}";
    return body;
}

std::string render_batch_attempt(const application::BatchExecutionAttemptRecord& attempt) {
    return "{\"planId\":" + quote(attempt.plan_id) +
           ",\"sampleId\":" + quote(attempt.sample_id) +
           ",\"attemptNumber\":" + std::to_string(attempt.attempt_number) +
           ",\"jobId\":" + quote(attempt.job_id) +
           ",\"parentJobId\":" + optional_string(attempt.parent_job_id) +
           ",\"mode\":" + quote(application::to_string(attempt.mode)) +
           ",\"createdAtUtc\":" + quote(attempt.created_at_utc) + "}";
}

application::ProjectWorkspaceBindingRequest parse_workspace_binding_request(
    const std::string_view sample_id,
    const std::string_view body
) {
    const JsonValue parsed = JsonParser{body}.parse();
    const auto& root = object(parsed, "binding request");
    reject_unknown(root, {"layout", "primaryFileId", "secondaryFileId", "referenceFileId"});
    const std::string layout_text = string_value(required(root, "layout"), "layout", 64U);
    return application::ProjectWorkspaceBindingRequest{
        .sample_id = std::string{sample_id},
        .layout = domain::sample_input_layout_from_string(layout_text),
        .primary_file_id = string_value(required(root, "primaryFileId"), "primaryFileId", 128U),
        .secondary_file_id = optional_string_value(root, "secondaryFileId", 128U),
        .reference_file_id = optional_string_value(root, "referenceFileId", 128U),
    };
}

application::ProjectWorkspaceBatchPreviewRequest parse_workspace_batch_preview_request(
    const std::string_view body
) {
    const JsonValue parsed = JsonParser{body}.parse();
    const auto& root = object(parsed, "batch preview request");
    reject_unknown(root, {"planId", "templateId", "templateVersion", "sampleIds", "parameterOverrides", "inputAssignments"});
    application::ProjectWorkspaceBatchPreviewRequest request{
        .plan_id = string_value(required(root, "planId"), "planId", 128U),
        .template_id = string_value(required(root, "templateId"), "templateId", 200U),
        .template_version = string_value(required(root, "templateVersion"), "templateVersion", 200U),
        .sample_ids = string_array(required(root, "sampleIds"), "sampleIds", application::BatchPlanningService::maximum_selected_samples),
        .parameter_overrides = {},
        .input_assignments = {},
    };
    if (const auto found = root.find("parameterOverrides"); found != root.end()) {
        const auto& values = array(found->second, "parameterOverrides");
        if (values.size() > 4096U) throw std::invalid_argument{"parameterOverrides contains too many items"};
        for (const auto& value : values) {
            const auto& item = object(value, "parameter override");
            reject_unknown(item, {"nodeId", "parameterName", "value"});
            request.parameter_overrides.push_back(application::WorkflowTemplateParameterOverride{
                .node_id = domain::WorkflowNodeId{string_value(required(item, "nodeId"), "nodeId", domain::WorkflowNodeId::maximum_length)},
                .parameter_name = string_value(required(item, "parameterName"), "parameterName", 200U),
                .value = string_value(required(item, "value"), "value", 4096U),
            });
        }
    }
    if (const auto found = root.find("inputAssignments"); found != root.end()) {
        const auto& values = array(found->second, "inputAssignments");
        if (values.size() > 4096U) throw std::invalid_argument{"inputAssignments contains too many items"};
        for (const auto& value : values) {
            const auto& item = object(value, "input assignment");
            reject_unknown(item, {"nodeId", "inputPort", "role"});
            const auto role_text = string_value(required(item, "role"), "role", 32U);
            const auto role = application::batch_input_file_role_from_string(role_text);
            if (!role.has_value()) throw std::invalid_argument{"Unsupported batch input role"};
            request.input_assignments.push_back(application::BatchWorkflowInputAssignment{
                .node_id = domain::WorkflowNodeId{string_value(required(item, "nodeId"), "nodeId", domain::WorkflowNodeId::maximum_length)},
                .input_port = string_value(required(item, "inputPort"), "inputPort", 200U),
                .role = *role,
            });
        }
    }
    return request;
}

std::vector<std::string> parse_workspace_exclusions(const std::string_view body) {
    const JsonValue parsed = JsonParser{body}.parse();
    const auto& root = object(parsed, "batch approval request");
    reject_unknown(root, {"excludedSampleIds"});
    const auto found = root.find("excludedSampleIds");
    if (found == root.end()) return {};
    return string_array(found->second, "excludedSampleIds", application::BatchPlanningService::maximum_selected_samples);
}

application::ProjectWorkspaceBatchSubmitRequest parse_workspace_batch_submit_request(
    const std::string_view body
) {
    if (body.empty()) return {};
    const JsonValue parsed = JsonParser{body}.parse();
    const auto& root = object(parsed, "batch submit request");
    reject_unknown(root, {"maximumConcurrentJobs", "priority"});
    application::ProjectWorkspaceBatchSubmitRequest request;
    if (const auto found = root.find("maximumConcurrentJobs"); found != root.end()) {
        const auto value = integer_value(found->second, "maximumConcurrentJobs");
        if (value < 1 || value > 64) throw std::invalid_argument{"maximumConcurrentJobs must be between 1 and 64"};
        request.maximum_concurrent_jobs = static_cast<std::size_t>(value);
    }
    request.priority = priority_from_object(root);
    return request;
}

domain::JobPriority parse_workspace_recovery_priority(const std::string_view body) {
    if (body.empty()) return domain::JobPriority::normal;
    const JsonValue parsed = JsonParser{body}.parse();
    const auto& root = object(parsed, "recovery request");
    reject_unknown(root, {"priority"});
    return priority_from_object(root);
}

}  // namespace biocore::presentation
