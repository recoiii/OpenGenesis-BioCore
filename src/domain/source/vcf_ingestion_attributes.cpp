#include "vcf_ingestion_internal.hpp"

#include <stdexcept>

namespace biocore::domain::vcf_detail {

[[nodiscard]] std::unordered_map<std::string, std::string> parse_structured_attributes(
    const std::string_view payload
) {
    std::unordered_map<std::string, std::string> result;
    std::size_t begin = 0U;
    while (begin < payload.size()) {
        std::size_t equal = payload.find('=', begin);
        if (equal == std::string_view::npos || equal == begin) {
            throw std::invalid_argument("VCF structured header attribute is malformed");
        }
        std::string key{payload.substr(begin, equal - begin)};
        std::size_t cursor = equal + 1U;
        std::string value;
        if (cursor < payload.size() && payload[cursor] == '"') {
            ++cursor;
            bool escaped = false;
            bool closed = false;
            for (; cursor < payload.size(); ++cursor) {
                const char character = payload[cursor];
                if (escaped) {
                    value.push_back(character);
                    escaped = false;
                    continue;
                }
                if (character == '\\') {
                    escaped = true;
                    continue;
                }
                if (character == '"') {
                    ++cursor;
                    closed = true;
                    break;
                }
                value.push_back(character);
            }
            if (!closed || escaped) {
                throw std::invalid_argument("VCF structured header has an unterminated quoted value");
            }
        } else {
            const auto comma = payload.find(',', cursor);
            const std::size_t end = comma == std::string_view::npos ? payload.size() : comma;
            value.assign(payload.substr(cursor, end - cursor));
            cursor = end;
        }
        if (!result.emplace(std::move(key), std::move(value)).second) {
            throw std::invalid_argument("VCF structured header attribute is duplicated");
        }
        if (cursor == payload.size()) {
            break;
        }
        if (payload[cursor] != ',') {
            throw std::invalid_argument("VCF structured header delimiter is malformed");
        }
        begin = cursor + 1U;
    }
    return result;
}

}  // namespace biocore::domain::vcf_detail
