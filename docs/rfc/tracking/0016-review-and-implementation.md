# RFC 0016 Review And Implementation Tracker

This document records review and implementation evidence for RFC 0016. It does
not approve the proposal. RFC frontmatter remains authoritative for status and
approvers.

## Current Snapshot

| Field | Value |
|---|---|
| Status | `ACCEPTED` |
| Proposal SHA-256 | `ec27f6d3015ed5f91d903671f225141832ef165eec8fd799845ae8913743baee` |
| Review manager | `rfc` |
| Decision | `ACCEPTED` (2026-08-26, all eight required owners) |
| Implementation | Unstarted; slices blocked until an implementation change begins |

The reviewed-and-approved snapshot was
`cfabf1a014521cd1897d6637eb2cb1997dbae6048e35860883a448c4bf183c51`; the ACCEPTED
snapshot above is its status-only superset (frontmatter status/approvers/decision
and the Status History row). The prior REVIEW snapshot was
`e421dc3bdeeead9d9ad7b504539b8a63cfde19380693ea2524aa7cf830a81d1b`, superseded
first by the `.agents/subagents` -> `.codex/subagents` correction
(`4d2a2858...`) and then by the `zom-v1` -> `zom` codec regeneration
(`cfabf1a0...`).

The previous draft baseline was
`a9d1995e60ac443e303ed01ac3f4bbe76a1e954feee1a9b0c192ba0fe1db314a`.
It did not define the final code-generation authority required by the
downstream LIR design, and its tracker recorded a stale current hash. No
approval from that snapshot carries forward.

## Discussion Record

### 2026-07-23 Final Code-Generation Authority Repair

Three independent pre-edit reviews found the same blocking contract:

- a public LIR/backend consumer could accept
  `VerifiedFinalTargetSelection`, while the downstream LIR design required a
  wrapper-owned operation;
- importing the downstream `LirAlgebraRegistry` and translator contract into
  RFC 0016 would create a circular proposal dependency;
- a runtime ABI manifest containing a semantic-context brand and physical
  function ABI keys could not exist before the final context and LIR ABI store;
  and
- a caller-provided monomorphization plan could not already bind target and
  runtime facts selected only inside the later target-dependent operation.

The repaired draft ends RFC 0016 at an acyclic authority boundary:

1. One process-root transaction publishes a complete
   `VerifiedTargetAuthorityBundle`.
2. The bundle contains the target registry, runtime capability snapshot,
   code-generation capability registry, and target-independent runtime ABI
   contract registry.
3. The exact bundle moves through the RFC 0012 preparation and final wrappers.
4. Final target verification is private.
5. The first target operation consumes the complete final wrapper and obtains
   one private operation state containing RFC 0012 cleanup ownership plus a
   move-only `VerifiedFinalCodegenAuthority`.
6. The downstream LIR RFC owns `lowerToLir`, the LIR algebra, LLVM translator
   contract, ABI-classifier registry, physical runtime ABI manifest, and
   monomorphization sequencing.
7. Later backend operations consume authority-carrying verified typestates;
   they do not return to the destroyed wrapper or reissue authority.

The draft also removes the controlled Linux/KVM coverage execution model. That
model was unrelated to target authority, occupied most of the proposal, and
was the source of repeated review returns. RFC 0016 now uses the repository's
native sanitizer, unit, integration, CTest, architecture, format, and CI gates.

### Review Transition Evidence

Three independent post-edit audits approved exact DRAFT SHA-256
`0f2cedcad9b06b6190657339d11eb2db61e453e2bf20d0252a3a36fd84166062`
without editing the proposal:

- backend contract: approved capability algebra, issuance order, ABI
  classifier ownership, and authority lineage;
- runtime authority: approved canonical capability predicates, logical runtime
  ABI lowering, callback ABI, bundle ownership, and physical-manifest handoff;
  and
- session governance: approved whole-wrapper consumption, one-shot issuance,
  RFC 0012 cleanup precedence, and closed bundle construction.

