#include "biocore/infrastructure/filesystem_input_file_storage.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace biocore::infrastructure {
namespace {

[[nodiscard]] std::filesystem::path path_from_utf8(const std::string_view value) {
#ifdef _WIN32
    std::u8string encoded;
    encoded.reserve(value.size());
    for (const char raw_character : value) {
        encoded.push_back(static_cast<char8_t>(static_cast<unsigned char>(raw_character)));
    }
    return std::filesystem::path{encoded};
#else
    return std::filesystem::path{value};
#endif
}

[[nodiscard]] std::string path_to_utf8(const std::filesystem::path& path) {
    const std::u8string encoded = path.generic_u8string();
    std::string result;
    result.reserve(encoded.size());
    for (const char8_t character : encoded) {
        result.push_back(static_cast<char>(character));
    }
    return result;
}

[[nodiscard]] bool is_blank(const std::string_view value) {
    return value.empty() || std::ranges::all_of(value, [](const char character) {
               return std::isspace(static_cast<unsigned char>(character)) != 0;
           });
}

void require_safe_identifier(const std::string_view value) {
    if (is_blank(value) || value == "." || value == ".." || value.size() > 128U ||
        !std::ranges::all_of(value, [](const char character) {
            const auto byte = static_cast<unsigned char>(character);
            return std::isalnum(byte) != 0 || character == '-' || character == '_';
        })) {
        throw std::invalid_argument("Managed file identifier is not a safe path segment");
    }
}

[[nodiscard]] std::filesystem::path require_safe_display_name(
    const std::string_view value
) {
    if (is_blank(value) || value.find('\0') != std::string_view::npos) {
        throw std::invalid_argument("Upload display name is invalid");
    }
    const std::filesystem::path name = path_from_utf8(value);
    if (name.empty() || name == "." || name == ".." ||
        name.is_absolute() || name.has_parent_path() || name.filename() != name) {
        throw std::invalid_argument("Upload display name must be one filename");
    }
    return name;
}

void rollback_paths(std::vector<std::filesystem::path>& paths) noexcept {
    for (auto iterator = paths.rbegin(); iterator != paths.rend(); ++iterator) {
        std::error_code ignored;
        (void)std::filesystem::remove(*iterator, ignored);
    }
    paths.clear();
}

class FilesystemInputImportTransaction final : public application::IInputFileImportTransaction {
public:
    FilesystemInputImportTransaction(
        application::PreparedManagedCopy prepared,
        std::vector<std::filesystem::path> created_paths
    )
        : prepared_{std::move(prepared)}, created_paths_{std::move(created_paths)} {}

    ~FilesystemInputImportTransaction() override {
        if (!committed_) {
            rollback_paths(created_paths_);
        }
    }

    [[nodiscard]] const application::PreparedManagedCopy& prepared_file() const noexcept override {
        return prepared_;
    }

    void commit() noexcept override {
        committed_ = true;
        created_paths_.clear();
    }

private:
    application::PreparedManagedCopy prepared_;
    std::vector<std::filesystem::path> created_paths_;
    bool committed_{false};
};

class BrowserUploadImportTransaction final : public application::IInputFileImportTransaction {
public:
    BrowserUploadImportTransaction(
        application::PreparedManagedCopy prepared,
        std::filesystem::path staged_path,
        std::filesystem::path final_path,
        std::filesystem::path destination_directory,
        std::filesystem::path upload_directory
    )
        : prepared_{std::move(prepared)},
          staged_path_{std::move(staged_path)},
          final_path_{std::move(final_path)},
          destination_directory_{std::move(destination_directory)},
          upload_directory_{std::move(upload_directory)} {}

    ~BrowserUploadImportTransaction() override {
        if (committed_) return;

        std::error_code status_error;
        const auto final_status = std::filesystem::symlink_status(final_path_, status_error);
        if (!status_error && final_status.type() == std::filesystem::file_type::regular) {
            std::error_code rename_error;
            std::filesystem::rename(final_path_, staged_path_, rename_error);
        }
        std::error_code ignored;
        (void)std::filesystem::remove(destination_directory_, ignored);
    }

    [[nodiscard]] const application::PreparedManagedCopy& prepared_file() const noexcept override {
        return prepared_;
    }

