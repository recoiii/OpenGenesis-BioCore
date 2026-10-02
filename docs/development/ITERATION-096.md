# Iteration 096 — Cohort Results Explorer & Reports

Baseline: `accepted/iteration-095` / `cc60d9bb6e895dfad19c6482c8464b409cf35b8b`.
Status: candidate only; independent Gemini review required before freeze.

## Scope

Iteration 096 adds bounded result exploration and report/export contracts on top of the
immutable Iteration 093 snapshot, the accepted Iteration 094 case/control statistics and
the durable Iteration 095 execution lineage. It does not introduce a new statistics
engine, scheduler, execution ledger, HTTP route or integrated browser workflow.

The explorer provides server-side pagination, shared/case-specific/control-specific
carrier-presence views, existing annotation links, statistical display filters and
stable sorting. CSV, TSV, JSON and HTML cohort reports carry the exact snapshot identity,
canonical display-filter definition, frozen denominators, exclusions, methods and
reference provenance.

## Scientific and provenance contract

- Display filters never redefine the frozen test universe and never recalculate Fisher
  p-values or Benjamini-Hochberg q-values.
- Shared/specific views are descriptive carrier-presence views: shared means at least one
  complete-call carrier in both groups; case-specific/control-specific mean carrier
  presence only in the named group.
- Missing p/q values remain missing and cannot satisfy numeric significance filters.
- Association family membership, variant identity and snapshot digest must match the
  approved snapshot before a result page or export is produced.
- Annotation links reuse existing annotation output summaries and do not create a new
  annotation engine.
- Reports expose approved case/control denominators, analysis exclusions, Fisher/OR/CI/FDR
  method identities and the pinned reference SHA-256.
- CSV/TSV/JSON and HTML all carry the same snapshot digest and canonical view-filter
  definition used by the explorer.
- Browser-sized pages are limited to 200 rows. Bounded server-side export is limited to
  10,000 matched rows; the complete large table is not required to be loaded by a browser.

## Verification gate

Dedicated integration tests cover pagination, common/specific views, frozen statistical
filtering, annotation linkage, provenance, CSV/TSV/JSON/HTML outputs, resource bounds and
snapshot/family drift rejection. The expected regression inventory is 358 CTest
registrations.

The exact candidate must pass GCC Debug, GCC Release, Clang Debug and GCC ASan+UBSan plus
cohort-scale evidence. The same commit must generate the exact four-part Gemini package
and source ZIP. No `accepted/iteration-096` ref is created before independent ACCEPT.

Integrated project/browser flow and known-result E2E remain Iteration 097. Native
Windows/Linux distribution/package hardening and Gemini+Claude final closure remain
Iteration 098.

Project owner/developer: Recep Çelik. ChatGPT contribution: AI-assisted architecture,
implementation, debugging, tests, documentation and independent-review preparation.
