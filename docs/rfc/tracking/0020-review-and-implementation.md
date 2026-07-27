# RFC 0020 Review And Implementation Tracker

This document records discussion, exact-snapshot owner review, decisions, and
implementation evidence for RFC 0020. The RFC frontmatter remains
authoritative for status and approvers. This tracker does not approve the
proposal or authorize implementation.

## Discussion Record

### 2026-07-20 Inverse-Key Implementation Discovery

RFC 0019 Phase 3 reached a provider-purity blocker while preparing
digest-routed definition syntax and definition-owner body queries:

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

The design defines a complete-record contextual definition-authority input and
complete-root readiness as low-durability semantic inputs.
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

### 2026-07-25 RFC 0025 Acceptance Synchronization

The RFC 0025 `R25-02` acceptance transaction is authorized by all twelve
required-owner approvals on exact proposal SHA-256
`4f4085c176a9f391115e12170da93af899e350fa92440d5a51577692faf8bad0`.
It synchronizes RFC 0020's current normative contract without changing this
RFC's `IMPLEMENTING` status.

| Binding | RFC 0025 Task Authority |
|---|---|
| Acceptance-time RFC synchronization | `R25-02` |
| Complete-context authority, readiness, and third-transaction installation | `R25-07` |
| Named-item and semantic caller cutover | `R25-08` |
| Native authority and semantic fixture migration | `R25-08T` |
| Contextual diagnostic publication | `R25-09E` |
| Read-set, transition, invalidation, and mutation evidence | `R25-07T` |
| Final integrated evidence | `R25-15` |

The acceptance evidence is the exact 12/12 RFC 0025 approval set. Existing
RFC 0020 evidence remains accurate for the fixed-key authority implementation
that preceded this transaction, but it is historical for the replaced keys;
the contextual replacement completes only through the listed RFC 0025 tasks.

### 2026-07-26 RFC 0026 Acceptance Synchronization

All four RFC 0026 required owners approved proposal SHA-256
`39df5d3f11dbdcb2e95056b1cd14fd5220a19688f31a3e3180230ad465a3f84d`.
Authority staging now depends on the RFC 0026 structural transaction, complete
graph and SCC roots, cyclic-component rejection, authority-only installation
transaction, and final-snapshot re-demand barrier. Completion evidence belongs
to RFC 0026 tasks `R26-05` through `R26-09` and the dependent RFC 0025 rows.

### 2026-07-27 RFC 0027 Acceptance Synchronization

Acceptance transaction `rfc0027-accept-20260727-e2f4ba5e` synchronizes RFC
0020 to exact RFC 0027 proposal SHA-256
`e2f4ba5eb777d3d70b8eb3ad75b18f5169afc61a83d989ccc61fc9d5d022f435`.
The current contract installs four complete contextual authority sequences and
`CompleteRootIdentityReadiness`, publishes all eight typed active-membership
queries, and seals the final snapshot after byte-equal graph, SCC, authority,
readiness, inventory, and header re-demands. RFC 0020 remains `IMPLEMENTING`;
completion evidence belongs to the RFC 0027 implementation tracker.

### 2026-07-27 RFC 0028 Acceptance Synchronization

Acceptance transaction `rfc0028-accept-20260727-944b68ff` synchronizes RFC
0020 to exact RFC 0028 proposal SHA-256
`944b68ffc0aff5576d079a243ff092d7d19fba5ffed65551dda8e68adf230db4`.
All eight membership descriptors now publish
`ActiveMembershipResult<Record>`, read readiness only on negative or
contradictory authority, compare complete authority, and enforce inherited
admission before membership and membership before interner access.

| Binding | RFC 0028 Task Authority |
|---|---|
| Routing and acceptance synchronization | `R28-11A`; `R28-12` |
| Closed runtime, transaction, seal, and admission types | `R28-13A`; `R28-13B` |
| Descriptor, membership caller, and native-test partitions | `R28-13C` through `R28-13G` |
| Indivisible source cutover | `R28-14` |
| Integrated evidence and truthful status transition | `R28-17` through `R28-19` |

RFC 0020 remains `IMPLEMENTING`. Existing authority-projection evidence does
not complete the membership and final-admission replacement; completion
authority belongs to the listed RFC 0028 tasks and their dependency edges.

### 2026-07-27 RFC 0029 Acceptance Synchronization

Acceptance transaction `rfc0029-accept-20260727-8d393a0c` synchronizes RFC
0020 to exact RFC 0029 proposal SHA-256
`8d393a0c6c00a7fad9ef086d3d25f5ed44300041afa9e1e1a4af5d68830fd3e7`.
The current named-item contract reads authority, conditional readiness,
selected source, parse, stable admission, named inventory, definition sites,
implementation sites, and named-item syntax in exact order. Its key-rejection
set contains only `InactiveOwner` and `MissingSelectedModuleSource`; missing
readiness is `ProviderRejected`, contradictory authority is
`InvariantViolation`, and child rejections retain their complete payload.

Definition-owned owner-body provenance first demands the typed named-item
branch and then the exact named-item syntax projection. `NoBody` alone
constructs `DefinitionWithoutBody`; the provider and rejection verifier use
separate executable-root admission algorithms. No owner-body capability reads
`OwnerBodySyntaxQuery`.