`python3 scripts/check-rfc.py`, `python3 scripts/check-format.py`, and
`git diff --check` passed before the status-only transition. The transition
changed only frontmatter, status history, index, and tracker state. The current
`REVIEW` snapshot then required every owner below to approve exact SHA-256
`fe1f2937b9426c0b0fe4729af50dc39930355d7fe7836de8b43c7501a3f4f59c`
before the RFC could move to `ACCEPTED`.

### 2026-08-11 Unversioned Contract Snapshot Rebinding

The repository-wide internal-contract replacement changed normative target,
runtime, and codec names after the prior REVIEW snapshot was recorded. No
required owner had approved that former snapshot. The current REVIEW snapshot
is SHA-256 `e421dc3bdeeead9d9ad7b504539b8a63cfde19380693ea2524aa7cf830a81d1b`.
Every required owner must review this exact snapshot; no earlier readiness or
review evidence carries forward.

## Required-Owner Review

Verdicts below are for the exact REVIEW snapshot
`e421dc3bdeeead9d9ad7b504539b8a63cfde19380693ea2524aa7cf830a81d1b`, from the
per-owner technical review conducted 2026-08-25 (see Review Result Record). Two
owners objected, so the RFC remains `REVIEW` and does not advance to `ACCEPTED`.

| Owner | Verdict (snapshot e421dc3b) | Required focus |
|---|---|---|
| `task-router` | OBJECT | Single-owner path census and routing consistency |
| `rfc` | Approve | RFC structure, dependency direction, status governance |
| `module-system` | Approve | Package wrapper ownership, phase authority, cleanup |
| `error-system` | Approve | Closed failure mapping and diagnostic ownership |
| `ir-backend` | OBJECT | LLVM admission, code-generation capability registry, downstream handoff |
| `runtime-memory` | Approve | Runtime capability and ABI contract registries |
| `spec-audit` | Approve | Architecture documentation and cross-RFC consistency |
| `verification` | Approve | Native tests, architecture gates, LLVM CI matrix |

Readiness-audit approvals are not required-owner approvals.

### 2026-08-25 Review Result Record

The eight required-owner focus reviews were performed against snapshot
`e421dc3b`. Six approved; two objected with blocking concerns. The RFC frontmatter
`approvers` stays empty and `decision` stays `TBD`: acceptance requires every
required owner to approve, and two have not. The blocking concerns are real and
were independently confirmed against the proposal bytes:

- `ir-backend` (blocking): the canonical codec golden preimages embed the
  revision-suffixed internal name `zom-v1`, banned by the design principles for
  internal generated artifacts. Confirmed by decoding the runtime-capability
  preimage at proposal line 1541 (`06 7a6f6d2d7631` = length-6 `zom-v1`) and the
  target-spec preimage at line 1552 (same `zom-v1` byte string). This also
  contradicts the prose at lines 977-994, which states `RuntimeAbiProfileId`'s
  only value is `Zom` and the initial registry is `zom -> {PanicAbort}` (profile
  bytes `zom`, no suffix) - a prose-versus-golden-bytes codec drift. The golden
  digests are derived over the `zom-v1` bytes, so both the name ban and the
  drift must be resolved together, and every affected preimage, length, and
  SHA-256 re-derived from the live encoder.
