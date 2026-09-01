from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def write(path: str, content: str) -> None:
    target = ROOT / path
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(content, encoding="utf-8")


def replace_one(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)


# 1) Scheduler resource ceiling is application-level, so every caller gets the same guard.
path = "src/application/include/biocore/application/job_scheduler.hpp"
text = read(path)
text = replace_one(
    text,
    "class JobScheduler final {\npublic:\n    JobScheduler(\n",
    "class JobScheduler final {\npublic:\n"
    "    static constexpr std::size_t default_maximum_concurrent_jobs = 2U;\n"
    "    static constexpr std::size_t maximum_supported_concurrent_jobs = 64U;\n\n"
    "    JobScheduler(\n",
    "job scheduler public resource constants",
)
write(path, text)

path = "src/application/source/job_scheduler.cpp"
text = read(path)
text = replace_one(
    text,
    "    if (maximum_concurrent_jobs_ == 0U) {\n"
    "        throw std::invalid_argument(\"Maximum concurrent jobs must be greater than zero\");\n"
    "    }\n",
    "    if (maximum_concurrent_jobs_ == 0U ||\n"
    "        maximum_concurrent_jobs_ > maximum_supported_concurrent_jobs) {\n"
    "        throw std::invalid_argument(\n"
    "            \"Maximum concurrent jobs must be between 1 and \" +\n"
    "            std::to_string(maximum_supported_concurrent_jobs)\n"
    "        );\n"
    "    }\n",
    "scheduler concurrency validation",
)
write(path, text)

# 2) Lifecycle broadcast queues share one immutable serialized payload per event.
path = "src/presentation/include/biocore/presentation/worker_lifecycle_event_broadcast.hpp"
text = read(path)
text = replace_one(text, "#include <map>\n#include <mutex>\n", "#include <map>\n#include <memory>\n#include <mutex>\n", "broadcast memory include")
text = replace_one(
    text,
    "    using SubscriptionId = std::uint64_t;\n    using Consumer = std::function<void(std::string_view)>;\n",
    "    using SubscriptionId = std::uint64_t;\n"
    "    using Consumer = std::function<void(std::string_view)>;\n"
    "    using SharedMessage = std::shared_ptr<const std::string>;\n",
    "broadcast shared message alias",
)
text = replace_one(
    text,
    "        std::deque<std::string> pending;\n",
    "        std::deque<SharedMessage> pending;\n",
    "broadcast pending shared queue",
)
write(path, text)

path = "src/presentation/source/worker_lifecycle_event_broadcast.cpp"
text = read(path)
text = replace_one(
    text,
    "    const std::string message = render_worker_lifecycle_event_json(event);\n"
    "    std::vector<SubscriptionId> drains;\n",
    "    const auto message = std::make_shared<const std::string>(\n"
    "        render_worker_lifecycle_event_json(event)\n"
    "    );\n"
    "    std::vector<SubscriptionId> drains;\n"
    "    drains.reserve(maximum_subscribers);\n",
    "broadcast allocate shared payload once",
)
text = replace_one(
    text,
    "        Consumer consumer;\n        std::string message;\n",
    "        Consumer consumer;\n        SharedMessage message;\n",
    "broadcast drain shared message variable",
)
text = replace_one(
    text,
    "            consumer(message);\n",
    "            consumer(*message);\n",
    "broadcast consume shared payload",
)
write(path, text)

