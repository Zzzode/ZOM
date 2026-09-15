---
rfc: 51
title: Query Final-Seal And Text-Scanning Evidence
type: compiler
status: DRAFT
author: ZOM Compiler Team
review-manager: rfc
required-owners: [error-system, ir-backend, module-system, rfc, task-router, verification]
approvers: []
created: 2026-09-14
updated: 2026-09-15
area: compiler
requires: [17, 21, 28, 47]
supersedes: []
superseded-by: []
discussion: TBD
decision: TBD
implementation: TBD
tracking-issue: TBD
---

# RFC 0051: Query Final-Seal And Text-Scanning Evidence

## Summary

This RFC re-reviews two related pieces of compiler infrastructure. First, the
query runtime's final-seal ceremony (RFC 0028): a three-phase lock, witness
re-derivation, authority token, and sealed-snapshot admission that sits on
every successful bind path and gates fourteen capability descriptors. Second,
the status of source-text-scanning Python scripts as normative architecture
completion gates: RFCs 0017 and 0021 require such scripts, while the later,
landed RFC 0047 proves that source text cannot establish architecture evidence
and deleted its own comparable gates, leaving a direct normative
contradiction. This RFC asks reviewers to decide the proportional scope of the
seal ceremony and to settle one repository-wide rule for what a text scan may
and may not certify. It weakens no implemented guarantee; the seal
failure closure and named native evidence are preserved.

## Motivation

Both mechanisms answer the same question: what evidence is admissible to
prove that an architecture boundary actually holds?

The final-seal ceremony was designed to prevent post-seal input mutation,
sealing over an open transaction, foreign or stale snapshot admission
(TOCTOU between the lock-free re-demand and the re-lock), incomplete
context-root authority, and materializers running on unsealed partial state.
Unlike some other bespoke machinery in the repository, it is genuinely
consumed: production seals on every bind path (`CompilerSession::Impl::
sealFinalSnapshot` in `compiler/driver/session/compiler-session.cc`, demanded
from the bind staging path), and fourteen capability descriptors require
`FinalSealedSnapshot`. The question is whether the full three-phase ceremony is
the minimal mechanism that provides those guarantees, or whether the
database's own transaction and identity machinery already covers part of it.

The text-scanning situation is sharper. RFC 0017 requires
`check-incremental-query-architecture.py` and
`check-query-descriptor-architecture.py` to enforce producer/verifier path
prefixes and forbidden edges, and RFC 0021 requires `check-ir-architecture.py`;
these are CTest-registered completion gates. RFC 0047 (LANDED) examined the
same technique and concluded: "Text scanners can find naming patterns but
cannot prove C++ dependency direction, factory privacy, exhaustive projection,
cache equivalence, or runtime publication semantics" and "repository searches
are useful during review but are not proof and do not become RFC-specific
architecture gates". It deleted its own two scanners. The contradiction is
observable in practice: `check-ir-architecture.py` asserts literal substrings
such as a call to `ir::linkAndPublish(` in `utils/zomc/zomc.cc`, yet did not
detect that the live backend selects lowering by function/block-count
heuristics, consumes ownership-checked Built MIR instead of the existing
`VerifiedExecutableMir` accessor, uses the host triple instead of verified
target selection, and publishes no `VerifiedLirModule` - all deviations from
RFCs 0010/0021 that compiled tests, not text searches, would reason about.

## Goals

- Decide whether the final-seal ceremony is retained, simplified, or removed,
  based on which of its stated threats each of its phases uniquely covers.
- Replace the normative contradiction between RFC 0047 and RFCs 0017/0021 with
  one rule: text scans are review/regression aids; architecture completion is
  established by compile-time access control, target linkage, and native
  behavior tests.
- For every brittle substring assertion that loses normative status, name the
  compiled or executed evidence that replaces it.
- Keep the fourteen sealed descriptors' guarantees: no descriptor may gain a
  way to run on an unsealed or foreign snapshot as a result of this RFC.

## Non-Goals

- Redesigning the query database, red-green evaluation, durability, single
  flight, or cancellation.
- Removing the Python scripts (they retain regression value) or changing the
  in-tree compile-fail negative fixtures, which are already compiled evidence.
- Revisiting cross-stage content revisions (RFC 0050) or RFC granularity
  policy (RFC 0052).
