---
rfc: 51
title: Query Final-Seal And Text-Scanning Evidence
type: compiler
status: REVIEW
author: ZOM Compiler Team
review-manager: rfc
required-owners: [error-system, ir-backend, module-system, rfc, task-router, verification]
approvers: []
created: 2026-09-14
updated: 2026-09-14
area: compiler
requires: [17, 21, 28, 47]
supersedes: []
superseded-by: []
discussion: docs/rfc/tracking/0051-query-final-seal-and-text-scanning-evidence-review.md#discussion-record
decision: TBD
implementation: TBD
tracking-issue: docs/rfc/tracking/0051-query-final-seal-and-text-scanning-evidence-review.md#decision-record
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
and may not certify. It changes no code in its DRAFT state and weakens no
implemented guarantee.

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

Current production facts (2026-09-14):

- `sealInputs` / `prepareFinalSeal` / `publishFinalSeal` and
  `admitFinalSnapshot` live in `compiler/query/query-database.{h,cc}`;
- three frozen witness inputs and the complete-context input occupy fixed
  descriptor ordinals specified by RFC 0028;
- production `verifyFinalAuthority` is supplied by the module-graph query TU;
- fourteen capability descriptors require `FinalSealedSnapshot`; three use
  `AnySnapshot` (parse and IDE-facing entry points);
- the seal is invoked once per successful bind path and `checkSources` demands
  sealed module graph, provenance, bound module, owner body, and core role
  seed capabilities from the admitted snapshot.

Options:

1. **Retain.** The three-phase lock, witness re-derivation, token, and
   admission stay normative unchanged; this RFC only documents the
   threat-to-phase table.
2. **Simplify (audit-recommended direction, to be proven in review).**
   Collapse phases where the database's exclusive input transaction and
   database-identity/snapshot checks already provide the same serializability;
   retain the irreversibility guarantee and the fourteen descriptors'
   admission requirement; retain one independent witness recomputation but
   place it inside the existing transaction rather than a lock-free phase;
   delete ceremony steps the threat table shows to be redundant, each with an
   injected-race or foreign-snapshot mutation test proving coverage is kept.
3. **Remove the seal.** Treat every demand as revision-scoped like Salsa.
   This conflicts with fourteen live production consumers and with the
   materializer ordering guarantees; choosing it requires demonstrating that
   ordinary input revisions and the existing final authority verifier cover
   every threat, and migrating the sealed descriptors and their tests.

The selected option must preserve: no materializer can access interner or
memo state before complete authority; a sealed snapshot rejects later input
mutation with the typed failure; a foreign database snapshot cannot be
admitted.

### Part 2: Text-scanning evidence

Normative contradiction to resolve:

- RFC 0017 mandates the two query architecture scanners as completion gates.
- RFC 0021 mandates the IR architecture scanner and ties acceptance to it.
- RFC 0047 (LANDED, later) forbids treating source scanners as architecture
  evidence and deleted its own.
- Repository rule `.codex/rules/cpp-zc.md` already restates the RFC 0047
  position, but the earlier RFC text and the scripts remain.

Options:

1. **Reposition (recommended).** Keep every existing Python scanner in the
   tree and CTest as a regression/review aid, but:
   - remove scanner invocation as a normative acceptance criterion from RFCs
     0017 and 0021 via this RFC's overlay, replacing each claim with the
     compiled or executed evidence that actually establishes it (private
     constructors + the `tests/compile-fail/query-runtime/` fixtures; separate
     verifier TUs and link targets; `native-execution-cli` and the object
       emission integration tests for the link/run call path);
   - strip brittle positive substring assertions that require specific call
     sites (for example the required `ir::linkAndPublish(` call text) and keep
     only negative tripwires (banned domains, banned identifiers, banned
     includes) with self-test mutation fixtures;
   - label the scripts "regression aid" in their headers and in the gate
     documentation.
2. **Amend RFC 0047.** If reviewers judge the scanners truly prove the
   enumerated properties, then the later RFC's blanket rejection is wrong and
   RFC 0047 must be superseded on that point with a precise statement of which
   text properties are mechanically checkable and sufficient. The bar is to
   explain why a scanner that missed four live RFC 0010/0021 deviations still
   constitutes proof.
