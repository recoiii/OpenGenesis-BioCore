#pragma once

#include <filesystem>
#include <istream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace biocore::alignment {

struct ReferenceContig final {
    std::string name;
    std::string sequence;
};

struct FastqRecord final {
    std::string header;
    std::string sequence;
    std::string quality;
};

struct MateIdentity final {
    std::string core_id;
    std::optional<unsigned int> mate;
};

class FastqRecordReader final {
public:
    explicit FastqRecordReader(std::istream& input) noexcept;
    [[nodiscard]] bool read(FastqRecord& record);

private:
    std::istream& input_;
    bool first_record_{true};
};

[[nodiscard]] std::unique_ptr<std::istream> open_fastq_input(
    const std::filesystem::path& path
);
[[nodiscard]] std::vector<ReferenceContig> read_reference_fasta(
    const std::filesystem::path& path
);
[[nodiscard]] std::vector<ReferenceContig> parse_reference_fasta(std::istream& input);
void validate_fastq_record(const FastqRecord& record);
[[nodiscard]] MateIdentity parse_mate_identity(const std::string& header);
void validate_paired_fastq_records(const FastqRecord& read1, const FastqRecord& read2);
[[nodiscard]] std::string sam_read_name(const std::string& header);
[[nodiscard]] std::string reverse_complement(std::string_view sequence);
[[nodiscard]] std::string reverse_quality(std::string_view quality);

}  // namespace biocore::alignment
