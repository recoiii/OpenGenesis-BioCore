#pragma once

#include "biocore/domain/reference_genome.hpp"
#include "biocore/domain/variant_record.hpp"

#include <optional>
#include <string>

namespace biocore::domain {

[[nodiscard]] std::optional<std::string> validate_reference_match(
    const VariantRecord& record,
    const ReferenceGenome& reference
);

void normalize_variant(VariantRecord& record, const ReferenceGenome& reference);

}  // namespace biocore::domain
