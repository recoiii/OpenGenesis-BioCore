#include <charconv>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "biocore/plugin_protocol/plugin_document_codec.hpp"

#include "variant_calling.hpp"
#include "variant_calling_invocation_contract.hpp"

namespace {

[[nodiscard]] std::string read_invocation(const std::filesystem::path& path) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error || std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status)) {
        throw std::invalid_argument("Variant invocation must be a regular non-symlink file");
    }
    const auto size = std::filesystem::file_size(path, error);
    if (error || size == 0U || size > biocore::plugin_protocol::maximum_plugin_invocation_bytes) {
        throw std::invalid_argument("Variant invocation file size is invalid");
    }
    std::ifstream input{path, std::ios::binary};
    if (!input) throw std::runtime_error("Unable to open variant invocation");
    std::string content{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    if (input.bad() || content.size() != size) throw std::runtime_error("Unable to read variant invocation");
    return content;
}

[[nodiscard]] const biocore::plugin_protocol::PluginInvocationInputDocument& find_input(
    const biocore::plugin_protocol::PluginInvocationDocument& invocation,
    const std::string_view port
) {
    for (const auto& input : invocation.inputs) if (input.port == port) return input;
    throw std::logic_error("Validated variant input disappeared");
}

[[nodiscard]] const biocore::plugin_protocol::PluginInvocationOutputDocument& find_output(
    const biocore::plugin_protocol::PluginInvocationDocument& invocation,
    const std::string_view port
) {
    for (const auto& output : invocation.outputs) if (output.port == port) return output;
    throw std::logic_error("Validated variant output disappeared");
}

[[nodiscard]] const biocore::plugin_protocol::PluginInvocationParameterDocument& find_parameter(
    const biocore::plugin_protocol::PluginInvocationDocument& invocation,
    const std::string_view name
) {
    for (const auto& parameter : invocation.parameters) if (parameter.name == name) return parameter;
    throw std::logic_error("Validated variant parameter disappeared");
}

void validate_document_contract(const biocore::plugin_protocol::PluginInvocationDocument& invocation) {
    std::vector<biocore::variant_calling::InvocationParameter> parameters;
    for (const auto& value : invocation.parameters) parameters.push_back({value.name, value.type, value.value});
    std::vector<biocore::variant_calling::InvocationInput> inputs;
    for (const auto& value : invocation.inputs) inputs.push_back({value.port, value.source_kind, value.file_type});
    std::vector<biocore::variant_calling::InvocationOutput> outputs;
    for (const auto& value : invocation.outputs) outputs.push_back({value.port, value.file_type});
    biocore::variant_calling::validate_invocation_contract(invocation.module_id, parameters, inputs, outputs);
}

[[nodiscard]] std::uint32_t parse_u32(const std::string_view text, const std::string_view label) {
    std::uint32_t value = 0U;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value, 10);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) throw std::invalid_argument(std::string{label} + " is invalid");
    return value;
}

[[nodiscard]] double parse_number(const std::string_view text, const std::string_view label) {
    double value = 0.0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value, std::chars_format::general);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || !std::isfinite(value)) throw std::invalid_argument(std::string{label} + " is invalid");
    return value;
}

[[nodiscard]] biocore::variant_calling::VariantCallingOptions options(
    const biocore::plugin_protocol::PluginInvocationDocument& invocation
) {
    biocore::variant_calling::VariantCallingOptions result;
    result.minimum_depth = parse_u32(find_parameter(invocation, "min-depth").value, "min-depth");
    result.minimum_alt_count = parse_u32(find_parameter(invocation, "min-alt-count").value, "min-alt-count");
    result.minimum_alt_fraction = parse_number(find_parameter(invocation, "min-alt-fraction").value, "min-alt-fraction");
    result.minimum_mapq = parse_u32(find_parameter(invocation, "min-mapq").value, "min-mapq");
    result.minimum_base_quality = parse_u32(find_parameter(invocation, "min-base-quality").value, "min-base-quality");
    biocore::variant_calling::validate_variant_calling_options(result);
    return result;
}

void write_text(const std::filesystem::path& path, const std::string_view content) {
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    if (!output) throw std::runtime_error("Unable to open variant-calling output");
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    output.flush();
    if (!output) throw std::runtime_error("Unable to write variant-calling output");
}

int run(const std::filesystem::path& invocation_path, const std::string_view module_id, const std::string_view step_id) {
    const auto invocation = biocore::plugin_protocol::parse_plugin_invocation_document(read_invocation(invocation_path));
    validate_document_contract(invocation);
    if (invocation.module_id != module_id || invocation.step_id != step_id) throw std::invalid_argument("Variant invocation identity mismatch");

    const auto reference = biocore::variant_calling::read_reference_fasta(std::filesystem::path{find_input(invocation, "reference").path});
    const auto caller_options = options(invocation);
    const auto& alignment_input = find_input(invocation, "alignment");
    const auto statistics = biocore::variant_calling::call_variants_from_alignment_file(
        std::filesystem::path{alignment_input.path}, alignment_input.file_type, reference, caller_options
    );
    write_text(std::filesystem::path{find_output(invocation, "variants").path}, biocore::variant_calling::render_vcf(statistics, reference, caller_options));
    write_text(std::filesystem::path{find_output(invocation, "summary").path}, biocore::variant_calling::render_variant_json(statistics, caller_options));
    write_text(std::filesystem::path{find_output(invocation, "table").path}, biocore::variant_calling::render_variant_tsv(statistics));
    return EXIT_SUCCESS;
}

}  // namespace

int main(const int argc, const char* const argv[]) {
    if (argc != 7 || std::string_view{argv[1]} != "--module-id" ||
        std::string_view{argv[3]} != "--step-id" || std::string_view{argv[5]} != "--invocation") {
        std::cerr << "Usage: biocore-variant-call-plugin --module-id <module-id> --step-id <step-id> --invocation <snapshot-path>\n";
        return 2;
    }
    const std::string_view module_id{argv[2]};
    const std::string_view step_id{argv[4]};
    if (module_id.empty() || step_id.empty()) return 2;
    try {
        return run(std::filesystem::path{argv[6]}, module_id, step_id);
    } catch (const std::exception& error) {
        std::cerr << "Variant calling failed: " << error.what() << '\n';
        return 3;
    }
}
