#include "vcf_qc.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace biocore::vcf_qc {
namespace {

constexpr std::size_t maximum_vcf_line_bytes = 64U * 1024U * 1024U;
constexpr std::size_t maximum_alt_alleles_per_record = 4096U;
constexpr std::size_t maximum_records = 50000000U;

[[nodiscard]] bool read_bounded_line(std::istream& input, std::string& line) {
    line.clear();
    bool saw = false;
    for (;;) {
        const int next = input.get();
        if (next == std::char_traits<char>::eof()) {
            if (input.bad()) throw std::runtime_error("Unable to read VCF input");
            return saw;
        }
        saw = true;
        const char value = static_cast<char>(next);
        if (value == '\n') break;
        if (line.size() >= maximum_vcf_line_bytes) throw std::invalid_argument("VCF line exceeds the safety limit");
        line.push_back(value);
    }
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return true;
}

void strip_utf8_bom(std::string& line) {
    if (line.size() >= 3U && static_cast<unsigned char>(line[0]) == 0xEFU &&
        static_cast<unsigned char>(line[1]) == 0xBBU && static_cast<unsigned char>(line[2]) == 0xBFU) {
        line.erase(0U, 3U);
    }
}

[[nodiscard]] std::vector<std::string_view> split(const std::string_view value, const char separator) {
    std::vector<std::string_view> fields;
    std::size_t begin = 0U;
    for (;;) {
        const auto end = value.find(separator, begin);
        fields.push_back(value.substr(begin, end == std::string_view::npos ? value.size() - begin : end - begin));
        if (end == std::string_view::npos) break;
        begin = end + 1U;
    }
    return fields;
}

[[nodiscard]] std::vector<std::string_view> split_tabs(const std::string& line) {
    return split(line, '\t');
}

template <typename T>
[[nodiscard]] T parse_unsigned(const std::string_view value, const std::string_view label) {
    if (value.empty() || value.front() == '-') throw std::invalid_argument(std::string{label} + " is invalid");
    unsigned long long parsed = 0U;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed, 10);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || parsed > std::numeric_limits<T>::max()) {
        throw std::invalid_argument(std::string{label} + " is invalid");
    }
    return static_cast<T>(parsed);
}

[[nodiscard]] double parse_number(const std::string_view value, const std::string_view label) {
    double parsed = 0.0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed, std::chars_format::general);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || !std::isfinite(parsed)) {
        throw std::invalid_argument(std::string{label} + " is invalid");
    }
    return parsed;
}

[[nodiscard]] bool safe_token(const std::string_view value) {
    if (value.empty()) return false;
    for (const char c : value) {
        if (c <= 0x20 || c == 0x7F || c == ';' || c == '\t') return false;
    }
    return true;
}

[[nodiscard]] std::string json_escape(const std::string_view value) {
    std::string out;
    out.reserve(value.size() + 8U);
    static constexpr char hex[] = "0123456789abcdef";
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
                    out.push_back(hex[(c >> 4U) & 0x0FU]);
                    out.push_back(hex[c & 0x0FU]);
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    return out;
}

[[nodiscard]] std::string tsv_escape(std::string value) {
    for (char& c : value) if (c == '\t' || c == '\r' || c == '\n') c = ' ';
    return value;
}

[[nodiscard]] std::unordered_map<std::string, std::string> parse_info(const std::string_view value) {
    std::unordered_map<std::string, std::string> info;
    if (value == ".") return info;
    for (const auto field : split(value, ';')) {
        if (field.empty()) throw std::invalid_argument("VCF INFO contains an empty field");
        const auto equal = field.find('=');
        const auto key = field.substr(0U, equal);
        if (!safe_token(key) || key.find('=') != std::string_view::npos) throw std::invalid_argument("VCF INFO key is invalid");
        const std::string key_string{key};
        const std::string val = equal == std::string_view::npos ? std::string{} : std::string{field.substr(equal + 1U)};
        if (!info.emplace(key_string, val).second) throw std::invalid_argument("VCF INFO key is duplicated");
    }
    return info;
}

[[nodiscard]] std::optional<std::uint64_t> scalar_u64(
    const std::unordered_map<std::string, std::string>& info,
    const std::string_view key
) {
    const auto it = info.find(std::string{key});
    if (it == info.end()) return std::nullopt;
    if (it->second.empty() || it->second.find(',') != std::string::npos) throw std::invalid_argument("VCF INFO " + std::string{key} + " must be a scalar integer");
    return parse_unsigned<std::uint64_t>(it->second, std::string{"VCF INFO "} + std::string{key});
}

