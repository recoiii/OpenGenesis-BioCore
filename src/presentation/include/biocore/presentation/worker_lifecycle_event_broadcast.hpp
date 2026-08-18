#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "biocore/application/i_worker_lifecycle_event_sink.hpp"

namespace biocore::presentation {

[[nodiscard]] std::string render_worker_lifecycle_event_json(
    const application::WorkerLifecycleEvent& event
);

class WorkerLifecycleEventBroadcastHub final
    : public application::IWorkerLifecycleEventSink {
public:
    using SubscriptionId = std::uint64_t;
    using Consumer = std::function<void(std::string_view)>;

    static constexpr std::size_t maximum_subscribers = 64U;
    static constexpr std::size_t maximum_pending_messages = 1024U;

    [[nodiscard]] SubscriptionId subscribe_paused(Consumer consumer);
    [[nodiscard]] bool activate(SubscriptionId subscription_id);
    void unsubscribe(SubscriptionId subscription_id) noexcept;
    [[nodiscard]] std::size_t subscriber_count() const;

    void publish(const application::WorkerLifecycleEvent& event) override;

private:
    struct Subscription final {
        Consumer consumer;
        bool active{false};
        bool draining{false};
        std::deque<std::string> pending;
    };

    [[nodiscard]] bool drain(SubscriptionId subscription_id);

    mutable std::mutex mutex_;
    std::map<SubscriptionId, Subscription> subscriptions_;
    SubscriptionId next_subscription_id_{1U};
};

}  // namespace biocore::presentation
