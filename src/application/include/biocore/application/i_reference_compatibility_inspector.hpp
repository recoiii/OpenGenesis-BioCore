#pragma once

#include <string>
#include <string_view>

#include "biocore/domain/managed_file.hpp"

namespace biocore::application {

enum class ReferenceCompatibilityStatus {
    not_declared_by_format,
    verified,
    mismatch,
    evidence_unavailable
};

[[nodiscard]] constexpr std::string_view to_string(
    const ReferenceCompatibilityStatus status
) noexcept {
    switch (status) {
        case ReferenceCompatibilityStatus::not_declared_by_format:
            return "not_declared_by_format";
        case ReferenceCompatibilityStatus::verified:
            return "verified";
        case ReferenceCompatibilityStatus::mismatch:
            return "mismatch";
        case ReferenceCompatibilityStatus::evidence_unavailable:
            return "evidence_unavailable";
    }
    return "evidence_unavailable";
}

struct ReferenceCompatibilityResult final {
    ReferenceCompatibilityStatus status{ReferenceCompatibilityStatus::evidence_unavailable};
    std::string detail;
};

class IReferenceCompatibilityInspector {
public:
    virtual ~IReferenceCompatibilityInspector() = default;

    [[nodiscard]] virtual ReferenceCompatibilityResult inspect(
        const domain::ManagedFile& input,
        const domain::ManagedFile& reference
    ) const = 0;
};

}  // namespace biocore::application
