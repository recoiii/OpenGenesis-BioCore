# Sample Registry v1 — Iteration 080

Baseline: `accepted/iteration-079` / `23a51bf32388965c558f9d909efc357398975579`.

## Scope

Iteration 080 adds a project-local sample registry and bounded CSV/TSV interchange.
It does not bind FASTQ/BAM/VCF inputs, create workflow runs, expand DAGs or implement
batch execution. Those remain Iterations 081–084.

Each registry row contains only:
- `sample_id` — required opaque UTF-8 text, 1–128 bytes.
- `display_name` — optional UTF-8 text, 0–255 bytes.
- `group` — optional UTF-8 text, 0–128 bytes.

IDs are not numeric identifiers. Values such as `001` and `0007` are preserved
byte-for-byte. Comparisons use SQLite BINARY semantics; no case folding or trimming
is applied. NUL bytes and whitespace-only required IDs are rejected.

## Ownership and persistence

Schema v11 adds `project_samples` to the existing project-local database. Each row
carries the canonical `project_id` and has a foreign key to the singleton
`project_metadata.project_id`. The primary key is `(project_id, sample_id)`.
The global catalogue and Iteration 079 research metadata remain unchanged.

`SqliteProjectSampleStore::add_batch` acquires one `BEGIN IMMEDIATE` transaction,
verifies the explicit project owner, checks duplicates and inserts the complete batch.
A duplicate, wrong project or SQLite failure cannot leave a partial import.

## Preview-before-write contract

`SampleRegistryImportService::preview` parses the complete table without writing.
The required header is exactly `sample_id,display_name,group` for CSV or the same
three fields separated by tabs for TSV. Diagnostics carry the physical input line
and a stable code. Duplicate IDs in the table, IDs already present in the registry,
invalid field bounds, malformed quoting and wrong column counts are reported before
commit.

Only a valid `SampleImportPreview` can be passed to `commit`. Persistence still
rechecks uniqueness inside the write transaction, so a stale preview fails atomically.

CSV/TSV fields support delimiter quoting and doubled quotes. Embedded CR/LF inside a
single quoted field are intentionally not supported in v1; records are physical lines.
Blank physical lines are ignored.

## Export

Export uses the same three-column schema and deterministic binary sample-id ordering.
Empty metadata, Unicode, delimiters and quotes round-trip without numeric coercion.
This is registry metadata interchange, not a portable project archive.

## Forward boundary

Iteration 081 may reference these sample identities when binding project-local managed
files and reference context. Iteration 082 must snapshot selected sample metadata into
its immutable batch plan rather than depending on later mutable registry state.