- Changing diagnostic codes or the RFC 0047 three-rail failure model.

## Prior Art

- **Salsa.** Databases are parameterized by inputs; revisions are monotonic;
  there is no irreversible sealing step, because a new revision models changed
  inputs naturally. ZOM's IDE edit-reuse loop needs exactly that property and
  already runs unsealed.
- **rustc query system.** Red-green evaluation and `DepNode`s provide
  consistency within a revision; the global context and query modals enforce
  access at compile time. There is no seal because the compilation driver
  controls when a fresh session begins.
- **Roslyn.** Immutable `Solution` snapshots give every consumer a consistent
  view without a sealing ceremony; new documents produce a new snapshot.
- **rustc `tidy`.** A text/style policy checker that the project explicitly
  treats as hygiene, never as proof of an architecture property.
- **LLVM and MLIR.** Boundaries are enforced by library/target structure,
  TableGen-generated accessors, C++ access control, and FileCheck/lit behavior
  tests; no production architecture gate greps for required call substrings.
- **Buck2 and Bazel.** Correctness follows from the enforced build graph and
  hermetic actions, not from scanning action source text for expected calls.
- **RFC 0047 (already landed in this repository).** Its rejected alternative
  "Prove Layering With Source-Text Scanners" is directly on point and is the
  governing later decision.

## Guide-Level Explanation

A contributor adding a stage should reason:

1. Can this boundary be expressed as a C++ access rule (private constructor,
   friend verifier, unnameable token type, separate link target)? Then
   compile-fail fixtures prove it for every build, and no script is needed.
2. Is the claim about runtime behavior (a function is actually called on the
   production path, a query refuses an unsealed snapshot, a binary executes and
   returns the expected code)? Then a native unit or integration test proves
   it, because it executes the claim.
3. Is the goal to catch accidental marker deletion, banned file naming, or
   obvious include drift during review? A Python scanner is a cheap and useful
   tripwire, kept in CI, but it is labeled a regression aid and never appears
   in an RFC acceptance criterion as the sole evidence.

For the seal, the contributor should see one documented table mapping each
threat (post-seal mutation, open-transaction seal, foreign snapshot,
phase-2/3 race, incomplete context authority) to the one mechanism that covers
it, so that any ceremony step without a unique threat can be removed with its
test.

## Reference-Level Design

### Part 1: Final-seal evidence

Current production facts (2026-09-15):

- `sealInputs` / `prepareFinalSeal` / `publishFinalSeal`, the fail-closed
  `rejectFinalSeal` and `validateSnapshotAdmission` halves, and
  `admitFinalSnapshot` live in `compiler/query/query-database.{h,cc}`;
- three frozen witness inputs and the complete-context input occupy fixed
  descriptor ordinals specified by RFC 0028;
- production `verifyFinalAuthority` is supplied by the module-graph query TU,
  with separate success and failure witness recomputation paths;
- fourteen capability descriptors require `FinalSealedSnapshot`; three use
  `AnySnapshot` (parse and IDE-facing entry points);
- the seal is invoked once per successful bind path; `checkSources` demands
  the sealed module graph and core capabilities directly, while provenance
  and owner-body capabilities are demanded transitively by the sealed
  materializer providers;
- the success/failure closure described by DRAFT RFC 0038 is already
  implemented: `FinalSnapshotClosureKind { Success, Failure }` and the
  descriptor `FinalFailureProjection { None, Source, Key, SourceOrKey }`
  column control how a sealed failed compilation projects source/key
  rejections. RFC 0038's status is stale relative to this landed machinery.

Any retain/simplify decision must preserve BOTH closures, not only the
success snapshot: a sealed rejected compilation must still authorize exactly
the source/key projections its descriptors declare, and success/failure
witnesses must not be interchangeable.

### Seal threat-to-mechanism table (required analysis input)

| Threat | Mechanism that must survive simplification |
|---|---|
| Second seal after publication | One-shot seal admission and typed already-published failure |
| Seal over an open input transaction | Exclusive input transaction plus the open-transaction failure |
| Foreign or stale snapshot admission | Database identity and snapshot coordinates checked on admission |
| Phase-2/3 TOCTOU race | Independent witness recomputation under the admission lock, covered by injected-race tests |
| Incomplete context-root authority | Complete-context input and descriptor-owned final authority verifier |
| Materializer running before authority | Sealed-descriptor demand fails before provider code, memo lookup, and interner access |
| Sealed failed compilation projecting wrong rejection | `FinalSnapshotClosureKind::Failure` and the descriptor `FinalFailureProjection` mapping |

