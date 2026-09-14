#pragma once

#include "biocore/domain/contig_table.hpp"

#include <cstdint>
#include <istream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace biocore::domain {

class ReferenceGenome final {
public:
    ReferenceGenome() = default;

    [[nodiscard]] static ReferenceGenome from_fasta(
        std::istream& input,
        ReferenceAssembly assembly = ReferenceAssembly::custom
    );

    [[nodiscard]] const ContigTable& contigs() const noexcept { return contigs_; }
    [[nodiscard]] std::optional<std::string_view> sequence(ContigId id) const noexcept;
    [[nodiscard]] std::optional<char> base(ContigId id, std::uint64_t position) const noexcept;
    [[nodiscard]] std::optional<std::string_view> slice(
        ContigId id,
        std::uint64_t start,
        std::uint64_t length
    ) const noexcept;

private:
    ContigTable contigs_;
    std::vector<std::string> sequences_;
};

}  // namespace biocore::domain
