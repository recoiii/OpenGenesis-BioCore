#pragma once

#include "biocore/domain/contig_table.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace biocore::domain::reference_detail {

struct PrimaryContigAlias final {
    std::string_view canonical;
    std::string_view grch37_accession;
    std::string_view grch38_accession;
};

constexpr std::array<PrimaryContigAlias, 25U> primary_contigs{{
    {"1", "NC_000001.10", "NC_000001.11"},
    {"2", "NC_000002.11", "NC_000002.12"},
    {"3", "NC_000003.11", "NC_000003.12"},
    {"4", "NC_000004.11", "NC_000004.12"},
    {"5", "NC_000005.9", "NC_000005.10"},
    {"6", "NC_000006.11", "NC_000006.12"},
    {"7", "NC_000007.13", "NC_000007.14"},
    {"8", "NC_000008.10", "NC_000008.11"},
    {"9", "NC_000009.11", "NC_000009.12"},
    {"10", "NC_000010.10", "NC_000010.11"},
    {"11", "NC_000011.9", "NC_000011.10"},
    {"12", "NC_000012.11", "NC_000012.12"},
    {"13", "NC_000013.10", "NC_000013.11"},
    {"14", "NC_000014.8", "NC_000014.9"},
    {"15", "NC_000015.9", "NC_000015.10"},
    {"16", "NC_000016.9", "NC_000016.10"},
    {"17", "NC_000017.10", "NC_000017.11"},
    {"18", "NC_000018.9", "NC_000018.10"},
    {"19", "NC_000019.9", "NC_000019.10"},
    {"20", "NC_000020.10", "NC_000020.11"},
    {"21", "NC_000021.8", "NC_000021.9"},
    {"22", "NC_000022.10", "NC_000022.11"},
    {"X", "NC_000023.10", "NC_000023.11"},
    {"Y", "NC_000024.9", "NC_000024.10"},
    {"MT", "NC_012920.1", "NC_012920.1"},
}};
[[nodiscard]] bool matches_primary_alias(
    const PrimaryContigAlias& entry,
    const std::string_view name,
    const ReferenceAssembly assembly
) noexcept {
    if (name == entry.canonical) {
        return true;
    }
    if (entry.canonical == "MT") {
        if (name == "chrM" || name == "chrMT") {
            return true;
        }
    } else {
        const std::string chr = "chr" + std::string{entry.canonical};
        if (name == chr) {
            return true;
        }
    }
    if (assembly == ReferenceAssembly::grch37 && name == entry.grch37_accession) {
        return true;
    }
    if (assembly == ReferenceAssembly::grch38 && name == entry.grch38_accession) {
        return true;
    }
    return false;
}

[[nodiscard]] std::pair<std::string, std::vector<std::string>> canonical_and_aliases(
    const std::string& raw_name,
    const ReferenceAssembly assembly
) {
    if (assembly != ReferenceAssembly::grch37 && assembly != ReferenceAssembly::grch38) {
        return {raw_name, {}};
    }

    for (const auto& entry : primary_contigs) {
        if (!matches_primary_alias(entry, raw_name, assembly)) {
            continue;
        }
        std::vector<std::string> aliases;
        aliases.push_back(raw_name);
        aliases.push_back(std::string{entry.canonical});
        if (entry.canonical == "MT") {
            aliases.emplace_back("chrM");
            aliases.emplace_back("chrMT");
        } else {
            aliases.push_back("chr" + std::string{entry.canonical});
        }
        aliases.push_back(std::string{
            assembly == ReferenceAssembly::grch37 ? entry.grch37_accession : entry.grch38_accession
        });
        std::sort(aliases.begin(), aliases.end());
        aliases.erase(std::unique(aliases.begin(), aliases.end()), aliases.end());
        aliases.erase(
            std::remove(aliases.begin(), aliases.end(), std::string{entry.canonical}),
            aliases.end()
        );
        return {std::string{entry.canonical}, std::move(aliases)};
    }
    return {raw_name, {}};
}


}  // namespace biocore::domain::reference_detail
