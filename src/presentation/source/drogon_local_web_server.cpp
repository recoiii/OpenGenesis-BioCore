#include "biocore/presentation/local_web_server.hpp"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <drogon/drogon.h>
#include <drogon/WebSocketController.h>

#include <cstdint>
#include <algorithm>
#include <fstream>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

#include "biocore/presentation/local_api.hpp"
#include "biocore/presentation/frontend_asset_store.hpp"
#include "biocore/presentation/worker_lifecycle_event_broadcast.hpp"

namespace biocore::presentation {
namespace {
[[nodiscard]] drogon::HttpResponsePtr frontend_response(
    const FrontendAsset& asset
) {
    auto response = drogon::HttpResponse::newHttpResponse();
    response->setStatusCode(drogon::k200OK);
    response->setContentTypeString(asset.content_type);
    response->setBody(asset.body);
    response->addHeader("Cache-Control", asset.cache_control);
    response->addHeader("X-Content-Type-Options", "nosniff");
    response->addHeader(
        "Content-Security-Policy",
        "default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data:; "
        "connect-src 'self'; base-uri 'none'; form-action 'none'; frame-ancestors 'none'"
    );
    response->addHeader("Referrer-Policy", "no-referrer");
    response->addHeader(
        "Permissions-Policy",
        "camera=(), microphone=(), geolocation=(), payment=(), usb=()"
    );
    return response;
}


[[nodiscard]] drogon::HttpStatusCode to_status(const int status) {
    return static_cast<drogon::HttpStatusCode>(status);
}

[[nodiscard]] LocalHttpRequest translate_request(const drogon::HttpRequestPtr& request) {
    HttpMethod method;
    if (request->method() == drogon::Get) {
        method = HttpMethod::get;
    } else if (request->method() == drogon::Post) {
        method = HttpMethod::post;
    } else {
        throw std::invalid_argument("HTTP method is not supported by the local API");
    }
    std::string target = request->getOriginalPath();
    if (!request->query().empty()) {
        target += '?';
        target += request->query();
    }
    return LocalHttpRequest{
        .method = method,
        .target = std::move(target),
        .authorization = request->getHeader("authorization"),
        .browser_session = request->getCookie("biocore_session"),
        .origin = request->getHeader("origin"),
        .content_type = request->getHeader("content-type"),
        .upload_offset = request->getHeader("x-biocore-upload-offset"),
        .body = std::string{request->body()},
    };
}

[[nodiscard]] drogon::HttpResponsePtr translate_response(
    const LocalHttpResponse& response,
    const drogon::HttpRequestPtr& request
) {
    drogon::HttpResponsePtr output;
    if (response.file.has_value()) {
        struct StreamState final {
            explicit StreamState(const LocalFileBody& file_body)
                : stream{file_body.content_path, std::ios::binary},
                  remaining{file_body.size_bytes >= 0 ? static_cast<std::uint64_t>(file_body.size_bytes) : 0U} {}
            std::ifstream stream;
            std::uint64_t remaining{0U};
        };
        auto stream = std::make_shared<StreamState>(*response.file);
        if (!stream->stream || response.file->size_bytes < 0) {
            output = drogon::HttpResponse::newHttpResponse();
            output->setStatusCode(drogon::k409Conflict);
            output->setContentTypeString("application/json; charset=utf-8");
            output->setBody("{\"error\":{\"code\":\"artifact_open_failed\",\"message\":\"Artifact changed after verification\"}}");
            return output;
        }
        output = drogon::HttpResponse::newStreamResponse(
            [stream](char* buffer, const std::size_t capacity) -> std::size_t {
                if (buffer == nullptr) {
                    stream->stream.close();
                    return 0U;
                }
                if (capacity == 0U || stream->remaining == 0U) return 0U;
                const std::uint64_t capacity64 = static_cast<std::uint64_t>(capacity);
                const std::uint64_t streamsize_max =
                    static_cast<std::uint64_t>((std::numeric_limits<std::streamsize>::max)());
                const std::uint64_t requested64 =
                    (std::min)({capacity64, stream->remaining, streamsize_max});
                const auto requested = static_cast<std::streamsize>(requested64);
                stream->stream.read(buffer, requested);
                const std::streamsize read = stream->stream.gcount();
                if (read <= 0) return 0U;
                const auto count = static_cast<std::uint64_t>(read);
                stream->remaining -= count;
                return static_cast<std::size_t>(count);
            },
            response.file->download_name,
            drogon::CT_APPLICATION_OCTET_STREAM,
            {},
            request
        );
    } else {
        output = drogon::HttpResponse::newHttpResponse();
        output->setBody(response.body);
    }
    output->setStatusCode(to_status(response.status));
    output->setContentTypeString(response.content_type);
    for (const auto& [name, value] : response.headers) output->addHeader(name, value);
    return output;
}


struct JobsWebSocketContext final {
    std::string authorization;
    std::string browser_session;
    std::string origin;
    WorkerLifecycleEventBroadcastHub::SubscriptionId subscription_id{0U};
};

class JobsWebSocketController final
    : public drogon::WebSocketController<JobsWebSocketController, false> {
public:
    JobsWebSocketController(
        LocalApiController& api,
        WorkerLifecycleEventBroadcastHub& lifecycle_events
    ) noexcept
        : api_{api}, lifecycle_events_{lifecycle_events} {}

    void handleNewMessage(
        const drogon::WebSocketConnectionPtr& connection,
        std::string&& message,
        const drogon::WebSocketMessageType& type
    ) override {
        if (type != drogon::WebSocketMessageType::Text || message != "snapshot") {
            connection->shutdown(
                drogon::CloseCode::kViolation,
                "Only the snapshot command is supported"
            );
            return;
        }

        const auto context = connection->getContext<JobsWebSocketContext>();
        if (context == nullptr) {
            connection->shutdown(
                drogon::CloseCode::kViolation,
                "WebSocket context is missing"
            );
            return;
        }
        const auto snapshot = api_.websocket_snapshot(
            context->authorization, context->browser_session, context->origin
        );
        if (!snapshot.has_value()) {
            connection->shutdown(
                drogon::CloseCode::kViolation,
                "WebSocket authorization failed"
            );
            return;
        }
        connection->send(*snapshot);
    }

    void handleNewConnection(
        const drogon::HttpRequestPtr& request,
        const drogon::WebSocketConnectionPtr& connection
    ) override {
        if (!api_.browser_host_authorized(request->getHeader("host"))) {
            connection->shutdown(
                drogon::CloseCode::kViolation,
                "WebSocket host validation failed"
            );
            return;
        }
        const std::string authorization = request->getHeader("authorization");
        const std::string browser_session = request->getCookie("biocore_session");
        const std::string origin = request->getHeader("origin");
        if (!api_.websocket_authorized(authorization, browser_session, origin)) {
            connection->shutdown(
                drogon::CloseCode::kViolation,
                "WebSocket authorization failed"
            );
            return;
        }

        WorkerLifecycleEventBroadcastHub::SubscriptionId subscription_id{0U};
        try {
            const std::weak_ptr<drogon::WebSocketConnection> weak_connection{connection};
            subscription_id = lifecycle_events_.subscribe_paused(
                [weak_connection](const std::string_view message) {
                    if (const auto current = weak_connection.lock(); current != nullptr) {
                        current->send(std::string{message});
                    }
                }
            );

            auto context = std::make_shared<JobsWebSocketContext>();
            context->authorization = authorization;
            context->browser_session = browser_session;
            context->origin = origin;
            context->subscription_id = subscription_id;
            connection->setContext(context);

            const auto snapshot = api_.websocket_snapshot(
                authorization, browser_session, origin
            );
            if (!snapshot.has_value()) {
                lifecycle_events_.unsubscribe(subscription_id);
                connection->shutdown(
                    drogon::CloseCode::kViolation,
                    "WebSocket authorization failed"
                );
                return;
            }

            connection->send(*snapshot);
            if (!lifecycle_events_.activate(subscription_id)) {
                lifecycle_events_.unsubscribe(subscription_id);
                connection->shutdown(
                    drogon::CloseCode::kViolation,
                    "WebSocket event subscription could not be activated"
                );
            }
        } catch (...) {
            if (subscription_id != 0U) {
                lifecycle_events_.unsubscribe(subscription_id);
            }
            connection->shutdown(
                drogon::CloseCode::kViolation,
                "WebSocket initialization failed"
            );
        }
    }

    void handleConnectionClosed(
        const drogon::WebSocketConnectionPtr& connection
    ) override {
        const auto context = connection->getContext<JobsWebSocketContext>();
        if (context != nullptr && context->subscription_id != 0U) {
            lifecycle_events_.unsubscribe(context->subscription_id);
        }
    }

    WS_PATH_LIST_BEGIN
    WS_PATH_ADD("/api/v1/ws", drogon::Get);
    WS_PATH_LIST_END

private:
    LocalApiController& api_;
    WorkerLifecycleEventBroadcastHub& lifecycle_events_;
};

class DrogonLocalWebServer final : public ILocalWebServer {
public:
    [[nodiscard]] bool available() const noexcept override { return true; }
    [[nodiscard]] std::string_view backend_name() const noexcept override { return "drogon"; }

