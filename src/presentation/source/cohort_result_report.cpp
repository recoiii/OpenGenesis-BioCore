#include "biocore/presentation/cohort_result_report.hpp"

#include <array>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace biocore::presentation {
namespace {

[[nodiscard]] std::string escape_json(const std::string_view value) {
    std::string out;
    out.reserve(value.size());
    constexpr std::array hex{'0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f'};
    for (const char raw : value) {
        const auto c = static_cast<unsigned char>(raw);
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20U) {
                    out += "\\u00";
                    out += hex[(c >> 4U) & 0x0fU];
                    out += hex[c & 0x0fU];
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

[[nodiscard]] std::string quote_json(const std::string_view value) {
    return "\"" + escape_json(value) + "\"";
}

[[nodiscard]] std::string optional_number_json(const std::optional<double>& value) {
    if (!value.has_value()) return "null";
    std::ostringstream stream;
    stream << std::setprecision(17) << *value;
    return stream.str();
}

[[nodiscard]] std::string odds_kind(const domain::AssociationOddsRatioKind kind) {
    switch (kind) {
        case domain::AssociationOddsRatioKind::undefined: return "undefined";
        case domain::AssociationOddsRatioKind::zero: return "zero";
        case domain::AssociationOddsRatioKind::finite: return "finite";
        case domain::AssociationOddsRatioKind::positive_infinity: return "positive-infinity";
    }
    return "undefined";
}

[[nodiscard]] std::string model_json(
    const application::CohortAssociationModelResult& model
) {
    std::string ci{"null"};
    if (model.odds_ratio_ci95.has_value()) {
        std::ostringstream stream;
        stream << std::setprecision(17)
               << "{\"lower\":" << model.odds_ratio_ci95->lower
               << ",\"upper\":" << model.odds_ratio_ci95->upper << "}";
        ci = stream.str();
    }
    std::ostringstream odds_value;
    odds_value << std::setprecision(17) << model.odds_ratio.value;
    return "{\"familyMember\":" + std::string{model.family_member ? "true" : "false"} +
           ",\"table\":{\"caseExposed\":" + std::to_string(model.table.case_exposed) +
           ",\"caseUnexposed\":" + std::to_string(model.table.case_unexposed) +
           ",\"controlExposed\":" + std::to_string(model.table.control_exposed) +
           ",\"controlUnexposed\":" + std::to_string(model.table.control_unexposed) + "}" +
           ",\"oddsRatio\":{\"kind\":" + quote_json(odds_kind(model.odds_ratio.kind)) +
           ",\"value\":" + odds_value.str() + "}" +
           ",\"ci95\":" + ci +
           ",\"fisherTwoSidedP\":" + optional_number_json(model.fisher_two_sided_p) +
           ",\"bhAdjustedQ\":" + optional_number_json(model.bh_adjusted_q) + "}";
}

[[nodiscard]] std::string annotation_json(
    const std::optional<application::CohortResultAnnotationLink>& annotation
) {
    if (!annotation.has_value()) return "null";
    std::string databases{"["};
    for (std::size_t i = 0U; i < annotation->database_ids.size(); ++i) {
        if (i != 0U) databases += ',';
        databases += quote_json(annotation->database_ids[i]);
    }
    databases += ']';
    return "{\"databaseIds\":" + databases +
           ",\"primaryRecordId\":" +
           (annotation->primary_record_id.has_value()
                ? quote_json(*annotation->primary_record_id) : "null") +
           ",\"primaryFeatureType\":" +
           (annotation->primary_feature_type.has_value()
                ? quote_json(*annotation->primary_feature_type) : "null") + "}";
}

[[nodiscard]] std::string row_json(const application::CohortResultRow& row) {
    const auto& v = row.association;
    return "{\"ordinal\":" + std::to_string(v.ordinal) +
           ",\"contig\":" + quote_json(v.contig) +
           ",\"start\":" + std::to_string(v.start) +
           ",\"end\":" + std::to_string(v.end) +
           ",\"reference\":" + quote_json(v.reference) +
           ",\"alternate\":" + quote_json(v.alternate) +
           ",\"totalCases\":" + std::to_string(v.total_cases) +
           ",\"totalControls\":" + std::to_string(v.total_controls) +
           ",\"caseUnobserved\":" + std::to_string(v.case_unobserved) +
           ",\"controlUnobserved\":" + std::to_string(v.control_unobserved) +
           ",\"caseNoCalls\":" + std::to_string(v.case_no_calls) +
           ",\"controlNoCalls\":" + std::to_string(v.control_no_calls) +
           ",\"casePartialCalls\":" + std::to_string(v.case_partial_calls) +
           ",\"controlPartialCalls\":" + std::to_string(v.control_partial_calls) +
           ",\"effectiveCaseSamples\":" + std::to_string(v.effective_case_samples) +
           ",\"effectiveControlSamples\":" + std::to_string(v.effective_control_samples) +
           ",\"allele\":" + model_json(v.allele) +
           ",\"carrier\":" + model_json(v.carrier) +
           ",\"annotation\":" + annotation_json(row.annotation) + "}";
}

[[nodiscard]] std::string escape_delimited(
    const std::string_view value,
    const char delimiter
) {
    const bool quote = value.find(delimiter) != std::string_view::npos ||
                       value.find('"') != std::string_view::npos ||
                       value.find('\n') != std::string_view::npos ||
                       value.find('\r') != std::string_view::npos ||
                       value.find('#') == 0U;
    if (!quote) return std::string{value};
    std::string out{"\""};
    for (const char c : value) {
        if (c == '"') out += "\"";
        out += c;
    }
    out += '"';
    return out;
}

[[nodiscard]] std::string optional_number_text(const std::optional<double>& value) {
    if (!value.has_value()) return {};
    std::ostringstream stream;
    stream << std::setprecision(17) << *value;
    return stream.str();
}

[[nodiscard]] std::string odds_value_text(const domain::AssociationOddsRatio& odds) {
    if (odds.kind == domain::AssociationOddsRatioKind::positive_infinity) return "inf";
    if (odds.kind == domain::AssociationOddsRatioKind::undefined) return {};
    std::ostringstream stream;
    stream << std::setprecision(17) << odds.value;
    return stream.str();
}

[[nodiscard]] std::string databases_text(
    const std::optional<application::CohortResultAnnotationLink>& annotation
) {
    if (!annotation.has_value()) return {};
    std::string result;
    for (std::size_t i = 0U; i < annotation->database_ids.size(); ++i) {
        if (i != 0U) result += '|';
        result += annotation->database_ids[i];
    }
    return result;
}

[[nodiscard]] std::string render_delimited(
    const application::CohortResultPackage& package,
    const char delimiter
) {
    std::string out;
    const auto add_meta = [&](const std::string_view key, const std::string_view value) {
        out += "# ";
        out += key;
        out += '=';
        out += value;
        out += '\n';
    };
    add_meta("snapshot_digest", package.snapshot_digest);
    add_meta("filter_definition", package.filter_definition);
    add_meta("analysis_id", package.analysis_id);
    add_meta("reference_sha256", package.reference_sha256);
    add_meta("association_contract_version", package.association_contract_version);
    add_meta("test_filter_version", package.test_filter_version);
    add_meta("no_call_policy", package.no_call_policy);
    add_meta("allele_family_size", std::to_string(package.allele_family_size));
    add_meta("carrier_family_size", std::to_string(package.carrier_family_size));
    add_meta("excluded_samples", std::to_string(package.excluded_samples));
    for (std::size_t index = 0U; index < package.exclusions.size(); ++index) {
        add_meta(
            "exclusion_" + std::to_string(index) + "_sample_id",
            quote_json(package.exclusions[index].sample_id)
        );
        add_meta(
            "exclusion_" + std::to_string(index) + "_reason",
            package.exclusions[index].reason.has_value()
                ? quote_json(*package.exclusions[index].reason) : "null"
        );
    }
    add_meta("statistical_test", package.statistical_test);
    add_meta("effect_measure", package.effect_measure);
    add_meta("confidence_interval_method", package.confidence_interval_method);
    add_meta("multiple_testing_method", package.multiple_testing_method);

    const std::array<std::string_view, 24> headers{
        "ordinal","contig","start","end","reference","alternate",
        "effective_cases","effective_controls",
        "allele_family","allele_or_kind","allele_or","allele_p","allele_q",
        "carrier_family","carrier_or_kind","carrier_or","carrier_p","carrier_q",
        "case_carriers","control_carriers","annotation_databases",
        "primary_record_id","primary_feature_type","snapshot_digest"
    };
    for (std::size_t i = 0U; i < headers.size(); ++i) {
        if (i != 0U) out += delimiter;
        out += headers[i];
    }
    out += '\n';

    for (const auto& row : package.rows) {
        const auto& v = row.association;
        const std::array<std::string, 24> values{
            std::to_string(v.ordinal), v.contig, std::to_string(v.start),
            std::to_string(v.end), v.reference, v.alternate,
            std::to_string(v.effective_case_samples),
            std::to_string(v.effective_control_samples),
            v.allele.family_member ? "1" : "0",
            odds_kind(v.allele.odds_ratio.kind),
            odds_value_text(v.allele.odds_ratio),
            optional_number_text(v.allele.fisher_two_sided_p),
            optional_number_text(v.allele.bh_adjusted_q),
            v.carrier.family_member ? "1" : "0",
            odds_kind(v.carrier.odds_ratio.kind),
            odds_value_text(v.carrier.odds_ratio),
            optional_number_text(v.carrier.fisher_two_sided_p),
            optional_number_text(v.carrier.bh_adjusted_q),
            std::to_string(v.carrier.table.case_exposed),
            std::to_string(v.carrier.table.control_exposed),
            databases_text(row.annotation),
            row.annotation.has_value() && row.annotation->primary_record_id.has_value()
                ? *row.annotation->primary_record_id : std::string{},
            row.annotation.has_value() && row.annotation->primary_feature_type.has_value()
                ? *row.annotation->primary_feature_type : std::string{},
            package.snapshot_digest
        };
        for (std::size_t i = 0U; i < values.size(); ++i) {
            if (i != 0U) out += delimiter;
            out += escape_delimited(values[i], delimiter);
        }
        out += '\n';
    }
    return out;
}

[[nodiscard]] std::string escape_html(const std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (const char c : value) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out += c;
        }
    }
    return out;
}

[[nodiscard]] std::string display_number(const std::optional<double>& value) {
    return value.has_value() ? optional_number_text(value) : "—";
}

}  // namespace

