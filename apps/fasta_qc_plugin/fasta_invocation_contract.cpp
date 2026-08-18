#include "fasta_invocation_contract.hpp"
#include <stdexcept>

namespace biocore::fasta_qc {
void validate_invocation_contract(
    const std::string_view module_id,
    const std::size_t parameter_count,
    const std::vector<InvocationInput>& inputs,
    const std::vector<InvocationOutput>& outputs
) {
    if (module_id != "org.biocore.fastaqc.stats") {
        throw std::invalid_argument("Unsupported FASTA QC module id");
    }
    if (parameter_count != 0U) {
        throw std::invalid_argument("FASTA QC stats module accepts no parameters");
    }
    if (inputs.size() != 1U ||
        inputs.front().port != "source" ||
        inputs.front().source_kind != "managedFile" ||
        inputs.front().file_type != "fasta") {
        throw std::invalid_argument(
            "FASTA QC requires exactly one managed-file 'source' input with fileType 'fasta'"
        );
    }
    if (outputs.size() != 2U) {
        throw std::invalid_argument("FASTA QC requires exactly two outputs");
    }

    bool summary = false;
    bool table = false;
    for (const auto& output : outputs) {
        if (output.port == "summary" && output.file_type == "json") {
            if (summary) throw std::invalid_argument("FASTA QC summary output is duplicated");
            summary = true;
        } else if (output.port == "table" && output.file_type == "tsv") {
            if (table) throw std::invalid_argument("FASTA QC table output is duplicated");
            table = true;
        } else {
            throw std::invalid_argument("FASTA QC output contract is invalid");
        }
    }
    if (!summary || !table) {
        throw std::invalid_argument("FASTA QC invocation is missing a required output");
    }
}
}  // namespace biocore::fasta_qc
