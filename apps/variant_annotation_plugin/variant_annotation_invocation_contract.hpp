#pragma once
#include <string_view>
#include <vector>

namespace biocore::variant_annotation {
struct InvocationInput { std::string_view port; std::string_view source_kind; std::string_view file_type; };
struct InvocationOutput { std::string_view port; std::string_view file_type; };
void validate_invocation_contract(
    std::string_view module_id,
    const std::vector<InvocationInput>& inputs,
    const std::vector<InvocationOutput>& outputs
);
}
