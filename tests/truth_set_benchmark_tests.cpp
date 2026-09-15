#include "biocore/domain/truth_set_benchmark.hpp"
#include "biocore/domain/reference_genome.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace biocore::domain;

void require(const bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_near(const std::optional<double>& value, const double expected, const char* message) {
    require(value.has_value(), message);
    require(std::abs(*value - expected) < 1e-12, message);
}

BenchmarkVariantObservation obs(
    std::string contig,
    const std::uint64_t start,
    std::string ref,
    std::string alt,
    std::optional<BenchmarkGenotype> genotype = std::nullopt
) {
    const auto type = classify_variant(ref, alt, false);
    return {
        .contig_name = std::move(contig),
        .start = start,
        .end = start + ref.size(),
        .reference_allele = std::move(ref),
        .alternate_allele = std::move(alt),
        .variant_type = type,
        .symbolic = false,
        .genotype = genotype,
    };
}

void test_metrics() {
    const std::vector truth{
        obs("1", 10U, "A", "G"),
        obs("1", 20U, "A", "AT"),
        obs("1", 30U, "AT", "A"),
        obs("1", 40U, "AC", "GT"),
    };
    const std::vector candidate{
        obs("1", 10U, "A", "G"),
        obs("1", 20U, "A", "AT"),
        obs("1", 30U, "AT", "A"),
        obs("1", 50U, "AC", "GTT"),
    };
    const auto result = benchmark_truth_set(truth, candidate);
    require(result.overall.truth_total == 4U, "truth total");
    require(result.overall.candidate_total == 4U, "candidate total");
    require(result.overall.true_positives == 3U, "true positives");
    require(result.overall.false_positives == 1U, "false positives");
    require(result.overall.false_negatives == 1U, "false negatives");
    require_near(result.overall.precision, 0.75, "precision");
    require_near(result.overall.recall, 0.75, "recall");
    require_near(result.overall.f1, 0.75, "F1");
}

void test_genotype() {
    const std::vector truth{
        obs("1", 10U, "A", "G", BenchmarkGenotype{2U, 1U}),
        obs("1", 20U, "C", "T", BenchmarkGenotype{2U, 2U}),
        obs("1", 30U, "G", "A", BenchmarkGenotype{2U, 1U}),
    };
    const std::vector candidate{
        obs("1", 10U, "A", "G", BenchmarkGenotype{2U, 1U}),
        obs("1", 20U, "C", "T", BenchmarkGenotype{2U, 1U}),
        obs("1", 30U, "G", "A"),
    };
    const auto result = benchmark_truth_set(truth, candidate);
    require(result.genotypes.comparable == 2U, "comparable genotypes");
    require(result.genotypes.concordant == 1U, "concordant genotypes");
    require(result.genotypes.discordant == 1U, "discordant genotypes");
    require_near(result.genotypes.concordance, 0.5, "genotype concordance");
}

void test_stratification() {
    const std::vector truth{
        obs("1", 10U, "A", "G"),
        obs("1", 20U, "A", "AT"),
        obs("1", 30U, "AT", "A"),
        obs("1", 40U, "AC", "GT"),
        obs("1", 50U, "AC", "GTT"),
    };
    const std::vector candidate{
        truth[0], truth[1], truth[3], truth[4],
        obs("1", 60U, "A", "C"),
    };
    const auto result = benchmark_truth_set(truth, candidate);
    require(result.by_type.size() == 6U, "all canonical strata emitted");
    auto find = [&](const VariantType type) -> const VariantBenchmarkStratum& {
        for (const auto& stratum : result.by_type) {
            if (stratum.variant_type == type) return stratum;
        }
        throw std::runtime_error("stratum not found");
    };
    require(find(VariantType::snv).variants.true_positives == 1U, "SNV TP");
    require(find(VariantType::snv).variants.false_positives == 1U, "SNV FP");
    require(find(VariantType::insertion).variants.true_positives == 1U, "INS TP");
    require(find(VariantType::deletion).variants.false_negatives == 1U, "DEL FN");
    require(find(VariantType::mnv).variants.true_positives == 1U, "MNV TP");
    require(find(VariantType::delins_complex).variants.true_positives == 1U, "DELINS TP");
}

void test_vcf_adapter() {
    std::istringstream fasta{">chr1\nAAAAAAAAAACAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"};
    const auto reference = ReferenceGenome::from_fasta(fasta, ReferenceAssembly::grch38);
    std::istringstream vcf{
        "##fileformat=VCFv4.3\n"
        "##FORMAT=<ID=GT,Number=1,Type=String,Description=\"Genotype\">\n"
        "#CHROM\tPOS\tID\tREF\tALT\tQUAL\tFILTER\tINFO\tFORMAT\tHG002\n"
        "chr1\t11\trs1\tC\tG\t50\tPASS\t.\tGT\t0/1\n"
        "chr1\t12\trs2\tA\tC,T\t50\tPASS\t.\tGT\t1/2\n"
        "chr1\t13\trs3\tA\tG\t50\tLowQual\t.\tGT\t1/1\n"
    };
    const auto ingested = ingest_vcf(vcf, reference);
    VcfBenchmarkAdapterOptions options;
    options.sample_id = "HG002";
    options.pass_only = true;
    const auto observations = benchmark_observations_from_vcf(
        ingested,
        reference.contigs(),
        options
    );
    require(observations.size() == 3U, "PASS adapter flattens two records into three allele observations");
    require(observations.front().contig_name == "1", "adapter uses canonical contig name");
    require(observations.front().genotype == std::optional<BenchmarkGenotype>{{2U, 1U}}, "biallelic GT is preserved as dosage");
    require(!observations[1].genotype.has_value() && !observations[2].genotype.has_value(), "multiallelic GT is not lossy-projected");
}

void test_validation() {
    bool duplicate_rejected = false;
    const std::vector duplicates{
        obs("1", 10U, "A", "G"),
        obs("1", 10U, "A", "G"),
    };
    try {
        static_cast<void>(benchmark_truth_set(duplicates, {}));
    } catch (const std::invalid_argument&) {
        duplicate_rejected = true;
    }
    require(duplicate_rejected, "duplicate truth identities fail closed");

    auto invalid_type = obs("1", 20U, "A", "AT");
    invalid_type.variant_type = VariantType::snv;
    bool type_rejected = false;
    try {
        validate_benchmark_observation(invalid_type);
    } catch (const std::invalid_argument&) {
        type_rejected = true;
    }
    require(type_rejected, "inconsistent variant type fails closed");

    std::istringstream fasta{">chr1\nAAAAAAAAAAAAAAAAAAAA\n"};
    const auto reference = ReferenceGenome::from_fasta(fasta, ReferenceAssembly::grch38);
    std::istringstream vcf{
        "##fileformat=VCFv4.3\n"
        "#CHROM\tPOS\tID\tREF\tALT\tQUAL\tFILTER\tINFO\n"
        "chr1\t1\t.\tA\tG\t.\tPASS\t.\n"
    };
    const auto ingested = ingest_vcf(vcf, reference);
    bool sample_rejected = false;
    try {
        VcfBenchmarkAdapterOptions options;
        options.sample_id = "HG002";
        static_cast<void>(benchmark_observations_from_vcf(ingested, reference.contigs(), options));
    } catch (const std::invalid_argument&) {
        sample_rejected = true;
    }
    require(sample_rejected, "missing requested sample fails closed");
}

}  // namespace

int main(const int argc, char** argv) {
    try {
        if (argc != 2) {
            throw std::invalid_argument("expected one test selector");
        }
        const std::string_view selector{argv[1]};
        if (selector == "metrics") test_metrics();
        else if (selector == "genotype") test_genotype();
        else if (selector == "stratification") test_stratification();
        else if (selector == "vcf_adapter") test_vcf_adapter();
        else if (selector == "validation") test_validation();
        else throw std::invalid_argument("unknown test selector");
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "truth-set benchmark test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