3. **Delete the scanners.** Keep only compiled and executed evidence. This
   loses cheap detection of marker/include drift; the self-test fixtures
   demonstrate that value, so deletion requires showing the drift they catch
   is caught elsewhere.

### Relationships

Overlays RFCs 0017, 0021, 0028, and 0047; note relationship to DRAFT RFC 0038
(final-sealed failure projection). No in-place normative edits before
acceptance; tracker entries bind the accepted text under the repository
convention.

## Repository Impact

| Area | Paths | Owner |
|---|---|---|
| Query database seal runtime | `compiler/query/query-database.{h,cc}`, `compiler/query/query-descriptor-schema.def` | module-system |
| Session seal production path | `compiler/driver/session/compiler-session.cc`, `compiler/driver/query/**` | module-system |
| Failure codes for seal and query failures | `compiler/query/query-types.h`, diagnostics projectors | error-system |
| IR architecture gate and backend evidence | `scripts/check-ir-architecture.py`, `compiler/lir/**`, `compiler/backend/llvm/**`, `utils/zomc/zomc.cc` | ir-backend |
| RFC text and process rules | `docs/rfc/0017-*`, `docs/rfc/0021-*`, `docs/rfc/0028-*`, `docs/rfc/0047-*`, `.codex/rules/cpp-zc.md`, `docs/rfc/README.md` | rfc |
| Gate routing and CTest registration | `.codex/subagents/**`, `tests/conformance/CMakeLists.txt` | task-router |
| Self-tests, compile-fail fixtures, integration tests | `scripts/check-*-architecture.py`, `tests/compile-fail/query-runtime/`, native execution integration tests | verification |

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
Part 2 option 1, edit the two RFC acceptance sections through the overlay,
relabel script headers, and migrate positive substring assertions to named
native tests in the same change; the scripts remain CTest targets throughout.
Each stage is independently revertible.

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
  failure test.
- The fourteen `FinalSealedSnapshot` descriptors still reject unsealed and
  foreign-snapshot demands under the native query tests.
- RFC 0047 and RFCs 0017/0021 no longer contradict; every former normative
  scanner claim cites compiled or executed evidence, and every removed
  positive substring assertion names the replacing test.
- All architecture scanner self-tests, compile-fail query fixtures, and the
  native execution integration tests pass.

## Implementation Plan

1. Produce the seal threat-to-phase table from the RFC 0028 test
   specifications and decide Part 1 in review.
2. Enumerate every normative scanner reference in RFCs 0017/0021 and map each
   to replacement evidence; decide Part 2.
3. Apply seal changes and/or gate repositioning in small gated commits with
   tracker entries.
4. Consolidate the evidence-class rule in `.codex/rules/cpp-zc.md`.

## Test Plan

- Build: `cmake --preset sanitizer && cmake --build --preset sanitizer`.
- Unit tests: full query database suite including one-shot/irreversibility,
  injected phase-race, and foreign-coordinate cases; module-graph and owner
  body query tests.
- Lit tests: unchanged.
- Conformance: architecture scanner `--check` and `--self-test`;
  `tests/compile-fail/query-runtime/`; `native-execution-cli` and object
  emission integration tests with the LLVM backend enabled.
- Generated files: none.
- Format: `python3 scripts/check-format.py`; `scripts/check-rfc.py`;
  `scripts/check-english-only.py`.

## Open Questions

- If Part 1 simplifies the ceremony, does the complete-context authority still
  need three separate frozen witness inputs, or can one composite verified
  input carry the same proof?
- Should scanner relabeling happen repo-wide in one change or per-RFC as the
  affected trackers close?

## Status History

| Date | Status | Notes |
|---|---|---|
| 2026-09-14 | DRAFT | Initial draft from the 2026-09-14 architecture audit. |
| 2026-09-14 | REVIEW | Frozen for required-owner review; tracker and SHA-256 snapshot bound |
