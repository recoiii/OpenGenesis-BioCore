#include "biocore/infrastructure/filesystem_reference_manifest_reader.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "biocore/application/i_input_file_storage.hpp"

namespace biocore::infrastructure {
namespace {

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

[[nodiscard]] application::ReferenceManifestRead status_only(
    const application::ReferenceManifestReadStatus status
) {
    return {
        .status = status,
        .contigs = {},
        .verified_sha256 = std::nullopt,
        .verified_size_bytes = 0,
    };
}

[[nodiscard]] std::string header_token(const std::string_view line) {
    if (line.empty() || line.front() != '>') {
        throw std::invalid_argument{"Reference FASTA header is invalid"};
    }
    const auto payload = line.substr(1U);
    const auto begin = payload.find_first_not_of(" \t");
    if (begin == std::string_view::npos) {
        throw std::invalid_argument{"Reference FASTA contig name is empty"};
    }
    const auto end = payload.find_first_of(" \t", begin);
    const auto token = payload.substr(
        begin, end == std::string_view::npos ? payload.size() - begin : end - begin);
    if (token.empty() || token.size() > 4096U ||
        token.find('\0') != std::string_view::npos) {
        throw std::invalid_argument{"Reference FASTA contig name is invalid"};
    }
    return std::string{token};
}

}  // namespace

FilesystemReferenceManifestReader::FilesystemReferenceManifestReader(
    const application::IInputFileStorage& input_storage
) noexcept : input_storage_{input_storage} {}

application::ReferenceManifestRead
FilesystemReferenceManifestReader::read_verified_manifest(
    const domain::ManagedFile& file,
    const std::size_t maximum_bytes
) {
    using application::ManagedFileIntegrityStatus;
    using application::ReferenceManifestRead;
    using application::ReferenceManifestReadStatus;

    const auto before = input_storage_.verify_managed_file(file);
    if (before.status != ManagedFileIntegrityStatus::verified ||
        !before.observed_sha256.has_value() ||
        !before.observed_size_bytes.has_value()) {
        return status_only(ReferenceManifestReadStatus::integrity_unverified);
    }
    if (*before.observed_size_bytes < 0 ||
        static_cast<std::uint64_t>(*before.observed_size_bytes) >
            static_cast<std::uint64_t>(maximum_bytes)) {
        return status_only(ReferenceManifestReadStatus::too_large);
    }
    if (!file.managed_path().has_value()) {
        return status_only(ReferenceManifestReadStatus::integrity_unverified);
    }

    try {
        std::ifstream input{path_from_utf8(*file.managed_path()), std::ios::binary};
        if (!input) return status_only(ReferenceManifestReadStatus::io_error);

        std::vector<application::ReferenceManifestContig> contigs;
        std::set<std::string, std::less<>> names;
        std::string current_name;
        std::uint64_t current_length = 0U;
        bool have_contig = false;
        std::string line;

        const auto finish_contig = [&]() {
            if (!have_contig) return;
            if (current_length == 0U || !names.emplace(current_name).second) {
                throw std::invalid_argument{"Reference FASTA contig is empty or duplicated"};
            }
            contigs.push_back({current_name, current_length});
        };

        while (std::getline(input, line)) {
            if (line.size() > 16U * 1024U * 1024U) {
                throw std::length_error{"Reference FASTA line is too large"};
            }
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            if (line.front() == '>') {
                finish_contig();
                current_name = header_token(line);
                current_length = 0U;
                have_contig = true;
                continue;
            }
            if (!have_contig) {
                throw std::invalid_argument{"Reference FASTA sequence precedes its first header"};
            }
            for (const char raw : line) {
                if (raw == ' ' || raw == '\t' || raw == '\r' || raw == '\n') {
                    throw std::invalid_argument{"Reference FASTA sequence contains whitespace"};
                }
                if (current_length == std::numeric_limits<std::uint64_t>::max()) {
                    throw std::overflow_error{"Reference FASTA contig length overflow"};
                }
                ++current_length;
            }
        }
        if (input.bad()) return status_only(ReferenceManifestReadStatus::io_error);
        finish_contig();
        if (contigs.empty()) {
            return status_only(ReferenceManifestReadStatus::invalid_fasta);
        }

        const auto after = input_storage_.verify_managed_file(file);
        if (after.status != ManagedFileIntegrityStatus::verified ||
            !after.observed_sha256.has_value() ||
            !after.observed_size_bytes.has_value() ||
            *after.observed_sha256 != *before.observed_sha256 ||
            *after.observed_size_bytes != *before.observed_size_bytes) {
            return status_only(ReferenceManifestReadStatus::changed_during_read);
        }

        return {
            .status = ReferenceManifestReadStatus::verified,
            .contigs = std::move(contigs),
            .verified_sha256 = *after.observed_sha256,
            .verified_size_bytes = *after.observed_size_bytes,
        };
    } catch (const std::invalid_argument&) {
        return status_only(ReferenceManifestReadStatus::invalid_fasta);
    } catch (const std::length_error&) {
        return status_only(ReferenceManifestReadStatus::invalid_fasta);
    } catch (...) {
        return status_only(ReferenceManifestReadStatus::io_error);
    }
}

}  // namespace biocore::infrastructure
