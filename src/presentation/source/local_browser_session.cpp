#include "biocore/presentation/local_browser_session.hpp"

#include <cstddef>
#include <stdexcept>
#include <utility>

namespace biocore::presentation {
namespace {

[[nodiscard]] bool valid_token(const std::string_view token) noexcept {
    if (token.size() != 64U) return false;
    for (const char character : token) {
        const bool digit = character >= '0' && character <= '9';
        const bool lower_hex = character >= 'a' && character <= 'f';
        if (!digit && !lower_hex) return false;
    }
    return true;
}

[[nodiscard]] bool constant_time_equals(
    const std::string_view left,
    const std::string_view right
) noexcept {
    const std::size_t maximum = left.size() > right.size() ? left.size() : right.size();
    unsigned int difference = static_cast<unsigned int>(left.size() ^ right.size());
    for (std::size_t index = 0U; index < maximum; ++index) {
        const unsigned char l = index < left.size()
            ? static_cast<unsigned char>(left[index])
            : 0U;
        const unsigned char r = index < right.size()
            ? static_cast<unsigned char>(right[index])
            : 0U;
        difference |= static_cast<unsigned int>(l ^ r);
    }
    return difference == 0U;
}

}  // namespace

LocalBrowserSession::LocalBrowserSession(
    const std::uint16_t port,
    std::string session_token
)
    : session_token_{std::move(session_token)} {
    if (port == 0U) {
        throw std::invalid_argument("Browser session port must be nonzero");
    }
    if (!valid_token(session_token_)) {
        throw std::invalid_argument(
            "Browser session token must be a 256-bit lowercase hexadecimal value"
        );
    }

    expected_host_ = "127.0.0.1";
    if (port != 80U) {
        expected_host_ += ':';
        expected_host_ += std::to_string(port);
    }
    expected_origin_ = "http://" + expected_host_;
}

bool LocalBrowserSession::host_allowed(const std::string_view host) const noexcept {
    return host == expected_host_;
}

bool LocalBrowserSession::origin_allowed(const std::string_view origin) const noexcept {
    return origin == expected_origin_;
}

bool LocalBrowserSession::token_matches(const std::string_view candidate) const noexcept {
    return constant_time_equals(candidate, session_token_);
}

std::string LocalBrowserSession::set_cookie_header() const {
    return std::string{cookie_name} + '=' + session_token_ +
           "; Path=/api/v1; HttpOnly; SameSite=Strict";
}

std::string_view LocalBrowserSession::expected_host() const noexcept {
    return expected_host_;
}

std::string_view LocalBrowserSession::expected_origin() const noexcept {
    return expected_origin_;
}

}  // namespace biocore::presentation
