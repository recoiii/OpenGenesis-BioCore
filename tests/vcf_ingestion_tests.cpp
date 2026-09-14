#include "biocore/domain/reference_genome.hpp"
#include "vcf_ingestion_test_support.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>

namespace {

[[nodiscard]] bool require(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "vcf_ingestion_tests: " << message << '\n';
        return false;
    }
    return true;
}

}  // namespace

int main() {
    using namespace biocore::domain;
    bool ok = true;

    std::istringstream fasta{">chr1 primary\nAAAAAACGTACGT\n"};
    const auto genome = ReferenceGenome::from_fasta(fasta, ReferenceAssembly::grch38);
    ok = require(genome.contigs().resolve("1") == genome.contigs().resolve("chr1"), "GRCh38 chr alias") && ok;
    ok = require(
        genome.contigs().resolve("1") == genome.contigs().resolve("NC_000001.11"),
        "GRCh38 accession alias"
    ) && ok;

    ok = run_vcf_ingestion_info_tests(genome) && ok;
    ok = run_vcf_ingestion_genotype_tests(genome) && ok;
    ok = run_vcf_ingestion_missing_tests(genome) && ok;
    ok = run_vcf_ingestion_edge_tests(genome) && ok;
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