    void commit() noexcept override {
        committed_ = true;
        std::error_code ignored;
        (void)std::filesystem::remove(upload_directory_, ignored);
    }

private:
    application::PreparedManagedCopy prepared_;
    std::filesystem::path staged_path_;
    std::filesystem::path final_path_;
    std::filesystem::path destination_directory_;
    std::filesystem::path upload_directory_;
    bool committed_{false};
};

[[nodiscard]] std::filesystem::path canonical_regular_file(
    const std::string_view source_path
) {
    if (is_blank(source_path) || source_path.find('\0') != std::string_view::npos) {
        throw std::invalid_argument(
            "Input source path must not be blank or contain NUL characters"
        );
    }

    const std::filesystem::path requested = path_from_utf8(source_path);
    std::error_code status_error;
    const std::filesystem::file_status requested_status =
        std::filesystem::symlink_status(requested, status_error);
    if (status_error || requested_status.type() != std::filesystem::file_type::regular) {
        throw std::invalid_argument(
            "Input source must be an existing non-symlink regular file"
        );
    }

    std::error_code canonical_error;
    const std::filesystem::path canonical =
        std::filesystem::canonical(requested, canonical_error);
    if (canonical_error) {
        throw std::runtime_error(
            "Unable to canonicalize input source: " + canonical_error.message()
        );
    }
    return canonical;
}

[[nodiscard]] std::filesystem::path require_direct_child_directory(
    const std::filesystem::path& root,
    const std::string_view identifier
) {
    require_safe_identifier(identifier);
    const std::filesystem::path requested = root / path_from_utf8(identifier);
    std::error_code status_error;
    const auto status = std::filesystem::symlink_status(requested, status_error);
    if (status_error || status.type() != std::filesystem::file_type::directory) {
        throw std::invalid_argument("Browser upload session was not found");
    }
    std::error_code canonical_error;
    const auto canonical = std::filesystem::canonical(requested, canonical_error);
    if (canonical_error || canonical.parent_path() != root || canonical != requested) {
        throw std::invalid_argument("Browser upload session path is unsafe");
    }
    return canonical;
}

[[nodiscard]] std::filesystem::path require_single_staged_file(
    const std::filesystem::path& upload_directory
) {
    std::filesystem::path staged;
    std::size_t count = 0U;
    std::error_code iteration_error;
    for (std::filesystem::directory_iterator iterator{upload_directory, iteration_error}, end;
         !iteration_error && iterator != end; iterator.increment(iteration_error)) {
        ++count;
        std::error_code status_error;
        const auto status = std::filesystem::symlink_status(iterator->path(), status_error);
        if (status_error || status.type() != std::filesystem::file_type::regular) {
            throw std::runtime_error("Browser upload staging contains an unsafe entry");
        }
        staged = iterator->path();
    }
    if (iteration_error || count != 1U) {
        throw std::runtime_error("Browser upload staging does not contain exactly one file");
    }
    std::error_code canonical_error;
    const auto canonical = std::filesystem::canonical(staged, canonical_error);
    if (canonical_error || canonical.parent_path() != upload_directory || canonical != staged) {
        throw std::runtime_error("Browser upload staging file is unsafe");
    }
    return canonical;
}

void cleanup_stale_uploads(const std::filesystem::path& root) noexcept {
    std::error_code iteration_error;
    for (std::filesystem::directory_iterator iterator{root, iteration_error}, end;
         !iteration_error && iterator != end; iterator.increment(iteration_error)) {
        std::error_code status_error;
        const auto status = std::filesystem::symlink_status(iterator->path(), status_error);
        if (status_error || status.type() != std::filesystem::file_type::directory) continue;

        std::error_code child_error;
        for (std::filesystem::directory_iterator child{iterator->path(), child_error}, child_end;
             !child_error && child != child_end; child.increment(child_error)) {
            std::error_code child_status_error;
            const auto child_status = std::filesystem::symlink_status(
                child->path(), child_status_error
            );
            if (!child_status_error &&
                child_status.type() == std::filesystem::file_type::regular) {
                std::error_code ignored;
                (void)std::filesystem::remove(child->path(), ignored);
            }
        }
        std::error_code ignored;
        (void)std::filesystem::remove(iterator->path(), ignored);
    }
}

}  // namespace

