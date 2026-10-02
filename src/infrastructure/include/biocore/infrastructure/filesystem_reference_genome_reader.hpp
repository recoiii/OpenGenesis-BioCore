#pragma once

#include "biocore/application/i_reference_genome_reader.hpp"

namespace biocore::application {
class IInputFileStorage;
}

namespace biocore::infrastructure {

class FilesystemReferenceGenomeReader final
    : public application::IReferenceGenomeReader {
public:
    explicit FilesystemReferenceGenomeReader(
        const application::IInputFileStorage& input_storage
    ) noexcept;

    [[nodiscard]] application::ReferenceGenomeRead read_verified_genome(
        const domain::ManagedFile& file,
        domain::ReferenceAssembly assembly,
        std::size_t maximum_bytes
    ) override;

private:
    const application::IInputFileStorage& input_storage_;
};

}  // namespace biocore::infrastructure
