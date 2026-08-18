#include "biocore/presentation/local_api.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "biocore/application/artifact_presentation_service.hpp"
#include "biocore/application/artifact_presentation_service_error.hpp"
#include "biocore/application/build_info.hpp"
#include "biocore/application/health_snapshot.hpp"
#include "biocore/application/i_utc_clock.hpp"
#include "biocore/application/job_service.hpp"
#include "biocore/application/job_service_error.hpp"
#include "biocore/application/i_job_submitter.hpp"
#include "biocore/application/job_submission_service_error.hpp"
#include "biocore/application/managed_file_service.hpp"
#include "biocore/application/managed_file_service_error.hpp"
#include "biocore/application/pipeline_bindings.hpp"
#include "biocore/domain/job.hpp"
#include "biocore/domain/managed_file.hpp"
#include "biocore/domain/pipeline_step.hpp"
#include "biocore/domain/plugin_io_contract.hpp"
#include "biocore/domain/job_priority.hpp"
#include "biocore/domain/job_status.hpp"
#include "biocore/domain/storage_mode.hpp"
#include "biocore/presentation/artifact_report.hpp"
#include "biocore/presentation/health_json.hpp"
#include "biocore/presentation/local_browser_session.hpp"

namespace biocore::presentation {
namespace {

[[nodiscard]] bool constant_time_equal(const std::string_view left, const std::string_view right) noexcept {
    const std::size_t maximum = std::max(left.size(), right.size());
    std::size_t difference = left.size() ^ right.size();
    for (std::size_t index = 0U; index < maximum; ++index) {
        const auto left_value = index < left.size() ? static_cast<unsigned char>(left[index]) : 0U;
        const auto right_value = index < right.size() ? static_cast<unsigned char>(right[index]) : 0U;
        difference |= static_cast<std::size_t>(left_value ^ right_value);
    }
    return difference == 0U;
}

[[nodiscard]] bool authorized(const std::string_view header, const std::string_view token) noexcept {
    constexpr std::string_view prefix{"Bearer "};
    return header.starts_with(prefix) && constant_time_equal(header.substr(prefix.size()), token);
}

[[nodiscard]] std::string escape_json(const std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size());
    constexpr std::array<char, 16> hexadecimal{
        '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
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

[[nodiscard]] std::string optional_json(const std::optional<std::string>& value) {
    return value.has_value() ? quote(*value) : "null";
}

[[nodiscard]] std::string render_job(const domain::Job& job) {
    return "{" + std::string{"\"id\":"} + quote(job.id()) +
           ",\"analysisId\":" + optional_json(job.analysis_id()) +
           ",\"pipelineId\":" + optional_json(job.pipeline_id()) +
           ",\"pipelineVersion\":" + optional_json(job.pipeline_version()) +
           ",\"status\":" + quote(domain::to_string(job.status())) +
           ",\"priority\":" + quote(domain::to_string(job.priority())) +
           ",\"progress\":" + std::to_string(job.progress()) +
           ",\"activeStepId\":" + optional_json(job.active_step_id()) +
           ",\"createdAtUtc\":" + quote(job.created_at_utc()) +
           ",\"updatedAtUtc\":" + quote(job.updated_at_utc()) +
           ",\"startedAtUtc\":" + optional_json(job.started_at_utc()) +
           ",\"finishedAtUtc\":" + optional_json(job.finished_at_utc()) +
           ",\"revision\":" + std::to_string(job.revision()) + "}";
}

[[nodiscard]] std::string render_jobs(std::vector<domain::Job> jobs) {
    std::ranges::sort(jobs, [](const domain::Job& left, const domain::Job& right) {
        if (left.created_at_utc() != right.created_at_utc()) {
            return left.created_at_utc() < right.created_at_utc();
        }
        return left.id() < right.id();
    });
    std::string output{"["};
    for (std::size_t index = 0U; index < jobs.size(); ++index) {
        if (index != 0U) output += ',';
        output += render_job(jobs[index]);
    }
    output += ']';
    return output;
}


[[nodiscard]] std::string render_managed_file(const domain::ManagedFile& file) {
    return "{" + std::string{"\"id\":"} + quote(file.id()) +
           ",\"displayName\":" + quote(file.display_name()) +
           ",\"storageMode\":" + quote(domain::to_string(file.storage_mode())) +
           ",\"fileType\":" + quote(file.file_type()) +
           ",\"sizeBytes\":" + std::to_string(file.size_bytes()) +
           ",\"createdAtUtc\":" + quote(file.created_at_utc()) + "}";
}

[[nodiscard]] std::string render_managed_inputs(
    std::vector<domain::ManagedFile> files
) {
    std::ranges::sort(files, [](const domain::ManagedFile& left, const domain::ManagedFile& right) {
        if (left.created_at_utc() != right.created_at_utc()) {
            return left.created_at_utc() < right.created_at_utc();
        }
        return left.id() < right.id();
    });
    std::string body{"["};
    bool first = true;
    for (const auto& file : files) {
        if (file.storage_mode() != domain::StorageMode::managed_copy) continue;
        if (!first) body += ',';
        first = false;
        body += render_managed_file(file);
    }
    body += ']';
    return body;
}

[[nodiscard]] std::string render_upload_session(
    const application::ManagedFileUploadSession& session
) {
    return "{" + std::string{"\"uploadId\":"} + quote(session.upload_id) +
           ",\"displayName\":" + quote(session.display_name) +
           ",\"fileType\":" + quote(session.file_type) +
           ",\"sizeBytes\":" + std::to_string(session.expected_size_bytes) +
           ",\"receivedBytes\":" + std::to_string(session.received_size_bytes) +
           ",\"maxChunkBytes\":" +
           std::to_string(application::ManagedFileService::maximum_upload_chunk_bytes) + "}";
}

void validate_utf8(const std::string_view value) {
    std::size_t index = 0U;
    while (index < value.size()) {
        const auto first = static_cast<unsigned char>(value[index]);
        if (first == 0U) throw std::invalid_argument("Request contains NUL data");
        if (first <= 0x7fU) { ++index; continue; }
        std::size_t length = 0U;
        std::uint32_t code_point = 0U;
        if (first >= 0xc2U && first <= 0xdfU) { length = 2U; code_point = first & 0x1fU; }
        else if (first >= 0xe0U && first <= 0xefU) { length = 3U; code_point = first & 0x0fU; }
        else if (first >= 0xf0U && first <= 0xf4U) { length = 4U; code_point = first & 0x07U; }
        else throw std::invalid_argument("Request is not valid UTF-8");
        if (index + length > value.size()) throw std::invalid_argument("Request ends inside a UTF-8 sequence");
        for (std::size_t offset = 1U; offset < length; ++offset) {
            const auto continuation = static_cast<unsigned char>(value[index + offset]);
            if ((continuation & 0xc0U) != 0x80U) throw std::invalid_argument("Request is not valid UTF-8");
            code_point = (code_point << 6U) | (continuation & 0x3fU);
        }
        if ((length == 3U && code_point < 0x800U) || (length == 4U && code_point < 0x10000U) ||
            code_point > 0x10ffffU || (code_point >= 0xd800U && code_point <= 0xdfffU)) {
            throw std::invalid_argument("Request contains an invalid UTF-8 code point");
        }
        index += length;
    }
}

[[nodiscard]] std::vector<std::string_view> split_path(const std::string_view target) {
    if (target.empty() || target.front() != '/' || target.find('?') != std::string_view::npos ||
        target.find('#') != std::string_view::npos || target.find('%') != std::string_view::npos ||
        target.find("//") != std::string_view::npos) {
        return {};
    }
    std::vector<std::string_view> parts;
    std::size_t position = 1U;
    while (position <= target.size()) {
        const std::size_t slash = target.find('/', position);
        const std::size_t end = slash == std::string_view::npos ? target.size() : slash;
        if (end == position) return {};
        if (end > position) parts.push_back(target.substr(position, end - position));
        if (slash == std::string_view::npos) break;
        position = slash + 1U;
    }
    return parts;
}

[[nodiscard]] bool safe_path_atom(const std::string_view value) noexcept {
    if (value.empty() || value.size() > 200U || value == "." || value == "..") return false;
    return std::ranges::all_of(value, [](const char character) {
        const auto c = static_cast<unsigned char>(character);
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
    });
}

[[nodiscard]] LocalHttpResponse json_response(const int status, std::string body) {
    LocalHttpResponse response;
    response.status = status;
    response.content_type = "application/json; charset=utf-8";
    response.body = std::move(body);
    response.headers.emplace_back("Cache-Control", "no-store");
    response.headers.emplace_back("X-Content-Type-Options", "nosniff");
    response.headers.emplace_back("Content-Security-Policy", "default-src 'none'; frame-ancestors 'none'");
    return response;
}

[[nodiscard]] LocalHttpResponse error_response(const int status, const std::string_view code, const std::string_view message) {
    return json_response(status, "{\"error\":{\"code\":" + quote(code) + ",\"message\":" + quote(message) + "}}");
}

struct CreateJobFields final {
    std::optional<std::string> analysis_id;
    std::optional<std::string> pipeline_id;
    std::optional<std::string> pipeline_version;
    domain::JobPriority priority{domain::JobPriority::normal};
    application::PipelineRunBindings bindings;
};

class CreateJobParser final {
public:
    explicit CreateJobParser(const std::string_view input) : input_{input} {}

    [[nodiscard]] CreateJobFields parse() {
        skip_space();
        require('{');
        skip_space();

        CreateJobFields fields;
        bool analysis_seen = false;
        bool pipeline_seen = false;
        bool version_seen = false;
        bool priority_seen = false;
        bool bindings_seen = false;

        if (consume('}')) {
            require_end();
            return fields;
        }

        while (true) {
            const std::string key = parse_string(64U);
            skip_space();
            require(':');
            skip_space();

            if (key == "analysisId") {
                if (analysis_seen) fail("Duplicate analysisId");
                analysis_seen = true;
                fields.analysis_id = parse_nullable_string(200U);
            } else if (key == "pipelineId") {
                if (pipeline_seen) fail("Duplicate pipelineId");
                pipeline_seen = true;
                fields.pipeline_id = parse_nullable_string(200U);
            } else if (key == "pipelineVersion") {
                if (version_seen) fail("Duplicate pipelineVersion");
                version_seen = true;
                fields.pipeline_version = parse_nullable_string(200U);
            } else if (key == "priority") {
                if (priority_seen) fail("Duplicate priority");
                priority_seen = true;
                const std::string priority = parse_string(16U);
                const auto parsed = domain::job_priority_from_string(priority);
                if (!parsed.has_value()) fail("Priority must be low, normal, or high");
                fields.priority = *parsed;
            } else if (key == "bindings") {
                if (bindings_seen) fail("Duplicate bindings");
                bindings_seen = true;
                fields.bindings = parse_bindings();
            } else {
                fail("Unknown create-job field");
            }

            skip_space();
            if (consume('}')) break;
            require(',');
            skip_space();
        }

        require_end();
        return fields;
    }

private:
    [[noreturn]] void fail(const std::string& message) const {
        throw std::invalid_argument(message);
    }

    void skip_space() {
        while (position_ < input_.size() &&
               (input_[position_] == ' ' || input_[position_] == '\n' ||
                input_[position_] == '\r' || input_[position_] == '\t')) {
            ++position_;
        }
    }

    void require(const char expected) {
        if (position_ >= input_.size() || input_[position_] != expected) {
            fail("Malformed create-job JSON");
        }
        ++position_;
    }

    [[nodiscard]] bool consume(const char expected) {
        if (position_ < input_.size() && input_[position_] == expected) {
            ++position_;
            return true;
        }
        return false;
    }

    [[nodiscard]] bool consume_literal(const std::string_view literal) {
        if (input_.substr(position_, literal.size()) != literal) return false;
        position_ += literal.size();
        return true;
    }

    void require_end() {
        skip_space();
        if (position_ != input_.size()) fail("Trailing create-job JSON data");
    }

    [[nodiscard]] std::string parse_string(const std::size_t maximum_length) {
        require('"');
        std::string value;
        while (position_ < input_.size()) {
            const char c = input_[position_++];
            if (c == '"') return value;
            if (static_cast<unsigned char>(c) < 0x20U) {
                fail("Control character in JSON string");
            }
            if (c == '\\') {
                if (position_ >= input_.size()) fail("Invalid JSON escape");
                const char escape = input_[position_++];
                switch (escape) {
                    case '"': value += '"'; break;
                    case '\\': value += '\\'; break;
                    case '/': value += '/'; break;
                    case 'b': value += '\b'; break;
                    case 'f': value += '\f'; break;
                    case 'n': value += '\n'; break;
                    case 'r': value += '\r'; break;
                    case 't': value += '\t'; break;
                    default: fail("Unsupported JSON escape in create-job body");
                }
            } else {
                value += c;
            }
            if (value.size() > maximum_length) fail("Create-job string is too long");
        }
        fail("Unterminated JSON string");
    }

    [[nodiscard]] std::optional<std::string> parse_nullable_string(
        const std::size_t maximum_length
    ) {
        if (consume_literal("null")) return std::nullopt;
        return parse_string(maximum_length);
    }

    [[nodiscard]] application::PipelineRunBindings parse_bindings() {
        require('{');
        skip_space();

        application::PipelineRunBindings bindings;
        bool steps_seen = false;
        if (consume('}')) return bindings;

        while (true) {
            const std::string key = parse_string(64U);
            skip_space();
            require(':');
            skip_space();

            if (key == "steps") {
                if (steps_seen) fail("Duplicate bindings.steps");
                steps_seen = true;
                bindings.steps = parse_steps();
            } else {
                fail("Unknown bindings field");
            }

            skip_space();
            if (consume('}')) break;
            require(',');
            skip_space();
        }
        return bindings;
    }

    [[nodiscard]] std::vector<application::PipelineStepBindings> parse_steps() {
        require('[');
        skip_space();

        std::vector<application::PipelineStepBindings> steps;
        if (consume(']')) return steps;
        while (true) {
            steps.push_back(parse_step());
            skip_space();
            if (consume(']')) break;
            require(',');
            skip_space();
        }
        return steps;
    }

    [[nodiscard]] application::PipelineStepBindings parse_step() {
        require('{');
        skip_space();

        std::optional<std::string> step_id;
        std::vector<application::PipelineParameterBinding> parameters;
        std::vector<application::PipelineInputBinding> inputs;
        bool step_seen = false;
        bool parameters_seen = false;
        bool inputs_seen = false;
        if (consume('}')) fail("Binding step requires stepId");

        while (true) {
            const std::string key = parse_string(64U);
            skip_space();
            require(':');
            skip_space();

            if (key == "stepId") {
                if (step_seen) fail("Duplicate stepId");
                step_seen = true;
                step_id = parse_string(domain::PipelineStep::maximum_id_length);
            } else if (key == "parameters") {
                if (parameters_seen) fail("Duplicate parameters");
                parameters_seen = true;
                parameters = parse_parameters();
            } else if (key == "inputs") {
                if (inputs_seen) fail("Duplicate inputs");
                inputs_seen = true;
                inputs = parse_inputs();
            } else {
                fail("Unknown binding-step field");
            }

            skip_space();
            if (consume('}')) break;
            require(',');
            skip_space();
        }

        if (!step_id.has_value()) fail("Binding step requires stepId");
        return application::PipelineStepBindings{
            .step_id = std::move(*step_id),
            .parameters = std::move(parameters),
            .inputs = std::move(inputs),
        };
    }

    [[nodiscard]] std::vector<application::PipelineParameterBinding> parse_parameters() {
        require('[');
        skip_space();

        std::vector<application::PipelineParameterBinding> parameters;
        if (consume(']')) return parameters;
        while (true) {
            parameters.push_back(parse_parameter());
            skip_space();
            if (consume(']')) break;
            require(',');
            skip_space();
        }
        return parameters;
    }

    [[nodiscard]] application::PipelineParameterBinding parse_parameter() {
        require('{');
        skip_space();

        std::optional<std::string> name;
        std::optional<domain::PluginParameterValue> value;
        bool name_seen = false;
        bool value_seen = false;
        if (consume('}')) fail("Parameter binding requires name and value");

        while (true) {
            const std::string key = parse_string(64U);
            skip_space();
            require(':');
            skip_space();

            if (key == "name") {
                if (name_seen) fail("Duplicate parameter name field");
                name_seen = true;
                name = parse_string(domain::PluginParameterDefinition::maximum_name_length);
            } else if (key == "value") {
                if (value_seen) fail("Duplicate parameter value field");
                value_seen = true;
                value = parse_parameter_value();
            } else {
                fail("Unknown parameter-binding field");
            }

            skip_space();
            if (consume('}')) break;
            require(',');
            skip_space();
        }

        if (!name.has_value() || !value.has_value()) {
            fail("Parameter binding requires name and value");
        }
        return application::PipelineParameterBinding{
            .name = std::move(*name),
            .value = std::move(*value),
        };
    }

    [[nodiscard]] domain::PluginParameterValue parse_parameter_value() {
        if (position_ >= input_.size()) fail("Missing parameter value");
        if (input_[position_] == '"') {
            return domain::PluginParameterValue{
                parse_string(domain::PluginParameterDefinition::maximum_string_length)
            };
        }
        if (consume_literal("true")) return domain::PluginParameterValue{true};
        if (consume_literal("false")) return domain::PluginParameterValue{false};
        if (input_[position_] == '-' ||
            (input_[position_] >= '0' && input_[position_] <= '9')) {
            return parse_number();
        }
        fail("Parameter value must be a string, integer, number, or boolean");
    }

    [[nodiscard]] domain::PluginParameterValue parse_number() {
        const std::size_t start = position_;
        if (consume('-') && position_ >= input_.size()) fail("Invalid JSON number");
        if (position_ >= input_.size()) fail("Invalid JSON number");

        if (consume('0')) {
            if (position_ < input_.size() &&
                input_[position_] >= '0' && input_[position_] <= '9') {
                fail("Leading zero in JSON number");
            }
        } else {
            if (input_[position_] < '1' || input_[position_] > '9') {
                fail("Invalid JSON number");
            }
            while (position_ < input_.size() &&
                   input_[position_] >= '0' && input_[position_] <= '9') {
                ++position_;
            }
        }

        bool floating = false;
        if (consume('.')) {
            floating = true;
            if (position_ >= input_.size() ||
                input_[position_] < '0' || input_[position_] > '9') {
                fail("Invalid JSON fraction");
            }
            while (position_ < input_.size() &&
                   input_[position_] >= '0' && input_[position_] <= '9') {
                ++position_;
            }
        }

        if (position_ < input_.size() &&
            (input_[position_] == 'e' || input_[position_] == 'E')) {
            floating = true;
            ++position_;
            if (position_ < input_.size() &&
                (input_[position_] == '+' || input_[position_] == '-')) {
                ++position_;
            }
            if (position_ >= input_.size() ||
                input_[position_] < '0' || input_[position_] > '9') {
                fail("Invalid JSON exponent");
            }
            while (position_ < input_.size() &&
                   input_[position_] >= '0' && input_[position_] <= '9') {
                ++position_;
            }
        }

        const std::string_view token = input_.substr(start, position_ - start);
        if (!floating) {
            std::int64_t parsed{};
            const auto result = std::from_chars(
                token.data(), token.data() + token.size(), parsed
            );
            if (result.ec != std::errc{} ||
                result.ptr != token.data() + token.size()) {
                fail("JSON integer is out of range");
            }
            return domain::PluginParameterValue{parsed};
        }

        double parsed{};
        const auto result = std::from_chars(
            token.data(), token.data() + token.size(), parsed, std::chars_format::general
        );
        if (result.ec != std::errc{} ||
            result.ptr != token.data() + token.size() ||
            !std::isfinite(parsed)) {
            fail("JSON number is invalid or out of range");
        }
        return domain::PluginParameterValue{parsed};
    }

    [[nodiscard]] std::vector<application::PipelineInputBinding> parse_inputs() {
        require('[');
        skip_space();

        std::vector<application::PipelineInputBinding> inputs;
        if (consume(']')) return inputs;
        while (true) {
            inputs.push_back(parse_input());
            skip_space();
            if (consume(']')) break;
            require(',');
            skip_space();
        }
        return inputs;
    }

    [[nodiscard]] application::PipelineInputBinding parse_input() {
        require('{');
        skip_space();

        std::optional<std::string> port_name;
        std::optional<std::string> file_id;
        bool port_seen = false;
        bool file_seen = false;
        if (consume('}')) fail("Input binding requires portName and managedFileId");

        while (true) {
            const std::string key = parse_string(64U);
            skip_space();
            require(':');
            skip_space();

            if (key == "portName") {
                if (port_seen) fail("Duplicate portName");
                port_seen = true;
                port_name = parse_string(domain::PluginInputPortDefinition::maximum_name_length);
            } else if (key == "managedFileId") {
                if (file_seen) fail("Duplicate managedFileId");
                file_seen = true;
                file_id = parse_string(domain::ManagedFile::maximum_id_length);
            } else {
                fail("Unknown input-binding field");
            }

            skip_space();
            if (consume('}')) break;
            require(',');
            skip_space();
        }

        if (!port_name.has_value() || !file_id.has_value()) {
            fail("Input binding requires portName and managedFileId");
        }
        return application::PipelineInputBinding{
            .port_name = std::move(*port_name),
            .source = application::ManagedFileInputSource{std::move(*file_id)},
        };
    }

    std::string_view input_;
    std::size_t position_{0U};
};


struct BeginUploadFields final {
    std::optional<std::string> display_name;
    std::optional<std::string> file_type;
    std::optional<std::uint64_t> size_bytes;
};

class BeginUploadParser final {
public:
    explicit BeginUploadParser(const std::string_view input) : input_{input} {}

    [[nodiscard]] BeginUploadFields parse() {
        skip_space(); require('{'); skip_space();
        BeginUploadFields fields;
        bool name_seen = false;
        bool type_seen = false;
        bool size_seen = false;
        if (consume('}')) fail("Upload body requires displayName, fileType, and sizeBytes");
        while (true) {
            const std::string key = parse_string(64U);
            skip_space(); require(':'); skip_space();
            if (key == "displayName") {
                if (name_seen) fail("Duplicate displayName");
                name_seen = true;
                fields.display_name = parse_string(domain::ManagedFile::maximum_display_name_length);
            } else if (key == "fileType") {
                if (type_seen) fail("Duplicate fileType");
                type_seen = true;
                fields.file_type = parse_string(domain::ManagedFile::maximum_file_type_length);
            } else if (key == "sizeBytes") {
                if (size_seen) fail("Duplicate sizeBytes");
                size_seen = true;
                fields.size_bytes = parse_uint64();
            } else {
                fail("Unknown upload-start field");
            }
            skip_space();
            if (consume('}')) break;
            require(','); skip_space();
        }
        skip_space();
        if (position_ != input_.size()) fail("Trailing upload-start JSON data");
        return fields;
    }

private:
    [[noreturn]] void fail(const std::string& message) const {
        throw std::invalid_argument(message);
    }
    void skip_space() {
        while (position_ < input_.size() &&
               (input_[position_] == ' ' || input_[position_] == '\n' ||
                input_[position_] == '\r' || input_[position_] == '\t')) ++position_;
    }
    void require(const char expected) {
        if (position_ >= input_.size() || input_[position_] != expected) {
            fail("Malformed upload-start JSON");
        }
        ++position_;
    }
    [[nodiscard]] bool consume(const char expected) {
        if (position_ < input_.size() && input_[position_] == expected) {
            ++position_;
            return true;
        }
        return false;
    }
    [[nodiscard]] std::string parse_string(const std::size_t maximum_length) {
        require('"');
        std::string value;
        while (position_ < input_.size()) {
            const char character = input_[position_++];
            if (character == '"') return value;
            if (static_cast<unsigned char>(character) < 0x20U) {
                fail("Control character in upload JSON string");
            }
            if (character == '\\') {
                if (position_ >= input_.size()) fail("Invalid upload JSON escape");
                const char escape = input_[position_++];
                switch (escape) {
                    case '"': value += '"'; break;
                    case '\\': value += '\\'; break;
                    case '/': value += '/'; break;
                    case 'b': value += '\b'; break;
                    case 'f': value += '\f'; break;
                    case 'n': value += '\n'; break;
                    case 'r': value += '\r'; break;
                    case 't': value += '\t'; break;
                    default: fail("Unsupported JSON escape in upload body");
                }
            } else {
                value += character;
            }
            if (value.size() > maximum_length) fail("Upload string is too long");
        }
        fail("Unterminated upload JSON string");
    }
    [[nodiscard]] std::uint64_t parse_uint64() {
        const std::size_t start = position_;
        if (position_ >= input_.size() || input_[position_] < '0' || input_[position_] > '9') {
            fail("sizeBytes must be a non-negative integer");
        }
        if (input_[position_] == '0') {
            ++position_;
            if (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') {
                fail("sizeBytes must not contain a leading zero");
            }
        } else {
            while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') {
                ++position_;
            }
        }
        const std::string_view token = input_.substr(start, position_ - start);
        std::uint64_t value{};
        const auto parsed = std::from_chars(token.data(), token.data() + token.size(), value);
        if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size()) {
            fail("sizeBytes is out of range");
        }
        return value;
    }
    std::string_view input_;
    std::size_t position_{0U};
};

[[nodiscard]] std::uint64_t parse_upload_offset(const std::string_view value) {
    if (value.empty()) throw std::invalid_argument("X-BioCore-Upload-Offset is required");
    std::uint64_t offset{};
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), offset);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
        throw std::invalid_argument("X-BioCore-Upload-Offset must be a non-negative integer");
    }
    return offset;
}