Options:

1. **Retain.** The three-phase lock, both success/failure witness paths,
   token, and admission stay normative unchanged; this RFC documents the
   table and reconciles RFC 0038's stale status.
2. **Simplify (audit-recommended direction, to be proven in review).**
   Collapse phases where the database's exclusive input transaction and
   database-identity/snapshot checks already provide the same serializability;
   retain the irreversibility guarantee, the fourteen descriptors' admission
   requirement, and both closure kinds; retain one independent witness
   recomputation but place it inside the existing transaction rather than a
   lock-free phase; delete ceremony steps the threat table shows to be
   redundant, each with a surviving injected-race, foreign-snapshot, or
   sealed-failure projection mutation test proving coverage is kept.
3. **Remove the seal.** Treat every demand as revision-scoped like Salsa.
   This conflicts with fourteen live production consumers, the failure
   closure, and the materializer ordering guarantees; choosing it requires
   demonstrating that ordinary input revisions and the existing final
   authority verifier cover every threat in the table, and migrating the
   sealed descriptors and their tests.

The selected option must preserve: no materializer can access interner or
memo state before complete authority; a sealed snapshot rejects later input
mutation with the typed failure; a foreign database snapshot cannot be
admitted; and a sealed failure still projects exactly its declared
source/key rejections.

### Part 2: Text-scanning evidence

Normative contradiction to resolve. The Round-1 review established the
conflict is repository-wide, not limited to two RFCs: scanner-as-proof text
also appears in RFCs 0002, 0003, 0004, 0005, 0007, 0008, 0011 (LANDED), 0016,
0018, 0020, 0024, 0025, 0028, 0030, 0032, and 0042. Acceptance of this RFC
therefore adopts one generic supersession rule covering every listed RFC
rather than editing only 0017 and 0021:

> For every RFC acceptance criterion that makes a source-text scan the sole
> proof of an architecture property, RFC 0051 governs: the scan is a
> regression aid and the property is established by compiled or executed
> evidence. Each implementing change amends its owning RFC/tracker row and
> names the replacement evidence; this RFC lists every affected RFC above.

The immediate sharp contradiction remains:

- RFC 0047 (LANDED) forbids treating source scanners as architecture evidence
  and deleted its own, while LANDED RFC 0011 and the IMPLEMENTING RFCs above
  still require scanners as proof.
- The IR scanner asserts literal call-site substrings and missed four live
  RFC 0010/0021 backend deviations.

### Positive-marker disposition (bound into this RFC)

Every current positive substring assertion is classified so the acceptance
criterion is executable:

| Assertion class | Examples | Disposition |
|---|---|---|
| Runtime call path | `ir::linkAndPublish(`, `runNativeExecutable()`, run compatibility and subprocess call text in `zomc.cc` | Replace with executed evidence: `native-run-cli`, `native-execution-cli`, and a required new negative test for cross-target run rejection |
| CMake build wiring | `ZOM_RUNTIME_ENTRY_OBJECT`, `ZOM_HOST_LINKER`, `zom-runtime-entry`, `add_library(zom-runtime-entry OBJECT ...)` | Keep as explicitly labeled build-definition tripwires until a CMake-target/link-graph check exists; they are build-graph claims no native unit currently proves |
| Runtime entry assembly | `.globl _start`, `call zom.module_init`, direct syscall in `entry-linux-x86_64.S` | Keep as a labeled tripwire until an entry-symbol/executable-inspection assertion covers the symbol content (the inspector currently checks for the symbol, not the asm body) |
| Banned includes / identifiers / domains | internal headers (`invoke-linker-internal.h` and peers), alternate MIR domains, versioned markers | Retain as negative tripwires until a real C++ boundary (restricted link target or private-header visibility) makes the inclusion fail to compile/link; they are the only guard on some internal surfaces today |
| Descriptor inventory consistency | on-disk schema vs generated inventory | Retain; it cross-checks the native compile-fail fixtures and generated bindings, not a runtime claim |

Options:

