#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "biocore/plugin_protocol/plugin_document_codec.hpp"

#include "fasta_invocation_contract.hpp"
#include "fasta_statistics.hpp"

namespace {

[[nodiscard]] std::string read_invocation(const std::filesystem::path& path) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error || std::filesystem::is_symlink(status) ||
        !std::filesystem::is_regular_file(status)) {
        throw std::invalid_argument("FASTA QC invocation must be a regular non-symlink file");
    }

    const auto size = std::filesystem::file_size(path, error);
    if (error || size == 0U ||
        size > biocore::plugin_protocol::maximum_plugin_invocation_bytes) {
        throw std::invalid_argument("Invalid FASTA QC invocation file size");
    }

    std::ifstream input{path, std::ios::binary};
    if (!input) {
        throw std::runtime_error("Unable to open FASTA QC invocation");
    }
    std::string content{
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{}
    };
    if (input.bad() || content.size() != size) {
        throw std::runtime_error("Unable to read FASTA QC invocation");
    }
    return content;
}

[[nodiscard]] const biocore::plugin_protocol::PluginInvocationOutputDocument& find_output(
    const biocore::plugin_protocol::PluginInvocationDocument& invocation,
    const std::string_view port
) {
    for (const auto& output : invocation.outputs) {
        if (output.port == port) return output;
    }
    throw std::logic_error("Validated FASTA QC output disappeared");
}

void validate_document_contract(
    const biocore::plugin_protocol::PluginInvocationDocument& invocation
) {
    std::vector<biocore::fasta_qc::InvocationInput> inputs;
    inputs.reserve(invocation.inputs.size());
    for (const auto& input : invocation.inputs) {
        inputs.push_back(biocore::fasta_qc::InvocationInput{
            input.port, input.source_kind, input.file_type
        });
    }

    std::vector<biocore::fasta_qc::InvocationOutput> outputs;
    outputs.reserve(invocation.outputs.size());
    for (const auto& output : invocation.outputs) {
        outputs.push_back(biocore::fasta_qc::InvocationOutput{
            output.port, output.file_type
        });
    }

    biocore::fasta_qc::validate_invocation_contract(
        invocation.module_id,
        invocation.parameters.size(),
        inputs,
        outputs
    );
}

void write_text_file(const std::filesystem::path& path, const std::string_view content) {
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    if (!output) {
        throw std::runtime_error("Unable to open FASTA QC output");
    }
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    output.flush();
    if (!output) {
        throw std::runtime_error("Unable to write FASTA QC output");
    }
}

int run(
    const std::filesystem::path& invocation_path,
    const std::string_view module_id,
    const std::string_view step_id
) {
    const auto invocation =
        biocore::plugin_protocol::parse_plugin_invocation_document(
            read_invocation(invocation_path)
        );

    validate_document_contract(invocation);
    if (invocation.module_id != module_id || invocation.step_id != step_id) {
        throw std::invalid_argument("FASTA QC invocation identity mismatch");
    }

    const auto& input = invocation.inputs.front();
    const auto& json_output = find_output(invocation, "summary");
    const auto& tsv_output = find_output(invocation, "table");

    std::ifstream fasta{std::filesystem::path{input.path}, std::ios::binary};
    if (!fasta) {
        throw std::runtime_error("Unable to open FASTA input");
    }

    const auto statistics = biocore::fasta_qc::analyze_fasta(fasta);
    const std::string json = biocore::fasta_qc::render_summary_json(statistics);
    const std::string tsv = biocore::fasta_qc::render_summary_tsv(statistics);

    write_text_file(std::filesystem::path{json_output.path}, json);
    write_text_file(std::filesystem::path{tsv_output.path}, tsv);
    return EXIT_SUCCESS;
}

}  // namespace

int main(const int argc, const char* const argv[]) {
    if (argc != 7 || std::string_view{argv[1]} != "--module-id" ||
        std::string_view{argv[3]} != "--step-id" ||
        std::string_view{argv[5]} != "--invocation") {
        std::cerr
            << "Usage: biocore-fasta-qc-plugin --module-id <module-id> "
               "--step-id <step-id> --invocation <snapshot-path>\n";
        return 2;
    }

    const std::string_view module_id{argv[2]};
    const std::string_view step_id{argv[4]};
    if (module_id.empty() || step_id.empty()) return 2;

    try {
        return run(std::filesystem::path{argv[6]}, module_id, step_id);
    } catch (const std::exception& error) {
        std::cerr << "FASTA QC failed: " << error.what() << '\n';
        return 3;
    }
}
