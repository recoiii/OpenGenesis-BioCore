#pragma once

#include "biocore/application/i_reference_manifest_reader.hpp"

namespace biocore::application {
class IInputFileStorage;
}

namespace biocore::infrastructure {

class FilesystemReferenceManifestReader final
    : public application::IReferenceManifestReader {
public:
    explicit FilesystemReferenceManifestReader(
        const application::IInputFileStorage& input_storage
    ) noexcept;

    [[nodiscard]] application::ReferenceManifestRead read_verified_manifest(
        const domain::ManagedFile& file,
        std::size_t maximum_bytes
    ) override;

private:
    const application::IInputFileStorage& input_storage_;
};

}  // namespace biocore::infrastructure
