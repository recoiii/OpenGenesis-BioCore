#include "biocore/infrastructure/filesystem_reference_compatibility_inspector.hpp"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace biocore::infrastructure {
namespace {

struct DeclaredContig final {
    std::string name;
    std::optional<std::uint64_t> length;
};

[[nodiscard]] std::filesystem::path path_from_utf8(const std::string_view value) {
#ifdef _WIN32
    std::u8string encoded;
    encoded.reserve(value.size());
    for (const char raw : value) {
        encoded.push_back(static_cast<char8_t>(static_cast<unsigned char>(raw)));
    }
    return std::filesystem::path{encoded};
#else
    return std::filesystem::path{value};
#endif
}

[[nodiscard]] std::filesystem::path managed_path(const domain::ManagedFile& file) {
    if (!file.managed_path().has_value()) {
        throw std::invalid_argument{"Reference inspection requires a managed file path"};
    }
    return path_from_utf8(*file.managed_path());
}

[[nodiscard]] std::uint64_t parse_u64(const std::string_view value, const char* field) {
    if (value.empty()) throw std::invalid_argument{std::string{field} + " is empty"};
    std::uint64_t parsed = 0U;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) {
        throw std::invalid_argument{std::string{field} + " is invalid"};
    }
    return parsed;
}

[[nodiscard]] std::string header_token(const std::string_view line) {
    if (line.empty() || line.front() != '>') {
        throw std::invalid_argument{"FASTA header is invalid"};
    }
    const std::string_view payload = line.substr(1U);
    const std::size_t begin = payload.find_first_not_of(" \t");
    if (begin == std::string_view::npos) {
        throw std::invalid_argument{"FASTA contig name is empty"};
    }
    const std::size_t end = payload.find_first_of(" \t", begin);
    return std::string{payload.substr(
        begin,
        end == std::string_view::npos ? payload.size() - begin : end - begin
    )};
}

[[nodiscard]] std::unordered_map<std::string, std::uint64_t> fasta_dictionary(
    const std::filesystem::path& path
) {
    std::ifstream input{path, std::ios::binary};
    if (!input) throw std::runtime_error{"Unable to open selected reference FASTA"};

    std::unordered_map<std::string, std::uint64_t> result;
    std::string current;
    std::uint64_t length = 0U;
    bool have_contig = false;
    std::string line;

    const auto finish = [&]() {
        if (!have_contig) return;
        if (length == 0U) throw std::invalid_argument{"Reference FASTA contains an empty contig"};
        if (!result.emplace(current, length).second) {
            throw std::invalid_argument{"Reference FASTA contains duplicate contig names"};
        }
    };

    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (line.front() == '>') {
            finish();
            current = header_token(line);
            length = 0U;
            have_contig = true;
            continue;
        }
        if (!have_contig) {
            throw std::invalid_argument{"Reference FASTA sequence appears before a header"};
        }
        for (const char raw : line) {
            if (raw == ' ' || raw == '\t' || raw == '\r' || raw == '\n') {
                throw std::invalid_argument{"Reference FASTA sequence contains whitespace"};
            }
            if (length == std::numeric_limits<std::uint64_t>::max()) {
                throw std::overflow_error{"Reference FASTA contig length overflow"};
            }
            ++length;
        }
    }
    if (input.bad()) throw std::runtime_error{"Unable to read selected reference FASTA"};
    finish();
    if (result.empty()) throw std::invalid_argument{"Reference FASTA has no contigs"};
    return result;
}

[[nodiscard]] std::vector<std::string_view> tab_fields(const std::string_view line) {
    std::vector<std::string_view> fields;
    std::size_t start = 0U;
    for (;;) {
        const std::size_t tab = line.find('\t', start);
        fields.push_back(line.substr(
            start,
            tab == std::string_view::npos ? line.size() - start : tab - start
        ));
        if (tab == std::string_view::npos) return fields;
        start = tab + 1U;
    }
}

