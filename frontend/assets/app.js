(() => {
  "use strict";

  const MAX_LOGS_PER_JOB = 200;
  const ACTIVE_STATUSES = new Set(["queued", "preparing", "running", "cancelling"]);
  const ATTENTION_STATUSES = new Set(["failed", "interrupted"]);
  const TERMINAL_STATUSES = new Set(["cancelled", "completed", "failed", "interrupted"]);

  const core = {
    clampProgress(value) {
      if (typeof value !== "number" || !Number.isFinite(value)) return 0;
      return Math.min(1, Math.max(0, value));
    },

    canCancelJob(status) {
      return new Set(["draft", "queued", "preparing", "running", "paused", "interrupted"]).has(status);
    },

    canRetryJob(status) {
      return status === "interrupted";
    },

    canVerifyExport(status) {
      return TERMINAL_STATUSES.has(status);
    },

    normalizeExportManifest(manifest) {
      const sha256 = value => typeof value === "string" && /^[0-9a-f]{64}$/i.test(value);
      if (!manifest || typeof manifest !== "object" || manifest.schemaVersion !== 1 ||
          !manifest.producer || typeof manifest.producer !== "object" ||
          manifest.producer.name !== "OpenGenesis-BioCore" ||
          typeof manifest.producer.version !== "string" || manifest.producer.version.length === 0 ||
          typeof manifest.stableSnapshot !== "boolean" ||
          !manifest.report || typeof manifest.report !== "object" ||
          typeof manifest.report.jobId !== "string" || manifest.report.jobId.length === 0 ||
          !Number.isSafeInteger(manifest.report.attemptNumber) || manifest.report.attemptNumber < 1 ||
          !Array.isArray(manifest.artifacts)) {
        return null;
      }
      const artifacts = [];
      for (const entry of manifest.artifacts) {
        if (!entry || typeof entry !== "object" ||
            !entry.metadata || typeof entry.metadata !== "object" ||
            typeof entry.metadata.managedFileId !== "string" ||
            entry.metadata.managedFileId.length === 0 || !sha256(entry.verifiedSha256)) {
          return null;
        }
        artifacts.push({
          managedFileId: entry.metadata.managedFileId,
          verifiedSha256: entry.verifiedSha256.toLowerCase()
        });
      }
      if (Number.isSafeInteger(manifest.artifactCount) && manifest.artifactCount !== artifacts.length) {
        return null;
      }
      return {
        schemaVersion: 1,
        producerVersion: manifest.producer.version,
        stableSnapshot: manifest.stableSnapshot,
        jobId: manifest.report.jobId,
        attemptNumber: manifest.report.attemptNumber,
        artifacts
      };
    },

    artifactVerificationState(artifact, manifest) {
      if (!artifact || !manifest || !Array.isArray(manifest.artifacts)) return "not_verified";
      const entry = manifest.artifacts.find(item => item.managedFileId === artifact.managedFileId);
      if (!entry) return "not_verified";
      const recorded = artifact.checksumAlgorithm === "sha256" &&
                       typeof artifact.checksumValue === "string" &&
                       /^[0-9a-f]{64}$/i.test(artifact.checksumValue)
        ? artifact.checksumValue.toLowerCase()
        : null;
      if (recorded !== null && recorded !== entry.verifiedSha256) return "mismatch";
      return "verified";
    },

    normalizeFailure(failure) {
      if (!failure || typeof failure !== "object" ||
          typeof failure.kind !== "string" || failure.kind.length === 0 ||
          typeof failure.message !== "string" || failure.message.length === 0 ||
          typeof failure.recordedAtUtc !== "string" || failure.recordedAtUtc.length === 0) {
        return null;
      }
      return {
        kind: failure.kind,
        message: failure.message,
        exitCode: Number.isInteger(failure.exitCode) && failure.exitCode >= 0
          ? failure.exitCode
          : null,
        workerTimestampUtc: typeof failure.workerTimestampUtc === "string"
          ? failure.workerTimestampUtc
          : null,
        recordedAtUtc: failure.recordedAtUtc
      };
    },

    normalizeJob(job) {
      if (!job || typeof job !== "object" || typeof job.id !== "string" || job.id.length === 0) {
        return null;
      }
      return {
        id: job.id,
        analysisId: typeof job.analysisId === "string" ? job.analysisId : null,
        pipelineId: typeof job.pipelineId === "string" ? job.pipelineId : null,
        pipelineVersion: typeof job.pipelineVersion === "string" ? job.pipelineVersion : null,
        status: typeof job.status === "string" ? job.status : "unknown",
        priority: typeof job.priority === "string" ? job.priority : "normal",
        progress: core.clampProgress(job.progress),
        activeStepId: typeof job.activeStepId === "string" ? job.activeStepId : null,
        createdAtUtc: typeof job.createdAtUtc === "string" ? job.createdAtUtc : "",
        updatedAtUtc: typeof job.updatedAtUtc === "string" ? job.updatedAtUtc : "",
        startedAtUtc: typeof job.startedAtUtc === "string" ? job.startedAtUtc : null,
        finishedAtUtc: typeof job.finishedAtUtc === "string" ? job.finishedAtUtc : null,
        revision: Number.isInteger(job.revision) ? job.revision : 0,
        attemptNumber: Number.isSafeInteger(job.attemptNumber) && job.attemptNumber >= 1
          ? job.attemptNumber
          : 1,
        failure: core.normalizeFailure(job.failure)
      };
    },

    replaceSnapshot(state, jobs) {
      const next = new Map();
      if (Array.isArray(jobs)) {
        for (const raw of jobs) {
          const job = core.normalizeJob(raw);
          if (job !== null) next.set(job.id, job);
        }
      }
      state.jobs = next;
      if (state.selectedJobId !== null && !next.has(state.selectedJobId)) {
        state.selectedJobId = null;
      }
      return state;
    },

    acceptLifecycleCursor(state, event) {
      if (!event || typeof event.jobId !== "string" ||
          !Number.isInteger(event.launchRevision) ||
          !Number.isInteger(event.sequence) || event.sequence <= 0) {
        return false;
      }

      const current = state.cursors.get(event.jobId);
      if (current !== undefined) {
        if (event.launchRevision < current.launchRevision) return false;
        if (event.launchRevision === current.launchRevision && event.sequence <= current.sequence) {
          return false;
        }
      }
      state.cursors.set(event.jobId, {
        launchRevision: event.launchRevision,
        sequence: event.sequence
      });
      return true;
    },

    applyLifecycle(state, event) {
      if (!event || event.type !== "worker.lifecycle" ||
          typeof event.eventType !== "string" ||
          typeof event.jobId !== "string") {
        return { accepted: false, artifactRefresh: false };
      }
      if (!core.acceptLifecycleCursor(state, event)) {
        return { accepted: false, artifactRefresh: false };
      }

      const existing = state.jobs.get(event.jobId);
      const job = existing ? { ...existing } : {
        id: event.jobId,
        analysisId: null,
        pipelineId: null,
        pipelineVersion: null,
        status: "unknown",
        priority: "normal",
        progress: 0,
        activeStepId: null,
        createdAtUtc: "",
        updatedAtUtc: "",
        startedAtUtc: null,
        finishedAtUtc: null,
        revision: 0,
        attemptNumber: 1,
        failure: null
      };

      if (typeof event.workerTimestampUtc === "string" &&
          (job.updatedAtUtc.length === 0 || event.workerTimestampUtc > job.updatedAtUtc)) {
        job.updatedAtUtc = event.workerTimestampUtc;
      }

      const mutableLifecycle = !TERMINAL_STATUSES.has(job.status);
      switch (event.eventType) {
        case "ready":
          if (mutableLifecycle) job.status = "running";
          break;
        case "progress":
          if (mutableLifecycle &&
              typeof event.progress === "number" && Number.isFinite(event.progress)) {
            job.progress = Math.max(job.progress, core.clampProgress(event.progress));
          }
          if (mutableLifecycle && typeof event.activeStepId === "string") {
            job.activeStepId = event.activeStepId;
          }
          break;
        case "completed":
          if (mutableLifecycle || job.status === "completed") {
            job.status = "completed";
            job.progress = 1;
            job.activeStepId = null;
            job.failure = null;
          }
          break;
        case "failed":
          if (mutableLifecycle || job.status === "failed") {
            job.status = "failed";
            job.activeStepId = null;
            if (typeof event.message === "string" && event.message.length > 0) {
              job.failure = {
                kind: "worker_reported_failure",
                message: event.message,
                exitCode: Number.isInteger(event.exitCode) && event.exitCode >= 0
                  ? event.exitCode
                  : null,
                workerTimestampUtc: typeof event.workerTimestampUtc === "string"
                  ? event.workerTimestampUtc
                  : null,
                recordedAtUtc: typeof event.workerTimestampUtc === "string"
                  ? event.workerTimestampUtc
                  : job.updatedAtUtc
              };
            }
          }
          break;
        default:
          break;
      }

      state.jobs.set(job.id, job);

      if (event.eventType === "log" &&
          typeof event.message === "string" &&
          typeof event.logLevel === "string") {
        const logs = state.logs.get(event.jobId) ?? [];
        logs.push({
          sequence: event.sequence,
          timestamp: typeof event.workerTimestampUtc === "string" ? event.workerTimestampUtc : "",
          level: event.logLevel,
          component: typeof event.component === "string" ? event.component : "",
          message: event.message
        });
        if (logs.length > MAX_LOGS_PER_JOB) {
          logs.splice(0, logs.length - MAX_LOGS_PER_JOB);
        }
        state.logs.set(event.jobId, logs);
      }

      return {
        accepted: true,
        artifactRefresh: event.eventType === "artifact" ||
                         event.eventType === "completed" ||
                         event.eventType === "failed"
      };
    },

    counts(jobs) {
      let active = 0;
      let completed = 0;
      let attention = 0;
      for (const job of jobs.values()) {
        if (ACTIVE_STATUSES.has(job.status)) active += 1;
        if (job.status === "completed") completed += 1;
        if (ATTENTION_STATUSES.has(job.status)) attention += 1;
      }
      return { active, completed, attention };
    },

    filterJobs(jobs, filter) {
      const values = Array.from(jobs.values());
      values.sort((left, right) => {
        const l = left.createdAtUtc || "";
        const r = right.createdAtUtc || "";
        if (l !== r) return l > r ? -1 : 1;
        return left.id.localeCompare(right.id);
      });
      if (filter === "active") return values.filter(job => ACTIVE_STATUSES.has(job.status));
      if (filter === "completed") return values.filter(job => job.status === "completed");
      if (filter === "attention") return values.filter(job => ATTENTION_STATUSES.has(job.status));
      return values;
    },

    safePathAtom(value) {
      return typeof value === "string" &&
             value.length > 0 &&
             value.length <= 128 &&
             value !== "." &&
             value !== ".." &&
             /^[A-Za-z0-9._-]+$/.test(value);
    },

    normalizeManagedFile(file) {
      if (!file || typeof file !== "object" || typeof file.id !== "string" ||
          !core.safePathAtom(file.id) || typeof file.displayName !== "string" ||
          typeof file.fileType !== "string" || !Number.isSafeInteger(file.sizeBytes) ||
          file.sizeBytes < 0) {
        return null;
      }
      return {
        id: file.id,
        displayName: file.displayName,
        fileType: file.fileType,
        sizeBytes: file.sizeBytes,
        createdAtUtc: typeof file.createdAtUtc === "string" ? file.createdAtUtc : ""
      };
    },

    buildManagedFileBindings(managedFileId) {
      if (!core.safePathAtom(managedFileId)) {
        throw new Error("Managed file ID is invalid.");
      }
      return {
        steps: [{
          stepId: "stats",
          inputs: [{ portName: "source", managedFileId }]
        }]
      };
    },

    buildFastaManagedFileBindings(managedFileId) {
      return core.buildManagedFileBindings(managedFileId);
    },

    buildAlignmentQcBindings(managedFileId) {
      if (!core.safePathAtom(managedFileId)) {
        throw new Error("Alignment QC managed file ID is invalid.");
      }
      return {
        steps: [{
          stepId: "qc",
          inputs: [{ portName: "alignment", managedFileId }]
        }]
      };
    },
    buildPairedFastqBindings(read1ManagedFileId, read2ManagedFileId) {
      if (!core.safePathAtom(read1ManagedFileId) || !core.safePathAtom(read2ManagedFileId)) {
        throw new Error("Paired FASTQ managed file ID is invalid.");
      }
      if (read1ManagedFileId === read2ManagedFileId) {
        throw new Error("Paired FASTQ requires two distinct managed files.");
      }
      return {
        steps: [{
          stepId: "stats",
          inputs: [
            { portName: "read1", managedFileId: read1ManagedFileId },
            { portName: "read2", managedFileId: read2ManagedFileId }
          ]
        }]
      };
    },

    normalizeTrimSettings(values, paired) {
      const integer = (value, label, minimum, maximum) => {
        const parsed = Number(value);
        if (!Number.isSafeInteger(parsed) || parsed < minimum || parsed > maximum) {
          throw new Error(`${label} must be an integer between ${minimum} and ${maximum}.`);
        }
        return parsed;
      };
      const adapter = value => typeof value === "string" ? value.trim().toUpperCase() : "";
      const settings = {
        adapterRead1: adapter(values.adapterRead1),
        adapterRead2: paired ? adapter(values.adapterRead2) : "",
        minAdapterOverlap: integer(values.minAdapterOverlap, "Minimum adapter overlap", 3, 64),
        maxAdapterMismatches: integer(values.maxAdapterMismatches, "Maximum adapter mismatches", 0, 8),
        qualityThreshold: integer(values.qualityThreshold, "Quality threshold", 0, 60),
        minimumLength: integer(values.minimumLength, "Minimum length", 1, 1000000)
      };
      if (settings.maxAdapterMismatches >= settings.minAdapterOverlap) {
        throw new Error("Maximum adapter mismatches must be smaller than minimum adapter overlap.");
      }
      const validAdapter = value => value === "" || /^[ACGTN]+$/.test(value);
      if (!validAdapter(settings.adapterRead1) || (paired && !validAdapter(settings.adapterRead2))) {
        throw new Error("Adapter sequences may contain only A, C, G, T, or N.");
      }
      if (settings.adapterRead1 && settings.minAdapterOverlap > settings.adapterRead1.length) {
        throw new Error("Minimum adapter overlap exceeds the R1 adapter length.");
      }
      if (paired && settings.adapterRead2 && settings.minAdapterOverlap > settings.adapterRead2.length) {
        throw new Error("Minimum adapter overlap exceeds the R2 adapter length.");
      }
      return settings;
    },

    buildSingleFastqTrimBindings(managedFileId, values) {
      if (!core.safePathAtom(managedFileId)) throw new Error("FASTQ managed file ID is invalid.");
      const settings = core.normalizeTrimSettings(values, false);
      return { steps: [{
        stepId: "trim",
        parameters: [
          { name: "adapter-sequence", value: settings.adapterRead1 },
          { name: "min-adapter-overlap", value: settings.minAdapterOverlap },
          { name: "max-adapter-mismatches", value: settings.maxAdapterMismatches },
          { name: "quality-threshold", value: settings.qualityThreshold },
          { name: "minimum-length", value: settings.minimumLength }
        ],
        inputs: [{ portName: "source", managedFileId }]
      }] };
    },

    buildPairedFastqTrimBindings(read1ManagedFileId, read2ManagedFileId, values) {
      if (!core.safePathAtom(read1ManagedFileId) || !core.safePathAtom(read2ManagedFileId)) {
        throw new Error("Paired FASTQ managed file ID is invalid.");
      }
      if (read1ManagedFileId === read2ManagedFileId) throw new Error("Paired FASTQ requires two distinct managed files.");
      const settings = core.normalizeTrimSettings(values, true);
      return { steps: [{
        stepId: "trim",
        parameters: [
          { name: "adapter-read1", value: settings.adapterRead1 },
          { name: "adapter-read2", value: settings.adapterRead2 },
          { name: "min-adapter-overlap", value: settings.minAdapterOverlap },
          { name: "max-adapter-mismatches", value: settings.maxAdapterMismatches },
          { name: "quality-threshold", value: settings.qualityThreshold },
          { name: "minimum-length", value: settings.minimumLength }
        ],
        inputs: [
          { portName: "read1", managedFileId: read1ManagedFileId },
          { portName: "read2", managedFileId: read2ManagedFileId }
        ]
      }] };
    },

    normalizeAlignmentMismatches(value) {
      const parsed = Number(value);
      if (!Number.isSafeInteger(parsed) || parsed < 0 || parsed > 12) {
        throw new Error("Maximum alignment mismatches must be an integer between 0 and 12.");
      }
      return parsed;
    },

    buildSingleAlignmentBindings(referenceManagedFileId, readsManagedFileId, maximumMismatches) {
      if (!core.safePathAtom(referenceManagedFileId) || !core.safePathAtom(readsManagedFileId)) {
        throw new Error("Alignment managed file ID is invalid.");
      }
      return { steps: [{
        stepId: "align",
        parameters: [
          { name: "max-mismatches", value: core.normalizeAlignmentMismatches(maximumMismatches) }
        ],
        inputs: [
          { portName: "reference", managedFileId: referenceManagedFileId },
          { portName: "reads", managedFileId: readsManagedFileId }
        ]
      }] };
    },

    buildPairedAlignmentBindings(referenceManagedFileId, read1ManagedFileId, read2ManagedFileId, maximumMismatches) {
      if (!core.safePathAtom(referenceManagedFileId) ||
          !core.safePathAtom(read1ManagedFileId) || !core.safePathAtom(read2ManagedFileId)) {
        throw new Error("Alignment managed file ID is invalid.");
      }
      if (read1ManagedFileId === read2ManagedFileId) {
        throw new Error("Paired alignment requires two distinct FASTQ managed files.");
      }
      return { steps: [{
        stepId: "align",
        parameters: [
          { name: "max-mismatches", value: core.normalizeAlignmentMismatches(maximumMismatches) }
        ],
        inputs: [
          { portName: "reference", managedFileId: referenceManagedFileId },
          { portName: "read1", managedFileId: read1ManagedFileId },
          { portName: "read2", managedFileId: read2ManagedFileId }
        ]
      }] };
    },


    normalizeVariantCallingSettings(values) {
      const integer = (value, label, minimum, maximum) => {
        const parsed = Number(value);
        if (!Number.isSafeInteger(parsed) || parsed < minimum || parsed > maximum) {
          throw new Error(`${label} must be an integer between ${minimum} and ${maximum}.`);
        }
        return parsed;
      };
      const fraction = Number(values.minAltFraction);
      if (!Number.isFinite(fraction) || fraction < 0.01 || fraction > 1.0) {
        throw new Error("Minimum ALT fraction must be a number between 0.01 and 1.0.");
      }
      return {
        minDepth: integer(values.minDepth, "Minimum depth", 1, 1000000),
        minAltCount: integer(values.minAltCount, "Minimum ALT count", 1, 1000000),
        minAltFraction: fraction,
        minMapq: integer(values.minMapq, "Minimum MAPQ", 0, 60),
        minBaseQuality: integer(values.minBaseQuality, "Minimum base quality", 0, 60)
      };
    },

    buildVariantCallBindings(referenceManagedFileId, alignmentManagedFileId, values) {
      if (!core.safePathAtom(referenceManagedFileId) || !core.safePathAtom(alignmentManagedFileId)) {
        throw new Error("Variant-calling managed file ID is invalid.");
      }
      const settings = core.normalizeVariantCallingSettings(values);
      return { steps: [{
        stepId: "call",
        parameters: [
          { name: "min-depth", value: settings.minDepth },
          { name: "min-alt-count", value: settings.minAltCount },
          { name: "min-alt-fraction", value: settings.minAltFraction },
          { name: "min-mapq", value: settings.minMapq },
          { name: "min-base-quality", value: settings.minBaseQuality }
        ],
        inputs: [
          { portName: "reference", managedFileId: referenceManagedFileId },
          { portName: "alignment", managedFileId: alignmentManagedFileId }
        ]
      }] };
    },

    normalizeVcfQcSettings(values) {
      const integer = (value, label, minimum, maximum) => {
        const parsed = Number(value);
        if (!Number.isSafeInteger(parsed) || parsed < minimum || parsed > maximum) {
          throw new Error(`${label} must be an integer between ${minimum} and ${maximum}.`);
        }
        return parsed;
      };
      const toggle = (value, label) => {
        if (typeof value === "undefined") return true;
        if (typeof value !== "boolean") throw new Error(`${label} must be enabled or disabled.`);
        return value;
      };
      const minAltFraction = Number(values.minAltFraction);
      if (!Number.isFinite(minAltFraction) || minAltFraction < 0.01 || minAltFraction > 1.0) {
        throw new Error("VCF minimum ALT fraction must be a number between 0.01 and 1.0.");
      }
      const minAltBaseQuality = Number(values.minAltBaseQuality);
      if (!Number.isFinite(minAltBaseQuality) || minAltBaseQuality < 0 || minAltBaseQuality > 93) {
        throw new Error("VCF minimum ALT base quality must be a number between 0 and 93.");
      }
      return {
        enableDepthFilter: toggle(values.enableDepthFilter, "VCF depth filter"),
        minDepth: integer(values.minDepth, "VCF minimum depth", 1, 1000000),
        enableAltCountFilter: toggle(values.enableAltCountFilter, "VCF ALT count filter"),
        minAltCount: integer(values.minAltCount, "VCF minimum ALT count", 1, 1000000),
        enableAltFractionFilter: toggle(values.enableAltFractionFilter, "VCF ALT fraction filter"),
        minAltFraction,
        enableAltBaseQualityFilter: toggle(values.enableAltBaseQualityFilter, "VCF ALT base quality filter"),
        minAltBaseQuality
      };
    },

    buildVcfQcBindings(managedFileId, values) {
      if (!core.safePathAtom(managedFileId)) throw new Error("VCF-QC managed file ID is invalid.");
      const settings = core.normalizeVcfQcSettings(values);
      return { steps: [{
        stepId: "qc",
        parameters: [
          { name: "enable-depth-filter", value: settings.enableDepthFilter },
          { name: "min-depth", value: settings.minDepth },
          { name: "enable-alt-count-filter", value: settings.enableAltCountFilter },
          { name: "min-alt-count", value: settings.minAltCount },
          { name: "enable-alt-fraction-filter", value: settings.enableAltFractionFilter },
          { name: "min-alt-fraction", value: settings.minAltFraction },
          { name: "enable-alt-base-quality-filter", value: settings.enableAltBaseQualityFilter },
          { name: "min-alt-base-quality", value: settings.minAltBaseQuality }
        ],
        inputs: [{ portName: "variants", managedFileId }]
      }] };
    },

    buildVariantAnnotationBindings(variantsManagedFileId, annotationManagedFileId) {
      if (!core.safePathAtom(variantsManagedFileId) || !core.safePathAtom(annotationManagedFileId)) {
        throw new Error("Variant annotation managed file ID is invalid.");
      }
      if (variantsManagedFileId === annotationManagedFileId) {
        throw new Error("Variant annotation requires distinct VCF and annotation TSV inputs.");
      }
      return { steps: [{
        stepId: "annotate",
        parameters: [],
        inputs: [
          { portName: "variants", managedFileId: variantsManagedFileId },
          { portName: "annotations", managedFileId: annotationManagedFileId }
        ]
      }] };
    },



    wizardWorkflows() {
      return [
        {
          id: "fasta-qc", label: "FASTA QC", stage: "QC",
          description: "Summarize a managed FASTA input.",
          pipelineId: "org.biocore.fastaqc.summary", pipelineVersion: "0.1.0",
          inputs: [{ key: "source", label: "FASTA input", types: ["fasta"] }],
          parameters: [], expectedArtifacts: ["JSON summary", "TSV table"]
        },
        {
          id: "fastq-qc", label: "FASTQ QC", stage: "QC",
          description: "Summarize a single plain or gzip FASTQ input.",
          pipelineId: "org.biocore.fastqqc.summary", pipelineVersion: "0.1.0",
          inputs: [{ key: "source", label: "FASTQ input", types: ["fastq"] }],
          parameters: [], expectedArtifacts: ["JSON summary", "TSV table"]
        },
        {
          id: "paired-fastq-qc", label: "Paired FASTQ QC", stage: "QC",
          description: "Validate and summarize explicit R1/R2 FASTQ mates.",
          pipelineId: "org.biocore.fastqqc.paired_summary", pipelineVersion: "0.1.0",
          inputs: [
            { key: "read1", label: "FASTQ read 1 (R1)", types: ["fastq"] },
            { key: "read2", label: "FASTQ read 2 (R2)", types: ["fastq"] }
          ],
          parameters: [], expectedArtifacts: ["JSON summary", "TSV table"]
        },
        {
          id: "trim-single", label: "Single FASTQ trimming", stage: "Pre-processing",
          description: "Adapter and 3′ quality trimming for one FASTQ input.",
          pipelineId: "org.biocore.fastqqc.trim_single", pipelineVersion: "0.1.0",
          inputs: [{ key: "source", label: "FASTQ input", types: ["fastq"] }],
          parameters: [
            { key: "adapterRead1", label: "Adapter", kind: "text", defaultValue: "AGATCGGAAGAGCACACGTCTGAACTCCAGTCA" },
            { key: "minAdapterOverlap", label: "Minimum adapter overlap", kind: "number", defaultValue: 6, min: 3, max: 64, step: 1 },
            { key: "maxAdapterMismatches", label: "Maximum adapter mismatches", kind: "number", defaultValue: 0, min: 0, max: 8, step: 1 },
            { key: "qualityThreshold", label: "3′ quality threshold", kind: "number", defaultValue: 20, min: 0, max: 60, step: 1 },
            { key: "minimumLength", label: "Minimum retained length", kind: "number", defaultValue: 20, min: 1, max: 1000000, step: 1 }
          ],
          expectedArtifacts: ["Trimmed FASTQ", "JSON summary", "TSV table"]
        },
        {
          id: "trim-paired", label: "Paired FASTQ trimming", stage: "Pre-processing",
          description: "Pair-preserving adapter and quality trimming for R1/R2.",
          pipelineId: "org.biocore.fastqqc.trim_paired", pipelineVersion: "0.1.0",
          inputs: [
            { key: "read1", label: "FASTQ read 1 (R1)", types: ["fastq"] },
            { key: "read2", label: "FASTQ read 2 (R2)", types: ["fastq"] }
          ],
          parameters: [
            { key: "adapterRead1", label: "R1 adapter", kind: "text", defaultValue: "AGATCGGAAGAGCACACGTCTGAACTCCAGTCA" },
            { key: "adapterRead2", label: "R2 adapter", kind: "text", defaultValue: "AGATCGGAAGAGCGTCGTGTAGGGAAAGAGTGT" },
            { key: "minAdapterOverlap", label: "Minimum adapter overlap", kind: "number", defaultValue: 6, min: 3, max: 64, step: 1 },
            { key: "maxAdapterMismatches", label: "Maximum adapter mismatches", kind: "number", defaultValue: 0, min: 0, max: 8, step: 1 },
            { key: "qualityThreshold", label: "3′ quality threshold", kind: "number", defaultValue: 20, min: 0, max: 60, step: 1 },
            { key: "minimumLength", label: "Minimum retained length", kind: "number", defaultValue: 20, min: 1, max: 1000000, step: 1 }
          ],
          expectedArtifacts: ["Trimmed R1 FASTQ", "Trimmed R2 FASTQ", "JSON summary", "TSV table"]
        },
        {
          id: "align-single", label: "Single-end alignment", stage: "Alignment",
          description: "Native ungapped alignment of FASTQ reads against a reference FASTA.",
          pipelineId: "org.biocore.align.single", pipelineVersion: "0.1.0",
          inputs: [
            { key: "reference", label: "Reference FASTA", types: ["fasta"] },
            { key: "reads", label: "FASTQ reads", types: ["fastq"] }
          ],
          parameters: [{ key: "maxMismatches", label: "Maximum mismatches", kind: "number", defaultValue: 2, min: 0, max: 12, step: 1 }],
          expectedArtifacts: ["SAM alignment", "JSON summary", "TSV table"]
        },
        {
          id: "align-paired", label: "Paired-end alignment", stage: "Alignment",
          description: "Native ungapped alignment of explicit paired FASTQ reads.",
          pipelineId: "org.biocore.align.paired", pipelineVersion: "0.1.0",
          inputs: [
            { key: "reference", label: "Reference FASTA", types: ["fasta"] },
            { key: "read1", label: "FASTQ read 1 (R1)", types: ["fastq"] },
            { key: "read2", label: "FASTQ read 2 (R2)", types: ["fastq"] }
          ],
          parameters: [{ key: "maxMismatches", label: "Maximum mismatches", kind: "number", defaultValue: 2, min: 0, max: 12, step: 1 }],
          expectedArtifacts: ["SAM alignment", "JSON summary", "TSV table"]
        },
        {
          id: "alignment-qc", label: "SAM/BAM alignment QC", stage: "Post-alignment",
          description: "Mapping, MAPQ, mismatch, coverage/depth and TLEN statistics.",
          pipelineId: "org.biocore.alignmentqc.summary", pipelineVersion: "0.1.0",
          inputs: [{ key: "alignment", label: "SAM or BAM alignment", types: ["sam", "bam"] }],
          parameters: [], expectedArtifacts: ["JSON summary", "TSV table"]
        },
        {
          id: "variant-call", label: "Native SNV calling", stage: "Variant analysis",
          description: "Deterministic SNV pileup from reference FASTA plus SAM/BAM.",
          pipelineId: "org.biocore.variantcall.snv", pipelineVersion: "0.1.0",
          inputs: [
            { key: "reference", label: "Reference FASTA", types: ["fasta"] },
            { key: "alignment", label: "SAM or BAM alignment", types: ["sam", "bam"] }
          ],
          parameters: [
            { key: "minDepth", label: "Minimum depth (DP)", kind: "number", defaultValue: 3, min: 1, max: 1000000, step: 1 },
            { key: "minAltCount", label: "Minimum ALT count", kind: "number", defaultValue: 2, min: 1, max: 1000000, step: 1 },
            { key: "minAltFraction", label: "Minimum ALT fraction", kind: "number", defaultValue: 0.20, min: 0.01, max: 1, step: 0.01 },
            { key: "minMapq", label: "Minimum MAPQ", kind: "number", defaultValue: 20, min: 0, max: 60, step: 1 },
            { key: "minBaseQuality", label: "Minimum base quality", kind: "number", defaultValue: 20, min: 0, max: 60, step: 1 }
          ],
          expectedArtifacts: ["VCF variants", "JSON summary", "TSV table"]
        },
        {
          id: "vcf-qc", label: "VCF QC / filtering", stage: "Variant analysis",
          description: "Audit-preserving VCF QC with independently selectable filters.",
          pipelineId: "org.biocore.vcfqc.filter", pipelineVersion: "0.1.0",
          inputs: [{ key: "variants", label: "VCF variants", types: ["vcf"] }],
          parameters: [
            { key: "enableDepthFilter", label: "Enable depth (DP) filter", kind: "checkbox", defaultValue: true },
            { key: "minDepth", label: "Minimum depth (DP)", kind: "number", defaultValue: 3, min: 1, max: 1000000, step: 1 },
            { key: "enableAltCountFilter", label: "Enable ALT count (AC) filter", kind: "checkbox", defaultValue: true },
            { key: "minAltCount", label: "Minimum ALT count", kind: "number", defaultValue: 2, min: 1, max: 1000000, step: 1 },
            { key: "enableAltFractionFilter", label: "Enable ALT fraction (AF) filter", kind: "checkbox", defaultValue: true },
            { key: "minAltFraction", label: "Minimum ALT fraction", kind: "number", defaultValue: 0.20, min: 0.01, max: 1, step: 0.01 },
            { key: "enableAltBaseQualityFilter", label: "Enable ALT base quality (ABQ) filter", kind: "checkbox", defaultValue: true },
            { key: "minAltBaseQuality", label: "Minimum ALT base quality", kind: "number", defaultValue: 20, min: 0, max: 93, step: 0.1 }
          ],
          expectedArtifacts: ["Filtered VCF", "JSON summary", "Annotation-ready TSV"]
        },
        {
          id: "variant-annotate", label: "Local variant annotation + report", stage: "Interpretation",
          description: "Join a VCF to a local annotation TSV and produce an offline report.",
          pipelineId: "org.biocore.variantannotate.local", pipelineVersion: "0.1.0",
          inputs: [
            { key: "variants", label: "VCF variants", types: ["vcf"] },
            { key: "annotations", label: "Local annotation TSV", types: ["tsv"] }
          ],
          parameters: [], expectedArtifacts: ["Annotated VCF", "JSON summary", "Per-ALT TSV", "HTML report"]
        }
      ];
    },

    wizardWorkflow(workflowId) {
      return core.wizardWorkflows().find(workflow => workflow.id === workflowId) ?? null;
    },

    buildWizardPreparation(workflowId, values) {
      const workflow = core.wizardWorkflow(workflowId);
      if (workflow === null) throw new Error("Analysis wizard workflow is invalid.");
      const inputs = values && values.inputs && typeof values.inputs === "object" ? values.inputs : {};
      const parameters = values && values.parameters && typeof values.parameters === "object" ? values.parameters : {};
      let bindings;
      switch (workflowId) {
        case "fasta-qc":
        case "fastq-qc":
          bindings = core.buildManagedFileBindings(inputs.source);
          break;
        case "paired-fastq-qc":
          bindings = core.buildPairedFastqBindings(inputs.read1, inputs.read2);
          break;
        case "trim-single":
          bindings = core.buildSingleFastqTrimBindings(inputs.source, parameters);
          break;
        case "trim-paired":
          bindings = core.buildPairedFastqTrimBindings(inputs.read1, inputs.read2, parameters);
          break;
        case "align-single":
          bindings = core.buildSingleAlignmentBindings(inputs.reference, inputs.reads, parameters.maxMismatches);
          break;
        case "align-paired":
          bindings = core.buildPairedAlignmentBindings(inputs.reference, inputs.read1, inputs.read2, parameters.maxMismatches);
          break;
        case "alignment-qc":
          bindings = core.buildAlignmentQcBindings(inputs.alignment);
          break;
        case "variant-call":
          bindings = core.buildVariantCallBindings(inputs.reference, inputs.alignment, parameters);
          break;
        case "vcf-qc":
          bindings = core.buildVcfQcBindings(inputs.variants, parameters);
          break;
        case "variant-annotate":
          bindings = core.buildVariantAnnotationBindings(inputs.variants, inputs.annotations);
          break;
        default:
          throw new Error("Analysis wizard workflow is not implemented.");
      }
      return {
        workflowId: workflow.id,
        label: workflow.label,
        stage: workflow.stage,
        pipelineId: workflow.pipelineId,
        pipelineVersion: workflow.pipelineVersion,
        bindings,
        expectedArtifacts: workflow.expectedArtifacts.slice()
      };
    },

    artifactCategory(artifact) {
      if (!artifact || typeof artifact !== "object") return "other";
      const fileType = typeof artifact.fileType === "string" ? artifact.fileType.toLowerCase() : "";
      const outputPort = typeof artifact.outputPort === "string" ? artifact.outputPort.toLowerCase() : "";
      if (fileType === "html" || outputPort === "report") return "report";
      if (["fasta", "fastq", "sam", "bam", "vcf"].includes(fileType) ||
          ["alignment", "variants", "filtered", "annotated", "trimmed", "trimmed_read1", "trimmed_read2"].includes(outputPort)) {
        return "primary";
      }
      if (["json", "tsv"].includes(fileType) || ["summary", "table"].includes(outputPort)) return "summary";
      return "other";
    },

    artifactCategoryLabel(category) {
      if (category === "report") return "Reports";
      if (category === "primary") return "Primary results";
      if (category === "summary") return "Summaries & tables";
      return "Other artifacts";
    },

    artifactActionLabel(artifact) {
      return core.artifactCategory(artifact) === "report" ? "Open report" : "Download";
    },

    inferManagedFileType(displayName) {
      if (typeof displayName !== "string") return null;
      const normalized = displayName.trim().toLowerCase();
      if (/\.(fastq|fq)(\.gz)?$/.test(normalized)) return "fastq";
      if (/\.(fa|fasta|fna|ffn|faa|frn)$/.test(normalized)) return "fasta";
      if (/\.sam$/.test(normalized)) return "sam";
      if (/\.bam$/.test(normalized)) return "bam";
      if (/\.vcf$/.test(normalized)) return "vcf";
      if (/\.tsv$/.test(normalized)) return "tsv";
      return null;
    },

    managedFileAnalysis(file) {
      if (!file || typeof file.fileType !== "string") return null;
      if (file.fileType === "fasta") {
        return {
          label: "FASTA QC",
          pipelineId: "org.biocore.fastaqc.summary",
          pipelineVersion: "0.1.0"
        };
      }
      if (file.fileType === "fastq") {
        return {
          label: "FASTQ QC",
          pipelineId: "org.biocore.fastqqc.summary",
          pipelineVersion: "0.1.0"
        };
      }
      if (file.fileType === "sam" || file.fileType === "bam") {
        return {
          label: "Alignment QC",
          pipelineId: "org.biocore.alignmentqc.summary",
          pipelineVersion: "0.1.0"
        };
      }
      if (file.fileType === "vcf") {
        return {
          label: "VCF QC / Filter",
          pipelineId: "org.biocore.vcfqc.filter",
          pipelineVersion: "0.1.0"
        };
      }
      return null;
    },

    formatBytes(value) {
      if (!Number.isFinite(value) || value < 0) return "0 B";
      const units = ["B", "KiB", "MiB", "GiB"];
      let amount = value;
      let unit = 0;
      while (amount >= 1024 && unit < units.length - 1) {
        amount /= 1024;
        unit += 1;
      }
      return `${unit === 0 ? Math.round(amount) : amount.toFixed(amount >= 10 ? 1 : 2)} ${units[unit]}`;
    },

    buildSubmitPayload(values) {
      const pipelineId = values.pipelineId.trim();
      const pipelineVersion = values.pipelineVersion.trim();
      const analysisId = values.analysisId.trim();
      const priority = values.priority;
      const bindingsText = values.bindingsText.trim();

      if (pipelineId.length === 0 || pipelineVersion.length === 0) {
        throw new Error("Pipeline ID and version are required.");
      }
      if (!["low", "normal", "high"].includes(priority)) {
        throw new Error("Priority is invalid.");
      }

      const payload = { pipelineId, pipelineVersion, priority };
      if (analysisId.length > 0) payload.analysisId = analysisId;

      if (bindingsText.length > 0) {
        let bindings;
        try {
          bindings = JSON.parse(bindingsText);
        } catch (_) {
          throw new Error("Bindings must be valid JSON.");
        }
        if (bindings === null || Array.isArray(bindings) || typeof bindings !== "object") {
          throw new Error("Bindings must be a JSON object.");
        }
        payload.bindings = bindings;
      }
      return payload;
    }
  };

  if (typeof module !== "undefined" && module.exports) {
    module.exports = { core, MAX_LOGS_PER_JOB };
  }

  if (typeof document === "undefined") return;

  const state = {
    jobs: new Map(),
    logs: new Map(),
    cursors: new Map(),
    artifacts: new Map(),
    exportManifests: new Map(),
    exportChecks: new Map(),
    managedFiles: new Map(),
    selectedJobId: null,
    filter: "all",
    authenticated: false,
    lifecycleSocket: null,
    reconnectTimer: null,
    reconnectAttempt: 0
  };

  const byId = id => document.getElementById(id);
  const serverState = byId("server-state");
  const serverStateText = byId("server-state-text");
  const liveState = byId("live-state");
  const liveStateText = byId("live-state-text");
  const chip = byId("health-chip");
  const healthLabel = byId("health-label");
  const sessionGate = byId("session-gate");
  const appShell = byId("app-shell");
  const sessionForm = byId("session-form");
  const sessionInput = byId("bootstrap-token");
  const sessionMessage = byId("session-message");
  const sessionButton = byId("session-submit");
  const jobList = byId("job-list");
  const jobsEmpty = byId("jobs-empty");
  const jobForm = byId("job-form");
  const jobFormMessage = byId("job-form-message");
  const jobSubmit = byId("job-submit");
  const liveLog = byId("live-log");
  const artifactList = byId("artifact-list");
  const managedFileInput = byId("managed-file-input");
  const managedFileType = byId("managed-file-type");
  const managedFileUpload = byId("managed-file-upload");
  const managedFileProgress = byId("managed-file-progress");
  const managedFileProgressLabel = byId("managed-file-progress-label");
  const managedFileMessage = byId("managed-file-message");
  const managedFileSelect = byId("managed-file-select");
  const useFastaInput = byId("use-fasta-input");
  const useFastqInput = byId("use-fastq-input");
  const useAlignmentQcInput = byId("use-alignment-qc-input");
  const pairedRead1Select = byId("paired-read1-select");
  const pairedRead2Select = byId("paired-read2-select");
  const usePairedFastqInput = byId("use-paired-fastq-input");
  const useSingleFastqTrim = byId("use-single-fastq-trim");
  const usePairedFastqTrim = byId("use-paired-fastq-trim");
  const alignmentReferenceSelect = byId("alignment-reference-select");
  const alignmentRead1Select = byId("alignment-read1-select");
  const alignmentRead2Select = byId("alignment-read2-select");
  const useSingleAlignment = byId("use-single-alignment");
  const usePairedAlignment = byId("use-paired-alignment");
  const variantReferenceSelect = byId("variant-reference-select");
  const variantAlignmentSelect = byId("variant-alignment-select");
  const useVariantCalling = byId("use-variant-calling");
  const vcfQcSelect = byId("vcf-qc-select");
  const useVcfQc = byId("use-vcf-qc");
  const annotationVcfSelect = byId("annotation-vcf-select");
  const annotationTableSelect = byId("annotation-table-select");
  const useVariantAnnotation = byId("use-variant-annotation");
  const wizardWorkflowSelect = byId("wizard-workflow");
  const wizardStage = byId("wizard-stage");
  const wizardDescription = byId("wizard-description");
  const wizardFields = byId("wizard-fields");
  const wizardMessage = byId("wizard-message");
  const wizardReview = byId("wizard-review");
  const wizardReviewPipeline = byId("wizard-review-pipeline");
  const wizardReviewBindings = byId("wizard-review-bindings");
  const wizardReviewArtifacts = byId("wizard-review-artifacts");
  const wizardPrepareButton = byId("wizard-prepare");
  const wizardSubmitPrepared = byId("wizard-submit-prepared");
  const wizardManageInputs = byId("wizard-manage-inputs");

  const setClassState = (element, kind) => {
    element.classList.remove("online", "offline");
    if (kind) element.classList.add(kind);
  };

  const setCoreState = (kind, topText, chipText) => {
    setClassState(serverState, kind);
    setClassState(chip, kind);
    serverStateText.textContent = topText;
    healthLabel.textContent = chipText;
  };

  const setLiveState = (kind, text) => {
    liveState.hidden = false;
    setClassState(liveState, kind);
    liveStateText.textContent = text;
  };

  const setSessionMessage = (message, kind = "") => {
    sessionMessage.textContent = message;
    sessionMessage.className = `session-message ${kind}`.trim();
  };

  const setFormMessage = (message, kind = "") => {
    jobFormMessage.textContent = message;
    jobFormMessage.className = `form-message ${kind}`.trim();
  };

  const setManagedFileMessage = (message, kind = "") => {
    managedFileMessage.textContent = message;
    managedFileMessage.className = `form-message ${kind}`.trim();
  };

  const setWizardMessage = (message, kind = "") => {
    wizardMessage.textContent = message;
    wizardMessage.className = `form-message ${kind}`.trim();
  };

  const humanTime = value => {
    if (!value) return "—";
    const parsed = new Date(value);
    if (Number.isNaN(parsed.getTime())) return value;
    return parsed.toLocaleString();
  };

  const statusLabel = value => {
    if (!value) return "Unknown";
    return value.charAt(0).toUpperCase() + value.slice(1);
  };

  const statusClass = value => {
    const accepted = new Set(["draft", "queued", "preparing", "running", "paused", "cancelling", "cancelled", "completed", "failed", "interrupted"]);
    return accepted.has(value) ? `status-${value}` : "status-idle";
  };

  const clearElement = element => {
    while (element.firstChild) element.removeChild(element.firstChild);
  };

  const renderMetrics = () => {
    const counts = core.counts(state.jobs);
    byId("metric-active").textContent = String(counts.active);
    byId("metric-completed").textContent = String(counts.completed);
    byId("metric-attention").textContent = String(counts.attention);
  };

  const selectJob = jobId => {
    state.selectedJobId = jobId;
    renderJobs();
    renderDetail();
    refreshArtifacts(jobId).catch(() => {});
  };

  const renderJobs = () => {
    clearElement(jobList);
    const jobs = core.filterJobs(state.jobs, state.filter);
    jobsEmpty.hidden = jobs.length !== 0;

    for (const job of jobs) {
      const button = document.createElement("button");
      button.type = "button";
      button.className = `job-row${state.selectedJobId === job.id ? " selected" : ""}`;
      button.addEventListener("click", () => selectJob(job.id));

      const top = document.createElement("div");
      top.className = "job-row-top";
      const id = document.createElement("div");
      id.className = "job-id";
      id.textContent = job.analysisId || job.id;
      const status = document.createElement("span");
      status.className = `status-badge ${statusClass(job.status)}`;
      status.textContent = statusLabel(job.status);
      top.append(id, status);

      const pipeline = document.createElement("div");
      pipeline.className = "job-pipeline";
      const pipelineIdentity = job.pipelineId && job.pipelineVersion
        ? `${job.pipelineId} · ${job.pipelineVersion}`
        : job.id;
      pipeline.textContent = `${pipelineIdentity} · attempt ${job.attemptNumber}`;

      const bottom = document.createElement("div");
      bottom.className = "job-row-bottom";
      const track = document.createElement("div");
      track.className = "job-progress-mini";
      const bar = document.createElement("span");
      bar.style.width = `${Math.round(core.clampProgress(job.progress) * 100)}%`;
      track.appendChild(bar);
      const label = document.createElement("span");
      label.className = "job-progress-label";
      label.textContent = `${Math.round(core.clampProgress(job.progress) * 100)}%`;
      bottom.append(track, label);

      button.append(top, pipeline, bottom);
      jobList.appendChild(button);
    }
    renderMetrics();
  };

  const renderLogs = jobId => {
    clearElement(liveLog);
    const logs = state.logs.get(jobId) ?? [];
    byId("log-count").textContent = `${logs.length} event${logs.length === 1 ? "" : "s"}`;
    if (logs.length === 0) {
      const empty = document.createElement("div");
      empty.className = "log-empty";
      empty.textContent = "No live log events received for this job.";
      liveLog.appendChild(empty);
      return;
    }

    for (const entry of logs) {
      const row = document.createElement("div");
      row.className = "log-line";

      const time = document.createElement("span");
      time.className = "log-time";
      const date = entry.timestamp ? new Date(entry.timestamp) : null;
      time.textContent = date && !Number.isNaN(date.getTime())
        ? date.toLocaleTimeString()
        : `#${entry.sequence}`;

      const level = document.createElement("span");
      level.className = `log-level ${entry.level}`;
      level.textContent = entry.level;

      const message = document.createElement("span");
      message.className = "log-message";
      message.textContent = entry.component
        ? `[${entry.component}] ${entry.message}`
        : entry.message;

      row.append(time, level, message);
      liveLog.appendChild(row);
    }
    liveLog.scrollTop = liveLog.scrollHeight;
  };

  const renderArtifacts = jobId => {
    clearElement(artifactList);
    const artifacts = state.artifacts.get(jobId) ?? [];
    const job = state.jobs.get(jobId);
    const cachedManifest = state.exportManifests.get(jobId) ?? null;
    const manifest = cachedManifest !== null && job && cachedManifest.attemptNumber === job.attemptNumber
      ? cachedManifest
      : null;
    byId("artifact-count").textContent = `${artifacts.length} file${artifacts.length === 1 ? "" : "s"}`;
    if (artifacts.length === 0) {
      const empty = document.createElement("div");
      empty.className = "artifact-empty";
      empty.textContent = "No generated artifacts.";
      artifactList.appendChild(empty);
      return;
    }

    const categoryOrder = ["report", "primary", "summary", "other"];
    const groups = new Map(categoryOrder.map(category => [category, []]));
    for (const artifact of artifacts) {
      groups.get(core.artifactCategory(artifact)).push(artifact);
    }

    for (const category of categoryOrder) {
      const groupedArtifacts = groups.get(category);
      if (!groupedArtifacts || groupedArtifacts.length === 0) continue;
      const group = document.createElement("section");
      group.className = "artifact-group";
      group.dataset.artifactCategory = category;
      const heading = document.createElement("div");
      heading.className = "artifact-group-heading";
      heading.textContent = `${core.artifactCategoryLabel(category)} · ${groupedArtifacts.length}`;
      group.appendChild(heading);

      for (const artifact of groupedArtifacts) {
        const row = document.createElement("div");
        row.className = "artifact-row";

        const name = document.createElement("div");
        name.className = "artifact-name";
        const title = document.createElement("strong");
        title.textContent = artifact.displayName || artifact.outputPort || artifact.managedFileId || "artifact";
        const subtitle = document.createElement("span");
        const pieces = [artifact.stepId, artifact.fileType, artifact.relativeProjectPath].filter(Boolean);
        subtitle.textContent = pieces.join(" · ");
        const categoryBadge = document.createElement("em");
        categoryBadge.className = "artifact-category";
        categoryBadge.textContent = core.artifactCategoryLabel(category);
        const verification = core.artifactVerificationState(artifact, manifest);
        const integrityBadge = document.createElement("em");
        integrityBadge.className = `artifact-integrity artifact-integrity-${verification.replaceAll("_", "-")}`;
        integrityBadge.textContent = verification === "verified"
          ? "SHA-256 verified"
          : verification === "mismatch" ? "Checksum mismatch" : "Not verified";
        name.append(title, subtitle, categoryBadge, integrityBadge);

        if (core.safePathAtom(artifact.stepId) && core.safePathAtom(artifact.outputPort)) {
          const download = document.createElement("a");
          download.className = "artifact-download";
          download.href = `/api/v1/jobs/${jobId}/artifacts/${artifact.stepId}/${artifact.outputPort}/download`;
          download.textContent = core.artifactActionLabel(artifact);
          if (category === "report") {
            download.target = "_blank";
            download.rel = "noopener";
          }
          row.append(name, download);
        } else {
          row.appendChild(name);
        }

        group.appendChild(row);
      }
      artifactList.appendChild(group);
    }
  };

  const renderExportStatus = job => {
    const cached = state.exportManifests.get(job.id) ?? null;
    const manifest = cached !== null && cached.attemptNumber === job.attemptNumber ? cached : null;
    const check = state.exportChecks.get(job.id) ?? { state: "idle", message: "Run verification to recompute artifact SHA-256 values on demand." };
    const status = byId("export-status");
    status.className = "status-badge status-idle";
    if (check.state === "checking") {
      status.textContent = "Checking";
      status.className = "status-badge status-running";
    } else if (check.state === "verified" && manifest !== null) {
      status.textContent = manifest.stableSnapshot ? "Verified" : "Verified · active";
      status.className = "status-badge status-completed";
    } else if (check.state === "error") {
      status.textContent = "Verification failed";
      status.className = "status-badge status-failed";
    } else {
      status.textContent = "Not verified";
    }
    byId("export-artifacts").textContent = manifest === null
      ? "Not checked"
      : `${manifest.artifacts.length} verified`;
    byId("export-producer").textContent = manifest === null
      ? "—"
      : `OpenGenesis-BioCore ${manifest.producerVersion}`;
    byId("export-note").textContent = check.message;
  };

  const renderDetail = () => {
    const job = state.selectedJobId ? state.jobs.get(state.selectedJobId) : null;
    byId("detail-empty").hidden = Boolean(job);
    byId("detail-content").hidden = !job;

    if (!job) {
      byId("detail-title").textContent = "No job selected";
      byId("detail-status").textContent = "Idle";
      byId("detail-status").className = "status-badge status-idle";
      byId("failure-evidence").hidden = true;
      return;
    }

    byId("detail-title").textContent = job.analysisId || job.id;
    byId("detail-status").textContent = statusLabel(job.status);
    byId("detail-status").className = `status-badge ${statusClass(job.status)}`;
    byId("detail-pipeline").textContent = job.pipelineId && job.pipelineVersion
      ? `${job.pipelineId} · ${job.pipelineVersion}`
      : "—";
    byId("detail-priority").textContent = statusLabel(job.priority);
    byId("detail-attempt").textContent = String(job.attemptNumber);
    byId("detail-revision").textContent = String(job.revision);
    byId("detail-step").textContent = job.activeStepId || "—";
    byId("detail-updated").textContent = humanTime(job.updatedAtUtc);
    const percent = Math.round(core.clampProgress(job.progress) * 100);
    byId("detail-progress-label").textContent = `${percent}%`;
    byId("detail-progress-bar").style.width = `${percent}%`;

    const failurePanel = byId("failure-evidence");
    failurePanel.hidden = job.failure === null;
    if (job.failure !== null) {
      byId("failure-kind").textContent = job.failure.kind.replaceAll("_", " ");
      byId("failure-exit").textContent = job.failure.exitCode === null
        ? "—"
        : String(job.failure.exitCode);
      byId("failure-observed").textContent = humanTime(
        job.failure.workerTimestampUtc || job.failure.recordedAtUtc
      );
      byId("failure-message").textContent = job.failure.message;
    }

    byId("report-json-link").href = `/api/v1/jobs/${job.id}/report.json`;
    byId("report-html-link").href = `/api/v1/jobs/${job.id}/report.html`;
    byId("export-manifest-link").href = `/api/v1/jobs/${job.id}/export-manifest.json`;
    const verifyButton = byId("verify-export");
    const exportCheck = state.exportChecks.get(job.id);
    verifyButton.disabled = !core.canVerifyExport(job.status) || exportCheck?.state === "checking";
    verifyButton.textContent = exportCheck?.state === "checking" ? "Verifying…" : "Verify export";
    const retryButton = byId("retry-job");
    retryButton.hidden = !core.canRetryJob(job.status);
    retryButton.disabled = false;
    retryButton.textContent = "Retry interrupted job";
    renderExportStatus(job);
    const cancelButton = byId("cancel-job");
    cancelButton.hidden = !(core.canCancelJob(job.status) || job.status === "cancelling");
    cancelButton.disabled = job.status === "cancelling";
    cancelButton.textContent = job.status === "cancelling" ? "Cancelling…" : "Cancel job";
    renderLogs(job.id);
    renderArtifacts(job.id);
  };

  const refreshArtifacts = async jobId => {
    if (!jobId || !state.jobs.has(jobId)) return;
    const response = await fetch(`/api/v1/jobs/${jobId}/artifacts`, {
      method: "GET",
      cache: "no-store",
      credentials: "same-origin",
      headers: { "Accept": "application/json" }
    });
    if (!response.ok) return;
    const payload = await response.json();
    if (!Array.isArray(payload)) return;
    state.artifacts.set(jobId, payload);
    if (state.selectedJobId === jobId) renderArtifacts(jobId);
  };

  const verifyExport = async jobId => {
    const job = state.jobs.get(jobId);
    if (!job || !core.canVerifyExport(job.status)) return;
    state.exportChecks.set(jobId, { state: "checking", message: "Recomputing artifact SHA-256 values from local storage…" });
    if (state.selectedJobId === jobId) renderDetail();
    try {
      const response = await fetch(`/api/v1/jobs/${jobId}/export-manifest.json`, {
        method: "GET",
        cache: "no-store",
        credentials: "same-origin",
        headers: { "Accept": "application/json" }
      });
      let payload = null;
      try { payload = await response.json(); } catch (_) { payload = null; }
      if (!response.ok) {
        const message = payload && payload.error && typeof payload.error.message === "string"
          ? payload.error.message
          : `Export verification failed (${response.status}).`;
        throw new Error(message);
      }
      const manifest = core.normalizeExportManifest(payload);
      if (manifest === null || manifest.jobId !== jobId || manifest.attemptNumber !== job.attemptNumber) {
        throw new Error("Export manifest identity does not match the selected job attempt.");
      }
      state.exportManifests.set(jobId, manifest);
      state.exportChecks.set(jobId, {
        state: "verified",
        message: `${manifest.artifacts.length} artifact SHA-256 value${manifest.artifacts.length === 1 ? "" : "s"} verified for attempt ${manifest.attemptNumber}.`
      });
    } catch (error) {
      state.exportManifests.delete(jobId);
      state.exportChecks.set(jobId, {
        state: "error",
        message: error instanceof Error ? error.message : "Export verification failed."
      });
    }
    if (state.selectedJobId === jobId) renderDetail();
  };

  const handleSnapshot = payload => {
    core.replaceSnapshot(state, payload.jobs);
    if (state.selectedJobId === null && state.jobs.size > 0) {
      state.selectedJobId = core.filterJobs(state.jobs, "all")[0].id;
    }
    renderJobs();
    renderDetail();
    if (state.selectedJobId !== null) refreshArtifacts(state.selectedJobId).catch(() => {});
  };

  const handleLifecycle = payload => {
    const result = core.applyLifecycle(state, payload);
    if (!result.accepted) return;
    renderJobs();
    if (state.selectedJobId === payload.jobId) renderDetail();
    if (result.artifactRefresh && state.selectedJobId === payload.jobId) {
      refreshArtifacts(payload.jobId).catch(() => {});
    }
  };

  const handleSocketMessage = event => {
    if (typeof event.data !== "string" || event.data.length > 1024 * 1024) return;
    let payload;
    try {
      payload = JSON.parse(event.data);
    } catch (_) {
      return;
    }
    if (payload && payload.type === "jobs.snapshot") {
      handleSnapshot(payload);
    } else if (payload && payload.type === "worker.lifecycle") {
      handleLifecycle(payload);
    }
  };

  const scheduleReconnect = () => {
    if (!state.authenticated || state.reconnectTimer !== null) return;
    const delay = Math.min(10000, 1000 * (2 ** Math.min(state.reconnectAttempt, 3)));
    state.reconnectAttempt += 1;
    state.reconnectTimer = window.setTimeout(() => {
      state.reconnectTimer = null;
      probeSession().catch(() => scheduleReconnect());
    }, delay);
  };

  const connectLifecycleSocket = () => {
    if (!state.authenticated || state.lifecycleSocket !== null) return;
    const scheme = window.location.protocol === "https:" ? "wss:" : "ws:";
    const socket = new WebSocket(`${scheme}//${window.location.host}/api/v1/ws`);
    state.lifecycleSocket = socket;
    setLiveState("offline", "Connecting live channel…");

    socket.addEventListener("open", () => {
      if (state.lifecycleSocket !== socket) return;
      state.reconnectAttempt = 0;
      setLiveState("online", "Live telemetry connected");
    });
    socket.addEventListener("message", handleSocketMessage);
    socket.addEventListener("close", () => {
      if (state.lifecycleSocket === socket) state.lifecycleSocket = null;
      setLiveState("offline", "Live telemetry disconnected");
      scheduleReconnect();
    });
    socket.addEventListener("error", () => {
      setLiveState("offline", "Live telemetry unavailable");
    });
  };

  const wizardValueSnapshot = () => {
    const inputs = {};
    const parameters = {};
    for (const element of wizardFields.querySelectorAll("[data-wizard-input]")) {
      inputs[element.dataset.wizardInput] = element.value;
    }
    for (const element of wizardFields.querySelectorAll("[data-wizard-param]")) {
      parameters[element.dataset.wizardParam] = element.type === "checkbox" ? element.checked : element.value;
    }
    return { inputs, parameters };
  };

  const renderWizardFields = (preserve = false) => {
    const previous = preserve ? wizardValueSnapshot() : { inputs: {}, parameters: {} };
    const workflow = core.wizardWorkflow(wizardWorkflowSelect.value);
    clearElement(wizardFields);
    wizardReview.hidden = true;
    wizardSubmitPrepared.disabled = true;
    if (workflow === null) {
      wizardStage.textContent = "—";
      wizardDescription.textContent = "Select an analysis workflow.";
      setWizardMessage("Select an analysis workflow.", "warning");
      return;
    }

    wizardStage.textContent = workflow.stage;
    wizardDescription.textContent = workflow.description;

    const files = Array.from(state.managedFiles.values()).sort((left, right) =>
      left.displayName.localeCompare(right.displayName));

    for (const inputDefinition of workflow.inputs) {
      const label = document.createElement("label");
      const title = document.createElement("span");
      title.textContent = inputDefinition.label;
      const select = document.createElement("select");
      select.dataset.wizardInput = inputDefinition.key;
      select.id = `wizard-input-${inputDefinition.key}`;
      const empty = document.createElement("option");
      empty.value = "";
      empty.textContent = state.managedFiles.size === 0 ? "No managed inputs available" : `Select ${inputDefinition.label}`;
      select.appendChild(empty);
      for (const file of files) {
        if (!inputDefinition.types.includes(file.fileType)) continue;
        const option = document.createElement("option");
        option.value = file.id;
        option.textContent = `${file.displayName} · ${file.fileType} · ${core.formatBytes(file.sizeBytes)}`;
        select.appendChild(option);
      }
      const previousValue = previous.inputs[inputDefinition.key];
      if (typeof previousValue === "string" && Array.from(select.options).some(option => option.value === previousValue)) {
        select.value = previousValue;
      }
      label.append(title, select);
      wizardFields.appendChild(label);
    }

    for (const parameter of workflow.parameters) {
      const label = document.createElement("label");
      const previousValue = previous.parameters[parameter.key];
      if (parameter.kind === "checkbox") {
        label.className = "wizard-checkbox";
        const input = document.createElement("input");
        input.type = "checkbox";
        input.dataset.wizardParam = parameter.key;
        input.id = `wizard-param-${parameter.key}`;
        input.checked = typeof previousValue === "boolean" ? previousValue : Boolean(parameter.defaultValue);
        const title = document.createElement("span");
        title.textContent = parameter.label;
        label.append(input, title);
      } else {
        const title = document.createElement("span");
        title.textContent = parameter.label;
        const input = document.createElement("input");
        input.type = parameter.kind === "number" ? "number" : "text";
        input.dataset.wizardParam = parameter.key;
        input.id = `wizard-param-${parameter.key}`;
        input.value = previousValue !== undefined ? String(previousValue) : String(parameter.defaultValue);
        if (parameter.kind === "number") {
          if (parameter.min !== undefined) input.min = String(parameter.min);
          if (parameter.max !== undefined) input.max = String(parameter.max);
          if (parameter.step !== undefined) input.step = String(parameter.step);
        }
        label.append(title, input);
      }
      wizardFields.appendChild(label);
    }

    const missingKinds = workflow.inputs.filter(inputDefinition =>
      !files.some(file => inputDefinition.types.includes(file.fileType)));
    if (missingKinds.length > 0) {
      setWizardMessage(`Import/select required managed inputs: ${missingKinds.map(item => item.label).join(", ")}.`, "warning");
    } else {
      setWizardMessage("Choose the managed inputs and review the prepared job.");
    }
  };

  const wizardCurrentValues = () => wizardValueSnapshot();

  const prepareWizardJob = () => {
    const prepared = core.buildWizardPreparation(wizardWorkflowSelect.value, wizardCurrentValues());
    byId("pipeline-id").value = prepared.pipelineId;
    byId("pipeline-version").value = prepared.pipelineVersion;
    byId("job-bindings").value = JSON.stringify(prepared.bindings, null, 2);

    wizardReviewPipeline.textContent = `${prepared.pipelineId} · ${prepared.pipelineVersion}`;
    wizardReviewBindings.textContent = JSON.stringify(prepared.bindings, null, 2);
    clearElement(wizardReviewArtifacts);
    for (const artifact of prepared.expectedArtifacts) {
      const chip = document.createElement("span");
      chip.className = "wizard-artifact-chip";
      chip.textContent = artifact;
      wizardReviewArtifacts.appendChild(chip);
    }
    wizardReview.hidden = false;
    wizardSubmitPrepared.disabled = false;
    setWizardMessage(`${prepared.label} is ready for exact submission. Review the bindings, then submit.`, "ready");
    return prepared;
  };

  const renderManagedFiles = () => {
    const values = Array.from(state.managedFiles.values());
    values.sort((left, right) => left.displayName.localeCompare(right.displayName));

    const populate = (select, requiredType, emptyLabel) => {
      clearElement(select);
      const empty = document.createElement("option");
      empty.value = "";
      empty.textContent = state.managedFiles.size === 0 ? "No managed inputs available" : emptyLabel;
      select.appendChild(empty);
      for (const file of values) {
        if (requiredType !== null) {
          const accepted = Array.isArray(requiredType) ? requiredType.includes(file.fileType) : file.fileType === requiredType;
          if (!accepted) continue;
        }
        const option = document.createElement("option");
        option.value = file.id;
        option.textContent = `${file.displayName} · ${file.fileType} · ${core.formatBytes(file.sizeBytes)}`;
        select.appendChild(option);
      }
    };

    populate(managedFileSelect, null, "Select a managed input");
    populate(pairedRead1Select, "fastq", "Select FASTQ read 1");
    populate(pairedRead2Select, "fastq", "Select FASTQ read 2");
    populate(alignmentReferenceSelect, "fasta", "Select reference FASTA");
    populate(alignmentRead1Select, "fastq", "Select FASTQ reads / R1");
    populate(alignmentRead2Select, "fastq", "Select FASTQ R2");
    populate(variantReferenceSelect, "fasta", "Select variant reference FASTA");
    populate(variantAlignmentSelect, ["sam", "bam"], "Select SAM/BAM alignment");
    populate(vcfQcSelect, "vcf", "Select VCF for QC/filtering");
    populate(annotationVcfSelect, "vcf", "Select VCF for annotation");
    populate(annotationTableSelect, "tsv", "Select local annotation TSV");
    renderWizardFields(true);
  };

  const loadManagedFiles = async () => {
    const response = await fetch("/api/v1/files", {
      method: "GET",
      cache: "no-store",
      credentials: "same-origin",
      headers: { "Accept": "application/json" }
    });
    if (!response.ok) throw new Error(`files-${response.status}`);
    const payload = await response.json();
    const next = new Map();
    if (Array.isArray(payload)) {
      for (const raw of payload) {
        const file = core.normalizeManagedFile(raw);
        if (file !== null) next.set(file.id, file);
      }
    }
    state.managedFiles = next;
    renderManagedFiles();
  };

  const applyManagedFile = (managedFileId, requiredType = null) => {
    const file = state.managedFiles.get(managedFileId);
    if (!file) throw new Error("Select a managed input first.");
    if (requiredType !== null && file.fileType !== requiredType) {
      throw new Error(`${requiredType.toUpperCase()} QC requires a managed file with file type ${requiredType}.`);
    }
    const analysis = core.managedFileAnalysis(file);
    if (analysis === null) {
      throw new Error(`No browser QC shortcut is available for file type ${file.fileType}.`);
    }
    byId("pipeline-id").value = analysis.pipelineId;
    byId("pipeline-version").value = analysis.pipelineVersion;
    const bindings = analysis.pipelineId === "org.biocore.alignmentqc.summary"
      ? core.buildAlignmentQcBindings(file.id)
      : analysis.pipelineId === "org.biocore.vcfqc.filter"
        ? core.buildVcfQcBindings(file.id, { minDepth: 3, minAltCount: 2, minAltFraction: 0.20, minAltBaseQuality: 20 })
        : core.buildManagedFileBindings(file.id);
    byId("job-bindings").value = JSON.stringify(bindings, null, 2);
    setFormMessage(`${analysis.label} prepared with ${file.displayName}.`, "ready");
  };

  const applyPairedFastq = (read1Id, read2Id) => {
    const read1 = state.managedFiles.get(read1Id);
    const read2 = state.managedFiles.get(read2Id);
    if (!read1 || !read2) throw new Error("Select both paired FASTQ inputs first.");
    if (read1.fileType !== "fastq" || read2.fileType !== "fastq") {
      throw new Error("Paired FASTQ QC requires two managed FASTQ inputs.");
    }
    byId("pipeline-id").value = "org.biocore.fastqqc.paired_summary";
    byId("pipeline-version").value = "0.1.0";
    byId("job-bindings").value = JSON.stringify(
      core.buildPairedFastqBindings(read1.id, read2.id), null, 2
    );
    setFormMessage(
      `Paired FASTQ QC prepared with R1 ${read1.displayName} and R2 ${read2.displayName}.`,
      "ready"
    );
  };

  const trimSettings = () => ({
    adapterRead1: byId("trim-adapter-read1").value,
    adapterRead2: byId("trim-adapter-read2").value,
    minAdapterOverlap: byId("trim-min-adapter-overlap").value,
    maxAdapterMismatches: byId("trim-max-adapter-mismatches").value,
    qualityThreshold: byId("trim-quality-threshold").value,
    minimumLength: byId("trim-minimum-length").value
  });

  const applySingleFastqTrim = managedFileId => {
    const file = state.managedFiles.get(managedFileId);
    if (!file || file.fileType !== "fastq") throw new Error("Single FASTQ trimming requires one managed FASTQ input.");
    byId("pipeline-id").value = "org.biocore.fastqqc.trim_single";
    byId("pipeline-version").value = "0.1.0";
    byId("job-bindings").value = JSON.stringify(
      core.buildSingleFastqTrimBindings(file.id, trimSettings()), null, 2
    );
    setFormMessage(`FASTQ trimming prepared with ${file.displayName}.`, "ready");
  };

  const applyPairedFastqTrim = (read1Id, read2Id) => {
    const read1 = state.managedFiles.get(read1Id);
    const read2 = state.managedFiles.get(read2Id);
    if (!read1 || !read2 || read1.fileType !== "fastq" || read2.fileType !== "fastq") {
      throw new Error("Paired FASTQ trimming requires two managed FASTQ inputs.");
    }
    byId("pipeline-id").value = "org.biocore.fastqqc.trim_paired";
    byId("pipeline-version").value = "0.1.0";
    byId("job-bindings").value = JSON.stringify(
      core.buildPairedFastqTrimBindings(read1.id, read2.id, trimSettings()), null, 2
    );
    setFormMessage(`Paired FASTQ trimming prepared with R1 ${read1.displayName} and R2 ${read2.displayName}.`, "ready");
  };

  const alignmentMaximumMismatches = () => byId("alignment-max-mismatches").value;

  const applySingleAlignment = (referenceId, readsId) => {
    const reference = state.managedFiles.get(referenceId);
    const reads = state.managedFiles.get(readsId);
    if (!reference || reference.fileType !== "fasta") {
      throw new Error("Single-end alignment requires a managed reference FASTA.");
    }
    if (!reads || reads.fileType !== "fastq") {
      throw new Error("Single-end alignment requires managed FASTQ reads.");
    }
    byId("pipeline-id").value = "org.biocore.align.single";
    byId("pipeline-version").value = "0.1.0";
    byId("job-bindings").value = JSON.stringify(
      core.buildSingleAlignmentBindings(reference.id, reads.id, alignmentMaximumMismatches()), null, 2
    );
    setFormMessage(`Single-end alignment prepared: ${reads.displayName} → ${reference.displayName}.`, "ready");
  };

  const applyPairedAlignment = (referenceId, read1Id, read2Id) => {
    const reference = state.managedFiles.get(referenceId);
    const read1 = state.managedFiles.get(read1Id);
    const read2 = state.managedFiles.get(read2Id);
    if (!reference || reference.fileType !== "fasta") {
      throw new Error("Paired alignment requires a managed reference FASTA.");
    }
    if (!read1 || !read2 || read1.fileType !== "fastq" || read2.fileType !== "fastq") {
      throw new Error("Paired alignment requires two managed FASTQ inputs.");
    }
    byId("pipeline-id").value = "org.biocore.align.paired";
    byId("pipeline-version").value = "0.1.0";
    byId("job-bindings").value = JSON.stringify(
      core.buildPairedAlignmentBindings(reference.id, read1.id, read2.id, alignmentMaximumMismatches()), null, 2
    );
    setFormMessage(`Paired alignment prepared: R1 ${read1.displayName}, R2 ${read2.displayName} → ${reference.displayName}.`, "ready");
  };


  const variantCallingSettings = () => ({
    minDepth: byId("variant-min-depth").value,
    minAltCount: byId("variant-min-alt-count").value,
    minAltFraction: byId("variant-min-alt-fraction").value,
    minMapq: byId("variant-min-mapq").value,
    minBaseQuality: byId("variant-min-base-quality").value
  });

  const applyVariantCalling = (referenceId, alignmentId) => {
    const reference = state.managedFiles.get(referenceId);
    const alignment = state.managedFiles.get(alignmentId);
    if (!reference || reference.fileType !== "fasta") {
      throw new Error("Variant calling requires a managed reference FASTA.");
    }
    if (!alignment || (alignment.fileType !== "sam" && alignment.fileType !== "bam")) {
      throw new Error("Variant calling requires a managed SAM or BAM alignment.");
    }
    byId("pipeline-id").value = "org.biocore.variantcall.snv";
    byId("pipeline-version").value = "0.1.0";
    byId("job-bindings").value = JSON.stringify(
      core.buildVariantCallBindings(reference.id, alignment.id, variantCallingSettings()), null, 2
    );
    setFormMessage(`SNV calling prepared: ${alignment.displayName} against ${reference.displayName}.`, "ready");
  };

  const vcfQcSettings = () => ({
    enableDepthFilter: byId("vcf-qc-enable-depth").checked,
    minDepth: byId("vcf-qc-min-depth").value,
    enableAltCountFilter: byId("vcf-qc-enable-alt-count").checked,
    minAltCount: byId("vcf-qc-min-alt-count").value,
    enableAltFractionFilter: byId("vcf-qc-enable-alt-fraction").checked,
    minAltFraction: byId("vcf-qc-min-alt-fraction").value,
    enableAltBaseQualityFilter: byId("vcf-qc-enable-alt-base-quality").checked,
    minAltBaseQuality: byId("vcf-qc-min-alt-base-quality").value
  });

  const applyVcfQc = managedFileId => {
    const file = state.managedFiles.get(managedFileId);
    if (!file || file.fileType !== "vcf") throw new Error("VCF QC/filtering requires a managed VCF input.");
    byId("pipeline-id").value = "org.biocore.vcfqc.filter";
    byId("pipeline-version").value = "0.1.0";
    byId("job-bindings").value = JSON.stringify(core.buildVcfQcBindings(file.id, vcfQcSettings()), null, 2);
    setFormMessage(`VCF QC/filtering prepared with ${file.displayName}.`, "ready");
  };

  const applyVariantAnnotation = (variantsId, annotationsId) => {
    const variants = state.managedFiles.get(variantsId);
    const annotations = state.managedFiles.get(annotationsId);
    if (!variants || variants.fileType !== "vcf") throw new Error("Variant annotation requires a managed VCF input.");
    if (!annotations || annotations.fileType !== "tsv") throw new Error("Variant annotation requires a managed annotation TSV input.");
    byId("pipeline-id").value = "org.biocore.variantannotate.local";
    byId("pipeline-version").value = "0.1.0";
    byId("job-bindings").value = JSON.stringify(core.buildVariantAnnotationBindings(variants.id, annotations.id), null, 2);
    setFormMessage(`Local annotation prepared: ${variants.displayName} + ${annotations.displayName}.`, "ready");
  };


  const parseErrorMessage = async (response, fallback) => {
    try {
      const payload = await response.json();
      if (payload && payload.error && typeof payload.error.message === "string") {
        return payload.error.message;
      }
    } catch (_) {}
    return fallback;
  };

  const uploadManagedFile = async file => {
    const startResponse = await fetch("/api/v1/files/uploads", {
      method: "POST",
      cache: "no-store",
      credentials: "same-origin",
      headers: {
        "Accept": "application/json",
        "Content-Type": "application/json"
      },
      body: JSON.stringify({
        displayName: file.name,
        fileType: managedFileType.value,
        sizeBytes: file.size
      })
    });
    if (!startResponse.ok) {
      throw new Error(await parseErrorMessage(startResponse, `Upload start failed (${startResponse.status}).`));
    }
    const session = await startResponse.json();
    if (!session || !core.safePathAtom(session.uploadId) ||
        !Number.isInteger(session.maxChunkBytes) || session.maxChunkBytes <= 0) {
      throw new Error("Upload session response is invalid.");
    }

    let completed = false;
    try {
      let offset = 0;
      managedFileProgress.hidden = false;
      managedFileProgress.value = 0;
      while (offset < file.size) {
        const end = Math.min(file.size, offset + session.maxChunkBytes);
        const chunk = file.slice(offset, end);
        const response = await fetch(`/api/v1/files/uploads/${session.uploadId}/chunks`, {
          method: "POST",
          cache: "no-store",
          credentials: "same-origin",
          headers: {
            "Accept": "application/json",
            "Content-Type": "application/octet-stream",
            "X-BioCore-Upload-Offset": String(offset)
          },
          body: chunk
        });
        if (!response.ok) {
          throw new Error(await parseErrorMessage(response, `Upload chunk failed (${response.status}).`));
        }
        offset = end;
        const percent = file.size === 0 ? 100 : Math.round((offset / file.size) * 100);
        managedFileProgress.value = percent;
        managedFileProgressLabel.textContent = `${percent}% · ${core.formatBytes(offset)} / ${core.formatBytes(file.size)}`;
      }

      const completeResponse = await fetch(`/api/v1/files/uploads/${session.uploadId}/complete`, {
        method: "POST",
        cache: "no-store",
        credentials: "same-origin",
        headers: { "Accept": "application/json" }
      });
      if (!completeResponse.ok) {
        throw new Error(await parseErrorMessage(completeResponse, `Upload completion failed (${completeResponse.status}).`));
      }
      const managed = core.normalizeManagedFile(await completeResponse.json());
      if (managed === null) throw new Error("Managed file response is invalid.");
      completed = true;
      state.managedFiles.set(managed.id, managed);
      renderManagedFiles();
      managedFileSelect.value = managed.id;
      if (managed.fileType === "fastq") {
        if (!pairedRead1Select.value) pairedRead1Select.value = managed.id;
        else if (!pairedRead2Select.value && pairedRead1Select.value !== managed.id) pairedRead2Select.value = managed.id;
      }
      managedFileProgress.value = 100;
      managedFileProgressLabel.textContent = `100% · ${core.formatBytes(file.size)}`;
      return managed;
    } finally {
      if (!completed) {
        fetch(`/api/v1/files/uploads/${session.uploadId}/cancel`, {
          method: "POST",
          cache: "no-store",
          credentials: "same-origin",
          headers: { "Accept": "application/json" }
        }).catch(() => {});
      }
    }
  };

  const loadJobs = async () => {
    const response = await fetch("/api/v1/jobs", {
      method: "GET",
      cache: "no-store",
      credentials: "same-origin",
      headers: { "Accept": "application/json" }
    });
    if (response.status === 401) {
      state.authenticated = false;
      sessionGate.hidden = false;
      appShell.hidden = true;
      throw new Error("unauthorized");
    }
    if (!response.ok) throw new Error(`jobs-${response.status}`);
    const jobs = await response.json();
    core.replaceSnapshot(state, jobs);
    if (state.selectedJobId === null && state.jobs.size > 0) {
      state.selectedJobId = core.filterJobs(state.jobs, "all")[0].id;
    }
    renderJobs();
    renderDetail();
    return true;
  };

  const probeSession = async () => {
    try {
      await loadJobs();
      await loadManagedFiles();
      state.authenticated = true;
      sessionGate.hidden = true;
      appShell.hidden = false;
      setSessionMessage("Browser session established.", "ready");
      connectLifecycleSocket();
      if (state.selectedJobId !== null) refreshArtifacts(state.selectedJobId).catch(() => {});
      return true;
    } catch (error) {
      if (error.message === "unauthorized") {
        sessionGate.hidden = false;
        appShell.hidden = true;
        setSessionMessage("Enter the bootstrap token printed by the OpenGenesis-BioCore process.");
        return false;
      }
      throw error;
    }
  };

  const checkHealth = async () => {
    try {
      const response = await fetch("/api/v1/health", {
        method: "GET",
        cache: "no-store",
        credentials: "same-origin",
        headers: { "Accept": "application/json" }
      });
      if (!response.ok) {
        setCoreState("offline", "Local core unavailable", `Health check failed (${response.status})`);
        return;
      }
      const payload = await response.json();
      if (!payload || payload.status !== "healthy") {
        setCoreState("offline", "Unexpected core response", "Health response invalid");
        return;
      }
      setCoreState("online", "Local core online", "Core healthy");
      await probeSession();
    } catch (_) {
      setCoreState("offline", "Local core unavailable", "Health or session check unavailable");
    }
  };

  sessionForm.addEventListener("submit", async event => {
    event.preventDefault();
    let bootstrapToken = sessionInput.value;
    sessionInput.value = "";
    sessionButton.disabled = true;
    setSessionMessage("Establishing local browser session…");

    try {
      const response = await fetch("/api/v1/session", {
        method: "POST",
        cache: "no-store",
        credentials: "same-origin",
        headers: {
          "Accept": "application/json",
          "Content-Type": "application/json"
        },
        body: JSON.stringify({ bootstrapToken })
      });
      if (response.status === 401) {
        setSessionMessage("Bootstrap token was not accepted.", "error");
        return;
      }
      if (!response.ok) {
        setSessionMessage(`Session establishment failed (${response.status}).`, "error");
        return;
      }
      await probeSession();
    } catch (_) {
      setSessionMessage("Session establishment could not reach the local core.", "error");
    } finally {
      bootstrapToken = "";
      sessionButton.disabled = false;
    }
  });

  managedFileInput.addEventListener("change", () => {
    const file = managedFileInput.files && managedFileInput.files[0];
    if (!file) return;
    const inferredType = core.inferManagedFileType(file.name);
    if (inferredType !== null) {
      managedFileType.value = inferredType;
      setManagedFileMessage(`Detected ${inferredType.toUpperCase()} from ${file.name}.`);
    }
  });

  managedFileUpload.addEventListener("click", async () => {
    const file = managedFileInput.files && managedFileInput.files[0];
    if (!file) {
      setManagedFileMessage("Choose a local FASTA, FASTQ, SAM, or BAM file first.", "warning");
      return;
    }
    managedFileUpload.disabled = true;
    useFastaInput.disabled = true;
    useFastqInput.disabled = true;
    useAlignmentQcInput.disabled = true;
    usePairedFastqInput.disabled = true;
    useSingleFastqTrim.disabled = true;
    usePairedFastqTrim.disabled = true;
    useSingleAlignment.disabled = true;
    usePairedAlignment.disabled = true;
    useVariantCalling.disabled = true;
    managedFileProgress.hidden = false;
    managedFileProgress.value = 0;
    managedFileProgressLabel.textContent = `0% · 0 B / ${core.formatBytes(file.size)}`;
    setManagedFileMessage(`Importing ${file.name}…`);
    try {
      const managed = await uploadManagedFile(file);
      setManagedFileMessage(
        `${managed.displayName} imported as ${managed.id}.`, "ready"
      );
      applyManagedFile(managed.id);
    } catch (error) {
      setManagedFileMessage(
        error instanceof Error ? error.message : "File import failed.", "error"
      );
    } finally {
      managedFileUpload.disabled = false;
      useFastaInput.disabled = false;
      useFastqInput.disabled = false;
      useAlignmentQcInput.disabled = false;
      usePairedFastqInput.disabled = false;
      useSingleFastqTrim.disabled = false;
      usePairedFastqTrim.disabled = false;
      useSingleAlignment.disabled = false;
      usePairedAlignment.disabled = false;
      useVariantCalling.disabled = false;
    }
  });

  useFastaInput.addEventListener("click", () => {
    try {
      applyManagedFile(managedFileSelect.value, "fasta");
      byId("job-form").scrollIntoView({ behavior: "smooth", block: "nearest" });
    } catch (error) {
      setManagedFileMessage(
        error instanceof Error ? error.message : "Managed input selection failed.", "warning"
      );
    }
  });

  useFastqInput.addEventListener("click", () => {
    try {
      applyManagedFile(managedFileSelect.value, "fastq");
      byId("job-form").scrollIntoView({ behavior: "smooth", block: "nearest" });
    } catch (error) {
      setManagedFileMessage(
        error instanceof Error ? error.message : "Managed input selection failed.", "warning"
      );
    }
  });

  useAlignmentQcInput.addEventListener("click", () => {
    try {
      const file = state.managedFiles.get(managedFileSelect.value);
      if (!file || (file.fileType !== "sam" && file.fileType !== "bam")) {
        throw new Error("Alignment QC requires a managed SAM or BAM input.");
      }
      applyManagedFile(file.id);
      byId("job-form").scrollIntoView({ behavior: "smooth", block: "nearest" });
    } catch (error) {
      setManagedFileMessage(error instanceof Error ? error.message : "Alignment QC setup failed.", "warning");
    }
  });

  usePairedFastqInput.addEventListener("click", () => {
    try {
      applyPairedFastq(pairedRead1Select.value, pairedRead2Select.value);
      byId("job-form").scrollIntoView({ behavior: "smooth", block: "nearest" });
    } catch (error) {
      setManagedFileMessage(
        error instanceof Error ? error.message : "Paired FASTQ selection failed.", "warning"
      );
    }
  });

  useSingleFastqTrim.addEventListener("click", () => {
    try {
      applySingleFastqTrim(managedFileSelect.value);
      byId("job-form").scrollIntoView({ behavior: "smooth", block: "nearest" });
    } catch (error) {
      setManagedFileMessage(error instanceof Error ? error.message : "FASTQ trimming setup failed.", "warning");
    }
  });

  usePairedFastqTrim.addEventListener("click", () => {
    try {
      applyPairedFastqTrim(pairedRead1Select.value, pairedRead2Select.value);
      byId("job-form").scrollIntoView({ behavior: "smooth", block: "nearest" });
    } catch (error) {
      setManagedFileMessage(error instanceof Error ? error.message : "Paired FASTQ trimming setup failed.", "warning");
    }
  });

  useSingleAlignment.addEventListener("click", () => {
    try {
      applySingleAlignment(alignmentReferenceSelect.value, alignmentRead1Select.value);
      byId("job-form").scrollIntoView({ behavior: "smooth", block: "nearest" });
    } catch (error) {
      setManagedFileMessage(error instanceof Error ? error.message : "Single-end alignment setup failed.", "warning");
    }
  });

  usePairedAlignment.addEventListener("click", () => {
    try {
      applyPairedAlignment(
        alignmentReferenceSelect.value, alignmentRead1Select.value, alignmentRead2Select.value
      );
      byId("job-form").scrollIntoView({ behavior: "smooth", block: "nearest" });
    } catch (error) {
      setManagedFileMessage(error instanceof Error ? error.message : "Paired alignment setup failed.", "warning");
    }
  });


  useVariantCalling.addEventListener("click", () => {
    try {
      applyVariantCalling(variantReferenceSelect.value, variantAlignmentSelect.value);
      byId("job-form").scrollIntoView({ behavior: "smooth", block: "nearest" });
    } catch (error) {
      setManagedFileMessage(error instanceof Error ? error.message : "Variant-calling setup failed.", "warning");
    }
  });

  useVcfQc.addEventListener("click", () => {
    try {
      applyVcfQc(vcfQcSelect.value);
      byId("job-form").scrollIntoView({ behavior: "smooth", block: "nearest" });
    } catch (error) {
      setManagedFileMessage(error instanceof Error ? error.message : "VCF QC/filtering setup failed.", "warning");
    }
  });

  useVariantAnnotation.addEventListener("click", () => {
    try {
      applyVariantAnnotation(annotationVcfSelect.value, annotationTableSelect.value);
      byId("job-form").scrollIntoView({ behavior: "smooth", block: "nearest" });
    } catch (error) {
      setManagedFileMessage(error instanceof Error ? error.message : "Variant annotation setup failed.", "warning");
    }
  });

  wizardWorkflowSelect.addEventListener("change", () => {
    renderWizardFields(false);
  });

  const invalidateWizardReview = () => {
    if (!wizardReview.hidden) {
      wizardReview.hidden = true;
      wizardSubmitPrepared.disabled = true;
      setWizardMessage("Wizard selections changed. Review the prepared job again.", "warning");
    }
  };
  wizardFields.addEventListener("input", invalidateWizardReview);
  wizardFields.addEventListener("change", invalidateWizardReview);

  wizardManageInputs.addEventListener("click", () => {
    byId("file-import-panel").scrollIntoView({ behavior: "smooth", block: "center" });
  });

  wizardPrepareButton.addEventListener("click", () => {
    try {
      prepareWizardJob();
    } catch (error) {
      wizardReview.hidden = true;
      wizardSubmitPrepared.disabled = true;
      setWizardMessage(error instanceof Error ? error.message : "Analysis wizard preparation failed.", "warning");
    }
  });

  wizardSubmitPrepared.addEventListener("click", () => {
    if (wizardSubmitPrepared.disabled || wizardReview.hidden) return;
    jobForm.requestSubmit();
  });

  jobForm.addEventListener("submit", async event => {
    event.preventDefault();
    jobSubmit.disabled = true;
    setFormMessage("Submitting prepared job…");

    try {
      const payload = core.buildSubmitPayload({
        pipelineId: byId("pipeline-id").value,
        pipelineVersion: byId("pipeline-version").value,
        analysisId: byId("analysis-id").value,
        priority: byId("job-priority").value,
        bindingsText: byId("job-bindings").value
      });

      const response = await fetch("/api/v1/jobs", {
        method: "POST",
        cache: "no-store",
        credentials: "same-origin",
        headers: {
          "Accept": "application/json",
          "Content-Type": "application/json"
        },
        body: JSON.stringify(payload)
      });

      let responsePayload = null;
      try {
        responsePayload = await response.json();
      } catch (_) {
        responsePayload = null;
      }

      if (!response.ok) {
        const message = responsePayload &&
                        responsePayload.error &&
                        typeof responsePayload.error.message === "string"
          ? responsePayload.error.message
          : `Job submission failed (${response.status}).`;
        throw new Error(message);
      }

      const job = core.normalizeJob(responsePayload);
      if (job !== null) {
        state.jobs.set(job.id, job);
        state.selectedJobId = job.id;
        renderJobs();
        renderDetail();
      }
      setFormMessage("Job prepared and queued.", "ready");
      if (!wizardReview.hidden) {
        wizardSubmitPrepared.disabled = true;
        setWizardMessage("Prepared analysis submitted. Select the job results when execution completes.", "ready");
      }
      byId("analysis-id").value = "";
      byId("job-bindings").value = "";
      byId("detail-content").scrollIntoView({ behavior: "smooth", block: "nearest" });
    } catch (error) {
      setFormMessage(error instanceof Error ? error.message : "Job submission failed.", "error");
    } finally {
      jobSubmit.disabled = false;
    }
  });

  byId("cancel-job").addEventListener("click", async () => {
    const job = state.selectedJobId ? state.jobs.get(state.selectedJobId) : null;
    if (!job || !core.canCancelJob(job.status)) return;
    const button = byId("cancel-job");
    button.disabled = true;
    button.textContent = "Cancelling…";
    try {
      const response = await fetch(`/api/v1/jobs/${job.id}/cancel`, {
        method: "POST",
        cache: "no-store",
        credentials: "same-origin",
        headers: { "Accept": "application/json" }
      });
      let payload = null;
      try { payload = await response.json(); } catch (_) { payload = null; }
      if (!response.ok) {
        const message = payload && payload.error && typeof payload.error.message === "string"
          ? payload.error.message
          : `Job cancellation failed (${response.status}).`;
        throw new Error(message);
      }
      const updated = core.normalizeJob(payload);
      if (updated !== null) state.jobs.set(updated.id, updated);
      renderJobs();
      renderDetail();
      window.setTimeout(() => loadJobs().catch(() => {}), 300);
      window.setTimeout(() => loadJobs().catch(() => {}), 1500);
    } catch (error) {
      setFormMessage(error instanceof Error ? error.message : "Job cancellation failed.", "error");
      renderDetail();
    }
  });

  byId("verify-export").addEventListener("click", () => {
    if (state.selectedJobId !== null) verifyExport(state.selectedJobId).catch(() => {});
  });

  byId("retry-job").addEventListener("click", async () => {
    const job = state.selectedJobId ? state.jobs.get(state.selectedJobId) : null;
    if (!job || !core.canRetryJob(job.status)) return;
    const button = byId("retry-job");
    button.disabled = true;
    button.textContent = "Retrying…";
    try {
      const response = await fetch(`/api/v1/jobs/${job.id}/retry`, {
        method: "POST",
        cache: "no-store",
        credentials: "same-origin",
        headers: { "Accept": "application/json" }
      });
      let payload = null;
      try { payload = await response.json(); } catch (_) { payload = null; }
      if (!response.ok) {
        const message = payload && payload.error && typeof payload.error.message === "string"
          ? payload.error.message
          : `Job retry failed (${response.status}).`;
        throw new Error(message);
      }
      const updated = core.normalizeJob(payload);
      if (updated === null || updated.id !== job.id || updated.attemptNumber <= job.attemptNumber) {
        throw new Error("Retry response did not advance the job attempt.");
      }
      state.jobs.set(updated.id, updated);
      state.exportManifests.delete(updated.id);
      state.exportChecks.delete(updated.id);
      renderJobs();
      renderDetail();
      refreshArtifacts(updated.id).catch(() => {});
      window.setTimeout(() => loadJobs().catch(() => {}), 300);
    } catch (error) {
      state.exportChecks.set(job.id, {
        state: "error",
        message: error instanceof Error ? error.message : "Job retry failed."
      });
      renderDetail();
    }
  });

  byId("refresh-jobs").addEventListener("click", () => {
    loadJobs().catch(() => setFormMessage("Could not refresh jobs.", "warning"));
  });
  byId("refresh-artifacts").addEventListener("click", () => {
    if (state.selectedJobId !== null) refreshArtifacts(state.selectedJobId).catch(() => {});
  });
  byId("new-job-shortcut").addEventListener("click", () => {
    byId("submit-panel").scrollIntoView({ behavior: "smooth", block: "start" });
    byId("pipeline-id").focus();
  });

  for (const filter of document.querySelectorAll("[data-filter]")) {
    filter.addEventListener("click", () => {
      state.filter = filter.dataset.filter || "all";
      for (const current of document.querySelectorAll("[data-filter]")) {
        current.classList.toggle("active", current === filter);
      }
      renderJobs();
    });
  }

  for (const navigation of document.querySelectorAll("[data-view]")) {
    navigation.addEventListener("click", () => {
      const view = navigation.dataset.view;
      if (view === "wizard") {
        byId("analysis-wizard-panel").scrollIntoView({ behavior: "smooth", block: "start" });
      } else if (view === "submit") {
        byId("submit-panel").scrollIntoView({ behavior: "smooth", block: "start" });
      } else if (view === "results") {
        if (state.selectedJobId !== null) {
          byId("artifact-list").scrollIntoView({ behavior: "smooth", block: "center" });
        } else {
          byId("job-list").scrollIntoView({ behavior: "smooth", block: "center" });
          setFormMessage("Select a job to open its grouped results.");
        }
      } else if (view === "files") {
        byId("file-import-panel").scrollIntoView({ behavior: "smooth", block: "center" });
      } else if (view === "jobs") {
        byId("job-list").scrollIntoView({ behavior: "smooth", block: "center" });
      } else {
        window.scrollTo({ top: document.querySelector(".workspace-shell").offsetTop, behavior: "smooth" });
      }
    });
  }

  renderWizardFields(false);
  checkHealth();
})();
