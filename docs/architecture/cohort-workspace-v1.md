# Cohort Analysis Workspace v1 — 089 contract

Status: accepted 089 contract; Iterations 090–091 are frozen, Iteration 092 matrix construction/dispatch is candidate. Version: 1.
All paths are relative to the repository. Current behavior is distinct from
required future cohort integration. Existing scientific engines remain unchanged.

## 1. Source map

| Responsibility | Existing source and symbol | Reuse / required addition |
|---|---|---|
| Project ownership | `src/application/source/project_service.cpp`, `ProjectService`; `src/infrastructure/source/sqlite/sqlite_project_repository.cpp`, `SqliteProjectRepository` | Reuse project identity and guards; no cross-project cohorts. |
| Samples | `src/domain/include/biocore/domain/project_sample.hpp`, `ProjectSample`; `src/application/source/sample_registry_import.cpp`, `SampleRegistryImportService`; `src/infrastructure/source/sqlite/sqlite_project_sample_store.cpp`, `SqliteProjectSampleStore` | Reuse exact string IDs and atomic import. Free-text `group_label` is not automatically a phenotype. 090 adds explicit cohort groups and revisions. |
| Input binding | `src/application/source/sample_binding_service.cpp`, `SampleBindingService`; `src/domain/include/biocore/domain/project_sample_binding.hpp`, `ProjectSampleBinding` | Reuse managed file ownership/reference evidence; 091 pins output selection, not just input binding. |
| Frozen batch plans | `src/application/source/batch_planning_service.cpp`, `BatchPlanningService`; `src/application/include/biocore/application/batch_plan.hpp`, `ApprovedBatchPlan` | Reuse snapshot/preview discipline; a batch plan is not a cohort analysis snapshot. |
| Output lineage/QC | `src/application/source/batch_results_service.cpp`, `BatchResultsService::overview`, `preview_variant_matrix`, `build_variant_matrix` | Existing preview resolves latest attempts; build supports exactly one matching VCF sample per source. Do not call this live resolver when executing a frozen cohort. Reuse artifact/QC contracts; add explicit selection adapter in 091–092. |
| Verified artifact reads | `src/application/include/biocore/application/i_result_artifact_reader.hpp`, `IResultArtifactReader::read_verified_text` | Reuse path/size/hash validation; missing checksum is not verified. Pin and reverify at approval and execution. |
| VCF/reference | `src/domain/source/vcf_ingestion.cpp`, `ingest_vcf`; `src/domain/source/vcf_ingestion_record.cpp`, normalization; `src/domain/include/biocore/domain/reference_genome.hpp`, `ReferenceGenome` | Reuse parser and normalization. VCF header model does not retain assembly provenance; supplied assembly enum alone is insufficient proof. 091 requires verified reference manifest. |
| Matrix | `src/domain/source/multi_sample_matrix_vcf.cpp`, `build_multi_sample_matrix`; `src/domain/source/multi_sample_matrix.cpp`, `MultiSampleMatrix` | Reuse exact normalized allele union, sorted sample IDs and sparse indices. 092 projects selected VCF columns into project IDs before calling engine. |
| Association | `src/domain/source/case_control_association.cpp`, `analyze_case_control`, `fisher_exact_two_sided`, `association_odds_ratio`, `association_odds_ratio_woolf_ci95`, `benjamini_hochberg_adjust` | Reuse unchanged; 094 supplies frozen labels/matrix and reports effective denominators. |
| Variant views/annotation | `src/domain/source/variant_workspace.cpp`, `VariantAnalysisWorkspace`; `src/domain/source/variant_annotation.cpp`, `annotate_variant`; `src/application/include/biocore/application/variant_export.hpp`, `build_variant_export_plan` | Reuse query/export and annotation primitives; 096 adds cohort result adapter and frozen provenance. Existing variant workspace is not a cohort results store. |
| Scheduling/process | `src/application/source/job_scheduler.cpp`, `JobScheduler::tick`; `src/application/source/worker_runtime.cpp`, `WorkerRuntime::run_cycle`; `src/application/source/job_service.cpp`, `JobService` | Reuse queued jobs, worker supervision, cancellation and artifact ingestion. Domain matrix/association currently have no cohort job dispatch. Add a thin native plugin adapter by 092, not a second engine or HTTP-thread execution. |
| Recovery | `src/application/source/batch_recovery_service.cpp`, `BatchRecoveryService`; `src/application/source/workflow_state_recovery_service.cpp`, `WorkflowStateRecoveryService`; `src/application/source/workflow_resume_planner.cpp`, `WorkflowResumePlanner` | Reuse state/verification primitives, not batch-specific membership assumptions. 095 hardens cohort attempt lineage. |
| Persistence | `src/infrastructure/source/sqlite/project_migration_runner.cpp`, `ProjectMigrationRunner::apply_pending`; `src/infrastructure/include/biocore/infrastructure/sqlite/project_migration_runner.hpp`, `latest_project_schema_version = 16` | 090 adds v16 registry/revision tables and immutable membership snapshots; later additive migrations remain owned by their iterations. |
| UI/API | `src/application/source/project_workspace_integration_service.cpp`, `ProjectWorkspaceIntegrationService`; `src/presentation/source/project_workspace_json.cpp`; `src/presentation/source/drogon_local_web_server.cpp`; `frontend/` | Extend existing project workspace and request/response pipeline in 097. 089 defines routes only; no callable cohort API yet. |
| Reports | `src/application/source/batch_result_package_service.cpp`, `BatchResultPackageService`; `src/presentation/source/batch_result_package_report.cpp` | Reuse verified packaging/escaping patterns; don't regenerate existing batch report feature. 096 adds cohort content. |

