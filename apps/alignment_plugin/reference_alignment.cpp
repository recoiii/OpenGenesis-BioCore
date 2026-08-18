#include "reference_alignment.hpp"

#include <algorithm>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace biocore::alignment {
namespace {

struct AlignmentHit final {
    bool mapped{false};
    std::size_t contig_index{0U};
    std::size_t zero_based_position{0U};
    bool reverse{false};
    std::uint32_t mismatches{0U};
    std::uint64_t best_hit_count{0U};
};

[[nodiscard]] char uppercase_ascii(const char value) noexcept {
    return value >= 'a' && value <= 'z'
        ? static_cast<char>(value - ('a' - 'A')) : value;
}

[[nodiscard]] bool canonical_match(const char left, const char right) noexcept {
    const char a = uppercase_ascii(left);
    const char b = uppercase_ascii(right);
    return (a == 'A' || a == 'C' || a == 'G' || a == 'T') && a == b;
}

[[nodiscard]] std::uint32_t hamming_mismatches(
    const std::string_view read,
    const std::string_view reference,
    const std::uint32_t stop_after
) {
    std::uint32_t mismatches = 0U;
    for (std::size_t i = 0U; i < read.size(); ++i) {
        if (!canonical_match(read[i], reference[i])) {
            ++mismatches;
            if (mismatches > stop_after) break;
        }
    }
    return mismatches;
}

[[nodiscard]] AlignmentHit find_best_alignment(
    const std::vector<ReferenceContig>& reference,
    const std::string_view read,
    const AlignmentOptions& options
) {
    AlignmentHit best;
    std::uint32_t best_mismatches = options.maximum_mismatches + 1U;
    const std::string reverse = reverse_complement(read);

    for (std::size_t contig_index = 0U; contig_index < reference.size(); ++contig_index) {
        const auto& contig = reference[contig_index];
        if (read.size() > contig.sequence.size()) continue;
        const std::size_t last = contig.sequence.size() - read.size();

        for (std::size_t position = 0U; position <= last; ++position) {
            const std::string_view window{contig.sequence.data() + position, read.size()};
            for (int strand = 0; strand < 2; ++strand) {
                const std::string_view query = strand == 0 ? read : std::string_view{reverse};
                const std::uint32_t ceiling = best_mismatches <= options.maximum_mismatches
                    ? best_mismatches : options.maximum_mismatches;
                const auto mismatches = hamming_mismatches(query, window, ceiling);
                if (mismatches > options.maximum_mismatches) continue;

                if (!best.mapped || mismatches < best_mismatches) {
                    best = AlignmentHit{
                        true, contig_index, position, strand == 1, mismatches, 1U
                    };
                    best_mismatches = mismatches;
                } else if (mismatches == best_mismatches) {
                    if (best.best_hit_count == std::numeric_limits<std::uint64_t>::max()) {
                        throw std::overflow_error("Best-hit counter overflow");
                    }
                    ++best.best_hit_count;
                }
            }
        }
    }
    return best;
}

void write_sam_header(std::ostream& sam, const std::vector<ReferenceContig>& reference) {
    sam << "@HD\tVN:1.6\tSO:unknown\n";
    for (const auto& contig : reference) {
        sam << "@SQ\tSN:" << contig.name << "\tLN:" << contig.sequence.size() << '\n';
    }
    sam << "@PG\tID:biocore-align\tPN:OpenGenesis-BioCore\tVN:0.1.0\n";
    if (!sam) throw std::runtime_error("Unable to write SAM header");
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

void accumulate(
    ReadAlignmentStatistics& statistics,
    const AlignmentHit& hit,
    const std::size_t read_length
) {
    checked_increment(statistics.total_reads, "Alignment read count");
    if (!hit.mapped) {
        checked_increment(statistics.unmapped_reads, "Unmapped read count");
        return;
    }
    checked_increment(statistics.mapped_reads, "Mapped read count");
    if (hit.best_hit_count == 1U) {
        checked_increment(statistics.uniquely_mapped_reads, "Unique read count");
    } else {
        checked_increment(statistics.multi_mapped_reads, "Multi-mapped read count");
    }
    if (hit.reverse) checked_increment(statistics.reverse_strand_reads, "Reverse read count");
    checked_add(statistics.aligned_bases, static_cast<std::uint64_t>(read_length), "Aligned base count");
    checked_add(statistics.mismatch_bases, hit.mismatches, "Mismatch base count");
}

[[nodiscard]] int sam_flag(
    const AlignmentHit& hit,
    const bool paired,
    const bool read1,
    const AlignmentHit* mate
) {
    int flag = 0;
    if (paired) {
        flag |= 0x1;
        flag |= read1 ? 0x40 : 0x80;
        if (mate != nullptr && !mate->mapped) flag |= 0x8;
        if (mate != nullptr && mate->mapped && mate->reverse) flag |= 0x20;
    }
    if (!hit.mapped) flag |= 0x4;
    if (hit.mapped && hit.reverse) flag |= 0x10;
    return flag;
}

[[nodiscard]] std::int64_t template_length(
    const AlignmentHit& self,
    const AlignmentHit& mate,
    const std::size_t self_length,
    const std::size_t mate_length,
    const bool read1
) noexcept {
    if (!self.mapped || !mate.mapped || self.contig_index != mate.contig_index) return 0;
    const std::size_t self_start = self.zero_based_position;
    const std::size_t mate_start = mate.zero_based_position;
    const std::size_t left = std::min(self_start, mate_start);
    const std::size_t right = std::max(self_start + self_length, mate_start + mate_length);
    const auto span = static_cast<std::int64_t>(right - left);
    if (self_start < mate_start) return span;
    if (self_start > mate_start) return -span;
    return read1 ? span : -span;
}

void write_sam_record(
    std::ostream& sam,
    const std::string_view qname,
    const FastqRecord& record,
    const AlignmentHit& hit,
    const std::vector<ReferenceContig>& reference,
    const bool paired,
    const bool read1,
    const AlignmentHit* mate,
    const std::size_t mate_length
) {
    const int flag = sam_flag(hit, paired, read1, mate);
    const std::string seq = hit.mapped && hit.reverse
        ? reverse_complement(record.sequence) : record.sequence;
    const std::string qual = hit.mapped && hit.reverse
        ? reverse_quality(record.quality) : record.quality;

    const std::string rname = hit.mapped ? reference[hit.contig_index].name : "*";
    const std::size_t pos = hit.mapped ? hit.zero_based_position + 1U : 0U;
    const int mapq = hit.mapped && hit.best_hit_count == 1U ? 60 : 0;
    const std::string cigar = hit.mapped ? std::to_string(record.sequence.size()) + "M" : "*";

    std::string rnext{"*"};
    std::size_t pnext = 0U;
    std::int64_t tlen = 0;
    if (paired && mate != nullptr && mate->mapped) {
        if (hit.mapped && hit.contig_index == mate->contig_index) rnext = "=";
        else rnext = reference[mate->contig_index].name;
        pnext = mate->zero_based_position + 1U;
        tlen = template_length(hit, *mate, record.sequence.size(), mate_length, read1);
    }

    sam << qname << '\t' << flag << '\t' << rname << '\t' << pos << '\t'
        << mapq << '\t' << cigar << '\t' << rnext << '\t' << pnext << '\t'
        << tlen << '\t' << seq << '\t' << qual;
    if (hit.mapped) {
        sam << "\tNM:i:" << hit.mismatches << "\tNH:i:" << hit.best_hit_count;
    } else {
        sam << "\tNH:i:0";
    }
    sam << '\n';
    if (!sam) throw std::runtime_error("Unable to write SAM alignment");
}

[[nodiscard]] double percent(const std::uint64_t numerator, const std::uint64_t denominator) noexcept {
    return denominator == 0U ? 0.0
        : 100.0 * static_cast<double>(numerator) / static_cast<double>(denominator);
}

[[nodiscard]] std::string number(const double value) {
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::fixed << std::setprecision(6) << value;
    return output.str();
}

void append_read_json(std::ostringstream& out, const ReadAlignmentStatistics& s, const std::string_view indent) {
    out << indent << "\"totalReads\": " << s.total_reads << ",\n"
        << indent << "\"mappedReads\": " << s.mapped_reads << ",\n"
        << indent << "\"mappedPercent\": " << number(percent(s.mapped_reads, s.total_reads)) << ",\n"
        << indent << "\"unmappedReads\": " << s.unmapped_reads << ",\n"
        << indent << "\"uniquelyMappedReads\": " << s.uniquely_mapped_reads << ",\n"
        << indent << "\"multiMappedReads\": " << s.multi_mapped_reads << ",\n"
        << indent << "\"reverseStrandReads\": " << s.reverse_strand_reads << ",\n"
        << indent << "\"alignedBases\": " << s.aligned_bases << ",\n"
        << indent << "\"mismatchBases\": " << s.mismatch_bases << '\n';
}

void append_read_tsv(std::ostringstream& out, const ReadAlignmentStatistics& s) {
    out << "total_reads\t" << s.total_reads << '\n'
        << "mapped_reads\t" << s.mapped_reads << '\n'
        << "mapped_percent\t" << number(percent(s.mapped_reads, s.total_reads)) << '\n'
        << "unmapped_reads\t" << s.unmapped_reads << '\n'
        << "uniquely_mapped_reads\t" << s.uniquely_mapped_reads << '\n'
        << "multi_mapped_reads\t" << s.multi_mapped_reads << '\n'
        << "reverse_strand_reads\t" << s.reverse_strand_reads << '\n'
        << "aligned_bases\t" << s.aligned_bases << '\n'
        << "mismatch_bases\t" << s.mismatch_bases << '\n';
}

}  // namespace

void validate_alignment_options(const AlignmentOptions& options) {
    if (options.maximum_mismatches > 12U) {
        throw std::invalid_argument("Maximum mismatches must be between 0 and 12");
    }
}

ReadAlignmentStatistics align_single_fastq(
    const std::vector<ReferenceContig>& reference,
    std::istream& reads,
    std::ostream& sam,
    const AlignmentOptions& options
) {
    validate_alignment_options(options);
    if (reference.empty()) throw std::invalid_argument("Alignment reference is empty");
    write_sam_header(sam, reference);

    ReadAlignmentStatistics statistics;
    FastqRecordReader reader{reads};
    FastqRecord record;
    while (reader.read(record)) {
        const auto hit = find_best_alignment(reference, record.sequence, options);
        write_sam_record(sam, sam_read_name(record.header), record, hit, reference,
                         false, true, nullptr, 0U);
        accumulate(statistics, hit, record.sequence.size());
    }
    if (statistics.total_reads == 0U) {
        throw std::invalid_argument("FASTQ contains no reads");
    }
    return statistics;
}

PairedAlignmentStatistics align_paired_fastq(
    const std::vector<ReferenceContig>& reference,
    std::istream& read1,
    std::istream& read2,
    std::ostream& sam,
    const AlignmentOptions& options
) {
    validate_alignment_options(options);
    if (reference.empty()) throw std::invalid_argument("Alignment reference is empty");
    write_sam_header(sam, reference);

    PairedAlignmentStatistics statistics;
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
        const auto identity = parse_mate_identity(record1.header);
        const auto hit1 = find_best_alignment(reference, record1.sequence, options);
        const auto hit2 = find_best_alignment(reference, record2.sequence, options);

        write_sam_record(sam, identity.core_id, record1, hit1, reference,
                         true, true, &hit2, record2.sequence.size());
        write_sam_record(sam, identity.core_id, record2, hit2, reference,
                         true, false, &hit1, record1.sequence.size());

        accumulate(statistics.reads, hit1, record1.sequence.size());
        accumulate(statistics.reads, hit2, record2.sequence.size());
        checked_increment(statistics.total_pairs, "Alignment pair count");
        if (hit1.mapped && hit2.mapped) checked_increment(statistics.both_mapped_pairs, "Both-mapped pair count");
        else if (hit1.mapped || hit2.mapped) checked_increment(statistics.single_mapped_pairs, "Single-mapped pair count");
        else checked_increment(statistics.unmapped_pairs, "Unmapped pair count");
    }
    if (statistics.total_pairs == 0U) {
        throw std::invalid_argument("Paired FASTQ contains no read pairs");
    }
    return statistics;
}

