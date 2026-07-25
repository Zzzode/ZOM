# RFC 0020 Review And Implementation Tracker

This document records discussion, exact-snapshot owner review, decisions, and
implementation evidence for RFC 0020. The RFC frontmatter remains
authoritative for status and approvers. This tracker does not approve the
proposal or authorize implementation.

## Discussion Record

### 2026-07-20 Inverse-Key Implementation Discovery

RFC 0019 Phase 3 reached a provider-purity blocker while preparing
`NamedItemSyntax(DefinitionKey)` and definition-owner body queries:

- `DefinitionKey` contains only a one-way 32-byte digest;
- its complete `DefinitionIdentityRecord`, including the owning module, is
  retained inside `NamedDefinitionInventory(ModuleKey)`;
- `QueryContext` exposes only tracked query reads and no ambient registry;
  and
- the current query catalog has no tracked `DefinitionKey` to complete-record
  or module authority.

The implementation stopped. A provider cannot find the owning module without
scanning every active module, reading untracked session state, changing the
accepted query key, or discarding complete-record collision authority. RFC
0020 proposes a session-maintained tracked input and a complete replacement
protocol instead of any of those paths.

### 2026-07-20 Draft Contract

The draft defines
`ActiveDefinitionAuthorityInput(DefinitionKey) -> DefinitionIdentityRecord`
and `ActiveDefinitionAuthorityReadyInput(CompilationUnit) ->
ActiveDefinitionAuthoritySetFingerprint` as low-durability semantic inputs.
`CompilerSession` removes readiness with the first base mutation, demands all
active named-definition inventories, independently reconstructs the complete
authority map, and atomically replaces the map and readiness marker before
demanding named-item or owner-body roots.

The input is navigation authority only. `NamedDefinitionInventory` remains
membership authority, and both the named-item provider and verifier must check
exact digest and complete-record membership in the derived owning module.

### 2026-07-20 Draft Pre-Review Return

The first DRAFT pre-review returned the proposal before formal review. The
blocking findings were:

- missing authority was described as both `MissingInput` and `InactiveOwner`;
- active-module coverage had no exact snapshot query chain;
- base inputs could commit while stale authority remained visible;
- the named-item query descriptors and stale-key ledger handoff were
  incomplete;
- the Salsa algorithm link was invalid and the prior-art set was too narrow;
- current architecture docs were not assigned to `spec-audit`; and
- differential, benchmark, and architecture commands referred to missing or
  non-executable evidence.

No approval was recorded.

### 2026-07-20 Draft Repair

The repaired draft adds fail-closed authority clearing before any base-input
mutation, snapshot-consistent `ActiveCrates` and `ActiveModules` coverage,
prebuilt non-failing key-ledger publication, complete named-item descriptors,
an exact `MissingInput` to `InactiveOwner` normalization rule, Skyframe prior
art, `spec-audit` ownership, a focused fresh-database differential test, the
missing RFC 0017 performance artifacts, and executable architecture commands.
The repaired DRAFT requires complete new pre-review before entering `REVIEW`.

### 2026-07-20 Second Draft Pre-Review Return

The complete new pre-review approved `spec-audit` and returned the draft for
four remaining blockers:

- clearing and re-adding equal authority inputs across separate revisions
  destroyed their prior `changedAt` values;
- failed installation left a key ledger that could not idempotently erase
  already-cleared inputs on retry;
- named-item queries did not define the RFC 0018 canonical authority-occurrence
  selection among repeated definition sites; and
- performance paths, pre-implementation baseline capture, and worker-count
  permutations were incomplete or non-executable.

No approval from this DRAFT pre-review carries into formal review.

### 2026-07-20 Second Draft Repair

The second repair replaces authority clearing with a readiness barrier.
Readiness is removed with the first base mutation; prior per-definition entries
remain available only when current exact inventory membership proves them.
The final transaction erases and sets the complete map in one operation, which
preserves `changedAt` for equal values, and restores a fingerprinted readiness
marker atomically. Negative or contradictory lookups read readiness
conditionally, so unchanged positive queries do not gain a broad dependency.

The repair also defines RFC 0018 authority-occurrence ordering, moves the
performance runner and reviewed detached-worktree baseline before product
implementation, corrects the release build path, and requires focused
differential execution under worker counts 1, 2, and 8. A complete new
pre-review is required before `REVIEW`.

### 2026-07-20 Third Draft Pre-Review Return

