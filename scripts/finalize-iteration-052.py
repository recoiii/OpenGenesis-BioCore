#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (root / path).read_text(encoding="utf-8")


def write(path: str, content: str) -> None:
    target = root / path
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(content, encoding="utf-8", newline="\n")


def replace_once(path: str, old: str, new: str) -> None:
    text = read(path)
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected one match, found {count}: {old[:120]!r}")
    write(path, text.replace(old, new, 1))


# Browser shell identity and durable operational surfaces.
replace_once(
    "frontend/index.html",
    "<div class=\"eyebrow\">PROJECT GENESIS · CORE 0.1</div>",
    "<div class=\"eyebrow\">OPENGENESIS · CORE 0.2 DEV</div>",
)
replace_once(
    "frontend/index.html",
    "                  <div class=\"detail-field\">\n                    <span>Priority</span>\n                    <strong id=\"detail-priority\">—</strong>\n                  </div>\n                  <div class=\"detail-field\">\n                    <span>Active step</span>",
    "                  <div class=\"detail-field\">\n                    <span>Priority</span>\n                    <strong id=\"detail-priority\">—</strong>\n                  </div>\n                  <div class=\"detail-field\">\n                    <span>Attempt</span>\n                    <strong id=\"detail-attempt\">—</strong>\n                  </div>\n                  <div class=\"detail-field\">\n                    <span>Revision</span>\n                    <strong id=\"detail-revision\">—</strong>\n                  </div>\n                  <div class=\"detail-field\">\n                    <span>Active step</span>",
)
replace_once(
    "frontend/index.html",
    "                <div class=\"detail-actions\">\n                  <a class=\"button button-quiet\" id=\"report-json-link\" href=\"#\" target=\"_blank\" rel=\"noopener\">JSON report</a>\n                  <a class=\"button button-quiet\" id=\"report-html-link\" href=\"#\" target=\"_blank\" rel=\"noopener\">HTML report</a>\n                  <button class=\"button button-quiet\" id=\"refresh-artifacts\" type=\"button\">Refresh artifacts</button>\n                  <button class=\"button button-danger\" id=\"cancel-job\" type=\"button\" hidden>Cancel job</button>\n                </div>",
    "                <section class=\"telemetry-card export-evidence\" id=\"export-evidence\">\n                  <div class=\"telemetry-heading\">\n                    <div>\n                      <div class=\"kicker\">Reproducibility</div>\n                      <h4>Export integrity</h4>\n                    </div>\n                    <span class=\"status-badge status-idle\" id=\"export-status\">Not verified</span>\n                  </div>\n                  <div class=\"detail-grid\">\n                    <div class=\"detail-field\">\n                      <span>Artifacts</span>\n                      <strong id=\"export-artifacts\">Not checked</strong>\n                    </div>\n                    <div class=\"detail-field\">\n                      <span>Producer</span>\n                      <strong id=\"export-producer\">—</strong>\n                    </div>\n                  </div>\n                  <p class=\"telemetry-note\" id=\"export-note\">\n                    Verification is on demand so large artifacts are not re-hashed during ordinary dashboard refreshes.\n                  </p>\n                </section>\n\n                <div class=\"detail-actions\">\n                  <a class=\"button button-quiet\" id=\"report-json-link\" href=\"#\" target=\"_blank\" rel=\"noopener\">JSON report</a>\n                  <a class=\"button button-quiet\" id=\"report-html-link\" href=\"#\" target=\"_blank\" rel=\"noopener\">HTML report</a>\n                  <a class=\"button button-quiet\" id=\"export-manifest-link\" href=\"#\" target=\"_blank\" rel=\"noopener\">Export manifest</a>\n                  <button class=\"button button-quiet\" id=\"verify-export\" type=\"button\">Verify export</button>\n                  <button class=\"button button-quiet\" id=\"refresh-artifacts\" type=\"button\">Refresh artifacts</button>\n                  <button class=\"button button-primary button-small\" id=\"retry-job\" type=\"button\" hidden>Retry interrupted job</button>\n                  <button class=\"button button-danger\" id=\"cancel-job\" type=\"button\" hidden>Cancel job</button>\n                </div>",
)

