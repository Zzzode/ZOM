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

## Owner Review Matrix

| Owner | Round 1 | Round 2 |
|---|---|---|
| `rfc` | REQUEST-CHANGES (illegal downgrade; index; disposition) | Pending |
| `task-router` | REQUEST-CHANGES (owner/trigger routing; impact rows; tie-break) | Pending |
| `verification` | Added on revision; Round 1 finding on gate commands | Pending |

## Decision Record

TBD.

## Implementation Tracker

Not started; the revised RFC is not ACCEPTED.
