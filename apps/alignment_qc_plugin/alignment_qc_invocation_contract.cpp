#include "alignment_qc_invocation_contract.hpp"

#include <stdexcept>
#include <unordered_set>

namespace biocore::alignment_qc {

void validate_invocation_contract(
    const std::string_view module_id,
    const std::vector<InvocationInput>& inputs,
    const std::vector<InvocationOutput>& outputs
) {
    if (module_id != "org.biocore.alignmentqc.summary") {
        throw std::invalid_argument("Unsupported alignment QC module id");
    }
    if (inputs.size() != 1U || inputs.front().port != "alignment" ||
        (inputs.front().source_kind != "managedFile" && inputs.front().source_kind != "stepOutput") ||
        (inputs.front().file_type != "sam" && inputs.front().file_type != "bam")) {
        throw std::invalid_argument("Alignment QC requires one SAM or BAM alignment input");
    }
    if (outputs.size() != 2U) {
        throw std::invalid_argument("Alignment QC requires JSON and TSV outputs");
    }
    std::unordered_set<std::string_view> seen;
    bool summary = false;
    bool table = false;
    for (const auto& output : outputs) {
        if (!seen.insert(output.port).second) {
            throw std::invalid_argument("Alignment QC output port is duplicated");
        }
        if (output.port == "summary" && output.file_type == "json") summary = true;
        else if (output.port == "table" && output.file_type == "tsv") table = true;
        else throw std::invalid_argument("Alignment QC output contract is invalid");
    }
    if (!summary || !table) throw std::invalid_argument("Alignment QC output is missing");
}

}  // namespace biocore::alignment_qc
