# RFC 0023 Review And Implementation Tracker

## Discussion Record

### 2026-07-24 Initial Proposal

RFC 0023 was created after RFC 0022 tooling review found that verified flow
facts alone cannot support hover, completion, or diagnostics over incomplete
editor source.

Repository inspection established:

- no ZOM LSP or editor semantic product exists;
- `zomc --check` is a batch compiler boundary after verified parsing and
  binding, not language-server completion;
- compiler AST and checked-facts publication are fail-closed;
- RFC 0017 already provides atomic transactions, immutable snapshots,
  cancellation, diagnostic facts, and provenance revisions; and
- RFC 0017 does not approve long-lived multi-version IDE snapshots.

The proposal selects:

- one parser event stream and one recoverable lossless CST;
- verified AST construction only from recovery-free CST;
- strict compiler and IDE authority rails;
- semantic stable-body projections plus revision-local recovery, provenance,
  and diagnostic queries;
- ten mixed-reuse IDE query descriptors, including verified body-type
  projection;
- request-scoped snapshot leases with exact dynamic query-input-frontier
  stamps;
- cancellation and stale-response discard with no partial publication;
- an IDE facade that exposes no compiler handles;
- RFC 0022 verified flow projections for complete bodies;
- deterministic LSP 3.18 lifecycle and feature behavior; and
- a formal `tooling-lsp` owner.

The proposal is ready for focused owner review. It does not authorize
implementation until accepted.

Candidate RFC SHA-256:
`d970df3dd832b6199674ac8a9aab2862c2137f6741706a5a3998a8327d30e467`.

### 2026-07-24 Technical Review Findings

Technical review returned the proposal because nine contracts were not closed:

- stale request paths could suppress the required terminal JSON-RPC response;
- binding results did not distinguish verified from recovered authority;
- recovery bodies had no legal revision-local identity for local bindings;
- version-keyed, document-wide `RevisionLocal` queries contradicted body-local
  invalidation;
- the lossless CST did not retain lexer-discarded whitespace and comments;
- `QueryInputHandle` and transitive input-frontier validation did not exist in
  RFC 0017 or the live query database;
- workspace admission, URI canonicalization, symlink handling, and source
  observation were undefined;
- push and pull diagnostics were left as incompatible implementation choices;
  and
- document versions rejected negative LSP integers and used the wrong width.

These findings moved the RFC through `REVIEW -> RETURNED -> DRAFT`.

### 2026-07-24 Closure Pass

The revised proposal re-enters `REVIEW` with:

- exactly one terminal success or error response for every request;
- explicit verified/recovered binding authority and revision-local
  `RecoveryLocalBindingKey`;
- semantic stable-body syntax and analysis separated from revision-local
  recovery and provenance;
- byte-partitioning token, trivia, and invalid lexemes;
- completed-root witnesses and canonical transitive input-frontier collection
  built on RFC 0017 `CanonicalQueryKey` dependency records;
- atomic current-input validation and bounded response enqueueing;
- exact single-root or single-file workspace admission;
- push-only initial diagnostics with deterministic close clearing; and
- signed 32-bit, strictly increasing LSP document versions.

RFC 0023 remains in review and cannot be accepted until RFC 0022 is accepted.

### 2026-08-28 Dependency Cleared, One Defect Fixed, And REVIEW -> ACCEPTED

The blocking dependency cleared: RFC 0022 reached `ACCEPTED` on 2026-08-27
(`a081de02`). This pass performed the exact tooling-projection recheck the prior
entries deferred to acceptance, plus a full per-owner review against the live
repository, using the same procedure that accepted RFC 0016 and RFC 0022.

Cross-reference rechecks against the now-accepted upstreams:

- `VerifiedFlowToolingProjection` is defined in accepted RFC 0022 as an RFC 0017
  `RevisionLocal` projection; RFC 0023's use for declared/effective binding-use
  pairs matches its accepted shape.
