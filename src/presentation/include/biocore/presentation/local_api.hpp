#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace biocore::application {
class ArtifactPresentationService;
class JobService;
class ManagedFileService;
class IJobSubmitter;
class IUtcClock;
}

namespace biocore::presentation {

class LocalBrowserSession;

enum class HttpMethod { get, post };

struct LocalHttpRequest final {
    HttpMethod method{HttpMethod::get};
    std::string target;
    std::string authorization;
    std::string browser_session{};
    std::string origin{};
    std::string content_type{};
    std::string upload_offset{};
    std::string body;
};

struct LocalFileBody final {
    std::string content_path;
    std::string download_name;
    std::string verified_sha256;
    std::int64_t size_bytes{0};
};

struct LocalHttpResponse final {
    int status{500};
    std::string content_type{"application/json; charset=utf-8"};
    std::string body;
    std::vector<std::pair<std::string, std::string>> headers;
    std::optional<LocalFileBody> file;
};

class LocalApiController final {
public:
    static constexpr std::size_t maximum_request_body_bytes = 16U * 1024U;

    LocalApiController(
        application::JobService& jobs,
        application::IJobSubmitter& submissions,
        application::ManagedFileService& managed_files,
        application::ArtifactPresentationService& artifacts,
        application::IUtcClock& clock,
        std::string bootstrap_token,
        LocalBrowserSession& browser_session
    );

    [[nodiscard]] LocalHttpResponse handle(const LocalHttpRequest& request);
    [[nodiscard]] bool websocket_authorized(std::string_view authorization) const;
    [[nodiscard]] bool websocket_authorized(
        std::string_view authorization,
        std::string_view browser_session,
        std::string_view origin
    ) const;
    [[nodiscard]] std::optional<std::string> websocket_snapshot(
        std::string_view authorization
    );
    [[nodiscard]] std::optional<std::string> websocket_snapshot(
        std::string_view authorization,
        std::string_view browser_session,
        std::string_view origin
    );
    [[nodiscard]] bool browser_host_authorized(std::string_view host) const noexcept;
    [[nodiscard]] std::string_view bootstrap_token() const noexcept;

private:
    application::JobService& jobs_;
    application::IJobSubmitter& submissions_;
    application::ManagedFileService& managed_files_;
    application::ArtifactPresentationService& artifacts_;
    application::IUtcClock& clock_;
    std::string bootstrap_token_;
    LocalBrowserSession& browser_session_;
};

}  // namespace biocore::presentation
