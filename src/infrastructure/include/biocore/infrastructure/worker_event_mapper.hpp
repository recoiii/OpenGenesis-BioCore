#pragma once

#include "biocore/application/worker_lifecycle_event.hpp"
#include "biocore/worker_protocol/worker_event.hpp"

namespace biocore::infrastructure {

[[nodiscard]] application::WorkerLifecycleEvent to_application_event(
    const worker_protocol::WorkerEvent& event
);

}  // namespace biocore::infrastructure