# Core frontend contracts: exact attempt identity, explicit retry eligibility and export normalization.
replace_once(
    "frontend/assets/app.js",
    "    canCancelJob(status) {\n      return new Set([\"draft\", \"queued\", \"preparing\", \"running\", \"paused\", \"interrupted\"]).has(status);\n    },\n\n    normalizeFailure(failure) {",
    "    canCancelJob(status) {\n      return new Set([\"draft\", \"queued\", \"preparing\", \"running\", \"paused\", \"interrupted\"]).has(status);\n    },\n\n    canRetryJob(status) {\n      return status === \"interrupted\";\n    },\n\n    canVerifyExport(status) {\n      return TERMINAL_STATUSES.has(status);\n    },\n\n    normalizeExportManifest(manifest) {\n      const sha256 = value => typeof value === \"string\" && /^[0-9a-f]{64}$/i.test(value);\n      if (!manifest || typeof manifest !== \"object\" || manifest.schemaVersion !== 1 ||\n          !manifest.producer || typeof manifest.producer !== \"object\" ||\n          manifest.producer.name !== \"OpenGenesis-BioCore\" ||\n          typeof manifest.producer.version !== \"string\" || manifest.producer.version.length === 0 ||\n          typeof manifest.stableSnapshot !== \"boolean\" ||\n          !manifest.report || typeof manifest.report !== \"object\" ||\n          typeof manifest.report.jobId !== \"string\" || manifest.report.jobId.length === 0 ||\n          !Number.isSafeInteger(manifest.report.attemptNumber) || manifest.report.attemptNumber < 1 ||\n          !Array.isArray(manifest.artifacts)) {\n        return null;\n      }\n      const artifacts = [];\n      for (const entry of manifest.artifacts) {\n        if (!entry || typeof entry !== \"object\" ||\n            !entry.metadata || typeof entry.metadata !== \"object\" ||\n            typeof entry.metadata.managedFileId !== \"string\" ||\n            entry.metadata.managedFileId.length === 0 || !sha256(entry.verifiedSha256)) {\n          return null;\n        }\n        artifacts.push({\n          managedFileId: entry.metadata.managedFileId,\n          verifiedSha256: entry.verifiedSha256.toLowerCase()\n        });\n      }\n      if (Number.isSafeInteger(manifest.artifactCount) && manifest.artifactCount !== artifacts.length) {\n        return null;\n      }\n      return {\n        schemaVersion: 1,\n        producerVersion: manifest.producer.version,\n        stableSnapshot: manifest.stableSnapshot,\n        jobId: manifest.report.jobId,\n        attemptNumber: manifest.report.attemptNumber,\n        artifacts\n      };\n    },\n\n    artifactVerificationState(artifact, manifest) {\n      if (!artifact || !manifest || !Array.isArray(manifest.artifacts)) return \"not_verified\";\n      const entry = manifest.artifacts.find(item => item.managedFileId === artifact.managedFileId);\n      if (!entry) return \"not_verified\";\n      const recorded = artifact.checksumAlgorithm === \"sha256\" &&\n                       typeof artifact.checksumValue === \"string\" &&\n                       /^[0-9a-f]{64}$/i.test(artifact.checksumValue)\n        ? artifact.checksumValue.toLowerCase()\n        : null;\n      if (recorded !== null && recorded !== entry.verifiedSha256) return \"mismatch\";\n      return \"verified\";\n    },\n\n    normalizeFailure(failure) {",
)
replace_once(
    "frontend/assets/app.js",
    "        revision: Number.isInteger(job.revision) ? job.revision : 0,\n        failure: core.normalizeFailure(job.failure)",
    "        revision: Number.isInteger(job.revision) ? job.revision : 0,\n        attemptNumber: Number.isSafeInteger(job.attemptNumber) && job.attemptNumber >= 1\n          ? job.attemptNumber\n          : 1,\n        failure: core.normalizeFailure(job.failure)",
)
replace_once(
    "frontend/assets/app.js",
    "        revision: 0,\n        failure: null",
    "        revision: 0,\n        attemptNumber: 1,\n        failure: null",
)
replace_once(
    "frontend/assets/app.js",
    "    artifacts: new Map(),\n    managedFiles: new Map(),",
    "    artifacts: new Map(),\n    exportManifests: new Map(),\n    exportChecks: new Map(),\n    managedFiles: new Map(),",
)
replace_once(
    "frontend/assets/app.js",
    "      pipeline.textContent = job.pipelineId && job.pipelineVersion\n        ? `${job.pipelineId} · ${job.pipelineVersion}`\n        : job.id;",
    "      const pipelineIdentity = job.pipelineId && job.pipelineVersion\n        ? `${job.pipelineId} · ${job.pipelineVersion}`\n        : job.id;\n      pipeline.textContent = `${pipelineIdentity} · attempt ${job.attemptNumber}`;",
)
replace_once(
    "frontend/assets/app.js",
    "    const artifacts = state.artifacts.get(jobId) ?? [];\n    byId(\"artifact-count\").textContent = `${artifacts.length} file${artifacts.length === 1 ? \"\" : \"s\"}`;",
    "    const artifacts = state.artifacts.get(jobId) ?? [];\n    const job = state.jobs.get(jobId);\n    const cachedManifest = state.exportManifests.get(jobId) ?? null;\n    const manifest = cachedManifest !== null && job && cachedManifest.attemptNumber === job.attemptNumber\n      ? cachedManifest\n      : null;\n    byId(\"artifact-count\").textContent = `${artifacts.length} file${artifacts.length === 1 ? \"\" : \"s\"}`;",
)
replace_once(
    "frontend/assets/app.js",
    "        categoryBadge.className = \"artifact-category\";\n        categoryBadge.textContent = core.artifactCategoryLabel(category);\n        name.append(title, subtitle, categoryBadge);",
    "        categoryBadge.className = \"artifact-category\";\n        categoryBadge.textContent = core.artifactCategoryLabel(category);\n        const verification = core.artifactVerificationState(artifact, manifest);\n        const integrityBadge = document.createElement(\"em\");\n        integrityBadge.className = `artifact-integrity artifact-integrity-${verification.replaceAll(\"_\", \"-\")}`;\n        integrityBadge.textContent = verification === \"verified\"\n          ? \"SHA-256 verified\"\n          : verification === \"mismatch\" ? \"Checksum mismatch\" : \"Not verified\";\n        name.append(title, subtitle, categoryBadge, integrityBadge);",
)
replace_once(
    "frontend/assets/app.js",
    "  const renderDetail = () => {",
    "  const renderExportStatus = job => {\n    const cached = state.exportManifests.get(job.id) ?? null;\n    const manifest = cached !== null && cached.attemptNumber === job.attemptNumber ? cached : null;\n    const check = state.exportChecks.get(job.id) ?? { state: \"idle\", message: \"Run verification to recompute artifact SHA-256 values on demand.\" };\n    const status = byId(\"export-status\");\n    status.className = \"status-badge status-idle\";\n    if (check.state === \"checking\") {\n      status.textContent = \"Checking\";\n      status.className = \"status-badge status-running\";\n    } else if (check.state === \"verified\" && manifest !== null) {\n      status.textContent = manifest.stableSnapshot ? \"Verified\" : \"Verified · active\";\n      status.className = \"status-badge status-completed\";\n    } else if (check.state === \"error\") {\n      status.textContent = \"Verification failed\";\n      status.className = \"status-badge status-failed\";\n    } else {\n      status.textContent = \"Not verified\";\n    }\n    byId(\"export-artifacts\").textContent = manifest === null\n      ? \"Not checked\"\n      : `${manifest.artifacts.length} verified`;\n    byId(\"export-producer\").textContent = manifest === null\n      ? \"—\"\n      : `OpenGenesis-BioCore ${manifest.producerVersion}`;\n    byId(\"export-note\").textContent = check.message;\n  };\n\n  const renderDetail = () => {",
)
replace_once(
    "frontend/assets/app.js",
    "    byId(\"detail-priority\").textContent = statusLabel(job.priority);\n    byId(\"detail-step\").textContent = job.activeStepId || \"—\";",
    "    byId(\"detail-priority\").textContent = statusLabel(job.priority);\n    byId(\"detail-attempt\").textContent = String(job.attemptNumber);\n    byId(\"detail-revision\").textContent = String(job.revision);\n    byId(\"detail-step\").textContent = job.activeStepId || \"—\";",
)
replace_once(
    "frontend/assets/app.js",
    "    byId(\"report-json-link\").href = `/api/v1/jobs/${job.id}/report.json`;\n    byId(\"report-html-link\").href = `/api/v1/jobs/${job.id}/report.html`;\n    const cancelButton = byId(\"cancel-job\");",
    "    byId(\"report-json-link\").href = `/api/v1/jobs/${job.id}/report.json`;\n    byId(\"report-html-link\").href = `/api/v1/jobs/${job.id}/report.html`;\n    byId(\"export-manifest-link\").href = `/api/v1/jobs/${job.id}/export-manifest.json`;\n    const verifyButton = byId(\"verify-export\");\n    const exportCheck = state.exportChecks.get(job.id);\n    verifyButton.disabled = !core.canVerifyExport(job.status) || exportCheck?.state === \"checking\";\n    verifyButton.textContent = exportCheck?.state === \"checking\" ? \"Verifying…\" : \"Verify export\";\n    const retryButton = byId(\"retry-job\");\n    retryButton.hidden = !core.canRetryJob(job.status);\n    retryButton.disabled = false;\n    retryButton.textContent = \"Retry interrupted job\";\n    renderExportStatus(job);\n    const cancelButton = byId(\"cancel-job\");",
)
replace_once(
    "frontend/assets/app.js",
    "  const handleSnapshot = payload => {",
    "  const verifyExport = async jobId => {\n    const job = state.jobs.get(jobId);\n    if (!job || !core.canVerifyExport(job.status)) return;\n    state.exportChecks.set(jobId, { state: \"checking\", message: \"Recomputing artifact SHA-256 values from local storage…\" });\n    if (state.selectedJobId === jobId) renderDetail();\n    try {\n      const response = await fetch(`/api/v1/jobs/${jobId}/export-manifest.json`, {\n        method: \"GET\",\n        cache: \"no-store\",\n        credentials: \"same-origin\",\n        headers: { \"Accept\": \"application/json\" }\n      });\n      let payload = null;\n      try { payload = await response.json(); } catch (_) { payload = null; }\n      if (!response.ok) {\n        const message = payload && payload.error && typeof payload.error.message === \"string\"\n          ? payload.error.message\n          : `Export verification failed (${response.status}).`;\n        throw new Error(message);\n      }\n      const manifest = core.normalizeExportManifest(payload);\n      if (manifest === null || manifest.jobId !== jobId || manifest.attemptNumber !== job.attemptNumber) {\n        throw new Error(\"Export manifest identity does not match the selected job attempt.\");\n      }\n      state.exportManifests.set(jobId, manifest);\n      state.exportChecks.set(jobId, {\n        state: \"verified\",\n        message: `${manifest.artifacts.length} artifact SHA-256 value${manifest.artifacts.length === 1 ? \"\" : \"s\"} verified for attempt ${manifest.attemptNumber}.`\n      });\n    } catch (error) {\n      state.exportManifests.delete(jobId);\n      state.exportChecks.set(jobId, {\n        state: \"error\",\n        message: error instanceof Error ? error.message : \"Export verification failed.\"\n      });\n    }\n    if (state.selectedJobId === jobId) renderDetail();\n  };\n\n  const handleSnapshot = payload => {",
)
replace_once(
    "frontend/assets/app.js",
    "  byId(\"refresh-jobs\").addEventListener(\"click\", () => {",
    "  byId(\"verify-export\").addEventListener(\"click\", () => {\n    if (state.selectedJobId !== null) verifyExport(state.selectedJobId).catch(() => {});\n  });\n\n  byId(\"retry-job\").addEventListener(\"click\", async () => {\n    const job = state.selectedJobId ? state.jobs.get(state.selectedJobId) : null;\n    if (!job || !core.canRetryJob(job.status)) return;\n    const button = byId(\"retry-job\");\n    button.disabled = true;\n    button.textContent = \"Retrying…\";\n    try {\n      const response = await fetch(`/api/v1/jobs/${job.id}/retry`, {\n        method: \"POST\",\n        cache: \"no-store\",\n        credentials: \"same-origin\",\n        headers: { \"Accept\": \"application/json\" }\n      });\n      let payload = null;\n      try { payload = await response.json(); } catch (_) { payload = null; }\n      if (!response.ok) {\n        const message = payload && payload.error && typeof payload.error.message === \"string\"\n          ? payload.error.message\n          : `Job retry failed (${response.status}).`;\n        throw new Error(message);\n      }\n      const updated = core.normalizeJob(payload);\n      if (updated === null || updated.id !== job.id || updated.attemptNumber <= job.attemptNumber) {\n        throw new Error(\"Retry response did not advance the job attempt.\");\n      }\n      state.jobs.set(updated.id, updated);\n      state.exportManifests.delete(updated.id);\n      state.exportChecks.delete(updated.id);\n      renderJobs();\n      renderDetail();\n      refreshArtifacts(updated.id).catch(() => {});\n      window.setTimeout(() => loadJobs().catch(() => {}), 300);\n    } catch (error) {\n      state.exportChecks.set(job.id, {\n        state: \"error\",\n        message: error instanceof Error ? error.message : \"Job retry failed.\"\n      });\n      renderDetail();\n    }\n  });\n\n  byId(\"refresh-jobs\").addEventListener(\"click\", () => {",
)

