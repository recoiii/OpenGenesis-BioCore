#include "biocore/pipeline_protocol/workflow_template_document_codec.hpp"

#include <charconv>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <locale>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "biocore/pipeline_protocol/workflow_document_codec.hpp"

namespace biocore::pipeline_protocol {
namespace {

using JsonScalar = std::variant<std::string, std::uint64_t>;
using JsonObject = std::map<std::string, JsonScalar, std::less<>>;

void validate_utf8(const std::string_view value) {
    std::size_t index = 0U;
    while (index < value.size()) {
        const auto first = static_cast<unsigned char>(value[index]);
        if (first <= 0x7FU) {
            ++index;
            continue;
        }

        std::size_t length = 0U;
        std::uint32_t code_point = 0U;
        if (first >= 0xC2U && first <= 0xDFU) {
            length = 2U;
            code_point = first & 0x1FU;
        } else if (first >= 0xE0U && first <= 0xEFU) {
            length = 3U;
            code_point = first & 0x0FU;
        } else if (first >= 0xF0U && first <= 0xF4U) {
            length = 4U;
            code_point = first & 0x07U;
        } else {
            throw std::invalid_argument(
                "Workflow template JSON is not valid UTF-8"
            );
        }

        if (index + length > value.size()) {
            throw std::invalid_argument(
                "Workflow template JSON ends inside a UTF-8 sequence"
            );
        }

        for (std::size_t offset = 1U; offset < length; ++offset) {
            const auto continuation =
                static_cast<unsigned char>(value[index + offset]);
            if ((continuation & 0xC0U) != 0x80U) {
                throw std::invalid_argument(
                    "Workflow template JSON is not valid UTF-8"
                );
            }
            code_point =
                (code_point << 6U) | (continuation & 0x3FU);
        }

        if ((length == 3U && code_point < 0x800U) ||
            (length == 4U && code_point < 0x10000U) ||
            code_point > 0x10FFFFU ||
            (code_point >= 0xD800U && code_point <= 0xDFFFU)) {
            throw std::invalid_argument(
                "Workflow template JSON contains an invalid UTF-8 code point"
            );
        }
        index += length;
    }
}

[[nodiscard]] std::uint32_t hex_value(const char value) {
    if (value >= '0' && value <= '9') {
        return static_cast<std::uint32_t>(value - '0');
    }
    if (value >= 'a' && value <= 'f') {
        return 10U + static_cast<std::uint32_t>(value - 'a');
    }
    if (value >= 'A' && value <= 'F') {
        return 10U + static_cast<std::uint32_t>(value - 'A');
    }
    throw std::invalid_argument(
        "Workflow template JSON escape contains a non-hexadecimal digit"
    );
}

void append_utf8(std::string& output, const std::uint32_t code_point) {
    if (code_point <= 0x7FU) {
        output.push_back(static_cast<char>(code_point));
    } else if (code_point <= 0x7FFU) {
        output.push_back(static_cast<char>(0xC0U | (code_point >> 6U)));
        output.push_back(
            static_cast<char>(0x80U | (code_point & 0x3FU))
        );
    } else if (code_point <= 0xFFFFU) {
        output.push_back(
            static_cast<char>(0xE0U | (code_point >> 12U))
        );
        output.push_back(
            static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU))
        );
        output.push_back(
            static_cast<char>(0x80U | (code_point & 0x3FU))
        );
    } else {
        output.push_back(
            static_cast<char>(0xF0U | (code_point >> 18U))
        );
        output.push_back(
            static_cast<char>(0x80U | ((code_point >> 12U) & 0x3FU))
        );
        output.push_back(
            static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU))
        );
        output.push_back(
            static_cast<char>(0x80U | (code_point & 0x3FU))
        );
    }
}

class Parser final {
public:
    explicit Parser(const std::string_view input) : input_{input} {}

    [[nodiscard]] JsonObject parse_document() {
        skip();
        consume('{');
        JsonObject result;
        skip();
        if (consume_if('}')) {
            return result;
        }

        while (true) {
            const std::string key = parse_string();
            consume(':');
            JsonScalar value = parse_scalar();
            if (!result.emplace(key, std::move(value)).second) {
                throw std::invalid_argument(
                    "Workflow template JSON contains duplicate fields"
                );
            }
            if (consume_if('}')) {
                break;
            }
            consume(',');
        }

        skip();
        if (position_ != input_.size()) {
            throw std::invalid_argument(
                "Workflow template JSON contains trailing content"
            );
        }
        return result;
    }

private:
    [[nodiscard]] JsonScalar parse_scalar() {
        skip();
        if (position_ >= input_.size()) {
            throw std::invalid_argument(
                "Workflow template JSON ended unexpectedly"
            );
        }
        if (input_[position_] == '"') {
            return JsonScalar{parse_string()};
        }
        if (input_[position_] >= '0' && input_[position_] <= '9') {
            return JsonScalar{parse_unsigned()};
        }
        throw std::invalid_argument(
            "Workflow template JSON value must be a string or unsigned integer"
        );
    }

