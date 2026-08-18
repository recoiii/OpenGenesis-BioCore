#pragma once
#include <cstdint>
#include <istream>
#include <string>
#include <unordered_map>
#include <vector>

namespace biocore::variant_annotation {
struct AnnotationRecord {
    std::string gene;
    std::string consequence;
    std::string clinical_significance;
    std::string source;
    std::string source_id;
};
struct AnnotationStatistics {
    std::uint64_t annotation_rows{};
    std::uint64_t vcf_records{};
    std::uint64_t alt_alleles{};
    std::uint64_t annotation_hits{};
    std::uint64_t annotation_misses{};
    std::uint64_t pass_records{};
    std::uint64_t filtered_records{};
    std::unordered_map<std::string, std::uint64_t> genes;
    std::unordered_map<std::string, std::uint64_t> consequences;
    std::unordered_map<std::string, std::uint64_t> clinical_significance;
};
struct AnnotationResult {
    AnnotationStatistics statistics;
    std::string annotated_vcf;
    std::string table_tsv;
    std::string report_html;
};
std::unordered_map<std::string, AnnotationRecord> load_annotation_table(std::istream& input, std::uint64_t& rows);
AnnotationResult annotate_vcf(std::istream& vcf, const std::unordered_map<std::string, AnnotationRecord>& annotations, std::uint64_t annotation_rows);
std::string render_annotation_json(const AnnotationStatistics& statistics);
}
