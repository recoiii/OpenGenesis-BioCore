#include "biocore/domain/case_control_association.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using namespace biocore::domain;
const std::filesystem::path fixture_root =
    std::filesystem::path{BIOCORE_SOURCE_ROOT} / "tests/fixtures/cohort-v1";

void require(const bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void near(const std::optional<double> actual, const double expected) {
    require(actual.has_value() && std::abs(*actual - expected) < 1e-10, "numeric oracle mismatch");
}
template<class Exception, class Function>
void rejects(Function function) {
    try { function(); } catch (const Exception&) { return; }
    throw std::runtime_error("expected rejection did not occur");
}
ReferenceGenome reference() {
    std::ifstream input{fixture_root / "reference.fa"};
    require(input.good(), "reference fixture unavailable");
    return ReferenceGenome::from_fasta(input, ReferenceAssembly::grch38);
}
VcfIngestionResult read_vcf(const char* file, const ReferenceGenome& genome) {
    std::ifstream input{fixture_root / file};
    require(input.good(), "VCF fixture unavailable");
    return ingest_vcf(input, genome);
}
struct Fixture final {
    ReferenceGenome genome{reference()};
    ReferenceAssemblyIdentity assembly{ReferenceAssembly::grch38, {}};
    VcfIngestionResult cases{read_vcf("cases.vcf", genome)};
    VcfIngestionResult control1{read_vcf("control-1.vcf", genome)};
    VcfIngestionResult control2{read_vcf("control-2.vcf", genome)};
    std::vector<MultiSampleMatrixSource> sources() const {
        return {{"cases", assembly, &genome.contigs(), &cases},
                {"control-1", assembly, &genome.contigs(), &control1},
                {"control-2", assembly, &genome.contigs(), &control2}};
    }
    MultiSampleMatrix matrix() const {
        return build_multi_sample_matrix(assembly, genome.contigs(), sources());
    }
};
std::vector<CaseControlSampleLabel> labels() {
    return {{"001", CaseControlPhenotype::case_sample},
            {"\xC3\x96rnek-A", CaseControlPhenotype::case_sample},
            {"ctrl-01", CaseControlPhenotype::control},
            {"ctrl-02", CaseControlPhenotype::control}};
}
void missingness() {
    Fixture f;
    const auto matrix = f.matrix();
    require(matrix.sample_count() == 4U && matrix.variant_count() == 4U
            && matrix.observation_count() == 12U, "fixture dimensions");
    require(matrix.sample_index("001").has_value() && !matrix.sample_index("1"), "leading zero lost");
    require(matrix.sample_index("\xC3\x96rnek-A").has_value(), "Unicode ID lost");
    const auto result = analyze_case_control(matrix, labels());
    const auto& r = result.variants.at(1U);
    require(r.case_no_calls == 1U && r.case_partial_calls == 1U && r.case_complete_calls == 0U
            && r.case_unobserved == 0U && r.control_complete_calls == 1U
            && r.control_unobserved == 1U && r.control_no_calls == 0U, "missingness collapsed");
    require(!matrix.find_observation(*matrix.sample_index("ctrl-02"), 1U), "absent became observed");
    const auto* homref = matrix.find_observation(*matrix.sample_index("ctrl-01"), 1U);
    require(homref && homref->reference_dosage == 2U && homref->alternate_dosage == 0U,
            "explicit hom-ref lost");
    require(r.allele_table == AssociationContingencyTable{0U, 0U, 0U, 2U}, "partial call entered table");
    require(!r.allele_fisher_two_sided_p && !r.allele_bh_adjusted_q, "non-testable row became numeric");
    require(result.variants.at(2U).case_unobserved == 2U, "absent cases became controls");
}
void statistics() {
    Fixture f;
    const auto result = analyze_case_control(f.matrix(), labels());
    const auto& first = result.variants.at(0U);
    const auto& last = result.variants.at(3U);
    require(first.allele_table == AssociationContingencyTable{3U, 1U, 1U, 3U}, "allele direction");
    require(first.carrier_table == AssociationContingencyTable{2U, 0U, 1U, 1U}, "carrier direction");
    near(first.allele_fisher_two_sided_p, 17.0 / 35.0);
    near(last.allele_fisher_two_sided_p, 1.0 / 35.0);
    near(first.allele_bh_adjusted_q, 17.0 / 35.0);
    near(last.allele_bh_adjusted_q, 2.0 / 35.0);
    near(first.carrier_fisher_two_sided_p, 1.0);
    near(last.carrier_fisher_two_sided_p, 1.0 / 3.0);
    near(last.carrier_bh_adjusted_q, 2.0 / 3.0);
    near(first.carrier_bh_adjusted_q, 1.0);
    require(first.allele_odds_ratio.kind == AssociationOddsRatioKind::finite, "finite OR kind");
    near(first.allele_odds_ratio.value, 9.0);
    require(first.allele_odds_ratio_ci95.has_value(), "positive cells require CI");
    const double width = 1.959963984540054 * std::sqrt(8.0 / 3.0);
    near(first.allele_odds_ratio_ci95->lower, std::exp(std::log(9.0) - width));
    near(first.allele_odds_ratio_ci95->upper, std::exp(std::log(9.0) + width));
    require(first.carrier_odds_ratio.kind == AssociationOddsRatioKind::positive_infinity
            && !first.carrier_odds_ratio_ci95, "zero-cell infinity/CI contract");
    require(last.allele_odds_ratio.kind == AssociationOddsRatioKind::zero
            && !last.allele_odds_ratio_ci95, "zero-cell zero/CI contract");
    require(association_odds_ratio({0U, 0U, 0U, 0U}).kind == AssociationOddsRatioKind::undefined,
            "undefined OR was coerced");
}
void ordering() {
    Fixture f;
    const auto first = f.matrix();
    auto sources = f.sources();
    std::reverse(sources.begin(), sources.end());
    const auto second = build_multi_sample_matrix(f.assembly, f.genome.contigs(), sources);
    for (std::size_t s = 0; s < first.sample_count(); ++s) {
        require(first.sample(s) == second.sample(s), "sample ordering changed");
        for (std::size_t v = 0; v < first.variant_count(); ++v) {
            require(first.variant(v).locus.start == second.variant(v).locus.start, "variant ordering changed");
            const auto* a = first.find_observation(s, v);
            const auto* b = second.find_observation(s, v);
            require((a == nullptr) == (b == nullptr), "absence ordering changed");
            if (a && b) require(a->reference_dosage == b->reference_dosage
                               && a->alternate_dosage == b->alternate_dosage
                               && a->missing_allele_count == b->missing_allele_count,
                               "dosage ordering changed");
        }
    }
}
void validation() {
    Fixture f;
    const auto matrix = f.matrix();
    auto missing = labels(); missing.pop_back();
    rejects<std::invalid_argument>([&] { (void)analyze_case_control(matrix, missing); });
    auto duplicate = labels(); duplicate.back() = duplicate.front();
    rejects<std::invalid_argument>([&] { (void)analyze_case_control(matrix, duplicate); });
    auto one_group = labels();
    for (auto& label : one_group) label.phenotype = CaseControlPhenotype::case_sample;
    rejects<std::invalid_argument>([&] { (void)analyze_case_control(matrix, one_group); });
    auto sources = f.sources(); sources.front().assembly.assembly = ReferenceAssembly::grch37;
    rejects<std::invalid_argument>([&] { (void)build_multi_sample_matrix(f.assembly, f.genome.contigs(), sources); });
    sources = f.sources(); sources.push_back(sources.front()); sources.back().source_id = "duplicate-attempt";
    rejects<std::invalid_argument>([&] { (void)build_multi_sample_matrix(f.assembly, f.genome.contigs(), sources); });
    MultiSampleMatrixOptions limits; limits.maximum_observations = 11U;
    rejects<std::length_error>([&] { (void)build_multi_sample_matrix(f.assembly, f.genome.contigs(), f.sources(), limits); });
    limits = {}; limits.maximum_samples = 3U;
    rejects<std::length_error>([&] { (void)build_multi_sample_matrix(f.assembly, f.genome.contigs(), f.sources(), limits); });
    CaseControlAssociationOptions options; options.maximum_fisher_table_states = 1U;
    rejects<std::length_error>([&] { (void)analyze_case_control(matrix, labels(), options); });
}
} // namespace

int main(const int argc, char** argv) {
    if (argc != 2) return 1;
    const std::string_view mode{argv[1]};
    if (mode == "missingness") missingness();
    else if (mode == "statistics") statistics();
    else if (mode == "ordering") ordering();
    else if (mode == "validation") validation();
    else return 1;
}
