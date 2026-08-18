#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "biocore/plugin_protocol/plugin_document_codec.hpp"

#include "alignment_invocation_contract.hpp"
#include "alignment_io.hpp"
#include "reference_alignment.hpp"

namespace {

[[nodiscard]] std::string read_invocation(const std::filesystem::path& path) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error || std::filesystem::is_symlink(status) ||
        !std::filesystem::is_regular_file(status)) {
        throw std::invalid_argument("Alignment invocation must be a regular non-symlink file");
    }
    const auto size = std::filesystem::file_size(path, error);
    if (error || size == 0U || size > biocore::plugin_protocol::maximum_plugin_invocation_bytes) {
        throw std::invalid_argument("Alignment invocation file size is invalid");
    }
    std::ifstream input{path, std::ios::binary};
    if (!input) throw std::runtime_error("Unable to open alignment invocation");
    std::string content{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    if (input.bad() || content.size() != size) throw std::runtime_error("Unable to read alignment invocation");
    return content;
}

[[nodiscard]] const biocore::plugin_protocol::PluginInvocationInputDocument& find_input(
    const biocore::plugin_protocol::PluginInvocationDocument& invocation,
    const std::string_view port
) {
    for (const auto& input : invocation.inputs) if (input.port == port) return input;
    throw std::logic_error("Validated alignment input disappeared");
}

[[nodiscard]] const biocore::plugin_protocol::PluginInvocationOutputDocument& find_output(
    const biocore::plugin_protocol::PluginInvocationDocument& invocation,
    const std::string_view port
) {
    for (const auto& output : invocation.outputs) if (output.port == port) return output;
    throw std::logic_error("Validated alignment output disappeared");
}

void validate_document_contract(
    const biocore::plugin_protocol::PluginInvocationDocument& invocation
) {
    std::vector<biocore::alignment::InvocationParameter> parameters;
    for (const auto& value : invocation.parameters) {
        parameters.push_back({value.name, value.type, value.value});
    }
    std::vector<biocore::alignment::InvocationInput> inputs;
    for (const auto& value : invocation.inputs) {
        inputs.push_back({value.port, value.source_kind, value.file_type});
    }
    std::vector<biocore::alignment::InvocationOutput> outputs;
    for (const auto& value : invocation.outputs) {
        outputs.push_back({value.port, value.file_type});
    }
    biocore::alignment::validate_invocation_contract(
        invocation.module_id, parameters, inputs, outputs
    );
}

[[nodiscard]] biocore::alignment::AlignmentOptions options(
    const biocore::plugin_protocol::PluginInvocationDocument& invocation
) {
    if (invocation.parameters.size() != 1U) throw std::logic_error("Validated alignment parameter disappeared");
    std::size_t consumed = 0U;
    unsigned long value = 0U;
    try {
        value = std::stoul(invocation.parameters.front().value, &consumed, 10);
    } catch (...) {
        throw std::invalid_argument("Alignment max-mismatches parameter is invalid");
    }
    if (consumed != invocation.parameters.front().value.size() || value > 12UL) {
        throw std::invalid_argument("Alignment max-mismatches parameter is invalid");
    }
    biocore::alignment::AlignmentOptions result{static_cast<std::uint32_t>(value)};
    biocore::alignment::validate_alignment_options(result);
    return result;
}

void write_text(const std::filesystem::path& path, const std::string_view content) {
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    if (!output) throw std::runtime_error("Unable to open alignment output");
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    output.flush();
    if (!output) throw std::runtime_error("Unable to write alignment output");
}

int run(
    const std::filesystem::path& invocation_path,
    const std::string_view module_id,
    const std::string_view step_id
) {
    const auto invocation = biocore::plugin_protocol::parse_plugin_invocation_document(
        read_invocation(invocation_path)
    );
    validate_document_contract(invocation);
    if (invocation.module_id != module_id || invocation.step_id != step_id) {
        throw std::invalid_argument("Alignment invocation identity mismatch");
    }

    const auto reference = biocore::alignment::read_reference_fasta(
        std::filesystem::path{find_input(invocation, "reference").path}
    );
    const auto alignment_options = options(invocation);
    const auto& sam_output = find_output(invocation, "alignment");
    std::ofstream sam{std::filesystem::path{sam_output.path}, std::ios::binary | std::ios::trunc};
    if (!sam) throw std::runtime_error("Unable to open SAM output");

    std::string json;
    std::string tsv;
    if (invocation.module_id == "org.biocore.align.single") {
        auto reads = biocore::alignment::open_fastq_input(
            std::filesystem::path{find_input(invocation, "reads").path}
        );
        const auto statistics = biocore::alignment::align_single_fastq(
            reference, *reads, sam, alignment_options
        );
        json = biocore::alignment::render_single_alignment_json(statistics, alignment_options);
        tsv = biocore::alignment::render_single_alignment_tsv(statistics, alignment_options);
    } else {
        const auto& read1_doc = find_input(invocation, "read1");
        const auto& read2_doc = find_input(invocation, "read2");
        if (read1_doc.source_id == read2_doc.source_id || read1_doc.path == read2_doc.path) {
            throw std::invalid_argument("Paired alignment requires distinct read1 and read2 sources");
        }
        auto read1 = biocore::alignment::open_fastq_input(std::filesystem::path{read1_doc.path});
        auto read2 = biocore::alignment::open_fastq_input(std::filesystem::path{read2_doc.path});
        const auto statistics = biocore::alignment::align_paired_fastq(
            reference, *read1, *read2, sam, alignment_options
        );
        json = biocore::alignment::render_paired_alignment_json(statistics, alignment_options);
        tsv = biocore::alignment::render_paired_alignment_tsv(statistics, alignment_options);
    }
    sam.flush();
    if (!sam) throw std::runtime_error("Unable to finalize SAM output");

    write_text(std::filesystem::path{find_output(invocation, "summary").path}, json);
    write_text(std::filesystem::path{find_output(invocation, "table").path}, tsv);
    return EXIT_SUCCESS;
}

}  // namespace

int main(const int argc, const char* const argv[]) {
    if (argc != 7 || std::string_view{argv[1]} != "--module-id" ||
        std::string_view{argv[3]} != "--step-id" ||
        std::string_view{argv[5]} != "--invocation") {
        std::cerr << "Usage: biocore-alignment-plugin --module-id <module-id> "
                     "--step-id <step-id> --invocation <snapshot-path>\n";
        return 2;
    }
    const std::string_view module_id{argv[2]};
    const std::string_view step_id{argv[4]};
    if (module_id.empty() || step_id.empty()) return 2;
    try {
        return run(std::filesystem::path{argv[6]}, module_id, step_id);
    } catch (const std::exception& error) {
        std::cerr << "Alignment failed: " << error.what() << '\n';
        return 3;
    }
}