# 3) Safe CLI concurrency tuning; option parsing remains order-independent.
path = "apps/biocore/main.cpp"
text = read(path)
text = replace_one(
    text,
    "#include \"biocore/application/build_info.hpp\"\n",
    "#include \"biocore/application/build_info.hpp\"\n#include \"biocore/application/job_scheduler.hpp\"\n",
    "main scheduler include",
)
text = replace_one(
    text,
    "constexpr std::string_view port_argument = \"--port\";\n",
    "constexpr std::string_view port_argument = \"--port\";\n"
    "constexpr std::string_view maximum_concurrent_jobs_argument = \"--max-concurrent-jobs\";\n",
    "main concurrency option",
)
text = replace_one(
    text,
    "              << \"  biocore --serve <project-root> [--port <1-65535>]\\n\";\n",
    "              << \"  biocore --serve <project-root> [--port <1-65535>] \"\n"
    "                 \"[--max-concurrent-jobs <1-64>]\\n\";\n",
    "main usage",
)
parse_port_block = '''[[nodiscard]] std::uint16_t parse_port(const std::string_view value) {
    unsigned int parsed = 0U;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || parsed == 0U || parsed > 65535U) {
        throw std::invalid_argument("Server port must be an integer between 1 and 65535");
    }
    return static_cast<std::uint16_t>(parsed);
}
'''
parse_port_plus = parse_port_block + '''
[[nodiscard]] std::size_t parse_maximum_concurrent_jobs(const std::string_view value) {
    std::size_t parsed = 0U;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size() ||
        parsed == 0U ||
        parsed > biocore::application::JobScheduler::maximum_supported_concurrent_jobs) {
        throw std::invalid_argument(
            "Maximum concurrent jobs must be an integer between 1 and " +
            std::to_string(biocore::application::JobScheduler::maximum_supported_concurrent_jobs)
        );
    }
    return parsed;
}
'''
text = replace_one(text, parse_port_block, parse_port_plus, "main concurrency parser")
old_parse = '''[[nodiscard]] biocore::bootstrap::ServeArguments parse_serve_arguments(const int argc, const char* const argv[]) {
    if (argc != 3 && argc != 5) throw std::invalid_argument("Invalid --serve arguments");
    biocore::bootstrap::ServeArguments arguments{
        .project_root = argv[2],
        .port = 8421U,
        .executable_path = argv[0],
        .pipeline_root = std::nullopt,
        .plugin_root = std::nullopt,
        .worker_executable = std::nullopt,
        .frontend_root = std::nullopt,
        .maximum_concurrent_jobs = 2U,
    };
    if (argc == 5) {
        if (std::string_view{argv[3]} != port_argument) throw std::invalid_argument("Expected --port after project root");
        arguments.port = parse_port(argv[4]);
    }
    return arguments;
}
'''
new_parse = '''[[nodiscard]] biocore::bootstrap::ServeArguments parse_serve_arguments(const int argc, const char* const argv[]) {
    if (argc < 3 || argc > 7 || ((argc - 3) % 2) != 0) {
        throw std::invalid_argument("Invalid --serve arguments");
    }
    biocore::bootstrap::ServeArguments arguments{
        .project_root = argv[2],
        .port = 8421U,
        .executable_path = argv[0],
        .pipeline_root = std::nullopt,
        .plugin_root = std::nullopt,
        .worker_executable = std::nullopt,
        .frontend_root = std::nullopt,
        .maximum_concurrent_jobs = biocore::application::JobScheduler::default_maximum_concurrent_jobs,
    };
    bool port_seen = false;
    bool concurrency_seen = false;
    for (int index = 3; index < argc; index += 2) {
        const std::string_view option{argv[index]};
        const std::string_view value{argv[index + 1]};
        if (option == port_argument && !port_seen) {
            arguments.port = parse_port(value);
            port_seen = true;
            continue;
        }
        if (option == maximum_concurrent_jobs_argument && !concurrency_seen) {
            arguments.maximum_concurrent_jobs = parse_maximum_concurrent_jobs(value);
            concurrency_seen = true;
            continue;
        }
        throw std::invalid_argument("Unknown or duplicate --serve option");
    }
    return arguments;
}
'''
text = replace_one(text, old_parse, new_parse, "main serve parser")
write(path, text)