std::string render_single_alignment_json(
    const ReadAlignmentStatistics& statistics,
    const AlignmentOptions& options
) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "{\n  \"schemaVersion\": 1,\n"
        << "  \"algorithm\": \"native-ungapped-hamming-v1\",\n"
        << "  \"maximumMismatches\": " << options.maximum_mismatches << ",\n";
    append_read_json(out, statistics, "  ");
    out << "}\n";
    return out.str();
}

std::string render_single_alignment_tsv(
    const ReadAlignmentStatistics& statistics,
    const AlignmentOptions& options
) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "metric\tvalue\nalgorithm\tnative-ungapped-hamming-v1\n"
        << "maximum_mismatches\t" << options.maximum_mismatches << '\n';
    append_read_tsv(out, statistics);
    return out.str();
}

std::string render_paired_alignment_json(
    const PairedAlignmentStatistics& statistics,
    const AlignmentOptions& options
) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "{\n  \"schemaVersion\": 1,\n"
        << "  \"algorithm\": \"native-ungapped-hamming-v1\",\n"
        << "  \"maximumMismatches\": " << options.maximum_mismatches << ",\n"
        << "  \"totalPairs\": " << statistics.total_pairs << ",\n"
        << "  \"bothMappedPairs\": " << statistics.both_mapped_pairs << ",\n"
        << "  \"singleMappedPairs\": " << statistics.single_mapped_pairs << ",\n"
        << "  \"unmappedPairs\": " << statistics.unmapped_pairs << ",\n"
        << "  \"reads\": {\n";
    append_read_json(out, statistics.reads, "    ");
    out << "  }\n}\n";
    return out.str();
}

std::string render_paired_alignment_tsv(
    const PairedAlignmentStatistics& statistics,
    const AlignmentOptions& options
) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "metric\tvalue\nalgorithm\tnative-ungapped-hamming-v1\n"
        << "maximum_mismatches\t" << options.maximum_mismatches << '\n'
        << "total_pairs\t" << statistics.total_pairs << '\n'
        << "both_mapped_pairs\t" << statistics.both_mapped_pairs << '\n'
        << "single_mapped_pairs\t" << statistics.single_mapped_pairs << '\n'
        << "unmapped_pairs\t" << statistics.unmapped_pairs << '\n';
    append_read_tsv(out, statistics.reads);
    return out.str();
}

}  // namespace biocore::alignment
