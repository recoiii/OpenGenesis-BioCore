#include "biocore/infrastructure/json_execution_plan_store.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "biocore/pipeline_protocol/pipeline_document.hpp"
#include "biocore/pipeline_protocol/pipeline_document_codec.hpp"

namespace biocore::infrastructure {
namespace {

[[nodiscard]] bool is_safe_path_component(const std::string_view value) {
    return !value.empty() && value.size() <= 128U &&
           std::ranges::all_of(value, [](const char raw) {
               const auto character = static_cast<unsigned char>(raw);
               return std::isalnum(character) != 0 || raw == '-' || raw == '_' || raw == '.';
           }) && value != "." && value != "..";
}

[[nodiscard]] std::string path_to_utf8(const std::filesystem::path& path) {
    const std::u8string value = path.u8string();
    return std::string{reinterpret_cast<const char*>(value.data()), value.size()};
}

[[nodiscard]] pipeline_protocol::ExecutionPlanDocument to_document(
    const application::ExecutionPlan& plan
) {
    std::vector<pipeline_protocol::ExecutionPlanStepDocument> steps;
    steps.reserve(plan.steps().size());
    for (const application::ExecutionPlanStep& step : plan.steps()) {
        std::vector<pipeline_protocol::ExecutionParameterDocument> parameters;
        parameters.reserve(step.parameters.size());
        for (const auto& parameter : step.parameters) {
            parameters.push_back({parameter.name, std::string{domain::to_string(parameter.type)}, parameter.value});
        }
        std::vector<pipeline_protocol::ExecutionInputBindingDocument> inputs;
        inputs.reserve(step.inputs.size());
        for (const auto& input : step.inputs) {
            inputs.push_back({input.port_name, std::string{application::to_string(input.source_kind)}, input.source_id, input.file_type, input.relative_project_path});
        }
        std::vector<pipeline_protocol::ExecutionOutputBindingDocument> outputs;
        outputs.reserve(step.outputs.size());
        for (const auto& output : step.outputs) {
            outputs.push_back({output.port_name, output.file_type, output.relative_project_path});
        }
        steps.push_back(pipeline_protocol::ExecutionPlanStepDocument{
            .id = step.id,
            .module_id = step.module_id,
            .plugin_id = step.plugin_id,
            .plugin_version = step.plugin_version,
            .plugin_manifest_version = step.plugin_manifest_version,
            .plugin_api_version = step.plugin_api_version,
            .module_type = std::string{domain::to_string(step.module_type)},
            .plugin_root_path = step.plugin_root_path,
            .executable_path = step.executable_path,
            .depends_on = step.depends_on,
            .weight = step.normalized_weight,
            .parameters = std::move(parameters),
            .inputs = std::move(inputs),
            .outputs = std::move(outputs),
        });
    }
    return pipeline_protocol::ExecutionPlanDocument{
        .schema_version = plan.schema_version(),
        .job_id = std::string{plan.job_id()},
        .job_revision = plan.job_revision(),
        .pipeline_id = std::string{plan.pipeline_id()},
        .pipeline_version = std::string{plan.pipeline_version()},
        .steps = std::move(steps),
    };
}

void write_all(std::ofstream& output, const std::string_view content) {
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    output.flush();
    if (!output) {
        throw std::runtime_error("Unable to write execution-plan snapshot");
    }
}

[[nodiscard]] std::filesystem::path require_real_directory(
    const std::filesystem::path& path,
    const std::string_view description
) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error || std::filesystem::is_symlink(status)) {
        throw std::invalid_argument(std::string{description} + " must not be a symbolic link");
    }
    const std::filesystem::path canonical = std::filesystem::canonical(path, error);
    if (error || path.lexically_normal() != canonical ||
        !std::filesystem::is_directory(canonical, error) || error) {
        throw std::invalid_argument(std::string{description} + " must be a canonical directory");
    }
    return canonical;
}

[[nodiscard]] bool create_real_directory(const std::filesystem::path& path) {
    std::error_code error;
    const bool created = std::filesystem::create_directory(path, error);
    if (error) {
        throw std::runtime_error("Unable to create execution-plan snapshot directory");
    }
    if (!created) {
        static_cast<void>(require_real_directory(path, "Execution-plan snapshot directory"));
    }
    return created;
}

void remove_if_created(const std::filesystem::path& path, const bool created) noexcept {
    if (!created) return;
    std::error_code error;
    static_cast<void>(std::filesystem::remove(path, error));
}

}  // namespace

JsonExecutionPlanStore::JsonExecutionPlanStore(std::filesystem::path project_root)
    : project_root_{require_real_directory(project_root, "Execution-plan project root")} {
    static_cast<void>(require_real_directory(
        project_root_ / ".biocore", "Project metadata directory"
    ));
    static_cast<void>(require_real_directory(
        project_root_ / ".biocore" / "runtime", "Project runtime directory"
    ));
}

