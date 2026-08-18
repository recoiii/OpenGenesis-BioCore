#include "biocore/presentation/local_web_server.hpp"

#include <memory>
#include <stdexcept>
#include <string_view>

#include "biocore/presentation/local_api.hpp"

namespace biocore::presentation {
namespace {

class UnavailableLocalWebServer final : public ILocalWebServer {
public:
    [[nodiscard]] bool available() const noexcept override { return false; }
    [[nodiscard]] std::string_view backend_name() const noexcept override { return "drogon-unavailable"; }
    void run(
        LocalApiController&,
        WorkerLifecycleEventBroadcastHub&,
        const LocalWebServerConfig&
    ) override {
        throw std::runtime_error("This OpenGenesis-BioCore build was compiled without Drogon support");
    }
};

}  // namespace

std::unique_ptr<ILocalWebServer> make_local_web_server() {
    return std::make_unique<UnavailableLocalWebServer>();
}

bool drogon_web_server_available() noexcept { return false; }

}  // namespace biocore::presentation
