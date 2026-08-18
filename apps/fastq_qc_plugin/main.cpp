#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "biocore/plugin_protocol/plugin_document_codec.hpp"

#include "fastq_input_stream.hpp"
#include "fastq_invocation_contract.hpp"
#include "fastq_statistics.hpp"
#include "fastq_trimming.hpp"
#include "paired_fastq_statistics.hpp"

namespace {

[[nodiscard]] std::string read_invocation(const std::filesystem::path& path) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error || std::filesystem::is_symlink(status) ||
        !std::filesystem::is_regular_file(status)) {
        throw std::invalid_argument("FASTQ QC invocation must be a regular non-symlink file");
    }

    const auto size = std::filesystem::file_size(path, error);
    if (error || size == 0U ||
        size > biocore::plugin_protocol::maximum_plugin_invocation_bytes) {
        throw std::invalid_argument("Invalid FASTQ QC invocation file size");
    }

    std::ifstream input{path, std::ios::binary};
    if (!input) {
        throw std::runtime_error("Unable to open FASTQ QC invocation");
    }
    std::string content{
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{}
    };
    if (input.bad() || content.size() != size) {
        throw std::runtime_error("Unable to read FASTQ QC invocation");
    }
    return content;
}

[[nodiscard]] const biocore::plugin_protocol::PluginInvocationOutputDocument& find_output(
    const biocore::plugin_protocol::PluginInvocationDocument& invocation,
    const std::string_view port
) {
    for (const auto& output : invocation.outputs) {
        if (output.port == port) {
            return output;
        }
    }
    throw std::logic_error("Validated FASTQ QC output disappeared");
}

[[nodiscard]] const biocore::plugin_protocol::PluginInvocationInputDocument& find_input(
    const biocore::plugin_protocol::PluginInvocationDocument& invocation,
    const std::string_view port
) {
    for (const auto& input : invocation.inputs) {
        if (input.port == port) return input;
    }
    throw std::logic_error("Validated FASTQ QC input disappeared");
}

void validate_document_contract(
    const biocore::plugin_protocol::PluginInvocationDocument& invocation
) {
    std::vector<biocore::fastq_qc::InvocationParameter> parameters;
    parameters.reserve(invocation.parameters.size());
    for (const auto& parameter : invocation.parameters) {
        parameters.push_back(biocore::fastq_qc::InvocationParameter{
            parameter.name, parameter.type, parameter.value
        });
    }

    std::vector<biocore::fastq_qc::InvocationInput> inputs;
    inputs.reserve(invocation.inputs.size());
    for (const auto& input : invocation.inputs) {
        inputs.push_back(biocore::fastq_qc::InvocationInput{
            input.port, input.source_kind, input.file_type
        });
    }

    std::vector<biocore::fastq_qc::InvocationOutput> outputs;
    outputs.reserve(invocation.outputs.size());
    for (const auto& output : invocation.outputs) {
        outputs.push_back(biocore::fastq_qc::InvocationOutput{
            output.port, output.file_type
        });
    }

    biocore::fastq_qc::validate_invocation_contract(
        invocation.module_id, parameters, inputs, outputs
    );
}

void write_text_file(const std::filesystem::path& path, const std::string_view content) {
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    if (!output) {
        throw std::runtime_error("Unable to open FASTQ QC output");
    }
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    output.flush();
    if (!output) {
        throw std::runtime_error("Unable to write FASTQ QC output");
    }
}

[[nodiscard]] const biocore::plugin_protocol::PluginInvocationParameterDocument& find_parameter(
    const biocore::plugin_protocol::PluginInvocationDocument& invocation,
    const std::string_view name
) {
    for (const auto& parameter : invocation.parameters) {
        if (parameter.name == name) return parameter;
    }
    throw std::logic_error("Validated FASTQ parameter disappeared");
}

[[nodiscard]] std::uint64_t parse_unsigned_parameter(
    const biocore::plugin_protocol::PluginInvocationDocument& invocation,
    const std::string_view name
) {
    const auto& parameter = find_parameter(invocation, name);
    std::size_t consumed = 0U;
    unsigned long long value = 0U;
    try {
        value = std::stoull(parameter.value, &consumed, 10);
    } catch (...) {
        throw std::invalid_argument("FASTQ trimming integer parameter is invalid");
    }
    if (consumed != parameter.value.size()) {
        throw std::invalid_argument("FASTQ trimming integer parameter is invalid");
    }
    return static_cast<std::uint64_t>(value);
}

