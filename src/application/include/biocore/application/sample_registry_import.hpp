#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "biocore/application/i_project_sample_store.hpp"

namespace biocore::application {

enum class SampleTableFormat {
    csv,
    tsv
};

struct SampleImportIssue final {
    std::size_t line{0U};
    std::string code;
    std::string message;
};

class SampleImportPreview final {
public:
    [[nodiscard]] std::string_view project_id() const noexcept;
    [[nodiscard]] const std::vector<domain::ProjectSample>& samples() const noexcept;
    [[nodiscard]] const std::vector<SampleImportIssue>& issues() const noexcept;
    [[nodiscard]] bool valid() const noexcept;

private:
    friend class SampleRegistryImportService;
    void add_issue(std::size_t line, std::string code, std::string message);

    std::string project_id_;
    std::vector<domain::ProjectSample> samples_;
    std::vector<SampleImportIssue> issues_;
};

class SampleRegistryImportService final {
public:
    explicit SampleRegistryImportService(IProjectSampleStore& store) noexcept;

    [[nodiscard]] SampleImportPreview preview(
        std::string_view project_id,
        std::string_view table_text,
        SampleTableFormat format);

    void commit(const SampleImportPreview& preview);

    [[nodiscard]] std::string export_table(
        std::string_view project_id,
        SampleTableFormat format);

private:
    IProjectSampleStore& store_;
};

}  // namespace biocore::application