## 2. Scientific input and method contract

**Input.** Plain UTF-8 VCF 4.2/4.3/4.4/4.5 accepted by `ingest_vcf`, selected
explicitly from completed, registered outputs in the current project. gzip/BCF,
gVCF reference blocks, symbolic/SV/breakend alleles, automatic liftover and automatic
downloads are outside cohort v1. The generic parser may accept more representations;
the cohort gate is deliberately narrower: normalized literal SNVs and small indels
(absolute allele-length difference 1..50 bases; complex substitutions excluded),
with diploid GT (phased or unphased). Missing GT, `./.` and partial `0/.` remain
distinct missing states. Haploid/polyploid calls must produce a blocking eligibility
issue before cohort approval; the generic matrix can represent these and is unchanged.
Missingness does not cause an implicit genotype call or a reference dosage.

Single- and multi-sample VCFs are supported by the planned adapter, using an explicit
one-to-one map `(artifact_id, VCF sample name) -> project_sample_id`. Selected columns
are copied/projected before matrix construction; unselected columns are not counted.
A shared VCF is parsed once. Same sample/individual repeated via two attempts is an
error; explicit biological-unit metadata must identify related technical records.
No automatic matching based on display name, case folding, trimming or numeric
conversion. Leading zeros and Unicode bytes survive; project sample ID limit is
128 bytes. Conflicting mapping or absence of the selected column blocks approval.

**Reference proof.** Pin assembly identity (including custom identity), FASTA managed
file ID/hash/size, canonical contig names/lengths and alias mapping, normalization
contract version, and the producing workflow's reference identity/hash. Require
matching reference evidence across every selected output; lack of evidence blocks.
An enum or `chr1 -> 1` alias does not prove identical assembly. Reverify actual
reference bytes as well as VCF bytes. Normalization uses the same pinned FASTA;
duplicate/conflicting normalized sample/allele observations are rejected by the
existing engine. Ordering is canonical by target contig table, locus and allele,
not input order; use a deterministic pinned target contig table.

**Association.** Existing engine returns BOTH models, with rows cases then controls:

| Model | Exposed | Unexposed | Method |
|---|---|---|---|
| Allele | Selected ALT count | REF plus other ALT counts | Probability-ordered two-sided Fisher exact on 2x2 table; OR = ad/bc |
| Carrier | Complete-call individuals with selected ALT dosage > 0 | Complete-call individuals with selected ALT dosage 0 | Same Fisher and OR |

At multiallelic loci, other ALT belongs to the non-selected category; this is not
silently reported as REF. Complete calls only enter tables. Unobserved, no-call and
partial-call counts remain separate per group; `total = unobserved + no_call +
partial_call + complete_call`. No-call/partial/absent never enter as hom-ref.
Labels must cover every included matrix sample exactly once; both groups must be
nonempty. Set minimum complete calls explicitly (default one in each group).
Rows failing this minimum have null p/q, not zero. Engine OR/CI may still be computed;
UI marks such rows non-testable rather than inventing significance.

Woolf log-OR 95% CI is available only with all four cells positive and finite bounds.
Zero cells retain OR kind `zero`, `positive_infinity` or `undefined`; no 0.5 correction.
JSON uses `{kind, value}` with null value for undefined/infinite states, never NaN/Inf.
BH correction is performed separately across non-null allele p values and non-null
carrier p values in the frozen tested matrix. It is not a joint family across models.
Store each family's size, tested allele keys and inclusion filter version. Display
filtering/pagination never recomputes p/q. No regression, covariates, ancestry,
relatedness correction, causal claim or clinical classification is added.

