#include "fastq_trimming.hpp"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string_view>

#include "paired_fastq_statistics.hpp"

namespace biocore::fastq_qc {
namespace {

struct TrimResult final {
    std::size_t final_length{0U};
    std::uint64_t adapter_bases{0U};
    std::uint64_t quality_bases{0U};
};

[[nodiscard]] char uppercase_ascii(const char value) noexcept {
    if (value >= 'a' && value <= 'z') return static_cast<char>(value - ('a' - 'A'));
    return value;
}

void validate_adapter(const std::string& adapter, const char* label) {
    if (adapter.empty()) return;
    if (adapter.size() > 4096U) {
        throw std::invalid_argument(std::string{label} + " exceeds the supported length");
    }
    for (const char raw : adapter) {
        const char value = uppercase_ascii(raw);
        if (value != 'A' && value != 'C' && value != 'G' && value != 'T' && value != 'N') {
            throw std::invalid_argument(std::string{label} + " must contain only A/C/G/T/N");
        }
    }
}

[[nodiscard]] std::size_t adapter_trim_position(
    const std::string& sequence,
    const std::string& adapter,
    const std::uint32_t minimum_overlap,
    const std::uint32_t maximum_mismatches
) {
    if (adapter.empty() || sequence.size() < minimum_overlap) return sequence.size();
    const std::size_t earliest = sequence.size() > adapter.size() ? sequence.size() - adapter.size() : 0U;
    const std::size_t latest = sequence.size() - minimum_overlap;
    for (std::size_t start = earliest; start <= latest; ++start) {
        const std::size_t overlap = sequence.size() - start;
        if (overlap > adapter.size()) continue;
        std::uint32_t mismatches = 0U;
        for (std::size_t index = 0U; index < overlap; ++index) {
            if (uppercase_ascii(sequence[start + index]) != uppercase_ascii(adapter[index])) {
                ++mismatches;
                if (mismatches > maximum_mismatches) break;
            }
        }
        if (mismatches <= maximum_mismatches) return start;
    }
    return sequence.size();
}

[[nodiscard]] std::uint32_t phred_at(const std::string& quality, const std::size_t index) {
    const auto encoded = static_cast<unsigned char>(quality[index]);
    if (encoded < 33U || encoded > 126U) {
        throw std::invalid_argument("FASTQ quality contains a character outside Phred+33 printable range");
    }
    return static_cast<std::uint32_t>(encoded - 33U);
}

[[nodiscard]] TrimResult trim_record(
    const FastqRecord& record, const std::string& adapter, const TrimmingOptions& options
) {
    std::size_t end = adapter_trim_position(
        record.sequence, adapter, options.minimum_adapter_overlap,
        options.maximum_adapter_mismatches
    );
    const std::size_t adapter_end = end;
    while (end > 0U && phred_at(record.quality, end - 1U) < options.quality_threshold) {
        --end;
    }
    return TrimResult{
        .final_length = end,
        .adapter_bases = static_cast<std::uint64_t>(record.sequence.size() - adapter_end),
        .quality_bases = static_cast<std::uint64_t>(adapter_end - end),
    };
}

void checked_increment(std::uint64_t& value, const char* label) {
    if (value == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error(std::string{label} + " overflow");
    }
    ++value;
}

void checked_add(std::uint64_t& value, const std::uint64_t amount, const char* label) {
    if (amount > std::numeric_limits<std::uint64_t>::max() - value) {
        throw std::overflow_error(std::string{label} + " overflow");
    }
    value += amount;
}

void write_record(std::ostream& output, const FastqRecord& record, const std::size_t length) {
    output << record.header << '\n'
           << std::string_view{record.sequence}.substr(0U, length) << '\n'
           << "+\n"
           << std::string_view{record.quality}.substr(0U, length) << '\n';
    if (!output) throw std::runtime_error("Unable to write trimmed FASTQ output");
}

[[nodiscard]] std::string json_string(const std::string& value) {
    std::string output{"\""};
    for (const char c : value) {
        if (c == '\\' || c == '"') output.push_back('\\');
        output.push_back(c);
    }
    output.push_back('"');
    return output;
}

void append_common_options(std::ostringstream& out, const TrimmingOptions& options, bool paired) {
    out << "    \"adapterRead1\": " << json_string(options.adapter_read1) << ",\n";
    if (paired) out << "    \"adapterRead2\": " << json_string(options.adapter_read2) << ",\n";
    out << "    \"minimumAdapterOverlap\": " << options.minimum_adapter_overlap << ",\n"
        << "    \"maximumAdapterMismatches\": " << options.maximum_adapter_mismatches << ",\n"
        << "    \"qualityThreshold\": " << options.quality_threshold << ",\n"
        << "    \"minimumLength\": " << options.minimum_length << "\n";
}

void append_common_tsv(std::ostringstream& out, const TrimmingOptions& options, bool paired) {
    out << "adapter_read1\t" << options.adapter_read1 << '\n';
    if (paired) out << "adapter_read2\t" << options.adapter_read2 << '\n';
    out << "minimum_adapter_overlap\t" << options.minimum_adapter_overlap << '\n'
        << "maximum_adapter_mismatches\t" << options.maximum_adapter_mismatches << '\n'
        << "quality_threshold\t" << options.quality_threshold << '\n'
        << "minimum_length\t" << options.minimum_length << '\n';
}

}  // namespace

void validate_trimming_options(const TrimmingOptions& options, const bool paired) {
    validate_adapter(options.adapter_read1, "FASTQ read1 adapter");
    if (paired) validate_adapter(options.adapter_read2, "FASTQ read2 adapter");
    if (options.minimum_adapter_overlap < 3U || options.minimum_adapter_overlap > 64U) {
        throw std::invalid_argument("FASTQ minimum adapter overlap must be between 3 and 64");
    }
    if (options.maximum_adapter_mismatches >= options.minimum_adapter_overlap ||
        options.maximum_adapter_mismatches > 8U) {
        throw std::invalid_argument("FASTQ maximum adapter mismatches are invalid");
    }
    if (options.quality_threshold > 60U) {
        throw std::invalid_argument("FASTQ quality threshold must be between 0 and 60");
    }
    if (options.minimum_length == 0U || options.minimum_length > 1000000U) {
        throw std::invalid_argument("FASTQ minimum length must be between 1 and 1000000");
    }
    if (!options.adapter_read1.empty() && options.minimum_adapter_overlap > options.adapter_read1.size()) {
        throw std::invalid_argument("FASTQ minimum adapter overlap exceeds read1 adapter length");
    }
    if (paired && !options.adapter_read2.empty() && options.minimum_adapter_overlap > options.adapter_read2.size()) {
        throw std::invalid_argument("FASTQ minimum adapter overlap exceeds read2 adapter length");
    }
}

SingleTrimmingStatistics trim_single_fastq(
    std::istream& input, std::ostream& output, const TrimmingOptions& options
) {
    validate_trimming_options(options, false);
    SingleTrimmingStatistics statistics;
    FastqRecordReader reader{input};
    FastqRecord record;
    while (reader.read(record)) {
        checked_increment(statistics.input_reads, "FASTQ input read count");
        accumulate_fastq_record(statistics.input, record);
        const TrimResult trim = trim_record(record, options.adapter_read1, options);
        if (trim.adapter_bases != 0U) checked_increment(statistics.adapter_trimmed_reads, "Adapter-trimmed read count");
        if (trim.quality_bases != 0U) checked_increment(statistics.quality_trimmed_reads, "Quality-trimmed read count");
        checked_add(statistics.adapter_trimmed_bases, trim.adapter_bases, "Adapter-trimmed base count");
        checked_add(statistics.quality_trimmed_bases, trim.quality_bases, "Quality-trimmed base count");
        if (trim.final_length < options.minimum_length) {
            checked_increment(statistics.discarded_reads, "Discarded read count");
            continue;
        }
        FastqRecord trimmed{record.header, record.sequence.substr(0U, trim.final_length), record.quality.substr(0U, trim.final_length)};
        write_record(output, trimmed, trimmed.sequence.size());
        accumulate_fastq_record(statistics.output, trimmed);
        checked_increment(statistics.kept_reads, "Kept read count");
    }
    if (statistics.input_reads == 0U) throw std::invalid_argument("FASTQ trimming input contains no reads");
    return statistics;
}

PairedTrimmingStatistics trim_paired_fastq(
    std::istream& read1, std::istream& read2,
    std::ostream& output1, std::ostream& output2,
    const TrimmingOptions& options
) {
    validate_trimming_options(options, true);
    PairedTrimmingStatistics statistics;
    FastqRecordReader reader1{read1};
    FastqRecordReader reader2{read2};
    FastqRecord record1;
    FastqRecord record2;
    for (;;) {
        const bool has1 = reader1.read(record1);
        const bool has2 = reader2.read(record2);
        if (has1 != has2) throw std::invalid_argument("Paired FASTQ files contain different record counts");
        if (!has1) break;
        validate_paired_fastq_records(record1, record2);
        checked_increment(statistics.input_pairs, "FASTQ input pair count");
        accumulate_fastq_record(statistics.input_read1, record1);
        accumulate_fastq_record(statistics.input_read2, record2);
        const TrimResult trim1 = trim_record(record1, options.adapter_read1, options);
        const TrimResult trim2 = trim_record(record2, options.adapter_read2, options);
        if (trim1.adapter_bases != 0U) checked_increment(statistics.read1_adapter_trimmed_reads, "R1 adapter-trimmed read count");
        if (trim2.adapter_bases != 0U) checked_increment(statistics.read2_adapter_trimmed_reads, "R2 adapter-trimmed read count");
        if (trim1.quality_bases != 0U) checked_increment(statistics.read1_quality_trimmed_reads, "R1 quality-trimmed read count");
        if (trim2.quality_bases != 0U) checked_increment(statistics.read2_quality_trimmed_reads, "R2 quality-trimmed read count");
        checked_add(statistics.read1_adapter_trimmed_bases, trim1.adapter_bases, "R1 adapter-trimmed base count");
        checked_add(statistics.read2_adapter_trimmed_bases, trim2.adapter_bases, "R2 adapter-trimmed base count");
        checked_add(statistics.read1_quality_trimmed_bases, trim1.quality_bases, "R1 quality-trimmed base count");
        checked_add(statistics.read2_quality_trimmed_bases, trim2.quality_bases, "R2 quality-trimmed base count");
        if (trim1.final_length < options.minimum_length || trim2.final_length < options.minimum_length) {
            checked_increment(statistics.discarded_pairs, "Discarded pair count");
            continue;
        }
        FastqRecord out1{record1.header, record1.sequence.substr(0U, trim1.final_length), record1.quality.substr(0U, trim1.final_length)};
        FastqRecord out2{record2.header, record2.sequence.substr(0U, trim2.final_length), record2.quality.substr(0U, trim2.final_length)};
        write_record(output1, out1, out1.sequence.size());
        write_record(output2, out2, out2.sequence.size());
        accumulate_fastq_record(statistics.output_read1, out1);
        accumulate_fastq_record(statistics.output_read2, out2);
        checked_increment(statistics.kept_pairs, "Kept pair count");
    }
    if (statistics.input_pairs == 0U) throw std::invalid_argument("Paired FASTQ trimming input contains no read pairs");
    return statistics;
}

std::string render_single_trimming_json(
    const SingleTrimmingStatistics& s, const TrimmingOptions& options
) {
    std::ostringstream out; out.imbue(std::locale::classic());
    out << "{\n  \"schemaVersion\": 1,\n  \"format\": \"FASTQ-TRIM-SINGLE\",\n  \"options\": {\n";
    append_common_options(out, options, false);
    out << "  },\n  \"inputReads\": " << s.input_reads
        << ",\n  \"keptReads\": " << s.kept_reads
        << ",\n  \"discardedReads\": " << s.discarded_reads
        << ",\n  \"adapterTrimmedReads\": " << s.adapter_trimmed_reads
        << ",\n  \"qualityTrimmedReads\": " << s.quality_trimmed_reads
        << ",\n  \"adapterTrimmedBases\": " << s.adapter_trimmed_bases
        << ",\n  \"qualityTrimmedBases\": " << s.quality_trimmed_bases
        << ",\n  \"outputBases\": " << s.output.total_bases << "\n}\n";
    return out.str();
}

std::string render_single_trimming_tsv(
    const SingleTrimmingStatistics& s, const TrimmingOptions& options
) {
    std::ostringstream out; out.imbue(std::locale::classic());
    out << "metric\tvalue\n"; append_common_tsv(out, options, false);
    out << "input_reads\t" << s.input_reads << '\n'
        << "kept_reads\t" << s.kept_reads << '\n'
        << "discarded_reads\t" << s.discarded_reads << '\n'
        << "adapter_trimmed_reads\t" << s.adapter_trimmed_reads << '\n'
        << "quality_trimmed_reads\t" << s.quality_trimmed_reads << '\n'
        << "adapter_trimmed_bases\t" << s.adapter_trimmed_bases << '\n'
        << "quality_trimmed_bases\t" << s.quality_trimmed_bases << '\n'
        << "output_bases\t" << s.output.total_bases << '\n';
    return out.str();
}

std::string render_paired_trimming_json(
    const PairedTrimmingStatistics& s, const TrimmingOptions& options
) {
    std::ostringstream out; out.imbue(std::locale::classic());
    out << "{\n  \"schemaVersion\": 1,\n  \"format\": \"FASTQ-TRIM-PAIRED\",\n  \"options\": {\n";
    append_common_options(out, options, true);
    out << "  },\n  \"inputPairs\": " << s.input_pairs
        << ",\n  \"keptPairs\": " << s.kept_pairs
        << ",\n  \"discardedPairs\": " << s.discarded_pairs
        << ",\n  \"read1AdapterTrimmedReads\": " << s.read1_adapter_trimmed_reads
        << ",\n  \"read2AdapterTrimmedReads\": " << s.read2_adapter_trimmed_reads
        << ",\n  \"read1QualityTrimmedReads\": " << s.read1_quality_trimmed_reads
        << ",\n  \"read2QualityTrimmedReads\": " << s.read2_quality_trimmed_reads
        << ",\n  \"read1AdapterTrimmedBases\": " << s.read1_adapter_trimmed_bases
        << ",\n  \"read2AdapterTrimmedBases\": " << s.read2_adapter_trimmed_bases
        << ",\n  \"read1QualityTrimmedBases\": " << s.read1_quality_trimmed_bases
        << ",\n  \"read2QualityTrimmedBases\": " << s.read2_quality_trimmed_bases
        << ",\n  \"outputRead1Bases\": " << s.output_read1.total_bases
        << ",\n  \"outputRead2Bases\": " << s.output_read2.total_bases << "\n}\n";
    return out.str();
}

std::string render_paired_trimming_tsv(
    const PairedTrimmingStatistics& s, const TrimmingOptions& options
) {
    std::ostringstream out; out.imbue(std::locale::classic());
    out << "metric\tvalue\n"; append_common_tsv(out, options, true);
    out << "input_pairs\t" << s.input_pairs << '\n'
        << "kept_pairs\t" << s.kept_pairs << '\n'
        << "discarded_pairs\t" << s.discarded_pairs << '\n'
        << "read1_adapter_trimmed_reads\t" << s.read1_adapter_trimmed_reads << '\n'
        << "read2_adapter_trimmed_reads\t" << s.read2_adapter_trimmed_reads << '\n'
        << "read1_quality_trimmed_reads\t" << s.read1_quality_trimmed_reads << '\n'
        << "read2_quality_trimmed_reads\t" << s.read2_quality_trimmed_reads << '\n'
        << "read1_adapter_trimmed_bases\t" << s.read1_adapter_trimmed_bases << '\n'
        << "read2_adapter_trimmed_bases\t" << s.read2_adapter_trimmed_bases << '\n'
        << "read1_quality_trimmed_bases\t" << s.read1_quality_trimmed_bases << '\n'
        << "read2_quality_trimmed_bases\t" << s.read2_quality_trimmed_bases << '\n'
        << "output_read1_bases\t" << s.output_read1.total_bases << '\n'
        << "output_read2_bases\t" << s.output_read2.total_bases << '\n';
    return out.str();
}

}  // namespace biocore::fastq_qc
