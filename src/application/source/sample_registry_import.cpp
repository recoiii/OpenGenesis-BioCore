#include "biocore/application/sample_registry_import.hpp"

#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace biocore::application {
namespace {

struct ParsedRow final {
    std::size_t line{0U};
    std::vector<std::string> fields;
    std::string error;
};

[[nodiscard]] char delimiter_for(const SampleTableFormat format) noexcept {
    return format == SampleTableFormat::csv ? ',' : '\t';
}

[[nodiscard]] ParsedRow parse_row(
    const std::string_view line,
    const std::size_t line_number,
    const char delimiter) {
    ParsedRow result;
    result.line = line_number;
    std::string field;
    bool quoted = false;
    bool closed_quote = false;

    for (std::size_t index = 0U; index <= line.size(); ++index) {
        const bool at_end = index == line.size();
        const char character = at_end ? delimiter : line[index];

        if (quoted) {
            if (at_end) {
                result.error = "unterminated quoted field";
                return result;
            }
            if (character == '"') {
                if (index + 1U < line.size() && line[index + 1U] == '"') {
                    field.push_back('"');
                    ++index;
                } else {
                    quoted = false;
                    closed_quote = true;
                }
            } else {
                field.push_back(character);
            }
            continue;
        }

        if (closed_quote) {
            if (character != delimiter) {
                result.error = "characters follow closing quote";
                return result;
            }
            result.fields.push_back(std::move(field));
            field.clear();
            closed_quote = false;
            continue;
        }

        if (character == delimiter) {
            result.fields.push_back(std::move(field));
            field.clear();
            continue;
        }

        if (character == '"') {
            if (!field.empty()) {
                result.error = "quote inside unquoted field";
                return result;
            }
            quoted = true;
            continue;
        }

        field.push_back(character);
    }
    return result;
}

[[nodiscard]] std::vector<ParsedRow> parse_rows(
    const std::string_view text,
    const SampleTableFormat format) {
    std::vector<ParsedRow> rows;
    std::size_t offset = 0U;
    std::size_t line_number = 1U;
    const char delimiter = delimiter_for(format);

    while (offset <= text.size()) {
        const std::size_t newline = text.find('\n', offset);
        std::string_view line = text.substr(
            offset, newline == std::string_view::npos ? text.size() - offset : newline - offset);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1U);

        if (!line.empty() || newline != std::string_view::npos || offset < text.size()) {
            rows.push_back(parse_row(line, line_number, delimiter));
        }
        if (newline == std::string_view::npos) break;
        offset = newline + 1U;
        ++line_number;
    }
    return rows;
}

[[nodiscard]] std::string escape_field(
    const std::string_view value,
    const char delimiter) {
    const bool quote = value.find(delimiter) != std::string_view::npos ||
                       value.find('"') != std::string_view::npos ||
                       value.find('\r') != std::string_view::npos ||
                       value.find('\n') != std::string_view::npos;
    if (!quote) return std::string{value};

    std::string output;
    output.reserve(value.size() + 2U);
    output.push_back('"');
    for (const char character : value) {
        if (character == '"') output.push_back('"');
        output.push_back(character);
    }
    output.push_back('"');
    return output;
}

}  // namespace

std::string_view SampleImportPreview::project_id() const noexcept { return project_id_; }

const std::vector<domain::ProjectSample>& SampleImportPreview::samples() const noexcept {
    return samples_;
}

const std::vector<SampleImportIssue>& SampleImportPreview::issues() const noexcept {
    return issues_;
}

bool SampleImportPreview::valid() const noexcept { return issues_.empty(); }

void SampleImportPreview::add_issue(
    const std::size_t line, std::string code, std::string message) {
    issues_.push_back(SampleImportIssue{line, std::move(code), std::move(message)});
}

SampleRegistryImportService::SampleRegistryImportService(IProjectSampleStore& store) noexcept
    : store_{store} {}

