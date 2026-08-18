#include "biocore/application/managed_file_service_error.hpp"

#include <utility>

namespace biocore::application {

ManagedFileServiceError::ManagedFileServiceError(
    const ManagedFileServiceErrorCode code,
    std::string message
)
    : std::runtime_error{std::move(message)}, code_{code} {}

ManagedFileServiceErrorCode ManagedFileServiceError::code() const noexcept { return code_; }

}  // namespace biocore::application
