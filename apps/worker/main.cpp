#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <exception>
#include <iterator>
#include <mutex>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include "biocore/application/build_info.hpp"
#include "biocore/application/execution_plan.hpp"
#include "biocore/pipeline_protocol/pipeline_document.hpp"
#include "biocore/pipeline_protocol/pipeline_document_codec.hpp"
#include "biocore/infrastructure/filesystem_output_artifact_inspector.hpp"
#include "biocore/infrastructure/platform_plugin_process_runner.hpp"
#include "biocore/infrastructure/json_plugin_invocation_store.hpp"
#include "biocore/worker_protocol/launch_arguments.hpp"
#include "biocore/worker_protocol/protocol.hpp"
#include "biocore/worker_protocol/worker_event.hpp"
#include "biocore/worker_protocol/worker_event_codec.hpp"

namespace {

constexpr std::string_view protocol_argument = "--protocol-version";
constexpr std::string_view self_test_argument = "--self-test";
constexpr int base_worker_argument_count = 7;
constexpr int plan_worker_argument_count = 9;
constexpr std::int64_t execution_failure_exit_code = 3;

void print_usage() {
    std::cout << "OpenGenesis-BioCore worker bootstrap executable\n"
              << "Usage:\n"
              << "  biocore-worker --protocol-version\n"
              << "  biocore-worker --self-test\n"
              << "  biocore-worker --job-id <id> --project-root <path> "
                 "--job-revision <revision> [--execution-plan <path>]\n";
}

[[nodiscard]] std::string now_utc_iso8601() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
#if defined(_WIN32)
    if (gmtime_s(&utc, &time) != 0) {
        throw std::runtime_error("Unable to convert worker timestamp to UTC");
    }
#else
    if (gmtime_r(&time, &utc) == nullptr) {
        throw std::runtime_error("Unable to convert worker timestamp to UTC");
    }
#endif
    std::ostringstream output;
    output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return output.str();
}

void emit_event(const biocore::worker_protocol::WorkerEvent& event) {
    std::cout << biocore::worker_protocol::serialize_worker_event(event) << '\n';
    std::cout.flush();
    if (!std::cout) {
        throw std::runtime_error("Unable to write worker lifecycle event");
    }
}

