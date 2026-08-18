#include "biocore/presentation/worker_lifecycle_event_broadcast.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace biocore::presentation {
namespace {

void append_json_string(std::string& output, const std::string_view value) {
    constexpr std::array<char, 16> hexadecimal{
        '0', '1', '2', '3', '4', '5', '6', '7',
        '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
    };

    output.push_back('"');
    for (const char raw_character : value) {
        const auto character = static_cast<unsigned char>(raw_character);
        switch (character) {
            case '"': output += "\\\""; break;
            case '\\': output += "\\\\"; break;
            case '\b': output += "\\b"; break;
            case '\f': output += "\\f"; break;
            case '\n': output += "\\n"; break;
            case '\r': output += "\\r"; break;
            case '\t': output += "\\t"; break;
            default:
                if (character < 0x20U) {
                    output += "\\u00";
                    output.push_back(hexadecimal[(character >> 4U) & 0x0FU]);
                    output.push_back(hexadecimal[character & 0x0FU]);
                } else {
                    output.push_back(static_cast<char>(character));
                }
                break;
        }
    }
    output.push_back('"');
}

void append_i64(std::string& output, const std::int64_t value) {
    std::array<char, 32> buffer{};
    const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    if (result.ec != std::errc{}) {
        throw std::runtime_error("Unable to serialize worker lifecycle integer");
    }
    output.append(buffer.data(), result.ptr);
}

void append_u64(std::string& output, const std::uint64_t value) {
    std::array<char, 32> buffer{};
    const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    if (result.ec != std::errc{}) {
        throw std::runtime_error("Unable to serialize worker lifecycle sequence");
    }
    output.append(buffer.data(), result.ptr);
}

void append_double(std::string& output, const double value) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument("Worker lifecycle progress must be finite");
    }
    std::array<char, 64> buffer{};
    const auto result = std::to_chars(
        buffer.data(),
        buffer.data() + buffer.size(),
        value,
        std::chars_format::general,
        std::numeric_limits<double>::max_digits10
    );
    if (result.ec != std::errc{}) {
        throw std::runtime_error("Unable to serialize worker lifecycle progress");
    }
    output.append(buffer.data(), result.ptr);
}

[[nodiscard]] std::string_view event_type_name(
    const application::WorkerLifecycleEventType type
) {
    using Type = application::WorkerLifecycleEventType;
    switch (type) {
        case Type::ready: return "ready";
        case Type::heartbeat: return "heartbeat";
        case Type::progress: return "progress";
        case Type::log: return "log";
        case Type::artifact: return "artifact";
        case Type::completed: return "completed";
        case Type::failed: return "failed";
    }
    throw std::invalid_argument("Unsupported worker lifecycle event type");
}

[[nodiscard]] std::string_view log_level_name(
    const application::WorkerLifecycleLogLevel level
) {
    using Level = application::WorkerLifecycleLogLevel;
    switch (level) {
        case Level::trace: return "trace";
        case Level::debug: return "debug";
        case Level::info: return "info";
        case Level::warning: return "warning";
        case Level::error: return "error";
        case Level::critical: return "critical";
    }
    throw std::invalid_argument("Unsupported worker lifecycle log level");
}

void append_string_field(
    std::string& output,
    const std::string_view name,
    const std::string_view value
) {
    output.push_back(',');
    append_json_string(output, name);
    output.push_back(':');
    append_json_string(output, value);
}

template <typename Optional>
void append_optional_string_field(
    std::string& output,
    const std::string_view name,
    const Optional& value
) {
    if (value.has_value()) {
        append_string_field(output, name, *value);
    }
}

}  // namespace

std::string render_worker_lifecycle_event_json(
    const application::WorkerLifecycleEvent& event
) {
    std::string output;
    output.reserve(384U);
    output += "{\"type\":\"worker.lifecycle\",\"eventType\":";
    append_json_string(output, event_type_name(event.type));
    append_string_field(output, "jobId", event.job_id);
    output += ",\"launchRevision\":";
    append_i64(output, event.launch_revision);
    output += ",\"sequence\":";
    append_u64(output, event.sequence);
    append_string_field(output, "workerTimestampUtc", event.worker_timestamp_utc);

    if (event.progress.has_value()) {
        output += ",\"progress\":";
        append_double(output, *event.progress);
    }
    append_optional_string_field(output, "activeStepId", event.active_step_id);
    if (event.log_level.has_value()) {
        append_string_field(output, "logLevel", log_level_name(*event.log_level));
    }
    append_optional_string_field(output, "component", event.component);
    append_optional_string_field(output, "message", event.message);
    append_optional_string_field(output, "artifactStepId", event.artifact_step_id);
    append_optional_string_field(output, "artifactOutputPort", event.artifact_output_port);
    append_optional_string_field(output, "artifactPluginId", event.artifact_plugin_id);
    append_optional_string_field(output, "artifactPluginVersion", event.artifact_plugin_version);
    append_optional_string_field(output, "artifactModuleId", event.artifact_module_id);
    append_optional_string_field(output, "artifactFileType", event.artifact_file_type);
    append_optional_string_field(
        output, "artifactRelativeProjectPath", event.artifact_relative_project_path
    );
    if (event.exit_code.has_value()) {
        output += ",\"exitCode\":";
        append_i64(output, *event.exit_code);
    }

    output.push_back('}');
    return output;
}