| Binding | RFC 0029 Task Authority |
|---|---|
| Stable authority schema, facts, and codecs | `R29-12A`; `R29-12B`; exact RFC 0030 atomic `R29-12AB` |
| Diagnostic prerequisite | `R29-12D` |
| Query types and token identity | `R29-13A` |
| Stable admission and exact named-item/owner-body failure contracts | `R29-13B` |
| Native conditional-read, rejection, decoder, and mutation gates | `R29-13C` |
| Atomic runtime landing and final evidence | `R29-14` through `R29-17` |

RFC 0020 remains `IMPLEMENTING`; the synchronized descriptor contracts are
pending their RFC 0029 implementation evidence.

## Implementation Tracker

Implementation started after the accepted RFC moved through `ACCEPTED` to
`IMPLEMENTING`. A phase completes only with the native and adversarial evidence
listed in the accepted proposal.

| Phase | Scope | Status | Evidence |
|---|---|---|---|
| 0 | RFC 0029 synchronized query identity and descriptor failure closure | Pending | RFC 0029 `R29-12A` through `R29-17`; no synchronized runtime implementation evidence is recorded |
| 1 | Performance runner, corpus, and reviewed pre-implementation baseline | Complete | `scripts/run-incremental-query-benchmarks.py`; closed corpus and baseline at revision `76e73196f3f9682bb1e5a6f88e7d77c00258a82f`; Release Binder median `332907000 ns`, MAD `1.44%`, peak RSS `5029888 bytes`; module projection median `28489000 ns`, MAD `2.17%`, peak RSS `2867200 bytes`; architecture check and self-test passed. |
| 2 | Tracked input probe and presence-aware dependency validation | Complete | `QueryContext::probeInput` and `QuerySnapshot::probeInput`; explicit `Present` and `Absent` dependency observations; codec-validated input keys; four presence transitions, equal present, cancellation, derived-kind rejection, context continuation, cloning, eviction, durability, and no-tombstone regressions; five Query runtime tests and incremental-query architecture check and self-test passed under the sanitizer build. |
| 3 | Complete definition-record decoder and fixed codec vectors | Complete | `DefinitionIdentityRecord::decodeCanonical` performs strict bounded standalone decoding. Native identity and authority tests cover the fixed SHA-256 vector, truncation, malformed lengths, trailing bytes, oversized records, and key-to-record digest mismatch under the sanitizer build. |
| 4 | Authority codec prerequisites | Complete | The bounded definition-record decoder, low-durability input registration, presence tracking, and canonical fingerprint primitives have strict native codec tests. The complete contextual transaction schemas and four-sequence readiness record remain assigned to RFC 0027. |
| 5 | Readiness barrier, complete reconstruction, atomic replacement, and stale erasure | Complete | `ActiveDefinitionAuthorityProjectionState` removes readiness in the first base mutation, reconstructs the exact active crate, module, stable graph, SCC, and named-inventory closure, rejects foreign-module membership, atomically erases stale keys and installs the complete map plus readiness, preserves equal-input `changedAt`, and publishes its ledger only after commit. Native tests cover old snapshots, failed refresh, stale-ledger retry, active-set shrink, rename, move, and module isolation. |
| 6 | Named-item dependency, authority occurrence, and independent verification | Complete | Semantic `NamedItemSyntax` and revision-local `NamedItemProvenance` have strict bounded codecs and registered query descriptors. Providers and independent verifiers recover the module only from tracked authority, prove exact owning-inventory membership, conditionally probe readiness only on absent or contradictory authority, select the canonical authority occurrence independently, and reconstruct detached syntax and total provenance through separate Binder implementations. `CompilerSession` demands every active pair from a ready snapshot before binding. |
| 7 | Architecture mutations, native regressions, benchmark comparison, and RFC 0019 integration | In progress | Focused authority, named-item, and owner-projection regressions pass with worker counts 1, 2, and 8, including fresh-database equivalence, dependency-shape equality, range-only and body-only edits, add, delete, rename, cross-module edit, active-set shrink, module move, canonical owner census, exact alternative dependencies, and strict owner codecs. Incremental-query, Binder, and CompilerSession architecture checks and adversarial self-tests pass. Release benchmark comparison, complete repository verification, final documentation review, and landing evidence remain open. |

### 2026-07-20 Authority And Named-Item Implementation Evidence

The production session now owns one readiness-gated active-definition
authority projection. Every base-input mutation removes readiness in the same
transaction. Refresh reads the exact active crate and module closure, verifies
the matching `ModuleGraph` and `ModuleGraphScc`, demands every active
`NamedDefinitionInventory`, independently validates every complete identity
record, and commits stale-key erasure, the complete replacement map, and the
set fingerprint in one transaction. Failed refresh publishes neither
readiness nor a partial ledger.

`ActiveDefinitionMembership(ContextualDefinitionKey)` publishes the complete
definition authority record only after exact owning-inventory and stable-header
verification. Positive lookups do not depend on complete-root readiness.
Absent or contradictory authority reads readiness conditionally and fails
closed before complete authority publication.

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