class BrowserSessionBootstrapParser final {
public:
    explicit BrowserSessionBootstrapParser(const std::string_view input) : input_{input} {}

    [[nodiscard]] std::string parse() {
        skip_space();
        require('{');
        skip_space();
        const std::string key = parse_string(64U);
        if (key != "bootstrapToken") fail("Unknown browser-session field");
        skip_space();
        require(':');
        skip_space();
        std::string token = parse_string(2048U);
        skip_space();
        require('}');
        skip_space();
        if (position_ != input_.size()) fail("Trailing browser-session JSON data");
        return token;
    }

private:
    [[noreturn]] void fail(const std::string& message) const {
        throw std::invalid_argument(message);
    }
    void skip_space() {
        while (position_ < input_.size() &&
               (input_[position_] == ' ' || input_[position_] == '\n' ||
                input_[position_] == '\r' || input_[position_] == '\t')) {
            ++position_;
        }
    }
    void require(const char expected) {
        if (position_ >= input_.size() || input_[position_] != expected) {
            fail("Malformed browser-session JSON");
        }
        ++position_;
    }
    [[nodiscard]] std::string parse_string(const std::size_t maximum_length) {
        require('"');
        std::string value;
        while (position_ < input_.size()) {
            const char character = input_[position_++];
            if (character == '"') return value;
            if (static_cast<unsigned char>(character) < 0x20U || character == '\\') {
                fail("Browser-session token must be an unescaped JSON string");
            }
            value.push_back(character);
            if (value.size() > maximum_length) fail("Browser-session field is too long");
        }
        fail("Unterminated browser-session JSON string");
    }
    std::string_view input_;
    std::size_t position_{0U};
};

