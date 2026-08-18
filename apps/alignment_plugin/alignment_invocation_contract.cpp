#include "alignment_invocation_contract.hpp"

#include <stdexcept>
#include <unordered_set>

namespace biocore::alignment {
namespace {

void validate_parameters(const std::vector<InvocationParameter>& parameters) {
    if (parameters.size() != 1U || parameters.front().name != "max-mismatches" ||
        parameters.front().type != "integer") {
        throw std::invalid_argument("Alignment module requires max-mismatches integer parameter");
    }
}

void validate_input(
    const InvocationInput& input,
    const std::string_view port,
    const std::string_view type
) {
    const bool valid_source = input.source_kind == "managedFile" || input.source_kind == "stepOutput";
    if (input.port != port || !valid_source || input.file_type != type) {
        throw std::invalid_argument("Alignment input contract is invalid");
    }
}

const InvocationInput* find(
    const std::vector<InvocationInput>& inputs, const std::string_view port
) {
    const InvocationInput* found = nullptr;
    for (const auto& input : inputs) {
        if (input.port == port) {
            if (found != nullptr) throw std::invalid_argument("Alignment input port is duplicated");
            found = &input;
        }
    }
    return found;
}

void validate_outputs(const std::vector<InvocationOutput>& outputs) {
    if (outputs.size() != 3U) throw std::invalid_argument("Alignment output count is invalid");
    std::unordered_set<std::string_view> seen;
    bool sam = false, summary = false, table = false;
    for (const auto& output : outputs) {
        if (!seen.insert(output.port).second) throw std::invalid_argument("Alignment output is duplicated");
        if (output.port == "alignment" && output.file_type == "sam") sam = true;
        else if (output.port == "summary" && output.file_type == "json") summary = true;
        else if (output.port == "table" && output.file_type == "tsv") table = true;
        else throw std::invalid_argument("Alignment output contract is invalid");
    }
    if (!sam || !summary || !table) throw std::invalid_argument("Alignment output is missing");
}

}  // namespace

void validate_invocation_contract(
    const std::string_view module_id,
    const std::vector<InvocationParameter>& parameters,
    const std::vector<InvocationInput>& inputs,
    const std::vector<InvocationOutput>& outputs
) {
    validate_parameters(parameters);
    validate_outputs(outputs);

    if (module_id == "org.biocore.align.single") {
        if (inputs.size() != 2U) throw std::invalid_argument("Single alignment requires reference and reads");
        const auto* reference = find(inputs, "reference");
        const auto* reads = find(inputs, "reads");
        if (reference == nullptr || reads == nullptr) throw std::invalid_argument("Single alignment input is missing");
        validate_input(*reference, "reference", "fasta");
        validate_input(*reads, "reads", "fastq");
    } else if (module_id == "org.biocore.align.paired") {
        if (inputs.size() != 3U) throw std::invalid_argument("Paired alignment requires reference, read1, and read2");
        const auto* reference = find(inputs, "reference");
        const auto* read1 = find(inputs, "read1");
        const auto* read2 = find(inputs, "read2");
        if (reference == nullptr || read1 == nullptr || read2 == nullptr) {
            throw std::invalid_argument("Paired alignment input is missing");
        }
        validate_input(*reference, "reference", "fasta");
        validate_input(*read1, "read1", "fastq");
        validate_input(*read2, "read2", "fastq");
    } else {
        throw std::invalid_argument("Unsupported alignment module id");
    }
}

}  // namespace biocore::alignment
