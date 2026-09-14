#include "biocore/domain/reference_genome.hpp"
#include "reference_genome_aliases.hpp"

#include <cctype>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace biocore::domain {
namespace {

constexpr std::size_t maximum_reference_line_bytes = 64U * 1024U * 1024U;
constexpr std::uint64_t maximum_reference_bases = 10000000000ULL;
constexpr std::size_t maximum_contigs = 1000000U;
constexpr std::size_t maximum_contig_name_bytes = 1024U;

[[nodiscard]] bool read_bounded_line(std::istream& input, std::string& line) {
    line.clear();
    bool saw = false;
    for (;;) {
        const int next = input.get();
        if (next == std::char_traits<char>::eof()) {
            if (input.bad()) {
                throw std::runtime_error("unable to read reference FASTA");
            }
            return saw;
        }
        saw = true;
        const char value = static_cast<char>(next);
        if (value == '\n') {
            break;
        }
        if (line.size() >= maximum_reference_line_bytes) {
            throw std::invalid_argument("reference FASTA line exceeds the safety limit");
        }
        line.push_back(value);
    }
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    return true;
}

[[nodiscard]] char upper_base(const char value) {
    return static_cast<char>(std::toupper(static_cast<unsigned char>(value)));
}

[[nodiscard]] bool valid_iupac_base(const char value) noexcept {
    switch (value) {
        case 'A': case 'C': case 'G': case 'T': case 'U': case 'R': case 'Y': case 'S': case 'W':
        case 'K': case 'M': case 'B': case 'D': case 'H': case 'V': case 'N':
            return true;
        default:
            return false;
    }
}

[[nodiscard]] std::string header_identifier(const std::string& line) {
    if (line.size() < 2U || line.front() != '>') {
        throw std::invalid_argument("reference FASTA header is invalid");
    }
    const std::string_view payload{line.data() + 1U, line.size() - 1U};
    const auto first = payload.find_first_not_of(" \t");
    if (first == std::string_view::npos) {
        throw std::invalid_argument("reference FASTA header identifier is empty");
    }
    const auto last = payload.find_first_of(" \t", first);
    const std::string_view token = payload.substr(first, last == std::string_view::npos ? payload.size() - first : last - first);
    if (token.empty() || token.size() > maximum_contig_name_bytes) {
        throw std::invalid_argument("reference FASTA contig identifier is invalid");
    }
    return std::string{token};
}

}  // namespace

ReferenceGenome ReferenceGenome::from_fasta(std::istream& input, const ReferenceAssembly assembly) {
    struct ParsedContig final {
        std::string raw_name;
        std::string canonical_name;
        std::vector<std::string> aliases;
        std::string sequence;
    };

    std::vector<ParsedContig> parsed;
    std::string line;
    std::uint64_t total_bases = 0U;

    while (read_bounded_line(input, line)) {
        if (line.empty()) {
            continue;
        }
        if (line.front() == '>') {
            if (parsed.size() >= maximum_contigs) {
                throw std::invalid_argument("reference FASTA contains too many contigs");
            }
            const std::string raw_name = header_identifier(line);
            auto [canonical_name, aliases] = reference_detail::canonical_and_aliases(raw_name, assembly);
            parsed.push_back(ParsedContig{
                raw_name,
                std::move(canonical_name),
                std::move(aliases),
                {},
            });
            continue;
        }
        if (parsed.empty()) {
            throw std::invalid_argument("reference FASTA sequence appears before the first header");
        }
        if (line.size() > maximum_reference_bases - total_bases) {
            throw std::invalid_argument("reference FASTA exceeds the base-count safety limit");
        }
        for (const char raw : line) {
            if (std::isspace(static_cast<unsigned char>(raw)) != 0) {
                throw std::invalid_argument("reference FASTA sequence lines must not contain whitespace");
            }
            const char base_value = upper_base(raw);
            if (!valid_iupac_base(base_value)) {
                throw std::invalid_argument("reference FASTA contains a non-IUPAC base");
            }
            parsed.back().sequence.push_back(base_value == 'U' ? 'T' : base_value);
        }
        total_bases += line.size();
    }

    if (parsed.empty() || total_bases == 0U) {
        throw std::invalid_argument("reference FASTA contains no sequence");
    }
    for (const auto& contig : parsed) {
        if (contig.sequence.empty()) {
            throw std::invalid_argument("reference FASTA contains an empty contig");
        }
    }

    ContigTableBuilder builder{assembly};
    for (const auto& contig : parsed) {
        builder.add_contig(contig.canonical_name, contig.aliases);
    }

    ReferenceGenome genome;
    genome.contigs_ = builder.build();
    genome.sequences_.resize(genome.contigs_.size());
    std::vector<bool> assigned(genome.contigs_.size(), false);
    for (auto& contig : parsed) {
        const auto id = genome.contigs_.resolve(contig.raw_name);
        if (!id.has_value()) {
            throw std::logic_error("reference contig was not registered in the deterministic contig table");
        }
        const std::size_t index = static_cast<std::size_t>(*id);
        if (assigned[index]) {
            throw std::invalid_argument("reference FASTA defines the same canonical contig more than once");
        }
        genome.sequences_[index] = std::move(contig.sequence);
        assigned[index] = true;
    }
    return genome;
}

std::optional<std::string_view> ReferenceGenome::sequence(const ContigId id) const noexcept {
    const std::size_t index = static_cast<std::size_t>(id);
    if (index >= sequences_.size()) {
        return std::nullopt;
    }
    return std::string_view{sequences_[index]};
}

std::optional<char> ReferenceGenome::base(const ContigId id, const std::uint64_t position) const noexcept {
    const auto sequence_value = sequence(id);
    if (!sequence_value.has_value() || position >= sequence_value->size()) {
        return std::nullopt;
    }
    return (*sequence_value)[static_cast<std::size_t>(position)];
}

std::optional<std::string_view> ReferenceGenome::slice(
    const ContigId id,
    const std::uint64_t start,
    const std::uint64_t length
) const noexcept {
    const auto sequence_value = sequence(id);
    if (!sequence_value.has_value()) {
        return std::nullopt;
    }
    if (start > sequence_value->size() || length > sequence_value->size() - start) {
        return std::nullopt;
    }
    return sequence_value->substr(static_cast<std::size_t>(start), static_cast<std::size_t>(length));
}

}  // namespace biocore::domain
