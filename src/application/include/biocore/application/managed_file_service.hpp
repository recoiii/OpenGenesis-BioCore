#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "biocore/domain/managed_file.hpp"

namespace biocore::application {

class IIdGenerator;
class IInputFileImportTransaction;
class IInputFileStorage;
class IManagedFileRepository;
class IMonotonicClock;
class IUtcClock;

struct RegisterManagedCopyRequest final {
    std::string source_path;
    std::string file_type;
};

struct BeginManagedFileUploadRequest final {
    std::string display_name;
    std::string file_type;
    std::uint64_t size_bytes{0U};
};

struct ManagedFileUploadSession final {
    std::string upload_id;
    std::string display_name;
    std::string file_type;
    std::uint64_t expected_size_bytes{0U};
    std::uint64_t received_size_bytes{0U};
};

class ManagedFileService final {
public:
    static constexpr int maximum_identifier_attempts = 8;
    static constexpr std::size_t maximum_active_uploads = 8U;
    static constexpr std::size_t maximum_upload_chunk_bytes = 1024U * 1024U;
    static constexpr std::uint64_t maximum_upload_file_bytes =
        8ULL * 1024ULL * 1024ULL * 1024ULL;
    static constexpr std::chrono::minutes upload_inactivity_timeout{30};

    ManagedFileService(
        IManagedFileRepository& repository,
        IInputFileStorage& input_storage,
        IIdGenerator& id_generator,
        IUtcClock& clock,
        IMonotonicClock& monotonic_clock
    ) noexcept;

    ~ManagedFileService();

    ManagedFileService(const ManagedFileService&) = delete;
    ManagedFileService& operator=(const ManagedFileService&) = delete;

    [[nodiscard]] domain::ManagedFile register_managed_copy(
        const RegisterManagedCopyRequest& request
    );
    [[nodiscard]] std::vector<domain::ManagedFile> list();
    [[nodiscard]] std::optional<domain::ManagedFile> find_by_id(std::string_view id);

    [[nodiscard]] ManagedFileUploadSession begin_upload(
        const BeginManagedFileUploadRequest& request
    );
    [[nodiscard]] ManagedFileUploadSession append_upload(
        std::string_view upload_id,
        std::uint64_t expected_offset,
        std::string_view bytes
    );
    [[nodiscard]] domain::ManagedFile complete_upload(std::string_view upload_id);
    [[nodiscard]] bool cancel_upload(std::string_view upload_id) noexcept;

private:
    struct UploadState final {
        ManagedFileUploadSession session;
        std::chrono::steady_clock::time_point last_activity;
        bool finalizing{false};
    };

    void expire_inactive_uploads_locked(
        std::chrono::steady_clock::time_point now
    ) noexcept;

    [[nodiscard]] domain::ManagedFile persist_import(
        std::string file_type,
        std::unique_ptr<IInputFileImportTransaction> import,
        std::string managed_file_id,
        std::string timestamp
    );

    IManagedFileRepository& repository_;
    IInputFileStorage& input_storage_;
    IIdGenerator& id_generator_;
    IUtcClock& clock_;
    IMonotonicClock& monotonic_clock_;

    std::mutex uploads_mutex_;
    std::unordered_map<std::string, UploadState> uploads_;
};

}  // namespace biocore::application
