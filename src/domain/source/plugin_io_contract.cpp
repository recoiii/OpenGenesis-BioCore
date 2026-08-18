#include "biocore/domain/plugin_io_contract.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cctype>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace biocore::domain {
namespace {

[[nodiscard]] bool valid_name(const std::string_view value, const std::size_t maximum_length) {
    if (value.empty() || value.size() > maximum_length ||
        value.find('\0') != std::string_view::npos) {
        return false;
    }
    if (!(value.front() >= 'a' && value.front() <= 'z')) return false;
    return std::ranges::all_of(value, [](const char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    });
}

void require_text(
    const std::string_view value,
    const std::string_view field,
    const std::size_t maximum_length
) {
    if (value.empty() || value.size() > maximum_length ||
        value.find('\0') != std::string_view::npos ||
        std::ranges::any_of(value, [](const char character) {
            return std::iscntrl(static_cast<unsigned char>(character)) != 0;
        })) {
        throw std::invalid_argument(std::string{field} + " is invalid");
    }
}

[[nodiscard]] bool value_matches_type(
    const PluginParameterValue& value,
    const PluginParameterType type
) noexcept {
    switch (type) {
        case PluginParameterType::string:
        case PluginParameterType::enumeration:
            return std::holds_alternative<std::string>(value);
        case PluginParameterType::integer:
            return std::holds_alternative<std::int64_t>(value);
        case PluginParameterType::number:
            return std::holds_alternative<double>(value);
        case PluginParameterType::boolean:
            return std::holds_alternative<bool>(value);
    }
    return false;
}

[[nodiscard]] double numeric_value(const PluginParameterValue& value) {
    if (const auto* integer = std::get_if<std::int64_t>(&value)) {
        return static_cast<double>(*integer);
    }
    if (const auto* number = std::get_if<double>(&value)) return *number;
    throw std::invalid_argument("Plugin parameter value is not numeric");
}

}  // namespace

std::string_view to_string(const PluginParameterType type) noexcept {
    switch (type) {
        case PluginParameterType::string: return "string";
        case PluginParameterType::integer: return "integer";
        case PluginParameterType::number: return "number";
        case PluginParameterType::boolean: return "boolean";
        case PluginParameterType::enumeration: return "enum";
    }
    return "unknown";
}

std::optional<PluginParameterType> plugin_parameter_type_from_string(
    const std::string_view value
) noexcept {
    if (value == "string") return PluginParameterType::string;
    if (value == "integer") return PluginParameterType::integer;
    if (value == "number") return PluginParameterType::number;
    if (value == "boolean") return PluginParameterType::boolean;
    if (value == "enum") return PluginParameterType::enumeration;
    return std::nullopt;
}

std::string plugin_parameter_value_to_string(
    const PluginParameterValue& value,
    const PluginParameterType type
) {
    if (!value_matches_type(value, type)) {
        throw std::invalid_argument("Plugin parameter value type does not match its definition");
    }
    switch (type) {
        case PluginParameterType::string:
        case PluginParameterType::enumeration:
            return std::get<std::string>(value);
        case PluginParameterType::integer:
            return std::to_string(std::get<std::int64_t>(value));
        case PluginParameterType::boolean:
            return std::get<bool>(value) ? "true" : "false";
        case PluginParameterType::number: {
            const double number = std::get<double>(value);
            if (!std::isfinite(number)) {
                throw std::invalid_argument("Plugin number parameter must be finite");
            }
            std::ostringstream output;
            output << std::setprecision(std::numeric_limits<double>::max_digits10) << number;
            return output.str();
        }
    }
    throw std::invalid_argument("Plugin parameter type is unsupported");
}

PluginParameterValue plugin_parameter_value_from_string(
    const std::string_view value,
    const PluginParameterType type
) {
    switch (type) {
        case PluginParameterType::string:
        case PluginParameterType::enumeration:
            return std::string{value};
        case PluginParameterType::boolean:
            if (value == "true") return true;
            if (value == "false") return false;
            throw std::invalid_argument("Plugin boolean parameter must be true or false");
        case PluginParameterType::integer: {
            std::int64_t parsed = 0;
            const auto [end, error] = std::from_chars(
                value.data(), value.data() + value.size(), parsed
            );
            if (error != std::errc{} || end != value.data() + value.size()) {
                throw std::invalid_argument("Plugin integer parameter is invalid");
            }
            return parsed;
        }
        case PluginParameterType::number: {
            double parsed = 0.0;
            const auto [end, error] = std::from_chars(
                value.data(), value.data() + value.size(), parsed, std::chars_format::general
            );
            if (error != std::errc{} || end != value.data() + value.size() ||
                !std::isfinite(parsed)) {
                throw std::invalid_argument("Plugin number parameter is invalid");
            }
            return parsed;
        }
    }
    throw std::invalid_argument("Plugin parameter type is unsupported");
}

