#include <chrono>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "biocore/application/i_id_generator.hpp"
#include "biocore/application/i_input_file_storage.hpp"
#include "biocore/application/i_managed_file_repository.hpp"
#include "biocore/application/i_monotonic_clock.hpp"
#include "biocore/application/i_utc_clock.hpp"
#include "biocore/application/managed_file_service.hpp"
#include "biocore/application/managed_file_service_error.hpp"
#include "biocore/domain/managed_file.hpp"

namespace {

using biocore::application::IIdGenerator;
using biocore::application::IInputFileImportTransaction;
using biocore::application::IInputFileStorage;
using biocore::application::IManagedFileRepository;
using biocore::application::IMonotonicClock;
using biocore::application::IUtcClock;
using biocore::application::ManagedFileService;
using biocore::application::ManagedFileServiceError;
using biocore::application::ManagedFileServiceErrorCode;
using biocore::application::BeginManagedFileUploadRequest;
using biocore::application::PreparedManagedCopy;
using biocore::application::RegisterManagedCopyRequest;
using biocore::domain::ManagedFile;

class FakeIdGenerator final : public IIdGenerator {
public:
    explicit FakeIdGenerator(std::deque<std::string> values, std::vector<std::string>* trace = nullptr)
        : values_{std::move(values)}, trace_{trace} {}
    std::string generate() override {
        events.push_back("id");
        if (trace_ != nullptr) trace_->push_back("id");
        ++calls;
        std::string value = std::move(values_.front());
        values_.pop_front();
        return value;
    }
    std::vector<std::string> events;
    int calls{0};
private:
    std::deque<std::string> values_;
    std::vector<std::string>* trace_;
};

class FakeClock final : public IUtcClock, public IMonotonicClock {
public:
    explicit FakeClock(std::vector<std::string>* trace = nullptr) : trace_{trace} {}
    std::string now_utc_iso8601() override {
        events.push_back("clock");
        if (trace_ != nullptr) trace_->push_back("clock");
        ++calls;
        return "2026-08-06T21:00:00Z";
    }
    std::chrono::steady_clock::time_point now() override { return monotonic_now; }
    void advance(const std::chrono::steady_clock::duration duration) {
        monotonic_now += duration;
    }
    std::vector<std::string> events;
    int calls{0};
    std::chrono::steady_clock::time_point monotonic_now{};
private:
    std::vector<std::string>* trace_;
};

class FakeTransaction final : public IInputFileImportTransaction {
public:
    FakeTransaction(
        PreparedManagedCopy prepared,
        bool& committed,
        bool& destroyed,
        std::vector<std::string>* trace
    )
        : prepared_{std::move(prepared)}, committed_{committed}, destroyed_{destroyed}, trace_{trace} {}
    ~FakeTransaction() override { destroyed_ = true; }
    const PreparedManagedCopy& prepared_file() const noexcept override { return prepared_; }
    void commit() noexcept override {
        committed_ = true;
        if (trace_ != nullptr) trace_->push_back("commit");
    }
private:
    PreparedManagedCopy prepared_;
    bool& committed_;
    bool& destroyed_;
    std::vector<std::string>* trace_;
};

class FakeStorage final : public IInputFileStorage {
public:
    explicit FakeStorage(std::vector<std::string>* trace = nullptr) : trace_{trace} {}
    std::unique_ptr<IInputFileImportTransaction> prepare_managed_copy(
        std::string_view source_path,
        std::string_view id
    ) override {
        ++calls;
        events.push_back("prepare:" + std::string{id});
        if (trace_ != nullptr) trace_->push_back("prepare");
        return std::make_unique<FakeTransaction>(PreparedManagedCopy{
            .display_name = "örnek.fastq",
            .original_path = std::string{source_path},
            .managed_path = "/project/inputs/" + std::string{id} + "/örnek.fastq",
            .relative_project_path = "inputs/" + std::string{id} + "/örnek.fastq",
            .size_bytes = 12,
            .checksum_algorithm = std::string{"sha256"},
            .checksum_value = std::string(64U, 'a'),
        }, committed, destroyed, trace_);
    }
    bool begin_browser_upload(
        std::string_view upload_id,
        std::string_view display_name
    ) override {
        ++begin_upload_calls;
        return browser_uploads.emplace(
            std::string{upload_id},
            BrowserUpload{std::string{display_name}, {}}
        ).second;
    }

