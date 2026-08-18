#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace biocore::domain {

enum class PluginParameterType {
    string,
    integer,
    number,
    boolean,
    enumeration,
};

using PluginParameterValue = std::variant<std::string, std::int64_t, double, bool>;

[[nodiscard]] std::string_view to_string(PluginParameterType type) noexcept;
[[nodiscard]] std::optional<PluginParameterType> plugin_parameter_type_from_string(
    std::string_view value
) noexcept;
[[nodiscard]] std::string plugin_parameter_value_to_string(
    const PluginParameterValue& value,
    PluginParameterType type
);
[[nodiscard]] PluginParameterValue plugin_parameter_value_from_string(
    std::string_view value,
    PluginParameterType type
);

class PluginParameterDefinition final {
public:
    static constexpr std::size_t maximum_name_length = 64U;
    static constexpr std::size_t maximum_string_length = 4096U;
    static constexpr std::size_t maximum_enum_values = 128U;

    PluginParameterDefinition(
        std::string name,
        PluginParameterType type,
        bool required,
        std::optional<PluginParameterValue> default_value = std::nullopt,
        std::optional<double> minimum = std::nullopt,
        std::optional<double> maximum = std::nullopt,
        std::vector<std::string> enum_values = {}
    );

    [[nodiscard]] std::string_view name() const noexcept;
    [[nodiscard]] PluginParameterType type() const noexcept;
    [[nodiscard]] bool required() const noexcept;
    [[nodiscard]] const std::optional<PluginParameterValue>& default_value() const noexcept;
    [[nodiscard]] const std::optional<double>& minimum() const noexcept;
    [[nodiscard]] const std::optional<double>& maximum() const noexcept;
    [[nodiscard]] const std::vector<std::string>& enum_values() const noexcept;

    void validate_value(const PluginParameterValue& value) const;

private:
    std::string name_;
    PluginParameterType type_;
    bool required_;
    std::optional<PluginParameterValue> default_value_;
    std::optional<double> minimum_;
    std::optional<double> maximum_;
    std::vector<std::string> enum_values_;
};

class PluginInputPortDefinition final {
public:
    static constexpr std::size_t maximum_name_length = 64U;
    static constexpr std::size_t maximum_file_types = 64U;
    static constexpr std::size_t maximum_file_type_length = 128U;

    PluginInputPortDefinition(
        std::string name,
        bool required,
        std::vector<std::string> accepted_file_types
    );

    [[nodiscard]] std::string_view name() const noexcept;
    [[nodiscard]] bool required() const noexcept;
    [[nodiscard]] const std::vector<std::string>& accepted_file_types() const noexcept;
    [[nodiscard]] bool accepts_file_type(std::string_view file_type) const noexcept;

private:
    std::string name_;
    bool required_;
    std::vector<std::string> accepted_file_types_;
};

class PluginOutputPortDefinition final {
public:
    static constexpr std::size_t maximum_name_length = 64U;
    static constexpr std::size_t maximum_file_type_length = 128U;

    PluginOutputPortDefinition(std::string name, std::string file_type);

    [[nodiscard]] std::string_view name() const noexcept;
    [[nodiscard]] std::string_view file_type() const noexcept;

private:
    std::string name_;
    std::string file_type_;
};

}  // namespace biocore::domain