[[nodiscard]] std::vector<std::uint64_t> allele_u64(
    const std::unordered_map<std::string, std::string>& info,
    const std::string_view key,
    const std::size_t allele_count
) {
    const auto it = info.find(std::string{key});
    if (it == info.end()) return {};
    const auto fields = split(it->second, ',');
    if (fields.size() != allele_count) throw std::invalid_argument("VCF INFO " + std::string{key} + " cardinality does not match ALT alleles");
    std::vector<std::uint64_t> values;
    values.reserve(fields.size());
    for (const auto field : fields) values.push_back(parse_unsigned<std::uint64_t>(field, std::string{"VCF INFO "} + std::string{key}));
    return values;
}

[[nodiscard]] std::vector<double> allele_number(
    const std::unordered_map<std::string, std::string>& info,
    const std::string_view key,
    const std::size_t allele_count,
    const double minimum,
    const double maximum
) {
    const auto it = info.find(std::string{key});
    if (it == info.end()) return {};
    const auto fields = split(it->second, ',');
    if (fields.size() != allele_count) throw std::invalid_argument("VCF INFO " + std::string{key} + " cardinality does not match ALT alleles");
    std::vector<double> values;
    values.reserve(fields.size());
    for (const auto field : fields) {
        const double value = parse_number(field, std::string{"VCF INFO "} + std::string{key});
        if (value < minimum || value > maximum) throw std::invalid_argument("VCF INFO " + std::string{key} + " is outside its valid range");
        values.push_back(value);
    }
    return values;
}

[[nodiscard]] bool symbolic_alt(const std::string_view alt) {
    return (alt.size() >= 2U && alt.front() == '<' && alt.back() == '>') ||
           alt.find('[') != std::string_view::npos || alt.find(']') != std::string_view::npos || alt == "*";
}

[[nodiscard]] std::string variant_type(const std::string_view ref, const std::string_view alt) {
    if (symbolic_alt(alt)) return "symbolic";
    if (ref.size() == 1U && alt.size() == 1U) return "snv";
    if (ref.size() == alt.size() && ref.size() > 1U) return "mnv";
    if (alt.size() > ref.size()) return "insertion_like";
    if (alt.size() < ref.size()) return "deletion_like";
    return "complex";
}

[[nodiscard]] char upper(const char c) noexcept {
    return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
}

[[nodiscard]] bool canonical(const char c) noexcept {
    const char u = upper(c);
    return u == 'A' || u == 'C' || u == 'G' || u == 'T';
}

[[nodiscard]] bool transition(const char ref, const char alt) noexcept {
    const char r = upper(ref);
    const char a = upper(alt);
    return (r == 'A' && a == 'G') || (r == 'G' && a == 'A') ||
           (r == 'C' && a == 'T') || (r == 'T' && a == 'C');
}

[[nodiscard]] std::vector<std::string> input_filters(const std::string_view filter) {
    if (filter == "PASS") return {};
    if (filter == ".") return {"BioCoreUnfiltered"};
    const auto fields = split(filter, ';');
    std::vector<std::string> result;
    result.reserve(fields.size());
    std::set<std::string> seen;
    for (const auto field : fields) {
        if (!safe_token(field) || field == "PASS" || field == ".") throw std::invalid_argument("VCF FILTER field is invalid");
        std::string value{field};
        if (!seen.insert(value).second) throw std::invalid_argument("VCF FILTER id is duplicated");
        result.push_back(std::move(value));
    }
    return result;
}

void add_reason(std::vector<std::string>& reasons, VcfQcStatistics& statistics, const std::string& reason) {
    if (std::find(reasons.begin(), reasons.end(), reason) == reasons.end()) reasons.push_back(reason);
    ++statistics.filter_reason_counts[reason];
}

[[nodiscard]] std::string join_semicolon(const std::vector<std::string>& values) {
    if (values.empty()) return "PASS";
    std::ostringstream out;
    for (std::size_t i = 0U; i < values.size(); ++i) {
        if (i != 0U) out << ';';
        out << values[i];
    }
    return out.str();
}

