# OpenGenesis-BioCore Analysis Wizard and Results Navigation

Iteration 040 adds a browser-only guided layer over the existing exact pipeline submission contract.

## Invariants

- The wizard never invents a server-side pipeline or bypasses Core validation.
- Every preparation resolves to an existing exact pipeline ID and version (`0.1.0`).
- Inputs are explicit managed-file IDs selected by the user and filtered by the declared OpenGenesis-BioCore file type.
- Parameters use the same client-side validation/binding builders already exercised by direct shortcuts.
- The prepared bindings JSON is displayed before submission and copied into the existing exact submission form.
- `Submit prepared job` invokes the same `/api/v1/jobs` path and therefore the same immutable execution-plan preparation flow.
- Changing wizard selections invalidates the prior review until the user prepares again.

## Guided analyses

The wizard covers FASTA QC, FASTQ QC, paired FASTQ QC, single/paired trimming, single/paired alignment,
SAM/BAM alignment QC, native SNV calling, VCF QC/filtering, and local variant annotation/reporting.

## Results navigation

Generated artifacts remain unchanged in persistence. The browser groups them for navigation only:

1. Reports — HTML/report outputs.
2. Primary results — FASTA/FASTQ/SAM/BAM/VCF or known primary output ports.
3. Summaries & tables — JSON/TSV summary/table outputs.
4. Other artifacts — anything outside the presentation categories above.

The grouping is not a biological classification and does not alter checksums, provenance, or download routes.
