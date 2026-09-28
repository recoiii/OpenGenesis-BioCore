#pragma once

#include <stdexcept>
#include <string_view>

namespace biocore::application {

enum class BatchResultPackageErrorCode {
    artifact_missing,
    artifact_identity_mismatch,
    artifact_integrity_error
};

[[nodiscard]] std::string_view to_string(BatchResultPackageErrorCode code) noexcept;

class BatchResultPackageError final : public std::runtime_error {
public:
    BatchResultPackageError(BatchResultPackageErrorCode code, const char* message);
    [[nodiscard]] BatchResultPackageErrorCode code() const noexcept;

private:
    BatchResultPackageErrorCode code_;
};

}  // namespace biocore::application