[[nodiscard]] bool header_has_filter(const std::vector<std::string>& headers, const std::string_view id) {
    const std::string needle = "##FILTER=<ID=" + std::string{id} + ",";
    return std::any_of(headers.begin(), headers.end(), [&](const std::string& line) { return line.rfind(needle, 0U) == 0U; });
}

[[nodiscard]] std::vector<std::pair<std::string, std::string>> biocore_filter_definitions() {
    return {
        {"BioCoreUnfiltered", "Input FILTER was '.' (filters not applied)"},
        {"BioCoreMissingDP", "Required INFO/DP metric is missing"},
        {"BioCoreLowDP", "INFO/DP is below the configured minimum depth"},
        {"BioCoreMissingAC", "Required INFO/AC metric is missing"},
        {"BioCoreLowAC", "INFO/AC is below the configured minimum alternate count"},
        {"BioCoreMissingAF", "Required INFO/AF metric is missing"},
        {"BioCoreLowAF", "INFO/AF is below the configured minimum alternate fraction"},
        {"BioCoreMissingABQ", "Required INFO/ABQ metric is missing"},
        {"BioCoreLowABQ", "INFO/ABQ is below the configured minimum alternate base quality"},
    };
}

}  // namespace

void validate_vcf_qc_options(const VcfQcOptions& options) {
    if (options.minimum_depth < 1U || options.minimum_depth > 1000000U) throw std::invalid_argument("Minimum VCF depth must be 1..1000000");
    if (options.minimum_alt_count < 1U || options.minimum_alt_count > 1000000U) throw std::invalid_argument("Minimum VCF ALT count must be 1..1000000");
    if (!std::isfinite(options.minimum_alt_fraction) || options.minimum_alt_fraction < 0.01 || options.minimum_alt_fraction > 1.0) throw std::invalid_argument("Minimum VCF ALT fraction must be 0.01..1.0");
    if (!std::isfinite(options.minimum_alt_base_quality) || options.minimum_alt_base_quality < 0.0 || options.minimum_alt_base_quality > 93.0) throw std::invalid_argument("Minimum VCF ALT base quality must be 0..93");
}

