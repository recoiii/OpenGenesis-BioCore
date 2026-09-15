#include "biocore/presentation/variant_export.hpp"
#include "variant_export_support.hpp"

#include <array>
#include <ostream>
#include <sstream>
#include <string_view>

namespace biocore::presentation {
namespace {
using application::VariantExportDatabaseProvenance;
using application::VariantExportPlan;
using domain::VariantWorkspaceRowDto;
using namespace variant_export_support;

constexpr std::array<std::string_view, 27U> columns{
    "contig","start0","end0","ref","alt","variant_type","symbolic","record_id","quality","filter",
    "carrier_sample_count","complete_call_count","primary_annotation_id","primary_feature_type",
    "allele_or_kind","allele_or","allele_ci95_low","allele_ci95_high","allele_fisher_p","allele_fdr_q",
    "carrier_or_kind","carrier_or","carrier_ci95_low","carrier_ci95_high","carrier_fisher_p","carrier_fdr_q",
    "variant_index"
};

void write_field(std::ostream& out, const std::string_view value, const char delimiter) {
    const bool quote = value.find(delimiter) != std::string_view::npos ||
        value.find('"') != std::string_view::npos || value.find('\n') != std::string_view::npos ||
        value.find('\r') != std::string_view::npos;
    if (!quote) { out << value; return; }
    out << '"';
    for (const char c : value) c == '"' ? out << "\"\"" : out << c;
    out << '"';
}

void write_preamble(std::ostream& out, const VariantExportPlan& plan) {
    const auto& p = plan.provenance();
    out << "#biocore_export_schema=" << p.schema_version << '\n'
        << "#producer=" << percent_encode(p.producer_name) << '/' << percent_encode(p.producer_version) << '\n'
        << "#generated_at=" << percent_encode(p.generated_at_utc) << '\n'
        << "#pipeline=" << percent_encode(p.pipeline.pipeline_id) << '@' << percent_encode(p.pipeline.pipeline_version) << '\n';
    if (p.pipeline.job_id) out << "#job_id=" << percent_encode(*p.pipeline.job_id) << '\n';
    if (p.pipeline.job_revision) out << "#job_revision=" << *p.pipeline.job_revision << '\n';
    if (p.pipeline.attempt_number) out << "#attempt_number=" << *p.pipeline.attempt_number << '\n';
    out << "#reference=" << percent_encode(p.reference.reference_id) << '|'
        << percent_encode(assembly_label(p.reference.assembly)) << '|' << p.reference.source_sha256 << '\n';
    for (const auto& db : p.databases) {
        out << "#database=" << percent_encode(db.database_id) << '|' << percent_encode(db.version) << '|'
            << db.schema_version << '|' << percent_encode(assembly_label(db.assembly)) << '|'
            << db.source_sha256 << '\n';
    }
}

void write_row(std::ostream& out, const VariantWorkspaceRowDto& r, const char d) {
    const auto field = [&](const std::string_view v) { write_field(out, v, d); out << d; };
    field(r.contig_name); out << r.start << d << r.end << d; field(r.reference_allele); field(r.alternate_allele);
    field(domain::to_string(r.variant_type)); out << (r.symbolic ? "true" : "false") << d;
    if (r.record_id) write_field(out, *r.record_id, d); else out << '.'; out << d;
    write_optional_double(out, r.quality); out << d; write_field(out, r.filter_status, d);
    out << d << r.carrier_sample_count << d << r.total_complete_calls << d;
    if (r.primary_annotation_id) write_field(out, *r.primary_annotation_id, d); else out << '.'; out << d;
    if (r.primary_feature_type) write_field(out, *r.primary_feature_type, d); else out << '.'; out << d;
    write_field(out, odds_kind(r.allele_odds_ratio.kind), d); out << d; write_optional_double(out, odds_value(r.allele_odds_ratio)); out << d;
    write_optional_double(out, r.allele_odds_ratio_ci95 ? std::optional<double>{r.allele_odds_ratio_ci95->lower} : std::nullopt); out << d;
    write_optional_double(out, r.allele_odds_ratio_ci95 ? std::optional<double>{r.allele_odds_ratio_ci95->upper} : std::nullopt); out << d;
    write_optional_double(out, r.allele_fisher_two_sided_p); out << d; write_optional_double(out, r.allele_bh_adjusted_q); out << d;
    write_field(out, odds_kind(r.carrier_odds_ratio.kind), d); out << d; write_optional_double(out, odds_value(r.carrier_odds_ratio)); out << d;
    write_optional_double(out, r.carrier_odds_ratio_ci95 ? std::optional<double>{r.carrier_odds_ratio_ci95->lower} : std::nullopt); out << d;
    write_optional_double(out, r.carrier_odds_ratio_ci95 ? std::optional<double>{r.carrier_odds_ratio_ci95->upper} : std::nullopt); out << d;
    write_optional_double(out, r.carrier_fisher_two_sided_p); out << d; write_optional_double(out, r.carrier_bh_adjusted_q);
    out << d << r.variant_index << '\n';
}

void write_delimited(std::ostream& out, const VariantExportPlan& plan, const char d) {
    write_preamble(out, plan);
    for (std::size_t i = 0; i < columns.size(); ++i) { if (i) out << d; write_field(out, columns[i], d); }
    out << '\n';
    for (std::size_t i = 0; i < plan.row_count(); ++i) write_row(out, plan.row(i), d);
    require_stream(out);
}

void write_db_json(std::ostream& out, const VariantExportDatabaseProvenance& db) {
    out << "{\"databaseId\":"; write_json_string(out, db.database_id);
    out << ",\"displayName\":"; write_json_string(out, db.display_name);
    out << ",\"version\":"; write_json_string(out, db.version);
    out << ",\"schemaVersion\":" << db.schema_version << ",\"assembly\":"; write_json_string(out, assembly_label(db.assembly));
    out << ",\"sourceSha256\":"; write_json_string(out, db.source_sha256); out << '}';
}

void write_row_json(std::ostream& out, const VariantWorkspaceRowDto& r) {
    out << "{\"variantIndex\":" << r.variant_index << ",\"contig\":"; write_json_string(out, r.contig_name);
    out << ",\"start0\":" << r.start << ",\"end0\":" << r.end << ",\"ref\":"; write_json_string(out, r.reference_allele);
    out << ",\"alt\":"; write_json_string(out, r.alternate_allele); out << ",\"variantType\":"; write_json_string(out, domain::to_string(r.variant_type));
    out << ",\"symbolic\":" << (r.symbolic ? "true" : "false") << ",\"recordId\":"; write_json_optional_string(out, r.record_id);
    out << ",\"quality\":"; write_json_optional_double(out, r.quality); out << ",\"filter\":"; write_json_string(out, r.filter_status);
    out << ",\"carrierSampleCount\":" << r.carrier_sample_count << ",\"completeCallCount\":" << r.total_complete_calls;
    out << ",\"annotation\":{\"available\":" << (r.annotation_available ? "true" : "false") << ",\"alleleCount\":" << r.allele_annotation_count
        << ",\"featureCount\":" << r.feature_annotation_count << ",\"primaryId\":"; write_json_optional_string(out, r.primary_annotation_id);
    out << ",\"primaryFeatureType\":"; write_json_optional_string(out, r.primary_feature_type); out << '}';
    out << ",\"association\":{\"available\":" << (r.association_available ? "true" : "false") << ",\"alleleOddsRatioKind\":";
    write_json_string(out, odds_kind(r.allele_odds_ratio.kind)); out << ",\"alleleOddsRatio\":"; write_json_optional_double(out, odds_value(r.allele_odds_ratio));
    out << ",\"alleleCi95\":";
    if (r.allele_odds_ratio_ci95) { out << "{\"lower\":"; write_double(out, r.allele_odds_ratio_ci95->lower); out << ",\"upper\":"; write_double(out, r.allele_odds_ratio_ci95->upper); out << '}'; } else out << "null";
    out << ",\"alleleFisherP\":"; write_json_optional_double(out, r.allele_fisher_two_sided_p); out << ",\"alleleFdrQ\":"; write_json_optional_double(out, r.allele_bh_adjusted_q);
    out << ",\"carrierOddsRatioKind\":"; write_json_string(out, odds_kind(r.carrier_odds_ratio.kind)); out << ",\"carrierOddsRatio\":"; write_json_optional_double(out, odds_value(r.carrier_odds_ratio));
    out << ",\"carrierCi95\":";
    if (r.carrier_odds_ratio_ci95) { out << "{\"lower\":"; write_double(out, r.carrier_odds_ratio_ci95->lower); out << ",\"upper\":"; write_double(out, r.carrier_odds_ratio_ci95->upper); out << '}'; } else out << "null";
    out << ",\"carrierFisherP\":"; write_json_optional_double(out, r.carrier_fisher_two_sided_p); out << ",\"carrierFdrQ\":"; write_json_optional_double(out, r.carrier_bh_adjusted_q); out << "}}";
}
} // namespace

void write_variant_export_tsv(std::ostream& out, const VariantExportPlan& plan) { write_delimited(out, plan, '\t'); }
void write_variant_export_csv(std::ostream& out, const VariantExportPlan& plan) { write_delimited(out, plan, ','); }

void write_variant_export_json(std::ostream& out, const VariantExportPlan& plan) {
    const auto& p = plan.provenance();
    out << "{\"schemaVersion\":" << p.schema_version << ",\"producer\":{\"name\":"; write_json_string(out, p.producer_name);
    out << ",\"version\":"; write_json_string(out, p.producer_version); out << "},\"generatedAtUtc\":"; write_json_string(out, p.generated_at_utc);
    out << ",\"pipeline\":{\"id\":"; write_json_string(out, p.pipeline.pipeline_id); out << ",\"version\":"; write_json_string(out, p.pipeline.pipeline_version);
    out << ",\"jobId\":"; write_json_optional_string(out, p.pipeline.job_id); out << ",\"jobRevision\":";
    if (p.pipeline.job_revision) out << *p.pipeline.job_revision; else out << "null"; out << ",\"attemptNumber\":";
    if (p.pipeline.attempt_number) out << *p.pipeline.attempt_number; else out << "null"; out << "},\"reference\":{\"id\":";
    write_json_string(out, p.reference.reference_id); out << ",\"assembly\":"; write_json_string(out, assembly_label(p.reference.assembly));
    out << ",\"sourceSha256\":"; write_json_string(out, p.reference.source_sha256); out << "},\"databases\":[";
    for (std::size_t i = 0; i < p.databases.size(); ++i) { if (i) out << ','; write_db_json(out, p.databases[i]); }
    out << "],\"variantCount\":" << plan.row_count() << ",\"variants\":[";
    for (std::size_t i = 0; i < plan.row_count(); ++i) { if (i) out << ','; write_row_json(out, plan.row(i)); }
    out << "]}\n"; require_stream(out);
}

std::string render_variant_export_tsv(const VariantExportPlan& plan) { std::ostringstream out; write_variant_export_tsv(out, plan); return out.str(); }
std::string render_variant_export_csv(const VariantExportPlan& plan) { std::ostringstream out; write_variant_export_csv(out, plan); return out.str(); }
std::string render_variant_export_json(const VariantExportPlan& plan) { std::ostringstream out; write_variant_export_json(out, plan); return out.str(); }

}  // namespace biocore::presentation
