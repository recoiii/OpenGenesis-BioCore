#pragma once

#include "biocore/application/i_reference_compatibility_inspector.hpp"

namespace biocore::infrastructure {

class FilesystemReferenceCompatibilityInspector final
    : public application::IReferenceCompatibilityInspector {
public:
    [[nodiscard]] application::ReferenceCompatibilityResult inspect(
        const domain::ManagedFile& input,
        const domain::ManagedFile& reference
    ) const override;
};

}  // namespace biocore::infrastructure