    std::uint64_t append_browser_upload(
        std::string_view upload_id,
        std::uint64_t expected_offset,
        std::string_view bytes
    ) override {
        ++append_upload_calls;
        auto iterator = browser_uploads.find(std::string{upload_id});
        if (iterator == browser_uploads.end() ||
            iterator->second.bytes.size() != expected_offset) {
            throw std::runtime_error("fake upload offset mismatch");
        }
        iterator->second.bytes.append(bytes);
        return static_cast<std::uint64_t>(iterator->second.bytes.size());
    }

    std::unique_ptr<IInputFileImportTransaction> prepare_browser_upload_commit(
        std::string_view upload_id,
        std::string_view id
    ) override {
        ++prepare_upload_commit_calls;
        auto iterator = browser_uploads.find(std::string{upload_id});
        if (iterator == browser_uploads.end()) {
            throw std::runtime_error("fake upload missing");
        }
        return std::make_unique<FakeTransaction>(PreparedManagedCopy{
            .display_name = iterator->second.display_name,
            .original_path = "/project/.biocore/runtime/browser-uploads/" +
                std::string{upload_id} + "/" + iterator->second.display_name,
            .managed_path = "/project/inputs/" + std::string{id} + "/" +
                iterator->second.display_name,
            .relative_project_path = "inputs/" + std::string{id} + "/" +
                iterator->second.display_name,
            .size_bytes = static_cast<std::int64_t>(iterator->second.bytes.size()) +
                reported_upload_size_delta,
            .checksum_algorithm = std::string{"sha256"},
            .checksum_value = std::string(64U, 'b'),
        }, committed, destroyed, trace_);
    }

    void discard_browser_upload(std::string_view upload_id) noexcept override {
        ++discard_upload_calls;
        browser_uploads.erase(std::string{upload_id});
    }

    struct BrowserUpload final {
        std::string display_name;
        std::string bytes;
    };

