#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace biocore::presentation {

class LocalBrowserSession final {
public:
    static constexpr std::string_view cookie_name{"biocore_session"};

    LocalBrowserSession(std::uint16_t port, std::string session_token);

    [[nodiscard]] bool host_allowed(std::string_view host) const noexcept;
    [[nodiscard]] bool origin_allowed(std::string_view origin) const noexcept;
    [[nodiscard]] bool token_matches(std::string_view candidate) const noexcept;
    [[nodiscard]] std::string set_cookie_header() const;
    [[nodiscard]] std::string_view expected_host() const noexcept;
    [[nodiscard]] std::string_view expected_origin() const noexcept;

private:
    std::string session_token_;
    std::string expected_host_;
    std::string expected_origin_;
};

}  // namespace biocore::presentation