VcfQcResult process_vcf(std::istream& input, const VcfQcOptions& options) {
    validate_vcf_qc_options(options);
    VcfQcResult result;
    std::vector<std::string> metadata;
    std::string column_header;
    std::vector<std::string> output_records;
    std::string line;
    bool first_line = true;
    bool saw_fileformat = false;
    bool saw_column_header = false;

    while (read_bounded_line(input, line)) {
        if (first_line) {
            strip_utf8_bom(line);
            first_line = false;
        }
        if (!saw_column_header && line.rfind("##", 0U) == 0U) {
            if (line.rfind("##fileformat=", 0U) == 0U) {
                if (saw_fileformat) throw std::invalid_argument("VCF fileformat header is duplicated");
                const std::string version = line.substr(std::string{"##fileformat="}.size());
                if (version != "VCFv4.2" && version != "VCFv4.3") throw std::invalid_argument("Only VCFv4.2 and VCFv4.3 are supported");
                result.statistics.file_format = version;
                saw_fileformat = true;
            }
            metadata.push_back(line);
            continue;
        }
        if (!saw_column_header && line.rfind("#CHROM\t", 0U) == 0U) {
            if (!saw_fileformat) throw std::invalid_argument("VCF fileformat header must precede #CHROM");
            const auto fields = split_tabs(line);
            if (fields.size() < 8U || fields[0] != "#CHROM" || fields[1] != "POS" || fields[2] != "ID" ||
                fields[3] != "REF" || fields[4] != "ALT" || fields[5] != "QUAL" || fields[6] != "FILTER" || fields[7] != "INFO") {
                throw std::invalid_argument("VCF #CHROM header is invalid");
            }
            column_header = line;
            saw_column_header = true;
            continue;
        }
        if (!saw_column_header) throw std::invalid_argument("VCF metadata/header structure is invalid");
        if (line.empty() || line.front() == '#') throw std::invalid_argument("VCF header/comment appears after #CHROM or record is empty");
        if (result.statistics.total_records >= maximum_records) throw std::invalid_argument("VCF record count exceeds the safety limit");

        auto fields = split_tabs(line);
        if (fields.size() < 8U) throw std::invalid_argument("VCF record has fewer than eight fields");
        const std::string chrom{fields[0]};
        if (!safe_token(chrom)) throw std::invalid_argument("VCF CHROM is invalid");
        const auto position = parse_unsigned<std::uint64_t>(fields[1], "VCF POS");
        if (position == 0U) throw std::invalid_argument("VCF POS must be positive");
        const std::string id{fields[2]};
        const std::string reference{fields[3]};
        if (reference.empty() || reference == ".") throw std::invalid_argument("VCF REF is invalid");
        const auto alt_views = split(fields[4], ',');
        if (alt_views.empty() || alt_views.size() > maximum_alt_alleles_per_record) throw std::invalid_argument("VCF ALT count is invalid");
        std::vector<std::string> alts;
        alts.reserve(alt_views.size());
        for (const auto alt : alt_views) {
            if (alt.empty() || alt == ".") throw std::invalid_argument("VCF ALT allele is invalid");
            alts.emplace_back(alt);
        }

        std::optional<double> qual;
        if (fields[5] != ".") {
            const double q = parse_number(fields[5], "VCF QUAL");
            if (q < 0.0) throw std::invalid_argument("VCF QUAL cannot be negative");
            qual = q;
        }
        const std::string input_filter{fields[6]};
        auto record_reasons = input_filters(input_filter);
        if (input_filter == ".") ++result.statistics.unfiltered_dot_records;
        else if (input_filter != "PASS") ++result.statistics.prefiltered_records;

        const auto info = parse_info(fields[7]);
        const auto depth = scalar_u64(info, "DP");
        const auto alt_counts = allele_u64(info, "AC", alts.size());
        const auto alt_fractions = allele_number(info, "AF", alts.size(), 0.0, 1.0);
        const auto alt_bq = allele_number(info, "ABQ", alts.size(), 0.0, 10000.0);

        ++result.statistics.total_records;
        result.statistics.total_alt_alleles += alts.size();
        ++result.statistics.contig_record_counts[chrom];

        bool all_alleles_pass = record_reasons.empty();
        std::vector<std::vector<std::string>> per_allele_reasons(alts.size(), record_reasons);
        for (std::size_t allele = 0U; allele < alts.size(); ++allele) {
            auto& reasons = per_allele_reasons[allele];
            auto metric_reason = [&](const std::string& reason) { add_reason(reasons, result.statistics, reason); };

            if (!depth.has_value()) {
                ++result.statistics.alleles_missing_depth;
                if (options.depth_filter_enabled) metric_reason("BioCoreMissingDP");
            } else if (options.depth_filter_enabled && *depth < options.minimum_depth) {
                metric_reason("BioCoreLowDP");
            }
            if (alt_counts.empty()) {
                ++result.statistics.alleles_missing_alt_count;
                if (options.alt_count_filter_enabled) metric_reason("BioCoreMissingAC");
            } else if (options.alt_count_filter_enabled && alt_counts[allele] < options.minimum_alt_count) {
                metric_reason("BioCoreLowAC");
            }
            if (alt_fractions.empty()) {
                ++result.statistics.alleles_missing_alt_fraction;
                if (options.alt_fraction_filter_enabled) metric_reason("BioCoreMissingAF");
            } else if (options.alt_fraction_filter_enabled &&
                       alt_fractions[allele] + std::numeric_limits<double>::epsilon() < options.minimum_alt_fraction) {
                metric_reason("BioCoreLowAF");
            }
            if (alt_bq.empty()) {
                ++result.statistics.alleles_missing_alt_base_quality;
                if (options.alt_base_quality_filter_enabled) metric_reason("BioCoreMissingABQ");
            } else if (options.alt_base_quality_filter_enabled &&
                       alt_bq[allele] + std::numeric_limits<double>::epsilon() < options.minimum_alt_base_quality) {
                metric_reason("BioCoreLowABQ");
            }

            const auto type = variant_type(reference, alts[allele]);
            if (type == "snv") ++result.statistics.snv_alleles;
            else if (type == "mnv") ++result.statistics.mnv_alleles;
            else if (type == "insertion_like") ++result.statistics.insertion_like_alleles;
            else if (type == "deletion_like") ++result.statistics.deletion_like_alleles;
            else if (type == "symbolic") ++result.statistics.symbolic_alleles;
            else ++result.statistics.complex_alleles;

            if (type == "snv" && canonical(reference.front()) && canonical(alts[allele].front())) {
                if (transition(reference.front(), alts[allele].front())) ++result.statistics.transitions;
                else ++result.statistics.transversions;
            }

            const bool passed = reasons.empty();
            if (passed) ++result.statistics.passed_alt_alleles;
            else { ++result.statistics.filtered_alt_alleles; all_alleles_pass = false; }

            result.statistics.rows.push_back(VariantQcRow{
                chrom + ":" + std::to_string(position) + ":" + reference + ":" + alts[allele],
                chrom,
                position,
                id,
                reference,
                alts[allele],
                static_cast<std::uint64_t>(allele + 1U),
                type,
                qual,
                input_filter,
                {},
                depth,
                alt_counts.empty() ? std::nullopt : std::optional<std::uint64_t>{alt_counts[allele]},
                alt_fractions.empty() ? std::nullopt : std::optional<double>{alt_fractions[allele]},
                alt_bq.empty() ? std::nullopt : std::optional<double>{alt_bq[allele]},
                passed
            });
        }

        std::vector<std::string> output_filters;
        std::set<std::string> seen_filters;
        if (input_filter == ".") {
            output_filters.push_back("BioCoreUnfiltered");
            seen_filters.insert("BioCoreUnfiltered");
            result.statistics.filter_reason_counts["BioCoreUnfiltered"] += static_cast<std::uint64_t>(alts.size());
        } else if (input_filter != "PASS") {
            for (const auto field : split(input_filter, ';')) {
                const std::string value{field};
                if (seen_filters.insert(value).second) output_filters.push_back(value);
            }
        }
        for (const auto& reasons : per_allele_reasons) {
            for (const auto& reason : reasons) {
                if (reason.rfind("BioCore", 0U) == 0U && seen_filters.insert(reason).second) output_filters.push_back(reason);
            }
        }
        const std::string output_filter = join_semicolon(output_filters);
        for (std::size_t i = result.statistics.rows.size() - alts.size(); i < result.statistics.rows.size(); ++i) {
            result.statistics.rows[i].output_filter = output_filter;
        }
        if (all_alleles_pass && output_filter == "PASS") ++result.statistics.passed_records;
        else ++result.statistics.filtered_records;

        std::ostringstream rebuilt;
        for (std::size_t index = 0U; index < fields.size(); ++index) {
            if (index != 0U) rebuilt << '\t';
            if (index == 6U) rebuilt << output_filter;
            else rebuilt << fields[index];
        }
        output_records.push_back(rebuilt.str());
    }

    if (!saw_fileformat || !saw_column_header) throw std::invalid_argument("VCF is missing required headers");

    std::ostringstream filtered;
    bool inserted = false;
    for (const auto& header : metadata) {
        filtered << header << '\n';
        if (!inserted && header.rfind("##fileformat=", 0U) == 0U) {
            filtered << "##BioCoreVCFQC=<Version=0.1.0,AnnotationKey=CHROM:POS:REF:ALT,Normalization=none"
                     << ",DPFilter=" << (options.depth_filter_enabled ? "on" : "off")
                     << ",MinDP=" << options.minimum_depth
                     << ",ACFilter=" << (options.alt_count_filter_enabled ? "on" : "off")
                     << ",MinAC=" << options.minimum_alt_count
                     << ",AFFilter=" << (options.alt_fraction_filter_enabled ? "on" : "off")
                     << ",MinAF=" << options.minimum_alt_fraction
                     << ",ABQFilter=" << (options.alt_base_quality_filter_enabled ? "on" : "off")
                     << ",MinABQ=" << options.minimum_alt_base_quality << ">\n";
            for (const auto& [id_value, description] : biocore_filter_definitions()) {
                if (!header_has_filter(metadata, id_value)) {
                    filtered << "##FILTER=<ID=" << id_value << ",Description=\"" << description << "\">\n";
                }
            }
            inserted = true;
        }
    }
    filtered << column_header << '\n';
    for (const auto& record : output_records) filtered << record << '\n';
    result.filtered_vcf = filtered.str();
    return result;
}