## 3. Persistent identities and transaction boundaries (090 onward)

`CohortDefinition`: project_id, cohort_id, current_revision, name, created_at.
`CohortRevision`: immutable (cohort_id, revision), parent_revision, creation time,
ordered member records and explicit inclusion/exclusion reasons. Each member pins
sample ID, display metadata copy, biological_unit_id and `case|control|unassigned`.
Missing group is `unassigned`; it is never default-constructed as control.
An unassigned member can exist in a draft but cannot enter an association snapshot.
Excluded members retain their reason and original label; they do not enter matrix.

`CohortAnalysisSnapshot`: immutable analysis_id, project_id, cohort_id/revision,
contract_version, canonical digest, sample metadata/group/inclusion copies, explicit
artifact/run/job/attempt/step/output-port/VCF-column mappings, file hashes/sizes,
reference proof, normalized allele contract, QC decisions, method version/options,
resource limits, test universe/family metadata, approved_at. Generated result
metadata extends provenance; it must never overwrite approval contents. Later
cohort/sample edits or newly successful batch attempts cannot change this snapshot.

Approval transaction rechecks project, revision, sample ownership, artifact
ownership/status, selected columns and integrity; writes the whole snapshot or
nothing. Stale preview/revision yields conflict and requires a fresh preview.
The approval digest is SHA-256 of a versioned canonical serialization: lexically
ordered UTF-8 keys, no insignificant whitespace, fixed array ordering, exact integer
representation; no locale or floating-point formatting dependence. All parameter
values have typed representations. Do not hash pretty-printed UI JSON.

Migration plan: 090 v15 -> v16 adds cohort registry, immutable revisions/members,
project/cohort/sample foreign keys, unique (cohort,revision,sample), checked group
tokens, revision optimistic concurrency. Later snapshot and attempt migrations are
separate additive versions and must not be pre-applied in 089. Use existing migration
transactions and schema guard; do not change historical migrations. Old-project copy
tests cover v9→current and v15→v16, failure injection, rollback/reopen, foreign-key
integrity and retained jobs/batches/reports. No downgrade guarantee. Mutable sample
deletion must be restricted or tombstoned when referenced; never cascade immutable
analysis history away. Entire cohort revision insert/update is atomic.

**Iteration 090 implementation.** Schema v16 now persists `cohort_definitions`,
`cohort_revisions` and `cohort_revision_members`. Revisions are sealed before the
optimistic `current_revision` advance; member rows copy display/group metadata at
revision creation and keep exact sample IDs, biological-unit IDs, explicit
`case|control|unassigned`, disposition and exclusion reason. Database foreign keys
and triggers reject cross-project/nonexistent samples, mutation or deletion of sealed
history, and deletion of referenced project samples. The application service rejects
duplicate sample IDs, stale revisions and invalid exclusion semantics before commit.
Artifact/run/attempt selection, analysis snapshots, execution and result APIs remain
091+ scope; 090 does not resolve a "latest" artifact or run scientific computation.

The route ownership table below names the domain/application operation introduced by
an iteration. Wiring these operations into the existing Drogon project-workspace HTTP
surface and the browser UI remains the integrated workspace work assigned to 097.


**Iteration 091 implementation.** Explicit selection preview now requires a frozen
cohort revision and a one-to-one project-sample mapping to a concrete
`plan_id + attempt_number + job_id + step_id + output_port + managed_file_id +
VCF sample name + expected SHA-256`. It never resolves "latest" implicitly.
Selections are accepted only from completed Jobs and frozen batch-attempt node scopes;
the generated-output module/plugin/file-type contract is rechecked against the frozen
batch plan. Shared multi-sample VCFs are verified/read once and columns are mapped by
exact UTF-8 header name without trimming, case folding or numeric coercion.

Reference admission pins assembly identity, optional custom assembly identity,
normalization-contract version, exact FASTA managed-file ID/size/SHA-256, canonical
contig names/lengths derived from verified FASTA bytes, and explicit alias mappings.
Every producing batch sample must carry the same reference file snapshot. FASTA bytes
are verified before and after streaming manifest extraction; VCF bytes are verified
through the existing result-artifact reader. Haploid/polyploid GT values block cohort
v1 admission. Resource admission enforces <=100 included samples, <=100 sources,
<=64 MiB per VCF, <=256 MiB combined selected VCF text and <=512 MiB reference FASTA.
The 10,000 normalized-allele and 1,000,000 observation matrix caps remain owned by
092 because 091 deliberately does not normalize or construct the matrix.

