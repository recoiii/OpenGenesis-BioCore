#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace biocore::application {

struct PreparedManagedCopy final {
    std::string display_name;
    std::string original_path;
    std::string managed_path;
    std::string relative_project_path;
    std::int64_t size_bytes;
};

class IInputFileImportTransaction {
public:
    virtual ~IInputFileImportTransaction() = default;

    [[nodiscard]] virtual const PreparedManagedCopy& prepared_file() const noexcept = 0;
    virtual void commit() noexcept = 0;
};

class IInputFileStorage {
public:
    virtual ~IInputFileStorage() = default;

    [[nodiscard]] virtual std::unique_ptr<IInputFileImportTransaction> prepare_managed_copy(
        std::string_view source_path,
        std::string_view managed_file_id
    ) = 0;

    [[nodiscard]] virtual bool begin_browser_upload(
        std::string_view upload_id,
        std::string_view display_name
    ) = 0;

    [[nodiscard]] virtual std::uint64_t append_browser_upload(
        std::string_view upload_id,
        std::uint64_t expected_offset,
        std::string_view bytes
    ) = 0;

    [[nodiscard]] virtual std::unique_ptr<IInputFileImportTransaction>
    prepare_browser_upload_commit(
        std::string_view upload_id,
        std::string_view managed_file_id
    ) = 0;

    virtual void discard_browser_upload(std::string_view upload_id) noexcept = 0;
};

}  // namespace biocore::application