# Minimal styles for operational evidence and per-artifact integrity labels.
css = read("frontend/assets/app.css")
css += r'''

.export-evidence {
  margin-top: 1rem;
  border-color: rgba(131, 183, 232, .22);
  background: rgba(31, 58, 83, .16);
}
.artifact-integrity {
  display: inline-flex;
  margin: 4px 0 0 5px;
  padding: 2px 5px;
  border-radius: 999px;
  font-size: 8px;
  font-style: normal;
}
.artifact-integrity-verified { background: rgba(76, 207, 145, .12); color: var(--mint); }
.artifact-integrity-mismatch { background: rgba(244, 114, 182, .12); color: #f48fbf; }
.artifact-integrity-not-verified { background: rgba(178, 214, 197, .07); color: var(--muted-2); }
'''
write("frontend/assets/app.css", css)

# Static frontend contract test keeps shipped HTML/JS surfaces coupled to the operational contract.
write("tests/frontend_operational_visibility_tests.cpp", r'''#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

#ifndef BIOCORE_SOURCE_ROOT
#error "BIOCORE_SOURCE_ROOT is required"
#endif

namespace {
std::string load(const std::string& relative) {
    std::ifstream input{std::string{BIOCORE_SOURCE_ROOT} + "/" + relative, std::ios::binary};
    if (!input) return {};
    std::ostringstream output;
    output << input.rdbuf();
    return output.str();
}

bool has(const std::string& text, const std::string_view needle) {
    return text.find(needle) != std::string::npos;
}
}

int main() {
    const std::string html = load("frontend/index.html");
    const std::string js = load("frontend/assets/app.js");
    if (html.empty() || js.empty()) return EXIT_FAILURE;

    const bool html_contract =
        has(html, "OPENGENESIS · CORE 0.2 DEV") &&
        !has(html, "PROJECT GENESIS · CORE 0.1") &&
        has(html, "id=\"detail-attempt\"") &&
        has(html, "id=\"detail-revision\"") &&
        has(html, "id=\"export-status\"") &&
        has(html, "id=\"export-manifest-link\"") &&
        has(html, "id=\"verify-export\"") &&
        has(html, "id=\"retry-job\"");

    const bool js_contract =
        has(js, "canRetryJob(status)") &&
        has(js, "normalizeExportManifest(manifest)") &&
        has(js, "artifactVerificationState(artifact, manifest)") &&
        has(js, "attemptNumber:") &&
        has(js, "/export-manifest.json") &&
        has(js, "/retry") &&
        has(js, "state.exportManifests.delete(updated.id)");

    if (!html_contract || !js_contract) {
        std::cerr << "Frontend operational visibility contract failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
''')

replace_once(
    "CMakeLists.txt",
    "    add_test(\n        NAME integration.export_report_foundation\n        COMMAND biocore-export-report-foundation-tests\n    )\nendif()",
    "    add_test(\n        NAME integration.export_report_foundation\n        COMMAND biocore-export-report-foundation-tests\n    )\n\n    add_executable(\n        biocore-frontend-operational-visibility-tests\n        tests/frontend_operational_visibility_tests.cpp\n    )\n    target_compile_definitions(\n        biocore-frontend-operational-visibility-tests\n        PRIVATE BIOCORE_SOURCE_ROOT=\"${CMAKE_CURRENT_SOURCE_DIR}\"\n    )\n    target_link_libraries(\n        biocore-frontend-operational-visibility-tests\n        PRIVATE BioCore::project_warnings BioCore::sanitizers\n    )\n    add_test(\n        NAME integration.frontend_operational_visibility\n        COMMAND biocore-frontend-operational-visibility-tests\n    )\nendif()",
)

Path(__file__).unlink()
print("Iteration 052 transformation complete")