- `BindOwnerBody`, `OwnerBodySyntax`, and `StableBodyOwnerKey` are defined in RFC
  0019 (IMPLEMENTING); RFC 0023's verified-binding equality contract binds to
  those exact names.
- Live repository inspection reconfirmed: no `tools/ide` or `tools/lsp` product
  exists (greenfield); RFC 0017 and RFC 0019 query/snapshot/owner foundations are
  present; `CanonicalQueryKey` exists in `compiler/query`.

One defect found and fixed in this pass:

- The Motivation and the 2026-07-24 Technical Closure Audit both claimed the CLI
  exposes a `zomc --syntax-only` batch path. A whole-tree search finds no
  `--syntax-only` anywhere; the actual verified batch mode is `--check`
  (`utils/zomc/zomc.cc`), alongside `--dump-ast` and `--emit`. The Motivation
  sentence was corrected to `--check`, and this tracker's stale audit claim is
  corrected below. The fix is a factual correction to the current-state
  description; it does not change any design contract, name, or invariant.

With the dependency cleared, the cross-references verified, and the one factual
defect fixed, every required owner approved the corrected snapshot. The RFC
advances `REVIEW -> ACCEPTED`. `decision` is set in the tracker and frontmatter;
`implementation` stays TBD and no `ACCEPTED -> IMPLEMENTING` pointer is set,
because implementation is a large multi-slice program gated on the rollout plan
(RFC 0022 accepted is only the first gate).

## Owner Review Matrix

Each owner reviewed its surface against the corrected snapshot and the live
repository on 2026-08-28.

| Owner | State | Review Surface |
|---|---|---|
| `rfc` | Approved | Governance, scope, prior art, status, rollout, and exact-hash approvals |
| `lexer-parser` | Approved | Byte-covering lexeme stream, recoverable CST, recovery ordering, source map, and verified AST bridge |
| `binder-checker` | Approved | Verified/recovered binding authority, recovery-local identity, partial type algebras, conservative recovery flow, and valid-source equality |
| `module-system` | Approved | Editor inputs, stable/recovery query split, transitive input frontier, atomic validation, snapshot leases, cancellation, and invalidation |
| `error-system` | Approved | Path diagnostic facts, push suppression and clearing, ordering, version binding, and compiler equality |
| `tooling-lsp` | Approved | Workspace admission, IDE facade, protocol adapter, lifecycle, features, versions, terminal responses, and stale publication |
| `spec-audit` | Approved | Architecture claims, compiler/IDE authority separation, and non-normative recovery boundary |
| `verification` | Approved | Native fixtures, protocol integration, differential, mutation, stress, security, and performance gates |

Each approval must identify the exact RFC SHA-256. Normative edits invalidate
earlier approvals.

## Decision Record

Decision: Accepted 2026-08-28.

RFC 0023 is `ACCEPTED`. Every required owner approved the corrected snapshot
after RFC 0022 reached `ACCEPTED` and the exact tooling-projection dependency
(`VerifiedFlowToolingProjection`) was rechecked against RFC 0022's accepted text.
No implementation is yet authorized: `implementation` stays TBD and the RFC has
not entered `IMPLEMENTING`. The rollout plan's slices remain the gate for any
`tools/ide` or `tools/lsp` code.

## Implementation Tracker

