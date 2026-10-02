#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "biocore/domain/managed_file.hpp"

namespace biocore::application {

enum class ReferenceManifestReadStatus {
    verified,
    too_large,
    integrity_unverified,
    invalid_fasta,
    changed_during_read,
    io_error
};

struct ReferenceManifestContig final {
    std::string canonical_name;
    std::uint64_t length{0U};
};

struct ReferenceManifestRead final {
    ReferenceManifestReadStatus status{ReferenceManifestReadStatus::io_error};
    std::vector<ReferenceManifestContig> contigs;
    std::optional<std::string> verified_sha256;
    std::int64_t verified_size_bytes{0};
};

class IReferenceManifestReader {
public:
    virtual ~IReferenceManifestReader() = default;

    [[nodiscard]] virtual ReferenceManifestRead read_verified_manifest(
        const domain::ManagedFile& file,
        std::size_t maximum_bytes
    ) = 0;
};

}  // namespace biocore::application
