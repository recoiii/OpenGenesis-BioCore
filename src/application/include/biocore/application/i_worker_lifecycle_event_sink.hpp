#pragma once

#include "biocore/application/worker_lifecycle_event.hpp"

namespace biocore::application {

class IWorkerLifecycleEventSink {
public:
    virtual ~IWorkerLifecycleEventSink() = default;
    virtual void publish(const WorkerLifecycleEvent& event) = 0;
};

}  // namespace biocore::application
