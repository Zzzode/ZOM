# RFC 0051 Review And Implementation Tracker

## Discussion Record

### 2026-09-14 DRAFT Authored And DRAFT -> REVIEW

RFC 0051 (Query Final-Seal And Text-Scanning Evidence) moves to `REVIEW`. It
contains two decision sections and the review manager may split them.

Part 1 re-reviews the RFC 0028 three-phase final-seal ceremony (lock,
independent witness re-derivation, authority token, sealed-snapshot
admission) for proportionality. The ceremony is genuinely consumed:
production seals on every successful bind path and fourteen capability
descriptors require `FinalSealedSnapshot`; retain/simplify/remove options are
presented and "remove" must answer the fourteen live consumers.

Part 2 resolves a direct normative contradiction: RFCs 0017 and 0021 make
source-text-scanning Python scripts (incremental-query, query-descriptor,
IR-architecture) normative completion gates, while LANDED RFC 0047 proves
source scans are not architecture evidence and deleted its own. The IR
scanner also asserts literal call-site substrings yet did not detect four
live RFC 0010/0021 backend deviations. Options: reposition scanners as
regression aids with replacement native evidence, amend RFC 0047, or delete.

Frontmatter is `status: REVIEW`, `updated: 2026-09-14`, with `discussion` and
`tracking-issue` bound here. `approvers` is empty; no approval or status
transition is recorded.

Frozen proposal snapshot (SHA-256 of the RFC document at REVIEW entry):

| Proposal SHA-256 | `4adc589e13275170b37d3d3ab549d4da507978527a09bebc18a4182bcf81bd34` |
|---|---|

## Owner Review Matrix

| Owner | Surface | Round 1 |
|---|---|---|
| `module-system` | Query database seal runtime, sealed descriptors, session bind path | Pending |
| `ir-backend` | IR architecture scanner requirements in RFC 0021 and backend replacement tests | Pending |
| `error-system` | Final-sealed failure projection and the relationship to DRAFT RFC 0038 | Pending |
| `verification` | Scanner self-tests, compile-fail fixtures, native-test replacements, CTest labeling | Pending |
| `task-router` | Gate routing, owner policy, evidence-class rule placement | Pending |
| `rfc` | Process, multi-part RFC handling, overlay of 0017/0021/0028/0047 | Pending |

## Decision Record

TBD.

## Implementation Tracker

Not started; the RFC is not ACCEPTED.
