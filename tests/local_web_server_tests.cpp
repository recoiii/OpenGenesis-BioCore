#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "biocore/application/worker_lifecycle_event.hpp"
#include "biocore/presentation/frontend_asset_store.hpp"
#include "biocore/presentation/local_browser_session.hpp"
#include "biocore/presentation/local_web_server.hpp"
#include "biocore/presentation/worker_lifecycle_event_broadcast.hpp"

namespace {

using biocore::application::WorkerLifecycleEvent;
using biocore::application::WorkerLifecycleEventType;
using biocore::presentation::FrontendAssetStore;
using biocore::presentation::LocalBrowserSession;
using biocore::presentation::WorkerLifecycleEventBroadcastHub;

void require(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void write_text(const std::filesystem::path& path, const std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    require(static_cast<bool>(output), "frontend fixture open");
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    require(static_cast<bool>(output), "frontend fixture write");
}

void create_frontend_fixture(const std::filesystem::path& root) {
    write_text(root / "index.html", "<!doctype html><title>OpenGenesis-BioCore</title>");
    write_text(root / "assets/app.css", "body{background:#07100d}");
    write_text(root / "assets/app.js", "fetch('/api/v1/health');");
}

void frontend_asset_store_contract() {
    const auto root = std::filesystem::temp_directory_path() /
                      "biocore-frontend-asset-store-tests";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    create_frontend_fixture(root);

    {
        FrontendAssetStore store{root};
        const auto slash = store.find("/");
        const auto index = store.find("/index.html");
        const auto css = store.find("/assets/app.css");
        const auto javascript = store.find("/assets/app.js");
        require(slash.has_value() && index.has_value(), "frontend root/index routes");
        require(slash->body == index->body, "root must alias index.html");
        require(slash->body.find("OpenGenesis-BioCore") != std::string::npos, "frontend HTML content");
        require(slash->content_type == "text/html; charset=utf-8", "frontend HTML MIME");
        require(slash->cache_control == "no-store", "frontend HTML cache policy");
        require(css.has_value() && css->content_type == "text/css; charset=utf-8",
                "frontend CSS MIME");
        require(css->cache_control == "no-cache", "frontend CSS cache policy");
        require(javascript.has_value() &&
                    javascript->content_type == "text/javascript; charset=utf-8",
                "frontend JavaScript MIME");
        require(javascript->cache_control == "no-cache", "frontend JS cache policy");

        require(!store.find("/README.md").has_value(), "README is not a public route");
        require(!store.find("/../index.html").has_value(), "parent traversal is rejected");
        require(!store.find("/assets/../index.html").has_value(),
                "nested traversal is rejected");
        require(!store.find("/%2e%2e/index.html").has_value(),
                "encoded traversal is rejected");
        require(!store.find("/assets/app.js?cache=1").has_value(),
                "query-bearing static route is rejected");
    }

    {
        create_frontend_fixture(root);
        std::ofstream invalid{root / "assets/app.js", std::ios::binary | std::ios::trunc};
        invalid.put(static_cast<char>(0xc3));
        invalid.put(static_cast<char>(0x28));
        invalid.close();
        bool rejected = false;
        try {
            FrontendAssetStore store{root};
            static_cast<void>(store);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        require(rejected, "invalid UTF-8 frontend asset must fail closed");
    }

    {
        create_frontend_fixture(root);
        std::ofstream nul{root / "assets/app.css", std::ios::binary | std::ios::app};
        nul.put('\0');
        nul.close();
        bool rejected = false;
        try {
            FrontendAssetStore store{root};
            static_cast<void>(store);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        require(rejected, "NUL-containing frontend asset must fail closed");
    }

    {
        create_frontend_fixture(root);
        std::filesystem::remove(root / "assets/app.js", error);
        bool rejected = false;
        try {
            FrontendAssetStore store{root};
            static_cast<void>(store);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        require(rejected, "missing required frontend asset must fail closed");
    }

    {
        create_frontend_fixture(root);
        std::ofstream large{root / "assets/app.js", std::ios::binary | std::ios::trunc};
        const std::string block(4096U, 'x');
        for (std::size_t written = 0U;
             written <= FrontendAssetStore::maximum_javascript_bytes;
             written += block.size()) {
            large.write(block.data(), static_cast<std::streamsize>(block.size()));
        }
        large.close();
        bool rejected = false;
        try {
            FrontendAssetStore store{root};
            static_cast<void>(store);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        require(rejected, "oversized frontend asset must fail closed");
    }

#if !defined(_WIN32)
    {
        create_frontend_fixture(root);
        const auto external = std::filesystem::temp_directory_path() /
                              "biocore-frontend-external.js";
        write_text(external, "console.log('external')");
        std::filesystem::remove(root / "assets/app.js", error);
        error.clear();
        std::filesystem::create_symlink(external, root / "assets/app.js", error);
        if (!error) {
            bool rejected = false;
            try {
                FrontendAssetStore store{root};
                static_cast<void>(store);
            } catch (const std::invalid_argument&) {
                rejected = true;
            }
            require(rejected, "symlink frontend asset must fail closed");
        }
        std::filesystem::remove(external, error);
    }
#endif

    std::filesystem::remove_all(root, error);
}


void local_browser_session_contract() {
    const std::string token(64U, 'c');
    LocalBrowserSession session{8421U, token};
    require(session.expected_host() == "127.0.0.1:8421", "browser expected host");
    require(session.expected_origin() == "http://127.0.0.1:8421", "browser expected origin");
    require(session.host_allowed("127.0.0.1:8421"), "canonical loopback host");
    require(!session.host_allowed("localhost:8421"), "localhost alias intentionally rejected");
    require(!session.host_allowed("evil.test:8421"), "DNS rebinding host rejected");
    require(session.origin_allowed("http://127.0.0.1:8421"), "canonical browser origin");
    require(!session.origin_allowed("http://evil.test:8421"), "cross-site origin rejected");
    require(session.token_matches(token), "browser session token accepted");
    require(!session.token_matches(std::string(64U, 'd')), "wrong browser session token rejected");
    const std::string cookie = session.set_cookie_header();
    require(cookie.find("biocore_session=") == 0U, "browser cookie name");
    require(cookie.find("Path=/api/v1") != std::string::npos, "browser cookie API path");
    require(cookie.find("HttpOnly") != std::string::npos, "browser cookie HttpOnly");
    require(cookie.find("SameSite=Strict") != std::string::npos, "browser cookie SameSite strict");
    require(cookie.find("Domain=") == std::string::npos, "browser cookie host-only");
}

WorkerLifecycleEvent event(const std::uint64_t sequence) {
    WorkerLifecycleEvent value{};
    value.type = WorkerLifecycleEventType::progress;
    value.job_id = "job-1";
    value.launch_revision = 7;
    value.sequence = sequence;
    value.worker_timestamp_utc = "2026-08-07T16:30:00Z";
    value.progress = 0.5;
    value.active_step_id = "step-a";
    return value;
}

std::uint64_t sequence_from_json(const std::string_view json) {
    constexpr std::string_view prefix = "\"sequence\":";
    const auto start = json.find(prefix);
    require(start != std::string_view::npos, "sequence field");
    std::size_t position = start + prefix.size();
    std::uint64_t value = 0U;
    while (position < json.size() && json[position] >= '0' && json[position] <= '9') {
        value = value * 10U + static_cast<std::uint64_t>(json[position] - '0');
        ++position;
    }
    return value;
}

void ordered_subscription_contract() {
    WorkerLifecycleEventBroadcastHub hub;
    std::vector<std::uint64_t> seen;
    const auto id = hub.subscribe_paused([&](const std::string_view json) {
        seen.push_back(sequence_from_json(json));
    });
    hub.publish(event(1U));
    hub.publish(event(2U));
    require(seen.empty(), "paused subscription must buffer before snapshot");
    require(hub.activate(id), "paused subscription activation");
    require(seen == std::vector<std::uint64_t>{1U, 2U}, "buffered event ordering");
    hub.publish(event(3U));
    require(seen == std::vector<std::uint64_t>{1U, 2U, 3U}, "live event ordering");
    hub.unsubscribe(id);
    hub.publish(event(4U));
    require(seen == std::vector<std::uint64_t>{1U, 2U, 3U}, "unsubscribe stops delivery");
}

void reentrant_order_contract() {
    WorkerLifecycleEventBroadcastHub hub;
    std::vector<std::uint64_t> seen;
    bool reentrant = false;
    const auto id = hub.subscribe_paused([&](const std::string_view json) {
        const auto sequence = sequence_from_json(json);
        seen.push_back(sequence);
        if (sequence == 1U && !reentrant) {
            reentrant = true;
            hub.publish(event(3U));
        }
    });
    hub.publish(event(1U));
    hub.publish(event(2U));
    require(hub.activate(id), "reentrant activation");
    require(seen == std::vector<std::uint64_t>{1U, 2U, 3U},
            "reentrant publish must not overtake queued messages");
}

void bounded_buffer_contract() {
    WorkerLifecycleEventBroadcastHub hub;
    std::size_t delivered = 0U;
    const auto id = hub.subscribe_paused([&](std::string_view) { ++delivered; });
    for (std::size_t index = 0U;
         index < WorkerLifecycleEventBroadcastHub::maximum_pending_messages + 1U;
         ++index) {
        hub.publish(event(static_cast<std::uint64_t>(index + 1U)));
    }
    require(!hub.activate(id), "overflowed paused subscription must fail closed");
    require(delivered == 0U, "overflowed paused subscription must not partially flush");
    require(hub.subscriber_count() == 0U, "overflowed subscription removed");
}

void consumer_isolation_contract() {
    WorkerLifecycleEventBroadcastHub hub;
    std::vector<std::uint64_t> healthy;
    const auto bad = hub.subscribe_paused([](std::string_view) {
        throw std::runtime_error("synthetic consumer failure");
    });
    const auto good = hub.subscribe_paused([&](const std::string_view json) {
        healthy.push_back(sequence_from_json(json));
    });
    require(hub.activate(bad), "bad subscriber activation before delivery");
    require(hub.activate(good), "healthy subscriber activation");
    hub.publish(event(9U));
    require(healthy == std::vector<std::uint64_t>{9U}, "consumer failure isolation");
    require(hub.subscriber_count() == 1U, "throwing subscriber removed");
}

void event_json_contract() {
    WorkerLifecycleEvent value{};
    value.type = WorkerLifecycleEventType::artifact;
    value.job_id = "job-\"\\\n";
    value.launch_revision = 4;
    value.sequence = 12U;
    value.worker_timestamp_utc = "2026-08-07T16:30:00Z";
    value.progress = 0.75;
    value.active_step_id = "step-a";
    value.log_level = biocore::application::WorkerLifecycleLogLevel::warning;
    value.component = "plugin";
    value.message = "line\nnext";
    value.artifact_step_id = "step-a";
    value.artifact_output_port = "result";
    value.artifact_plugin_id = "org.biocore.demo";
    value.artifact_plugin_version = "1.0.0";
    value.artifact_module_id = "copy";
    value.artifact_file_type = "txt";
    value.artifact_relative_project_path = "outputs/result.txt";
    value.exit_code = 0;

    const auto json = biocore::presentation::render_worker_lifecycle_event_json(value);
    require(json.find("\"type\":\"worker.lifecycle\"") != std::string::npos, "event envelope");
    require(json.find("\"eventType\":\"artifact\"") != std::string::npos, "event type");
    require(json.find("job-\\\"\\\\\\n") != std::string::npos, "JSON escaping");
    require(json.find("\"logLevel\":\"warning\"") != std::string::npos, "log level");
    require(json.find("\"artifactRelativeProjectPath\":\"outputs/result.txt\"") !=
                std::string::npos,
            "artifact relative path");
    require(json.find("\"exitCode\":0") != std::string::npos, "exit code");

    value.progress = std::numeric_limits<double>::infinity();
    bool rejected = false;
    try {
        static_cast<void>(biocore::presentation::render_worker_lifecycle_event_json(value));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "non-finite progress must not enter JSON");
}

}  // namespace

int main() {
    const bool available = biocore::presentation::drogon_web_server_available();
    const auto server = biocore::presentation::make_local_web_server();
    require(server != nullptr, "web server factory must return an adapter");
    require(server->available() == available, "web server availability contract");
    frontend_asset_store_contract();
    local_browser_session_contract();
    ordered_subscription_contract();
    reentrant_order_contract();
    bounded_buffer_contract();
    consumer_isolation_contract();
    event_json_contract();
    std::cout << "Local web server, frontend asset and lifecycle broadcast tests passed\n";
    return EXIT_SUCCESS;
}
