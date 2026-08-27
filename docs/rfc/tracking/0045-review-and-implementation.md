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

## Owner Review Matrix

| Owner | Surface | Status |
|---|---|---|
| `rfc` | Process, template conformance, scope | Pending |
| `ir-backend` | DWARF emission from verified LIR, object verification | Pending |
| `module-system` | Debug artifact publication, source inventory, source-name encoding | Pending |
| `runtime-memory` | Runtime value layouts, scalar decode surface | Pending |
| `tooling-lsp` | DAP adapter, lldb-dap endpoint, editor integration | Pending |
| `error-system` | Failure materialization for debug rejects | Pending |
| `verification` | Idempotence, DWARF verification, native lanes, CI gates | Pending |

## Decision Record

No decision recorded. RFC 0045 is in REVIEW; `decision` remains TBD until the
required owners approve a frozen snapshot.

## Implementation Tracker

No implementation authorized. RFC 0045 is not `ACCEPTED` and not `IMPLEMENTING`;
the implementation pointer stays TBD. Implementation is gated on the backend
prerequisites (RFC 0043 link/publication, native object emission, and LLVM
debug-info generation), none of which exist in the tree yet.

## Verification Evidence

- `python3 scripts/check-rfc.py` passes for this transition.
