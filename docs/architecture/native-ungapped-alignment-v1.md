# Native Ungapped Reference Alignment v1

## Scope

Iteration 034 introduces a correctness-first native short-read alignment layer inside the plugin boundary.
It does **not** modify Core scheduling, persistence, worker protocol, execution-plan schema, authentication,
or localhost privacy behavior.

Plugin: `org.biocore.align`

Modules:
- `org.biocore.align.single`
- `org.biocore.align.paired`

Pipelines:
- `org.biocore.align.single`
- `org.biocore.align.paired`

Inputs are an explicit reference FASTA plus single-end FASTQ or explicit R1/R2 FASTQ files.
FASTQ may be plain or gzip-compressed and gzip detection is by magic bytes. The reference FASTA is
currently plain-text FASTA.

## Alignment contract

The v1 native algorithm is `native-ungapped-hamming-v1`.

For each read OpenGenesis-BioCore:
1. validates FASTQ/IUPAC/Phred+33 structure;
2. evaluates both forward and reverse-complement orientations;
3. scans all legal positions in all reference contigs;
4. counts non-canonical or unequal bases as mismatches;
5. retains the deterministic first best-scoring hit;
6. reports all equally best positions through `NH`;
7. marks a unique best hit with MAPQ 60 and a multi-best hit with MAPQ 0;
8. emits an unmapped SAM record when no position is within `max-mismatches`.

The mismatch limit is immutable execution-plan data, defaults to 2, and is bounded to 0..12.

The v1 CIGAR is `<read-length>M`; insertions, deletions, clipping, splice junctions, affine-gap scoring,
and local alignment are intentionally outside this contract.

## SAM contract

The output file type is `sam` and includes:
- `@HD`, `@SQ`, and `@PG` headers;
- forward/reverse flagging;
- paired/read1/read2/mate-unmapped/mate-reverse flags where applicable;
- RNEXT/PNEXT and deterministic TLEN when both mates map to the same contig;
- `NM:i` mismatch count for mapped reads;
- `NH:i` count of equally best hits.

Paired mode preserves the frozen Iteration 032 mate-identity rules and rejects unequal pair counts or
contradictory/mismatched mate identifiers. It does not claim the SAM `proper pair` flag because no
insert-size/orientation library model is part of v1.

## Safety bounds and performance

Reference input is bounded to 512 MiB on disk and 500,000,000 bases in this iteration. FASTQ line safety
remains 64 MiB.

The v1 implementation is an exhaustive ungapped scan and is deliberately a correctness/contract
foundation, not a whole-human-genome production aligner. Its output contract is intended to let later
iterations replace or augment the search engine with an indexed/gapped backend without changing the
Core execution architecture.

## Outputs

Each alignment step emits one atomic artifact batch:
- `alignment` — SAM
- `summary` — JSON
- `table` — TSV

Generated outputs use the existing Worker Protocol v2 artifact events and Core-side checksum/provenance
persistence. No new persistence path is introduced.

## Deferred native Windows closure

Per project policy, per-iteration native Windows MSVC and CPack validation is deferred to the final
consolidated release closure. The Windows entrypoint and runtime dependency packaging rules are present
in source and will be validated at that closure.