WorkerLifecycleEventBroadcastHub::SubscriptionId
WorkerLifecycleEventBroadcastHub::subscribe_paused(Consumer consumer) {
    if (!consumer) {
        throw std::invalid_argument("Worker lifecycle consumer must be callable");
    }

    std::scoped_lock lock{mutex_};
    if (subscriptions_.size() >= maximum_subscribers) {
        throw std::runtime_error("Worker lifecycle subscriber capacity is exhausted");
    }
    if (next_subscription_id_ == std::numeric_limits<SubscriptionId>::max()) {
        throw std::overflow_error("Worker lifecycle subscription identifier exhausted");
    }

    const SubscriptionId id = next_subscription_id_++;
    const auto [iterator, inserted] = subscriptions_.emplace(
        id,
        Subscription{
            .consumer = std::move(consumer),
            .active = false,
            .draining = false,
            .pending = {},
        }
    );
    static_cast<void>(iterator);
    if (!inserted) {
        throw std::runtime_error("Worker lifecycle subscription identifier collision");
    }
    return id;
}

bool WorkerLifecycleEventBroadcastHub::activate(const SubscriptionId subscription_id) {
    bool should_drain = false;
    {
        std::scoped_lock lock{mutex_};
        const auto iterator = subscriptions_.find(subscription_id);
        if (iterator == subscriptions_.end()) {
            return false;
        }

        Subscription& subscription = iterator->second;
        subscription.active = true;
        if (!subscription.pending.empty() && !subscription.draining) {
            subscription.draining = true;
            should_drain = true;
        }
    }

    return !should_drain || drain(subscription_id);
}

void WorkerLifecycleEventBroadcastHub::unsubscribe(
    const SubscriptionId subscription_id
) noexcept {
    try {
        std::scoped_lock lock{mutex_};
        subscriptions_.erase(subscription_id);
    } catch (...) {
    }
}

std::size_t WorkerLifecycleEventBroadcastHub::subscriber_count() const {
    std::scoped_lock lock{mutex_};
    return subscriptions_.size();
}

void WorkerLifecycleEventBroadcastHub::publish(
    const application::WorkerLifecycleEvent& event
) {
    const std::string message = render_worker_lifecycle_event_json(event);
    std::vector<SubscriptionId> drains;

    {
        std::scoped_lock lock{mutex_};
        for (auto iterator = subscriptions_.begin(); iterator != subscriptions_.end();) {
            Subscription& subscription = iterator->second;
            if (subscription.pending.size() >= maximum_pending_messages) {
                iterator = subscriptions_.erase(iterator);
                continue;
            }

            subscription.pending.push_back(message);
            if (subscription.active && !subscription.draining) {
                subscription.draining = true;
                drains.push_back(iterator->first);
            }
            ++iterator;
        }
    }

    for (const SubscriptionId id : drains) {
        static_cast<void>(drain(id));
    }
}

bool WorkerLifecycleEventBroadcastHub::drain(const SubscriptionId subscription_id) {
    for (;;) {
        Consumer consumer;
        std::string message;
        {
            std::scoped_lock lock{mutex_};
            const auto iterator = subscriptions_.find(subscription_id);
            if (iterator == subscriptions_.end()) {
                return false;
            }

            Subscription& subscription = iterator->second;
            if (!subscription.active) {
                subscription.draining = false;
                return true;
            }
            if (subscription.pending.empty()) {
                subscription.draining = false;
                return true;
            }

            consumer = subscription.consumer;
            message = std::move(subscription.pending.front());
            subscription.pending.pop_front();
        }

        try {
            consumer(message);
        } catch (...) {
            std::scoped_lock lock{mutex_};
            subscriptions_.erase(subscription_id);
            return false;
        }
    }
}

}  // namespace biocore::presentation
