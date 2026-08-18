#pragma once

#include <string_view>
#include <vector>

namespace biocore::fastq_qc {

struct InvocationParameter final {
    std::string_view name;
    std::string_view type;
    std::string_view value;
};

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
    const std::vector<InvocationParameter>& parameters,
    const std::vector<InvocationInput>& inputs,
    const std::vector<InvocationOutput>& outputs
);

}  // namespace biocore::fastq_qc