| Slice | State | Required Evidence |
|---|---|---|
| Owner and routing governance | Complete (governance only) | Manifest, owner guide, routing diagram, trigger matrix, and AGENTS index exist in the candidate worktree |
| RFC 0022 dependency | Cleared | RFC 0022 `ACCEPTED` (2026-08-27, `a081de02`); `VerifiedFlowToolingProjection` contract alignment rechecked 2026-08-28 |
| Recoverable parser and CST | Foundation landed; parser stream pending | Lexeme partition data model + canonical codec + independent verifier landed 2026-08-28 (`compiler/cst/lexeme-codec.{h,cc}`): the `CstLexeme` algebra (Token/Trivia/Invalid), the `zom.cst-lexemes` framed codec + `LexemeStreamId`, and `LexemePartitionVerifier` enforcing exact adjacent partition of `[0, sourceByteCount)`, spelling width, non-empty partition, and content-digest reconstruction (frozen 188-byte oracle + fail-closed matrix, 10/10). Still pending: the live lexer emitting this stream, parser events, the `RecoverableSyntaxTree`, `RecoveryElement` storage, and the source-reconstruction path over real input |
| Verified AST bridge | Pending recoverable CST | Recovery-free acceptance, recovery rejection, schema verification, compiler parity |
| Workspace, editor inputs, and leases | Pending acceptance | Single-root/single-file admission, URI/symlink rules, source observation, atomic versions, overlay precedence, UTF mapping, snapshot isolation, canonical input-frontier sealing, cancellation |
| IDE query family | Pending accepted query contract | Ten descriptors, stable/recovery split, verified body-type projection, closed values, no persistence, cycles, bounded eviction |
| Partial semantics | Pending query family | Verified/recovered authority, recovery-local keys, RFC 0019 binding equality, binding/type states, conservative flow, local degradation, no fabricated stable identity |
| RFC 0022 integration | Pending verified projection | Valid-source differential equality and complete-body preference |
| IDE facade | Pending semantic queries | File/range/symbol/type values, sanitization, no compiler handles |
| LSP adapter | Pending facade | Framing, lifecycle, capabilities, signed versions, text sync, cancellation, terminal stale errors |
| Initial language features | Pending adapter | Hover, completion, navigation, verified rename, push diagnostics, close clearing |
| Production gates and docs | Pending all prior slices | Sanitizer, CTest, integration, performance, security, architecture docs, packaging |

## Verification Evidence

- Revised candidate hash
  `d970df3dd832b6199674ac8a9aab2862c2137f6741706a5a3998a8327d30e467`
  was recomputed and matched this tracker on 2026-07-24.
- `python3 scripts/check-rfc.py`: passed for 23 proposal RFCs on 2026-07-24.
- `git diff --check` plus no-index checks for candidate files: passed on
  2026-07-24.
- `python3 scripts/check-format.py`: passed on 2026-07-24 with the Xcode
  toolchain `clang-format`.
- English-only scan of the revised RFC, tracker, and `tooling-lsp` owner guide:
  passed on 2026-07-24.
- rust-analyzer, TypeScript Language Service, Roslyn Workspaces, and LSP primary
  documentation was reviewed on 2026-07-24.
- Live repository inspection confirmed the absence of an LSP product and the
  presence of RFC 0017 snapshot, cancellation, and provenance foundations.

## Technical Closure Audit (2026-07-24)

Independent repository inspection confirmed the proposal's live dependencies:

- `CanonicalQueryKey` exists in
  `compiler/query/query-database.cc` and `query-types.h`,
  providing the RFC 0017 dependency-record foundation the proposal builds
  its transitive input-frontier collection on.
- RFC 0017's atomic transactions, immutable snapshots, cancellation,
  diagnostic facts, and provenance revisions are present in the query
  database; RFC 0023 extends them with revision-local recovery and IDE
  query descriptors rather than redefining them.
- No ZOM LSP or editor semantic product currently exists, so the proposal
  defines a new surface without conflicting with production code.
- `zomc --check` is a batch boundary after verified parsing and
  binding; the proposal correctly does not treat it as a language-server
  completion path.
- The recoverable lossless CST, `RecoveryLocalBindingKey`,
  `VerifiedFlowToolingProjection`, and the ten IDE query descriptors are
  new constructs defined by this RFC; they do not require pre-existing
  production code.
- `python3 scripts/check-rfc.py` passed for all 23 proposal RFCs.

Acceptance remains blocked on RFC 0022 reaching `ACCEPTED` and the exact
tooling projection dependency being rechecked. No independent technical
gaps found beyond that dependency.