FilesystemInputFileStorage::FilesystemInputFileStorage(
    const std::string_view canonical_project_root
) {
    if (is_blank(canonical_project_root) ||
        canonical_project_root.find('\0') != std::string_view::npos) {
        throw std::invalid_argument(
            "Project root must not be blank or contain NUL characters"
        );
    }

    const std::filesystem::path requested = path_from_utf8(canonical_project_root);
    std::error_code canonical_error;
    project_root_ = std::filesystem::canonical(requested, canonical_error);
    if (canonical_error || path_to_utf8(project_root_) != canonical_project_root) {
        throw std::invalid_argument("Input storage requires a canonical project root");
    }

    std::error_code root_error;
    const auto root_status = std::filesystem::symlink_status(project_root_, root_error);
    if (root_error || root_status.type() != std::filesystem::file_type::directory) {
        throw std::invalid_argument(
            "Project root must be an existing non-symlink directory"
        );
    }

    inputs_directory_ = project_root_ / "inputs";
    std::error_code inputs_error;
    const auto inputs_status =
        std::filesystem::symlink_status(inputs_directory_, inputs_error);
    if (inputs_error || inputs_status.type() != std::filesystem::file_type::directory) {
        throw std::invalid_argument(
            "Project inputs entry must be an existing non-symlink directory"
        );
    }

    std::error_code inputs_canonical_error;
    const std::filesystem::path canonical_inputs =
        std::filesystem::canonical(inputs_directory_, inputs_canonical_error);
    if (inputs_canonical_error || canonical_inputs != inputs_directory_) {
        throw std::invalid_argument(
            "Project inputs directory must not redirect through a symlink"
        );
    }

    const std::filesystem::path runtime_directory = project_root_ / ".biocore" / "runtime";
    browser_uploads_directory_ = runtime_directory / "browser-uploads";
    std::error_code runtime_error;
    const auto runtime_status =
        std::filesystem::symlink_status(runtime_directory, runtime_error);
    if (runtime_error == std::errc::no_such_file_or_directory) {
        return;
    }
    if (runtime_error || runtime_status.type() != std::filesystem::file_type::directory) {
        throw std::invalid_argument(
            "Project runtime entry must be an existing non-symlink directory"
        );
    }
    std::error_code runtime_canonical_error;
    const auto canonical_runtime =
        std::filesystem::canonical(runtime_directory, runtime_canonical_error);
    if (runtime_canonical_error || canonical_runtime != runtime_directory) {
        throw std::invalid_argument(
            "Project runtime directory must not redirect through a symlink"
        );
    }

    std::error_code upload_status_error;
    const auto upload_status =
        std::filesystem::symlink_status(browser_uploads_directory_, upload_status_error);
    if (upload_status_error &&
        upload_status_error != std::errc::no_such_file_or_directory) {
        throw std::runtime_error(
            "Unable to inspect browser upload staging: " +
            upload_status_error.message()
        );
    }
    if (!upload_status_error) {
        if (upload_status.type() != std::filesystem::file_type::directory) {
            throw std::invalid_argument(
                "Browser upload staging must be a non-symlink directory"
            );
        }
        std::error_code upload_canonical_error;
        const auto canonical_uploads = std::filesystem::canonical(
            browser_uploads_directory_, upload_canonical_error
        );
        if (upload_canonical_error || canonical_uploads != browser_uploads_directory_) {
            throw std::invalid_argument(
                "Browser upload staging must not redirect through a symlink"
            );
        }
    } else {
        std::error_code create_error;
        if (!std::filesystem::create_directory(browser_uploads_directory_, create_error) ||
            create_error) {
            throw std::runtime_error(
                "Unable to create browser upload staging: " + create_error.message()
            );
        }
    }
    cleanup_stale_uploads(browser_uploads_directory_);
}