- `task-router` (blocking, now resolved): the Repository Impact
  ownership-routing row listed a nonexistent `.agents/subagents/**` path for the
  manifest, README, and per-owner files, while the live routing files are under
  `.codex/subagents/**`, leaving the real files covered by no row. This was a
  census gap from a repository-wide stale path (17 proposal RFCs and this
  README's acceptance gates also cited the old path). It has since been
  corrected repository-wide (`.agents/subagents` -> `.codex/subagents`),
  including in this proposal's Repository Impact row.

Non-blocking notes raised by approving owners: the module-system Repository
Impact row spells `driver/compiler-session.*` while the live path is
`driver/session/compiler-session.*`; and the `verification` owner flagged that
the CI contract pins exact LLVM `22.1.8` while sourcing a rolling Homebrew
`llvm@22` formula. Neither blocks acceptance.

Because acceptance requires unanimous required-owner approval and two owners
object, RFC 0016 does not transition to `ACCEPTED`. The `task-router` path
objection has been resolved by the repository-wide `.agents/subagents` ->
`.codex/subagents` correction (which also produced a new proposal snapshot,
below). The `ir-backend` `zom-v1` naming and codec-drift objection is not yet
resolved: it requires re-deriving the affected golden preimages, lengths, and
digests - including the embedded RFC 0010 `TargetSpecId` values in the profile
and registry records - from the live encoder, which this record does not
fabricate. RFC 0016 stays in `REVIEW`.

### 2026-08-25 Snapshot rebinding after path correction

The repository-wide `.agents/subagents` -> `.codex/subagents` correction edited
this proposal's Repository Impact row, so the REVIEW snapshot moved from
`e421dc3b...` to SHA-256
`4d2a285807604fcacade8ec372425c0deebc26087a8890f93799d395284fb988`. This change
resolves only the `task-router` objection; it does not touch the codec golden
values, so the `ir-backend` `zom-v1` objection still stands against the new
snapshot. Every required owner must review the new snapshot before acceptance;
the six prior approvals were against `e421dc3b` and do not carry forward
unchanged. Re-review is required.

### 2026-08-26 Codec fixtures regenerated from the live encoder

The `ir-backend` blocking objection is now resolved in substance. The canonical
codec golden fixtures in `Runtime, target, and registry codecs` embedded the
banned revision-suffixed internal name `zom-v1` as the runtime ABI profile
bytes, while the live encoder and the live test use `zom` with no suffix
(`products/zomlang/compiler/ir/target-registry.cc` `computeTargetSpecId`,
`encodeProfileRevision`, and the `zom.target-registry` preimage builder;
`products/zomlang/utils/zomc/zomc.cc` constructing the target spec with
`runtimeAbiProfile = "zom"`; and
`products/zomlang/tests/unittests/compiler/ir/target-registry-test.cc` asserting
the `zom` `TargetSpecId` `b5171e0d...`). Every affected preimage, byte length,
nested length prefix, and SHA-256 was re-derived with `zom` from a replica of
the live encoder algorithm, cross-checked against the live-test `b5171e0d`
target-spec digest and the prior `zom-v1` digests before any regenerated value
was trusted. The regenerated values are:

| Fixture | Old length | New length | Old SHA-256 | New SHA-256 |
|---|---|---|---|---|
| runtime-capabilities preimage | 56 | 53 | `69405a91bd7dead95a783941e6a0da363366b6a4fe5f1c5339d54bb62328146b` | `d28e24b427e44fc9fd1545956a3cc8ec8f70454c92b07a2728b5953a1a93a70e` |
| minimal target-spec oracle | 108 | 105 | `d972a7d918fc7d64c002b4245b8e6f5151d3c3c8507ae26e1ca1cbd1a026c90b` | `b5171e0d457c8ddac8eec7df5625c5edcec1b4b20d1f42945053ce95300c4c0b` |
| 87-byte-layout `Unwind` target-spec | 186 | 183 | `29fef3fa59a2166a285c371e02f4c28d0d25aa98e401ca2d0a9c2b3b5a390540` | `6c4ac5a58c4897f024830425a951e9a9b24386b3f1b71f69de512aaa0843fe7c` |
| 87-byte-layout `Abort` target-spec | 186 | 183 | `5a7710feedc3b1c63870b4c493676466952b0929730f0f97f41124b224bde54b` | `a9a0f57e1b80cfb0a3d734ab4f99f7e4fbfb41ef958e2b9882c3fd6ee5af31c9` |
| profile revision record | 197 | 194 | `a5d3e5b0806c3bf8b73d3e1bb6d3c76f6575f63da6a980a68ca0441c3e6b87df` | `53f341ba10c347ac125d38c8eaff7b8e0ed75ac0bae88d40b0fd398ef26ac7e0` |
| `zom.target-registry` preimage | 245 | 242 | `b519c204011b1d9d337d47a747d99c1f22bf47c4f09547e2fecee2c049eecbef` | `460c7b56abf177df2488b63f5e7349a6721084c0c858e49dd2e47841486ee06f` |

The 49-byte outer-registry-framing oracle
(`f25c636ba9240f1454cb006c39a7fce3443b7f2871de97923e01757e5048e272`) carried no
`zom-v1` bytes and is unchanged. The profile record's inner length prefix moved
from `0xc5` (197) to `0xc2` (194) as `zom-v1` (6 bytes) became `zom` (3 bytes).
The prior stale profile and registry records also embedded target-spec digests
that never reproduced from any target-spec preimage; the regenerated records
embed the regenerated standalone `Unwind` and `Abort` oracle IDs, so the profile
and registry preimages are now internally consistent with the live encoder.

Editing these bytes produces a new review snapshot. The proposal now reproduces
SHA-256 `cfabf1a014521cd1897d6637eb2cb1997dbae6048e35860883a448c4bf183c51`. The
prior approvals recorded against earlier snapshots do not carry forward, so
re-review of this snapshot is required. RFC 0016 remains `REVIEW`; frontmatter
`approvers` stays empty and `decision` stays `TBD`. Acceptance still requires the
required owners to review the new snapshot; the `ir-backend` codec defect that
blocked it is resolved here, but acceptance is recorded, not performed.

### 2026-08-26 Re-review of snapshot cfabf1a0 and acceptance

All eight required owners re-reviewed the `cfabf1a0` snapshot against their
focus areas. Seven approved immediately; `ir-backend` initially objected again
with a new, real cross-RFC defect: the `zom-v1` purge was not repository-wide -
RFC 0010's independent target-spec oracle still carried a 108-byte `zom-v1`
preimage with the stale digest `d972a7d9`, and RFC 0016's cross-reference to
"the RFC 0010 105-byte value" was therefore false against the authority it
overlays. That defect was fixed by regenerating the RFC 0010 oracle to the
105-byte `zom` form (digest `b5171e0d...`, identical to RFC 0016 and the live
`target-registry-test.cc`); `zom-v1` now survives only in this tracker's history
and in the banned-versioning gate's own detection fixture. `ir-backend` then
re-verified independently (re-hashing the RFC 0010 in-file preimage to
`b5171e0d...` at 105 bytes, confirming the cross-reference, and confirming the
RFC 0016 body still hashes to `cfabf1a0`) and approved.

| Owner | Verdict (snapshot cfabf1a0) |
|---|---|
| `task-router` | Approve |
| `rfc` | Approve |
| `module-system` | Approve |
| `error-system` | Approve |
| `ir-backend` | Approve |
| `runtime-memory` | Approve |
| `spec-audit` | Approve |
| `verification` | Approve |

With all eight required owners approving, RFC 0016 transitions `REVIEW` ->
`ACCEPTED`. The acceptance edit (frontmatter `status`/`approvers`/`decision`,
Status History row) changes the proposal bytes to the final ACCEPTED snapshot
SHA-256 `ec27f6d3015ed5f91d903671f225141832ef165eec8fd799845ae8913743baee`; the
reviewed-and-approved content is the `cfabf1a0` snapshot, of which the acceptance
edit is a status-only superset. `implementation` stays `TBD`: acceptance
authorizes implementation but no implementation slice has begun.

## Decision Record

`ACCEPTED` on 2026-08-26. All eight required owners (`task-router`, `rfc`,
`module-system`, `error-system`, `ir-backend`, `runtime-memory`, `spec-audit`,
`verification`) approved the `cfabf1a0` review snapshot after the two prior
blocking objections were resolved: the `.agents/subagents` -> `.codex/subagents`
routing correction (task-router) and the repository-wide `zom-v1` -> `zom`
codec-fixture regeneration across RFC 0016 and RFC 0010 (ir-backend), each
verified against the live encoder and the live `target-registry-test`. The
acceptance edit produced the final ACCEPTED snapshot
`ec27f6d3015ed5f91d903671f225141832ef165eec8fd799845ae8913743baee`.
Implementation remains unstarted; the Implementation Tracker slices stay blocked
until an implementation change begins.

## Implementation Tracker

| Slice | State | Required evidence |
|---|---|---|
| LLVM build and CI contract | Landed 2026-08-27 (`db8fe17e`) | LLVM 22.1.8 provisioned from source (glibc-2.36-native); `cmake/utils/llvm.cmake` behind `ZOM_ENABLE_LLVM_BACKEND` (default OFF) runs the fail-closed discovery gate with all seven `ZOM-CMAKE-LLVM-*` identifiers; positive fixture records exact 22.1.8 provenance + 11 components + X86/AArch64; 13 negative configure fixtures each assert their exact identifier before any target generates. Verified: sanitizer frontend unaffected (OFF), positive admits provisioned 22.1.8 (ON), 14/14 fixtures pass. |
| Runtime capability and ABI contract registries | Blocked by acceptance | Generated closed records, deterministic revisions, independent encoders, no ambient query |
| Target and code-generation capability registries | Blocked by prior slice | LLVM-backed admission, exact target associations, bundle publication |
| Package-session authority bundle | Blocked by prior slice | Exact RFC 0012 handoffs, move-only ownership, cleanup precedence |
| Final code-generation issuance | Blocked by prior slice | Private target selection, one-shot state, cross-wrapper/reuse rejection |
| Consumer cutover | Blocked by prior slice | Consuming first wrapper operation, authority-carrying later typestates, deleted public final-target consumer path |
| Repository verification | Pending | Sanitizer build, full CTest, architecture, RFC, format, determinism, and diff gates |

Implementation states may change only after the RFC decision permits work and
the named evidence is attached.

## Technical Closure Audit (2026-07-24)

Independent repository inspection confirmed the proposal's live dependencies:

- `CanonicalQueryKey` and the RFC 0017 snapshot/cancellation/provenance
  foundations exist in `products/zomlang/compiler/query/query-database.cc`
  and `query-types.h`.
- The `VerifiedTargetAuthorityBundle`, `VerifiedFinalCodegenAuthority`,
  code-generation capability registry, and target-independent runtime ABI
  contract registry are new types defined by this RFC; they do not require
  pre-existing production code.
- No diagnostic code conflicts are introduced; RFC 0016 does not register
  new `ZOMxxxx` codes.
- The dependency direction is acyclic: RFC 0016 imports only RFC 0012
  preparation/final wrappers and RFC 0016's own target-authority bundle;
  the downstream LIR design (RFC 0021) consumes RFC 0016's output, not
  the reverse.
- `python3 scripts/check-rfc.py` passed for all 23 proposal RFCs.

No blocking technical gaps found. Remaining work is required-owner approval
of the former REVIEW SHA-256
`fe1f2937b9426c0b0fe4729af50dc39930355d7fe7836de8b43c7501a3f4f59c`.
The current REVIEW snapshot requires a fresh audit and owner approval.

### 2026-08-12 Current Snapshot Readiness Audit

The current proposal bytes reproduce SHA-256
`e421dc3bdeeead9d9ad7b504539b8a63cfde19380693ea2524aa7cf830a81d1b`.
Every required-owner row remains pending for that exact snapshot, so this audit
does not record an approval, decision, status transition, or implementation
authorization.

The current target-registry unit suite passes its canonical target, Mach-O,
revision-bound selection, unavailable capability, foreign revision, and
projection-mismatch cases under the sanitizer build. It validates the existing
RFC 0010 registry surface only; it does not provide the context-bound bundle or
final code-generation authority defined by this RFC.

The repository currently has no LLVM package discovery or component-linking
contract. The local development environment resolves `llvm-config` and `clang`
at version `19.1.5`, while this RFC requires the exact `22.1.8` package,
explicit CMake provenance, and the `X86` and `AArch64` component inventory.
That mismatch is expected before implementation and must be corrected through
the accepted cross-platform CMake and CI transaction, not by admitting an
ambient local LLVM installation.

## Required-Owner Review Guide (2026-08-25)

This guide facilitates the pending required-owner review of the exact REVIEW
snapshot `e421dc3bdeeead9d9ad7b504539b8a63cfde19380693ea2524aa7cf830a81d1b`.
It records no approval, decision, status transition, or implementation
authorization; RFC frontmatter remains authoritative. Each owner still records
their own approve or object result against that snapshot in the Required-Owner
Review table. Section references are headings in
`docs/rfc/0016-context-bound-target-registry-verification.md`.

Cross-cutting evidence already on record for every owner: the 2026-07-24
Technical Closure Audit found no blocking technical gap (acyclic dependency
direction, no `ZOMxxxx` conflicts, `check-rfc.py` passing); the 2026-08-12
Readiness Audit reproduced the current snapshot hash and confirmed the LLVM
`19.1.5` vs required `22.1.8` mismatch is an expected pre-implementation state,
not a proposal defect.

- `task-router` - focus: single-owner path census and routing consistency.
  Review `Repository Impact` and confirm every listed path family maps to
  exactly one owner with no gap or overlap. Open question to confirm: the
  ownership-routing paths are written under `.codex/subagents/**` while the
  live tree uses `.codex/subagents/**`; confirm whether this is an intended
  post-acceptance move or a path that must be corrected before acceptance.
- `rfc` - focus: RFC structure, dependency direction, status governance.
  Review the frontmatter, `Summary`, and `Non-Goals`; confirm the additive
  overlay over RFCs 0006/0008/0010/0011/0012 imports only downstream-safe
  contracts and that `requires: [6, 8, 10, 11, 12]` is complete. Evidence: the
  Technical Closure Audit confirmed acyclic direction (0016 feeds 0021, never
  the reverse).
- `module-system` - focus: package wrapper ownership, phase authority, cleanup.
  Review `Session ordering`, `Preparatory and final context separation`, and
  `Registry construction failures`; confirm the one-authority-host rule and the
  move-only, at-most-once final code-generation issuance leave no context-free
  target-token path.
- `error-system` - focus: closed failure mapping and diagnostic ownership.
  Review `Registry construction failures` and `Target-spec admission`; confirm
  every failure maps to a closed registered diagnostic and that, per the
  Technical Closure Audit, no new `ZOMxxxx` codes are introduced or conflicting.
- `ir-backend` - focus: LLVM admission, code-generation capability registry,
  downstream handoff. Review `Target-spec admission` (`Triple`, `LLVM data
  layout`, `Object format`, `LLVM backend admission`), `Runtime, target, and
  registry codecs`, and `LLVM build and CI contract`; confirm the LLVM `22.1.8`
  API baseline, exact-byte data-layout identity, and that this RFC stops at the
  authority handoff without defining LIR or translation (owned by RFC 0021).
- `runtime-memory` - focus: runtime capability and ABI contract registries.
  Review `Panic-strategy mapping` and the runtime-capability/ABI-contract
  registry clauses in `Reference-Level Design`; confirm brand-and-revision
  binding and that the host runtime's ambient behavior is never accepted as
  target-runtime capability evidence (a stated Non-Goal).
- `spec-audit` - focus: architecture documentation and cross-RFC consistency.
  Review `Prior Art`, `Alternatives Considered`, and `Repository Impact`
  (`docs/design/**`); confirm the overlay claims match the current spec/design
  surface and cite mature prior art (LLVM data-layout, triples, Rust target
  specs).
- `verification` - focus: native tests, architecture gates, LLVM CI matrix.
  Review `LLVM build and CI contract`, `Architecture enforcement`, and
  `Determinism and ordering`; confirm the acceptance evidence in the
  Implementation Tracker (LLVM provenance, X86/AArch64 inventory, positive and
  negative configure tests, determinism and diff gates) is sufficient and
  testable. Note: these slices are correctly `Blocked by acceptance` and must
  not change state before the decision.

This guide does not alter the snapshot; the RFC proposal bytes are unchanged and
still reproduce `e421dc3bdeeead9d9ad7b504539b8a63cfde19380693ea2524aa7cf830a81d1b`.
