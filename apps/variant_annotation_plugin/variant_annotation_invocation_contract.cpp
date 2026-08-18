#include "variant_annotation_invocation_contract.hpp"
#include <stdexcept>
#include <unordered_set>

namespace biocore::variant_annotation {
void validate_invocation_contract(
    const std::string_view module_id,
    const std::vector<InvocationInput>& inputs,
    const std::vector<InvocationOutput>& outputs
) {
    if (module_id != "org.biocore.variantannotate.local") throw std::invalid_argument("Unsupported variant-annotation module id");
    if (inputs.size() != 2U) throw std::invalid_argument("Variant annotation requires VCF and TSV inputs");
    bool variants=false, annotations=false;
    std::unordered_set<std::string_view> in_ports;
    for (const auto& input : inputs) {
        if (!in_ports.insert(input.port).second) throw std::invalid_argument("Variant-annotation input port is duplicated");
        if (input.source_kind != "managedFile" && input.source_kind != "stepOutput") throw std::invalid_argument("Variant-annotation input source kind is invalid");
        if (input.port == "variants" && input.file_type == "vcf") variants=true;
        else if (input.port == "annotations" && input.file_type == "tsv") annotations=true;
        else throw std::invalid_argument("Variant-annotation input contract is invalid");
    }
    if (!variants || !annotations) throw std::invalid_argument("Variant-annotation input is missing");
    if (outputs.size() != 4U) throw std::invalid_argument("Variant annotation requires VCF, JSON, TSV and HTML outputs");
    bool vcf=false, json=false, tsv=false, html=false;
    std::unordered_set<std::string_view> out_ports;
    for (const auto& output : outputs) {
        if (!out_ports.insert(output.port).second) throw std::invalid_argument("Variant-annotation output port is duplicated");
        if (output.port=="annotated" && output.file_type=="vcf") vcf=true;
        else if (output.port=="summary" && output.file_type=="json") json=true;
        else if (output.port=="table" && output.file_type=="tsv") tsv=true;
        else if (output.port=="report" && output.file_type=="html") html=true;
        else throw std::invalid_argument("Variant-annotation output contract is invalid");
    }
    if (!vcf || !json || !tsv || !html) throw std::invalid_argument("Variant-annotation output is missing");
}
}
