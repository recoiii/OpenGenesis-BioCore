#include "biocore/domain/variant_types.hpp"

namespace biocore::domain {

std::string_view to_string(const VariantType type) noexcept {
    switch (type) {
        case VariantType::snv:
            return "SNV";
        case VariantType::insertion:
            return "INSERTION";
        case VariantType::deletion:
            return "DELETION";
        case VariantType::mnv:
            return "MNV";
        case VariantType::delins_complex:
            return "DELINS_COMPLEX";
        case VariantType::unknown:
            return "UNKNOWN";
    }
    return "UNKNOWN";
}

std::optional<VariantType> variant_type_from_string(const std::string_view value) noexcept {
    if (value == "SNV") {
        return VariantType::snv;
    }
    if (value == "INSERTION") {
        return VariantType::insertion;
    }
    if (value == "DELETION") {
        return VariantType::deletion;
    }
    if (value == "MNV") {
        return VariantType::mnv;
    }
    if (value == "DELINS_COMPLEX") {
        return VariantType::delins_complex;
    }
    if (value == "UNKNOWN") {
        return VariantType::unknown;
    }
    return std::nullopt;
}

VariantType classify_variant(
    const std::string_view reference,
    const std::string_view alternate,
    const bool symbolic
) noexcept {
    if (symbolic || reference.empty() || alternate.empty() || reference == alternate) {
        return VariantType::unknown;
    }
    if (reference.size() == alternate.size()) {
        return reference.size() == 1U ? VariantType::snv : VariantType::mnv;
    }
    if (alternate.size() > reference.size() && alternate.starts_with(reference)) {
        return VariantType::insertion;
    }
    if (reference.size() > alternate.size() && reference.starts_with(alternate)) {
        return VariantType::deletion;
    }
    return VariantType::delins_complex;
}

}  // namespace biocore::domain