std::unique_ptr<application::IInputFileImportTransaction>
FilesystemInputFileStorage::prepare_managed_copy(
    const std::string_view source_path,
    const std::string_view managed_file_id
) {
    require_safe_identifier(managed_file_id);
    const std::filesystem::path source = canonical_regular_file(source_path);

    const std::filesystem::path destination_directory =
        inputs_directory_ / path_from_utf8(managed_file_id);
    const std::filesystem::path display_name = source.filename();
    if (display_name.empty() || display_name == "." || display_name == "..") {
        throw std::invalid_argument("Input source has an invalid display name");
    }

    std::vector<std::filesystem::path> created_paths;
    created_paths.reserve(2U);

    try {
        std::error_code create_error;
        const bool created =
            std::filesystem::create_directory(destination_directory, create_error);
        if (create_error) {
            throw std::runtime_error(
                "Unable to create managed input directory: " + create_error.message()
            );
        }
        if (!created) {
            throw std::runtime_error("Managed input directory already exists");
        }
        created_paths.push_back(destination_directory);

        const std::filesystem::path final_path = destination_directory / display_name;
        const std::filesystem::path temporary_path = destination_directory /
            (display_name == ".biocore-import.0.tmp"
                 ? ".biocore-import.1.tmp"
                 : ".biocore-import.0.tmp");

        std::error_code size_error;
        const std::uintmax_t size_before =
            std::filesystem::file_size(source, size_error);
        if (size_error ||
            size_before > static_cast<std::uintmax_t>(
                std::numeric_limits<std::int64_t>::max()
            )) {
            throw std::runtime_error(
                "Unable to determine a supported input file size"
            );
        }

        std::error_code timestamp_error;
        const auto modified_before =
            std::filesystem::last_write_time(source, timestamp_error);
        if (timestamp_error) {
            throw std::runtime_error(
                "Unable to inspect input source modification time"
            );
        }

        created_paths.push_back(temporary_path);
        std::error_code copy_error;
        const bool copied = std::filesystem::copy_file(
            source,
            temporary_path,
            std::filesystem::copy_options::none,
            copy_error
        );
        if (copy_error || !copied) {
            throw std::runtime_error(
                "Unable to copy input file: " + copy_error.message()
            );
        }

        size_error.clear();
        const std::uintmax_t size_after =
            std::filesystem::file_size(source, size_error);
        if (size_error || size_after != size_before) {
            throw std::runtime_error("Input source changed while it was being copied");
        }
        size_error.clear();
        const std::uintmax_t copied_size =
            std::filesystem::file_size(temporary_path, size_error);
        if (size_error || copied_size != size_before) {
            throw std::runtime_error(
                "Managed input copy size does not match the source"
            );
        }
        timestamp_error.clear();
        const auto modified_after =
            std::filesystem::last_write_time(source, timestamp_error);
        if (timestamp_error || modified_after != modified_before) {
            throw std::runtime_error("Input source changed while it was being copied");
        }

        std::error_code rename_error;
        std::filesystem::rename(temporary_path, final_path, rename_error);
        if (rename_error) {
            throw std::runtime_error(
                "Unable to publish managed input file: " + rename_error.message()
            );
        }
        created_paths.back() = final_path;

        const std::filesystem::path relative =
            std::filesystem::relative(final_path, project_root_);
        application::PreparedManagedCopy prepared{
            .display_name = path_to_utf8(display_name),
            .original_path = path_to_utf8(source),
            .managed_path = path_to_utf8(final_path),
            .relative_project_path = path_to_utf8(relative),
            .size_bytes = static_cast<std::int64_t>(size_before),
        };
        return std::make_unique<FilesystemInputImportTransaction>(
            std::move(prepared),
            std::move(created_paths)
        );
    } catch (...) {
        rollback_paths(created_paths);
        throw;
    }
}

bool FilesystemInputFileStorage::begin_browser_upload(
    const std::string_view upload_id,
    const std::string_view display_name
) {
    require_safe_identifier(upload_id);
    const std::filesystem::path name = require_safe_display_name(display_name);
    const std::filesystem::path upload_directory =
        browser_uploads_directory_ / path_from_utf8(upload_id);

    std::error_code create_error;
    const bool created =
        std::filesystem::create_directory(upload_directory, create_error);
    if (create_error) {
        throw std::runtime_error(
            "Unable to create browser upload session: " + create_error.message()
        );
    }
    if (!created) return false;

    const std::filesystem::path staged_path = upload_directory / name;
    std::ofstream output{staged_path, std::ios::binary | std::ios::trunc};
    if (!output) {
        std::error_code ignored;
        (void)std::filesystem::remove(upload_directory, ignored);
        throw std::runtime_error("Unable to create browser upload staging file");
    }
    output.flush();
    if (!output) {
        std::error_code ignored;
        (void)std::filesystem::remove(staged_path, ignored);
        (void)std::filesystem::remove(upload_directory, ignored);
        throw std::runtime_error("Unable to initialize browser upload staging file");
    }
    return true;
}

