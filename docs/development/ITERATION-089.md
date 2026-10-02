# Iteration 089 — Scope, Baseline & Cohort Contracts

Opened 2026-10-02. Candidate only; Gemini review pending. This iteration does not
implement a cohort registry, migrate a database, or expose cohort HTTP endpoints.
The accepted 2026-09-28 roadmap covers iterations 089–098. Its theme is Cohort
Analysis Workspace; the first gate is an executable, source-grounded contract.

## Verified starting identity

Remote refs were read on 2026-10-02; the tag and frozen branch agree:

- Repository: https://github.com/recoiii/OpenGenesis-BioCore
- Annotated tag `v0.5.0`: `e7f8f269c195ccd9a5804c11f21277e433352f95`
- Tag target and `accepted/iteration-088`: `c82d2ef0210f4a74d191b2c27c9c73d640544ebf`
- Baseline tree: `e3087bde6594b603db4f4a06152c3519fa6f7e06`
- Reproduced baseline ZIP SHA-256: `06faa0b999b882f2efe3c1cf43804052b98f885432a12a7de69a30e3e8c18795`
- Archive recipe: `git archive --format=zip --prefix=OpenGenesis-BioCore/ <baseline>`.
  This is a newly reproduced archive, not a claim about historical uploaded ZIP bytes.
- Development branch: `v0.6.0-dev`, directly descended from that baseline.
- Baseline schema: 15. Baseline CTest inventory: 288; see existing v0.5 workflow.

## Deliverables and acceptance

1. `docs/architecture/cohort-workspace-v1.md`: file/symbol inventory, supported
   scientific semantics, immutable data model, API/UI and execution contracts,
   additive migration plan, limits and iteration ownership.
2. `tests/fixtures/cohort-v1/`: synthetic reference and VCFs with independently
   enumerated expected counts. No patient data.
3. `tests/cohort_contract_tests.cpp`: tests the retained parser/matrix/association
   against these fixtures, including absent/no-call/partial/hom-ref, canonical
   ordering, explicit labels, reference mismatch, limits and zero-cell behavior.
4. `tests/cohort_contract_benchmark.cpp`: separately varies samples, variants and
   occupancy; records actual dimensions, timing and checksum. CI captures hardware,
   peak RSS and output bytes. This measures the existing engine, not future UI.
5. Development identity `0.6.0-dev`; stable README/CITATION/release history remains
   v0.5.0. New v0.6 workflow supplies same-commit Linux regression and benchmark evidence.
6. Four review parts plus full source ZIP and actual SHA-256 hashes. All changed
   text files appear verbatim exactly once; unchanged scientific context is in ZIP.

## Boundaries and review gate

The source confirms matrix and association algorithms exist. Cohort snapshot
persistence, multi-sample selection adapters and scheduled cohort dispatch do not.
They are explicitly assigned to 090–095; none is advertised as implemented here.
The roadmap's earlier standalone Gemini scope review is not available in this
checkout. The 089 reviewer must explicitly assess the scope and contract as well
as this candidate. No prior independent ACCEPT is inferred from owner approval.

CI passing is not acceptance. No `accepted/iteration-089` is created, and 090 must
not begin, until Gemini ACCEPT is bound to this exact candidate. Final 098 requires
Gemini and Claude ACCEPT on one final source identity, including native Windows
and package evidence. No accepted ref, release or default branch is moved here.

Project owner/developer: Recep Çelik. ChatGPT contribution: AI-assisted source
inspection, architecture/contracts, fixture and test implementation, debugging,
documentation, CI and review packaging. Independent reviews remain external gates.