The third pre-review approved the named-item occurrence and failure contracts
but found that the current query runtime cannot recover from an ordinary
missing-input read: `QueryContext::get` marks the provider context failed and
does not record an absence dependency. The draft's conditional readiness read
was therefore not implementable. Verification also required the diagnostic
coverage and lit execution-root gate pairs in the full command inventory.

No approval was recorded.

### 2026-07-20 Third Draft Repair

The draft now defines an input-only tracked `probeInput` operation. Present and
absent observations are both completed dependency reads; validation records the
observed presence alternative so both transition directions are red while
stable absence remains green without retaining tombstones. Ordinary required
input reads preserve their existing `MissingInput` behavior. Named-item queries
use probes for authority and conditional readiness, and the full verification
gate inventory now includes diagnostic coverage and lit execution-root checks.
A complete new pre-review is required before `REVIEW`.

### 2026-07-20 Final Draft Pre-Review Repair

The next pre-review approved Binder, module-system red-green, and verification
contracts but required explicit governance closure for the new generic query
runtime surface. The proposal now declares its `probeInput` and
presence-validation rules as a normative RFC 0017 amendment, requires both
architecture documents to record that contract, places the runtime API before
authority implementation in rollout order, and removes the complete API and
metadata surface on rollback when no other accepted caller exists. The DRAFT
requires one final complete pre-review before formal review.

### 2026-07-20 Formal Review Entry

All five required owners approved DRAFT readiness. RFC 0020 entered `REVIEW`
at exact proposal SHA-256
`4b06c2b226632b19244c5a48f74cce01af978b31f417209cfd19a41c74b84bfe`.
Pre-review conclusions do not approve the proposal. Every required owner must
approve this exact REVIEW snapshot; any normative proposal edit changes the
hash and voids the complete formal approval set.

## Owner Review Matrix

| Owner | Required review | Status | Evidence |
|---|---|---|---|
| `rfc` | metadata, prior art, scope, status, and replacement discipline | Approved | Exact REVIEW snapshot `4b06c2b2...4b84bfe`. |
| `binder-checker` | named-item membership, owner-body boundary, and failure behavior | Approved | Exact REVIEW snapshot `4b06c2b2...4b84bfe`. |
| `module-system` | input descriptors, readiness staging, red-green behavior, and provider purity | Approved | Exact REVIEW snapshot `4b06c2b2...4b84bfe`. |
| `spec-audit` | accepted-RFC consistency and current architecture documentation | Approved | Exact REVIEW snapshot `4b06c2b2...4b84bfe`. |
| `verification` | native regressions, adversarial mutations, benchmark, and full-gate evidence | Approved | Exact REVIEW snapshot `4b06c2b2...4b84bfe`. |

All required owners approved the exact REVIEW snapshot. No approval covers a
later normative design change; any such change requires a new governed RFC
transition.

## Decision Record

Accepted on 2026-07-20. All five required owners approved exact REVIEW
snapshot
`4b06c2b226632b19244c5a48f74cce01af978b31f417209cfd19a41c74b84bfe`
without a blocking objection. The accepted design adds tracked optional input
presence, a readiness-gated complete definition-authority projection, exact
owning-inventory membership, and RFC 0018 authority-occurrence selection.
Implementation may begin; no approval covers module scanning, registry reads,
untracked missing-input recovery, or a dual Binder path.

## Implementation Tracker

Implementation started after the accepted RFC moved through `ACCEPTED` to
`IMPLEMENTING`. A phase completes only with the native and adversarial evidence
listed in the accepted proposal.