[[nodiscard]] biocore::fastq_qc::TrimmingOptions trimming_options(
    const biocore::plugin_protocol::PluginInvocationDocument& invocation,
    const bool paired
) {
    biocore::fastq_qc::TrimmingOptions options;
    if (paired) {
        options.adapter_read1 = find_parameter(invocation, "adapter-read1").value;
        options.adapter_read2 = find_parameter(invocation, "adapter-read2").value;
    } else {
        options.adapter_read1 = find_parameter(invocation, "adapter-sequence").value;
        options.adapter_read2.clear();
    }
    const auto overlap = parse_unsigned_parameter(invocation, "min-adapter-overlap");
    const auto mismatches = parse_unsigned_parameter(invocation, "max-adapter-mismatches");
    const auto quality = parse_unsigned_parameter(invocation, "quality-threshold");
    options.minimum_length = parse_unsigned_parameter(invocation, "minimum-length");
    if (overlap > std::numeric_limits<std::uint32_t>::max() ||
        mismatches > std::numeric_limits<std::uint32_t>::max() ||
        quality > std::numeric_limits<std::uint32_t>::max()) {
        throw std::invalid_argument("FASTQ trimming integer parameter is out of range");
    }
    options.minimum_adapter_overlap = static_cast<std::uint32_t>(overlap);
    options.maximum_adapter_mismatches = static_cast<std::uint32_t>(mismatches);
    options.quality_threshold = static_cast<std::uint32_t>(quality);
    biocore::fastq_qc::validate_trimming_options(options, paired);
    return options;
}

[[nodiscard]] std::ofstream open_output_file(const std::filesystem::path& path) {
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    if (!output) throw std::runtime_error("Unable to open FASTQ output");
    return output;
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
        throw std::invalid_argument("FASTQ QC invocation identity mismatch");
    }

    const auto& json_output = find_output(invocation, "summary");
    const auto& tsv_output = find_output(invocation, "table");

    std::string json;
    std::string tsv;
    if (invocation.module_id == "org.biocore.fastqqc.stats") {
        const auto& input = find_input(invocation, "source");
        auto fastq = biocore::fastq_qc::open_fastq_input(std::filesystem::path{input.path});
        const auto statistics = biocore::fastq_qc::analyze_fastq(*fastq);
        json = biocore::fastq_qc::render_summary_json(statistics);
        tsv = biocore::fastq_qc::render_summary_tsv(statistics);
    } else if (invocation.module_id == "org.biocore.fastqqc.paired-stats") {
        const auto& read1 = find_input(invocation, "read1");
        const auto& read2 = find_input(invocation, "read2");
        if (read1.source_id == read2.source_id || read1.path == read2.path) {
            throw std::invalid_argument("Paired FASTQ inputs must reference two distinct managed files");
        }
        auto input1 = biocore::fastq_qc::open_fastq_input(std::filesystem::path{read1.path});
        auto input2 = biocore::fastq_qc::open_fastq_input(std::filesystem::path{read2.path});
        const auto statistics = biocore::fastq_qc::analyze_paired_fastq(*input1, *input2);
        json = biocore::fastq_qc::render_paired_summary_json(statistics);
        tsv = biocore::fastq_qc::render_paired_summary_tsv(statistics);
    } else if (invocation.module_id == "org.biocore.fastqqc.trim-single") {
        const auto& input = find_input(invocation, "source");
        const auto& trimmed_output = find_output(invocation, "trimmed");
        auto fastq = biocore::fastq_qc::open_fastq_input(std::filesystem::path{input.path});
        auto trimmed = open_output_file(std::filesystem::path{trimmed_output.path});
        const auto options = trimming_options(invocation, false);
        const auto statistics = biocore::fastq_qc::trim_single_fastq(*fastq, trimmed, options);
        trimmed.flush();
        if (!trimmed) throw std::runtime_error("Unable to finalize trimmed FASTQ output");
        json = biocore::fastq_qc::render_single_trimming_json(statistics, options);
        tsv = biocore::fastq_qc::render_single_trimming_tsv(statistics, options);
    } else {
        const auto& read1 = find_input(invocation, "read1");
        const auto& read2 = find_input(invocation, "read2");
        if (read1.source_id == read2.source_id || read1.path == read2.path) {
            throw std::invalid_argument("Paired FASTQ inputs must reference two distinct managed files");
        }
        const auto& trimmed1_output = find_output(invocation, "trimmed_read1");
        const auto& trimmed2_output = find_output(invocation, "trimmed_read2");
        auto input1 = biocore::fastq_qc::open_fastq_input(std::filesystem::path{read1.path});
        auto input2 = biocore::fastq_qc::open_fastq_input(std::filesystem::path{read2.path});
        auto output1 = open_output_file(std::filesystem::path{trimmed1_output.path});
        auto output2 = open_output_file(std::filesystem::path{trimmed2_output.path});
        const auto options = trimming_options(invocation, true);
        const auto statistics = biocore::fastq_qc::trim_paired_fastq(*input1, *input2, output1, output2, options);
        output1.flush(); output2.flush();
        if (!output1 || !output2) throw std::runtime_error("Unable to finalize trimmed paired FASTQ output");
        json = biocore::fastq_qc::render_paired_trimming_json(statistics, options);
        tsv = biocore::fastq_qc::render_paired_trimming_tsv(statistics, options);
    }

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
            << "Usage: biocore-fastq-qc-plugin --module-id <module-id> "
               "--step-id <step-id> --invocation <snapshot-path>\n";
        return 2;
    }

    const std::string_view module_id{argv[2]};
    const std::string_view step_id{argv[4]};
    if (module_id.empty() || step_id.empty()) {
        return 2;
    }

    try {
        return run(std::filesystem::path{argv[6]}, module_id, step_id);
    } catch (const std::exception& error) {
        std::cerr << "FASTQ QC failed: " << error.what() << '\n';
        return 3;
    }
}
