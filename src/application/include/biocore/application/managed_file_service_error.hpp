#pragma once

#include <stdexcept>
#include <string>

namespace biocore::application {

enum class ManagedFileServiceErrorCode {
    identifier_generation_exhausted,
    upload_session_limit,
    upload_not_found,
    upload_offset_mismatch,
    upload_size_exceeded,
    upload_incomplete,
    upload_staging_mismatch
};

class ManagedFileServiceError final : public std::runtime_error {
public:
    ManagedFileServiceError(ManagedFileServiceErrorCode code, std::string message);

    [[nodiscard]] ManagedFileServiceErrorCode code() const noexcept;

private:
    ManagedFileServiceErrorCode code_;
};

}  // namespace biocore::application