    std::vector<std::string> events;
    std::unordered_map<std::string, BrowserUpload> browser_uploads;
    bool committed{false};
    bool destroyed{false};
    int calls{0};
    int begin_upload_calls{0};
    int append_upload_calls{0};
    int prepare_upload_commit_calls{0};
    int discard_upload_calls{0};
    std::int64_t reported_upload_size_delta{0};
private:
    std::vector<std::string>* trace_;
};

class FakeRepository final : public IManagedFileRepository {
public:
    explicit FakeRepository(std::vector<std::string>* trace = nullptr) : trace_{trace} {}
    bool add(const ManagedFile& file) override {
        ++add_calls;
        events.push_back("add:" + std::string{file.id()});
        if (trace_ != nullptr) trace_->push_back("add");
        if (throw_on_add) {
            throw std::runtime_error("database failed");
        }
        if (force_conflict) {
            return false;
        }
        files.push_back(file);
        return true;
    }
    std::optional<ManagedFile> find_by_id(std::string_view id) override {
        ++find_calls;
        events.push_back("find:" + std::string{id});
        if (trace_ != nullptr) trace_->push_back("find");
        for (const auto& file : files) {
            if (file.id() == id) return file;
        }
        return std::nullopt;
    }
    std::optional<ManagedFile> find_by_relative_project_path(std::string_view path) override {
        for (const auto& file : files) {
            if (file.relative_project_path().has_value() && *file.relative_project_path() == path) return file;
        }
        return std::nullopt;
    }
    std::vector<ManagedFile> list() override { return files; }
    bool add_generated_output(const ManagedFile&, const biocore::application::GeneratedOutputProvenance&) override { return false; }
    bool add_generated_outputs_batch(
        std::span<const biocore::application::GeneratedOutputArtifact>
    ) override { return false; }
    std::optional<biocore::application::GeneratedOutputArtifact> find_generated_output(std::string_view, std::string_view, std::string_view) override { return std::nullopt; }
    std::vector<biocore::application::GeneratedOutputArtifact> list_generated_outputs(std::string_view) override { return {}; }
    std::vector<ManagedFile> files;
    std::vector<std::string> events;
    bool force_conflict{false};
    bool throw_on_add{false};
    int find_calls{0};
    int add_calls{0};
private:
    std::vector<std::string>* trace_;
};

[[nodiscard]] ManagedFile existing(std::string id) {
    return ManagedFile{std::move(id), "x", biocore::domain::StorageMode::managed_copy, "/s", "/m", "inputs/x", "fastq", 1, std::nullopt, std::nullopt, std::nullopt, "c", "u"};
}

[[nodiscard]] bool success_contract() {
    std::vector<std::string> trace;
    FakeRepository repository{&trace};
    FakeStorage storage{&trace};
    FakeIdGenerator ids{{"file-1"}, &trace};
    FakeClock clock{&trace};
    ManagedFileService service{repository, storage, ids, clock, clock};
    const ManagedFile file = service.register_managed_copy({"/source/örnek.fastq", "fastq"});
    return file.id() == "file-1" && file.display_name() == "örnek.fastq" &&
           file.size_bytes() == 12 && file.checksum_algorithm() == std::optional<std::string>{"sha256"} &&
           file.checksum_value() == std::optional<std::string>{std::string(64U, 'a')} &&
           file.created_at_utc() == "2026-08-06T21:00:00Z" &&
           repository.find_calls == 1 && repository.add_calls == 1 && storage.calls == 1 &&
           storage.committed && storage.destroyed && ids.calls == 1 && clock.calls == 1 &&
           trace == std::vector<std::string>({"clock", "id", "find", "prepare", "add", "commit"});
}

[[nodiscard]] bool collision_contract() {
    FakeRepository repository;
    repository.files.push_back(existing("collision"));
    FakeStorage storage;
    FakeIdGenerator ids{{"collision", "file-2"}};
    FakeClock clock;
    ManagedFileService service{repository, storage, ids, clock, clock};
    const ManagedFile file = service.register_managed_copy({"/source/x", "unknown"});
    if (file.id() != "file-2" || storage.calls != 1 || ids.calls != 2 || clock.calls != 1) return false;

    FakeRepository conflicts;
    conflicts.force_conflict = true;
    FakeStorage conflict_storage;
    FakeIdGenerator conflict_ids{{"1", "2", "3", "4", "5", "6", "7", "8"}};
    FakeClock conflict_clock;
    ManagedFileService conflict_service{conflicts, conflict_storage, conflict_ids, conflict_clock, conflict_clock};
    try {
        static_cast<void>(conflict_service.register_managed_copy({"/source/x", "unknown"}));
    } catch (const ManagedFileServiceError& error) {
        return error.code() == ManagedFileServiceErrorCode::identifier_generation_exhausted &&
               conflict_storage.calls == ManagedFileService::maximum_identifier_attempts &&
               conflict_ids.calls == ManagedFileService::maximum_identifier_attempts &&
               conflict_clock.calls == 1;
    }
    return false;
}

[[nodiscard]] bool rollback_and_validation_contract() {
    FakeRepository repository;
    repository.throw_on_add = true;
    FakeStorage storage;
    FakeIdGenerator ids{{"file-1"}};
    FakeClock clock;
    ManagedFileService service{repository, storage, ids, clock, clock};
    try {
        static_cast<void>(service.register_managed_copy({"/source/x", "fastq"}));
        return false;
    } catch (const std::runtime_error&) {
        if (storage.committed || !storage.destroyed) return false;
    }

    FakeRepository validation_repository;
    FakeStorage validation_storage;
    FakeIdGenerator validation_ids{{"unused"}};
    FakeClock validation_clock;
    ManagedFileService validation_service{
        validation_repository, validation_storage, validation_ids, validation_clock,
        validation_clock};
    try {
        static_cast<void>(validation_service.register_managed_copy({" ", "fastq"}));
        return false;
    } catch (const std::invalid_argument&) {
        return validation_storage.calls == 0 && validation_ids.calls == 0 &&
               validation_clock.calls == 0 && validation_repository.add_calls == 0;
    }
}


[[nodiscard]] bool browser_upload_contract() {
    FakeRepository repository;
    FakeStorage storage;
    FakeIdGenerator ids{{"upload-1", "file-1"}};
    FakeClock clock;
    ManagedFileService service{repository, storage, ids, clock, clock};

    const auto started = service.begin_upload(BeginManagedFileUploadRequest{
        .display_name = "genome.fa",
        .file_type = "fasta",
        .size_bytes = 4U,
    });
    if (started.upload_id != "upload-1" || started.received_size_bytes != 0U ||
        storage.begin_upload_calls != 1) {
        return false;
    }

    const auto first = service.append_upload("upload-1", 0U, "AC");
    const auto second = service.append_upload("upload-1", 2U, "GT");
    if (first.received_size_bytes != 2U || second.received_size_bytes != 4U) {
        return false;
    }

    try {
        static_cast<void>(service.append_upload("upload-1", 1U, "A"));
        return false;
    } catch (const ManagedFileServiceError& error) {
        if (error.code() != ManagedFileServiceErrorCode::upload_offset_mismatch) {
            return false;
        }
    }

    const ManagedFile file = service.complete_upload("upload-1");
    return file.id() == "file-1" && file.display_name() == "genome.fa" &&
           file.file_type() == "fasta" && file.size_bytes() == 4 &&
           file.checksum_algorithm() == std::optional<std::string>{"sha256"} &&
           file.checksum_value() == std::optional<std::string>{std::string(64U, 'b')} &&
           storage.prepare_upload_commit_calls == 1 &&
           storage.discard_upload_calls == 1 && storage.browser_uploads.empty() &&
           repository.files.size() == 1U && clock.calls == 1;
}

[[nodiscard]] bool upload_inactivity_ttl_contract() {
    using namespace std::chrono_literals;

    FakeRepository repository;
    FakeStorage storage;
    FakeIdGenerator ids{{
        "u1", "u2", "u3", "u4", "u5", "u6", "u7", "u8", "u9",
        "refresh", "refresh-file", "stale-append", "stale-complete"
    }};
    FakeClock clock;
    ManagedFileService service{repository, storage, ids, clock, clock};

    for (int index = 0; index < 8; ++index) {
        static_cast<void>(service.begin_upload({
            .display_name = "stale-" + std::to_string(index) + ".fa",
            .file_type = "fasta",
            .size_bytes = 1U,
        }));
    }

    clock.advance(ManagedFileService::upload_inactivity_timeout - 1ms);
    try {
        static_cast<void>(service.begin_upload({
            .display_name = "still-full.fa", .file_type = "fasta", .size_bytes = 1U
        }));
        return false;
    } catch (const ManagedFileServiceError& error) {
        if (error.code() != ManagedFileServiceErrorCode::upload_session_limit ||
            storage.discard_upload_calls != 0) {
            return false;
        }
    }

    clock.advance(1ms);
    const auto reclaimed = service.begin_upload({
        .display_name = "reclaimed.fa", .file_type = "fasta", .size_bytes = 1U
    });
    if (reclaimed.upload_id != "u9" || storage.discard_upload_calls != 8 ||
        storage.browser_uploads.size() != 1U) {
        return false;
    }
    if (!service.cancel_upload("u9")) return false;

    const auto refresh = service.begin_upload({
        .display_name = "refresh.fa", .file_type = "fasta", .size_bytes = 2U
    });
    clock.advance(20min);
    static_cast<void>(service.append_upload(refresh.upload_id, 0U, "A"));
    clock.advance(20min);
    static_cast<void>(service.append_upload(refresh.upload_id, 1U, "C"));
    clock.advance(20min);
    const auto file = service.complete_upload(refresh.upload_id);
    if (file.id() != "refresh-file" || file.size_bytes() != 2 ||
        !storage.browser_uploads.empty()) {
        return false;
    }

    const auto stale_append = service.begin_upload({
        .display_name = "stale-append.fa", .file_type = "fasta", .size_bytes = 1U
    });
    clock.advance(ManagedFileService::upload_inactivity_timeout);
    try {
        static_cast<void>(service.append_upload(stale_append.upload_id, 0U, "A"));
        return false;
    } catch (const ManagedFileServiceError& error) {
        if (error.code() != ManagedFileServiceErrorCode::upload_not_found ||
            !storage.browser_uploads.empty()) {
            return false;
        }
    }

    const auto stale_complete = service.begin_upload({
        .display_name = "stale-complete.fa", .file_type = "fasta", .size_bytes = 1U
    });
    clock.advance(ManagedFileService::upload_inactivity_timeout);
    try {
        static_cast<void>(service.complete_upload(stale_complete.upload_id));
        return false;
    } catch (const ManagedFileServiceError& error) {
        return error.code() == ManagedFileServiceErrorCode::upload_not_found &&
               storage.browser_uploads.empty();
    }
}

[[nodiscard]] bool browser_upload_fail_closed_contract() {
    FakeRepository repository;
    FakeStorage storage;
    FakeIdGenerator ids{{"upload-a", "upload-b", "file-tampered", "upload-c"}};
    FakeClock clock;
    ManagedFileService service{repository, storage, ids, clock, clock};

    try {
        static_cast<void>(service.begin_upload({
            .display_name = "../escape.fa", .file_type = "fasta", .size_bytes = 1U
        }));
        return false;
    } catch (const std::invalid_argument&) {
        if (storage.begin_upload_calls != 0) return false;
    }

    static_cast<void>(service.begin_upload({
        .display_name = "partial.fa", .file_type = "fasta", .size_bytes = 3U
    }));
    static_cast<void>(service.append_upload("upload-a", 0U, "AC"));
    try {
        static_cast<void>(service.complete_upload("upload-a"));
        return false;
    } catch (const ManagedFileServiceError& error) {
        if (error.code() != ManagedFileServiceErrorCode::upload_incomplete) {
            return false;
        }
    }
    if (!service.cancel_upload("upload-a") || service.cancel_upload("upload-a")) {
        return false;
    }

    static_cast<void>(service.begin_upload({
        .display_name = "tampered.fa", .file_type = "fasta", .size_bytes = 2U
    }));
    static_cast<void>(service.append_upload("upload-b", 0U, "AC"));
    storage.reported_upload_size_delta = 1;
    try {
        static_cast<void>(service.complete_upload("upload-b"));
        return false;
    } catch (const ManagedFileServiceError& error) {
        if (error.code() != ManagedFileServiceErrorCode::upload_staging_mismatch ||
            !repository.files.empty()) {
            return false;
        }
    }
    storage.reported_upload_size_delta = 0;
    if (!service.cancel_upload("upload-b")) return false;

    static_cast<void>(service.begin_upload({
        .display_name = "too-small.fa", .file_type = "fasta", .size_bytes = 1U
    }));
    try {
        static_cast<void>(service.append_upload("upload-c", 0U, "AC"));
        return false;
    } catch (const ManagedFileServiceError& error) {
        return error.code() == ManagedFileServiceErrorCode::upload_size_exceeded;
    }
}

}  // namespace

int main() {
    if (!success_contract() || !collision_contract() || !rollback_and_validation_contract() ||
        !browser_upload_contract() || !upload_inactivity_ttl_contract() ||
        !browser_upload_fail_closed_contract()) {
        std::cerr << "ManagedFileService contract failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