No analysis snapshot is persisted in 091. QC inclusion approval, group-denominator
checks and immutable analysis approval remain 093 scope; matrix construction/dispatch
remain 092 scope. HTTP/browser wiring remains 097.


**Iteration 092 implementation.** Matrix construction re-runs the accepted 091
selection preview, reloads the pinned FASTA from verified managed bytes and rechecks
its SHA-256/size before scientific work. Each selected VCF artifact is verified and
parsed exactly once, including shared multi-sample VCFs. Only explicitly selected
VCF columns are projected into one-sample ingestion views and renamed to the exact
project sample ID before the retained `build_multi_sample_matrix` engine is called.
Unselected VCF columns never enter matrix denominators.

The adapter reuses `ingest_vcf` normalization and the retained sparse matrix engine.
A deterministic target contig table is derived from the reverified reference and the
pinned explicit alias map. Cohort v1 then rejects symbolic/SV alleles, MNV/complex
substitutions and indels whose normalized inserted/deleted length exceeds 50 bases.
Matrix admission fixes the initial integration envelope at <=100 samples, <=100
selected source artifacts, <=10,000 normalized alleles and <=1,000,000 observations.
Sparse absence remains absence: a sample with no record at another source's locus has
no matrix observation and is not converted to hom-ref/no-call.

092 also materializes a deterministic matrix-stage dispatch descriptor with native
module identity `org.biocore.cohort.matrix`, pinned reference hash, selected
artifact/hash/column mappings and resource ceilings. It does not invent a second
scheduler or dynamic plugin-path bundle. Durable attempt reservation, scheduler
handoff, cancellation/retry and crash reconciliation remain 095 scope, where the
existing JobScheduler/worker protocol is hardened around the fixed stage descriptor.
QC approval and immutable analysis snapshot persistence remain 093; association
execution remains 094. Project schema therefore remains v16 in 092.

## 4. Execution and retry contract (dispatch in 092, hardening in 095)

Approval creates `approved`, no computation. Submit uses a durable idempotency key
scoped to project+analysis+operation and payload digest. Same key/digest returns the
same logical analysis/attempt; different digest returns conflict. A new submit key
cannot create a second active initial attempt for the same analysis.

State sequence: approved -> queued -> running -> completed|failed|cancelled;
queued/running may become interrupted on reconciliation. Cancellation requests are
persisted separately until worker termination is observed. Completed is written only
after output registration/hash verification and durable result-manifest commit.
A partial output or merely exited process is not completion. Retain failed outputs
as diagnostics or quarantine according to existing cleanup rules, never as results.

Persist attempt reservation and execution-plan identity before scheduler handoff;
reconcile a crash between persistence and queueing by stable job/attempt ID. This
is the same durable-reservation discipline used by batch execution. Retry creates
a new attempt_id/job_id and parent_attempt_id, reusing the same immutable input
snapshot. Only failed/interrupted/cancelled attempts can retry. Never overwrite
successful artifacts; concurrent retry requests resolve one reserved child.

Use a thin native cohort plugin with the existing worker/plugin protocol and
JobScheduler, invoking retained domain matrix/QC/association functions. 092 adds
the matrix dispatch path; 093/094 extend steps on the same fixed execution plan.
No dynamic batch fan-in, new scheduler, or temporary synchronous HTTP execution.
Missing domain cancellation callbacks do not mean cancellation is available inside
a Fisher loop: WorkerRuntime cancellation/termination bounds the process lifetime;
integration must measure cancellation latency before 095 acceptance. Resume only
at verified step boundaries using existing checkpoints; restarting an interrupted
matrix step is allowed. Reuse requires the same snapshot/method/input hashes.
Changed bytes block execution/retry; a new selection requires a new analysis ID.

## 5. API/UI draft (not active routes)

Use the existing `/api/v1` project-context API and JSON error envelope. Every request
must validate explicit project ID against the currently opened project. Names below
are new endpoints, not claims about routes present in 089.

| Method + proposed route under `/api/v1/projects/{project_id}` | Payload / result | Owner |
|---|---|---|
| POST `/cohorts` | name, members with explicit labels; returns cohort/revision | 090 |
| POST `/cohorts/{id}/revisions` | expected_revision + full member edit; conflict on stale revision | 090 |
| GET `/cohorts/{id}` | current revision or explicit revision; immutable member view | 090 |
| POST `/cohorts/{id}/analysis-previews` | revision + explicit artifacts/reference/mapping/options; issues and preview digest | 091–093 |
| POST `/cohorts/{id}/analyses` | approved preview digest; revalidation; immutable snapshot | 093 |
| POST `/analyses/{id}/submit` | idempotency key + snapshot digest; stable job/attempt | 092–095 |
| GET `/analyses/{id}` | snapshot, state, attempts, progress and errors | 095 |
| POST `/analyses/{id}/cancel` or `/retry` | expected attempt + idempotency key | 095 |
| GET `/analyses/{id}/results` | snapshot-bound cursor, filters, limit 1..100; totals/denominators | 096 |
| POST `/analyses/{id}/reports` | completed attempt + display filter; verified JSON/HTML/tabular output | 096 |

