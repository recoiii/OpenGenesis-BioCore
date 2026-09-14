#include "vcf_ingestion_internal.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <stdexcept>

namespace biocore::domain::vcf_detail {

constexpr std::size_t maximum_vcf_line_bytes = 64U * 1024U * 1024U;

[[nodiscard]] bool read_bounded_line(std::istream& input, std::string& line) {
    line.clear();
    bool saw = false;
    for (;;) {
        const int next = input.get();
        if (next == std::char_traits<char>::eof()) {
            if (input.bad()) {
                throw std::runtime_error("unable to read VCF input");
            }
            return saw;
        }
        saw = true;
        const char value = static_cast<char>(next);
        if (value == '\n') {
            break;
        }
        if (line.size() >= maximum_vcf_line_bytes) {
            throw std::invalid_argument("VCF line exceeds the safety limit");
        }
        line.push_back(value);
    }
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    return true;
}

void strip_utf8_bom(std::string& line) {
    if (line.size() >= 3U && static_cast<unsigned char>(line[0]) == 0xEFU
        && static_cast<unsigned char>(line[1]) == 0xBBU
        && static_cast<unsigned char>(line[2]) == 0xBFU) {
        line.erase(0U, 3U);
    }
}

[[nodiscard]] std::vector<std::string_view> split(const std::string_view value, const char delimiter) {
    std::vector<std::string_view> result;
    std::size_t begin = 0U;
    for (;;) {
        const auto end = value.find(delimiter, begin);
        result.push_back(value.substr(begin, end == std::string_view::npos ? value.size() - begin : end - begin));
        if (end == std::string_view::npos) {
            break;
        }
        begin = end + 1U;
    }
    return result;
}

[[nodiscard]] std::int32_t parse_i32(const std::string_view value, const std::string_view label) {
    std::int32_t result = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result, 10);
    if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
        throw std::invalid_argument(std::string{label} + " is not a valid 32-bit integer");
    }
    return result;
}

[[nodiscard]] std::uint64_t parse_u64(const std::string_view value, const std::string_view label) {
    if (value.empty() || value.front() == '-') {
        throw std::invalid_argument(std::string{label} + " is not a valid unsigned integer");
    }
    std::uint64_t result = 0U;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result, 10);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
        throw std::invalid_argument(std::string{label} + " is not a valid unsigned integer");
    }
    return result;
}

[[nodiscard]] float parse_float(const std::string_view value, const std::string_view label) {
    float result = 0.0F;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result, std::chars_format::general);
    if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()
        || !std::isfinite(result)) {
        throw std::invalid_argument(std::string{label} + " is not a finite float");
    }
    return result;
}

[[nodiscard]] double parse_double(const std::string_view value, const std::string_view label) {
    double result = 0.0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result, std::chars_format::general);
    if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()
        || !std::isfinite(result)) {
        throw std::invalid_argument(std::string{label} + " is not a finite number");
    }
    return result;
}

[[nodiscard]] bool symbolic_alt(const std::string_view value) noexcept {
    return (value.size() >= 2U && value.front() == '<' && value.back() == '>')
        || value.find('[') != std::string_view::npos
        || value.find(']') != std::string_view::npos
        || value == "*";
}

void uppercase_allele(std::string& value) {
    for (char& character : value) {
        if (character >= 'a' && character <= 'z') {
            character = static_cast<char>(character - 'a' + 'A');
        }
    }
}

[[nodiscard]] bool valid_sequence_allele(const std::string_view value) noexcept {
    return !value.empty() && std::all_of(value.begin(), value.end(), [](const char character) {
        return character == 'A' || character == 'C' || character == 'G'
            || character == 'T' || character == 'N';
    });
}

[[nodiscard]] bool safe_identifier(const std::string_view value) noexcept {
    if (value.empty()) {
        return false;
    }
    return std::none_of(value.begin(), value.end(), [](const char character) {
        const unsigned char code = static_cast<unsigned char>(character);
        return code <= 0x20U || code == 0x7FU || character == ';' || character == '=' || character == ',';
    });
}


}  // namespace biocore::domain::vcf_detail
