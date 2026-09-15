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

| Proposal SHA-256 | `734b46130a7a277995ff379b8ea857f7aa9583651f61942affd70830850597f4` |
|---|---|


## Owner Review Matrix

| Owner | Round 1 | Round 2 |
|---|---|---|
| `ir-backend` | REQUEST-CHANGES (evidence rows; scheduling) | Pending |
| `module-system` | REQUEST-CHANGES (same rows; lease preservation) | Pending |
| `runtime-memory` | Surfaced as missing owner; added on revision | Pending |
| `verification` | REQUEST-CHANGES (parity invocation; oracle files) | Pending |
| `rfc` | REQUEST-CHANGES (evidence completeness; owners) | Pending |

## Decision Record

TBD.

## Implementation Tracker

Not started; the revised RFC is not ACCEPTED.
