# RFC 0043 Review And Implementation Tracker

## Discussion Record

### 2026-08-27 Review-Readiness Assessment (held in DRAFT)

RFC 0043 (Platform Link And Executable Publication) was assessed for a
`DRAFT -> REVIEW` transition against the RFC process gates in
`docs/rfc/README.md` and the review-readiness bar in `.codex/skills/rfc/SKILL.md`.
The assessment used the same procedure that moved RFC 0021 into REVIEW: confirm
required sections, discussion and tracking links, required-owner/Repository-Impact
agreement, upstream snapshot binding, and Open Questions handling.

Structural state that is already satisfied:

- All 19 required template sections are present in order, and
  `python3 scripts/check-rfc.py` passes for the current tree.
- `required-owners` (`rfc`, `ir-backend`, `module-system`, `runtime-memory`,
  `error-system`, `verification`) exactly matches the Repository Impact owner
  set, and every id exists in `.codex/subagents/manifest.yaml`.
- `review-manager` is `rfc`.

Blocking findings that keep the RFC in `DRAFT`:

1. Open Questions are not handled. The RFC's own Open Questions section defers
   three substantive design decisions and assigns each one explicitly to be
   answered `before REVIEW`, yet all three remain unresolved:
   - which exact toolchain-discovery record binds the macOS SDK and Linux
     sysroot inputs without inheriting host search paths (assigned to
     `ir-backend`, `module-system`, `runtime-memory`);
   - which existing RFC 0010 failure detail rows cover linker process failures
     without adding a new diagnostic family (assigned to `error-system`); and
   - which native architecture lanes are available for mandatory CI execution
     (assigned to `verification`).
   Entering REVIEW now would contradict the RFC's own declared precondition.
   These are genuine design decisions, not mechanical drift, so they are not
   resolved in this pass.
2. `discussion` and `tracking-issue` are both `TBD`. The REVIEW gate requires
   both to point at a discussion thread, issue, or local tracking document.
   This tracker file now exists and can serve as the local tracking target once
   the three Open Questions are closed and the frontmatter is updated, but the
   frontmatter links are unchanged while the RFC stays `DRAFT`.

No upstream snapshot pins are recorded in the RFC body. Because the RFC is held
in DRAFT, upstream pin binding to RFC 0006, 0010, 0012, 0016, and 0021 is not
performed in this pass; it becomes required work when the design blockers clear
and the RFC is prepared for REVIEW.

Outcome: RFC 0043 remains `DRAFT`. No frontmatter status change and no RFC index
change were made. No approval, decision, or transition is recorded.

## Owner Review Matrix

| Owner | State | Review Surface |
|---|---|---|
| `rfc` | Pending | Governance completeness, prior art, scope, Open Questions handling, and transition readiness |
| `ir-backend` | Pending | Object-to-executable pipeline, link plan, driver invocation, executable verifier, and toolchain-discovery record |
| `module-system` | Pending | Package session, target capability, artifact requests, and sysroot/SDK input binding |
| `runtime-memory` | Pending | Runtime closure, platform ABI records, and startup-object containment |
| `error-system` | Pending | Reuse of RFC 0010 failure detail rows for linker process failures without a new diagnostic family |
| `verification` | Pending | Native and cross-target lanes, mandatory CI execution architecture, and evidence gates |

Each approval must identify the exact RFC SHA-256. Normative edits invalidate
earlier approvals.

## Decision Record

Decision: Pending. RFC 0043 is held in `DRAFT`. The three `before REVIEW` Open
Questions must be resolved and `discussion`/`tracking-issue` bound before a
`DRAFT -> REVIEW` transition is legal. No implementation is authorized by this
tracker.

## Implementation Tracker

| Slice | State | Required evidence |
|---|---|---|
| Accepted RFC 0016 target authority and RFC 0021 verified object artifact binding | Blocked by DRAFT status | Accepted upstream hashes and object-emission contract with an implementation pointer |
| Verified runtime closure discovery and verifier | Pending acceptance | Closed Linux ELF and macOS Mach-O toolchain closure and mutation tests |
| Canonical link-plan construction and verification | Pending acceptance | Independent verifier, deterministic `LinkPlanId`, and mutation matrix |
| Target-selected driver invocation and cleanup | Pending acceptance | Sanitized environment, argument-vector construction, temporary-output removal |
| Executable inspection, manifest, and atomic publication | Pending acceptance | ELF/Mach-O verifier, `.zom-artifact` manifest, atomic two-file publication |
| Host-compatibility-gated `zomc run` cutover | Pending all prior slices | Host-profile match, cross-target rejection, and documentation/CI updates |

## Verification Evidence

- `python3 scripts/check-rfc.py`: passed for 46 proposal RFCs on 2026-08-27.
- Repository inspection confirmed RFC 0043's stated dependency boundary: no
  `VerifiedObjectArtifact` production path exists yet, and RFC 0021 is
  `ACCEPTED` without an `IMPLEMENTING` pointer.
