#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace biocore::domain {

enum class SampleInputLayout {
    single_fastq,
    paired_fastq,
    alignment,
    variants
};

[[nodiscard]] std::string_view to_string(SampleInputLayout layout) noexcept;
[[nodiscard]] SampleInputLayout sample_input_layout_from_string(std::string_view value);

class ProjectSampleBinding final {
public:
    static constexpr std::size_t maximum_identifier_bytes = 128U;

    ProjectSampleBinding(
        std::string project_id,
        std::string sample_id,
        SampleInputLayout layout,
        std::string primary_file_id,
        std::optional<std::string> secondary_file_id,
        std::optional<std::string> reference_file_id
    );

    [[nodiscard]] std::string_view project_id() const noexcept;
    [[nodiscard]] std::string_view sample_id() const noexcept;
    [[nodiscard]] SampleInputLayout layout() const noexcept;
    [[nodiscard]] std::string_view primary_file_id() const noexcept;
    [[nodiscard]] const std::optional<std::string>& secondary_file_id() const noexcept;
    [[nodiscard]] const std::optional<std::string>& reference_file_id() const noexcept;

    friend bool operator==(const ProjectSampleBinding&, const ProjectSampleBinding&) = default;

private:
    std::string project_id_;
    std::string sample_id_;
    SampleInputLayout layout_;
    std::string primary_file_id_;
    std::optional<std::string> secondary_file_id_;
    std::optional<std::string> reference_file_id_;
};

}  // namespace biocore::domain
