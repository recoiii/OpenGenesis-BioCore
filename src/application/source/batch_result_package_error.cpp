#include "biocore/application/batch_result_package_error.hpp"

namespace biocore::application {

std::string_view to_string(const BatchResultPackageErrorCode code) noexcept {
    switch (code) {
        case BatchResultPackageErrorCode::artifact_missing:
            return "artifact_missing";
        case BatchResultPackageErrorCode::artifact_identity_mismatch:
            return "artifact_identity_mismatch";
        case BatchResultPackageErrorCode::artifact_integrity_error:
            return "artifact_integrity_error";
    }
    return "artifact_integrity_error";
}

BatchResultPackageError::BatchResultPackageError(
    const BatchResultPackageErrorCode code,
    const char* message
) : std::runtime_error{message}, code_{code} {}

BatchResultPackageErrorCode BatchResultPackageError::code() const noexcept {
    return code_;
}

}  // namespace biocore::application
