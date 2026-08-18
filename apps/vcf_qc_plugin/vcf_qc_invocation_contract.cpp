#include "vcf_qc_invocation_contract.hpp"

#include <array>
#include <stdexcept>
#include <unordered_set>

namespace biocore::vcf_qc {

void validate_invocation_contract(
    const std::string_view module_id,
    const std::vector<InvocationParameter>& parameters,
    const std::vector<InvocationInput>& inputs,
    const std::vector<InvocationOutput>& outputs
) {
    if (module_id != "org.biocore.vcfqc.filter") throw std::invalid_argument("Unsupported VCF-QC module id");
    if (parameters.size() != 8U) throw std::invalid_argument("VCF-QC requires eight parameters");
    const std::array<std::pair<std::string_view, std::string_view>, 8> expected{{
        {"enable-depth-filter", "boolean"},
        {"min-depth", "integer"},
        {"enable-alt-count-filter", "boolean"},
        {"min-alt-count", "integer"},
        {"enable-alt-fraction-filter", "boolean"},
        {"min-alt-fraction", "number"},
        {"enable-alt-base-quality-filter", "boolean"},
        {"min-alt-base-quality", "number"},
    }};
    std::unordered_set<std::string_view> seen;
    for (const auto& parameter : parameters) {
        if (!seen.insert(parameter.name).second) throw std::invalid_argument("VCF-QC parameter is duplicated");
        bool matched = false;
        for (const auto& [name, type] : expected) {
            if (parameter.name == name) {
                if (parameter.type != type) throw std::invalid_argument("VCF-QC parameter type is invalid");
                matched = true;
                break;
            }
        }
        if (!matched) throw std::invalid_argument("VCF-QC parameter is unknown");
    }
    for (const auto& [name, type] : expected) {
        static_cast<void>(type);
        if (!seen.contains(name)) throw std::invalid_argument("VCF-QC parameter is missing");
    }

    if (inputs.size() != 1U || inputs.front().port != "variants" || inputs.front().file_type != "vcf" ||
        (inputs.front().source_kind != "managedFile" && inputs.front().source_kind != "stepOutput")) {
        throw std::invalid_argument("VCF-QC requires one managed/step-output VCF input");
    }
    if (outputs.size() != 3U) throw std::invalid_argument("VCF-QC requires VCF, JSON and TSV outputs");
    bool filtered = false;
    bool summary = false;
    bool table = false;
    std::unordered_set<std::string_view> output_ports;
    for (const auto& output : outputs) {
        if (!output_ports.insert(output.port).second) throw std::invalid_argument("VCF-QC output port is duplicated");
        if (output.port == "filtered" && output.file_type == "vcf") filtered = true;
        else if (output.port == "summary" && output.file_type == "json") summary = true;
        else if (output.port == "table" && output.file_type == "tsv") table = true;
        else throw std::invalid_argument("VCF-QC output contract is invalid");
    }
    if (!filtered || !summary || !table) throw std::invalid_argument("VCF-QC output is missing");
}

}  // namespace biocore::vcf_qc
