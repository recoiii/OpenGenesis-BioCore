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
#include "vcf_qc.hpp"
#include "vcf_qc_invocation_contract.hpp"

namespace {

constexpr std::uint64_t maximum_vcf_input_bytes = 512ULL * 1024ULL * 1024ULL;

[[nodiscard]] std::string read_invocation(const std::filesystem::path& path) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error || std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status)) throw std::invalid_argument("VCF-QC invocation must be a regular non-symlink file");
    const auto size = std::filesystem::file_size(path, error);
    if (error || size == 0U || size > biocore::plugin_protocol::maximum_plugin_invocation_bytes) throw std::invalid_argument("VCF-QC invocation file size is invalid");
    std::ifstream input{path, std::ios::binary};
    if (!input) throw std::runtime_error("Unable to open VCF-QC invocation");
    std::string content{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    if (input.bad() || content.size() != size) throw std::runtime_error("Unable to read VCF-QC invocation");
    return content;
}

[[nodiscard]] const biocore::plugin_protocol::PluginInvocationInputDocument& find_input(
    const biocore::plugin_protocol::PluginInvocationDocument& invocation, const std::string_view port
) {
    for (const auto& input : invocation.inputs) if (input.port == port) return input;
    throw std::logic_error("Validated VCF-QC input disappeared");
}

[[nodiscard]] const biocore::plugin_protocol::PluginInvocationOutputDocument& find_output(
    const biocore::plugin_protocol::PluginInvocationDocument& invocation, const std::string_view port
) {
    for (const auto& output : invocation.outputs) if (output.port == port) return output;
    throw std::logic_error("Validated VCF-QC output disappeared");
}

[[nodiscard]] const biocore::plugin_protocol::PluginInvocationParameterDocument& find_parameter(
    const biocore::plugin_protocol::PluginInvocationDocument& invocation, const std::string_view name
) {
    for (const auto& parameter : invocation.parameters) if (parameter.name == name) return parameter;
    throw std::logic_error("Validated VCF-QC parameter disappeared");
}

void validate_document_contract(const biocore::plugin_protocol::PluginInvocationDocument& invocation) {
    std::vector<biocore::vcf_qc::InvocationParameter> parameters;
    for (const auto& value : invocation.parameters) parameters.push_back({value.name, value.type, value.value});
    std::vector<biocore::vcf_qc::InvocationInput> inputs;
    for (const auto& value : invocation.inputs) inputs.push_back({value.port, value.source_kind, value.file_type});
    std::vector<biocore::vcf_qc::InvocationOutput> outputs;
    for (const auto& value : invocation.outputs) outputs.push_back({value.port, value.file_type});
    biocore::vcf_qc::validate_invocation_contract(invocation.module_id, parameters, inputs, outputs);
}

[[nodiscard]] std::uint64_t parse_u64(const std::string_view text, const std::string_view label) {
    std::uint64_t value = 0U;
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

[[nodiscard]] bool parse_boolean(const std::string_view text, const std::string_view label) {
    if (text == "true") return true;
    if (text == "false") return false;
    throw std::invalid_argument(std::string{label} + " is invalid");
}

[[nodiscard]] biocore::vcf_qc::VcfQcOptions options(const biocore::plugin_protocol::PluginInvocationDocument& invocation) {
    biocore::vcf_qc::VcfQcOptions result;
    result.depth_filter_enabled = parse_boolean(find_parameter(invocation, "enable-depth-filter").value, "enable-depth-filter");
    result.minimum_depth = parse_u64(find_parameter(invocation, "min-depth").value, "min-depth");
    result.alt_count_filter_enabled = parse_boolean(find_parameter(invocation, "enable-alt-count-filter").value, "enable-alt-count-filter");
    result.minimum_alt_count = parse_u64(find_parameter(invocation, "min-alt-count").value, "min-alt-count");
    result.alt_fraction_filter_enabled = parse_boolean(find_parameter(invocation, "enable-alt-fraction-filter").value, "enable-alt-fraction-filter");
    result.minimum_alt_fraction = parse_number(find_parameter(invocation, "min-alt-fraction").value, "min-alt-fraction");
    result.alt_base_quality_filter_enabled = parse_boolean(find_parameter(invocation, "enable-alt-base-quality-filter").value, "enable-alt-base-quality-filter");
    result.minimum_alt_base_quality = parse_number(find_parameter(invocation, "min-alt-base-quality").value, "min-alt-base-quality");
    biocore::vcf_qc::validate_vcf_qc_options(result);
    return result;
}

void validate_vcf_file(const std::filesystem::path& path) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error || std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status)) throw std::invalid_argument("VCF input must be a regular non-symlink file");
    const auto size = std::filesystem::file_size(path, error);
    if (error || size == 0U || size > maximum_vcf_input_bytes) throw std::invalid_argument("VCF input file size is invalid");
}

void write_text(const std::filesystem::path& path, const std::string_view content) {
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    if (!output) throw std::runtime_error("Unable to open VCF-QC output");
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    output.flush();
    if (!output) throw std::runtime_error("Unable to write VCF-QC output");
}

int run(const std::filesystem::path& invocation_path, const std::string_view module_id, const std::string_view step_id) {
    const auto invocation = biocore::plugin_protocol::parse_plugin_invocation_document(read_invocation(invocation_path));
    validate_document_contract(invocation);
    if (invocation.module_id != module_id || invocation.step_id != step_id) throw std::invalid_argument("VCF-QC invocation identity mismatch");
    const auto& source = find_input(invocation, "variants");
    const std::filesystem::path input_path{source.path};
    validate_vcf_file(input_path);
    std::ifstream input{input_path, std::ios::binary};
    if (!input) throw std::runtime_error("Unable to open VCF input");
    const auto selected_options = options(invocation);
    const auto result = biocore::vcf_qc::process_vcf(input, selected_options);
    write_text(std::filesystem::path{find_output(invocation, "filtered").path}, result.filtered_vcf);
    write_text(std::filesystem::path{find_output(invocation, "summary").path}, biocore::vcf_qc::render_vcf_qc_json(result.statistics, selected_options));
    write_text(std::filesystem::path{find_output(invocation, "table").path}, biocore::vcf_qc::render_vcf_qc_tsv(result.statistics));
    return EXIT_SUCCESS;
}

}  // namespace

int main(const int argc, const char* const argv[]) {
    if (argc != 7 || std::string_view{argv[1]} != "--module-id" || std::string_view{argv[3]} != "--step-id" || std::string_view{argv[5]} != "--invocation") {
        std::cerr << "Usage: biocore-vcf-qc-plugin --module-id <module-id> --step-id <step-id> --invocation <snapshot-path>\n";
        return 2;
    }
    const std::string_view module_id{argv[2]};
    const std::string_view step_id{argv[4]};
    if (module_id.empty() || step_id.empty()) return 2;
    try { return run(std::filesystem::path{argv[6]}, module_id, step_id); }
    catch (const std::exception& error) {
        std::cerr << "VCF QC/filtering failed: " << error.what() << '\n';
        return 3;
    }
}
