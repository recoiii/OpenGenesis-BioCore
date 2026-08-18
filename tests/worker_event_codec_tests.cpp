#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "biocore/worker_protocol/ndjson_framer.hpp"
#include "biocore/worker_protocol/worker_event.hpp"
#include "biocore/worker_protocol/worker_event_codec.hpp"

namespace {

using biocore::worker_protocol::MessageType;
using biocore::worker_protocol::NdjsonFramer;
using biocore::worker_protocol::WorkerEvent;
using biocore::worker_protocol::WorkerLogLevel;
using biocore::worker_protocol::maximum_event_line_bytes;
using biocore::worker_protocol::parse_worker_event;
using biocore::worker_protocol::serialize_worker_event;

[[nodiscard]] WorkerEvent base(const MessageType type, const std::uint64_t sequence) {
    return WorkerEvent{
        .protocol_version = biocore::worker_protocol::current_protocol_version,
        .type = type,
        .job_id = "job-\"quoted\\unicode-ç",
        .job_revision = 7,
        .sequence = sequence,
        .timestamp_utc = "2026-08-06T23:58:00Z",
        .progress = std::nullopt,
        .active_step_id = std::nullopt,
        .log_level = std::nullopt,
        .component = std::nullopt,
        .message = std::nullopt,
        .artifact_step_id = std::nullopt,
        .artifact_output_port = std::nullopt,
        .artifact_plugin_id = std::nullopt,
        .artifact_plugin_version = std::nullopt,
        .artifact_module_id = std::nullopt,
        .artifact_file_type = std::nullopt,
        .artifact_relative_project_path = std::nullopt,
        .exit_code = std::nullopt,
    };
}

[[nodiscard]] bool round_trip_contract() {
    std::vector<WorkerEvent> events;
    events.push_back(base(MessageType::ready, 1U));
    events.push_back(base(MessageType::heartbeat, 2U));

    auto progress = base(MessageType::progress, 3U);
    progress.progress = 0.375;
    progress.active_step_id = "align-step";
    events.push_back(progress);

    auto log = base(MessageType::log, 4U);
    log.log_level = WorkerLogLevel::warning;
    log.component = "worker\\component";
    log.message = "line one\nline two\t\"quoted\"";
    events.push_back(log);

    auto artifact = base(MessageType::artifact, 5U);
    artifact.artifact_step_id = "copy";
    artifact.artifact_output_port = "result";
    artifact.artifact_plugin_id = "org.biocore.demo";
    artifact.artifact_plugin_version = "0.1.0";
    artifact.artifact_module_id = "org.biocore.demo.copy";
    artifact.artifact_file_type = "txt";
    artifact.artifact_relative_project_path = "outputs/job--copy--result.out";
    events.push_back(artifact);

    auto completed = base(MessageType::completed, 6U);
    completed.exit_code = 0;
    events.push_back(completed);

    auto failed = base(MessageType::failed, 7U);
    failed.exit_code = 23;
    failed.message = "tool failed safely";
    events.push_back(failed);

    for (const WorkerEvent& event : events) {
        const std::string encoded = serialize_worker_event(event);
        const WorkerEvent parsed = parse_worker_event(encoded);
        if (parsed.protocol_version != event.protocol_version || parsed.type != event.type ||
            parsed.job_id != event.job_id || parsed.job_revision != event.job_revision ||
            parsed.sequence != event.sequence || parsed.timestamp_utc != event.timestamp_utc ||
            parsed.progress != event.progress || parsed.active_step_id != event.active_step_id ||
            parsed.log_level != event.log_level || parsed.component != event.component ||
            parsed.message != event.message || parsed.artifact_step_id != event.artifact_step_id ||
            parsed.artifact_output_port != event.artifact_output_port ||
            parsed.artifact_plugin_id != event.artifact_plugin_id ||
            parsed.artifact_plugin_version != event.artifact_plugin_version ||
            parsed.artifact_module_id != event.artifact_module_id ||
            parsed.artifact_file_type != event.artifact_file_type ||
            parsed.artifact_relative_project_path != event.artifact_relative_project_path ||
            parsed.exit_code != event.exit_code) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool strict_schema_contract() {
    const std::vector<std::string> invalid{
        "",
        "{}",
        R"({"protocolVersion":2,"protocolVersion":2,"type":"ready","jobId":"j","jobRevision":0,"sequence":1,"timestampUtc":"t"})",
        R"({"protocolVersion":3,"type":"ready","jobId":"j","jobRevision":0,"sequence":1,"timestampUtc":"t"})",
        R"({"protocolVersion":2,"type":"cancel","jobId":"j","jobRevision":0,"sequence":1,"timestampUtc":"t"})",
        R"({"protocolVersion":2,"type":"ready","jobId":"j","jobRevision":0,"sequence":1,"timestampUtc":"t","unknown":1})",
        R"({"protocolVersion":2,"type":"progress","jobId":"j","jobRevision":0,"sequence":1,"timestampUtc":"t","progress":1.1})",
        R"({"protocolVersion":2,"type":"completed","jobId":"j","jobRevision":0,"sequence":1,"timestampUtc":"t","exitCode":2})",
        R"({"protocolVersion":2,"type":"failed","jobId":"j","jobRevision":0,"sequence":1,"timestampUtc":"t","exitCode":0,"message":"x"})",
        R"({"protocolVersion":2,"type":"log","jobId":"j","jobRevision":0,"sequence":1,"timestampUtc":"t","level":"unknown","component":"c","message":"m"})",
        R"({"protocolVersion":2,"type":"artifact","jobId":"j","jobRevision":0,"sequence":1,"timestampUtc":"t","stepId":"s","outputPort":"o","pluginId":"p","pluginVersion":"1","moduleId":"p.m","fileType":"txt"})",
        R"({"protocolVersion":2,"type":"ready","jobId":"\uD800","jobRevision":0,"sequence":1,"timestampUtc":"t"})",
    };
    for (const std::string& value : invalid) {
        try {
            static_cast<void>(parse_worker_event(value));
            return false;
        } catch (const std::invalid_argument&) {
        }
    }

    try {
        static_cast<void>(parse_worker_event(std::string(maximum_event_line_bytes + 1U, 'x')));
        return false;
    } catch (const std::invalid_argument&) {
    }

    std::string invalid_utf8 =
        R"({"protocolVersion":2,"type":"ready","jobId":")";
    invalid_utf8.push_back(static_cast<char>(0xC0));
    invalid_utf8.push_back(static_cast<char>(0xAF));
    invalid_utf8 += R"(","jobRevision":0,"sequence":1,"timestampUtc":"t"})";
    try {
        static_cast<void>(parse_worker_event(invalid_utf8));
        return false;
    } catch (const std::invalid_argument&) {
    }

