#include "biocore/domain/vcf_ingestion.hpp"

#include "vcf_ingestion_internal.hpp"

#include <stdexcept>
#include <unordered_set>

namespace biocore::domain {

namespace {
constexpr std::size_t maximum_samples = 100000U;
constexpr std::size_t maximum_records = 50000000U;
}

VcfIngestionResult ingest_vcf(std::istream& input, const ReferenceGenome& reference) {
    using namespace vcf_detail;
    VcfIngestionResult result;
    std::string line;
    bool first_line = true;
    bool saw_fileformat = false;
    bool saw_columns = false;

    while (read_bounded_line(input, line)) {
        if (first_line) {
            strip_utf8_bom(line);
            first_line = false;
        }
        if (!saw_columns && line.starts_with("##")) {
            if (line.starts_with("##fileformat=")) {
                if (saw_fileformat) {
                    throw std::invalid_argument("VCF fileformat header is duplicated");
                }
                result.header.file_format = line.substr(std::string{"##fileformat="}.size());
                if (result.header.file_format != "VCFv4.2" && result.header.file_format != "VCFv4.3"
                    && result.header.file_format != "VCFv4.4" && result.header.file_format != "VCFv4.5") {
                    throw std::invalid_argument("VCF fileformat must be VCFv4.2, VCFv4.3, VCFv4.4 or VCFv4.5");
                }
                saw_fileformat = true;
            }
            parse_header_line(line, result.header);
            continue;
        }
        if (!saw_columns && line.starts_with("#CHROM\t")) {
            if (!saw_fileformat) {
                throw std::invalid_argument("VCF fileformat header must precede #CHROM");
            }
            const auto columns = split(line, '\t');
            if (columns.size() < 8U || columns[0] != "#CHROM" || columns[1] != "POS"
                || columns[2] != "ID" || columns[3] != "REF" || columns[4] != "ALT"
                || columns[5] != "QUAL" || columns[6] != "FILTER" || columns[7] != "INFO") {
                throw std::invalid_argument("VCF #CHROM header has invalid mandatory columns");
            }
            if (columns.size() == 9U) {
                throw std::invalid_argument("VCF FORMAT column requires at least one sample column");
            }
            if (columns.size() > 8U) {
                if (columns[8] != "FORMAT") {
                    throw std::invalid_argument("VCF ninth column must be FORMAT when samples are present");
                }
                if (columns.size() - 9U > maximum_samples) {
                    throw std::invalid_argument("VCF declares too many samples");
                }
                std::unordered_set<std::string> seen_samples;
                for (std::size_t index = 9U; index < columns.size(); ++index) {
                    if (columns[index].empty()) {
                        throw std::invalid_argument("VCF sample name must not be empty");
                    }
                    std::string sample_name{columns[index]};
                    if (!seen_samples.insert(sample_name).second) {
                        throw std::invalid_argument("VCF sample name is duplicated");
                    }
                    result.header.sample_names.push_back(std::move(sample_name));
                }
            }
            saw_columns = true;
            continue;
        }
        if (!saw_columns) {
            throw std::invalid_argument("VCF data appears before the #CHROM header");
        }
        if (line.empty() || line.front() == '#') {
            throw std::invalid_argument("VCF contains an unexpected line after the #CHROM header");
        }
        if (result.records.size() >= maximum_records) {
            throw std::invalid_argument("VCF record count exceeds the safety limit");
        }

        const auto fields = split(line, '\t');
        result.records.push_back(parse_record(fields, result.header, reference));
    }

    if (!saw_fileformat || !saw_columns) {
        throw std::invalid_argument("VCF is missing required headers");
    }
    return result;
}

}  // namespace biocore::domain
