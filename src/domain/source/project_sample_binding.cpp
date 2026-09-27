#include "biocore/domain/project_sample_binding.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <utility>

namespace biocore::domain {
namespace {

[[nodiscard]] bool blank(const std::string_view value) {
    return value.empty() || std::ranges::all_of(value, [](const char character) {
        return std::isspace(static_cast<unsigned char>(character)) != 0;
    });
}

void validate_identifier(const std::string_view value, const char* const field) {
    if (blank(value) || value.size() > ProjectSampleBinding::maximum_identifier_bytes ||
        value.find('\0') != std::string_view::npos) {
        throw std::invalid_argument{std::string{"Invalid project sample binding "} + field};
    }
}

void validate_optional_identifier(
    const std::optional<std::string>& value,
    const char* const field
) {
    if (value.has_value()) validate_identifier(*value, field);
}

}  // namespace

std::string_view to_string(const SampleInputLayout layout) noexcept {
    switch (layout) {
        case SampleInputLayout::single_fastq: return "single_fastq";
        case SampleInputLayout::paired_fastq: return "paired_fastq";
        case SampleInputLayout::alignment: return "alignment";
        case SampleInputLayout::variants: return "variants";
    }
    return "single_fastq";
}

SampleInputLayout sample_input_layout_from_string(const std::string_view value) {
    if (value == "single_fastq") return SampleInputLayout::single_fastq;
    if (value == "paired_fastq") return SampleInputLayout::paired_fastq;
    if (value == "alignment") return SampleInputLayout::alignment;
    if (value == "variants") return SampleInputLayout::variants;
    throw std::invalid_argument{"Unknown sample input layout"};
}

ProjectSampleBinding::ProjectSampleBinding(
    std::string project_id,
    std::string sample_id,
    const SampleInputLayout layout,
    std::string primary_file_id,
    std::optional<std::string> secondary_file_id,
    std::optional<std::string> reference_file_id
)
    : project_id_{std::move(project_id)},
      sample_id_{std::move(sample_id)},
      layout_{layout},
      primary_file_id_{std::move(primary_file_id)},
      secondary_file_id_{std::move(secondary_file_id)},
      reference_file_id_{std::move(reference_file_id)} {
    validate_identifier(project_id_, "project id");
    validate_identifier(sample_id_, "sample id");
    validate_identifier(primary_file_id_, "primary file id");
    validate_optional_identifier(secondary_file_id_, "secondary file id");
    validate_optional_identifier(reference_file_id_, "reference file id");

    if (layout_ == SampleInputLayout::paired_fastq) {
        if (!secondary_file_id_.has_value()) {
            throw std::invalid_argument{"Paired FASTQ binding requires a read2 file"};
        }
        if (*secondary_file_id_ == primary_file_id_) {
            throw std::invalid_argument{"Paired FASTQ binding requires distinct read1/read2 files"};
        }
    } else if (secondary_file_id_.has_value()) {
        throw std::invalid_argument{"Only paired FASTQ bindings may contain a secondary file"};
    }

    if (reference_file_id_.has_value() &&
        (*reference_file_id_ == primary_file_id_ ||
         (secondary_file_id_.has_value() && *reference_file_id_ == *secondary_file_id_))) {
        throw std::invalid_argument{"Reference file must be distinct from sample input files"};
    }
}

std::string_view ProjectSampleBinding::project_id() const noexcept { return project_id_; }
std::string_view ProjectSampleBinding::sample_id() const noexcept { return sample_id_; }
SampleInputLayout ProjectSampleBinding::layout() const noexcept { return layout_; }
std::string_view ProjectSampleBinding::primary_file_id() const noexcept { return primary_file_id_; }
const std::optional<std::string>& ProjectSampleBinding::secondary_file_id() const noexcept {
    return secondary_file_id_;
}
const std::optional<std::string>& ProjectSampleBinding::reference_file_id() const noexcept {
    return reference_file_id_;
}

}  // namespace biocore::domain
