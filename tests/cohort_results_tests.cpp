#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "biocore/application/cohort_results_service.hpp"
#include "biocore/application/i_cohort_analysis_snapshot_store.hpp"
#include "biocore/presentation/cohort_result_report.hpp"

namespace {
using namespace biocore;

constexpr const char* project_id = "p-096";
constexpr const char* analysis_id = "a-096";
constexpr const char* snapshot_digest =
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
constexpr const char* reference_hash =
    "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";

void check(const bool value, const char* message) {
    if (!value) throw std::runtime_error{message};
}

template<class Exception, class Function>
void rejects(Function function) {
    try { function(); } catch (const Exception&) { return; }
    throw std::runtime_error{"Expected exception was not thrown"};
}

class Snapshots final : public application::ICohortAnalysisSnapshotStore {
public:
    application::CohortAnalysisStoreResult create(
        const application::CohortAnalysisSnapshot& value
    ) override {
        snapshot = value;
        return application::CohortAnalysisStoreResult::stored;
    }

    std::optional<application::CohortAnalysisSnapshot> find(
        const std::string_view project,
        const std::string_view analysis
    ) override {
        if (!snapshot.has_value() || snapshot->project_id != project ||
            snapshot->analysis_id != analysis) return std::nullopt;
        return snapshot;
    }