[[nodiscard]] std::string sanitize_download_name(std::string value) {
    for (char& character : value) {
        const auto c = static_cast<unsigned char>(character);
        if (c < 0x20U || character == '"' || character == '\\' || character == '/' || character == ';') {
            character = '_';
        }
    }
    if (value.empty()) return "artifact.bin";
    if (value.size() > 180U) value.resize(180U);
    return value;
}

}  // namespace

LocalApiController::LocalApiController(
    application::JobService& jobs,
    application::IJobSubmitter& submissions,
    application::ManagedFileService& managed_files,
    application::ArtifactPresentationService& artifacts,
    application::IUtcClock& clock,
    std::string bootstrap_token,
    LocalBrowserSession& browser_session
)
    : jobs_{jobs}, submissions_{submissions}, managed_files_{managed_files}, artifacts_{artifacts}, clock_{clock},
      bootstrap_token_{std::move(bootstrap_token)}, browser_session_{browser_session} {
    if (bootstrap_token_.size() < 32U || bootstrap_token_.size() > 2048U) {
        throw std::invalid_argument("Bootstrap token length is invalid");
    }
}

LocalHttpResponse LocalApiController::handle(const LocalHttpRequest& request) {
    if (request.target.size() > 2048U || request.authorization.size() > 4096U ||
        request.browser_session.size() > 4096U || request.origin.size() > 512U ||
        request.content_type.size() > 256U || request.upload_offset.size() > 64U) {
        return error_response(413, "request_too_large", "Request metadata is too large");
    }
    try {
        validate_utf8(request.target);
        validate_utf8(request.authorization);
        validate_utf8(request.browser_session);
        validate_utf8(request.origin);
        validate_utf8(request.content_type);
        validate_utf8(request.upload_offset);
    } catch (const std::invalid_argument& error) {
        return error_response(400, "invalid_request_encoding", error.what());
    }
    const auto path = split_path(request.target);
    const bool upload_chunk_route =
        path.size() == 6U && path[0] == "api" && path[1] == "v1" &&
        path[2] == "files" && path[3] == "uploads" &&
        safe_path_atom(path[4]) && path[5] == "chunks" &&
        request.method == HttpMethod::post;
    if (upload_chunk_route) {
        if (request.body.empty() ||
            request.body.size() > application::ManagedFileService::maximum_upload_chunk_bytes) {
            return error_response(413, "upload_chunk_size", "Upload chunk must contain 1 to 1048576 bytes");
        }
    } else {
        if (request.body.size() > maximum_request_body_bytes) {
            return error_response(413, "request_too_large", "Request body is too large");
        }
        try {
            validate_utf8(request.body);
        } catch (const std::invalid_argument& error) {
            return error_response(400, "invalid_request_encoding", error.what());
        }
    }
    if (path.size() == 3U && path[0] == "api" && path[1] == "v1" && path[2] == "health" && request.method == HttpMethod::get) {
        const application::HealthSnapshot snapshot{
            .status = "healthy",
            .component = "biocore-local-api",
            .version = std::string{application::BuildInfo::version()},
            .timestamp_utc = clock_.now_utc_iso8601(),
        };
        return json_response(200, render_health_json(snapshot));
    }
    if (path.size() == 3U && path[0] == "api" && path[1] == "v1" &&
        path[2] == "session" && request.method == HttpMethod::post) {
        if (!browser_session_.origin_allowed(request.origin)) {
            return error_response(
                403, "browser_origin_rejected", "Browser session establishment requires the exact local origin"
            );
        }
        const std::string presented = BrowserSessionBootstrapParser{request.body}.parse();
        if (!constant_time_equal(presented, bootstrap_token_)) {
            return error_response(401, "unauthorized", "Bootstrap token was not accepted");
        }
        auto response = json_response(200, "{\"status\":\"ready\"}");
        response.headers.emplace_back("Set-Cookie", browser_session_.set_cookie_header());
        return response;
    }

    const bool bearer_authorized = authorized(request.authorization, bootstrap_token_);
    const bool browser_authorized = browser_session_.token_matches(request.browser_session);
    if (!bearer_authorized && !browser_authorized) {
        auto response = error_response(401, "unauthorized", "A valid local bearer or browser session is required");
        response.headers.emplace_back("WWW-Authenticate", "Bearer realm=\"OpenGenesis-BioCore local\"");
        return response;
    }
    if (!bearer_authorized && browser_authorized && request.method == HttpMethod::post &&
        !browser_session_.origin_allowed(request.origin)) {
        return error_response(
            403, "browser_origin_rejected", "Cookie-authenticated writes require the exact local origin"
        );
    }
    if (path.size() < 3U || path[0] != "api" || path[1] != "v1") {
        return error_response(404, "not_found", "Route was not found");
    }

    try {

if (path.size() == 3U && path[2] == "files" && request.method == HttpMethod::get) {
    return json_response(200, render_managed_inputs(managed_files_.list()));
}
if (path.size() == 4U && path[2] == "files" && safe_path_atom(path[3]) &&
    request.method == HttpMethod::get) {
    const auto file = managed_files_.find_by_id(path[3]);
    if (!file.has_value() || file->storage_mode() != domain::StorageMode::managed_copy) {
        return error_response(404, "managed_file_not_found", "Managed input file was not found");
    }
    return json_response(200, render_managed_file(*file));
}
if (path.size() == 4U && path[2] == "files" && path[3] == "uploads" &&
    request.method == HttpMethod::post) {
    const BeginUploadFields fields = BeginUploadParser{request.body}.parse();
    if (!fields.display_name.has_value() || !fields.file_type.has_value() ||
        !fields.size_bytes.has_value()) {
        throw std::invalid_argument(
            "displayName, fileType, and sizeBytes are required"
        );
    }
    const auto session = managed_files_.begin_upload({
        .display_name = *fields.display_name,
        .file_type = *fields.file_type,
        .size_bytes = *fields.size_bytes,
    });
    auto response = json_response(201, render_upload_session(session));
    response.headers.emplace_back(
        "Location", "/api/v1/files/uploads/" + session.upload_id
    );
    return response;
}
if (path.size() == 6U && path[2] == "files" && path[3] == "uploads" &&
    safe_path_atom(path[4]) && path[5] == "chunks" &&
    request.method == HttpMethod::post) {
    if (request.content_type != "application/octet-stream") {
        return error_response(
            415, "unsupported_media_type",
            "Upload chunks require application/octet-stream"
        );
    }
    const auto session = managed_files_.append_upload(
        path[4], parse_upload_offset(request.upload_offset), request.body
    );
    return json_response(200, render_upload_session(session));
}
if (path.size() == 6U && path[2] == "files" && path[3] == "uploads" &&
    safe_path_atom(path[4]) && path[5] == "complete" &&
    request.method == HttpMethod::post) {
    if (!request.body.empty()) {
        throw std::invalid_argument("Upload completion request body must be empty");
    }
    const auto file = managed_files_.complete_upload(path[4]);
    auto response = json_response(201, render_managed_file(file));
    response.headers.emplace_back("Location", "/api/v1/files/" + std::string{file.id()});
    return response;
}
if (path.size() == 6U && path[2] == "files" && path[3] == "uploads" &&
    safe_path_atom(path[4]) && path[5] == "cancel" &&
    request.method == HttpMethod::post) {
    if (!request.body.empty()) {
        throw std::invalid_argument("Upload cancellation request body must be empty");
    }
    if (!managed_files_.cancel_upload(path[4])) {
        return error_response(404, "upload_not_found", "Browser upload session was not found");
    }
    return json_response(200, "{\"status\":\"cancelled\"}");
}

        if (path.size() == 3U && path[2] == "jobs" && request.method == HttpMethod::get) {
            return json_response(200, render_jobs(jobs_.list()));
        }
        if (path.size() == 3U && path[2] == "jobs" && request.method == HttpMethod::post) {
            CreateJobFields fields = CreateJobParser{request.body}.parse();
            if (!fields.pipeline_id.has_value() || !fields.pipeline_version.has_value()) {
                throw std::invalid_argument("pipelineId and pipelineVersion are required");
            }
            const domain::Job job = submissions_.submit(application::SubmitJobRequest{
                .analysis_id = fields.analysis_id,
                .pipeline_id = *fields.pipeline_id,
                .pipeline_version = *fields.pipeline_version,
                .priority = fields.priority,
                .bindings = std::move(fields.bindings),
            });
            auto response = json_response(201, render_job(job));
            response.headers.emplace_back("Location", "/api/v1/jobs/" + std::string{job.id()});
            return response;
        }
        if (path.size() >= 4U && path[2] == "jobs" && safe_path_atom(path[3])) {
            const std::string_view job_id = path[3];
            if (path.size() == 4U && request.method == HttpMethod::get) {
                auto job = jobs_.find_by_id(job_id);
                if (!job.has_value()) return error_response(404, "job_not_found", "Job was not found");
                return json_response(200, render_job(*job));
            }
            if (path.size() == 5U && path[4] == "cancel" &&
                request.method == HttpMethod::post) {
                if (!request.body.empty()) {
                    throw std::invalid_argument("Job cancellation request body must be empty");
                }
                auto current = jobs_.find_by_id(job_id);
                if (!current.has_value()) {
                    return error_response(404, "job_not_found", "Job was not found");
                }

                using domain::JobStatus;
                if (current->status() == JobStatus::cancelled ||
                    current->status() == JobStatus::cancelling) {
                    return json_response(200, render_job(*current));
                }
                if (current->status() == JobStatus::completed ||
                    current->status() == JobStatus::failed) {
                    return error_response(
                        409, "job_not_cancellable", "Terminal job cannot be cancelled"
                    );
                }

                const JobStatus target =
                    current->status() == JobStatus::draft ||
                            current->status() == JobStatus::queued ||
                            current->status() == JobStatus::interrupted
                        ? JobStatus::cancelled
                        : JobStatus::cancelling;
                const domain::Job updated = jobs_.transition(
                    job_id, target, current->progress(), std::nullopt
                );
                return json_response(target == JobStatus::cancelled ? 200 : 202, render_job(updated));
            }
            if (path.size() == 5U && path[4] == "artifacts" && request.method == HttpMethod::get) {
                const auto values = artifacts_.list_for_job(job_id);
                std::string body{"["};
                for (std::size_t index = 0U; index < values.size(); ++index) {
                    if (index != 0U) body += ',';
                    body += render_artifact_metadata_json(values[index]);
                }
                body += ']';
                return json_response(200, std::move(body));
            }
            if (path.size() == 5U && path[4] == "report.json" && request.method == HttpMethod::get) {
                return json_response(200, render_pipeline_execution_report_json(artifacts_.build_job_report(job_id)));
            }
            if (path.size() == 5U && path[4] == "report.html" && request.method == HttpMethod::get) {
                LocalHttpResponse response;
                response.status = 200;
                response.content_type = "text/html; charset=utf-8";
                response.body = render_pipeline_execution_report_html(artifacts_.build_job_report(job_id));
                response.headers.emplace_back("Cache-Control", "no-store");
                response.headers.emplace_back("X-Content-Type-Options", "nosniff");
                response.headers.emplace_back("Content-Security-Policy", "default-src 'none'; style-src 'unsafe-inline'; frame-ancestors 'none'");
                return response;
            }
            if (path.size() >= 7U && path[4] == "artifacts" && safe_path_atom(path[5]) && safe_path_atom(path[6]) && request.method == HttpMethod::get) {
                const std::string_view step_id = path[5];
                const std::string_view output_port = path[6];
                if (path.size() == 7U) {
                    const auto artifact = artifacts_.find_artifact(job_id, step_id, output_port);
                    if (!artifact.has_value()) return error_response(404, "artifact_not_found", "Artifact was not found");
                    return json_response(200, render_artifact_metadata_json(*artifact));
                }
                if (path.size() == 8U && path[7] == "download") {
                    const auto descriptor = artifacts_.prepare_download(job_id, step_id, output_port);
                    LocalHttpResponse response;
                    response.status = 200;
                    response.content_type = "application/octet-stream";
                    response.file = LocalFileBody{
                        .content_path = descriptor.content_path,
                        .download_name = sanitize_download_name(descriptor.metadata.display_name),
                        .verified_sha256 = descriptor.verified_sha256,
                        .size_bytes = descriptor.metadata.size_bytes,
                    };
                    response.headers.emplace_back("Cache-Control", "no-store");
                    response.headers.emplace_back("X-Content-Type-Options", "nosniff");
                    response.headers.emplace_back("X-BioCore-SHA256", descriptor.verified_sha256);
                    return response;
                }
            }
        }
        return error_response(404, "not_found", "Route was not found");
    } catch (const application::ArtifactPresentationError& error) {
        using Code = application::ArtifactPresentationErrorCode;
        const bool missing = error.code() == Code::job_not_found || error.code() == Code::artifact_not_found || error.code() == Code::content_missing;
        const bool conflict = error.code() == Code::checksum_mismatch || error.code() == Code::size_mismatch || error.code() == Code::unsafe_content || error.code() == Code::content_not_regular || error.code() == Code::checksum_unavailable;
        return error_response(missing ? 404 : (conflict ? 409 : 500), missing ? "not_found" : (conflict ? "artifact_integrity_error" : "internal_error"), error.what());

} catch (const application::ManagedFileServiceError& error) {
    using Code = application::ManagedFileServiceErrorCode;
    switch (error.code()) {
        case Code::upload_not_found:
            return error_response(404, "upload_not_found", error.what());
        case Code::upload_offset_mismatch:
            return error_response(409, "upload_offset_mismatch", error.what());
        case Code::upload_incomplete:
            return error_response(409, "upload_incomplete", error.what());
        case Code::upload_staging_mismatch:
            return error_response(409, "upload_staging_mismatch", error.what());
        case Code::upload_size_exceeded:
            return error_response(413, "upload_size_exceeded", error.what());
        case Code::upload_session_limit:
            return error_response(429, "upload_session_limit", error.what());
        case Code::identifier_generation_exhausted:
            return error_response(409, "identifier_exhausted", error.what());
    }
    return error_response(500, "managed_file_error", error.what());
    } catch (const application::JobSubmissionError& error) {
        using Code = application::JobSubmissionErrorCode;
        if (error.code() == Code::pipeline_not_found) {
            return error_response(404, "pipeline_not_found", error.what());
        }
        if (error.code() == Code::identifier_generation_exhausted) {
            return error_response(409, "identifier_exhausted", error.what());
        }
        return error_response(500, "job_submission_error", error.what());
    } catch (const application::JobServiceError& error) {
        if (error.code() == application::JobServiceErrorCode::job_not_found) {
            return error_response(404, "job_not_found", error.what());
        }
        if (error.code() == application::JobServiceErrorCode::concurrent_update) {
            return error_response(409, "concurrent_update", error.what());
        }
        return error_response(500, "job_service_error", error.what());
    } catch (const std::invalid_argument& error) {
        return error_response(400, "invalid_request", error.what());
    } catch (const std::exception&) {
        return error_response(500, "internal_error", "The local API request failed");
    }
}

