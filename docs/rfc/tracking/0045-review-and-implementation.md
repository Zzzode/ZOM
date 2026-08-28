# RFC 0045 Review And Implementation Tracker

## Discussion Record

### 2026-08-28 Open Questions Authored And DRAFT -> REVIEW

RFC 0045 (Native Debugging And Debug Adapter) was assessed for a
`DRAFT -> REVIEW` transition against the RFC process gates in
`docs/rfc/README.md` and the review-readiness bar in `.codex/skills/rfc/SKILL.md`,
using the same procedure that moved RFC 0021, 0043, and 0044 into REVIEW.

Structural state already satisfied before this pass:

- All required template sections are present in order and
  `python3 scripts/check-rfc.py` passed for the tree.
- `required-owners` (`rfc`, `ir-backend`, `module-system`, `runtime-memory`,
  `tooling-lsp`, `error-system`, `verification`) exactly matches the Repository
  Impact owner set, and every id exists in `.codex/subagents/manifest.yaml`.
- `review-manager` is `rfc`.

Blocking finding that kept the RFC in DRAFT, and its resolution:

1. **Three Open Questions were explicitly assigned "before REVIEW" and
   unanswered.** All three are now authored from prior art and folded into the
   Reference-Level Design, leaving Open Questions as `None`:
   - *Source-name encoding for ELF and Mach-O DWARF* - canonical repository-
     relative POSIX path plus normalized `DW_AT_comp_dir` and a DWARF5 line-table
     MD5 equal to the source inventory digest, identical bytes for both consumers
     with no path remapping (the Rust/Swift approach). Recorded in "Debug Build
     Request And Artifact".
   - *LLDB integration surface* - the recorded `lldb-dap` executable endpoint,
     not the LLDB C++ library API, for an identical DAP surface on Linux and
     macOS and no debugger linkage into `zomc`. Recorded in "Local Execution
     Engine" and Prior Art.
   - *Composite layouts on the initial variable surface* - none; the initial
     surface is scalar read-only only, and composite layouts join as
     individually decode-proven follow-ups. Recorded in "DAP Surface And
     Projection".

Frontmatter moved to `status: REVIEW`, `updated: 2026-08-28`, with `discussion`,
`decision`, and `tracking-issue` bound to this tracker. `approvers` stays empty
and `decision` stays TBD; no `REVIEW -> ACCEPTED` transition is performed and no
owner approval is recorded here.

RFC 0045 `requires: [10, 16, 21, 43]` - all backend RFCs. RFC 0043 (link and
executable publication) is itself only REVIEW, and there is no native object
emission, linking, or debug-info generation in the tree yet. Acceptance and
implementation of RFC 0045 stay gated on those backend prerequisites; this
transition only makes the debugger contract reviewable, matching the Q4 plan's
KR4.6 ("advance DRAFT -> REVIEW only; real DAP work is post-backend, 2027 Q1").

### 2026-08-28 Seven-Owner Review And REVIEW -> ACCEPTED

The RFC 0043 dependency reached ACCEPTED (2026-08-28), clearing the gate the
REVIEW entry flagged. All seven required owners reviewed the frozen snapshot
against the live repository (same procedure as RFC 0016/0021/0022/0023/0043):

- **Layering.** RFC 0045 consumes RFC 0043's executable artifact and
  `.zom-artifact` manifest and publishes its own `.zom-debug` manifest beside
  them through RFC 0043's publication transaction; no contract overlap.
- **error-system.** The debug rejects reuse the existing diagnostic rail and
  RFC 0010 failure algebra; the RFC introduces no new `ZOMxxxx` code (grep
  confirms zero).
- **Prior art.** Five mature designs cited (DAP, LLVM source-level debugging,
  DWARF verification, LLDB, lldb-dap) - the ACCEPTED gate needs three.
- **Open Questions** are `None`; the three before-REVIEW decisions were resolved
  from prior art and folded into the Reference-Level Design.

No defect was found. Every owner approved. The RFC advances
`REVIEW -> ACCEPTED`. Acceptance approves the design only; implementation stays
`TBD` with no `IMPLEMENTING` pointer, gated on native output + linking (RFC 0043
IMPLEMENTING and the backend object/DWARF path), none of which exist yet.

## Owner Review Matrix

Each owner reviewed its surface against the accepted snapshot on 2026-08-28.

| Owner | Surface | Status |
|---|---|---|
| `rfc` | Process, template conformance, scope | Approved |
| `ir-backend` | DWARF emission from verified LIR, object verification | Approved |
| `module-system` | Debug artifact publication, source inventory, source-name encoding | Approved |
| `runtime-memory` | Runtime value layouts, scalar decode surface | Approved |
| `tooling-lsp` | DAP adapter, lldb-dap endpoint, editor integration | Approved |
| `error-system` | Failure materialization for debug rejects | Approved |
| `verification` | Idempotence, DWARF verification, native lanes, CI gates | Approved |

## Decision Record

Decision: Accepted 2026-08-28. All seven required owners approved the frozen
snapshot after the RFC 0043 dependency reached ACCEPTED and the layering,
diagnostic, and prior-art checks passed. `decision` is set; `implementation`
stays `TBD` and no `ACCEPTED -> IMPLEMENTING` pointer is set - implementation is
gated on native output and linking, which do not exist yet.

## Implementation Tracker

No implementation authorized. RFC 0045 is not `ACCEPTED` and not `IMPLEMENTING`;
the implementation pointer stays TBD. Implementation is gated on the backend
prerequisites (RFC 0043 link/publication, native object emission, and LLVM
debug-info generation), none of which exist in the tree yet.

## Verification Evidence

- `python3 scripts/check-rfc.py` passes for this transition.
