# Iteration 091 — Explicit Artifact Mapping & Reference Provenance

Opened 2026-10-02. Candidate only; independent Gemini review pending.

Frozen baseline:
- Ref: `accepted/iteration-090`
- Commit: `4d049e7c25f06f9ed50bc5c9b26f02eb34cc0cf3`
- Tree: `8effd1fe05152ef325e36ff062884d2994e63987`

## Scope implemented

Iteration 091 adds the explicit cohort analysis-selection admission layer promised by
the accepted v0.6 cohort contract. A selection identifies the exact frozen cohort
revision and explicitly maps each included project sample to:

- batch plan ID,
- batch attempt number,
- Job ID,
- workflow step ID,
- output port,
- generated managed-file ID,
- expected lowercase SHA-256,
- exact VCF sample-column name.

No "latest successful result" lookup is used. The selected attempt must exist in the
persisted attempt lineage, its Job must be completed, its step must be inside the
immutable attempt scope, and the generated artifact must match the frozen batch node
module/plugin/output contract.

Single- and multi-sample VCFs are supported. Multiple project samples may select
different columns from one shared VCF, but one VCF column cannot map to multiple
project samples. Shared VCF bytes are verified and parsed once. Exact UTF-8 sample
names are preserved: there is no trimming, case folding or numeric conversion.

## Reference evidence

The request pins:
- reference managed-file ID,
- assembly identity,
- custom assembly identifier when applicable,
- normalization contract version,
- explicit alias-to-canonical mappings.

The selected FASTA must be a verified managed input. The new streaming reference
manifest reader verifies the managed FASTA bytes, derives canonical contig names and
lengths, and re-verifies integrity after reading. Duplicate/empty contigs fail.
Aliases must be unique, must not shadow canonical names, and must target an existing
canonical contig.

Every selected producer sample's frozen batch plan must reference the same FASTA
managed-file ID, byte size and SHA-256. Therefore an assembly enum or alias alone is
never treated as reference proof.

## VCF admission

Each selected VCF must:
- be a frozen VCF output of the selected attempt,
- have a completed Job,
- retain the selected managed-file identity,
- match the explicit SHA-256,
- pass exact-byte verification through `IResultArtifactReader`,
- contain the explicitly selected VCF sample column exactly once.

GT is inspected for the selected columns only. Missing GT and `.` remain missing.
Diploid phased or unphased calls are accepted. Haploid and polyploid GT values produce
the stable blocker code `unsupported_ploidy`. This iteration does not normalize
variants or construct the multi-sample matrix.

## Resource admission

091 enforces pre-matrix limits:
- 100 included samples,
- 100 selected VCF sources,
- 64 MiB per selected VCF,
- 256 MiB combined selected VCF text,
- 512 MiB reference FASTA.

The 10,000 normalized-allele and 1,000,000 observation limits are intentionally owned
by 092, where the normalized matrix actually exists.

## Explicit non-scope

091 does not:
- persist an immutable cohort analysis snapshot,
- execute or dispatch matrix construction,
- apply QC inclusion/exclusion approval,
- calculate case/control association,
- change scheduler/recovery semantics,
- expose new HTTP routes or browser UI.

Those remain 092–097 scope. Project schema therefore remains v16 in this iteration.

## Verification gate

Eight dedicated CTest modes cover:
1. valid exact mapping and pinned reference manifest,
2. shared multi-sample VCF mapping with one physical read,
3. same cohort sample selected from two attempts,
4. stale/mismatched artifact SHA-256,
5. producing-workflow/reference-byte mismatch,
6. haploid/polyploid GT rejection,
7. resource admission limits,
8. real filesystem FASTA manifest extraction and tamper rejection.

Expected full CTest inventory: 308 tests (300 inherited + 8 new).

The standard four Linux lanes and retained cohort benchmark must pass on the exact
candidate commit. The Gemini package remains exactly four Markdown files plus the
exact source ZIP and SHA-256 manifest. Do not create `accepted/iteration-091` and do
not begin 092 before independent Gemini ACCEPT for the exact candidate identity.

Project owner/developer: **Recep Çelik**.

**ChatGPT contribution:** AI-assisted architecture, implementation, validation logic,
test design, debugging, documentation, source-identity verification and independent
review-package preparation.
