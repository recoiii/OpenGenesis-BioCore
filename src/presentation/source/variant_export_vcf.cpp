#include "biocore/presentation/variant_export.hpp"
#include "variant_export_support.hpp"

#include <limits>
#include <ostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>

namespace biocore::presentation {
namespace {
using application::VariantExportPlan;
using namespace variant_export_support;

void meta(std::ostream& out, const std::string_view key, const std::string_view value) {
    out << "##" << key << "=\"";
    for (const char c : value) {
        if (c == '"' || c == '\\') out << '\\';
        out << ((c == '\n' || c == '\r') ? ' ' : c);
    }
    out << "\"\n";
}

void info(std::ostream& out, const std::string_view key, const std::optional<double>& value) {
    if (!value) return;
    out << ';' << key << '='; write_double(out, *value);
}
} // namespace

void write_variant_export_vcf(std::ostream& out, const VariantExportPlan& plan) {
    const auto& p = plan.provenance();
    out << "##fileformat=VCFv4.3\n";
    meta(out, "source", p.producer_name + "-" + p.producer_version);
    meta(out, "biocore_generated_at", p.generated_at_utc);
    meta(out, "biocore_pipeline_id", p.pipeline.pipeline_id);
    meta(out, "biocore_pipeline_version", p.pipeline.pipeline_version);
    if (p.pipeline.job_id) meta(out, "biocore_job_id", *p.pipeline.job_id);
    meta(out, "biocore_reference_id", p.reference.reference_id);
    meta(out, "biocore_reference_assembly", assembly_label(p.reference.assembly));
    meta(out, "biocore_reference_sha256", p.reference.source_sha256);
    for (const auto& db : p.databases) {
        meta(out, "biocore_database", db.database_id + "|" + db.version + "|" +
            std::to_string(db.schema_version) + "|" + assembly_label(db.assembly) + "|" + db.source_sha256);
    }
    std::set<std::string> contigs;
    for (std::size_t i = 0; i < plan.row_count(); ++i) {
        if (!vcf_contig_safe(plan.row(i).contig_name)) throw std::invalid_argument("Variant export VCF contains an unsafe contig name");
        contigs.insert(plan.row(i).contig_name);
    }
    for (const auto& c : contigs) out << "##contig=<ID=" << c << ">\n";
    out << "##INFO=<ID=BC_TYPE,Number=1,Type=String,Description=\"OpenGenesis-BioCore canonical variant type\">\n"
        << "##INFO=<ID=BC_CARRIERS,Number=1,Type=Integer,Description=\"Complete-call samples carrying the selected alternate allele\">\n"
        << "##INFO=<ID=BC_COMPLETE,Number=1,Type=Integer,Description=\"Complete sample calls represented for this allele\">\n"
        << "##INFO=<ID=BC_ANN_ID,Number=1,Type=String,Description=\"Primary annotation record identifier when available\">\n"
        << "##INFO=<ID=BC_FEATURE,Number=1,Type=String,Description=\"Primary overlapping feature type when available\">\n"
        << "##INFO=<ID=BC_AOR_KIND,Number=1,Type=String,Description=\"Allele odds-ratio state\">\n"
        << "##INFO=<ID=BC_AOR,Number=1,Type=Float,Description=\"Allele odds ratio when finite\">\n"
        << "##INFO=<ID=BC_AOR_CI95_L,Number=1,Type=Float,Description=\"Allele odds-ratio 95% CI lower bound\">\n"
        << "##INFO=<ID=BC_AOR_CI95_U,Number=1,Type=Float,Description=\"Allele odds-ratio 95% CI upper bound\">\n"
        << "##INFO=<ID=BC_AP,Number=1,Type=Float,Description=\"Allele Fisher exact two-sided P value\">\n"
        << "##INFO=<ID=BC_AQ,Number=1,Type=Float,Description=\"Allele Benjamini-Hochberg adjusted q value\">\n"
        << "##INFO=<ID=BC_COR_KIND,Number=1,Type=String,Description=\"Carrier odds-ratio state\">\n"
        << "##INFO=<ID=BC_COR,Number=1,Type=Float,Description=\"Carrier odds ratio when finite\">\n"
        << "##INFO=<ID=BC_COR_CI95_L,Number=1,Type=Float,Description=\"Carrier odds-ratio 95% CI lower bound\">\n"
        << "##INFO=<ID=BC_COR_CI95_U,Number=1,Type=Float,Description=\"Carrier odds-ratio 95% CI upper bound\">\n"
        << "##INFO=<ID=BC_CP,Number=1,Type=Float,Description=\"Carrier Fisher exact two-sided P value\">\n"
        << "##INFO=<ID=BC_CQ,Number=1,Type=Float,Description=\"Carrier Benjamini-Hochberg adjusted q value\">\n"
        << "#CHROM\tPOS\tID\tREF\tALT\tQUAL\tFILTER\tINFO\n";
    for (std::size_t i = 0; i < plan.row_count(); ++i) {
        const auto& r = plan.row(i);
        if (r.start == std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("Variant export VCF coordinate exceeds 1-based POS capacity");
        out << r.contig_name << '\t' << (r.start + 1U) << '\t';
        if (r.record_id && vcf_identifier_safe(*r.record_id)) out << *r.record_id; else out << '.';
        out << '\t' << r.reference_allele << '\t' << r.alternate_allele << '\t'; write_optional_double(out, r.quality); out << '\t';
        out << ((r.filter_status == "." || r.filter_status == "PASS" || vcf_identifier_safe(r.filter_status)) ? r.filter_status : ".");
        out << "\tBC_TYPE=" << percent_encode(domain::to_string(r.variant_type))
            << ";BC_CARRIERS=" << r.carrier_sample_count << ";BC_COMPLETE=" << r.total_complete_calls;
        if (r.primary_annotation_id) out << ";BC_ANN_ID=" << percent_encode(*r.primary_annotation_id);
        if (r.primary_feature_type) out << ";BC_FEATURE=" << percent_encode(*r.primary_feature_type);
        if (r.association_available) {
            out << ";BC_AOR_KIND=" << odds_kind(r.allele_odds_ratio.kind); info(out, "BC_AOR", odds_value(r.allele_odds_ratio));
            info(out, "BC_AOR_CI95_L", r.allele_odds_ratio_ci95 ? std::optional<double>{r.allele_odds_ratio_ci95->lower} : std::nullopt);
            info(out, "BC_AOR_CI95_U", r.allele_odds_ratio_ci95 ? std::optional<double>{r.allele_odds_ratio_ci95->upper} : std::nullopt);
            info(out, "BC_AP", r.allele_fisher_two_sided_p); info(out, "BC_AQ", r.allele_bh_adjusted_q);
            out << ";BC_COR_KIND=" << odds_kind(r.carrier_odds_ratio.kind); info(out, "BC_COR", odds_value(r.carrier_odds_ratio));
            info(out, "BC_COR_CI95_L", r.carrier_odds_ratio_ci95 ? std::optional<double>{r.carrier_odds_ratio_ci95->lower} : std::nullopt);
            info(out, "BC_COR_CI95_U", r.carrier_odds_ratio_ci95 ? std::optional<double>{r.carrier_odds_ratio_ci95->upper} : std::nullopt);
            info(out, "BC_CP", r.carrier_fisher_two_sided_p); info(out, "BC_CQ", r.carrier_bh_adjusted_q);
        }
        out << '\n';
    }
    require_stream(out);
}

std::string render_variant_export_vcf(const VariantExportPlan& plan) { std::ostringstream out; write_variant_export_vcf(out, plan); return out.str(); }

}  // namespace biocore::presentation