    void run(
        LocalApiController& api,
        WorkerLifecycleEventBroadcastHub& lifecycle_events,
        const LocalWebServerConfig& config
    ) override {
        if (config.bind_address != "127.0.0.1") {
            throw std::invalid_argument("OpenGenesis-BioCore local server may bind only to 127.0.0.1");
        }
        if (config.port == 0U || config.worker_threads == 0U || config.worker_threads > 64U) {
            throw std::invalid_argument("Local server port/thread configuration is invalid");
        }
        FrontendAssetStore frontend{config.frontend_root};

        auto frontend_handler = [&frontend, &api](
                                    const drogon::HttpRequestPtr& request,
                                    std::function<void(const drogon::HttpResponsePtr&)>&& callback
                                ) {
            if (!api.browser_host_authorized(request->getHeader("host"))) {
                auto response = drogon::HttpResponse::newHttpResponse();
                response->setStatusCode(drogon::k403Forbidden);
                response->setBody("Misdirected request");
                response->addHeader("Cache-Control", "no-store");
                callback(response);
                return;
            }
            if (!request->query().empty()) {
                auto response = drogon::HttpResponse::newHttpResponse();
                response->setStatusCode(drogon::k404NotFound);
                response->setContentTypeString("text/plain; charset=utf-8");
                response->setBody("Not found");
                response->addHeader("Cache-Control", "no-store");
                response->addHeader("X-Content-Type-Options", "nosniff");
                callback(response);
                return;
            }
            const auto asset = frontend.find(request->getOriginalPath());
            if (!asset.has_value()) {
                auto response = drogon::HttpResponse::newHttpResponse();
                response->setStatusCode(drogon::k404NotFound);
                response->setContentTypeString("text/plain; charset=utf-8");
                response->setBody("Not found");
                response->addHeader("Cache-Control", "no-store");
                response->addHeader("X-Content-Type-Options", "nosniff");
                callback(response);
                return;
            }
            callback(frontend_response(*asset));
        };

        auto handler = [&api](
                           const drogon::HttpRequestPtr& request,
                           std::function<void(const drogon::HttpResponsePtr&)>&& callback
                       ) {
            if (!api.browser_host_authorized(request->getHeader("host"))) {
                auto response = drogon::HttpResponse::newHttpResponse();
                response->setStatusCode(drogon::k403Forbidden);
                response->setBody("Misdirected request");
                response->addHeader("Cache-Control", "no-store");
                callback(response);
                return;
            }
            try {
                callback(translate_response(api.handle(translate_request(request)), request));
            } catch (const std::invalid_argument& error) {
                auto response = drogon::HttpResponse::newHttpResponse();
                response->setStatusCode(drogon::k405MethodNotAllowed);
                response->setContentTypeString("application/json; charset=utf-8");
                static_cast<void>(error);
                response->setBody("{\"error\":{\"code\":\"method_not_allowed\",\"message\":\"HTTP method is not supported by the local API\"}}");
                callback(response);
            }
        };

        auto web_socket_controller =
            std::make_shared<JobsWebSocketController>(api, lifecycle_events);
        drogon::app().registerController(web_socket_controller);

        drogon::app()
            .setThreadNum(config.worker_threads)
            .addListener(config.bind_address, config.port)
            .registerHandlerViaRegex(
                "^/$|^/index\\.html$|^/assets/app\\.css$|^/assets/app\\.js$",
                frontend_handler,
                {drogon::Get}
            )
            .registerHandlerViaRegex("^/api/v1/.*$", handler, {drogon::Get, drogon::Post})
            .run();
    }
};

}  // namespace

std::unique_ptr<ILocalWebServer> make_local_web_server() {
    return std::make_unique<DrogonLocalWebServer>();
}

bool drogon_web_server_available() noexcept { return true; }

}  // namespace biocore::presentation