std::string render_vcf_qc_json(const VcfQcStatistics& s, const VcfQcOptions& o) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(6);
    out << "{\"schemaVersion\":1,\"module\":\"org.biocore.vcfqc.filter\",\"fileFormat\":\"" << json_escape(s.file_format)
        << "\",\"annotationKeyContract\":\"CHROM:POS:REF:ALT\",\"normalization\":\"none\",\"options\":{"
        << "\"depthFilterEnabled\":" << (o.depth_filter_enabled ? "true" : "false")
        << ",\"minDepth\":" << o.minimum_depth
        << ",\"altCountFilterEnabled\":" << (o.alt_count_filter_enabled ? "true" : "false")
        << ",\"minAltCount\":" << o.minimum_alt_count
        << ",\"altFractionFilterEnabled\":" << (o.alt_fraction_filter_enabled ? "true" : "false")
        << ",\"minAltFraction\":" << o.minimum_alt_fraction
        << ",\"altBaseQualityFilterEnabled\":" << (o.alt_base_quality_filter_enabled ? "true" : "false")
        << ",\"minAltBaseQuality\":" << o.minimum_alt_base_quality << "},\"metrics\":{"
        << "\"totalRecords\":" << s.total_records << ",\"totalAltAlleles\":" << s.total_alt_alleles
        << ",\"passedRecords\":" << s.passed_records << ",\"filteredRecords\":" << s.filtered_records
        << ",\"passedAltAlleles\":" << s.passed_alt_alleles << ",\"filteredAltAlleles\":" << s.filtered_alt_alleles
        << ",\"snvAlleles\":" << s.snv_alleles << ",\"mnvAlleles\":" << s.mnv_alleles
        << ",\"insertionLikeAlleles\":" << s.insertion_like_alleles << ",\"deletionLikeAlleles\":" << s.deletion_like_alleles
        << ",\"symbolicAlleles\":" << s.symbolic_alleles << ",\"complexAlleles\":" << s.complex_alleles
        << ",\"transitions\":" << s.transitions << ",\"transversions\":" << s.transversions << ",\"tiTvRatio\":";
    if (s.transversions == 0U) out << "null"; else out << static_cast<double>(s.transitions) / static_cast<double>(s.transversions);
    out << ",\"unfilteredDotRecords\":" << s.unfiltered_dot_records << ",\"prefilteredRecords\":" << s.prefiltered_records
        << ",\"allelesMissingDepth\":" << s.alleles_missing_depth << ",\"allelesMissingAltCount\":" << s.alleles_missing_alt_count
        << ",\"allelesMissingAltFraction\":" << s.alleles_missing_alt_fraction << ",\"allelesMissingAltBaseQuality\":" << s.alleles_missing_alt_base_quality
        << "},\"filterReasons\":{";
    bool first = true;
    for (const auto& [key, value] : s.filter_reason_counts) {
        if (!first) out << ',';
        first = false;
        out << '"' << json_escape(key) << "\":" << value;
    }
    out << "},\"contigs\":{";
    first = true;
    for (const auto& [key, value] : s.contig_record_counts) {
        if (!first) out << ',';
        first = false;
        out << '"' << json_escape(key) << "\":" << value;
    }
    out << "}}\n";
    return out.str();
}

