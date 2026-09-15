# RFC 0051 Review And Implementation Tracker

## Discussion Record

### 2026-09-14 DRAFT -> REVIEW

Two-part RFC: final-seal ceremony proportionality and text-scanner evidence.

| Round-1 proposal SHA-256 (superseded) | `4adc589e13275170b37d3d3ab549d4da507978527a09bebc18a4182bcf81bd34` |
|---|---|

### 2026-09-15 Round 1 results

- `error-system`: APPROVED (editorial only): seal failures are internal
  incidents, never ZOM codes; reconcile stale RFC 0038 status.
- `ir-backend`: APPROVED with conditions C1-C3: keep internal-header
  banned-include tripwires until a real compile/link boundary exists; bind
  the assertion-to-native-test map; cover runCompatibility cross-target
  rejection.
- `module-system`: REQUEST-CHANGES: Part 1 omitted the already-landed RFC
  0038 Success/Failure closure (`FinalSnapshotClosureKind`,
  `FinalFailureProjection`); simplification must preserve both.
- `verification`: REQUEST-CHANGES: scanner normativity spans ~15 RFCs not
  just 0017/0021; CMake/asm positive markers have no native replacement; the
  lexer gate lacks a self-test.
- `rfc`: REQUEST-CHANGES with the same scope finding.

### 2026-09-15 RETURNED and revised; pending Round 2

The revised DRAFT adds the seal threat-to-mechanism table including failure
closure, a repository-wide generic supersession rule listing every affected
RFC, a bound positive-marker disposition table (runtime assertions replaced
by executed tests; build/asm/banned-include checks retained as labeled
tripwires pending real boundaries), corrected impact ownership (rfc owns the
rfc skill, task-router owns subagent files), and concrete test commands.

### 2026-09-15 Revised DRAFT -> REVIEW Round 2

| Proposal SHA-256 | `83c708c33ae359cceafa3a8f042ee03173db16de14a8d05e02e96ce5cd7b2e27` |
|---|---|


## Owner Review Matrix

| Owner | Round 1 | Round 2 |
|---|---|---|
| `module-system` | REQUEST-CHANGES (RFC 0038 failure closure; demand edges) | Pending |
| `ir-backend` | APPROVED with conditions C1-C3 | Pending confirmation against the bound disposition table |
| `error-system` | APPROVED (editorial) | Pending confirmation |
| `verification` | REQUEST-CHANGES (repo-wide scope; build/asm markers; self-tests) | Pending |
| `task-router` | Pending in Round 1; impact rows corrected in revision | Pending |
| `rfc` | REQUEST-CHANGES (scope; stale wording) | Pending |

## Decision Record

TBD.

## Implementation Tracker

Not started; the revised RFC is not ACCEPTED.
