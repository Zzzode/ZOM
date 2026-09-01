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

Decision: Accepted 2026-08-28. Implementation started 2026-08-28.

RFC 0023 is `IMPLEMENTING`. Every required owner approved the accepted snapshot
after RFC 0022 reached `ACCEPTED` and the exact tooling-projection dependency
(`VerifiedFlowToolingProjection`) was rechecked against RFC 0022's accepted text.
The first authorized implementation slice, the verified lexeme-partition model,
landed in `ed8a797f` and established the implementation pointer. Subsequent
commits added the recovery sequence, live lexer bridge, precise trivia
classification, and parse-eligibility gate without changing the accepted RFC
contract.

No `tools/ide` or `tools/lsp` product exists yet. The next production-boundary
replacement remains the parser event stream plus `RecoverableSyntaxTree`,
followed by verified `ast::Tree` construction from recovery-free CST.

## Implementation Tracker

| Slice | State | Required Evidence |
|---|---|---|
| Owner and routing governance | Complete | Manifest, owner guide, routing diagram, trigger matrix, and AGENTS index are present |
| RFC 0022 dependency | Cleared | RFC 0022 `ACCEPTED` (`a081de02`); `VerifiedFlowToolingProjection` alignment rechecked 2026-08-28 |
| Lexeme partition model and verifier | Landed (`ed8a797f`) | `compiler/cst/lexeme-codec.{h,cc}`; closed Token/Trivia/Invalid algebra; `zom.cst-lexemes` codec; `LexemePartitionVerifier`; frozen 188-byte oracle and 10/10 fail-closed matrix |
| Recovery sequence model and verifier | Landed (`69292ead`) | `compiler/cst/recovery-codec.{h,cc}`; MissingToken/MissingSubtree/SkippedTokens algebra; canonical order; stream binding; frozen 128-byte oracle and 10/10 fail-closed matrix |
| Live lexer-to-lexeme bridge | Landed (`c6df8569`, `b0200614`) | `buildLexemeStreamFromTokens` consumes the production lexer's tokens, reconstructs the exact source, retains inter-token/trailing trivia, and classifies Whitespace/LineComment/BlockComment; integration coverage 6/6 |
| Parse eligibility gate | Landed (`7c438f0d`) | `parseEligibility` composes verified lexemes and recovery, rejects recovery, Invalid lexemes, error diagnostics, and cross-stream recovery; 5/5 tests |
| Clean parser event stream and `RecoverableSyntaxTree` | Candidate in worktree | `ParserSyntaxFactory` emits the closed construction-event algebra through `ParserEventBuilder`; the live lexeme stream and verified empty recovery sequence are retained in one immutable `RecoverableSyntaxTree`; parser sources contain no `ast::TreeBuilder`; mutation coverage rejects every event-result family |
| Verified AST bridge | Candidate in worktree | `ParseSyntaxVerifier` applies eligibility, independently replays node/list/intern/root events, verifies event identities and RFC 0002 schema, and is the production compiler path used by `Parser::parse()`; 918-test AST conformance parity passes |
| Explicit recovery production and parser publication | Candidate in worktree | Parser recovery frames produce SkippedTokens or MissingSubtree, parser diagnostic summaries produce MissingToken/MissingSubtree, Unknown and uncovered invalid bytes remain Invalid lexemes, records are canonically sorted/deduplicated and independently verified, and `Parser::takeRecoverableSyntax()` publishes the single-use immutable result after successful or failed traversal |
| Diagnostic-bound invalid lexemes and IDE query publication | Pending | Replace the current deterministic opaque Invalid digest with the exact retained ParserDiagnosticFact binding, publish recoverable syntax through the IDE query/input lease boundary, and prove stale/cancellation behavior |
| Workspace, editor inputs, and leases | Pending verified AST bridge | Single-root/single-file admission, URI/symlink rules, source observation, atomic versions, overlay precedence, UTF mapping, snapshot isolation, canonical input-frontier sealing, cancellation |
| IDE query family | Pending workspace/snapshot inputs | Ten descriptors, stable/recovery split, verified body-type projection, closed values, no persistence, cycles, bounded eviction |
| Partial semantics | Pending IDE query family | Verified/recovered authority, recovery-local keys, RFC 0019 binding equality, binding/type states, conservative flow, local degradation, no fabricated stable identity |
| RFC 0022 integration | Pending verified tooling projection | Valid-source differential equality and complete-body preference |
| IDE facade | Pending semantic queries | File/range/symbol/type values, sanitization, no compiler handles |
| LSP adapter | Pending facade | Framing, lifecycle, capabilities, signed versions, text sync, cancellation, terminal stale errors |
| Initial language features | Pending adapter | Hover, completion, navigation, verified rename, push diagnostics, close clearing |
| Production gates and docs | Pending all prior slices | Sanitizer, CTest, integration, performance, security, architecture docs, packaging |

