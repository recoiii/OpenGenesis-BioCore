#pragma once

#include <string>
#include <string_view>

#include "biocore/worker_protocol/worker_event.hpp"

namespace biocore::worker_protocol {

// Produces one JSON object without a trailing newline.
[[nodiscard]] std::string serialize_worker_event(const WorkerEvent& event);

// Parses one complete JSON object. Blank lines, unknown/duplicate fields, nested
// values, invalid UTF-8 escape sequences, and unsupported event types are rejected.
[[nodiscard]] WorkerEvent parse_worker_event(std::string_view json_line);

}  // namespace biocore::worker_protocol
