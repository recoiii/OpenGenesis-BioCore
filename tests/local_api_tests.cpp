#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "biocore/application/artifact_presentation_service.hpp"
#include "biocore/application/generated_output_artifact.hpp"
#include "biocore/application/i_artifact_content_access.hpp"
#include "biocore/application/i_id_generator.hpp"
#include "biocore/application/i_job_repository.hpp"
#include "biocore/application/i_job_submitter.hpp"
#include "biocore/application/i_input_file_storage.hpp"
#include "biocore/application/i_managed_file_repository.hpp"
#include "biocore/application/i_monotonic_clock.hpp"
#include "biocore/application/i_utc_clock.hpp"
#include "biocore/application/job_service.hpp"
#include "biocore/application/managed_file_service.hpp"
#include "biocore/application/pipeline_bindings.hpp"
#include "biocore/domain/job.hpp"
#include "biocore/domain/managed_file.hpp"
#include "biocore/domain/storage_mode.hpp"
#include "biocore/presentation/local_api.hpp"
#include "biocore/presentation/local_browser_session.hpp"

namespace {

class FakeClock final : public biocore::application::IUtcClock,
                        public biocore::application::IMonotonicClock {
public:
    std::string now_utc_iso8601() override { return "2026-08-07T11:40:00Z"; }
    std::chrono::steady_clock::time_point now() override { return {}; }
};

class FakeIdGenerator final : public biocore::application::IIdGenerator {
public:
    std::string generate() override { return "job-new"; }
};

class FakeJobRepository final : public biocore::application::IJobRepository {
public:
    bool add(const biocore::domain::Job& job) override {
        if (find_by_id(job.id()).has_value()) return false;
        jobs.push_back(job);
        return true;
    }
    std::optional<biocore::domain::Job> find_by_id(const std::string_view job_id) override {
        for (const auto& job : jobs) if (job.id() == job_id) return job;
        return std::nullopt;
    }
    std::vector<biocore::domain::Job> list() override { return jobs; }
    bool update_runtime_state(const biocore::domain::Job& job, const std::int64_t expected_revision) override {
        for (auto& stored : jobs) {
            if (stored.id() == job.id() && stored.revision() == expected_revision) {
                stored = job;
                return true;
            }
        }
        return false;
    }
    std::vector<biocore::domain::Job> jobs;
};


class FakeJobSubmitter final : public biocore::application::IJobSubmitter {
public:
    biocore::domain::Job submit(const biocore::application::SubmitJobRequest& request) override {
        last_request = request;
        return biocore::domain::Job{
            "job-new", request.analysis_id, request.pipeline_id, request.pipeline_version,
            biocore::domain::JobStatus::queued, request.priority, 0.0, std::nullopt,
            "2026-08-07T11:40:00Z", "2026-08-07T11:40:01Z",
            std::nullopt, std::nullopt, 1
        };
    }
    std::optional<biocore::application::SubmitJobRequest> last_request;
};

class FakeManagedFileRepository final : public biocore::application::IManagedFileRepository {
public:
    bool add(const biocore::domain::ManagedFile& file) override {
        if (find_by_id(file.id()).has_value()) return false;
        files.push_back(file);
        return true;
    }
    std::optional<biocore::domain::ManagedFile> find_by_id(std::string_view id) override {
        for (const auto& file : files) if (file.id() == id) return file;
        return std::nullopt;
    }
    std::optional<biocore::domain::ManagedFile> find_by_relative_project_path(std::string_view path) override {
        for (const auto& file : files) {
            if (file.relative_project_path().has_value() && *file.relative_project_path() == path) return file;
        }
        return std::nullopt;
    }
    std::vector<biocore::domain::ManagedFile> list() override { return files; }
    bool add_generated_output(const biocore::domain::ManagedFile&, const biocore::application::GeneratedOutputProvenance&) override { return false; }
    bool add_generated_outputs_batch(std::span<const biocore::application::GeneratedOutputArtifact>) override { return false; }
    std::optional<biocore::application::GeneratedOutputArtifact> find_generated_output(
        const std::string_view job_id,
        const std::string_view step_id,
        const std::string_view output_port
    ) override {
        if (artifact.has_value() && artifact->provenance.job_id == job_id && artifact->provenance.step_id == step_id && artifact->provenance.output_port == output_port) return artifact;
        return std::nullopt;
    }
    std::vector<biocore::application::GeneratedOutputArtifact> list_generated_outputs(const std::string_view job_id) override {
        if (artifact.has_value() && artifact->provenance.job_id == job_id) return {*artifact};
        return {};
    }
    std::optional<double> latest_generated_output_progress(std::string_view) override { return std::nullopt; }
    std::optional<biocore::application::GeneratedOutputArtifact> artifact;
    std::vector<biocore::domain::ManagedFile> files;
};


class SequenceIdGenerator final : public biocore::application::IIdGenerator {
public:
    explicit SequenceIdGenerator(std::deque<std::string> values)
        : values_{std::move(values)} {}
    std::string generate() override {
        if (values_.empty()) throw std::runtime_error("No fake ID remaining");
        std::string value = std::move(values_.front());
        values_.pop_front();
        return value;
    }
private:
    std::deque<std::string> values_;
};

class FakeInputTransaction final : public biocore::application::IInputFileImportTransaction {
public:
    explicit FakeInputTransaction(biocore::application::PreparedManagedCopy prepared)
        : prepared_{std::move(prepared)} {}
    const biocore::application::PreparedManagedCopy& prepared_file() const noexcept override {
        return prepared_;
    }
    void commit() noexcept override { committed = true; }
    bool committed{false};
private:
    biocore::application::PreparedManagedCopy prepared_;
};

class FakeInputStorage final : public biocore::application::IInputFileStorage {
public:
    std::unique_ptr<biocore::application::IInputFileImportTransaction> prepare_managed_copy(
        std::string_view, std::string_view
    ) override {
        throw std::runtime_error("not used");
    }
    bool begin_browser_upload(std::string_view upload_id, std::string_view display_name) override {
        return uploads.emplace(std::string{upload_id}, Upload{std::string{display_name}, {}}).second;
    }
    std::uint64_t append_browser_upload(
        std::string_view upload_id,
        std::uint64_t expected_offset,
        std::string_view bytes
    ) override {
        auto iterator = uploads.find(std::string{upload_id});
        if (iterator == uploads.end() || iterator->second.bytes.size() != expected_offset) {
            throw std::runtime_error("fake upload mismatch");
        }
        iterator->second.bytes.append(bytes);
        return static_cast<std::uint64_t>(iterator->second.bytes.size());
    }
    std::unique_ptr<biocore::application::IInputFileImportTransaction>
    prepare_browser_upload_commit(std::string_view upload_id, std::string_view file_id) override {
        auto iterator = uploads.find(std::string{upload_id});
        if (iterator == uploads.end()) throw std::runtime_error("fake upload missing");
        return std::make_unique<FakeInputTransaction>(biocore::application::PreparedManagedCopy{
            .display_name = iterator->second.display_name,
            .original_path = "/project/.biocore/runtime/browser-uploads/" + std::string{upload_id},
            .managed_path = "/project/inputs/" + std::string{file_id} + "/" + iterator->second.display_name,
            .relative_project_path = "inputs/" + std::string{file_id} + "/" + iterator->second.display_name,
            .size_bytes = static_cast<std::int64_t>(iterator->second.bytes.size()),
        });
    }
    void discard_browser_upload(std::string_view upload_id) noexcept override {
        uploads.erase(std::string{upload_id});
    }
private:
    struct Upload final { std::string display_name; std::string bytes; };
    std::unordered_map<std::string, Upload> uploads;
};

class FakeContentAccess final : public biocore::application::IArtifactContentAccess {
public:
    biocore::application::ArtifactContentVerification verify_for_download(
        const biocore::application::GeneratedOutputArtifact&
    ) override {
        return {
            .status = biocore::application::ArtifactContentStatus::verified,
            .content_path = "/secret/project/outputs/job-a--step-a--result.out",
            .computed_sha256 = std::string(64U, 'a'),
            .actual_size_bytes = 7,
        };
    }
};

[[nodiscard]] biocore::domain::Job existing_job() {
    return biocore::domain::Job{
        "job-a", std::nullopt, "pipe", "1.0.0",
        biocore::domain::JobStatus::completed, biocore::domain::JobPriority::normal,
        1.0, std::nullopt, "2026-01-01T00:00:00Z", "2026-01-01T00:01:00Z",
        "2026-01-01T00:00:01Z", "2026-01-01T00:01:00Z", 3
    };
}

[[nodiscard]] biocore::domain::Job cancellable_job(
    std::string id,
    const biocore::domain::JobStatus status,
    const double progress
) {
    return biocore::domain::Job{
        std::move(id), std::nullopt, "pipe", "1.0.0", status,
        biocore::domain::JobPriority::normal, progress, std::nullopt,
        "2026-01-01T00:00:00Z", "2026-01-01T00:00:30Z",
        status == biocore::domain::JobStatus::running
            ? std::optional<std::string>{"2026-01-01T00:00:01Z"}
            : std::nullopt,
        std::nullopt,
        2
    };
}

[[nodiscard]] biocore::application::GeneratedOutputArtifact existing_artifact() {
    return {
        .file = biocore::domain::ManagedFile{
            "file-a", "result.bin", biocore::domain::StorageMode::generated_output,
            std::nullopt, "/secret/project/outputs/job-a--step-a--result.out",
            "outputs/job-a--step-a--result.out", "binary", 7, "2026-01-01T00:00:30Z",
            "sha256", std::string(64U, 'a'), "2026-01-01T00:00:30Z", "2026-01-01T00:00:30Z"
        },
        .provenance = {
            .job_id = "job-a", .step_id = "step-a", .output_port = "result",
            .plugin_id = "org.biocore.demo", .plugin_version = "1.0.0",
            .module_id = "org.biocore.demo.copy", .file_type = "binary",
            .relative_project_path = "outputs/job-a--step-a--result.out", .step_progress = 1.0,
            .registered_at_utc = "2026-01-01T00:00:30Z",
        },
    };
}

void require(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

}  // namespace

int main() {
    FakeClock clock;
    FakeIdGenerator ids;
    FakeJobRepository jobs_repo;
    jobs_repo.jobs.push_back(existing_job());
    jobs_repo.jobs.push_back(cancellable_job(
        "job-run", biocore::domain::JobStatus::running, 0.5
    ));
    jobs_repo.jobs.push_back(cancellable_job(
        "job-queued", biocore::domain::JobStatus::queued, 0.0
    ));
    FakeManagedFileRepository files_repo;
    files_repo.artifact = existing_artifact();
    FakeContentAccess content;
    FakeInputStorage input_storage;
    SequenceIdGenerator file_ids{{"upload-1", "file-1", "upload-2"}};
    biocore::application::JobService jobs{jobs_repo, ids, clock};
    biocore::application::ManagedFileService managed_files{
        files_repo, input_storage, file_ids, clock, clock
    };
    biocore::application::ArtifactPresentationService artifacts{files_repo, jobs_repo, content, clock};
    FakeJobSubmitter submissions;
    const std::string bootstrap_token(64U, 'b');
    const std::string browser_token(64U, 'c');
    biocore::presentation::LocalBrowserSession browser_session{8421U, browser_token};
    biocore::presentation::LocalApiController api{
        jobs, submissions, managed_files, artifacts, clock, bootstrap_token, browser_session
    };

    const auto health = api.handle({.method = biocore::presentation::HttpMethod::get, .target = "/api/v1/health", .authorization = {}, .body = {}});
    require(health.status == 200, "health should be public");
    require(health.body.find("biocore-local-api") != std::string::npos, "health component");

    const auto unauthorized = api.handle({.method = biocore::presentation::HttpMethod::get, .target = "/api/v1/jobs", .authorization = {}, .body = {}});
    require(unauthorized.status == 401, "jobs must require authorization");
    const auto prefix_only = api.handle({.method = biocore::presentation::HttpMethod::get, .target = "/api/v1/jobs", .authorization = "Bearer ", .body = {}});
    require(prefix_only.status == 401, "empty bearer token must fail");

    const std::string auth = "Bearer " + bootstrap_token;

    const auto wrong_origin_session = api.handle({
        .method = biocore::presentation::HttpMethod::post,
        .target = "/api/v1/session",
        .authorization = {},
        .browser_session = {},
        .origin = "http://evil.test:8421",
        .body = std::string{"{\"bootstrapToken\":\""} + bootstrap_token + "\"}",
    });
    require(wrong_origin_session.status == 403,
            "browser session exchange must require exact local origin");

    const auto wrong_token_session = api.handle({
        .method = biocore::presentation::HttpMethod::post,
        .target = "/api/v1/session",
        .authorization = {},
        .browser_session = {},
        .origin = "http://127.0.0.1:8421",
        .body = R"({"bootstrapToken":"wrong"})",
    });
    require(wrong_token_session.status == 401,
            "browser session exchange must reject wrong bootstrap token");

    const auto browser_exchange = api.handle({
        .method = biocore::presentation::HttpMethod::post,
        .target = "/api/v1/session",
        .authorization = {},
        .browser_session = {},
        .origin = "http://127.0.0.1:8421",
        .body = std::string{"{\"bootstrapToken\":\""} + bootstrap_token + "\"}",
    });
    require(browser_exchange.status == 200,
            "correct bootstrap token should establish browser session");
    const auto cookie_header = std::ranges::find_if(
        browser_exchange.headers,
        [](const auto& header) { return header.first == "Set-Cookie"; }
    );
    require(cookie_header != browser_exchange.headers.end(), "session exchange Set-Cookie header");
    require(cookie_header->second.find(browser_token) != std::string::npos,
            "session exchange must issue the separate browser token");
    require(cookie_header->second.find(bootstrap_token) == std::string::npos,
            "bootstrap bearer must never become the browser cookie");
    require(cookie_header->second.find("HttpOnly") != std::string::npos &&
                cookie_header->second.find("SameSite=Strict") != std::string::npos,
            "session cookie security attributes");

    const auto cookie_list = api.handle({
        .method = biocore::presentation::HttpMethod::get,
        .target = "/api/v1/jobs",
        .authorization = {},
        .browser_session = browser_token,
        .origin = {},
        .body = {},
    });
    require(cookie_list.status == 200 && cookie_list.body.find("job-a") != std::string::npos,
            "HttpOnly browser session must authorize REST reads");

    const auto wrong_cookie_list = api.handle({
        .method = biocore::presentation::HttpMethod::get,
        .target = "/api/v1/jobs",
        .authorization = {},
        .browser_session = std::string(64U, 'd'),
        .origin = {},
        .body = {},
    });
    require(wrong_cookie_list.status == 401,
            "wrong browser session token must not authorize REST reads");


    const auto upload_wrong_origin = api.handle({
        .method = biocore::presentation::HttpMethod::post,
        .target = "/api/v1/files/uploads",
        .authorization = {},
        .browser_session = browser_token,
        .origin = "http://evil.test:8421",
        .content_type = "application/json",
        .body = R"({"displayName":"genome.fa","fileType":"fasta","sizeBytes":5})",
    });
    require(upload_wrong_origin.status == 403,
            "cookie-authenticated file upload must require exact local origin");

    const auto upload_start = api.handle({
        .method = biocore::presentation::HttpMethod::post,
        .target = "/api/v1/files/uploads",
        .authorization = {},
        .browser_session = browser_token,
        .origin = "http://127.0.0.1:8421",
        .content_type = "application/json",
        .body = R"({"displayName":"genome.fa","fileType":"fasta","sizeBytes":5})",
    });
    require(upload_start.status == 201 &&
                upload_start.body.find("\"uploadId\":\"upload-1\"") != std::string::npos &&
                upload_start.body.find("\"maxChunkBytes\":1048576") != std::string::npos,
            "browser upload start contract");

    const std::string binary_chunk{"AC\0GT", 5U};
    const auto upload_chunk = api.handle({
        .method = biocore::presentation::HttpMethod::post,
        .target = "/api/v1/files/uploads/upload-1/chunks",
        .authorization = {},
        .browser_session = browser_token,
        .origin = "http://127.0.0.1:8421",
        .content_type = "application/octet-stream",
        .upload_offset = "0",
        .body = binary_chunk,
    });
    require(upload_chunk.status == 200 &&
                upload_chunk.body.find("\"receivedBytes\":5") != std::string::npos,
            "binary upload chunk must bypass UTF-8 body validation");

    const auto upload_wrong_offset = api.handle({
        .method = biocore::presentation::HttpMethod::post,
        .target = "/api/v1/files/uploads/upload-1/chunks",
        .authorization = {},
        .browser_session = browser_token,
        .origin = "http://127.0.0.1:8421",
        .content_type = "application/octet-stream",
        .upload_offset = "4",
        .body = "A",
    });
    require(upload_wrong_offset.status == 409,
            "out-of-order upload chunk must be rejected");

    const auto upload_complete = api.handle({
        .method = biocore::presentation::HttpMethod::post,
        .target = "/api/v1/files/uploads/upload-1/complete",
        .authorization = {},
        .browser_session = browser_token,
        .origin = "http://127.0.0.1:8421",
        .body = {},
    });
    require(upload_complete.status == 201 &&
                upload_complete.body.find("\"id\":\"file-1\"") != std::string::npos &&
                upload_complete.body.find("genome.fa") != std::string::npos &&
                upload_complete.body.find("/project/") == std::string::npos,
            "completed upload must return path-free managed-file metadata");

    const auto file_list = api.handle({
        .method = biocore::presentation::HttpMethod::get,
        .target = "/api/v1/files",
        .authorization = {},
        .browser_session = browser_token,
        .origin = {},
        .body = {},
    });
    require(file_list.status == 200 &&
                file_list.body.find("\"id\":\"file-1\"") != std::string::npos &&
                file_list.body.find("/project/") == std::string::npos,
            "managed input listing must not expose absolute project paths");

    const auto cookie_write_wrong_origin = api.handle({
        .method = biocore::presentation::HttpMethod::post,
        .target = "/api/v1/jobs",
        .authorization = {},
        .browser_session = browser_token,
        .origin = "http://evil.test:8421",
        .body = R"({"pipelineId":"pipe-2","pipelineVersion":"2.0"})",
    });
    require(cookie_write_wrong_origin.status == 403,
            "cookie-authenticated write must require exact local origin");

    require(api.browser_host_authorized("127.0.0.1:8421"),
            "canonical browser Host must be accepted");
    require(!api.browser_host_authorized("evil.test:8421"),
            "DNS-rebinding Host must be rejected");
    require(api.websocket_authorized({}, browser_token, "http://127.0.0.1:8421"),
            "browser cookie + exact Origin must authorize WebSocket");
    require(!api.websocket_authorized({}, browser_token, "http://evil.test:8421"),
            "browser WebSocket wrong Origin must fail");
    require(api.websocket_authorized(auth, {}, {}),
            "existing bearer WebSocket compatibility must remain");
    const auto list = api.handle({.method = biocore::presentation::HttpMethod::get, .target = "/api/v1/jobs", .authorization = auth, .body = {}});
    require(list.status == 200 && list.body.find("job-a") != std::string::npos, "authorized job list");

    const auto create = api.handle({
        .method = biocore::presentation::HttpMethod::post,
        .target = "/api/v1/jobs",
        .authorization = auth,
        .body = R"({"pipelineId":"pipe-2","pipelineVersion":"2.0","priority":"high"})",
    });
    require(create.status == 201, "job create should return 201");
    require(create.body.find("job-new") != std::string::npos, "job create id");
    require(create.body.find("\"priority\":\"high\"") != std::string::npos, "job create priority");
    require(create.body.find("\"status\":\"queued\"") != std::string::npos, "submitted job must already be queued");
    require(submissions.last_request.has_value() && submissions.last_request->pipeline_id == "pipe-2" &&
            submissions.last_request->pipeline_version == "2.0", "create request must flow through prepared submitter");


    const auto create_with_bindings = api.handle({
        .method = biocore::presentation::HttpMethod::post,
        .target = "/api/v1/jobs",
        .authorization = auth,
        .body = R"({"pipelineId":"pipe-2","pipelineVersion":"2.0","bindings":{"steps":[{"stepId":"copy","parameters":[{"name":"label","value":"hello"},{"name":"repeat","value":2},{"name":"ratio","value":2.5},{"name":"enabled","value":true}],"inputs":[{"portName":"source","managedFileId":"file-1"}]}]}})",
    });
    require(create_with_bindings.status == 201, "job create with bindings should return 201");
    require(submissions.last_request.has_value(), "binding create request must reach submitter");
    const auto& run_bindings = submissions.last_request->bindings;
    require(run_bindings.steps.size() == 1U, "REST binding step count");
    const auto& step_bindings = run_bindings.steps.front();
    require(step_bindings.step_id == "copy", "REST binding step id");
    require(step_bindings.parameters.size() == 4U, "REST parameter binding count");
    const auto* string_value = std::get_if<std::string>(&step_bindings.parameters[0].value);
    const auto* integer_value = std::get_if<std::int64_t>(&step_bindings.parameters[1].value);
    const auto* number_value = std::get_if<double>(&step_bindings.parameters[2].value);
    const auto* boolean_value = std::get_if<bool>(&step_bindings.parameters[3].value);
    require(string_value != nullptr && *string_value == "hello", "REST string parameter type");
    require(integer_value != nullptr && *integer_value == 2, "REST integer parameter type");
    require(number_value != nullptr && *number_value == 2.5, "REST number parameter type");
    require(boolean_value != nullptr && *boolean_value, "REST boolean parameter type");
    require(step_bindings.inputs.size() == 1U && step_bindings.inputs[0].port_name == "source",
            "REST managed input binding count/name");
    const auto* managed_source =
        std::get_if<biocore::application::ManagedFileInputSource>(&step_bindings.inputs[0].source);
    require(managed_source != nullptr && managed_source->file_id == "file-1",
            "REST managed input source");

    const auto rejects_create = [&](const std::string& body) {
        return api.handle({
            .method = biocore::presentation::HttpMethod::post,
            .target = "/api/v1/jobs",
            .authorization = auth,
            .body = body,
        }).status == 400;
    };
    require(rejects_create(
        R"({"pipelineId":"pipe-2","pipelineVersion":"2.0","bindings":{"steps":[{"stepId":"copy","parameters":[{"name":"x","value":null}]}]}})"
    ), "null parameter values must fail at REST boundary");
    require(rejects_create(
        R"({"pipelineId":"pipe-2","pipelineVersion":"2.0","bindings":{"steps":[{"stepId":"copy","parameters":[{"name":"x","value":{}}]}]}})"
    ), "object parameter values must fail at REST boundary");
    require(rejects_create(
        R"({"pipelineId":"pipe-2","pipelineVersion":"2.0","bindings":{"steps":[{"stepId":"copy","parameters":[{"name":"x","value":[]}]}]}})"
    ), "array parameter values must fail at REST boundary");
    require(rejects_create(
        R"({"pipelineId":"pipe-2","pipelineVersion":"2.0","bindings":{"steps":[{"stepId":"copy","parameters":[{"name":"x","value":9223372036854775808}]}]}})"
    ), "out-of-range JSON integer must fail");
    require(rejects_create(
        R"({"pipelineId":"pipe-2","pipelineVersion":"2.0","bindings":{"steps":[{"stepId":"copy","parameters":[{"name":"x","value":01}]}]}})"
    ), "leading-zero JSON integer must fail");
    require(rejects_create(
        R"({"pipelineId":"pipe-2","pipelineVersion":"2.0","bindings":{"steps":[{"stepId":"copy","inputs":[{"portName":"source","stepId":"upstream","outputPort":"result"}]}]}})"
    ), "REST must not expose step-output input bindings");
    require(rejects_create(
        R"({"pipelineId":"pipe-2","pipelineVersion":"2.0","bindings":{"steps":[],"unknown":[]}})"
    ), "unknown nested binding fields must fail");
    require(rejects_create(
        R"({"pipelineId":"pipe-2","pipelineVersion":"2.0","bindings":{},"bindings":{}})"
    ), "duplicate bindings field must fail");

    const std::string oversized_parameter_value(4097U, 'x');
    require(rejects_create(
        std::string{R"({"pipelineId":"pipe-2","pipelineVersion":"2.0","bindings":{"steps":[{"stepId":"copy","parameters":[{"name":"label","value":")"} +
        oversized_parameter_value +
        R"("}] }]}})"
    ), "parameter string beyond Domain maximum must fail");

    const std::string oversized_file_id(129U, 'f');
    require(rejects_create(
        std::string{R"({"pipelineId":"pipe-2","pipelineVersion":"2.0","bindings":{"steps":[{"stepId":"copy","inputs":[{"portName":"source","managedFileId":")"} +
        oversized_file_id +
        R"("}]}]}})"
    ), "managed-file id beyond Domain maximum must fail");

    const std::string invalid_utf8{static_cast<char>(0xc3), static_cast<char>(0x28)};
    const auto invalid_encoding = api.handle({
        .method = biocore::presentation::HttpMethod::post,
        .target = "/api/v1/jobs",
        .authorization = auth,
        .body = invalid_utf8,
    });
    require(invalid_encoding.status == 400 && invalid_encoding.body.find("invalid_request_encoding") != std::string::npos, "invalid UTF-8 must fail before JSON parsing");

    const auto unknown = api.handle({
        .method = biocore::presentation::HttpMethod::post,
        .target = "/api/v1/jobs",
        .authorization = auth,
        .body = R"({"bogus":"x"})",
    });
    require(unknown.status == 400, "unknown create-job fields must fail");

    const auto cancel_running = api.handle({
        .method = biocore::presentation::HttpMethod::post,
        .target = "/api/v1/jobs/job-run/cancel",
        .authorization = auth,
        .body = {},
    });
    require(cancel_running.status == 202 &&
                cancel_running.body.find("\"status\":\"cancelling\"") != std::string::npos,
            "running job cancellation must enter cancelling state");
    require(jobs_repo.find_by_id("job-run")->status() == biocore::domain::JobStatus::cancelling,
            "running cancellation must persist cancelling state");

    const auto cancel_queued = api.handle({
        .method = biocore::presentation::HttpMethod::post,
        .target = "/api/v1/jobs/job-queued/cancel",
        .authorization = auth,
        .body = {},
    });
    require(cancel_queued.status == 200 &&
                cancel_queued.body.find("\"status\":\"cancelled\"") != std::string::npos,
            "queued job cancellation must complete without launching a worker");

    const auto cancel_terminal = api.handle({
        .method = biocore::presentation::HttpMethod::post,
        .target = "/api/v1/jobs/job-a/cancel",
        .authorization = auth,
        .body = {},
    });
    require(cancel_terminal.status == 409 &&
                cancel_terminal.body.find("job_not_cancellable") != std::string::npos,
            "completed job cancellation must fail with conflict");

    const auto cancel_with_body = api.handle({
        .method = biocore::presentation::HttpMethod::post,
        .target = "/api/v1/jobs/job-run/cancel",
        .authorization = auth,
        .body = "{}",
    });
    require(cancel_with_body.status == 400,
            "job cancellation request body must be empty");

    const auto artifacts_response = api.handle({.method = biocore::presentation::HttpMethod::get, .target = "/api/v1/jobs/job-a/artifacts", .authorization = auth, .body = {}});
    require(artifacts_response.status == 200, "artifact list");
    require(artifacts_response.body.find("relativeProjectPath") != std::string::npos, "safe relative path exposed");
    require(artifacts_response.body.find("/secret/project") == std::string::npos, "absolute content path must not leak into artifact JSON");

    const auto report_json = api.handle({.method = biocore::presentation::HttpMethod::get, .target = "/api/v1/jobs/job-a/report.json", .authorization = auth, .body = {}});
    require(report_json.status == 200 && report_json.body.find("\"schemaVersion\":1") != std::string::npos, "JSON report route");
    require(report_json.body.find("/secret/project") == std::string::npos, "report must not expose absolute content path");

    const auto download = api.handle({.method = biocore::presentation::HttpMethod::get, .target = "/api/v1/jobs/job-a/artifacts/step-a/result/download", .authorization = auth, .body = {}});
    require(download.status == 200 && download.file.has_value(), "download descriptor");
    require(download.body.empty(), "download must not serialize internal path into body");
    require(download.file->content_path.starts_with("/secret/project/"), "internal adapter should receive verified content path");
    require(download.file->download_name == "result.bin", "safe download filename");

    const auto traversal = api.handle({.method = biocore::presentation::HttpMethod::get, .target = "/api/v1/jobs/job-a/artifacts/../result", .authorization = auth, .body = {}});
    require(traversal.status == 404, "path traversal atoms must not route");
    const auto encoded = api.handle({.method = biocore::presentation::HttpMethod::get, .target = "/api/v1/jobs/job%2Da", .authorization = auth, .body = {}});
    require(encoded.status == 404, "percent-encoded path must fail closed");

    const auto query = api.handle({.method = biocore::presentation::HttpMethod::get, .target = "/api/v1/jobs?limit=1", .authorization = auth, .body = {}});
    require(query.status == 404, "query strings must fail closed until explicitly supported");

    const auto oversized_auth = api.handle({
        .method = biocore::presentation::HttpMethod::get,
        .target = "/api/v1/jobs",
        .authorization = std::string(4097U, 'x'),
        .body = {},
    });
    require(oversized_auth.status == 413, "oversized authorization header must be rejected");

    require(api.websocket_authorized(auth), "websocket bearer auth helper must accept valid token");
    require(!api.websocket_authorized("Bearer wrong"), "websocket bearer auth helper must reject wrong token");
    require(!api.websocket_snapshot("Bearer wrong").has_value(), "websocket wrong auth must fail");
    const auto ws = api.websocket_snapshot(auth);
    require(ws.has_value() && ws->find("jobs.snapshot") != std::string::npos && ws->find("job-a") != std::string::npos, "websocket initial snapshot");

    std::cout << "Local API tests passed\n";
    return 0;
}
