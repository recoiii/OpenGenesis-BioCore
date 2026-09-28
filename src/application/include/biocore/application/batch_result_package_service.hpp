#pragma once

#include <string_view>

#include "biocore/application/batch_result_package.hpp"

namespace biocore::application {

class BatchResultsService;
class IArtifactContentAccess;
class IManagedFileRepository;
class IUtcClock;

class BatchResultPackageService final {
public:
    BatchResultPackageService(
        BatchResultsService& results,
        IManagedFileRepository& managed_files,
        IArtifactContentAccess& content_access,
        IUtcClock& clock
    ) noexcept;

    [[nodiscard]] BatchResultPackage build(std::string_view plan_id);

private:
    BatchResultsService& results_;
    IManagedFileRepository& managed_files_;
    IArtifactContentAccess& content_access_;
    IUtcClock& clock_;
};

}  // namespace biocore::application