    std::optional<application::CohortAnalysisSnapshot> snapshot;
};

application::CohortAssociationModelResult model(
    const bool family,
    const domain::AssociationContingencyTable table,
    const double p,
    const double q
) {
    return {
        .family_member = family,
        .table = table,
        .odds_ratio = domain::association_odds_ratio(table),
        .odds_ratio_ci95 = domain::association_odds_ratio_woolf_ci95(table),
        .fisher_two_sided_p = family ? std::optional<double>{p} : std::nullopt,
        .bh_adjusted_q = family ? std::optional<double>{q} : std::nullopt,
    };
}

application::CohortAnalysisVariantUniverse universe(
    const std::size_t ordinal,
    const std::uint64_t position,
    const bool family
) {
    return {
        .ordinal = ordinal,
        .contig = "chr1",
        .start = position,
        .end = position + 1U,
        .reference = "A",
        .alternate = ordinal == 3U ? "T" : "G",
        .case_complete_calls = 2U,
        .control_complete_calls = 2U,
        .allele_family_member = family,
        .carrier_family_member = family,
    };
}

application::CohortAnalysisSampleSnapshot sample_snapshot(
    const std::size_t ordinal,
    std::string id,
    const application::CohortGroup group,
    const application::CohortAnalysisDisposition disposition,
    std::optional<std::string> reason = std::nullopt
) {
    return {
        .ordinal = ordinal,
        .sample_id = std::move(id),
        .sample_display_name = {},
        .sample_group_metadata = {},
        .biological_unit_id = {},
        .group = group,
        .cohort_disposition = application::CohortMemberDisposition::included,
        .cohort_exclusion_reason = std::nullopt,
        .analysis_disposition = disposition,
        .analysis_reason = std::move(reason),
        .qc = {},
    };
}

application::CohortAnalysisSnapshot snapshot() {
    application::CohortAnalysisSnapshot value;
    value.analysis_id = analysis_id;
    value.project_id = project_id;
    value.cohort_id = "c-096";
    value.cohort_revision = 7U;
    value.preview_digest =
        "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";
    value.snapshot_digest = snapshot_digest;
    value.approved_at_utc = "2026-10-02T19:10:00Z";
    value.reference.managed_file_id = "reference-1";
    value.reference.sha256 = reference_hash;
    value.reference.assembly = domain::ReferenceAssembly::grch38;
    value.reference.normalization_contract_version = "biocore.vcf-normalization.v1";
    value.approved_case_samples = 2U;
    value.approved_control_samples = 2U;
    value.allele_family_size = 3U;
    value.carrier_family_size = 3U;
    value.samples = {
        sample_snapshot(0U, "case-1", application::CohortGroup::case_group,
                        application::CohortAnalysisDisposition::included),
        sample_snapshot(1U, "case-2", application::CohortGroup::case_group,
                        application::CohortAnalysisDisposition::included),
        sample_snapshot(2U, "control-1", application::CohortGroup::control,
                        application::CohortAnalysisDisposition::included),
        sample_snapshot(3U, "control-2", application::CohortGroup::control,
                        application::CohortAnalysisDisposition::included),
        sample_snapshot(4U, "qc-excluded", application::CohortGroup::control,
                        application::CohortAnalysisDisposition::excluded,
                        std::string{"QC"}),
    };
    value.test_universe = {
        universe(0U, 10U, true),
        universe(1U, 20U, true),
        universe(2U, 30U, true),
        universe(3U, 40U, false),
    };
    return value;
}

application::CohortAssociationVariantResult row(
    const std::size_t ordinal,
    const std::uint64_t position,
    const domain::AssociationContingencyTable carrier,
    const bool family,
    const double p,
    const double q
) {
    return {
        .ordinal = ordinal,
        .contig = "chr1",
        .start = position,
        .end = position + 1U,
        .reference = "A",
        .alternate = ordinal == 3U ? "T" : "G",
        .total_cases = 2U,
        .total_controls = 2U,
        .effective_case_samples = 2U,
        .effective_control_samples = 2U,
        .allele = model(family, carrier, p, q),
        .carrier = model(family, carrier, p, q),
    };
}

application::CohortCaseControlAnalysisResult analysis() {
    application::CohortCaseControlAnalysisResult value;
    value.project_id = project_id;
    value.analysis_id = analysis_id;
    value.snapshot_digest = snapshot_digest;
    value.association_contract_version = "biocore.case-control.v1";
    value.test_filter_version = "biocore.complete-call-gate.v1";
    value.approved_case_samples = 2U;
    value.approved_control_samples = 2U;
    value.allele_family_size = 3U;
    value.carrier_family_size = 3U;
    value.variants = {
        row(0U, 10U, {1U,1U,1U,1U}, true, 0.01, 0.03),
        row(1U, 20U, {2U,0U,0U,2U}, true, 0.02, 0.04),
        row(2U, 30U, {0U,2U,1U,1U}, true, 0.20, 0.20),
        row(3U, 40U, {0U,2U,0U,2U}, false, 1.0, 1.0),
    };
    return value;
}

struct Harness final {
    Harness() {
        snapshots.snapshot = snapshot();
    }
    Snapshots snapshots;
    application::CohortResultsService service{snapshots};
    application::CohortCaseControlAnalysisResult result{analysis()};
};

std::vector<application::CohortResultAnnotationLink> annotations() {
    return {
        {
            .ordinal = 1U,
            .database_ids = {"ClinVar"},
            .primary_record_id = std::string{"RCV096"},
            .primary_feature_type = std::string{"gene"},
        },
        {
            .ordinal = 2U,
            .database_ids = {"dbSNP"},
            .primary_record_id = std::string{"rs960"},
            .primary_feature_type = std::nullopt,
        },
    };
}

void pagination_test() {
    Harness h;
    application::CohortResultQuery query;
    query.limit = 2U;
    const auto first = h.service.query(project_id, analysis_id, h.result, query);
    check(first.total_unfiltered_variants == 4U, "unfiltered total");
    check(first.total_matched_variants == 4U, "matched total");
    check(first.rows.size() == 2U && first.rows[0].association.ordinal == 0U,
          "first bounded page");

    query.offset = 2U;
    const auto second = h.service.query(project_id, analysis_id, h.result, query);
    check(second.rows.size() == 2U && second.rows[0].association.ordinal == 2U,
          "second bounded page");
    check(first.filter_definition == second.filter_definition,
          "pagination does not alter canonical view filter");
}

void views_test() {
    Harness h;
    application::CohortResultQuery query;
    query.variant_view = application::CohortResultVariantView::shared;
    auto page = h.service.query(project_id, analysis_id, h.result, query);
    check(page.total_matched_variants == 1U &&
          page.rows[0].association.ordinal == 0U, "shared carrier view");

    query.variant_view = application::CohortResultVariantView::case_specific;
    page = h.service.query(project_id, analysis_id, h.result, query);
    check(page.total_matched_variants == 1U &&
          page.rows[0].association.ordinal == 1U, "case-specific carrier view");

    query.variant_view = application::CohortResultVariantView::control_specific;
    page = h.service.query(project_id, analysis_id, h.result, query);
    check(page.total_matched_variants == 1U &&
          page.rows[0].association.ordinal == 2U, "control-specific carrier view");
}

void statistics_test() {
    Harness h;
    application::CohortResultQuery query;
    query.maximum_allele_fdr_q = 0.05;
    const auto page = h.service.query(project_id, analysis_id, h.result, query);
    check(page.total_matched_variants == 2U, "FDR display filter count");
    check(page.rows[0].association.allele.bh_adjusted_q ==
          std::optional<double>{0.03}, "frozen q value retained");
    check(page.rows[1].association.allele.bh_adjusted_q ==
          std::optional<double>{0.04}, "display filter does not recalculate q");
}

void annotation_test() {
    Harness h;
    application::CohortResultQuery query;
    query.annotated_only = true;
    auto page = h.service.query(
        project_id, analysis_id, h.result, query, annotations()
    );
    check(page.total_matched_variants == 2U, "annotated-only view");

    query.annotated_only = false;
    query.search_text = "clinvar";
    page = h.service.query(
        project_id, analysis_id, h.result, query, annotations()
    );
    check(page.total_matched_variants == 1U &&
          page.rows[0].annotation.has_value() &&
          page.rows[0].annotation->primary_record_id ==
              std::optional<std::string>{"RCV096"},
          "existing annotation linkage searchable");
}

void provenance_test() {
    Harness h;
    application::CohortResultQuery query;
    query.variant_view = application::CohortResultVariantView::case_specific;
    const auto package = h.service.build_package(
        project_id, analysis_id, h.result, query,
        "0.6.0-dev", "2026-10-02T19:15:00Z", annotations()
    );
    check(package.snapshot_digest == snapshot_digest, "snapshot provenance");
    check(package.reference_sha256 == reference_hash, "reference provenance");
    check(package.approved_case_samples == 2U &&
          package.approved_control_samples == 2U, "frozen denominators visible");
    check(package.excluded_samples == 1U, "exclusion count visible");
    check(package.total_matched_variants == 1U, "package filter applied");
}

void exports_test() {
    Harness h;
    application::CohortResultQuery query;
    query.maximum_carrier_fdr_q = 0.05;
    const auto package = h.service.build_package(
        project_id, analysis_id, h.result, query,
        "0.6.0-dev", "2026-10-02T19:16:00Z", annotations()
    );
    const auto json = presentation::render_cohort_result_package_json(package);
    const auto csv = presentation::render_cohort_result_package_csv(package);
    const auto tsv = presentation::render_cohort_result_package_tsv(package);
    const auto html = presentation::render_cohort_result_package_html(package);

    check(json.find(snapshot_digest) != std::string::npos &&
          json.find(package.filter_definition) != std::string::npos,
          "JSON carries snapshot and filter");
    check(csv.find(std::string{"# snapshot_digest="} + snapshot_digest) != std::string::npos &&
          csv.find(std::string{"# filter_definition="} + package.filter_definition) != std::string::npos,
          "CSV carries snapshot and filter");
    check(tsv.find(std::string{"# snapshot_digest="} + snapshot_digest) != std::string::npos,
          "TSV carries snapshot");
    check(html.find(snapshot_digest) != std::string::npos &&
          html.find("Display filters do not redefine the frozen statistical test universe") !=
              std::string::npos,
          "HTML exposes provenance and filter semantics");
}

void bounded_test() {
    Harness h;
    application::CohortResultQuery invalid;
    invalid.limit = 201U;
    rejects<std::invalid_argument>([&] {
        (void)h.service.query(project_id, analysis_id, h.result, invalid);
    });

    application::CohortResultQuery all;
    rejects<std::length_error>([&] {
        (void)h.service.build_package(
            project_id, analysis_id, h.result, all,
            "0.6.0-dev", "2026-10-02T19:17:00Z", {}, 2U
        );
    });
}

void drift_test() {
    Harness h;
    auto changed = h.result;
    changed.snapshot_digest =
        "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd";
    rejects<std::invalid_argument>([&] {
        (void)h.service.query(
            project_id, analysis_id, changed, application::CohortResultQuery{}
        );
    });

    changed = h.result;
    changed.variants[0].allele.family_member = false;
    rejects<std::invalid_argument>([&] {
        (void)h.service.query(
            project_id, analysis_id, changed, application::CohortResultQuery{}
        );
    });
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::invalid_argument{"mode required"};
        const std::string mode{argv[1]};
        if (mode == "pagination") pagination_test();
        else if (mode == "views") views_test();
        else if (mode == "statistics") statistics_test();
        else if (mode == "annotations") annotation_test();
        else if (mode == "provenance") provenance_test();
        else if (mode == "exports") exports_test();
        else if (mode == "bounded") bounded_test();
        else if (mode == "drift") drift_test();
        else throw std::invalid_argument{"unknown mode"};
        std::cout << "PASS " << mode << '\n';
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
