#include "variant_calling_invocation_contract.hpp"

#include <array>
#include <stdexcept>
#include <unordered_set>

namespace biocore::variant_calling {

void validate_invocation_contract(
    const std::string_view module_id,
    const std::vector<InvocationParameter>& parameters,
    const std::vector<InvocationInput>& inputs,
    const std::vector<InvocationOutput>& outputs
) {
    if (module_id != "org.biocore.variantcall.snv") {
        throw std::invalid_argument("Unsupported variant-calling module id");
    }
    if (parameters.size() != 5U) {
        throw std::invalid_argument("Variant calling requires five parameters");
    }
    const std::array<std::pair<std::string_view, std::string_view>, 5> expected{{
        {"min-depth", "integer"},
        {"min-alt-count", "integer"},
        {"min-alt-fraction", "number"},
        {"min-mapq", "integer"},
        {"min-base-quality", "integer"},
    }};
    std::unordered_set<std::string_view> seen_parameters;
    for (const auto& parameter : parameters) {
        if (!seen_parameters.insert(parameter.name).second) {
            throw std::invalid_argument("Variant-calling parameter is duplicated");
        }
        bool matched = false;
        for (const auto& [name, type] : expected) {
            if (parameter.name == name) {
                if (parameter.type != type) {
                    throw std::invalid_argument("Variant-calling parameter type is invalid");
                }
                matched = true;
                break;
            }
        }
        if (!matched) throw std::invalid_argument("Variant-calling parameter is unknown");
    }
    for (const auto& [name, type] : expected) {
        static_cast<void>(type);
        if (!seen_parameters.contains(name)) {
            throw std::invalid_argument("Variant-calling parameter is missing");
        }
    }

    if (inputs.size() != 2U) {
        throw std::invalid_argument("Variant calling requires reference and alignment inputs");
    }
    bool reference = false;
    bool alignment = false;
    std::unordered_set<std::string_view> seen_inputs;
    for (const auto& input : inputs) {
        if (!seen_inputs.insert(input.port).second) {
            throw std::invalid_argument("Variant-calling input port is duplicated");
        }
        if (input.source_kind != "managedFile" && input.source_kind != "stepOutput") {
            throw std::invalid_argument("Variant-calling input source kind is invalid");
        }
        if (input.port == "reference" && input.file_type == "fasta") reference = true;
        else if (input.port == "alignment" && (input.file_type == "sam" || input.file_type == "bam")) alignment = true;
        else throw std::invalid_argument("Variant-calling input contract is invalid");
    }
    if (!reference || !alignment) {
        throw std::invalid_argument("Variant calling requires FASTA reference and SAM/BAM alignment");
    }

    if (outputs.size() != 3U) {
        throw std::invalid_argument("Variant calling requires VCF, JSON and TSV outputs");
    }
    bool vcf = false;
    bool summary = false;
    bool table = false;
    std::unordered_set<std::string_view> seen_outputs;
    for (const auto& output : outputs) {
        if (!seen_outputs.insert(output.port).second) {
            throw std::invalid_argument("Variant-calling output port is duplicated");
        }
        if (output.port == "variants" && output.file_type == "vcf") vcf = true;
        else if (output.port == "summary" && output.file_type == "json") summary = true;
        else if (output.port == "table" && output.file_type == "tsv") table = true;
        else throw std::invalid_argument("Variant-calling output contract is invalid");
    }
    if (!vcf || !summary || !table) {
        throw std::invalid_argument("Variant-calling output is missing");
    }
}

}  // namespace biocore::variant_calling