# 4) Fail before touching project state if a programmatic caller exceeds the same ceiling.
path = "apps/biocore/local_server_bootstrap.cpp"
text = read(path)
text = replace_one(
    text,
    "    if (arguments.maximum_concurrent_jobs == 0U) {\n"
    "        throw std::invalid_argument(\"Maximum concurrent jobs must be greater than zero\");\n"
    "    }\n",
    "    if (arguments.maximum_concurrent_jobs == 0U ||\n"
    "        arguments.maximum_concurrent_jobs >\n"
    "            application::JobScheduler::maximum_supported_concurrent_jobs) {\n"
    "        throw std::invalid_argument(\n"
    "            \"Maximum concurrent jobs must be between 1 and \" +\n"
    "            std::to_string(application::JobScheduler::maximum_supported_concurrent_jobs)\n"
    "        );\n"
    "    }\n",
    "bootstrap concurrency ceiling",
)
text = replace_one(
    text,
    "    standard_output << \"OpenGenesis-BioCore pipelines: \" << pipeline_report.loaded_pipelines\n"
    "                    << \", plugin modules: \" << plugin_report.loaded_modules << \".\\n\";\n",
    "    standard_output << \"OpenGenesis-BioCore pipelines: \" << pipeline_report.loaded_pipelines\n"
    "                    << \", plugin modules: \" << plugin_report.loaded_modules << \".\\n\";\n"
    "    standard_output << \"OpenGenesis-BioCore worker concurrency: \"\n"
    "                    << arguments.maximum_concurrent_jobs << \" / \"\n"
    "                    << application::JobScheduler::maximum_supported_concurrent_jobs\n"
    "                    << \" hard maximum.\\n\";\n",
    "bootstrap concurrency visibility",
)
write(path, text)

# 5) Focused resource/performance stress contract.
test = r'''#include <cassert>
#include <concepts>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "biocore/application/job_scheduler.hpp"
#include "biocore/application/worker_lifecycle_event.hpp"
#include "biocore/presentation/worker_lifecycle_event_broadcast.hpp"

namespace {
using Hub = biocore::presentation::WorkerLifecycleEventBroadcastHub;
using Event = biocore::application::WorkerLifecycleEvent;
using EventType = biocore::application::WorkerLifecycleEventType;

Event make_event(const std::uint64_t sequence, std::string message = {}) {
    Event event;
    event.type = EventType::log;
    event.job_id = "resource-job";
    event.launch_revision = 7;
    event.sequence = sequence;
    event.worker_timestamp_utc = "2026-09-01T12:00:00Z";
    event.log_level = biocore::application::WorkerLifecycleLogLevel::info;
    event.component = "resource-test";
    event.message = std::move(message);
    return event;
}
}  // namespace

int main() {
    static_assert(biocore::application::JobScheduler::default_maximum_concurrent_jobs == 2U);
    static_assert(biocore::application::JobScheduler::maximum_supported_concurrent_jobs == 64U);
    static_assert(std::same_as<Hub::SharedMessage, std::shared_ptr<const std::string>>);
    static_assert(Hub::maximum_subscribers == 64U);
    static_assert(Hub::maximum_pending_messages == 1024U);

    Hub hub;
    constexpr std::size_t subscriber_count = 8U;
    constexpr std::size_t message_count = 256U;
    std::vector<std::vector<std::string>> received(subscriber_count);
    std::vector<Hub::SubscriptionId> subscriptions;
    subscriptions.reserve(subscriber_count);

    for (std::size_t index = 0; index < subscriber_count; ++index) {
        subscriptions.push_back(hub.subscribe_paused(
            [&, index](const std::string_view payload) {
                received[index].emplace_back(payload);
            }
        ));
    }

    const std::string payload(2048U, 'x');
    for (std::size_t index = 0; index < message_count; ++index) {
        hub.publish(make_event(static_cast<std::uint64_t>(index + 1U), payload));
    }
    assert(hub.subscriber_count() == subscriber_count);

    for (const auto subscription : subscriptions) {
        assert(hub.activate(subscription));
    }
    for (const auto& messages : received) {
        assert(messages.size() == message_count);
        assert(messages.front() == received.front().front());
        assert(messages.back() == received.front().back());
    }

    Hub bounded;
    const auto slow = bounded.subscribe_paused([](const std::string_view) {});
    static_cast<void>(slow);
    for (std::size_t index = 0; index < Hub::maximum_pending_messages; ++index) {
        bounded.publish(make_event(static_cast<std::uint64_t>(index + 1U), "bounded"));
    }
    assert(bounded.subscriber_count() == 1U);
    bounded.publish(make_event(
        static_cast<std::uint64_t>(Hub::maximum_pending_messages + 1U), "overflow"
    ));
    assert(bounded.subscriber_count() == 0U);

    Hub capacity;
    std::vector<Hub::SubscriptionId> capacity_ids;
    for (std::size_t index = 0; index < Hub::maximum_subscribers; ++index) {
        capacity_ids.push_back(capacity.subscribe_paused([](const std::string_view) {}));
    }
    assert(capacity.subscriber_count() == Hub::maximum_subscribers);
    bool rejected = false;
    try {
        static_cast<void>(capacity.subscribe_paused([](const std::string_view) {}));
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    assert(rejected);

    return 0;
}
'''
write("tests/resource_performance_hardening_tests.cpp", test)