[[nodiscard]] std::vector<DeclaredContig> sam_dictionary(
    const std::filesystem::path& path
) {
    std::ifstream input{path, std::ios::binary};
    if (!input) throw std::runtime_error{"Unable to open SAM input"};

    std::vector<DeclaredContig> result;
    std::unordered_map<std::string, bool> names;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (line.front() != '@') break;
        if (!line.starts_with("@SQ\t")) continue;

        std::optional<std::string> name;
        std::optional<std::uint64_t> length;
        const auto fields = tab_fields(line);
        for (std::size_t index = 1U; index < fields.size(); ++index) {
            if (fields[index].starts_with("SN:")) {
                if (name.has_value() || fields[index].size() <= 3U) {
                    throw std::invalid_argument{"SAM @SQ SN is invalid"};
                }
                name = std::string{fields[index].substr(3U)};
            } else if (fields[index].starts_with("LN:")) {
                if (length.has_value()) throw std::invalid_argument{"SAM @SQ LN is duplicated"};
                length = parse_u64(fields[index].substr(3U), "SAM @SQ LN");
            }
        }
        if (!name.has_value() || !length.has_value() ||
            !names.emplace(*name, true).second) {
            throw std::invalid_argument{"SAM @SQ declaration is incomplete or duplicated"};
        }
        result.push_back(DeclaredContig{std::move(*name), length});
    }
    if (input.bad()) throw std::runtime_error{"Unable to read SAM input"};
    return result;
}

[[nodiscard]] std::optional<std::string_view> vcf_attribute(
    const std::string_view body,
    const std::string_view key
) {
    std::size_t start = 0U;
    while (start <= body.size()) {
        const std::size_t comma = body.find(',', start);
        const std::string_view field = body.substr(
            start,
            comma == std::string_view::npos ? body.size() - start : comma - start
        );
        if (field.starts_with(key) && field.size() > key.size() &&
            field[key.size()] == '=') {
            return field.substr(key.size() + 1U);
        }
        if (comma == std::string_view::npos) break;
        start = comma + 1U;
    }
    return std::nullopt;
}

[[nodiscard]] std::vector<DeclaredContig> vcf_dictionary(
    const std::filesystem::path& path
) {
    std::ifstream input{path, std::ios::binary};
    if (!input) throw std::runtime_error{"Unable to open VCF input"};

    std::vector<DeclaredContig> result;
    std::unordered_map<std::string, bool> names;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.starts_with("#CHROM")) break;
        constexpr std::string_view prefix = "##contig=<";
        if (!line.starts_with(prefix) || line.size() <= prefix.size() ||
            line.back() != '>') {
            continue;
        }

        const std::string_view body{
            line.data() + prefix.size(),
            line.size() - prefix.size() - 1U
        };
        const auto id = vcf_attribute(body, "ID");
        if (!id.has_value() || id->empty()) {
            throw std::invalid_argument{"VCF contig declaration has no ID"};
        }
        if (!names.emplace(std::string{*id}, true).second) {
            throw std::invalid_argument{"VCF contig declaration is duplicated"};
        }
        std::optional<std::uint64_t> length;
        if (const auto declared_length = vcf_attribute(body, "length");
            declared_length.has_value()) {
            length = parse_u64(*declared_length, "VCF contig length");
        }
        result.push_back(DeclaredContig{std::string{*id}, length});
    }
    if (input.bad()) throw std::runtime_error{"Unable to read VCF input"};
    return result;
}

[[nodiscard]] gzFile open_gzip(const std::filesystem::path& path) {
#ifdef _WIN32
    return gzopen_w(path.c_str(), "rb");
#else
    return gzopen(path.c_str(), "rb");
#endif
}

void gz_read_exact(gzFile file, void* output, const std::size_t bytes) {
    auto* cursor = static_cast<unsigned char*>(output);
    std::size_t remaining = bytes;
    while (remaining > 0U) {
        const unsigned int chunk = static_cast<unsigned int>(
            std::min<std::size_t>(remaining, std::numeric_limits<unsigned int>::max())
        );
        const int read = gzread(file, cursor, chunk);
        if (read <= 0) throw std::runtime_error{"Truncated BAM header"};
        cursor += static_cast<std::size_t>(read);
        remaining -= static_cast<std::size_t>(read);
    }
}

[[nodiscard]] std::int32_t read_i32(gzFile file) {
    std::array<unsigned char, 4> bytes{};
    gz_read_exact(file, bytes.data(), bytes.size());
    const std::uint32_t value =
        static_cast<std::uint32_t>(bytes[0]) |
        (static_cast<std::uint32_t>(bytes[1]) << 8U) |
        (static_cast<std::uint32_t>(bytes[2]) << 16U) |
        (static_cast<std::uint32_t>(bytes[3]) << 24U);
    return static_cast<std::int32_t>(value);
}

void gz_skip(gzFile file, std::size_t bytes) {
    std::array<unsigned char, 8192> buffer{};
    while (bytes > 0U) {
        const std::size_t chunk = std::min(bytes, buffer.size());
        gz_read_exact(file, buffer.data(), chunk);
        bytes -= chunk;
    }
}