std::string render_cohort_result_package_json(
    const application::CohortResultPackage& package
) {
    std::string rows{"["};
    for (std::size_t i = 0U; i < package.rows.size(); ++i) {
        if (i != 0U) rows += ',';
        rows += row_json(package.rows[i]);
    }
    rows += ']';

    std::string exclusions{"["};
    for (std::size_t i = 0U; i < package.exclusions.size(); ++i) {
        if (i != 0U) exclusions += ',';
        exclusions += "{\"sampleId\":" + quote_json(package.exclusions[i].sample_id) +
                      ",\"reason\":" +
                      (package.exclusions[i].reason.has_value()
                           ? quote_json(*package.exclusions[i].reason) : "null") + "}";
    }
    exclusions += ']';

    return "{\"schemaVersion\":" + std::to_string(package.schema_version) +
           ",\"producer\":{\"name\":" + quote_json(package.producer_name) +
           ",\"version\":" + quote_json(package.producer_version) + "}" +
           ",\"generatedAtUtc\":" + quote_json(package.generated_at_utc) +
           ",\"projectId\":" + quote_json(package.project_id) +
           ",\"cohortId\":" + quote_json(package.cohort_id) +
           ",\"cohortRevision\":" + std::to_string(package.cohort_revision) +
           ",\"analysisId\":" + quote_json(package.analysis_id) +
           ",\"snapshotDigest\":" + quote_json(package.snapshot_digest) +
           ",\"filterDefinition\":" + quote_json(package.filter_definition) +
           ",\"reference\":{\"fileId\":" + quote_json(package.reference_file_id) +
           ",\"sha256\":" + quote_json(package.reference_sha256) +
           ",\"assembly\":" + quote_json(package.reference_assembly) + "}" +
           ",\"approvedCaseSamples\":" + std::to_string(package.approved_case_samples) +
           ",\"approvedControlSamples\":" + std::to_string(package.approved_control_samples) +
           ",\"excludedSamples\":" + std::to_string(package.excluded_samples) +
           ",\"exclusions\":" + exclusions +
           ",\"contracts\":{\"association\":" + quote_json(package.association_contract_version) +
           ",\"testFilter\":" + quote_json(package.test_filter_version) + "}" +
           ",\"testFamilies\":{\"allele\":" + std::to_string(package.allele_family_size) +
           ",\"carrier\":" + std::to_string(package.carrier_family_size) + "}" +
           ",\"methods\":{\"noCallPolicy\":" + quote_json(package.no_call_policy) +
           ",\"statisticalTest\":" + quote_json(package.statistical_test) +
           ",\"effectMeasure\":" + quote_json(package.effect_measure) +
           ",\"confidenceInterval\":" + quote_json(package.confidence_interval_method) +
           ",\"multipleTesting\":" + quote_json(package.multiple_testing_method) + "}" +
           ",\"totalUnfilteredVariants\":" + std::to_string(package.total_unfiltered_variants) +
           ",\"totalMatchedVariants\":" + std::to_string(package.total_matched_variants) +
           ",\"rows\":" + rows + "}";
}