PluginParameterDefinition::PluginParameterDefinition(
    std::string name,
    const PluginParameterType type,
    const bool required,
    std::optional<PluginParameterValue> default_value,
    std::optional<double> minimum,
    std::optional<double> maximum,
    std::vector<std::string> enum_values
)
    : name_{std::move(name)},
      type_{type},
      required_{required},
      default_value_{std::move(default_value)},
      minimum_{minimum},
      maximum_{maximum},
      enum_values_{std::move(enum_values)} {
    if (!valid_name(name_, maximum_name_length)) {
        throw std::invalid_argument("Plugin parameter name is invalid");
    }
    if ((minimum_.has_value() || maximum_.has_value()) &&
        type_ != PluginParameterType::integer && type_ != PluginParameterType::number) {
        throw std::invalid_argument("Only numeric plugin parameters may define bounds");
    }
    if ((minimum_.has_value() && !std::isfinite(*minimum_)) ||
        (maximum_.has_value() && !std::isfinite(*maximum_)) ||
        (minimum_.has_value() && maximum_.has_value() && *minimum_ > *maximum_)) {
        throw std::invalid_argument("Plugin parameter bounds are invalid");
    }
    if (type_ == PluginParameterType::enumeration) {
        if (enum_values_.empty() || enum_values_.size() > maximum_enum_values) {
            throw std::invalid_argument("Enumeration parameter values are invalid");
        }
        std::unordered_set<std::string_view> unique;
        unique.reserve(enum_values_.size());
        for (const std::string& value : enum_values_) {
            require_text(value, "Enumeration value", maximum_string_length);
            if (!unique.insert(value).second) {
                throw std::invalid_argument("Enumeration parameter contains duplicate values");
            }
        }
    } else if (!enum_values_.empty()) {
        throw std::invalid_argument("Only enumeration parameters may define enum values");
    }
    if (default_value_.has_value()) validate_value(*default_value_);
}

std::string_view PluginParameterDefinition::name() const noexcept { return name_; }
PluginParameterType PluginParameterDefinition::type() const noexcept { return type_; }
bool PluginParameterDefinition::required() const noexcept { return required_; }
const std::optional<PluginParameterValue>& PluginParameterDefinition::default_value() const noexcept {
    return default_value_;
}
const std::optional<double>& PluginParameterDefinition::minimum() const noexcept { return minimum_; }
const std::optional<double>& PluginParameterDefinition::maximum() const noexcept { return maximum_; }
const std::vector<std::string>& PluginParameterDefinition::enum_values() const noexcept {
    return enum_values_;
}

void PluginParameterDefinition::validate_value(const PluginParameterValue& value) const {
    if (!value_matches_type(value, type_)) {
        throw std::invalid_argument("Plugin parameter value type does not match its definition");
    }
    if (const auto* text = std::get_if<std::string>(&value)) {
        if (text->size() > maximum_string_length || text->find('\0') != std::string::npos) {
            throw std::invalid_argument("Plugin parameter string is invalid");
        }
        if (type_ == PluginParameterType::enumeration &&
            std::ranges::find(enum_values_, *text) == enum_values_.end()) {
            throw std::invalid_argument("Plugin enumeration parameter value is not allowed");
        }
    }
    if (type_ == PluginParameterType::integer || type_ == PluginParameterType::number) {
        const double number = numeric_value(value);
        if (!std::isfinite(number) || (minimum_.has_value() && number < *minimum_) ||
            (maximum_.has_value() && number > *maximum_)) {
            throw std::invalid_argument("Plugin numeric parameter value is outside its bounds");
        }
    }
}

PluginInputPortDefinition::PluginInputPortDefinition(
    std::string name,
    const bool required,
    std::vector<std::string> accepted_file_types
)
    : name_{std::move(name)},
      required_{required},
      accepted_file_types_{std::move(accepted_file_types)} {
    if (!valid_name(name_, maximum_name_length) || accepted_file_types_.empty() ||
        accepted_file_types_.size() > maximum_file_types) {
        throw std::invalid_argument("Plugin input port definition is invalid");
    }
    std::unordered_set<std::string_view> unique;
    unique.reserve(accepted_file_types_.size());
    for (const std::string& type : accepted_file_types_) {
        require_text(type, "Plugin input file type", maximum_file_type_length);
        if (!unique.insert(type).second) {
            throw std::invalid_argument("Plugin input port contains duplicate file types");
        }
    }
}

std::string_view PluginInputPortDefinition::name() const noexcept { return name_; }
bool PluginInputPortDefinition::required() const noexcept { return required_; }
const std::vector<std::string>& PluginInputPortDefinition::accepted_file_types() const noexcept {
    return accepted_file_types_;
}
bool PluginInputPortDefinition::accepts_file_type(const std::string_view file_type) const noexcept {
    return std::ranges::find(accepted_file_types_, "*") != accepted_file_types_.end() ||
           std::ranges::find(accepted_file_types_, file_type) != accepted_file_types_.end();
}

PluginOutputPortDefinition::PluginOutputPortDefinition(std::string name, std::string file_type)
    : name_{std::move(name)}, file_type_{std::move(file_type)} {
    if (!valid_name(name_, maximum_name_length)) {
        throw std::invalid_argument("Plugin output port name is invalid");
    }
    require_text(file_type_, "Plugin output file type", maximum_file_type_length);
}

std::string_view PluginOutputPortDefinition::name() const noexcept { return name_; }
std::string_view PluginOutputPortDefinition::file_type() const noexcept { return file_type_; }

}  // namespace biocore::domain