| Phase | Scope | Status | Evidence |
|---|---|---|---|
| 1 | Performance runner, corpus, and reviewed pre-implementation baseline | Complete | `scripts/run-incremental-query-benchmarks.py`; closed corpus and baseline at revision `76e73196f3f9682bb1e5a6f88e7d77c00258a82f`; Release Binder median `332907000 ns`, MAD `1.44%`, peak RSS `5029888 bytes`; module projection median `28489000 ns`, MAD `2.17%`, peak RSS `2867200 bytes`; architecture check and self-test passed. |
| 2 | Tracked input probe and presence-aware dependency validation | Complete | `QueryContext::probeInput` and `QuerySnapshot::probeInput`; explicit `Present` and `Absent` dependency observations; codec-validated input keys; four presence transitions, equal present, cancellation, derived-kind rejection, context continuation, cloning, eviction, durability, and no-tombstone regressions; five Query runtime tests and incremental-query architecture check and self-test passed under the sanitizer build. |
| 3 | Complete definition-record decoder and fixed codec vectors | Complete | `DefinitionIdentityRecord::decodeCanonical` performs strict bounded standalone decoding. Native identity and authority tests cover the fixed SHA-256 vector, truncation, malformed lengths, trailing bytes, oversized records, and key-to-record digest mismatch under the sanitizer build. |
| 4 | Authority and readiness input kinds with strict codecs | Complete | `ActiveDefinitionAuthorityInput` and `ActiveDefinitionAuthorityReadyInput` use the canonical domains, low durability, strict key and value decoders, canonical set fingerprints, and input-only registration. Native tests cover malformed 31-byte and 33-byte keys and fingerprints plus exact round trips. |
| 5 | Readiness barrier, complete reconstruction, atomic replacement, and stale erasure | Complete | `ActiveDefinitionAuthorityProjectionState` removes readiness in the first base mutation, reconstructs the exact active crate, module, binding-order, and named-inventory closure, rejects foreign-module membership, atomically erases stale keys and installs the complete map plus readiness, preserves equal-input `changedAt`, and publishes its ledger only after commit. Native tests cover old snapshots, failed refresh, stale-ledger retry, active-set shrink, rename, move, and module isolation. |
| 6 | Named-item dependency, authority occurrence, and independent verification | Complete | Semantic `NamedItemSyntax` and revision-local `NamedItemProvenance` have strict bounded codecs and registered query descriptors. Providers and independent verifiers recover the module only from tracked authority, prove exact owning-inventory membership, conditionally probe readiness only on absent or contradictory authority, select the canonical authority occurrence independently, and reconstruct detached syntax and total provenance through separate Binder implementations. `CompilerSession` demands every active pair from a ready snapshot before binding. |
| 7 | Architecture mutations, native regressions, benchmark comparison, and RFC 0019 integration | In progress | Focused authority, named-item, and owner-projection regressions pass with worker counts 1, 2, and 8, including fresh-database equivalence, dependency-shape equality, range-only and body-only edits, add, delete, rename, cross-module edit, active-set shrink, module move, canonical owner census, exact alternative dependencies, and strict owner codecs. Incremental-query, Binder, and CompilerSession architecture checks and adversarial self-tests pass. Release benchmark comparison, complete repository verification, final documentation review, and landing evidence remain open. |

### 2026-07-20 Authority And Named-Item Implementation Evidence

The production session now owns one readiness-gated active-definition
authority projection. Every base-input mutation removes readiness in the same
transaction. Refresh reads the exact active crate and module closure, compares
it with `ModuleBindingOrder`, demands every active
`NamedDefinitionInventory`, independently validates every complete identity
record, and commits stale-key erasure, the complete replacement map, and the
set fingerprint in one transaction. Failed refresh publishes neither
readiness nor a partial ledger.

`NamedItemSyntax(DefinitionKey)` and `NamedItemProvenance(DefinitionKey)` are
the only named-item query roots. Their providers and verifiers recover the
owning module solely from the tracked complete record, demand exact membership
in that module's named-definition inventory, and use separate Binder
reconstruction paths. Positive lookups do not depend on the compilation-wide
readiness marker. Absent or contradictory authority reads readiness
conditionally and fails closed before replacement completion.

Evidence recorded for this slice:

- sanitizer `active-definition-authority-query-test`,
  `active-definition-authority-session-test`,
  `incremental-binding-query-adapter-test`, and
  `compiler-session-package-test` pass;
- the focused differential test repeats the edit sequence with worker counts
  1, 2, and 8 and byte-compares reused and clean values and dependency groups;
- active-set shrink erases removed keys, definition moves erase the prior key
  and install the new key, and an unrelated module edit preserves the observed
  named-item semantic `changedAt` through backdated-equal recomputation; and
- incremental-query, Binder, and CompilerSession architecture checks and
  adversarial self-tests pass.

This evidence unblocks RFC 0019 Phase 3. The first follow-on slice now
implements `ModuleBodyOwners`, `OwnerBodySyntax`, and
`OwnerBodyProvenance` with exact dependencies and independent verification. It
does not implement `BindModuleSkeleton`, `BindOwnerBody`,
`MaterializeOwnerBody`, aggregate publication, or production cutover.