SampleImportPreview SampleRegistryImportService::preview(
    const std::string_view project_id,
    const std::string_view table_text,
    const SampleTableFormat format) {
    SampleImportPreview preview;
    preview.project_id_ = std::string{project_id};

    const auto existing = store_.list(project_id);
    if (!existing.has_value()) {
        preview.add_issue(1U, "project_not_found",
                          "The requested project does not own this project database");
        return preview;
    }

    const auto rows = parse_rows(table_text, format);
    if (rows.empty()) {
        preview.add_issue(1U, "missing_header", "Sample table header is missing");
        return preview;
    }

    const auto& header = rows.front();
    if (!header.error.empty() || header.fields.size() != 3U ||
        header.fields[0] != "sample_id" ||
        header.fields[1] != "display_name" ||
        header.fields[2] != "group") {
        preview.add_issue(1U, "invalid_header",
                          "Expected sample_id, display_name, group header");
        return preview;
    }

    std::unordered_set<std::string> existing_ids;
    existing_ids.reserve(existing->size());
    for (const auto& sample : *existing) {
        existing_ids.emplace(sample.sample_id());
    }

    std::unordered_map<std::string, std::size_t> first_line;
    for (std::size_t index = 1U; index < rows.size(); ++index) {
        const auto& row = rows[index];
        if (row.fields.size() == 1U && row.fields.front().empty() && row.error.empty()) {
            continue;
        }
        if (!row.error.empty()) {
            preview.add_issue(row.line, "malformed_row", row.error);
            continue;
        }
        if (row.fields.size() != 3U) {
            preview.add_issue(row.line, "column_count",
                              "Each sample row must contain exactly three columns");
            continue;
        }

        try {
            domain::ProjectSample sample{
                std::string{project_id}, row.fields[0], row.fields[1], row.fields[2]};
            const std::string id{sample.sample_id()};
            const auto [where, inserted] = first_line.emplace(id, row.line);
            if (!inserted) {
                preview.add_issue(
                    row.line, "duplicate_sample_id",
                    "Sample id duplicates line " + std::to_string(where->second));
                continue;
            }
            if (existing_ids.contains(id)) {
                preview.add_issue(row.line, "sample_already_exists",
                                  "Sample id already exists in this project");
                continue;
            }
            preview.samples_.push_back(std::move(sample));
        } catch (const std::invalid_argument& error) {
            preview.add_issue(row.line, "invalid_sample", error.what());
        }
    }

    if (preview.samples_.empty() && preview.issues_.empty()) {
        preview.add_issue(1U, "no_samples", "Sample table contains no data rows");
    }
    return preview;
}

void SampleRegistryImportService::commit(const SampleImportPreview& preview) {
    if (!preview.valid()) {
        throw std::invalid_argument{"Cannot commit an invalid sample import preview"};
    }

    const SampleBatchAddResult result =
        store_.add_batch(preview.project_id(), preview.samples());
    if (result == SampleBatchAddResult::duplicate_sample) {
        throw std::runtime_error{
            "Sample registry changed after preview; import was not applied"};
    }
    if (result == SampleBatchAddResult::project_not_found) {
        throw std::runtime_error{"Project no longer owns this sample registry"};
    }
}

std::string SampleRegistryImportService::export_table(
    const std::string_view project_id,
    const SampleTableFormat format) {
    const auto samples = store_.list(project_id);
    if (!samples.has_value()) {
        throw std::invalid_argument{"Project does not own this sample registry"};
    }

    const char delimiter = delimiter_for(format);
    std::string output = "sample_id";
    output.push_back(delimiter);
    output += "display_name";
    output.push_back(delimiter);
    output += "group\n";

    for (const auto& sample : *samples) {
        output += escape_field(sample.sample_id(), delimiter);
        output.push_back(delimiter);
        output += escape_field(sample.display_name(), delimiter);
        output.push_back(delimiter);
        output += escape_field(sample.group_label(), delimiter);
        output.push_back('\n');
    }
    return output;
}

}  // namespace biocore::application
