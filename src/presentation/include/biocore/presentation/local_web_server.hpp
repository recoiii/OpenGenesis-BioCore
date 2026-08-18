#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace biocore::presentation {

class LocalApiController;
class WorkerLifecycleEventBroadcastHub;

struct LocalWebServerConfig final {
    std::string bind_address{"127.0.0.1"};
    std::uint16_t port{8421U};
    std::size_t worker_threads{1U};
    std::filesystem::path frontend_root;
};

class ILocalWebServer {
public:
    virtual ~ILocalWebServer() = default;
    [[nodiscard]] virtual bool available() const noexcept = 0;
    [[nodiscard]] virtual std::string_view backend_name() const noexcept = 0;
    virtual void run(
        LocalApiController& api,
        WorkerLifecycleEventBroadcastHub& lifecycle_events,
        const LocalWebServerConfig& config
    ) = 0;
};

[[nodiscard]] std::unique_ptr<ILocalWebServer> make_local_web_server();
[[nodiscard]] bool drogon_web_server_available() noexcept;

}  // namespace biocore::presentation
