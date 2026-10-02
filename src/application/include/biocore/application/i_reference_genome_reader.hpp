#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "biocore/domain/managed_file.hpp"
#include "biocore/domain/reference_genome.hpp"

namespace biocore::application {

enum class ReferenceGenomeReadStatus {
    verified,
    too_large,
    integrity_unverified,
    invalid_fasta,
    changed_during_read,
    io_error
};

struct ReferenceGenomeRead final {
    ReferenceGenomeReadStatus status{ReferenceGenomeReadStatus::io_error};
    std::optional<domain::ReferenceGenome> genome;
    std::optional<std::string> verified_sha256;
    std::int64_t verified_size_bytes{0};
};

class IReferenceGenomeReader {
public:
    virtual ~IReferenceGenomeReader() = default;

    [[nodiscard]] virtual ReferenceGenomeRead read_verified_genome(
        const domain::ManagedFile& file,
        domain::ReferenceAssembly assembly,
        std::size_t maximum_bytes
    ) = 0;
};

}  // namespace biocore::application
