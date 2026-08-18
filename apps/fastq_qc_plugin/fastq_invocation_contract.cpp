#include "fastq_invocation_contract.hpp"

#include <stdexcept>
#include <string_view>
#include <unordered_set>

namespace biocore::fastq_qc {
namespace {

void validate_fastq_input(const InvocationInput& input, const std::string_view expected_port) {
    if (input.port != expected_port || input.source_kind != "managedFile" || input.file_type != "fastq") {
        throw std::invalid_argument("FASTQ input must be a managed-file FASTQ on the required port");
    }
}

void validate_parameter_set(
    const std::vector<InvocationParameter>& parameters,
    const std::vector<std::pair<std::string_view, std::string_view>>& expected
) {
    if (parameters.size() != expected.size()) throw std::invalid_argument("FASTQ module parameter count is invalid");
    std::unordered_set<std::string_view> seen;
    for (const auto& parameter : parameters) {
        if (!seen.insert(parameter.name).second) throw std::invalid_argument("FASTQ module parameter is duplicated");
        bool matched = false;
        for (const auto& [name, type] : expected) {
            if (parameter.name == name) {
                if (parameter.type != type) throw std::invalid_argument("FASTQ module parameter type is invalid");
                matched = true;
                break;
            }
        }
        if (!matched) throw std::invalid_argument("FASTQ module parameter name is invalid");
    }
}

void validate_exact_outputs(
    const std::vector<InvocationOutput>& outputs,
    const std::vector<std::pair<std::string_view, std::string_view>>& expected
) {
    if (outputs.size() != expected.size()) throw std::invalid_argument("FASTQ module output count is invalid");
    std::unordered_set<std::string_view> seen;
    for (const auto& output : outputs) {
        if (!seen.insert(output.port).second) throw std::invalid_argument("FASTQ module output is duplicated");
        bool matched = false;
        for (const auto& [port, type] : expected) {
            if (output.port == port && output.file_type == type) { matched = true; break; }
        }
        if (!matched) throw std::invalid_argument("FASTQ module output contract is invalid");
    }
}

void validate_single_input(const std::vector<InvocationInput>& inputs) {
    if (inputs.size() != 1U) throw std::invalid_argument("FASTQ single-end module requires exactly one input");
    validate_fastq_input(inputs.front(), "source");
}

void validate_paired_inputs(const std::vector<InvocationInput>& inputs) {
    if (inputs.size() != 2U) throw std::invalid_argument("FASTQ paired-end module requires exactly two inputs");
    const InvocationInput* read1 = nullptr;
    const InvocationInput* read2 = nullptr;
    for (const auto& input : inputs) {
        if (input.port == "read1") {
            if (read1 != nullptr) throw std::invalid_argument("FASTQ read1 input is duplicated");
            read1 = &input;
        } else if (input.port == "read2") {
            if (read2 != nullptr) throw std::invalid_argument("FASTQ read2 input is duplicated");
            read2 = &input;
        } else {
            throw std::invalid_argument("FASTQ paired-end input port is invalid");
        }
    }
    if (read1 == nullptr || read2 == nullptr) throw std::invalid_argument("FASTQ paired-end invocation is missing a mate input");
    validate_fastq_input(*read1, "read1");
    validate_fastq_input(*read2, "read2");
}

}  // namespace

void validate_invocation_contract(
    const std::string_view module_id,
    const std::vector<InvocationParameter>& parameters,
    const std::vector<InvocationInput>& inputs,
    const std::vector<InvocationOutput>& outputs
) {
    static const std::vector<std::pair<std::string_view, std::string_view>> single_trim_parameters{
        {"adapter-sequence", "string"}, {"min-adapter-overlap", "integer"},
        {"max-adapter-mismatches", "integer"}, {"quality-threshold", "integer"},
        {"minimum-length", "integer"},
    };
    static const std::vector<std::pair<std::string_view, std::string_view>> paired_trim_parameters{
        {"adapter-read1", "string"}, {"adapter-read2", "string"},
        {"min-adapter-overlap", "integer"}, {"max-adapter-mismatches", "integer"},
        {"quality-threshold", "integer"}, {"minimum-length", "integer"},
    };
    if (module_id == "org.biocore.fastqqc.stats") {
        if (!parameters.empty()) throw std::invalid_argument("FASTQ QC modules accept no parameters");
        validate_single_input(inputs);
        validate_exact_outputs(outputs, {{"summary", "json"}, {"table", "tsv"}});
    } else if (module_id == "org.biocore.fastqqc.paired-stats") {
        if (!parameters.empty()) throw std::invalid_argument("FASTQ QC modules accept no parameters");
        validate_paired_inputs(inputs);
        validate_exact_outputs(outputs, {{"summary", "json"}, {"table", "tsv"}});
    } else if (module_id == "org.biocore.fastqqc.trim-single") {
        validate_parameter_set(parameters, single_trim_parameters);
        validate_single_input(inputs);
        validate_exact_outputs(outputs, {{"trimmed", "fastq"}, {"summary", "json"}, {"table", "tsv"}});
    } else if (module_id == "org.biocore.fastqqc.trim-paired") {
        validate_parameter_set(parameters, paired_trim_parameters);
        validate_paired_inputs(inputs);
        validate_exact_outputs(outputs, {
            {"trimmed_read1", "fastq"}, {"trimmed_read2", "fastq"},
            {"summary", "json"}, {"table", "tsv"}
        });
    } else {
        throw std::invalid_argument("Unsupported FASTQ module id");
    }
}

}  // namespace biocore::fastq_qc
