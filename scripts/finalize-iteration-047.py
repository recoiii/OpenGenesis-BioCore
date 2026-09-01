from pathlib import Path


def replace_exact(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected exactly one match, found {count}")
    p.write_text(text.replace(old, new, 1), encoding="utf-8")

# Durable failure JSON on job list/detail API.
replace_exact(
    "src/presentation/source/local_api.cpp",
    '''[[nodiscard]] std::string optional_json(const std::optional<std::string>& value) {\n    return value.has_value() ? quote(*value) : "null";\n}\n\n[[nodiscard]] std::string render_job(const domain::Job& job) {''',
    '''[[nodiscard]] std::string optional_json(const std::optional<std::string>& value) {\n    return value.has_value() ? quote(*value) : "null";\n}\n\n[[nodiscard]] std::string render_failure(const std::optional<domain::JobFailure>& failure) {\n    if (!failure.has_value()) return "null";\n    return "{" + std::string{"\\"kind\\":"} + quote(domain::to_string(failure->kind())) +\n           ",\\"message\\":" + quote(failure->message()) +\n           ",\\"exitCode\\":" +\n           (failure->exit_code().has_value() ? std::to_string(*failure->exit_code()) : "null") +\n           ",\\"workerTimestampUtc\\":" + optional_json(failure->worker_timestamp_utc()) +\n           ",\\"recordedAtUtc\\":" + quote(failure->recorded_at_utc()) + "}";\n}\n\n[[nodiscard]] std::string render_job(const domain::Job& job) {''',
)
replace_exact(
    "src/presentation/source/local_api.cpp",
    '''           ",\\"finishedAtUtc\\":" + optional_json(job.finished_at_utc()) +\n           ",\\"revision\\":" + std::to_string(job.revision()) + "}";''',
    '''           ",\\"finishedAtUtc\\":" + optional_json(job.finished_at_utc()) +\n           ",\\"revision\\":" + std::to_string(job.revision()) +\n           ",\\"failure\\":" + render_failure(job.failure()) + "}";''',
)

# Durable failure panel in local browser HTML.
replace_exact(
    "frontend/index.html",
    '''                </div>\n\n                <div class="detail-actions">\n                  <a class="button button-quiet" id="report-json-link" href="#" target="_blank" rel="noopener">JSON report</a>''',
    '''                </div>\n\n                <section class="telemetry-card" id="failure-evidence" hidden>\n                  <div class="telemetry-heading">\n                    <div>\n                      <div class="kicker">Durable diagnostic</div>\n                      <h4>Failure evidence</h4>\n                    </div>\n                    <span id="failure-kind">—</span>\n                  </div>\n                  <div class="detail-grid">\n                    <div class="detail-field">\n                      <span>Exit code</span>\n                      <strong id="failure-exit">—</strong>\n                    </div>\n                    <div class="detail-field">\n                      <span>Observed</span>\n                      <strong id="failure-observed">—</strong>\n                    </div>\n                  </div>\n                  <p class="telemetry-note" id="failure-message"></p>\n                </section>\n\n                <div class="detail-actions">\n                  <a class="button button-quiet" id="report-json-link" href="#" target="_blank" rel="noopener">JSON report</a>''',
)

# Browser state normalization, live failed-event evidence and durable detail rendering.
replace_exact(
    "frontend/assets/app.js",
    '''    normalizeJob(job) {\n      if (!job || typeof job !== "object" || typeof job.id !== "string" || job.id.length === 0) {\n        return null;\n      }\n      return {''',
    '''    normalizeFailure(failure) {\n      if (!failure || typeof failure !== "object" ||\n          typeof failure.kind !== "string" || failure.kind.length === 0 ||\n          typeof failure.message !== "string" || failure.message.length === 0 ||\n          typeof failure.recordedAtUtc !== "string" || failure.recordedAtUtc.length === 0) {\n        return null;\n      }\n      return {\n        kind: failure.kind,\n        message: failure.message,\n        exitCode: Number.isInteger(failure.exitCode) ? failure.exitCode : null,\n        workerTimestampUtc: typeof failure.workerTimestampUtc === "string"\n          ? failure.workerTimestampUtc\n          : null,\n        recordedAtUtc: failure.recordedAtUtc\n      };\n    },\n\n    normalizeJob(job) {\n      if (!job || typeof job !== "object" || typeof job.id !== "string" || job.id.length === 0) {\n        return null;\n      }\n      return {''',
)
replace_exact(
    "frontend/assets/app.js",
    '''        finishedAtUtc: typeof job.finishedAtUtc === "string" ? job.finishedAtUtc : null,\n        revision: Number.isInteger(job.revision) ? job.revision : 0\n''',
    '''        finishedAtUtc: typeof job.finishedAtUtc === "string" ? job.finishedAtUtc : null,\n        revision: Number.isInteger(job.revision) ? job.revision : 0,\n        failure: core.normalizeFailure(job.failure)\n''',
)
replace_exact(
    "frontend/assets/app.js",
    '''        finishedAtUtc: null,\n        revision: 0\n      };''',
    '''        finishedAtUtc: null,\n        revision: 0,\n        failure: null\n      };''',
)
replace_exact(
    "frontend/assets/app.js",
    '''        case "completed":\n          if (mutableLifecycle || job.status === "completed") {\n            job.status = "completed";\n            job.progress = 1;\n            job.activeStepId = null;\n          }\n          break;\n        case "failed":\n          if (mutableLifecycle || job.status === "failed") {\n            job.status = "failed";\n            job.activeStepId = null;\n          }\n          break;''',
    '''        case "completed":\n          if (mutableLifecycle || job.status === "completed") {\n            job.status = "completed";\n            job.progress = 1;\n            job.activeStepId = null;\n            job.failure = null;\n          }\n          break;\n        case "failed":\n          if (mutableLifecycle || job.status === "failed") {\n            job.status = "failed";\n            job.activeStepId = null;\n            if (typeof event.message === "string" && event.message.length > 0) {\n              job.failure = {\n                kind: "worker_reported_failure",\n                message: event.message,\n                exitCode: Number.isInteger(event.exitCode) ? event.exitCode : null,\n                workerTimestampUtc: typeof event.workerTimestampUtc === "string"\n                  ? event.workerTimestampUtc\n                  : null,\n                recordedAtUtc: typeof event.workerTimestampUtc === "string"\n                  ? event.workerTimestampUtc\n                  : job.updatedAtUtc\n              };\n            }\n          }\n          break;''',
)
replace_exact(
    "frontend/assets/app.js",
    '''    if (!job) {\n      byId("detail-title").textContent = "No job selected";\n      byId("detail-status").textContent = "Idle";\n      byId("detail-status").className = "status-badge status-idle";\n      return;\n    }''',
    '''    if (!job) {\n      byId("detail-title").textContent = "No job selected";\n      byId("detail-status").textContent = "Idle";\n      byId("detail-status").className = "status-badge status-idle";\n      byId("failure-evidence").hidden = true;\n      return;\n    }''',
)
replace_exact(
    "frontend/assets/app.js",
    '''    byId("detail-progress-label").textContent = `${percent}%`;\n    byId("detail-progress-bar").style.width = `${percent}%`;\n\n    byId("report-json-link").href = `/api/v1/jobs/${job.id}/report.json`;''',
    '''    byId("detail-progress-label").textContent = `${percent}%`;\n    byId("detail-progress-bar").style.width = `${percent}%`;\n\n    const failurePanel = byId("failure-evidence");\n    failurePanel.hidden = job.failure === null;\n    if (job.failure !== null) {\n      byId("failure-kind").textContent = job.failure.kind.replaceAll("_", " ");\n      byId("failure-exit").textContent = job.failure.exitCode === null\n        ? "—"\n        : String(job.failure.exitCode);\n      byId("failure-observed").textContent = humanTime(\n        job.failure.workerTimestampUtc || job.failure.recordedAtUtc\n      );\n      byId("failure-message").textContent = job.failure.message;\n    }\n\n    byId("report-json-link").href = `/api/v1/jobs/${job.id}/report.json`;''',
)

# Schema-v7 job fixture rows must carry terminal evidence for failed/interrupted states.
replace_exact(
    "tests/project_database_tests.cpp",
    '''    for (std::size_t index = 0; index < job_statuses.size(); ++index) {\n        connection.execute(\n            "INSERT INTO jobs(id, status, progress, created_at_utc, updated_at_utc) VALUES "\n            "('job-" +\n            std::to_string(index) + "', '" + std::string{to_string(job_statuses[index])} +\n            "', 0.25, 't', 't');"\n        );\n    }''',
    '''    for (std::size_t index = 0; index < job_statuses.size(); ++index) {\n        const bool has_failure = job_statuses[index] == JobStatus::failed ||\n                                 job_statuses[index] == JobStatus::interrupted;\n        connection.execute(\n            "INSERT INTO jobs(id, status, progress, created_at_utc, updated_at_utc" +\n            std::string{has_failure\n                ? ", failure_kind, failure_message, failure_recorded_at_utc"\n                : ""} +\n            ") VALUES ('job-" + std::to_string(index) + "', '" +\n            std::string{to_string(job_statuses[index])} + "', 0.25, 't', 't'" +\n            std::string{has_failure\n                ? ", 'legacy_terminal_state', 'test terminal evidence', 't'"\n                : ""} +\n            ");"\n        );\n    }''',
)

# Remove staging-only finalizer after it has produced the exact candidate.
Path("scripts/finalize-iteration-047.py").unlink(missing_ok=True)
Path(".github/workflows/iteration-047-finalize.yml").unlink(missing_ok=True)
