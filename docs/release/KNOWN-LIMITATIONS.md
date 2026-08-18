# OpenGenesis-BioCore v0.1.0 Known Limitations

- Native plugins are trusted local executables; signatures, publisher trust and sandboxing are not yet
  implemented.
- Browser uploads are sequential and in-process; they are not resumable across a Core restart and do not
  support parallel/out-of-order chunks.
- Current biological workflows include FASTA/FASTQ QC, paired-end QC, adapter/quality trimming, native
  ungapped alignment, SAM/BAM alignment QC with coverage/depth, native SNV calling, VCF QC/filtering,
  and local variant annotation/reporting.
- The Iteration 034 native aligner is an exhaustive ungapped/Hamming implementation intended as a
  correctness and SAM-contract foundation. It does not yet support indels/gapped alignment, affine-gap
  scoring, splice-aware alignment, or whole-human-genome-scale indexed performance. Reference FASTA is
  currently bounded to 512 MiB / 500,000,000 bases for this native backend.
- Managed browser inputs do not yet receive a new checksum/backfill policy at import time; generated
  outputs retain the existing SHA-256 integrity path.
- Interrupted Jobs are conservatively recovered; automatic native-session adoption/resume is not
  implemented.
- Full filesystem TOCTOU elimination and explicit fsync/FlushFileBuffers guarantees are not claimed.
- The supported network surface is localhost only; remote serving/TLS is not a v0.1.0 feature.

## Alignment QC (Iterations 035-036)

- Native Alignment QC accepts plain SAM and BGZF/gzip BAM and reports primary-record mapping metrics, MAPQ distribution, NM mismatch statistics, paired flags, primary mapped counts, exact CIGAR-derived coverage/depth summaries, and proper-pair positive-TLEN statistics.
- Mapping rate is intentionally computed from primary records only; secondary and supplementary records are reported separately and do not inflate the denominator.
- BAM parsing is streaming after gzip/BGZF inflation and does not require htslib, but this iteration does not create, sort, index, or rewrite BAM files.
- Iteration 036 coverage uses primary mapped CIGAR blocks (`M`, `=`, `X`) with sweep-line depth aggregation over declared reference lengths; deletions/skips advance the reference but do not count as covered bases. Secondary/supplementary records do not contribute to coverage.
- Template-length statistics use only positive TLEN values from primary, mapped, same-contig records flagged as proper pairs, preventing the usual signed TLEN double count.
- Target/interval-specific coverage, GC-bias analysis, fragment-overlap de-duplication, duplicate marking, BAM sort/index/write, and CRAM parsing remain outside scope. Existing duplicate/QC-fail flags are counted when present.

## Iteration 037 native SNV calling boundary

The native `org.biocore.variantcall.snv` caller is a deterministic **SNV-only pileup foundation**. It
uses an explicit reference FASTA plus SAM/BAM alignments and calls canonical A/C/G/T substitutions
from primary mapped, non-duplicate, non-QC-fail records that pass configurable MAPQ and base-quality
thresholds. It emits sites-only VCFv4.3 plus JSON/TSV summaries. VCF QUAL is deliberately `.` because
Iteration 037 does not implement a calibrated probabilistic genotype/variant quality model.

Outside Iteration 037 scope: indel calling, local reassembly, haplotype-aware calling, genotype/sample
FORMAT fields, ploidy modeling, strand-bias/error-model statistics, overlapping-mate de-duplication,
base-quality recalibration, duplicate marking, multi-sample calling, VCF normalization/left-alignment,
and indexed whole-genome pileup performance. Those are future extensions and are not implied by the
`native-snv-pileup-v1` contract.


## Iteration 038 VCF QC / filtering and annotation foundation

- VCF QC/filtering currently accepts plain-text VCFv4.2 or VCFv4.3. bgzip-compressed VCF/BCF is not yet parsed.
- Threshold filtering requires INFO `DP`, `AC`, `AF`, and `ABQ`; missing required metrics are explicitly soft-filtered rather than assumed to pass.
- Multiallelic `AC`, `AF`, and `ABQ` values must have one value per ALT allele. A record is `PASS` only when every ALT allele passes and the input record was already `PASS`.
- Existing FILTER identifiers are preserved. Input `FILTER=.` is surfaced as `BioCoreUnfiltered`; records are never silently promoted to PASS.
- The annotation-ready TSV emits a stable raw key `CHROM:POS:REF:ALT`, but Iteration 038 performs **no** left-alignment, minimal representation, reference normalization, rsID lookup, transcript consequence prediction, population-frequency lookup, or clinical annotation. The metadata explicitly reports `Normalization=none`.
- Length-based `insertion_like`/`deletion_like` classifications are descriptive only until a later normalization/annotation layer is introduced.

## Iteration 039 local variant annotation

- Variant annotation is deliberately local-only and does not contact ClinVar, Ensembl, VEP, or any remote service.
- The annotation source is a managed TSV with the exact header `key`, `gene`, `consequence`, `clinical_significance`, `source`, `source_id` (tab-separated).
- Join identity is the raw Iteration 038 key `CHROM:POS:REF:ALT`; normalization remains `none`. Left-alignment/normalization and transcript-aware consequence generation are outside Iteration 039.
- One annotation row per raw key is accepted; duplicate annotation keys fail closed. Multi-source aggregation must be performed before import or by a later dedicated annotation-dataset layer.
- The HTML report is a local static artifact and renders at most 500 annotation-hit rows; full data remains available in annotated VCF and TSV artifacts.

## Iteration 040 guided analysis / results navigation

- The browser analysis wizard is a guided **single-job preparation layer** over already registered OpenGenesis-BioCore pipelines. It does not create a new server-side workflow engine, auto-chain multiple jobs, or infer hidden file relationships.
- The wizard exposes exact managed-file choices and supported parameters, then writes the same pipeline ID/version and `PipelineRunBindings` used by the advanced exact-submission form. Core remains authoritative for typed binding and plugin validation.
- Result navigation groups already-registered artifacts into Reports, Primary results, Summaries & tables, and Other artifacts. The categories are browser presentation only and do not change artifact persistence, checksums, provenance, or download routes.
- HTML artifacts can be opened directly from the grouped result list; all other artifact types continue to use the existing authenticated local download route.
## Iteration 041 runtime resilience / cancellation

- Long-running native plugin execution now emits explicit worker heartbeats while a plugin process is running; log or artifact traffic is deliberately not treated as liveness.
- Job cancellation is available for cancellable states through the authenticated localhost API and browser. Running work is cancelled by terminating the native worker process and then finalizing the job as `cancelled`; cancellation is not a cooperative plugin cleanup callback.
- Supervisor stdout/stderr draining and runtime background observation retention are bounded to prevent one noisy worker or an undrained observer from growing the Core control plane without bound. Dropped historical background observations are surfaced by synthetic retention notices.
- Iteration 041 does **not** add OS-level CPU, RAM, wall-clock, or disk quotas for plugin children, does not guarantee output-space preflight, and does not add checkpoint/resume. Native plugins remain trusted local executables subject to the existing project-local output and cleanup contracts.

