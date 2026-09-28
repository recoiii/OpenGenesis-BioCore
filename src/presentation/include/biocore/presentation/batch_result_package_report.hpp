#pragma once

#include <string>

#include "biocore/application/batch_result_package.hpp"

namespace biocore::presentation {

[[nodiscard]] std::string render_batch_result_package_manifest_json(
    const application::BatchResultPackage& package
);

[[nodiscard]] std::string render_batch_result_package_html(
    const application::BatchResultPackage& package
);

}  // namespace biocore::presentation
