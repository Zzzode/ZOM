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

### 2026-08-27 Upstream Determinability Cross-Check (held in DRAFT)

A second pass re-examined whether the three `before REVIEW` Open Questions are
determinately answerable from the already-`ACCEPTED` upstream RFC 0016
(`docs/rfc/0016-context-bound-target-registry-verification.md`) and RFC 0021
(`docs/rfc/0021-target-aware-lir-and-llvm-translation.md`), rather than being
genuine design choices. The RFC honesty rules forbid inventing an answer merely
to clear the REVIEW gate, so each question was tested against exact upstream
sections. All three remain genuine open design decisions:

1. **Toolchain-discovery record for macOS SDK and Linux sysroot** (owners
   `ir-backend`, `module-system`, `runtime-memory`) is NOT determined by
   accepted upstream. RFC 0016's only discovery contract is its "LLVM build and
   CI contract" (RFC 0016 lines 1908-1978), which governs how CMake locates the
   LLVM package used to *build the ZOM compiler* (the `LLVM_DIR` /
   `llvm-config` provenance chain and `find_package(LLVM ...)`), not how a
   compiled ZOM binary discovers a target SDK or sysroot for *linking a user
   program*. RFC 0016's `VerifiedTargetAuthorityBundle` (RFC 0016 lines
   556-562) carries only the runtime-capability snapshot, target registry,
   code-generation capability registry, and runtime ABI contract registry; it
   defines no SDK, sysroot, CRT, startup-object, linker-driver, or
   toolchain-closure field (repository grep over RFC 0016 for
   `sysroot|sdk|linker|crt|startup|driver` returns no such record). RFC 0021
   explicitly makes this a downstream-RFC responsibility: its Non-Goals defer
   "product link planning, linker invocation, runtime archive closure,
   executable manifests, or binary publication. Those contracts require a
   separate RFC after verified object emission exists." (RFC 0021 lines
   122-124). The verified toolchain-closure record RFC 0043 needs is therefore
   a new design this RFC must author itself; upstream neither fixes nor
   constrains its shape. OPEN.

2. **Which RFC 0010 failure rows cover linker-process failures** (owner
   `error-system`) is NOT determined by accepted upstream. RFC 0010's
   `IrFailurePhase` is a closed enum whose sixteen tags
   (`0x01` through `0x10`, RFC 0010 lines 1101-1106 and 1177) end at
   `ObjectEmission` and `FeatureBoundaryVerification`; there is no linking,
   link-plan, or executable-publication phase. Its `BackendOperation` enum
   (RFC 0010 lines 1132-1134) ends at `EmitObject`, and the `ObjectEmission`
   row's only capability kind is `OutputCreationFailed` (RFC 0010 line 1265),
   defined as failure to create the requested object output, not a linker
   subprocess exit, missing link output, or malformed executable. RFC 0021 does
   not model a linker either (it stops at verified object emission). Whether a
   linker-process failure reuses `OutputCreationFailed` under `ObjectEmission`,
   reuses another existing kind, or requires a new phase/kind is precisely the
   `error-system` design decision RFC 0043 defers; accepted upstream does not
   assign it. The RFC's own body asserts it "adds no failure branch or
   diagnostic code" (RFC 0043 Reference-Level Design, Inputs And Link Plan),
   but does not yet demonstrate that any existing RFC 0010 row actually covers a
   linker-process failure, and no accepted upstream row does. OPEN.

3. **Which native architecture lanes exist for mandatory CI execution** (owner
   `verification`) is NOT determined by accepted upstream. RFC 0016 fixes the
   compiler *build-host* runner labels `macos-15` and `ubuntu-24.04` and the
   package sources (Homebrew `llvm@22`, apt.llvm.org LLVM 22), RFC 0016 lines
   1969-1977, and fixes the code-generation *backend* set to LLVM `X86` and
   `AArch64` (RFC 0016 line 1459). Neither statement fixes the host CPU
   architecture of those runners nor commits to a native *execution* lane per
   architecture. RFC 0043's Operational Readiness and Acceptance Criteria
   require producing and *executing* a minimal ZOM executable on each supported
   native architecture and inspecting cross-target artifacts without executing
   them; that lane matrix (for example whether an `aarch64` native execution
   runner is available in CI, versus `aarch64` only as a cross-published,
   inspected-not-run target) is a `verification` availability and policy
   decision. RFC 0016's build-host and backend contract does not settle it.
   The current repository CI (`.github/workflows/CI.yml`) still uses
   `ubuntu-latest` and `macos-latest` with no `aarch64` execution lane, so
   there is no existing lane matrix to cite as the answer either. OPEN.

Conclusion of this pass: none of the three questions is mechanically resolvable
from accepted RFC 0016 or RFC 0021. Resolving them by choosing a
toolchain-closure schema, a linker-failure classification, and a CI execution
lane matrix is exactly the design work the questions defer, and inventing those
answers to satisfy the REVIEW precondition would violate the honesty rule.
RFC 0043 is therefore held in `DRAFT`. `discussion` and `tracking-issue` remain
`TBD`, no upstream snapshot pins are bound, and no RFC index or frontmatter
status change is made. `python3 scripts/check-rfc.py` passes clean (46 proposal
RFCs) for the current tree, with no RFC 0043 finding.

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
`DRAFT -> REVIEW` transition is legal. A 2026-08-27 upstream determinability
cross-check confirmed that none of the three questions is settled by the
accepted RFC 0016 or RFC 0021 contracts: the target toolchain-closure/SDK/
sysroot record, the linker-process failure classification, and the native CI
execution lane matrix are each genuine design decisions this RFC must make and
route to their owners, not mechanical drift resolvable from upstream. They were
therefore not invented in this pass. No implementation is authorized by this
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
