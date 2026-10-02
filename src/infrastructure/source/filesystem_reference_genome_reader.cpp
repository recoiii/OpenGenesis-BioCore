#include "biocore/infrastructure/filesystem_reference_genome_reader.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

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

[[nodiscard]] application::ReferenceGenomeRead status_only(
    const application::ReferenceGenomeReadStatus status
) {
    return {
        .status = status,
        .genome = std::nullopt,
        .verified_sha256 = std::nullopt,
        .verified_size_bytes = 0,
    };
}

}  // namespace

FilesystemReferenceGenomeReader::FilesystemReferenceGenomeReader(
    const application::IInputFileStorage& input_storage
) noexcept : input_storage_{input_storage} {}

application::ReferenceGenomeRead
FilesystemReferenceGenomeReader::read_verified_genome(
    const domain::ManagedFile& file,
    const domain::ReferenceAssembly assembly,
    const std::size_t maximum_bytes
) {
    using application::ManagedFileIntegrityStatus;
    using application::ReferenceGenomeReadStatus;

    const auto before = input_storage_.verify_managed_file(file);
    if (before.status != ManagedFileIntegrityStatus::verified ||
        !before.observed_sha256.has_value() ||
        !before.observed_size_bytes.has_value() ||
        !file.managed_path().has_value()) {
        return status_only(ReferenceGenomeReadStatus::integrity_unverified);
    }
    if (*before.observed_size_bytes < 0 ||
        static_cast<std::uint64_t>(*before.observed_size_bytes) >
            static_cast<std::uint64_t>(maximum_bytes)) {
        return status_only(ReferenceGenomeReadStatus::too_large);
    }

    try {
        const auto path = path_from_utf8(*file.managed_path());
        std::error_code status_error;
        const auto status = std::filesystem::symlink_status(path, status_error);
        if (status_error || status.type() != std::filesystem::file_type::regular) {
            return status_only(ReferenceGenomeReadStatus::integrity_unverified);
        }

        std::ifstream input{path, std::ios::binary};
        if (!input) return status_only(ReferenceGenomeReadStatus::io_error);

        auto genome = domain::ReferenceGenome::from_fasta(input, assembly);
        if (input.bad()) return status_only(ReferenceGenomeReadStatus::io_error);

        const auto after = input_storage_.verify_managed_file(file);
        if (after.status != ManagedFileIntegrityStatus::verified ||
            !after.observed_sha256.has_value() ||
            !after.observed_size_bytes.has_value()) {
            return status_only(ReferenceGenomeReadStatus::changed_during_read);
        }
        if (*after.observed_sha256 != *before.observed_sha256 ||
            *after.observed_size_bytes != *before.observed_size_bytes) {
            return status_only(ReferenceGenomeReadStatus::changed_during_read);
        }

        return {
            .status = ReferenceGenomeReadStatus::verified,
            .genome = std::move(genome),
            .verified_sha256 = *after.observed_sha256,
            .verified_size_bytes = *after.observed_size_bytes,
        };
    } catch (const std::invalid_argument&) {
        return status_only(ReferenceGenomeReadStatus::invalid_fasta);
    } catch (const std::length_error&) {
        return status_only(ReferenceGenomeReadStatus::invalid_fasta);
    } catch (...) {
        return status_only(ReferenceGenomeReadStatus::io_error);
    }
}

}  // namespace biocore::infrastructure
