# OpenGenesis-BioCore

[![DOI](https://zenodo.org/badge/DOI/10.5281/zenodo.22012037.svg)](https://doi.org/10.5281/zenodo.22012037)

OpenGenesis-BioCore is a local-first C++20 bioinformatics platform with a localhost browser UI, native worker
processes, SQLite project persistence, plugin/pipeline execution, live job telemetry, durable artifacts,
and an integrated FASTA/FASTQ-to-variant-analysis workflow suite.

**Current stable release: v0.3.0**

## Highlights

- Project workspace initialization through `biocore --init-project`.
- Loopback-only local web server and browser frontend with local authenticated sessions.
- Managed-file import, SHA-256 integrity verification, durable artifacts and reproducible execution plans.
- Native out-of-process workers/plugins, bounded job scheduling, cancellation, retry and live lifecycle telemetry.
- FASTA/FASTQ QC, paired-end processing, trimming/filtering, alignment, SAM/BAM QC and coverage/depth.
- Canonical small-variant representation for SNV, insertion, deletion, MNV and DELINS/complex alleles.
- VCF ingestion, coordinate indexing and FASTA-backed normalization.
- Local realignment/indel candidate evidence plus unified probabilistic SNV+indel genotyping.
- Advanced variant filtering including strand-bias, homopolymer and regional evidence.
- Generic variant prioritization rules independent of annotation.
- Versioned local reference-database framework and exact allele / GFF-GTF feature-overlap annotation.
- Multi-sample genotype matrices and case/control association with Fisher exact tests, odds ratios, 95% CI and BH FDR.
- Indexed variant-workspace query/detail APIs with bounded windows and deterministic sorting.
- Streaming TSV/CSV/JSON/site-level VCF export with pipeline/reference/database provenance.
- Synthetic truth-set metrics and pinned GIAB HG002 GRCh38 v4.2.1 compatibility evidence.

## Validation

OpenGenesis-BioCore v0.3.0 closes against one exact Iteration 068 source candidate. Linux GCC Debug/Release,
Clang Debug, GCC ASan+UBSan, native Windows MSVC Debug/Release, clean install and portable-package smoke,
all retained performance/truth-set gates, and independent Gemini + Claude Code release review must refer to
the same candidate identity. Historical evidence is never reused to close a newer source state.

See [`docs/release/VALIDATION.md`](docs/release/VALIDATION.md),
[`docs/release/FINAL-WINDOWS-CLOSURE.md`](docs/release/FINAL-WINDOWS-CLOSURE.md),
[`docs/release/KNOWN-LIMITATIONS.md`](docs/release/KNOWN-LIMITATIONS.md), and
[`docs/release/RELEASE-NOTES-0.3.0.md`](docs/release/RELEASE-NOTES-0.3.0.md).

## Build on Linux

```bash
cmake --preset linux-gcc-debug
cmake --build --preset linux-gcc-debug --parallel
ctest --preset linux-gcc-debug
```

## Build on Windows

Use an x64 Visual Studio Developer PowerShell with CMake and the required dependencies available through
your toolchain/vcpkg setup:

```powershell
cmake --preset windows-msvc-release
cmake --build --preset windows-msvc-release --parallel
ctest --preset windows-msvc-release
```

## Create a project

The project root can be a new directory whose parent already exists. Keep the global catalog outside the
project workspace.

```bash
biocore --init-project ./my-project \
  --name "My OpenGenesis-BioCore Project" \
  --catalog ./biocore-data/catalog.sqlite
```

Then start the local server:

```bash
biocore --serve ./my-project
```

The terminal prints the localhost UI URL and a one-time process bootstrap bearer token. Paste that token
into the browser session form; the browser session uses a separate HttpOnly cookie.

## Security posture

OpenGenesis-BioCore binds the supported web surface to `127.0.0.1`, sends no telemetry, and does not place bearer
secrets in URLs or browser storage. Native plugins are trusted local executables and are **not sandboxed**;
installing a plugin is equivalent to installing native software from that publisher. See [`SECURITY.md`](SECURITY.md).

## Scientific scope

The v0.3.0 release focuses on canonical small-variant analysis. Structural-variant/CNV calling, tumor-normal
somatic calling, full GWAS, ACMG clinical classification and distributed/cloud execution remain outside scope.
The current Annotation 2.0 implementation performs exact allele lookup and GFF/GTF feature overlap; it does not
claim transcript/CDS/codon/protein consequence prediction. The GIAB gate demonstrates truth-set ingestion and
comparison compatibility, not end-to-end caller accuracy on HG002 sequencing reads.

## License

OpenGenesis-BioCore is distributed under the [MIT License](LICENSE).

## Citation

The stable Zenodo concept DOI for OpenGenesis-BioCore is **10.5281/zenodo.22012037**. The repository citation
metadata identify this source as v0.3.0. A version-specific v0.3.0 DOI is added only after the accepted release
source is deposited; no unreleased DOI is fabricated in this candidate.

Citation metadata are available in [`CITATION.cff`](CITATION.cff).