1. **Reposition (recommended).** Keep every existing Python scanner in the
   tree and CTest as a regression/review aid, apply the generic supersession
   rule, replace runtime call-path assertions with the named executed tests
   (adding the missing cross-target rejection test), and keep the build/asm/
   banned-include classes as labeled tripwires pending real compile/link
   boundaries. Scripts gain a "regression aid, not architecture proof"
   header; the assertion-to-test table above is the binding map.
2. **Amend RFC 0047.** If reviewers judge the scanners truly prove the
   enumerated properties, then the later RFC's blanket rejection is wrong and
   RFC 0047 must be superseded on that point with a precise statement of which
   text properties are mechanically checkable and sufficient. The bar is to
   explain why a scanner that missed four live RFC 0010/0021 deviations still
   constitutes proof.
3. **Delete the scanners.** Rejected for the banned-include and build-wiring
   classes until real boundaries exist, because removing them would leave the
   internal-header minter and CMake entry wiring unguarded.

### Relationships

Overlays the scanner-related text of all RFCs listed in Part 2 plus RFCs 0028
and 0038 for the seal; RFC 0038's closure is production-implemented and is
reconciled here rather than re-designed. No in-place normative edits before
acceptance; tracker entries bind the accepted text under the repository
convention.

## Repository Impact

| Area | Paths | Owner |
|---|---|---|
| Query database seal runtime | `compiler/query/query-database.{h,cc}`, `compiler/query/query-descriptor-schema.def` | module-system |
| Session seal production path | `compiler/driver/session/compiler-session.cc`, `compiler/driver/query/**` | module-system |
| Failure codes for seal and query failures | `compiler/query/query-types.h`, diagnostics projectors | error-system |
| IR architecture gate and backend evidence | `scripts/check-ir-architecture.py`, `compiler/lir/**`, `compiler/backend/llvm/**`, `utils/zomc/zomc.cc` | ir-backend |
| RFC text and rfc skill | `docs/rfc/00{02,03,04,05,07,08,11,16,17,18,20,21,24,25,28,30,32,42,47}-*`, `.codex/skills/rfc/SKILL.md`, `.codex/rules/cpp-zc.md`, `docs/rfc/README.md` | rfc |
| Gate routing and subagent policy | `.codex/subagents/task-router.md`, `.codex/subagents/manifest.yaml`, `tests/conformance/CMakeLists.txt` | task-router |
| Self-tests, compile-fail fixtures, integration tests | `scripts/check-*-architecture.py`, `tests/compile-fail/query-runtime/`, native execution and cross-target rejection tests | verification |

## Security And Safety Impact

The seal guards integrity of materialized compiler state, not memory safety.
Any simplification must preserve the typed failure ordering (seal violations
fail before provider code, memo lookup, and interner access) and the
all-or-nothing publication semantics; mutation tests with injected phase
races and foreign coordinates must continue to fail. Repositioning scanners
must not delete the negative compile fixtures that prove query internals are
unnameable; those are compiled evidence and are unaffected.

## Drawbacks And Risks

- Simplifying the seal could introduce a seal-time race if a step is removed
  without mapping its threat; mitigation is the existing injected-race test
  suite, expanded per removed step, not prose assurance.
- Repositioning scanners reduces the cheap, fast signal that catches marker
  deletion before linking; mitigation keeps the scripts in CI as aids and
  strengthens the negative self-tests.
- Overlaying four RFCs adds process overhead; the alternative (leaving the
  contradiction) leaves implementers choosing which LANDED rule to follow.
- Removing positive call-site assertions changes what the IR gate enforces;
  any replaced assertion must list the concrete native test that observes the
  behavior.

## Alternatives Considered

- **Do nothing and tolerate the contradiction.** Rejected: RFC 0047's
  reasoning is correct and the missed backend deviations demonstrate the
  scanner's evidential limit; two conflicting normative rules erode the gate
  system.
- **Replace text scans with a Clang-based dependency graph tool.** Possible in
  a later tooling RFC; it would still be evidence about includes, not runtime
  publication semantics, and does not resolve the substring-assertion problem.
- **Make the seal optional per compilation mode.** Rejected: the project
  deliberately avoids mode-dependent internals; the IDE unsealed path already
  has its own `AnySnapshot` descriptors rather than a mode flag.

## Compatibility And Rollout

