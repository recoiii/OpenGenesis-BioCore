#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "biocore/application/i_reference_compatibility_inspector.hpp"
#include "biocore/domain/project_sample_binding.hpp"

namespace biocore::application {

class IInputFileStorage;
class IManagedFileRepository;
class IProjectSampleBindingStore;
class IProjectSampleStore;

enum class SampleBindingIssueSeverity {
    blocker,
    warning
};

struct SampleBindingIssue final {
    SampleBindingIssueSeverity severity{SampleBindingIssueSeverity::blocker};
    std::string code;
    std::string message;
};

enum class SampleBindingReferenceStatus {
    not_requested,
    not_declared_by_format,
    verified,
    mismatch,
    evidence_unavailable
};

struct SampleBindingPreview final {
    domain::ProjectSampleBinding binding;
    SampleBindingReferenceStatus reference_status{SampleBindingReferenceStatus::not_requested};
    std::vector<SampleBindingIssue> issues;

    [[nodiscard]] bool valid() const noexcept;
};

class SampleBindingService final {
public:
    SampleBindingService(
        IProjectSampleStore& samples,
        IProjectSampleBindingStore& bindings,
        IManagedFileRepository& managed_files,
        const IInputFileStorage& input_storage,
        const IReferenceCompatibilityInspector& reference_inspector
    ) noexcept;

    [[nodiscard]] SampleBindingPreview preview(
        const domain::ProjectSampleBinding& binding
    );

    // Re-validates current file integrity/reference evidence before persistence.
    [[nodiscard]] SampleBindingPreview commit(
        const domain::ProjectSampleBinding& binding
    );

    [[nodiscard]] std::optional<domain::ProjectSampleBinding> find(
        std::string_view project_id,
        std::string_view sample_id
    );

private:
    IProjectSampleStore& samples_;
    IProjectSampleBindingStore& bindings_;
    IManagedFileRepository& managed_files_;
    const IInputFileStorage& input_storage_;
    const IReferenceCompatibilityInspector& reference_inspector_;
};

}  // namespace biocore::application