[[nodiscard]] biocore::worker_protocol::WorkerEvent make_base_event(
    const biocore::worker_protocol::WorkerLaunchArguments& arguments,
    const biocore::worker_protocol::MessageType type,
    const std::uint64_t sequence
) {
    return biocore::worker_protocol::WorkerEvent{
        .protocol_version = biocore::worker_protocol::current_protocol_version,
        .type = type,
        .job_id = arguments.job_id,
        .job_revision = arguments.job_revision,
        .sequence = sequence,
        .timestamp_utc = now_utc_iso8601(),
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
}

class LifecycleEmitter final {
public:
    explicit LifecycleEmitter(
        const biocore::worker_protocol::WorkerLaunchArguments& arguments
    ) noexcept
        : arguments_{arguments} {}

    void emit(const biocore::worker_protocol::MessageType type) {
        emit(type, [](auto&) {});
    }

    template <typename Decorator>
    void emit(
        const biocore::worker_protocol::MessageType type,
        Decorator&& decorator
    ) {
        std::scoped_lock lock{mutex_};
        auto event = make_base_event(arguments_, type, next_sequence_);
        decorator(event);
        emit_event(event);
        ++next_sequence_;
    }

private:
    const biocore::worker_protocol::WorkerLaunchArguments& arguments_;
    std::mutex mutex_;
    std::uint64_t next_sequence_{1U};
};

class PluginHeartbeatGuard final {
public:
    explicit PluginHeartbeatGuard(LifecycleEmitter& emitter)
        : emitter_{emitter}, thread_{[this] { run(); }} {}

    ~PluginHeartbeatGuard() { stop(); }

    PluginHeartbeatGuard(const PluginHeartbeatGuard&) = delete;
    PluginHeartbeatGuard& operator=(const PluginHeartbeatGuard&) = delete;

    void stop() noexcept {
        {
            std::scoped_lock lock{wait_mutex_};
            stopping_ = true;
        }
        wait_condition_.notify_all();
        if (thread_.joinable()) thread_.join();
    }

    void rethrow_if_failed() const {
        std::scoped_lock lock{failure_mutex_};
        if (failure_ != nullptr) std::rethrow_exception(failure_);
    }

private:
    static constexpr auto interval = std::chrono::seconds{1};

    void run() noexcept {
        std::unique_lock wait_lock{wait_mutex_};
        for (;;) {
            if (wait_condition_.wait_for(wait_lock, interval, [this] { return stopping_; })) {
                return;
            }
            wait_lock.unlock();
            try {
                emitter_.emit(biocore::worker_protocol::MessageType::heartbeat);
            } catch (...) {
                {
                    std::scoped_lock failure_lock{failure_mutex_};
                    failure_ = std::current_exception();
                }
                {
                    std::scoped_lock stop_lock{wait_mutex_};
                    stopping_ = true;
                }
                wait_condition_.notify_all();
                return;
            }
            wait_lock.lock();
        }
    }

    LifecycleEmitter& emitter_;
    mutable std::mutex failure_mutex_;
    std::exception_ptr failure_;
    std::mutex wait_mutex_;
    std::condition_variable wait_condition_;
    bool stopping_{false};
    std::thread thread_;
};

void emit_failed(
    const biocore::worker_protocol::WorkerLaunchArguments& arguments,
    const std::uint64_t sequence,
    const std::string_view message
) {
    auto failed = make_base_event(
        arguments, biocore::worker_protocol::MessageType::failed, sequence
    );
    failed.exit_code = execution_failure_exit_code;
    constexpr std::size_t maximum_failure_message = 8U * 1024U;
    failed.message = message.empty()
                         ? std::string{"Worker execution failed"}
                         : std::string{message.substr(0U, maximum_failure_message)};
    emit_event(failed);
}

[[nodiscard]] std::filesystem::path path_from_utf8(const std::string_view value) {
    std::u8string utf8;
    utf8.reserve(value.size());
    for (const char character : value) {
        utf8.push_back(static_cast<char8_t>(static_cast<unsigned char>(character)));
    }
    return std::filesystem::path{utf8};
}

[[nodiscard]] bool is_within(
    const std::filesystem::path& parent,
    const std::filesystem::path& child
) {
    const std::filesystem::path relative = child.lexically_relative(parent);
    if (relative.empty() || relative.is_absolute()) return false;
    const auto first = relative.begin();
    return first != relative.end() && *first != "..";
}

[[nodiscard]] std::string read_execution_plan(
    const biocore::worker_protocol::WorkerLaunchArguments& arguments
) {
    if (!arguments.execution_plan_path.has_value()) {
        throw std::invalid_argument("Worker execution plan path is missing");
    }
    std::error_code error;
    const std::filesystem::path project_input = path_from_utf8(arguments.project_root);
    const std::filesystem::path project_root = std::filesystem::canonical(project_input, error);
    if (error || project_input.lexically_normal() != project_root ||
        !std::filesystem::is_directory(project_root, error) || error) {
        throw std::invalid_argument("Worker project root is invalid");
    }
    const std::filesystem::path input = path_from_utf8(*arguments.execution_plan_path);
    const auto status = std::filesystem::symlink_status(input, error);
    if (error || std::filesystem::is_symlink(status)) {
        throw std::invalid_argument("Worker execution plan must not be a symbolic link");
    }
    const std::filesystem::path plan_path = std::filesystem::canonical(input, error);
    if (error || input.lexically_normal() != plan_path ||
        !std::filesystem::is_regular_file(plan_path, error) || error ||
        !is_within(project_root, plan_path)) {
        throw std::invalid_argument("Worker execution plan must be a canonical project-local file");
    }
    const auto size = std::filesystem::file_size(plan_path, error);
    if (error || size == 0U ||
        size > biocore::pipeline_protocol::maximum_pipeline_document_bytes) {
        throw std::invalid_argument("Worker execution plan file size is invalid");
    }
    std::ifstream input_stream{plan_path, std::ios::binary};
    if (!input_stream) throw std::runtime_error("Unable to open worker execution plan");
    std::string content{
        std::istreambuf_iterator<char>{input_stream}, std::istreambuf_iterator<char>{}
    };
    if (input_stream.bad()) throw std::runtime_error("Unable to read worker execution plan");
    return content;
}

[[nodiscard]] biocore::application::ExecutionPlan to_execution_plan(
    const biocore::pipeline_protocol::ExecutionPlanDocument& document
) {
    std::vector<biocore::application::ExecutionPlanStep> steps;
    steps.reserve(document.steps.size());
    for (const auto& step : document.steps) {
        const auto module_type = biocore::domain::plugin_module_type_from_string(
            step.module_type
        );
        if (!module_type.has_value()) {
            throw std::invalid_argument("Execution plan module type is unsupported");
        }
        std::vector<biocore::application::ExecutionParameterBinding> parameters;
        parameters.reserve(step.parameters.size());
        for (const auto& parameter : step.parameters) {
            const auto type = biocore::domain::plugin_parameter_type_from_string(parameter.type);
            if (!type.has_value()) throw std::invalid_argument("Execution plan parameter type is unsupported");
            parameters.push_back({parameter.name, *type, parameter.value});
        }
        std::vector<biocore::application::ExecutionInputBinding> inputs;
        inputs.reserve(step.inputs.size());
        for (const auto& input : step.inputs) {
            biocore::application::ExecutionInputSourceKind kind;
            if (input.source_kind == "managedFile") kind = biocore::application::ExecutionInputSourceKind::managed_file;
            else if (input.source_kind == "stepOutput") kind = biocore::application::ExecutionInputSourceKind::step_output;
            else throw std::invalid_argument("Execution plan input source kind is unsupported");
            inputs.push_back({input.port, kind, input.source_id, input.file_type, input.relative_project_path});
        }
        std::vector<biocore::application::ExecutionOutputBinding> outputs;
        outputs.reserve(step.outputs.size());
        for (const auto& output : step.outputs) {
            outputs.push_back({output.port, output.file_type, output.relative_project_path});
        }
        steps.push_back(biocore::application::ExecutionPlanStep{
            .id = step.id,
            .module_id = step.module_id,
            .plugin_id = step.plugin_id,
            .plugin_version = step.plugin_version,
            .module_type = *module_type,
            .plugin_root_path = step.plugin_root_path,
            .executable_path = step.executable_path,
            .depends_on = step.depends_on,
            .normalized_weight = step.weight,
            .parameter_definitions = {}, .input_definitions = {}, .output_definitions = {},
            .parameters = std::move(parameters), .inputs = std::move(inputs), .outputs = std::move(outputs),
        });
    }
    return biocore::application::ExecutionPlan{
        document.schema_version,
        document.job_id,
        document.job_revision,
        document.pipeline_id,
        document.pipeline_version,
        std::move(steps),
    };
}

int run_execution_plan(
    const biocore::worker_protocol::WorkerLaunchArguments& arguments
) {
    using biocore::worker_protocol::MessageType;
    using biocore::worker_protocol::WorkerLogLevel;

    const auto document = biocore::pipeline_protocol::parse_execution_plan_document(
        read_execution_plan(arguments)
    );
    const auto plan = to_execution_plan(document);
    if (plan.job_id() != arguments.job_id || plan.job_revision() != arguments.job_revision) {
        throw std::invalid_argument("Execution plan identity does not match worker launch");
    }
    biocore::infrastructure::PlatformPluginProcessRunner plugin_runner;
    const std::filesystem::path project_root = path_from_utf8(arguments.project_root);
    biocore::infrastructure::JsonPluginInvocationStore invocation_store{project_root};
    biocore::infrastructure::FilesystemOutputArtifactInspector artifact_inspector{project_root};

    LifecycleEmitter lifecycle{arguments};
    lifecycle.emit(MessageType::ready);
    lifecycle.emit(MessageType::heartbeat);

    std::unordered_map<std::string, double> progress_by_step;
    progress_by_step.reserve(plan.steps().size());
    try {
        for (const auto& step : plan.steps()) {
            lifecycle.emit(MessageType::log, [&](auto& log) {
                log.log_level = WorkerLogLevel::info;
                log.component = "pipeline-engine";
                log.message = "Executing plugin module " + step.module_id +
                              " from " + step.plugin_id + "@" + step.plugin_version;
            });

            const std::string invocation_snapshot = invocation_store.store(
                plan.job_id(), plan.job_revision(), step
            );
            int plugin_exit_code = 0;
            {
                PluginHeartbeatGuard heartbeat{lifecycle};
                plugin_exit_code = plugin_runner.run(
                    step.executable_path,
                    step.plugin_root_path,
                    step.plugin_id,
                    step.plugin_version,
                    step.module_id,
                    step.id,
                    invocation_snapshot
                );
                heartbeat.stop();
                heartbeat.rethrow_if_failed();
            }
            if (plugin_exit_code != 0) {
                throw std::runtime_error(
                    "Plugin module exited with code " + std::to_string(plugin_exit_code)
                );
            }

            for (const auto& output : step.outputs) {
                static_cast<void>(
                    artifact_inspector.inspect_existing_output(output.relative_project_path)
                );
            }
            for (const auto& output : step.outputs) {
                lifecycle.emit(MessageType::artifact, [&](auto& artifact) {
                    artifact.artifact_step_id = step.id;
                    artifact.artifact_output_port = output.port_name;
                    artifact.artifact_plugin_id = step.plugin_id;
                    artifact.artifact_plugin_version = step.plugin_version;
                    artifact.artifact_module_id = step.module_id;
                    artifact.artifact_file_type = output.file_type;
                    artifact.artifact_relative_project_path = output.relative_project_path;
                });
            }

            progress_by_step[step.id] = 1.0;
            lifecycle.emit(MessageType::progress, [&](auto& progress) {
                progress.progress = plan.calculate_overall_progress(progress_by_step);
                progress.active_step_id = step.id;
            });
        }

        lifecycle.emit(MessageType::completed, [](auto& completed) {
            completed.exit_code = 0;
        });
        return 0;
    } catch (const std::exception& error) {
        lifecycle.emit(MessageType::failed, [&](auto& failed) {
            failed.exit_code = execution_failure_exit_code;
            constexpr std::size_t maximum_failure_message = 8U * 1024U;
            const std::string_view message = error.what();
            failed.message = message.empty()
                                 ? std::string{"Worker execution failed"}
                                 : std::string{message.substr(0U, maximum_failure_message)};
        });
        return static_cast<int>(execution_failure_exit_code);
    }
}

void run_bootstrap_lifecycle(
    const biocore::worker_protocol::WorkerLaunchArguments& arguments
) {
    using biocore::worker_protocol::MessageType;
    using biocore::worker_protocol::WorkerLogLevel;

    emit_event(make_base_event(arguments, MessageType::ready, 1U));
    emit_event(make_base_event(arguments, MessageType::heartbeat, 2U));

    auto progress = make_base_event(arguments, MessageType::progress, 3U);
    progress.progress = 0.5;
    progress.active_step_id = "bootstrap";
    emit_event(progress);

    auto log = make_base_event(arguments, MessageType::log, 4U);
    log.log_level = WorkerLogLevel::info;
    log.component = "biocore-worker";
    log.message = "Bootstrap lifecycle completed";
    emit_event(log);

    auto completed = make_base_event(arguments, MessageType::completed, 5U);
    completed.exit_code = 0;
    emit_event(completed);
}

}  // namespace