Validation returns stable issue codes plus sample/artifact/field context. Examples:
`duplicate_sample`, `wrong_project`, `unassigned_group`, `mapping_conflict`,
`reference_unverified`, `artifact_changed`, `unsupported_ploidy`, `limit_exceeded`,
`stale_revision`, `idempotency_conflict`. Validation failures are 400, missing IDs
404, stale/conflicting state 409, size limits 413; unexpected I/O failures do not
produce successful partial snapshots. Existing local-origin/path protections apply.

UI: project -> cohorts list -> members/groups -> completed artifact/column selection
-> compatibility issues -> matrix/QC preview -> inclusion approval -> execution and
attempt history -> paged results/report. A human sees per-group before/after counts,
all exclusions, reference and model definitions before approval. Missing metrics
display unavailable with a reason; they are not zero. Historical analysis views read
snapshot metadata. Results show stable allele key, groups, effective counts, OR kind,
CI availability, p/q and two BH family sizes. Escape HTML and spreadsheet formula
prefixes using existing export patterns. No patient data in CI/demo fixtures.

## 6. Resource contract and measurement

Existing engine guards are **ceilings, not tested capacity claims**: matrix defaults
4096 sources, 100000 samples, 10000000 alleles, 100000000 observations; association
100000 labels, 10000000 variants, 1000000 Fisher states. Batch VCF read cap is 512 MiB
per file. Parser materializes records and matrix builds in memory; no streaming
cohort matrix exists. Do not multiply these maxima into an advertised supported size.

Cohort v1 initial integration budget (enforce in 091–092 before committing work):
100 samples, 100 sources, 10000 normalized alleles, 1000000 observed sample/allele
cells, 64 MiB per selected VCF, 256 MiB combined VCF text, 512 MiB reference FASTA,
100000 Fisher table states. These are independent limits; also cap total resident
worker memory at 2 GiB and wall time at 120 s via the existing process supervisor
integration. If native enforcement cannot be provided, fail the relevant integration
gate or explicitly revise the budget; do not claim the limit is enforced in 089.
Total report budget 64 MiB; result response at most 100 rows/1 MiB; target local
paged response <=500 ms at this envelope. UI timing, worker caps and report bytes
are future acceptance targets, not benchmark results from 089.

089 benchmark matrix varies one factor at a time: (20,1000,100%),
(100,1000,100%), (20,10000,100%), (100,10000,10%), (100,10000,100%).
Each sample has its own sparse source; empty record means absent, not no-call.
Record CPU/platform/compiler, peak process RSS, matrix build/association milliseconds,
observation count and emitted summary bytes. Do not extrapolate from these synthetic
runs to whole genomes. Measured outputs are retained in same-candidate CI artifacts.
Engine scale gate: <=2 GiB RSS and <=120 s per run on the recorded runner.

## 7. Verification ownership

089 executes parser→matrix→association fixture tests and existing regressions.
Registry duplicate/wrong-project/Unicode/revision atomicity: 090. Explicit multi-VCF
mapping, same sample two attempts, hash/reference mismatch, unsupported ploidy,
resource admission: 091. Canonical matrix/absence/order and dispatch: 092. Empty
cohort/all-excluded/missing QC/group denominators: 093. Fixed hand-counted tables,
zero cells, test family, frozen labels: 094. Duplicate submit, crash/retry/cancel,
disk full and recovery: 095. Injection, pagination and unchanged historical reports:
096. UI flow and old project E2E: 097. Same-source native Windows/Linux, migration
failure, install/package smoke and Gemini+Claude final review: 098.

Existing regression anchors: `tests/multi_sample_matrix*_tests.cpp`,
`tests/case_control_*tests.cpp`, `tests/batch_results_tests.cpp`,
`tests/batch_recovery_tests.cpp`, `tests/project_workspace_v05_e2e_tests.cpp`,
`tests/v04_release_compatibility_tests.cpp`. The v0.6 CI runs all registered tests
in GCC Debug/Release, Clang Debug and GCC ASan+UBSan; native Windows remains a
separate required final closure gate. No new runtime feature is marked tested in 089.
