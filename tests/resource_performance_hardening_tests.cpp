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

void require(const bool condition, const std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string{message});
    }
}

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
    require(hub.subscriber_count() == subscriber_count, "subscriber count changed before activation");

    for (const auto subscription : subscriptions) {
        require(hub.activate(subscription), "paused subscriber activation failed");
    }
    for (const auto& messages : received) {
        require(messages.size() == message_count, "subscriber did not receive the full message stream");
        require(messages.front() == received.front().front(), "first payload diverged across subscribers");
        require(messages.back() == received.front().back(), "last payload diverged across subscribers");
    }

    Hub bounded;
    const auto slow = bounded.subscribe_paused([](const std::string_view) {});
    static_cast<void>(slow);
    for (std::size_t index = 0; index < Hub::maximum_pending_messages; ++index) {
        bounded.publish(make_event(static_cast<std::uint64_t>(index + 1U), "bounded"));
    }
    require(bounded.subscriber_count() == 1U, "slow subscriber dropped before queue limit");
    bounded.publish(make_event(
        static_cast<std::uint64_t>(Hub::maximum_pending_messages + 1U), "overflow"
    ));
    require(bounded.subscriber_count() == 0U, "slow subscriber survived pending-message overflow");

    Hub capacity;
    for (std::size_t index = 0; index < Hub::maximum_subscribers; ++index) {
        static_cast<void>(capacity.subscribe_paused([](const std::string_view) {}));
    }
    require(capacity.subscriber_count() == Hub::maximum_subscribers, "subscriber capacity was not reached");
    bool rejected = false;
    try {
        static_cast<void>(capacity.subscribe_paused([](const std::string_view) {}));
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "subscriber capacity overflow was accepted");

    return 0;
}
