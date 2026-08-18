#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace biocore::alignment_qc {

struct InvocationInput final {
    std::string port;
    std::string source_kind;
    std::string file_type;
};

struct InvocationOutput final {
    std::string port;
    std::string file_type;
};

void validate_invocation_contract(
    std::string_view module_id,
    const std::vector<InvocationInput>& inputs,
    const std::vector<InvocationOutput>& outputs
);

}  // namespace biocore::alignment_qc