std::string render_vcf_qc_tsv(const VcfQcStatistics& s) {
    std::ostringstream out;
    out << "variant_key\tchrom\tposition\tid\tref\talt\tallele_index\tvariant_type\tqual\tinput_filter\toutput_filter\tdp\tac\taf\tabq\tdecision\n";
    out << std::fixed << std::setprecision(6);
    for (const auto& row : s.rows) {
        out << tsv_escape(row.variant_key) << '\t' << tsv_escape(row.chrom) << '\t' << row.position << '\t' << tsv_escape(row.id)
            << '\t' << tsv_escape(row.reference) << '\t' << tsv_escape(row.alternate) << '\t' << row.allele_index << '\t' << row.variant_type << '\t';
        if (row.qual.has_value()) out << *row.qual; else out << '.';
        out << '\t' << tsv_escape(row.input_filter) << '\t' << tsv_escape(row.output_filter) << '\t';
        if (row.depth.has_value()) out << *row.depth; else out << '.';
        out << '\t'; if (row.alt_count.has_value()) out << *row.alt_count; else out << '.';
        out << '\t'; if (row.alt_fraction.has_value()) out << *row.alt_fraction; else out << '.';
        out << '\t'; if (row.alt_base_quality.has_value()) out << *row.alt_base_quality; else out << '.';
        out << '\t' << (row.passed ? "PASS" : "FILTERED") << '\n';
    }
    return out.str();
}

}  // namespace biocore::vcf_qc
