# RFC 0052 Review And Implementation Tracker

## Discussion Record

### 2026-09-14 DRAFT -> REVIEW

Process RFC for an internal-change threshold.

| Round-1 proposal SHA-256 (superseded) | `aaf8faf591d7aafaa38e78f49ca244e65f396e6253e7d2db8eaa9d4955d24f63` |
|---|---|

### 2026-09-15 Round 1 results

- `rfc`: REQUEST-CHANGES. The downgrade clause had no legal state-machine
  edge; tracked changes cannot join the main index table (check-rfc.py
  requires a proposal per row); closure disposition must note the
  implementation-field requirement.
- `task-router`: REQUEST-CHANGES. Tracked changes had no path-owner
  selection rule or trigger; the impact table wrongly assigned the rfc skill
  to task-router; the downgrade edge was missing; two matrix cells needed a
  tie-break adjudicator.
- `verification`: REQUEST-CHANGES. The test plan invoked a non-existent
  `check-rfc.py --check` and self-test; the script is flag-less and owned by
  verification in the manifest, so any optional gate hint requires that owner.

### 2026-09-15 RETURNED and revised; pending Round 2

The revised DRAFT specifies `REVIEW -> WITHDRAWN` plus retained-file pointer
as the only disposition, routes tracked changes to manifest path owners with
rfc as process host, fixes matrix adjudication and defaults, places tracked
changes in a separate index subsection (not the proposal table), adds
`verification` owner for the optional gate hint, and corrects the commands.

### 2026-09-15 Revised DRAFT -> REVIEW Round 2

| Proposal SHA-256 | `2b5bf1916a6f9977e2889726366952ede5dfff33b8095a2e8d81b1d0501bc3af` |
|---|---|


### 2026-09-15 Round 3 - owner convergence

All three required owners approved at Round 3 (rfc, task-router, verification). No `approvers` entry and no ACCEPTED transition are recorded by the review panel; acceptance is a separate decision.

## Owner Review Matrix

| Owner | Round 1 | Round 2 |
|---|---|---|
| `rfc` | REQUEST-CHANGES R1/R2 | APPROVED R3 |
| `task-router` | REQUEST-CHANGES R2 | APPROVED R3 |
| `verification` | REQUEST-CHANGES R2 (commands) | APPROVED R3 |

## Decision Record

ACCEPTED 2026-09-15. Full-RFC / lightweight tracked-change / ordinary-commit matrix with path-owner routing, REVIEW->WITHDRAWN disposition for superseded proposals, separate tracked-changes index subsection, and lazy closure-series disposition. Review window is owner discretion.

## Implementation Tracker

Not started; the revised RFC is not ACCEPTED.