bool LocalApiController::websocket_authorized(const std::string_view authorization) const {
    return authorized(authorization, bootstrap_token_);
}

bool LocalApiController::websocket_authorized(
    const std::string_view authorization,
    const std::string_view browser_session,
    const std::string_view origin
) const {
    if (websocket_authorized(authorization)) return true;
    return browser_session_.origin_allowed(origin) &&
           browser_session_.token_matches(browser_session);
}

std::optional<std::string> LocalApiController::websocket_snapshot(const std::string_view authorization) {
    if (!websocket_authorized(authorization)) return std::nullopt;
    return "{\"type\":\"jobs.snapshot\",\"jobs\":" + render_jobs(jobs_.list()) + "}";
}

std::optional<std::string> LocalApiController::websocket_snapshot(
    const std::string_view authorization,
    const std::string_view browser_session,
    const std::string_view origin
) {
    if (!websocket_authorized(authorization, browser_session, origin)) return std::nullopt;
    return "{\"type\":\"jobs.snapshot\",\"jobs\":" + render_jobs(jobs_.list()) + "}";
}

bool LocalApiController::browser_host_authorized(const std::string_view host) const noexcept {
    return browser_session_.host_allowed(host);
}

std::string_view LocalApiController::bootstrap_token() const noexcept { return bootstrap_token_; }

}  // namespace biocore::presentation