std::string render_cohort_result_package_csv(
    const application::CohortResultPackage& package
) {
    return render_delimited(package, ',');
}

std::string render_cohort_result_package_tsv(
    const application::CohortResultPackage& package
) {
    return render_delimited(package, '\t');
}

std::string render_cohort_result_package_html(
    const application::CohortResultPackage& package
) {
    std::string rows;
    for (const auto& row : package.rows) {
        const auto& v = row.association;
        rows += "<tr><td>" + std::to_string(v.ordinal) + "</td><td>" +
                escape_html(v.contig) + ":" + std::to_string(v.start) + "</td><td>" +
                escape_html(v.reference) + "&gt;" + escape_html(v.alternate) + "</td><td>" +
                std::to_string(v.effective_case_samples) + "</td><td>" +
                std::to_string(v.effective_control_samples) + "</td><td>" +
                escape_html(odds_kind(v.allele.odds_ratio.kind)) + " " +
                escape_html(odds_value_text(v.allele.odds_ratio)) + "</td><td>" +
                escape_html(display_number(v.allele.fisher_two_sided_p)) + "</td><td>" +
                escape_html(display_number(v.allele.bh_adjusted_q)) + "</td><td>" +
                escape_html(odds_kind(v.carrier.odds_ratio.kind)) + " " +
                escape_html(odds_value_text(v.carrier.odds_ratio)) + "</td><td>" +
                escape_html(display_number(v.carrier.fisher_two_sided_p)) + "</td><td>" +
                escape_html(display_number(v.carrier.bh_adjusted_q)) + "</td><td>" +
                escape_html(databases_text(row.annotation)) + "</td></tr>";
    }
    if (rows.empty()) {
        rows = "<tr><td colspan=\"12\">No variants match the selected view filter.</td></tr>";
    }

    std::string exclusion_rows;
    for (const auto& exclusion : package.exclusions) {
        exclusion_rows += "<tr><td>" + escape_html(exclusion.sample_id) + "</td><td>" +
                          (exclusion.reason.has_value()
                               ? escape_html(*exclusion.reason) : "—") + "</td></tr>";
    }
    if (exclusion_rows.empty()) {
        exclusion_rows = "<tr><td colspan=\"2\">No analysis exclusions.</td></tr>";
    }

    return "<!doctype html><html><head><meta charset=\"utf-8\">"
           "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
           "<title>OpenGenesis-BioCore Cohort Report " + escape_html(package.analysis_id) +
           "</title><style>body{font-family:system-ui,sans-serif;margin:2rem;line-height:1.45;color:#111}"
           "main{max-width:1400px;margin:auto}table{border-collapse:collapse;width:100%;margin:1rem 0 2rem}"
           "th,td{border:1px solid #bbb;padding:.4rem;text-align:left;vertical-align:top}"
           "th{background:#f2f2f2}code{word-break:break-all}.meta{display:grid;grid-template-columns:max-content 1fr;gap:.35rem 1rem}"
           "</style></head><body><main><h1>OpenGenesis-BioCore Cohort Results Report</h1>"
           "<dl class=\"meta\"><dt>Project</dt><dd>" + escape_html(package.project_id) +
           "</dd><dt>Cohort</dt><dd>" + escape_html(package.cohort_id) +
           " revision " + std::to_string(package.cohort_revision) +
           "</dd><dt>Analysis</dt><dd>" + escape_html(package.analysis_id) +
           "</dd><dt>Snapshot digest</dt><dd><code>" + escape_html(package.snapshot_digest) +
           "</code></dd><dt>View filter</dt><dd><code>" + escape_html(package.filter_definition) +
           "</code></dd><dt>Reference</dt><dd>" + escape_html(package.reference_assembly) +
           " · " + escape_html(package.reference_file_id) + " · <code>" +
           escape_html(package.reference_sha256) +
           "</code></dd><dt>Approved groups</dt><dd>cases=" +
           std::to_string(package.approved_case_samples) + ", controls=" +
           std::to_string(package.approved_control_samples) +
           ", excluded=" + std::to_string(package.excluded_samples) +
           "</dd><dt>Association contract</dt><dd>" +
           escape_html(package.association_contract_version) +
           "</dd><dt>Test filter</dt><dd>" + escape_html(package.test_filter_version) +
           "</dd><dt>No-call policy</dt><dd>" + escape_html(package.no_call_policy) +
           "</dd><dt>Frozen test families</dt><dd>allele=" +
           std::to_string(package.allele_family_size) + ", carrier=" +
           std::to_string(package.carrier_family_size) +
           "</dd><dt>Statistical test</dt><dd>" + escape_html(package.statistical_test) +
           "</dd><dt>Effect measure</dt><dd>" + escape_html(package.effect_measure) +
           "</dd><dt>95% CI</dt><dd>" + escape_html(package.confidence_interval_method) +
           "</dd><dt>Multiple testing</dt><dd>" +
           escape_html(package.multiple_testing_method) +
           "</dd><dt>Variants</dt><dd>" + std::to_string(package.total_matched_variants) +
           " matched of " + std::to_string(package.total_unfiltered_variants) +
           "</dd></dl><p><strong>Display filters do not redefine the frozen statistical test universe.</strong></p>"
           "<h2>Analysis exclusions</h2><table><thead><tr><th>Sample</th><th>Reason</th>"
           "</tr></thead><tbody>" + exclusion_rows + "</tbody></table><h2>Variants</h2>"
           "<table><thead><tr><th>#</th><th>Locus</th><th>Allele</th><th>Effective cases</th>"
           "<th>Effective controls</th><th>Allele OR</th><th>Allele p</th><th>Allele q</th>"
           "<th>Carrier OR</th><th>Carrier p</th><th>Carrier q</th><th>Annotation DBs</th>"
           "</tr></thead><tbody>" + rows + "</tbody></table></main></body></html>";
}

}  // namespace biocore::presentation
