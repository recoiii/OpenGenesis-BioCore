#include "biocore/domain/managed_file.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <utility>

namespace biocore::domain {
namespace {

[[nodiscard]] bool is_blank(const std::string_view value) {
    return value.empty() || std::ranges::all_of(value, [](const char character) {
               return std::isspace(static_cast<unsigned char>(character)) != 0;
           });
}

void require_text(
    const std::string_view value,
    const char* const field_name,
    const std::size_t maximum_length
) {
    if (is_blank(value)) {
        throw std::invalid_argument(std::string{field_name} + " must not be blank");
    }
    if (value.find('\0') != std::string_view::npos) {
        throw std::invalid_argument(std::string{field_name} + " must not contain NUL characters");
    }
    if (value.size() > maximum_length) {
        throw std::invalid_argument(std::string{field_name} + " exceeds the maximum length");
    }
}

void require_optional_text(
    const std::optional<std::string>& value,
    const char* const field_name,
    const std::size_t maximum_length
) {
    if (value.has_value()) {
        require_text(*value, field_name, maximum_length);
    }
}

[[nodiscard]] bool is_lower_sha256(const std::string_view value) {
    return value.size() == 64U && std::ranges::all_of(value, [](const char character) {
               return (character >= '0' && character <= '9') ||
                      (character >= 'a' && character <= 'f');
           });
}

}  // namespace

ManagedFile::ManagedFile(
    std::string id,
    std::string display_name,
    const StorageMode storage_mode,
    std::optional<std::string> original_path,
    std::optional<std::string> managed_path,
    std::optional<std::string> relative_project_path,
    std::string file_type,
    const std::int64_t size_bytes,
    std::optional<std::string> modified_at_utc,
    std::optional<std::string> checksum_algorithm,
    std::optional<std::string> checksum_value,
    std::string created_at_utc,
    std::string updated_at_utc
)
    : id_{std::move(id)},
      display_name_{std::move(display_name)},
      storage_mode_{storage_mode},
      original_path_{std::move(original_path)},
      managed_path_{std::move(managed_path)},
      relative_project_path_{std::move(relative_project_path)},
      file_type_{std::move(file_type)},
      size_bytes_{size_bytes},
      modified_at_utc_{std::move(modified_at_utc)},
      checksum_algorithm_{std::move(checksum_algorithm)},
      checksum_value_{std::move(checksum_value)},
      created_at_utc_{std::move(created_at_utc)},
      updated_at_utc_{std::move(updated_at_utc)} {
    require_text(id_, "Managed file id", maximum_id_length);
    require_text(display_name_, "Managed file display name", maximum_display_name_length);
    require_optional_text(original_path_, "Managed file original path", maximum_metadata_length);
    require_optional_text(managed_path_, "Managed file managed path", maximum_metadata_length);
    require_optional_text(relative_project_path_, "Managed file relative project path", maximum_metadata_length);
    require_text(file_type_, "Managed file type", maximum_file_type_length);
    require_optional_text(modified_at_utc_, "Managed file modification timestamp", maximum_metadata_length);
    require_optional_text(checksum_algorithm_, "Managed file checksum algorithm", maximum_metadata_length);
    require_optional_text(checksum_value_, "Managed file checksum value", maximum_metadata_length);
    require_text(created_at_utc_, "Managed file creation timestamp", maximum_metadata_length);
    require_text(updated_at_utc_, "Managed file update timestamp", maximum_metadata_length);

    if (size_bytes_ < 0) {
        throw std::invalid_argument("Managed file size must not be negative");
    }
    if (!original_path_.has_value() && !managed_path_.has_value() &&
        !relative_project_path_.has_value()) {
        throw std::invalid_argument("Managed file must reference at least one path");
    }
    if ((checksum_algorithm_.has_value() && !checksum_value_.has_value()) ||
        (!checksum_algorithm_.has_value() && checksum_value_.has_value())) {
        throw std::invalid_argument("Managed file checksum algorithm and value must be provided together");
    }
    if (storage_mode_ == StorageMode::managed_copy || storage_mode_ == StorageMode::managed_move) {
        if (!original_path_.has_value() || !managed_path_.has_value() ||
            !relative_project_path_.has_value()) {
            throw std::invalid_argument("Managed input files require original, managed, and relative paths");
        }
    }
    if (storage_mode_ == StorageMode::external_reference && !original_path_.has_value()) {
        throw std::invalid_argument("External references require an original path");
    }
    if (storage_mode_ == StorageMode::generated_output &&
        (!managed_path_.has_value() || !relative_project_path_.has_value())) {
        throw std::invalid_argument("Generated outputs require managed and relative project paths");
    }
    if (storage_mode_ == StorageMode::generated_output && checksum_algorithm_.has_value() &&
        (*checksum_algorithm_ != "sha256" || !is_lower_sha256(*checksum_value_))) {
        throw std::invalid_argument("Generated output checksum must be lowercase SHA-256");
    }
}

std::string_view ManagedFile::id() const noexcept { return id_; }
std::string_view ManagedFile::display_name() const noexcept { return display_name_; }
StorageMode ManagedFile::storage_mode() const noexcept { return storage_mode_; }
const std::optional<std::string>& ManagedFile::original_path() const noexcept { return original_path_; }
const std::optional<std::string>& ManagedFile::managed_path() const noexcept { return managed_path_; }
const std::optional<std::string>& ManagedFile::relative_project_path() const noexcept { return relative_project_path_; }
std::string_view ManagedFile::file_type() const noexcept { return file_type_; }
std::int64_t ManagedFile::size_bytes() const noexcept { return size_bytes_; }
const std::optional<std::string>& ManagedFile::modified_at_utc() const noexcept { return modified_at_utc_; }
const std::optional<std::string>& ManagedFile::checksum_algorithm() const noexcept { return checksum_algorithm_; }
const std::optional<std::string>& ManagedFile::checksum_value() const noexcept { return checksum_value_; }
std::string_view ManagedFile::created_at_utc() const noexcept { return created_at_utc_; }
std::string_view ManagedFile::updated_at_utc() const noexcept { return updated_at_utc_; }

}  // namespace biocore::domain
