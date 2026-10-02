# Iteration 094 — Case/Control Analysis Integration

Baseline: `accepted/iteration-093` / `0d3027bc567342dc9093418c5f3737b20eeb95ae`.
Status: candidate only; independent Gemini review required before freeze.

## Scope

Iteration 094 connects the immutable Iteration 093 analysis snapshot to the retained
v0.3 case/control association engine. It does not implement a new statistical engine,
persist execution attempts, expose result pagination/reports, or wire browser/HTTP
surfaces. Those remain Iterations 095–097.

The integration reads frozen case/control labels and QC inclusion decisions from the
snapshot, projects the already-validated matrix to analysis-included samples without
converting absent/no-call/partial states to reference calls, and invokes the retained
`analyze_case_control` implementation. Matrix variant identity, effective missingness
counts and frozen test-family membership are rechecked against the snapshot before a
result is accepted.

## Scientific contract

- Case rows remain numerator direction; control rows remain comparator direction.
- Both allele and carrier models are returned for each frozen normalized allele.
- Only complete calls enter contingency tables. Unobserved, no-call and partial-call
  counts remain separately visible; effective sample counts are complete calls.
- Probability-ordered two-sided Fisher exact, OR `ad/bc`, Woolf log-OR 95% CI and
  separate Benjamini-Hochberg families for allele and carrier models are inherited
  unchanged from the accepted Iteration 089 contract.
- Zero cells retain explicit OR kinds (`zero`, `positive_infinity`, `undefined`);
  no Haldane/Anscombe 0.5 correction is introduced. Unavailable CI and non-testable
  p/q values remain null/optional rather than fabricated zeros.
- Frozen minimum-complete-call thresholds and Fisher state limits are passed from the
  approved snapshot. Result p/q presence must exactly match frozen family membership.

## Verification gate

Hand-counted tables, explicit missingness, zero-cell behavior, QC exclusion projection,
frozen-label direction, empty test families, matrix drift, empty-group rejection and
contract-version rejection are covered by dedicated integration tests. Existing v0.6
four-lane Linux regression and cohort-scale evidence remain mandatory. Exact four-part
Gemini review package and source ZIP must be generated from the same candidate commit.
No `accepted/iteration-094` ref is created until Gemini returns ACCEPT.

Project owner/developer: Recep Çelik. ChatGPT contribution: AI-assisted architecture,
implementation, debugging, tests, documentation and independent-review preparation.