int main(const int argc, const char* const argv[]) {
    if (argc == 2) {
        const std::string_view argument{argv[1]};

        if (argument == protocol_argument) {
            std::cout << biocore::worker_protocol::current_protocol_version << '\n';
            return 0;
        }

        if (argument == self_test_argument) {
            std::cout << "{\"status\":\"healthy\",\"component\":\"biocore-worker\",\"version\":\""
                      << biocore::application::BuildInfo::version() << "\",\"protocolVersion\":"
                      << biocore::worker_protocol::current_protocol_version << "}\n";
            return 0;
        }
    }

    if (argc == base_worker_argument_count || argc == plan_worker_argument_count) {
        std::vector<std::string_view> arguments;
        arguments.reserve(static_cast<std::size_t>(argc - 1));
        for (int index = 1; index < argc; ++index) arguments.emplace_back(argv[index]);
        try {
            const auto parsed = biocore::worker_protocol::parse_launch_arguments(arguments);
            if (parsed.execution_plan_path.has_value()) return run_execution_plan(parsed);
            run_bootstrap_lifecycle(parsed);
            return 0;
        } catch (const std::exception& error) {
            try {
                const auto parsed = biocore::worker_protocol::parse_launch_arguments(arguments);
                emit_failed(parsed, 1U, error.what());
            } catch (...) {
                std::cerr << error.what() << '\n';
                return 2;
            }
            return static_cast<int>(execution_failure_exit_code);
        }
    }

    print_usage();
    return argc == 1 ? 0 : 2;
}
