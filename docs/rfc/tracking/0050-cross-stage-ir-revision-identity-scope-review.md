# RFC 0050 Review And Implementation Tracker

## Discussion Record

### 2026-09-14 DRAFT -> REVIEW

RFC 0050 re-reviews SHA-256 canonical revisions at in-memory IR boundaries.

| Round-1 proposal SHA-256 (superseded) | `5e4d0462b91d6c2f9578f889110201e69e38fa1d5b17a959b44021e5457f9d9d` |
|---|---|

### 2026-09-15 Round 1 - REQUEST-CHANGES; RETURNED and revised

- `ir-backend` REQUEST-CHANGES, endorsing option B: the feature-boundary
  registry revision is unbuilt (only a zero-consumer failure template), the
  executable-MIR set revision has no record, and option A wrongly scheduled
  `LirRevisionId` on RFC 0048 Phase 4 which never names an LIR verifier.
- `module-system` REQUEST-CHANGES with the same evidence corrections,
  preferring B and requiring lease checks and foreign-database tests to
  survive option C.
- `rfc` REQUEST-CHANGES: the inventory also omitted implemented serialized
  artifacts (link plan, executable manifest, ownership overlay, checker
  revisions) which are the boundary exemplars; add `runtime-memory` owner for
  `compiler/ownership/**`.
- `verification` REQUEST-CHANGES: the parity command omitted required
  `--zomc`/`--snapshot` flags and the tool is not a standalone CTest target;
  name concrete oracle and mutation files; add the internal-versioning gate.

The revised DRAFT corrects the evidence table against the 2026-09-15 tree,
adds the serialized-artifact and ownership rows, adds `runtime-memory`, fixes
the RFC 0048 cross-reference, and gives machine-enforceable verification
commands. It awaits a new REVIEW freeze for Round 2.

### 2026-09-15 Revised DRAFT -> REVIEW Round 2

| Proposal SHA-256 | `f36572c5437171de49965f768f6eba9dd75758f5635c2c1b09ed8576af2363a7` |
|---|---|


### 2026-09-15 Round 3 - owner convergence

All five required owners approved at Round 3 (ir-backend, module-system, rfc, runtime-memory, verification). The recommended option is B (re-scope); the option decision itself awaits the human decision-maker before ACCEPTED. No `approvers` entry and no ACCEPTED transition are recorded by the review panel; acceptance is a separate decision.

## Owner Review Matrix

| Owner | Round 1 | Round 2 |
|---|---|---|
| `ir-backend` | REQUEST-CHANGES R1 | APPROVED R2/R3 |
| `module-system` | REQUEST-CHANGES R1/R2 (core marker family) | APPROVED R3 |
| `runtime-memory` | Owner added R2 | APPROVED R2/R3 |
| `verification` | REQUEST-CHANGES R1/R2 (parity wiring wording) | APPROVED R3 |
| `rfc` | REQUEST-CHANGES R1/R2 (inventory completeness) | APPROVED R3 |

## Decision Record

ACCEPTED 2026-09-15 with option B (re-scope): SHA-256 content revisions are retained only where an in-process lease/lineage mismatch check or verified external/serialized data requires them; LirRevisionId, the feature-boundary registry, and the executable-MIR set revision are deferred until a named consumer exists; AlgebraRevision gets a consumer or is absorbed; persistence is reserved to a future RFC.

## Implementation Tracker

Not started; the revised RFC is not ACCEPTED.