    auto sequence_overflow = base(MessageType::ready, 1U);
    sequence_overflow.sequence = std::numeric_limits<std::uint64_t>::max();
    try {
        static_cast<void>(serialize_worker_event(sequence_overflow));
        return false;
    } catch (const std::invalid_argument&) {
    }
    return true;
}


[[nodiscard]] bool artifact_requires_protocol_v2_contract() {
    const std::string legacy_artifact =
        R"({"protocolVersion":1,"type":"artifact","jobId":"job-legacy","jobRevision":0,"sequence":1,"timestampUtc":"2026-08-07T08:58:00Z","stepId":"copy","outputPort":"result","pluginId":"org.biocore.demo","pluginVersion":"0.1.0","moduleId":"org.biocore.demo.copy","fileType":"txt","relativeProjectPath":"outputs/job-legacy--copy--result.out"})";

    try {
        static_cast<void>(parse_worker_event(legacy_artifact));
        return false;
    } catch (const std::invalid_argument& error) {
        return std::string_view{error.what()} ==
               "Worker event protocol version is unsupported";
    }
}

[[nodiscard]] bool framing_contract() {
    NdjsonFramer framer{128U};
    auto lines = framer.feed("{\"a\":1}");
    if (!lines.empty() || framer.buffered_bytes() != 7U) return false;
    lines = framer.feed("\nsecond\r\nthird");
    if (lines.size() != 2U || lines[0] != "{\"a\":1}" || lines[1] != "second") {
        return false;
    }
    const auto final = framer.finish();
    if (!final.has_value() || *final != "third" || framer.buffered_bytes() != 0U) {
        return false;
    }

    try {
        NdjsonFramer nul{16U};
        static_cast<void>(nul.feed(std::string{"a\0b", 3U}));
        return false;
    } catch (const std::invalid_argument&) {
    }
    try {
        NdjsonFramer oversized{4U};
        static_cast<void>(oversized.feed("12345"));
        return false;
    } catch (const std::invalid_argument&) {
    }
    NdjsonFramer bounded{4U};
    try {
        static_cast<void>(bounded.feed("12345"));
        return false;
    } catch (const std::invalid_argument&) {
        if (bounded.buffered_bytes() != 4U) return false;
    }
    return true;
}

}  // namespace

int main() {
    if (!round_trip_contract()) {
        std::cerr << "Worker event codec round-trip contract failed\n";
        return EXIT_FAILURE;
    }
    if (!strict_schema_contract()) {
        std::cerr << "Worker event strict schema contract failed\n";
        return EXIT_FAILURE;
    }
    if (!artifact_requires_protocol_v2_contract()) {
        std::cerr << "Worker artifact protocol-version contract failed\n";
        return EXIT_FAILURE;
    }
    if (!framing_contract()) {
        std::cerr << "NDJSON framing contract failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