    [[nodiscard]] std::string parse_string() {
        consume('"');
        std::string result;
        while (position_ < input_.size()) {
            const char value = input_[position_++];
            if (value == '"') {
                return result;
            }
            if (static_cast<unsigned char>(value) < 0x20U) {
                throw std::invalid_argument(
                    "Workflow template JSON string contains control characters"
                );
            }
            if (value != '\\') {
                result.push_back(value);
                continue;
            }
            if (position_ >= input_.size()) {
                throw std::invalid_argument(
                    "Workflow template JSON string escape is incomplete"
                );
            }
            const char escaped = input_[position_++];
            switch (escaped) {
                case '"': result.push_back('"'); break;
                case '\\': result.push_back('\\'); break;
                case '/': result.push_back('/'); break;
                case 'b': result.push_back('\b'); break;
                case 'f': result.push_back('\f'); break;
                case 'n': result.push_back('\n'); break;
                case 'r': result.push_back('\r'); break;
                case 't': result.push_back('\t'); break;
                case 'u': {
                    if (position_ + 4U > input_.size()) {
                        throw std::invalid_argument(
                            "Workflow template JSON unicode escape is incomplete"
                        );
                    }
                    std::uint32_t code_point = 0U;
                    for (std::size_t index = 0U; index < 4U; ++index) {
                        code_point =
                            (code_point << 4U) |
                            hex_value(input_[position_++]);
                    }
                    if (code_point >= 0xD800U && code_point <= 0xDBFFU) {
                        if (position_ + 6U > input_.size() ||
                            input_[position_] != '\\' ||
                            input_[position_ + 1U] != 'u') {
                            throw std::invalid_argument(
                                "Workflow template JSON contains an unpaired high surrogate"
                            );
                        }
                        position_ += 2U;
                        std::uint32_t low = 0U;
                        for (std::size_t index = 0U; index < 4U; ++index) {
                            low = (low << 4U) |
                                  hex_value(input_[position_++]);
                        }
                        if (low < 0xDC00U || low > 0xDFFFU) {
                            throw std::invalid_argument(
                                "Workflow template JSON contains an invalid surrogate pair"
                            );
                        }
                        code_point = 0x10000U +
                            ((code_point - 0xD800U) << 10U) +
                            (low - 0xDC00U);
                    } else if (
                        code_point >= 0xDC00U &&
                        code_point <= 0xDFFFU
                    ) {
                        throw std::invalid_argument(
                            "Workflow template JSON contains an unpaired low surrogate"
                        );
                    }
                    append_utf8(result, code_point);
                    break;
                }
                default:
                    throw std::invalid_argument(
                        "Workflow template JSON string escape is invalid"
                    );
            }
        }
        throw std::invalid_argument(
            "Workflow template JSON string is unterminated"
        );
    }

    [[nodiscard]] std::uint64_t parse_unsigned() {
        const std::size_t begin = position_;
        if (input_[position_] == '0') {
            ++position_;
            if (position_ < input_.size() &&
                input_[position_] >= '0' &&
                input_[position_] <= '9') {
                throw std::invalid_argument(
                    "Workflow template JSON number has a leading zero"
                );
            }
        } else {
            while (
                position_ < input_.size() &&
                input_[position_] >= '0' &&
                input_[position_] <= '9'
            ) {
                ++position_;
            }
        }
        std::uint64_t result = 0U;
        const auto [end, error] = std::from_chars(
            input_.data() + begin,
            input_.data() + position_,
            result
        );
        if (error != std::errc{} ||
            end != input_.data() + position_) {
            throw std::invalid_argument(
                "Workflow template JSON integer is invalid"
            );
        }
        return result;
    }

    void consume(const char expected) {
        skip();
        if (position_ >= input_.size() ||
            input_[position_] != expected) {
            throw std::invalid_argument(
                "Workflow template JSON punctuation is invalid"
            );
        }
        ++position_;
    }