std::uint64_t FilesystemInputFileStorage::append_browser_upload(
    const std::string_view upload_id,
    const std::uint64_t expected_offset,
    const std::string_view bytes
) {
    const auto upload_directory =
        require_direct_child_directory(browser_uploads_directory_, upload_id);
    const auto staged_path = require_single_staged_file(upload_directory);

    std::error_code size_error;
    const std::uintmax_t size_before =
        std::filesystem::file_size(staged_path, size_error);
    if (size_error || size_before != expected_offset) {
        throw std::runtime_error("Browser upload staging offset mismatch");
    }
    if (bytes.size() > std::numeric_limits<std::uint64_t>::max() - expected_offset) {
        throw std::overflow_error("Browser upload staging size overflow");
    }

    std::ofstream output{staged_path, std::ios::binary | std::ios::app};
    if (!output) {
        throw std::runtime_error("Unable to open browser upload staging file");
    }
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.flush();
    if (!output) {
        throw std::runtime_error("Unable to append browser upload staging bytes");
    }

    size_error.clear();
    const std::uintmax_t size_after =
        std::filesystem::file_size(staged_path, size_error);
    const std::uint64_t expected_size =
        expected_offset + static_cast<std::uint64_t>(bytes.size());
    if (size_error || size_after != expected_size) {
        throw std::runtime_error("Browser upload staging size verification failed");
    }
    return expected_size;
}

std::unique_ptr<application::IInputFileImportTransaction>
FilesystemInputFileStorage::prepare_browser_upload_commit(
    const std::string_view upload_id,
    const std::string_view managed_file_id
) {
    require_safe_identifier(managed_file_id);
    const auto upload_directory =
        require_direct_child_directory(browser_uploads_directory_, upload_id);
    const auto staged_path = require_single_staged_file(upload_directory);

    std::error_code size_error;
    const std::uintmax_t size = std::filesystem::file_size(staged_path, size_error);
    if (size_error ||
        size > static_cast<std::uintmax_t>(std::numeric_limits<std::int64_t>::max())) {
        throw std::runtime_error("Browser upload has an unsupported final size");
    }

    const std::filesystem::path destination_directory =
        inputs_directory_ / path_from_utf8(managed_file_id);
    std::error_code create_error;
    const bool created =
        std::filesystem::create_directory(destination_directory, create_error);
    if (create_error) {
        throw std::runtime_error(
            "Unable to create managed upload directory: " + create_error.message()
        );
    }
    if (!created) {
        throw std::runtime_error("Managed upload directory already exists");
    }

    const std::filesystem::path final_path =
        destination_directory / staged_path.filename();
    std::error_code rename_error;
    std::filesystem::rename(staged_path, final_path, rename_error);
    if (rename_error) {
        std::error_code ignored;
        (void)std::filesystem::remove(destination_directory, ignored);
        throw std::runtime_error(
            "Unable to publish browser upload into managed inputs: " +
            rename_error.message()
        );
    }

    const std::filesystem::path relative =
        std::filesystem::relative(final_path, project_root_);
    application::PreparedManagedCopy prepared{
        .display_name = path_to_utf8(final_path.filename()),
        .original_path = path_to_utf8(staged_path),
        .managed_path = path_to_utf8(final_path),
        .relative_project_path = path_to_utf8(relative),
        .size_bytes = static_cast<std::int64_t>(size),
    };
    return std::make_unique<BrowserUploadImportTransaction>(
        std::move(prepared),
        staged_path,
        final_path,
        destination_directory,
        upload_directory
    );
}

void FilesystemInputFileStorage::discard_browser_upload(
    const std::string_view upload_id
) noexcept {
    try {
        require_safe_identifier(upload_id);
        const std::filesystem::path upload_directory =
            browser_uploads_directory_ / path_from_utf8(upload_id);
        std::error_code status_error;
        const auto status =
            std::filesystem::symlink_status(upload_directory, status_error);
        if (status_error || status.type() != std::filesystem::file_type::directory) {
            return;
        }

        std::error_code child_error;
        for (std::filesystem::directory_iterator child{upload_directory, child_error}, end;
             !child_error && child != end; child.increment(child_error)) {
            std::error_code child_status_error;
            const auto child_status = std::filesystem::symlink_status(
                child->path(), child_status_error
            );
            if (!child_status_error &&
                child_status.type() == std::filesystem::file_type::regular) {
                std::error_code ignored;
                (void)std::filesystem::remove(child->path(), ignored);
            }
        }
        std::error_code ignored;
        (void)std::filesystem::remove(upload_directory, ignored);
    } catch (...) {
    }
}

}  // namespace biocore::infrastructure