Internal infrastructure only; no CLI, language, or diagnostic surface. Under
Part 1 option 2, ship each ceremony simplification with its race mutation
tests in one change and keep the typed failure names where possible. Under
Part 2 option 1, apply the generic supersession rule to every listed RFC via
its owning tracker, relabel script headers, migrate the runtime call-path
assertions to named native tests (adding the cross-target run-rejection test),
and retain the build/asm/banned-include tripwires as explicitly labeled aids
in the same change; the scripts remain CTest targets throughout. Each stage is
independently revertible.

## Documentation And Teaching Plan

Update `docs/design/query-runtime.md`, `docs/design/architecture.md`, and the
affected RFC trackers; add a short "evidence classes for architecture
boundaries" subsection to `.codex/rules/cpp-zc.md` consolidating the three-tier
rule (compile, execute, scan) so future RFCs cite one source.

## Operational Readiness

CI impact: scanner CTest labels remain; their role in merge gating is
unchanged in practice (they still run), only their normative status changes.
No release or runtime concern.

## Acceptance Criteria

- One approved option per part, bound into the overlaid RFC trackers.
- A threat-to-mechanism table for the seal exists and every retained ceremony
  step covers a unique threat; every removed step has a surviving injected
  failure test, and sealed Success/Failure closures both remain correct.
- The fourteen `FinalSealedSnapshot` descriptors still reject unsealed and
  foreign-snapshot demands under the native query tests, and sealed-failure
  source/key projection stays descriptor-authorized.
- No RFC acceptance criterion makes a source scan the sole architecture
  proof; the generic supersession rule is applied per owning tracker for every
  RFC listed in Part 2; every migrated runtime call-path assertion names the
  replacing executed test, including the new cross-target run-rejection test;
  retained build/asm/banned-include tripwires are labeled as aids and their
  eventual real-boundary replacement is named.
- All architecture scanner self-tests that exist, compile-fail query fixtures,
  and the native execution integration tests pass. The lexer architecture
  script, which currently has no self-test, is either given one or explicitly
  excluded from the self-test requirement.

## Implementation Plan

1. Produce the seal threat-to-phase table from the RFC 0028 test
   specifications, including the RFC 0038 failure closure, and decide Part 1.
2. Enumerate every normative scanner reference across all RFCs listed in Part
   2 and map each to replacement evidence or its retained-tripwire class;
   decide Part 2.
3. Apply seal changes and/or gate repositioning in small gated commits with
   per-RFC tracker entries.
4. Consolidate the evidence-class rule in `.codex/rules/cpp-zc.md`.

## Test Plan

- Build: `cmake --preset sanitizer && cmake --build --preset sanitizer`.
- Unit tests: full query database suite including one-shot/irreversibility,
  injected phase-race, foreign-coordinate, and sealed-failure projection
  cases; module-graph and owner body query tests.
- Lit tests: unchanged.
- Conformance: architecture scanner `--check` and `--self-test` where
  implemented; `tests/compile-fail/query-runtime/`; `native-execution-cli`,
  `native-run-cli`, object-emission tests, and the new cross-target
  run-rejection negative test with the LLVM backend enabled.
- Generated files: none.
- Format: `python3 scripts/check-format.py`; `python3 scripts/check-rfc.py`;
  `python3 scripts/check-english-only.py` (relabeled script headers stay
  ASCII English).

## Open Questions

- If Part 1 simplifies the ceremony, does the complete-context authority still
  need three separate frozen witness inputs, or can one composite verified
  input carry the same proof?
- Scanner relabeling happens per-RFC as affected trackers close, with the
  generic rule landing first; confirm no implementation work is gated on the
  relabeling itself.

## Status History

| Date | Status | Notes |
|---|---|---|
| 2026-09-14 | DRAFT | Initial draft from the 2026-09-14 architecture audit. |
| 2026-09-14 | REVIEW | Frozen for required-owner review; tracker and SHA-256 snapshot bound |
| 2026-09-15 | RETURNED | Round 1: Part 1 omitted the landed RFC 0038 Success/Failure closure; Part 2 scope covered only 2 of ~15 affected RFCs and left build/asm markers without replacement disposition. |
| 2026-09-15 | DRAFT | Revised threat table with failure closure; repository-wide supersession rule; bound positive-marker to native-test map; corrected owner/impact rows and tests. |
