#pragma once

#include "biocore/application/variant_export.hpp"

#include <iosfwd>
#include <string>

namespace biocore::presentation {

void write_variant_export_tsv(
    std::ostream& output,
    const application::VariantExportPlan& plan
);
void write_variant_export_csv(
    std::ostream& output,
    const application::VariantExportPlan& plan
);
void write_variant_export_json(
    std::ostream& output,
    const application::VariantExportPlan& plan
);
void write_variant_export_vcf(
    std::ostream& output,
    const application::VariantExportPlan& plan
);

[[nodiscard]] std::string render_variant_export_tsv(
    const application::VariantExportPlan& plan
);
[[nodiscard]] std::string render_variant_export_csv(
    const application::VariantExportPlan& plan
);
[[nodiscard]] std::string render_variant_export_json(
    const application::VariantExportPlan& plan
);
[[nodiscard]] std::string render_variant_export_vcf(
    const application::VariantExportPlan& plan
);

}  // namespace biocore::presentation
