#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

#include "biocore/plugin_protocol/plugin_document_codec.hpp"

namespace {

[[nodiscard]] std::string read_file(const std::filesystem::path& path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size == 0U || size > biocore::plugin_protocol::maximum_plugin_invocation_bytes) {
        throw std::invalid_argument("Invalid demo plugin invocation file size");
    }
    std::ifstream input{path, std::ios::binary};
    if (!input) throw std::runtime_error("Unable to open demo plugin invocation");
    std::string content{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    if (input.bad()) throw std::runtime_error("Unable to read demo plugin invocation");
    return content;
}

[[nodiscard]] const biocore::plugin_protocol::PluginInvocationParameterDocument* parameter(
    const biocore::plugin_protocol::PluginInvocationDocument& document,
    const std::string_view name
) {
    for (const auto& value : document.parameters) if (value.name == name) return &value;
    return nullptr;
}

[[nodiscard]] int run_copy(
    const biocore::plugin_protocol::PluginInvocationDocument& document
) {
    if (document.inputs.size() != 1U || document.inputs.front().port != "source" ||
        document.outputs.size() != 1U || document.outputs.front().port != "result") {
        throw std::invalid_argument("Demo copy module requires source and result bindings");
    }
    const auto* label = parameter(document, "label");
    const auto* repeat = parameter(document, "repeat");
    const auto* enabled = parameter(document, "enabled");
    const auto* mode = parameter(document, "mode");
    if (label == nullptr || repeat == nullptr || enabled == nullptr || mode == nullptr) {
        throw std::invalid_argument("Demo copy module parameters are incomplete");
    }
    const auto repeat_value = std::stoll(repeat->value);
    if (repeat_value < 1 || repeat_value > 3 ||
        (enabled->value != "true" && enabled->value != "false") ||
        (mode->value != "copy" && mode->value != "touch")) {
        throw std::invalid_argument("Demo copy module parameters are invalid");
    }
    const std::filesystem::path source{document.inputs.front().path};
    const std::filesystem::path result{document.outputs.front().path};
    std::error_code error;
    if (!std::filesystem::is_regular_file(source, error) || error ||
        std::filesystem::exists(result, error) || error) {
        throw std::invalid_argument("Demo copy module file bindings are invalid");
    }
    std::ofstream output{result, std::ios::binary | std::ios::trunc};
    if (!output) throw std::runtime_error("Unable to create demo plugin output");
    if (enabled->value == "true" && mode->value == "copy") {
        std::ifstream input{source, std::ios::binary};
        if (!input) throw std::runtime_error("Unable to open demo plugin source");
        output << input.rdbuf();
        if (!input.eof() && input.fail()) throw std::runtime_error("Unable to read demo plugin source");
    }
    for (long long index = 0; index < repeat_value; ++index) {
        output << "\n" << label->value;
    }
    output.flush();
    if (!output) throw std::runtime_error("Unable to write demo plugin output");
    return EXIT_SUCCESS;
}


[[nodiscard]] int run_multi(
    const biocore::plugin_protocol::PluginInvocationDocument& document
) {
    if (!document.inputs.empty() || !document.parameters.empty() ||
        document.outputs.size() != 2U || document.outputs[0].port != "left" ||
        document.outputs[1].port != "right") {
        throw std::invalid_argument("Demo multi module requires left and right output bindings");
    }

    const std::filesystem::path left{document.outputs[0].path};
    const std::filesystem::path right{document.outputs[1].path};
    std::error_code error;
    if (std::filesystem::exists(left, error) || error ||
        std::filesystem::exists(right, error) || error) {
        throw std::invalid_argument("Demo multi module output bindings are invalid");
    }

    std::ofstream left_output{left, std::ios::binary | std::ios::trunc};
    if (!left_output) throw std::runtime_error("Unable to create demo multi left output");
    left_output << "left";
    left_output.flush();
    if (!left_output) throw std::runtime_error("Unable to write demo multi left output");

    std::ofstream right_output{right, std::ios::binary | std::ios::trunc};
    if (!right_output) throw std::runtime_error("Unable to create demo multi right output");
    right_output << "right";
    right_output.flush();
    if (!right_output) throw std::runtime_error("Unable to write demo multi right output");
    return EXIT_SUCCESS;
}


[[nodiscard]] int run_fail_with_partial_output(
    const biocore::plugin_protocol::PluginInvocationDocument& document
) {
    if (document.outputs.size() != 1U || document.outputs.front().port != "partial") {
        throw std::invalid_argument("Demo fail module requires partial output binding");
    }
    const std::filesystem::path result{document.outputs.front().path};
    std::error_code error;
    if (std::filesystem::exists(result, error) || error) {
        throw std::invalid_argument("Demo fail module output binding is invalid");
    }
    std::ofstream output{result, std::ios::binary | std::ios::trunc};
    if (!output) throw std::runtime_error("Unable to create demo fail partial output");
    output << "partial";
    output.flush();
    if (!output) throw std::runtime_error("Unable to write demo fail partial output");
    return 3;
}

[[nodiscard]] bool supported_static_module(const std::string_view value) noexcept {
    return value == "org.biocore.demo.validate" || value == "org.biocore.demo.scan" ||
           value == "org.biocore.demo.report";
}

}  // namespace

int main(const int argc, const char* const argv[]) {
    try {
        if (argc != 7 || std::string_view{argv[1]} != "--module-id" ||
            std::string_view{argv[3]} != "--step-id" ||
            std::string_view{argv[5]} != "--invocation") {
            std::cerr << "Invalid demo plugin arguments\n";
            return 2;
        }
        const std::string_view module_id{argv[2]};
        const std::string_view step_id{argv[4]};
        if (step_id.empty()) return 2;
        const auto invocation = biocore::plugin_protocol::parse_plugin_invocation_document(
            read_file(std::filesystem::path{argv[6]})
        );
        if (invocation.step_id != step_id || invocation.module_id != module_id) {
            throw std::invalid_argument("Demo plugin invocation identity mismatch");
        }
        std::cout << "demo-plugin-stdout-must-not-enter-worker-protocol\n";
        if (module_id == "org.biocore.demo.copy") return run_copy(invocation);
        if (module_id == "org.biocore.demo.multi") return run_multi(invocation);
        if (module_id == "org.biocore.demo.fail") return run_fail_with_partial_output(invocation);
        if (supported_static_module(module_id)) return EXIT_SUCCESS;
        std::cerr << "Unsupported demo plugin module\n";
        return 3;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 3;
    }
}
