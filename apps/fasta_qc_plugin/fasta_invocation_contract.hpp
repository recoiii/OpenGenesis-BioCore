#pragma once
#include <cstddef>
#include <string_view>
#include <vector>

namespace biocore::fasta_qc {
struct InvocationInput final {
    std::string_view port;
    std::string_view source_kind;
    std::string_view file_type;
};
struct InvocationOutput final {
    std::string_view port;
    std::string_view file_type;
};
void validate_invocation_contract(
    std::string_view module_id,
    std::size_t parameter_count,
    const std::vector<InvocationInput>& inputs,
    const std::vector<InvocationOutput>& outputs
);
}  // namespace biocore::fasta_qc
