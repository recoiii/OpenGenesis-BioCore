#include "biocore/application/managed_file_service.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "biocore/application/i_id_generator.hpp"
#include "biocore/application/i_input_file_storage.hpp"
#include "biocore/application/i_managed_file_repository.hpp"
#include "biocore/application/i_monotonic_clock.hpp"
#include "biocore/application/i_utc_clock.hpp"
#include "biocore/application/managed_file_service_error.hpp"
#include "biocore/domain/storage_mode.hpp"

namespace biocore::application {
namespace {

[[nodiscard]] bool is_blank(const std::string_view value) {
    return value.empty() || std::ranges::all_of(value, [](const char character) {
               return std::isspace(static_cast<unsigned char>(character)) != 0;
           });
}

void validate_file_type(const std::string_view file_type) {
    if (is_blank(file_type) || file_type.find('\0') != std::string_view::npos ||
        file_type.size() > domain::ManagedFile::maximum_file_type_length) {
        throw std::invalid_argument("Input file type is invalid");
    }
}

void validate_request(const RegisterManagedCopyRequest& request) {
    if (is_blank(request.source_path) ||
        request.source_path.find('\0') != std::string::npos) {
        throw std::invalid_argument(
            "Input source path must not be blank or contain NUL characters"
        );
    }
    validate_file_type(request.file_type);
}

void validate_upload_display_name(const std::string_view value) {
    if (is_blank(value) || value == "." || value == ".." ||
        value.size() > domain::ManagedFile::maximum_display_name_length ||
        value.find('\0') != std::string_view::npos ||
        value.ends_with(' ') || value.ends_with('.')) {
        throw std::invalid_argument("Upload display name is invalid");
    }

    for (const char raw : value) {
        const auto character = static_cast<unsigned char>(raw);
        if (character < 0x20U || character == 0x7fU ||
            raw == '/' || raw == '\\' || raw == ':' || raw == '*' ||
            raw == '?' || raw == '"' || raw == '<' || raw == '>' || raw == '|') {
            throw std::invalid_argument(
                "Upload display name contains an unsafe filename character"
            );
        }
    }
}

}  // namespace

ManagedFileService::ManagedFileService(
    IManagedFileRepository& repository,
    IInputFileStorage& input_storage,
    IIdGenerator& id_generator,
    IUtcClock& clock,
    IMonotonicClock& monotonic_clock
) noexcept
    : repository_{repository},
      input_storage_{input_storage},
      id_generator_{id_generator},
      clock_{clock},
      monotonic_clock_{monotonic_clock} {}

ManagedFileService::~ManagedFileService() {
    std::lock_guard lock{uploads_mutex_};
    for (const auto& [upload_id, state] : uploads_) {
        static_cast<void>(state);
        input_storage_.discard_browser_upload(upload_id);
    }
}

void ManagedFileService::expire_inactive_uploads_locked(
    const std::chrono::steady_clock::time_point now
) noexcept {
    for (auto iterator = uploads_.begin(); iterator != uploads_.end();) {
        UploadState& state = iterator->second;
        if (!state.finalizing &&
            now - state.last_activity >= upload_inactivity_timeout) {
            input_storage_.discard_browser_upload(iterator->first);
            iterator = uploads_.erase(iterator);
            continue;
        }
        ++iterator;
    }
}

domain::ManagedFile ManagedFileService::persist_import(
    std::string file_type,
    std::unique_ptr<IInputFileImportTransaction> import,
    std::string managed_file_id,
    std::string timestamp
) {
    const PreparedManagedCopy& prepared = import->prepared_file();

    domain::ManagedFile file{
        std::move(managed_file_id),
        prepared.display_name,
        domain::StorageMode::managed_copy,
        prepared.original_path,
        prepared.managed_path,
        prepared.relative_project_path,
        std::move(file_type),
        prepared.size_bytes,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        timestamp,
        timestamp,
    };

    if (!repository_.add(file)) {
        throw ManagedFileServiceError{
            ManagedFileServiceErrorCode::identifier_generation_exhausted,
            "Managed file persistence conflicted with an existing identifier",
        };
    }

    import->commit();
    return file;
}

domain::ManagedFile ManagedFileService::register_managed_copy(
    const RegisterManagedCopyRequest& request
) {
    validate_request(request);
    const std::string timestamp = clock_.now_utc_iso8601();

    for (int attempt = 0; attempt < maximum_identifier_attempts; ++attempt) {
        const std::string id = id_generator_.generate();
        if (repository_.find_by_id(id).has_value()) {
            continue;
        }

        auto import = input_storage_.prepare_managed_copy(request.source_path, id);
        try {
            return persist_import(request.file_type, std::move(import), id, timestamp);
        } catch (const ManagedFileServiceError& error) {
            if (error.code() != ManagedFileServiceErrorCode::identifier_generation_exhausted) {
                throw;
            }
        }
    }

    throw ManagedFileServiceError{
        ManagedFileServiceErrorCode::identifier_generation_exhausted,
        "Unable to generate a unique managed file identifier",
    };
}

std::vector<domain::ManagedFile> ManagedFileService::list() {
    return repository_.list();
}

std::optional<domain::ManagedFile> ManagedFileService::find_by_id(
    const std::string_view id
) {
    return repository_.find_by_id(id);
}

ManagedFileUploadSession ManagedFileService::begin_upload(
    const BeginManagedFileUploadRequest& request
) {
    validate_upload_display_name(request.display_name);
    validate_file_type(request.file_type);
    if (request.size_bytes > maximum_upload_file_bytes ||
        request.size_bytes > static_cast<std::uint64_t>(
            std::numeric_limits<std::int64_t>::max()
        )) {
        throw ManagedFileServiceError{
            ManagedFileServiceErrorCode::upload_size_exceeded,
            "Upload exceeds the supported file-size limit",
        };
    }

    const auto now = monotonic_clock_.now();
    std::lock_guard lock{uploads_mutex_};
    expire_inactive_uploads_locked(now);
    if (uploads_.size() >= maximum_active_uploads) {
        throw ManagedFileServiceError{
            ManagedFileServiceErrorCode::upload_session_limit,
            "Too many browser file uploads are active",
        };
    }

    for (int attempt = 0; attempt < maximum_identifier_attempts; ++attempt) {
        const std::string upload_id = id_generator_.generate();
        if (uploads_.contains(upload_id)) {
            continue;
        }
        if (!input_storage_.begin_browser_upload(upload_id, request.display_name)) {
            continue;
        }

        ManagedFileUploadSession session{
            .upload_id = upload_id,
            .display_name = request.display_name,
            .file_type = request.file_type,
            .expected_size_bytes = request.size_bytes,
            .received_size_bytes = 0U,
        };
        uploads_.emplace(
            upload_id,
            UploadState{
                .session = session,
                .last_activity = now,
                .finalizing = false,
            }
        );
        return session;
    }

    throw ManagedFileServiceError{
        ManagedFileServiceErrorCode::identifier_generation_exhausted,
        "Unable to generate a unique browser upload identifier",
    };
}

ManagedFileUploadSession ManagedFileService::append_upload(
    const std::string_view upload_id,
    const std::uint64_t expected_offset,
    const std::string_view bytes
) {
    if (bytes.empty() || bytes.size() > maximum_upload_chunk_bytes) {
        throw std::invalid_argument("Upload chunk size is invalid");
    }

    const auto now = monotonic_clock_.now();
    std::lock_guard lock{uploads_mutex_};
    expire_inactive_uploads_locked(now);
    const auto iterator = uploads_.find(std::string{upload_id});
    if (iterator == uploads_.end() || iterator->second.finalizing) {
        throw ManagedFileServiceError{
            ManagedFileServiceErrorCode::upload_not_found,
            "Browser upload session was not found",
        };
    }

    auto& session = iterator->second.session;
    if (expected_offset != session.received_size_bytes) {
        throw ManagedFileServiceError{
            ManagedFileServiceErrorCode::upload_offset_mismatch,
            "Upload chunk offset does not match the next expected byte",
        };
    }

    const std::uint64_t chunk_size = static_cast<std::uint64_t>(bytes.size());
    if (chunk_size > session.expected_size_bytes - session.received_size_bytes) {
        throw ManagedFileServiceError{
            ManagedFileServiceErrorCode::upload_size_exceeded,
            "Upload chunk exceeds the declared file size",
        };
    }

    const std::uint64_t stored_size =
        input_storage_.append_browser_upload(upload_id, expected_offset, bytes);
    if (stored_size != session.received_size_bytes + chunk_size) {
        throw std::runtime_error(
            "Browser upload staging size diverged from Application state"
        );
    }

    session.received_size_bytes = stored_size;
    iterator->second.last_activity = now;
    return session;
}

domain::ManagedFile ManagedFileService::complete_upload(
    const std::string_view upload_id
) {
    const auto now = monotonic_clock_.now();
    std::lock_guard lock{uploads_mutex_};
    expire_inactive_uploads_locked(now);
    const auto iterator = uploads_.find(std::string{upload_id});
    if (iterator == uploads_.end() || iterator->second.finalizing) {
        throw ManagedFileServiceError{
            ManagedFileServiceErrorCode::upload_not_found,
            "Browser upload session was not found",
        };
    }

    auto& state = iterator->second;
    if (state.session.received_size_bytes != state.session.expected_size_bytes) {
        throw ManagedFileServiceError{
            ManagedFileServiceErrorCode::upload_incomplete,
            "Browser upload is incomplete",
        };
    }
    state.finalizing = true;
    const std::string timestamp = clock_.now_utc_iso8601();

    try {
        for (int attempt = 0; attempt < maximum_identifier_attempts; ++attempt) {
            const std::string id = id_generator_.generate();
            if (repository_.find_by_id(id).has_value()) {
                continue;
            }

            auto import = input_storage_.prepare_browser_upload_commit(upload_id, id);
            const std::int64_t staged_size = import->prepared_file().size_bytes;
            if (staged_size < 0 ||
                static_cast<std::uint64_t>(staged_size) !=
                    state.session.expected_size_bytes) {
                throw ManagedFileServiceError{
                    ManagedFileServiceErrorCode::upload_staging_mismatch,
                    "Browser upload staging size changed before finalization",
                };
            }
            try {
                domain::ManagedFile file =
                    persist_import(state.session.file_type, std::move(import), id, timestamp);
                input_storage_.discard_browser_upload(upload_id);
                uploads_.erase(iterator);
                return file;
            } catch (const ManagedFileServiceError& error) {
                if (error.code() !=
                    ManagedFileServiceErrorCode::identifier_generation_exhausted) {
                    throw;
                }
            }
        }
    } catch (...) {
        state.finalizing = false;
        state.last_activity = now;
        throw;
    }

    state.finalizing = false;
    state.last_activity = now;
    throw ManagedFileServiceError{
        ManagedFileServiceErrorCode::identifier_generation_exhausted,
        "Unable to generate a unique managed file identifier",
    };
}

bool ManagedFileService::cancel_upload(const std::string_view upload_id) noexcept {
    std::lock_guard lock{uploads_mutex_};
    const auto iterator = uploads_.find(std::string{upload_id});
    if (iterator == uploads_.end() || iterator->second.finalizing) {
        return false;
    }
    input_storage_.discard_browser_upload(upload_id);
    uploads_.erase(iterator);
    return true;
}

}  // namespace biocore::application
