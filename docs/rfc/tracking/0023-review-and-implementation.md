# RFC 0023 Review And Implementation Tracker

## Discussion Record

### 2026-07-24 Initial Proposal

RFC 0023 was created after RFC 0022 tooling review found that verified flow
facts alone cannot support hover, completion, or diagnostics over incomplete
editor source.

Repository inspection established:

- no ZOM LSP or editor semantic product exists;
- `zomc --syntax-only` is a batch compiler boundary after verified parsing and
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

## Owner Review Matrix

| Owner | State | Review Surface |
|---|---|---|
| `rfc` | Pending | Governance, scope, prior art, status, rollout, and exact-hash approvals |
| `lexer-parser` | Pending | Byte-covering lexeme stream, recoverable CST, recovery ordering, source map, and verified AST bridge |
| `binder-checker` | Pending | Verified/recovered binding authority, recovery-local identity, partial type algebras, conservative recovery flow, and valid-source equality |
| `module-system` | Pending | Editor inputs, stable/recovery query split, transitive input frontier, atomic validation, snapshot leases, cancellation, and invalidation |
| `error-system` | Pending | Path diagnostic facts, push suppression and clearing, ordering, version binding, and compiler equality |
| `tooling-lsp` | Pending | Workspace admission, IDE facade, protocol adapter, lifecycle, features, versions, terminal responses, and stale publication |
| `spec-audit` | Pending | Architecture claims, compiler/IDE authority separation, and non-normative recovery boundary |
| `verification` | Pending | Native fixtures, protocol integration, differential, mutation, stress, security, and performance gates |

Each approval must identify the exact RFC SHA-256. Normative edits invalidate
earlier approvals.

## Decision Record

Decision: Pending.

RFC 0023 is in `REVIEW`. No implementation is authorized by this tracker.
Acceptance remains blocked until RFC 0022 reaches `ACCEPTED` and the exact
tooling projection dependency is rechecked.

## Implementation Tracker

| Slice | State | Required Evidence |
|---|---|---|
| Owner and routing governance | Complete (governance only) | Manifest, owner guide, routing diagram, trigger matrix, and AGENTS index exist in the candidate worktree |
| RFC 0022 dependency | Blocking acceptance | RFC 0022 accepted status and exact projection-contract alignment |
| Recoverable parser and CST | Pending acceptance | One byte-covering lexeme/parser stream, exact source reconstruction, lossless recovery, deterministic facts, no grammar fork |
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
  `zomlang/compiler/query/query-database.cc` and `query-types.h`,
  providing the RFC 0017 dependency-record foundation the proposal builds
  its transitive input-frontier collection on.
- RFC 0017's atomic transactions, immutable snapshots, cancellation,
  diagnostic facts, and provenance revisions are present in the query
  database; RFC 0023 extends them with revision-local recovery and IDE
  query descriptors rather than redefining them.
- No ZOM LSP or editor semantic product currently exists, so the proposal
  defines a new surface without conflicting with production code.
- `zomc --syntax-only` is a batch boundary after verified parsing and
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