[[nodiscard]] std::vector<DeclaredContig> bam_dictionary(
    const std::filesystem::path& path
) {
    gzFile raw = open_gzip(path);
    if (raw == nullptr) throw std::runtime_error{"Unable to open BAM input"};
    struct Guard final {
        gzFile file;
        ~Guard() { if (file != nullptr) gzclose(file); }
    } guard{raw};

    std::array<char, 4> magic{};
    gz_read_exact(raw, magic.data(), magic.size());
    if (std::string_view{magic.data(), magic.size()} != std::string_view{"BAM\1", 4U}) {
        throw std::invalid_argument{"BAM magic is invalid"};
    }

    const std::int32_t text_length = read_i32(raw);
    if (text_length < 0 || text_length > 64 * 1024 * 1024) {
        throw std::invalid_argument{"BAM header text length is invalid"};
    }
    gz_skip(raw, static_cast<std::size_t>(text_length));

    const std::int32_t reference_count = read_i32(raw);
    if (reference_count < 0 || reference_count > 1000000) {
        throw std::invalid_argument{"BAM reference count is invalid"};
    }

    std::vector<DeclaredContig> result;
    result.reserve(static_cast<std::size_t>(reference_count));
    std::unordered_map<std::string, bool> names;
    for (std::int32_t index = 0; index < reference_count; ++index) {
        const std::int32_t name_length = read_i32(raw);
        if (name_length <= 1 || name_length > 4096) {
            throw std::invalid_argument{"BAM reference name length is invalid"};
        }
        std::string name(static_cast<std::size_t>(name_length), '\0');
        gz_read_exact(raw, name.data(), name.size());
        if (name.back() != '\0') {
            throw std::invalid_argument{"BAM reference name is not NUL terminated"};
        }
        name.pop_back();

        const std::int32_t length = read_i32(raw);
        if (length <= 0) throw std::invalid_argument{"BAM reference length is invalid"};
        if (!names.emplace(name, true).second) {
            throw std::invalid_argument{"BAM reference declaration is duplicated"};
        }
        result.push_back(DeclaredContig{
            std::move(name),
            static_cast<std::uint64_t>(length)
        });
    }
    return result;
}

[[nodiscard]] application::ReferenceCompatibilityResult compare(
    const std::vector<DeclaredContig>& declarations,
    const std::unordered_map<std::string, std::uint64_t>& reference
) {
    if (declarations.empty()) {
        return {
            application::ReferenceCompatibilityStatus::not_declared_by_format,
            "Input format contains no usable reference/contig declaration; compatibility is not verified"
        };
    }

    for (const auto& contig : declarations) {
        const auto found = reference.find(contig.name);
        if (found == reference.end()) {
            return {
                application::ReferenceCompatibilityStatus::mismatch,
                "Input declares contig '" + contig.name +
                    "' which is absent from the selected reference"
            };
        }
        if (contig.length.has_value() && *contig.length != found->second) {
            return {
                application::ReferenceCompatibilityStatus::mismatch,
                "Input contig '" + contig.name +
                    "' length disagrees with the selected reference"
            };
        }
    }

    return {
        application::ReferenceCompatibilityStatus::verified,
        "Declared input contigs are compatible with the selected reference"
    };
}

}  // namespace

application::ReferenceCompatibilityResult
FilesystemReferenceCompatibilityInspector::inspect(
    const domain::ManagedFile& input,
    const domain::ManagedFile& reference
) const {
    if (reference.file_type() != "fasta") {
        return {
            application::ReferenceCompatibilityStatus::mismatch,
            "Reference compatibility requires a FASTA reference"
        };
    }

    if (input.file_type() == "fastq") {
        return {
            application::ReferenceCompatibilityStatus::not_declared_by_format,
            "FASTQ does not declare a reference assembly; the selected FASTA is not verified from FASTQ metadata"
        };
    }

    try {
        const auto reference_dictionary = fasta_dictionary(managed_path(reference));
        if (input.file_type() == "sam") {
            return compare(sam_dictionary(managed_path(input)), reference_dictionary);
        }
        if (input.file_type() == "bam") {
            return compare(bam_dictionary(managed_path(input)), reference_dictionary);
        }
        if (input.file_type() == "vcf") {
            return compare(vcf_dictionary(managed_path(input)), reference_dictionary);
        }
        return {
            application::ReferenceCompatibilityStatus::evidence_unavailable,
            "Input file type has no supported reference compatibility inspector"
        };
    } catch (const std::exception& error) {
        return {
            application::ReferenceCompatibilityStatus::evidence_unavailable,
            error.what()
        };
    }
}

}  // namespace biocore::infrastructure