    [[nodiscard]] bool consume_if(const char expected) {
        skip();
        if (position_ < input_.size() &&
            input_[position_] == expected) {
            ++position_;
            return true;
        }
        return false;
    }

    void skip() {
        while (position_ < input_.size()) {
            const char value = input_[position_];
            if (value != ' ' && value != '\n' &&
                value != '\r' && value != '\t') {
                break;
            }
            ++position_;
        }
    }

    std::string_view input_;
    std::size_t position_{0U};
};

void require_only(
    const JsonObject& object,
    const std::initializer_list<std::string_view> allowed
) {
    for (const auto& [key, value] : object) {
        static_cast<void>(value);
        bool known = false;
        for (const std::string_view candidate : allowed) {
            if (key == candidate) {
                known = true;
                break;
            }
        }
        if (!known) {
            throw std::invalid_argument(
                "Workflow template JSON contains an unknown field"
            );
        }
    }
}

[[nodiscard]] const JsonScalar& require_field(
    const JsonObject& object,
    const std::string_view name
) {
    const auto iterator = object.find(name);
    if (iterator == object.end()) {
        throw std::invalid_argument(
            "Workflow template JSON is missing a required field"
        );
    }
    return iterator->second;
}

[[nodiscard]] std::string require_string(
    const JsonObject& object,
    const std::string_view name
) {
    const auto* value =
        std::get_if<std::string>(&require_field(object, name));
    if (value == nullptr) {
        throw std::invalid_argument(
            "Workflow template JSON field must be a string"
        );
    }
    return *value;
}

[[nodiscard]] std::uint32_t require_schema_version(
    const JsonObject& object
) {
    const auto* value =
        std::get_if<std::uint64_t>(&require_field(object, "schemaVersion"));
    if (value == nullptr ||
        *value > std::numeric_limits<std::uint32_t>::max()) {
        throw std::invalid_argument(
            "Workflow template schema version is invalid"
        );
    }
    return static_cast<std::uint32_t>(*value);
}

void append_string(
    std::ostringstream& output,
    const std::string_view value
) {
    output << '"';
    constexpr char hex[] = "0123456789abcdef";
    for (const char raw_character : value) {
        const auto character =
            static_cast<unsigned char>(raw_character);
        switch (character) {
            case '"': output << "\\\""; break;
            case '\\': output << "\\\\"; break;
            case '\b': output << "\\b"; break;
            case '\f': output << "\\f"; break;
            case '\n': output << "\\n"; break;
            case '\r': output << "\\r"; break;
            case '\t': output << "\\t"; break;
            default:
                if (character < 0x20U) {
                    output << "\\u00"
                           << hex[(character >> 4U) & 0x0FU]
                           << hex[character & 0x0FU];
                } else {
                    output << static_cast<char>(character);
                }
        }
    }
    output << '"';
}

}  // namespace

domain::WorkflowTemplate parse_workflow_template_document(
    const std::string_view json
) {
    if (json.empty() ||
        json.size() > maximum_workflow_template_document_bytes) {
        throw std::invalid_argument(
            "Workflow template JSON size is invalid"
        );
    }
    validate_utf8(json);

    const JsonObject root = Parser{json}.parse_document();
    require_only(
        root,
        {
            "schemaVersion",
            "id",
            "version",
            "name",
            "description",
            "workflowDocument"
        }
    );

    return domain::WorkflowTemplate{
        require_schema_version(root),
        require_string(root, "id"),
        require_string(root, "version"),
        require_string(root, "name"),
        require_string(root, "description"),
        parse_workflow_document(
            require_string(root, "workflowDocument")
        )
    };
}

std::string serialize_workflow_template_document(
    const domain::WorkflowTemplate& value
) {
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << "{\"schemaVersion\":"
           << value.schema_version()
           << ",\"id\":";
    append_string(output, value.id());
    output << ",\"version\":";
    append_string(output, value.version());
    output << ",\"name\":";
    append_string(output, value.name());
    output << ",\"description\":";
    append_string(output, value.description());
    output << ",\"workflowDocument\":";
    append_string(
        output,
        serialize_workflow_document(value.blueprint())
    );
    output << '}';

    std::string result = output.str();
    if (result.size() >
        maximum_workflow_template_document_bytes) {
        throw std::invalid_argument(
            "Serialized workflow template document exceeds the maximum size"
        );
    }
    return result;
}

}  // namespace biocore::pipeline_protocol