# 6) Register the 75th test.
path = "CMakeLists.txt"
text = read(path)
anchor = '''    add_test(
        NAME integration.frontend_operational_visibility
        COMMAND biocore-frontend-operational-visibility-tests
    )
'''
addition = anchor + '''
    add_executable(
        biocore-resource-performance-hardening-tests
        tests/resource_performance_hardening_tests.cpp
    )
    target_link_libraries(
        biocore-resource-performance-hardening-tests
        PRIVATE BioCore::application BioCore::presentation BioCore::project_warnings BioCore::sanitizers
    )
    add_test(
        NAME integration.resource_performance_hardening
        COMMAND biocore-resource-performance-hardening-tests
    )
'''
text = replace_one(text, anchor, addition, "register resource performance test")
write(path, text)

# 7) Record the accepted predecessor and current iteration contract.
write(
    "docs/development/ITERATION-052-ACCEPTANCE.md",
    """# Iteration 052 Acceptance\n\n"
    "- Verdict: `ACCEPT`\n"
    "- Confidence: `100%`\n"
    "- Exact accepted SHA: `6b2d61ae41b46babc9e908d57667a028808fc5c1`\n"
    "- CI run: `33504518699`\n"
    "- Linux matrix: `74/74` GCC Debug, `74/74` GCC Release, `74/74` Clang Debug, `74/74` GCC ASan+UBSan (`296/296`)\n"
    "- Gemini artifact: `9799111233`\n"
    "- Artifact digest: `sha256:92c0580aba22ee2c62dab8a1ef91bdb1916de12b6009ac662af5d62697a53dac`\n"
    "- Blocking findings: none\n"
    "- Non-blocking findings: none\n\n"
    "Iteration 052 is frozen at `accepted/iteration-052`.\n"
    """,
)

write(
    "docs/development/ITERATION-053.md",
    """# Iteration 053 — Cross-platform, Resource & Performance Hardening\n\n"
    "## Scope\n\n"
    "- Preserve project database schema v8 and Worker Protocol v2.\n"
    "- Preserve all scientific algorithms, thresholds, plugin versions, pipeline IDs and output formats.\n"
    "- Bound scheduler worker concurrency consistently for every caller.\n"
    "- Keep the default worker concurrency at 2 while allowing explicit local tuning from 1 through 64.\n"
    "- Remove per-subscriber lifecycle JSON payload duplication by sharing one immutable serialized message across subscriber queues.\n"
    "- Preserve existing subscriber and pending-message fail-closed limits.\n"
    "- Keep process-tree containment, bounded stdout/stderr drains, managed-file SHA-256 streaming and localhost security unchanged.\n\n"
    "## Resource contract\n\n"
    "`JobScheduler::maximum_supported_concurrent_jobs` is the single application-level hard ceiling. The CLI and local-server bootstrap reject values outside the same range before worker scheduling can begin.\n\n"
    "`WorkerLifecycleEventBroadcastHub` serializes each lifecycle event once. Subscriber queues retain `shared_ptr<const string>` references, so payload storage is not multiplied by the number of connected browser consumers. Slow subscribers are still dropped when their pending queue reaches the existing bound.\n\n"
    "## Validation target\n\n"
    "- CTest floor: at least 75 tests.\n"
    "- New `integration.resource_performance_hardening` stress/contract test.\n"
    "- GCC Debug, GCC Release, Clang Debug and GCC ASan+UBSan.\n"
    "- Exact `0.2.0-dev` identity and existing browser/plugin-pin guardrails.\n"
    "- Gemini review package must contain exactly four Markdown parts.\n"
    """,
)

print("Iteration 053 source transformation complete")