### 2026-08-30 Tracker Reconciliation

This tracker is reconciled to the production tree through `7c438f0d`. The live
lexer bridge and parse-eligibility gate were already committed but were not
represented in the implementation table. This update changes no RFC contract or
status. It records the exact landed evidence and makes the remaining parser/CST
direct replacement explicit.

### 2026-08-30 Parser Event And Verified AST Candidate

The current worktree directly replaces parser-owned `ast::TreeBuilder`
construction on the clean compiler path. `ParserSyntaxFactory` now records the
schema-generated construction calls as one closed event stream. The same parser
traversal's buffered lexer tokens produce the verified byte-covering lexeme
stream; the clean path binds the verified empty recovery sequence. These values
form `RecoverableSyntaxTree`, and `ParseSyntaxVerifier` is the only production
component that constructs `ast::Tree` by independently replaying the event
stream and running the RFC 0002 schema verifier.

The candidate now also covers malformed-source parser publication. Recovery
frames and retained parser diagnostics produce canonical MissingToken,
MissingSubtree, and SkippedTokens records; invalid source bytes remain Invalid
lexemes; and `Parser::takeRecoverableSyntax()` exposes the single-use immutable
result while compiler AST publication remains fail-closed. The Invalid lexeme's
diagnostic field is still a deterministic opaque digest rather than the exact
retained ParserDiagnosticFact, and no IDE query/snapshot lease consumes the
result yet. No second parser, grammar, or AST-derived CST rail was introduced.

## Verification Evidence

- `ed8a797f`: lexeme codec/verifier oracle and fail-closed matrix, 10/10.
- `69292ead`: recovery codec/verifier oracle and fail-closed matrix, 10/10.
- `c6df8569` plus `b0200614`: production lexer bridge, exact source
  reconstruction, and precise trivia classification, 6/6.
- `7c438f0d`: parse-eligibility composition and rejection matrix, 5/5.
- Current worktree candidate: `parser-event-stream-test` covers successful event
  replay, mutated event rejection, schema rejection, recovery rejection, and
  parser-error rejection; parser unit tests and the 918-case AST conformance
  suite pass through the new production path.
- Current malformed-source candidate: parser tests prove missing-semicolon
  MissingToken production, Invalid byte retention, SkippedTokens production,
  single-use recoverable publication after rejection, clean-source publication,
  and continued compiler-AST rejection.
- `ctest --preset default --output-on-failure`: 308/308 passed on 2026-08-30;
  the run includes all CST tests, parser tests, conformance AST/grammar,
  architecture gates, and the long sanitizer ownership suites.
- `python3 scripts/check-rfc.py`: passed for 46 proposal RFCs on 2026-08-30.
- `python3 scripts/check-format.py`, English-only, diff-hygiene, and
  `git diff --check`: passed on 2026-08-30.

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

At the 2026-07-24 audit point, acceptance remained blocked on RFC 0022 reaching
`ACCEPTED` and the exact tooling projection dependency being rechecked. That
gate cleared on 2026-08-28; the current implementation state is recorded above.
