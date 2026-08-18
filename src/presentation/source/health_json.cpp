#include "biocore/presentation/health_json.hpp"

#include <array>
#include <string>
#include <string_view>

namespace biocore::presentation {
namespace {

std::string escape_json(const std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size());

    constexpr std::array hexadecimal_digits{
        '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

    for (const char raw_character : value) {
        const auto character = static_cast<unsigned char>(raw_character);
        switch (character) {
            case '"':
                escaped += "\\\"";
                break;
            case '\\':
                escaped += "\\\\";
                break;
            case '\b':
                escaped += "\\b";
                break;
            case '\f':
                escaped += "\\f";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                if (character < 0x20U) {
                    escaped += "\\u00";
                    escaped += hexadecimal_digits[(character >> 4U) & 0x0FU];
                    escaped += hexadecimal_digits[character & 0x0FU];
                } else {
                    escaped += static_cast<char>(character);
                }
                break;
        }
    }

    return escaped;
}

}  // namespace

std::string render_health_json(const application::HealthSnapshot& snapshot) {
    return "{\"status\":\"" + escape_json(snapshot.status) + "\",\"component\":\"" +
           escape_json(snapshot.component) + "\",\"version\":\"" + escape_json(snapshot.version) +
           "\",\"timestampUtc\":\"" + escape_json(snapshot.timestamp_utc) + "\"}";
}

}  // namespace biocore::presentation