std::string JsonExecutionPlanStore::store(const application::ExecutionPlan& plan) {
    if (!is_safe_path_component(plan.job_id())) {
        throw std::invalid_argument("Execution-plan job id is not safe for snapshot storage");
    }

    const std::filesystem::path runtime = project_root_ / ".biocore" / "runtime";
    const std::filesystem::path jobs_directory = runtime / "jobs";
    bool created_jobs_directory = false;
    bool created_job_directory = false;
    bool published = false;
    const std::filesystem::path job_directory = jobs_directory / std::string{plan.job_id()};
    const std::filesystem::path final_path = job_directory /
        ("execution-plan-r" + std::to_string(plan.job_revision()) + ".json");
    const std::filesystem::path temporary_path = final_path.string() + ".tmp";

    try {
        created_jobs_directory = create_real_directory(jobs_directory);
        created_job_directory = create_real_directory(job_directory);

        std::error_code error;
        if (std::filesystem::exists(final_path, error) || error) {
            throw std::runtime_error("Execution-plan snapshot path already exists");
        }
        error.clear();
        if (std::filesystem::exists(temporary_path, error) || error) {
            throw std::runtime_error("Execution-plan temporary path already exists");
        }

        const std::string content = pipeline_protocol::serialize_execution_plan_document(
            to_document(plan)
        );
        std::ofstream output{temporary_path, std::ios::binary | std::ios::trunc};
        if (!output) {
            throw std::runtime_error("Unable to create execution-plan temporary file");
        }
        write_all(output, content);
        output.close();
        if (!output) {
            throw std::runtime_error("Unable to close execution-plan temporary file");
        }
        std::filesystem::rename(temporary_path, final_path, error);
        if (error) {
            throw std::runtime_error("Unable to publish execution-plan snapshot");
        }
        published = true;

        const auto status = std::filesystem::symlink_status(final_path, error);
        const std::filesystem::path canonical = std::filesystem::canonical(final_path, error);
        if (error || std::filesystem::is_symlink(status) ||
            !std::filesystem::is_regular_file(canonical, error) || error ||
            canonical.parent_path() != std::filesystem::canonical(job_directory, error) || error) {
            throw std::runtime_error("Unable to validate execution-plan snapshot");
        }
        return path_to_utf8(canonical);
    } catch (...) {
        std::error_code error;
        static_cast<void>(std::filesystem::remove(temporary_path, error));
        if (published) {
            error.clear();
            static_cast<void>(std::filesystem::remove(final_path, error));
        }
        remove_if_created(job_directory, created_job_directory);
        remove_if_created(jobs_directory, created_jobs_directory);
        throw;
    }
}

void JsonExecutionPlanStore::discard(const std::string_view snapshot_path) {
    if (snapshot_path.empty() || snapshot_path.find('\0') != std::string_view::npos ||
        snapshot_path.size() > 32U * 1024U) {
        throw std::invalid_argument("Execution-plan snapshot path is invalid");
    }
    const std::filesystem::path input{std::string{snapshot_path}};
    if (!input.is_absolute()) {
        throw std::invalid_argument("Execution-plan snapshot path must be absolute");
    }
    std::error_code error;
    const auto status = std::filesystem::symlink_status(input, error);
    if (error) {
        if (error == std::errc::no_such_file_or_directory) return;
        throw std::runtime_error("Unable to inspect execution-plan snapshot for discard");
    }
    if (std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status)) {
        throw std::invalid_argument("Execution-plan discard target must be a regular non-symlink file");
    }
    const auto canonical = std::filesystem::canonical(input, error);
    if (error) throw std::runtime_error("Unable to canonicalize execution-plan discard target");
    const auto jobs_root = std::filesystem::canonical(
        project_root_ / ".biocore" / "runtime" / "jobs", error
    );
    if (error) throw std::runtime_error("Execution-plan jobs directory is unavailable");
    const auto relative = canonical.lexically_relative(jobs_root);
    if (relative.empty() || relative.is_absolute() || relative.begin() == relative.end() ||
        *relative.begin() == ".." || canonical.parent_path().parent_path() != jobs_root ||
        !canonical.filename().string().starts_with("execution-plan-r") ||
        canonical.extension() != ".json") {
        throw std::invalid_argument("Execution-plan discard target is outside the reserved snapshot namespace");
    }
    if (!std::filesystem::remove(canonical, error) || error) {
        throw std::runtime_error("Unable to discard execution-plan snapshot");
    }
    const auto job_directory = canonical.parent_path();
    error.clear();
    static_cast<void>(std::filesystem::remove(job_directory, error));
}

}  // namespace biocore::infrastructure
