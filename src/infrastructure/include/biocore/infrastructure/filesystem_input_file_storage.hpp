#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string_view>

#include "biocore/application/i_input_file_storage.hpp"

namespace biocore::infrastructure {

class FilesystemInputFileStorage final : public application::IInputFileStorage {
public:
    explicit FilesystemInputFileStorage(std::string_view canonical_project_root);

    [[nodiscard]] std::unique_ptr<application::IInputFileImportTransaction> prepare_managed_copy(
        std::string_view source_path,
        std::string_view managed_file_id
    ) override;

    [[nodiscard]] bool begin_browser_upload(
        std::string_view upload_id,
        std::string_view display_name
    ) override;

    [[nodiscard]] std::uint64_t append_browser_upload(
        std::string_view upload_id,
        std::uint64_t expected_offset,
        std::string_view bytes
    ) override;

    [[nodiscard]] std::unique_ptr<application::IInputFileImportTransaction>
    prepare_browser_upload_commit(
        std::string_view upload_id,
        std::string_view managed_file_id
    ) override;

    void discard_browser_upload(std::string_view upload_id) noexcept override;

    [[nodiscard]] application::ManagedFileIntegrityResult verify_managed_file(
        const domain::ManagedFile& file
    ) const override;

private:
    std::filesystem::path project_root_;
    std::filesystem::path inputs_directory_;
    std::filesystem::path browser_uploads_directory_;
};

}  // namespace biocore::infrastructure
