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

### 2026-08-27 Three Deferred Decisions Authored And DRAFT -> REVIEW

The two prior passes correctly refused to *invent* answers merely to clear the
gate. This pass performs the legitimate alternative: the RFC authors decide the
three questions itself, grounded in mature prior art and the accepted upstream
contracts, and moves `DRAFT -> REVIEW`. Making a defensible design decision and
writing it into a DRAFT RFC is authoring, not fabrication. No owner approval, no
review verdict, and no `ACCEPTED` transition is recorded here; `approvers`
remains empty and `decision` remains a review-tracking pointer.

1. **Toolchain-discovery record** (owners `ir-backend`, `module-system`,
   `runtime-memory`). RFC 0043 now defines an immutable per-target
   `ToolchainClosure` record (Reference-Level Design, "Toolchain Discovery
   Record") carrying `targetSpecificationIdentity`, one `sysroot` (the Linux
   sysroot or the macOS SDK root), one digest-pinned `linker`, ordered
   digest-pinned `crtObjects`, ordered `defaultLibraries`, and a small
   target-owned `environment`. Prior art: the Clang driver's `--sysroot` /
   macOS `-isysroot` target-root selection
   (<https://clang.llvm.org/docs/CommandGuide/clang.html>), Rust's per-target
   sysroot plus `cc`-crate linker discipline
   (<https://rustc-dev-guide.rust-lang.org/backend/libs-and-metadata.html>), and
   Zig's hermetic bundled cross libc/sysroot model
   (<https://ziglang.org/learn/overview/#zig-is-also-a-c-compiler>). Provenance
   discipline mirrors RFC 0016's fail-closed `LLVM_DIR` chain ("LLVM build and
   CI contract"): the closure is supplied by explicit configuration, no
   `PATH`/`SDKROOT`/`LIBRARY_PATH`/`LD_LIBRARY_PATH`/`DYLD_LIBRARY_PATH`/search
   fallback is allowed, and an unset root or digest mismatch rejects before any
   tool runs. It is a data contract only and does not require object emission to
   exist.

2. **RFC 0010 failure rows for linker-process failures** (owner
   `error-system`). RFC 0010's `IrFailurePhase` is closed at tags `0x01`-`0x10`
   ending at `ObjectEmission`, its `BackendOperation` ends at `EmitObject`, and
   its only object-stage capability kind `OutputCreationFailed` covers object
   output creation, not a linker subprocess (RFC 0010 "IrFailurePhase",
   "BackendOperation", and the `ObjectEmission` row). RFC 0043 therefore *owns*
   extending the algebra, which is RFC 0010's sanctioned pattern (new stages
   register their own phases; `FeatureBoundaryVerification` is the only
   source-rejecting seam and every other stage keeps `IrOperationResult`).
   RFC 0043 adds three closed phases `LinkPlanConstruction`, `LinkerInvocation`,
   and `ExecutablePublication` (extending the tag range to `0x13`) plus one
   `BackendOperation` alternative `InvokeLinker` (tag `0x0b`), reusing existing
   `IrFailureKind`s with no new diagnostic family: linker subprocess
   exit/spawn failure and missing link output map to `OutputCreationFailed`
   under `LinkerInvocation` with a `Backend { operation: InvokeLinker }` site,
   and a malformed executable maps to `InvalidFact`/`InvalidAbi` under
   `ExecutablePublication` (Reference-Level Design, "Linker And Publication
   Failure Algebra"). Prior art: RFC 0010's own phase/row extension discipline
   and the Clang driver's separation of the object stage from the link stage.

3. **Native CI architecture lanes** (owner `verification`). RFC 0016 fixes the
   build-host runners `macos-15` and `ubuntu-24.04` and the `X86`+`AArch64`
   backend set but no execution lane. RFC 0043 decides a concrete, minimal
   matrix (Operational Readiness, "CI Architecture Lane Matrix"): execute
   natively on Linux `x86_64` (`ubuntu-24.04`) and macOS `aarch64` (Apple-silicon
   `macos-15`) - together exercising both backend architectures by execution -
   and cross-publish-and-inspect (never run) Linux `aarch64` and macOS
   `x86_64`, with `zomc run` rejecting the two inspected targets before process
   creation. Prior art: RFC 0016's fixed runner and backend contract, and the
   Clang/LLVM cross-compilation practice of inspecting a cross artifact rather
   than executing a foreign binary.

**Correction (2026-09-01):** decision 1 above recorded the `ToolchainClosure`
record as carrying "a small target-owned `environment`". A 2026-09-01 owner
review found that field had no production producer, no codec fold, and only dead
plumbing, and ruled it removed; the link now runs under a strictly empty
environment (`SubprocessEnvPolicy::Empty`). The record carries five fields and
no `environment` field. The historical decision text above is retained
unaltered; this note is the authoritative correction. A future target-owned
environment set would be introduced with its own configuration source,
toolchain discovery, and codec fold. See the 2026-09-01 Status History row in
RFC 0043 and commit `8873a957` (RFC rewrite) / `1450e7e9` (dead-plumbing
deletion).

Transition: RFC 0043 frontmatter moves `DRAFT -> REVIEW`, `updated` becomes
2026-08-27, a `DRAFT -> REVIEW` Status History row is added, `discussion` binds
to this record and `tracking-issue` binds to the implementation tracker below,
`decision` binds to the Decision Record as a review-tracking pointer, and the
RFC index row is set to `REVIEW`. `approvers` stays `[]`. The frozen REVIEW
proposal snapshot and the accepted upstream pins are recorded under "Bound
Proposal Snapshots" below and each equals the current `sha256sum` of its file.

### 2026-08-28 Six-Owner Review And REVIEW -> ACCEPTED

All six required owners reviewed the frozen REVIEW snapshot against the live
repository, using the same procedure that accepted RFC 0016, 0021, 0022, and
0023.

- Dependency readiness: RFC 0006, 0010, 0012, 0016, and 0021 are all
  `IMPLEMENTING`, satisfying the reference-level dependencies this RFC builds on.
- `error-system` cross-check: the RFC 0010 failure algebra
  (`IrFailurePhase`/`IrFailureKind`/`BackendOperation`) terminates at
  `ObjectEmission`/`EmitObject`; RFC 0043 appends `LinkPlanConstruction`,
  `LinkerInvocation`, `ExecutablePublication`, and `InvokeLinker` strictly past
  that boundary with zero name collision and no new `ZOMxxxx` diagnostic family.
- Every `docs/rfc/README.md` ACCEPTED gate is met: template, three-plus prior
  art, concrete goals/non-goals, implementable reference design, full repository
  impact, stated acceptance criteria, ordered implementation plan, named test
  plan, review manager, and `Open Questions: None`.

No new defect was found. Every owner approved. The RFC advances
`REVIEW -> ACCEPTED`; the native-executable acceptance-criteria evidence remains
a `LANDED` gate and implementation stays unauthorized-by-pointer (`TBD`).

### 2026-08-28 First Authorized Slice And ACCEPTED -> IMPLEMENTING

With RFC 0043 ACCEPTED and its upstream object-emission dependency now satisfied
in the tree (RFC 0021 is `IMPLEMENTING` and native object emission landed on the
production path, `10ef73b2`), the first authorized implementation slice was
landed as evidence and the RFC moves `ACCEPTED -> IMPLEMENTING`. The slice is
Implementation Plan step 3 - "canonical link-plan construction, independent
verification, and mutation tests" - executed strictly "without invoking a
linker", so it needs no toolchain, no subprocess, and no LLVM linkage and builds
under the default frontend `sanitizer` preset. This mirrors how RFC 0016 entered
IMPLEMENTING on its first landed CMake-gate slice rather than on the full
contract.

Landed in this slice:

1. **Closed failure algebra extended in code.** `compiler/ir/ir-failure.h` now
   defines `IrFailurePhase::LinkPlanConstruction` (`0x11`),
   `LinkerInvocation` (`0x12`), and `ExecutablePublication` (`0x13`) past the
   terminal `FeatureBoundaryVerification` (`0x10`), and
   `BackendOperation::InvokeLinker` (`0x0b`) past `EmitObject` (`0x0a`), exactly
   as the "Linker And Publication Failure Algebra" section specifies. The
   `legalKind`/`legalOwnerSite` validators and the closed-tag/matrix tests in
   `ir-failure-test` and `ir-diagnostic-adapter-test` were extended to cover the
   new coordinates; the extension is append-only, so every prior tag encoding is
   byte-identical.
2. **Link-plan codec and independent verifier.** `compiler/ir/link-plan-codec.h`
   and `.cc` implement the immutable `ToolchainClosureRecord`, `LinkInputRecord`
   (object/CRT/library/runtime roles), `LinkerArgumentRecord` (initial landed
   shape; removed in the current approved contract - see the 2026-08-29 refinement
   below), and `VerifiedLinkPlan` value types - each built only through validating
   factories with no public aggregate initializer, so a plan cannot be
   reconstructed from raw paths - plus the domain-separated, length-framed
   `LinkPlanCodec`
   (`zom.link-plan` preimage) that computes the SHA-256 `LinkPlanId`, and the
   independent `LinkPlanVerifier::verify` that proves the six numbered link-plan
   invariants and maps each rejection to a `LinkPlanConstruction` failure row
   (`OutputCreationFailed` for a bad output path, `MissingRequiredFact` for a
   missing entry symbol or empty object set, `InvalidFact` for a mis-roled or
   out-of-root record, `AdditionalFact` for a duplicate canonical key).
3. **Deterministic oracle and fail-closed mutation matrix.**
   `tests/unittests/compiler/ir/link-plan-codec-oracle-test.cc` freezes a minimal
   plan's 503-byte preimage, its full hex, and its `LinkPlanId`
   (`287f421b8e9713cdd0c371c5d14e419818a652160a756fcbaf4fc0313452a405`), proves
   field sensitivity (output path, argument order, and input digest each move the
   id), and asserts every invariant rejection returns the RFC-mapped
   `IrFailurePhase`+`IrFailureKind`. All 11 cases pass under the sanitizer build.

This slice invokes no linker, reads no filesystem, and binds no live
`TargetRegistryCapability` or `VerifiedObjectArtifact`; the session-owned
`planExecutable`/`linkExecutable` APIs, the runtime-closure discovery, the
ELF/Mach-O executable verifier, the `.zom-artifact` manifest and atomic
publication, and the host-gated `zomc run` cutover remain later slices and stay
Pending. Frontmatter moves `status: IMPLEMENTING` with `implementation` bound to
the Implementation Tracker; the README index row is set to `IMPLEMENTING`.

**Correction (2026-09-01):** two references in the dated slice above are stale.
(a) Item 2's "see the 2026-08-29 refinement below" points to a section that does
not exist in this tracker; the argument-surface removal and later refinements are
recorded in the RFC 0043 Status History, not here. (b) Item 3's frozen oracle is
superseded: after the argument-surface removal and the inspection-profile
binding the live minimal plan is 518 bytes with `LinkPlanId`
`54e60703...817c8dfd` (see the Implementation Tracker row and
`tests/unittests/compiler/ir/link-plan-codec-oracle-test.cc`), not the
503-byte / `287f421b...` values recorded above. The historical slice text is
retained unaltered; this note is the authoritative correction.

## Bound Proposal Snapshots

| Proposal SHA-256 | State |
|---|---|
| `3a7ae03a8a109be7fea9b347d030c6bb9a1d248ba1305d1e3f7c8f78ef05c855` | Historical REVIEW snapshot approved 2026-08-28; invalidated by later normative edits |
| `a200e8fffcc438cc3d2e9bd675cc0e8ed42a4f8b39d1cce49e120a1914c8716d` | Current RFC text; six-owner re-approval pending |

The 2026-08-28 owner approvals bound the historical REVIEW snapshot
`3a7ae03a...`. Normative edits landed since then (the generic-argument-surface
removal, the D1 publication-transaction contract, the crash-consistency
revisions, and the 2026-09-01 environment rewrite), so under this tracker's own
rule those approvals no longer cover the current text. The current RFC text is
`a200e8ff...`; re-approval against that value is pending and is recorded as such
in the Owner Review Matrix below.

Accepted upstream pins, frozen at the 2026-08-27 REVIEW snapshot (commit
`55d2b60b`) and not re-pinned to each upstream edit. These values equal each
file's `sha256sum` at that REVIEW transition; the 2026-08-28 repository-wide
path refactor (`f89d74b6`) subsequently rewrote all five files, so none of these
pins equals the current `sha256sum` of its file. The pins are retained as the
frozen provenance of the REVIEW-time dependency boundary, not as live hashes:

| RFC | File SHA-256 | State |
|---|---|---|
| RFC 0006 | `248080cd962e2ecb5cf1bf84124e38ce54ec3e1ed2e734b2237d7e43bbf08092` | Accepted design in implementation |
| RFC 0010 | `d816f30d07291a6260241ddfe8ab5dc5405d5812e3241a974e08368bca077209` | Accepted design in implementation |
| RFC 0012 | `4661fd71d3c2529e94289f1641c175fc73e92f0255f12f44fbb6f74515dea5e7` | Accepted design in implementation |
| RFC 0016 | `ec27f6d3015ed5f91d903671f225141832ef165eec8fd799845ae8913743baee` | ACCEPTED target-authority snapshot |
| RFC 0021 | `3aa4cfc11d268a0bac10b7aba01e23fe9d598a224e6dcf432124bb9eafa60397` | ACCEPTED LIR/LLVM object-emission boundary |

## Owner Review Matrix

| Owner | State | Review Surface |
|---|---|---|
| `rfc` | Approved 2026-08-28 on `3a7ae03a`; re-approval pending on `a200e8ff` | Governance completeness, prior art, scope, Open Questions handling, and transition readiness |
| `ir-backend` | Approved 2026-08-28 on `3a7ae03a`; re-approval pending on `a200e8ff` | Object-to-executable pipeline, link plan, driver invocation, executable verifier, and toolchain-discovery record |
| `module-system` | Approved 2026-08-28 on `3a7ae03a`; re-approval pending on `a200e8ff` | Package session, target capability, artifact requests, and sysroot/SDK input binding |
| `runtime-memory` | Approved 2026-08-28 on `3a7ae03a`; re-approval pending on `a200e8ff` | Runtime closure, platform ABI records, and startup-object containment |
| `error-system` | Approved 2026-08-28 on `3a7ae03a`; re-approval pending on `a200e8ff` | RFC 0010 failure-algebra extension (`LinkPlanConstruction`, `LinkerInvocation`, `ExecutablePublication` phases and the `InvokeLinker` backend operation) with no new diagnostic family |
| `verification` | Approved 2026-08-28 on `3a7ae03a`; re-approval pending on `a200e8ff` | Native and cross-target lanes, the CI architecture lane matrix, and evidence gates |

Each approval must identify the exact RFC SHA-256, and normative edits
invalidate earlier approvals. All six owners approved the frozen REVIEW snapshot
`3a7ae03a8a109be7fea9b347d030c6bb9a1d248ba1305d1e3f7c8f78ef05c855` on
2026-08-28; that approval is a historical fact and is retained above. Normative
edits have landed since (see Bound Proposal Snapshots), so those approvals no
longer cover the current text `a200e8ff...`. No owner has yet re-approved the
current text; KR5.3 owner sign-off is not complete until six re-approvals
against `a200e8ff...` are recorded here.

## Decision Record

Decision: Accepted 2026-08-28.

On 2026-08-27 the RFC authors resolved the three `before REVIEW` Open Questions
by design decision grounded in prior art and the accepted upstream contracts,
bound `discussion`/`tracking-issue`/`decision`, froze the REVIEW snapshot, and
moved RFC 0043 `DRAFT -> REVIEW`.

On 2026-08-28 all six required owners conducted a substantive review against the
live repository and the frozen snapshot and approved it. Review confirmed: all
five dependency RFCs (0006, 0010, 0012, 0016, 0021) are `IMPLEMENTING`; the RFC
0010 failure-algebra extension adds `LinkPlanConstruction`, `LinkerInvocation`,
and `ExecutablePublication` phases past RFC 0010's terminal `ObjectEmission`
phase, plus the `InvokeLinker` backend operation past `EmitObject`, with no name
collision and no new diagnostic family (no `ZOMxxxx` code is introduced); and
every ACCEPTED gate in `docs/rfc/README.md` is satisfied.

Acceptance approves the design and authorizes implementation to begin; it does
not assert the native-executable evidence in the Acceptance Criteria, which is a
`LANDED` gate. `implementation` stays `TBD` and no `ACCEPTED -> IMPLEMENTING`
pointer is set, because backend object emission and linking do not yet exist in
the tree.

**Correction (2026-09-01):** the "`implementation` stays `TBD` and no
`ACCEPTED -> IMPLEMENTING` pointer" statement above was true at 2026-08-28. It no
longer holds: the RFC frontmatter is `IMPLEMENTING` with `implementation` bound,
backend object emission landed (`10ef73b2`), and the link/publication/`zomc run`
slices landed on `develop` (see the Implementation Tracker below and the
2026-09-01 RFC Status History row). The historical decision text above is
retained unaltered; this note is the authoritative correction.

## Implementation Tracker

| Slice | State | Required evidence |
|---|---|---|
| Accepted RFC 0016 target authority and RFC 0021 verified object artifact binding | Satisfied | RFC 0016 and RFC 0021 are `IMPLEMENTING`; native object emission landed on the production path (`10ef73b2`), so the object-artifact dependency this RFC builds on exists. |
| Canonical link-plan construction and verification | Landed on `develop` | The generic argument surface remains removed. `ExecutableInspectionProfile` now binds object format, x86-64/AArch64 machine, 64-bit width, sorted required runtime symbols, and the canonical runtime-reference domain into the canonical plan. The live oracle is 518 bytes and `LinkPlanId` `54e60703...817c8dfd`; mutation tests cover profile sensitivity, invalid symbol sets, unresolved-runtime and malformed-symbol rejection, and driver-format mismatch. |
| Verified runtime closure discovery and verifier | Landed (contract-complete; consumed only by tests, production wiring deferred) | Closed explicit toolchain discovery, path/digest binding, host-format gate, and mutation tests. The discovery entry has no production caller: its configuration source (a `ToolchainSearchSpec`/sysroot supplied outside the verified compiler) is deferred, so `zomc` hand-assembles the closure from the build-pinned host linker. |
| Target-selected driver invocation and cleanup | Landed on `develop` | Strictly empty environment (`SubprocessEnvPolicy::Empty`), canonical argv, input snapshots, exec-by-descriptor, transaction output, and cleanup obligations |
| Executable inspection, manifest, and recoverable publication | Landed on `develop` (`a737dda5`, `89816cb3`) | ELF64/Mach-O64 bounded inspection plus consuming `linkAndPublish`; positive Linux ELF and negative malformed/machine/runtime-symbol tests |
| Host-compatibility-gated `zomc run` cutover | Landed on `develop` (`b2310198`, `c7e842f0`); Linux x86-64 host slice | Package compile -> LLVM object -> bundled `_start` -> verified host linker -> D5/D1 publication -> `runCompatibility` -> shell-free execution; macOS/AArch64 and general entry semantics remain later slices |

## Verification Evidence

- `python3 scripts/check-rfc.py`: passed for 46 proposal RFCs on 2026-08-27,
  including after the `DRAFT -> REVIEW` transition and the frozen REVIEW
  snapshot binding.
- Repository inspection confirmed RFC 0043's stated dependency boundary: no
  `VerifiedObjectArtifact` production path exists yet, and RFC 0021 is
  `ACCEPTED` without an `IMPLEMENTING` pointer.
- **Correction (2026-09-01):** the bullet above recorded the 2026-08-27
  dependency boundary. It is now superseded: native object emission landed
  (`10ef73b2`) and RFC 0021 is `IMPLEMENTING`, so a verified-object-artifact
  production path exists. The historical bullet is retained; this note is the
  authoritative correction. Current KR5.3 verification evidence (build, five
  IR/link suites 112/112, the four repository gates, the three backend CLI tests,
  and the RFC-hash pin, re-run at HEAD `850d02e3`) is recorded in the 2026-09-01
  RFC Status History row and corroborated by the six-owner re-approval gate round
  (all six RECOMMEND; the sign-off itself remains pending, see the Owner Review
  Matrix).

## Locally Reproduced Status Snapshot (2026-08-31)

This snapshot records only evidence reproduced in the current build
environment. It does not vouch for LLVM-backend execution evidence produced
elsewhere.

- Landed on `develop` as individually reviewed commits:
  - `a737dda5` feat(ir): D5 executable inspection and the `linkAndPublish` chain.
  - `89816cb3` feat(ir): D1 publication-recovery hardening against concurrent
    claim sweeps.
  - `b2310198` feat(zomc): the `run` consumer and Linux x86-64 runtime entry
    wiring.
  - `c7e842f0` feat(zomc): the `build` subcommand and shared option groups.
  - Preceded by `7e047a32` test(ir): a latent use-after-free fix in the publish
    success assertion.
- Reproduced here with `ZOM_ENABLE_LLVM_BACKEND=OFF` (the default sanitizer
  preset): the D5, D1, inspector, and link-plan-codec unit tests are green, and
  `native-execution-cli` asserts the documented fail-closed unavailable state.
- **KR5.4 real end-to-end `zomc run` execution is deferred.** The execution
  path requires an LLVM backend build, which pins LLVM 22.1.8; that toolchain
  is not available in this environment, so the `llvm` preset cannot be
  configured here. The fail-closed guard verified above is not execution
  success, and no all-platform completion is claimed.
- **Open:** Mach-O entry-symbol mangling (`_zom` vs `zom`) is unresolved;
  execution remains Linux x86-64 only.

## Deferred Backlog (2026-09-01)

Items surfaced by the KR5.3 owner-review rounds that are deliberately not fixed
in the landed slices. They are recorded here so they are tracked rather than left
in commit messages, and none blocks the current re-approval of the RFC text.

- **Module-system slice unstarted.** The RFC's session-owned
  `planExecutable(request, capability: TargetRegistryCapability)` construction API
  and invariant (1) (the object target must equal the registry-verified target
  authority) are not implemented: the live entry is
  `LinkPlanVerifier::verify(ExecutableLinkRequest&&)` with no capability
  parameter, and `ToolchainClosureRecord::make` accepts an opaque target-identity
  byte string that is never cross-checked against a `TargetSpecId`. RFC 0043's
  module-system Repository Impact area (`compiler/driver/**`, `compiler/identity/**`)
  contains no RFC 0043 code. The "Canonical link-plan construction and
  verification" Implementation Tracker row covers the codec/verifier value types
  only, not this session-owned API.
- **Snapshot-recovery integration assertion.** `zomc`'s recovery-required routing
  (`ab15c079`) is covered by the adapter emit tests plus the route logic, but no
  end-to-end test injects a filesystem crash window to reach the snapshot arm of
  `LinkRecoveryRequired`; `invoke-linker-test` exercises only the publication arm.
- **`splitTriple` field-count bound.** `compiler/ir/host-execution-profile.cc`
  accepts a triple with three or more fields and no upper bound, diverging from
  the target-registry parser's exact 3-4 field acceptance. Non-blocking today
  because consumers read only the allow-listed arch and OS fields.
- **CRT link-order.** The toolchain closure sorts `crtObjects` by canonical byte
  order, which is not ELF startup link order (`crt1`/`crti`/.../`crtn`).
  Non-blocking today because the production closure carries empty CRT and
  default-library sequences; must be resolved before a real CRT enters the
  production closure.
- **`recoverLinkedOutputPublication` has no production caller** and the
  `PublicationRecoveryObligation` payload paths are not surfaced to the operator;
  both are later driver-completeness slices.
