#include "biocore/domain/reference_genome.hpp"
#include "biocore/domain/variant_normalization.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {

[[nodiscard]] bool require(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "variant_normalization_tests: " << message << '\n';
        return false;
    }
    return true;
}

template <typename Function>
[[nodiscard]] bool throws_invalid_argument(Function&& function) {
    try {
        function();
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

}  // namespace

int main() {
    using namespace biocore::domain;
    bool ok = true;

    std::istringstream fasta{">1\nAAAAAACGTACGT\n"};
    const auto reference = ReferenceGenome::from_fasta(fasta, ReferenceAssembly::grch38);
    const auto chr1 = reference.contigs().resolve("chr1");
    ok = require(chr1.has_value(), "chr1 resolution") && ok;
    if (!chr1.has_value()) return EXIT_FAILURE;

    VariantRecord deletion;
    deletion.locus = {*chr1, 2U, 4U};
    deletion.reference = Allele{"AA", VariantType::unknown, false};
    deletion.alternates.push_back(Allele{"A", VariantType::deletion, false});
    normalize_variant(deletion, reference);
    ok = require(deletion.locus.start == 0U && deletion.locus.end == 2U, "homopolymer deletion left-aligned") && ok;
    ok = require(deletion.reference.sequence == "AA" && deletion.alternates[0U].sequence == "A", "left-aligned alleles") && ok;

    const VariantRecord once = deletion;
    normalize_variant(deletion, reference);
    ok = require(deletion.locus.start == once.locus.start && deletion.locus.end == once.locus.end, "normalization idempotent coordinates") && ok;
    ok = require(deletion.reference.sequence == once.reference.sequence && deletion.alternates[0U].sequence == once.alternates[0U].sequence, "normalization idempotent alleles") && ok;

    VariantRecord insertion;
    insertion.locus = {*chr1, 3U, 4U};
    insertion.reference = Allele{"A", VariantType::unknown, false};
    insertion.alternates.push_back(Allele{"AA", VariantType::insertion, false});
    normalize_variant(insertion, reference);
    ok = require(insertion.locus.start == 0U, "homopolymer insertion left-aligned") && ok;

    VariantRecord multi;
    multi.locus = {*chr1, 6U, 8U};
    multi.reference = Allele{"CG", VariantType::unknown, false};
    multi.alternates.push_back(Allele{"CT", VariantType::mnv, false});
    multi.alternates.push_back(Allele{"CA", VariantType::mnv, false});
    normalize_variant(multi, reference);
    ok = require(multi.locus.start == 7U && multi.reference.sequence == "G", "joint multi-allelic prefix trim") && ok;
    ok = require(multi.alternates[0U].sequence == "T" && multi.alternates[1U].sequence == "A", "ALT order preserved") && ok;

    VariantRecord mismatch;
    mismatch.locus = {*chr1, 6U, 7U};
    mismatch.reference = Allele{"A", VariantType::unknown, false};
    mismatch.alternates.push_back(Allele{"T", VariantType::snv, false});
    ok = require(throws_invalid_argument([&] { normalize_variant(mismatch, reference); }), "reference mismatch fail-closed") && ok;

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
