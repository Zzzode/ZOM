# RFC 0004 Review And Implementation Tracker

This document is the local discussion and tracking record for RFC 0004. It
does not approve the proposal. RFC status, approvers, and the recorded decision
remain authoritative in the proposal frontmatter.

## Discussion Record

### 2026-07-10 RFC 0010 Dependency Review

The binder owner review returned RFC 0004 for the following blocking issues:

- binder metadata exposes local `SymbolId` values and pointer-derived scope
  identifiers instead of context-checked `ModuleId`, `DefId`, and explicit
  arena-owned `ScopeId` values;
- no binding verifier freezes complete facts before checker or checked-module
  construction;
- RFC 0004 and RFC 0008 formed a dependency cycle: canonical module identity
  was assigned to RFC 0008 while RFC 0008 required RFC 0004 and RFC 0005;
- the proposal describes `use` and glob imports that the current Chapter 13
  contract does not support, allocates a second diagnostic family for module
  failures, and does not preserve canonical definition identity through
  imports and re-exports;
- module item forward references and source-ordered local binding activation
  are not separated, so the design can expose local declarations before their
  declaration point;
- live scopes and symbols are keyed by names or object addresses, declaration
  provenance uses a synthetic zero buffer, and binder-created symbols carry
  placeholder types;
- capture, label, shadow, import, and deferred-member metadata claimed by the
  RFC is incomplete or not behaviorally tested;
- acceptance evidence and status history claim complete behavior while several
  criteria explicitly defer diagnostics or use non-asserting placeholder tests.

RFC 0011 now owns the canonical semantic context and identity hierarchy
without depending on RFC 0004. Before re-entering review, RFC 0004 must define
a verified binding handoff, source-ordered local semantics, canonical
import/re-export targets, and exact conformance gates against the current
module and declaration specifications.

### 2026-07-10 Draft Revision Response

The proposal was rewritten around context-checked module input,
`SemanticContextBrand`, deterministic `ScopeId` and `DefId`, module-item skeleton
collection, source-ordered local activation, explicit import/re-export alias
provenance, deferred type-directed members, closure free-variable facts, and a
`BindingVerifier` that alone can publish `VerifiedBindingMetadata`. The
identity dependency now points to RFC 0011. A binder-owned
`VerifiedExportSurfaceView` removes the hidden dependency on RFC 0008 while
still allowing RFC 0008 to assemble full session inputs later. The old
implementation link and complete claims are removed.

These edits address the written blockers but do not constitute approval. RFC
0004 remains `DRAFT` until RFC 0011 enters review and all owners review the new
contract.

### 2026-07-11 Entry Review Return

The required owner entry review returned the revised draft. The blocking
findings were:

- the prebinding inventory did not prove one context-global definition and impl
  freeze before per-module views, and several RFC 0011 identity-producing or
  no-identity syntax rows lacked an exact site mapping;
- scope, label, activation, output-fact, closure propagation, source receipt,
  and verifier contracts were not exhaustive enough to implement without
  choosing new semantics;
- diagnostic producers, redeclaration-kind mapping, invariant codes, emitter
  ordinals, and typed-safe display arguments were incomplete;
- import and export records lacked exact visibility envelopes, revisions,
  requester filtering, source ancestry, and terminated re-export provenance;
- local export lookup conflicted with the foreign re-export diagnostic rows;
- module alias declarations had a frozen `DefId(ModuleAlias)` but no exact
  resolved `DefId -> ModuleId` input or output fact;
- module scope zero had no source-range rule when a source omitted an explicit
  module declaration; and
- RFC 0008 did not preserve the RFC 0004 binding surface or represent
  type-enriched module targets in its verified interface schema and revision.

### 2026-07-11 Entry Review Response

The draft now specifies one context-wide prebinding collection and freeze,
exact declaration and pattern-binding sites, the complete scope/label producer
and activation tables, closed binding fact sums, nested closure propagation,
and receipt-backed source provenance. `BindingInputVerifier` is the only
producer of exact requester-filtered import, foreign re-export, and module-alias
records. `BindingVerifier` is the only producer of complete verified metadata
and the current module surface.

The diagnostic table now gives one producer and suppression rule for every
`ZOM3001-ZOM3019` code, reserves local export lookup failure for `ZOM3001`,
limits `ZOM3015-ZOM3016` to foreign re-exports, defines `ZOM9922-ZOM9926`, and
requires typed-safe arguments and deterministic emitter ordinals. Module scope
zero uses the verified source-root range. RFC 0008 now embeds the exact verified
binding surface and revision and uses a closed definition-or-module projection
for visible and exported bindings. Acceptance criteria and tests cover negative
proofs for all of these contracts.

Follow-up review refined the handoff again. Parsing now creates an unbranded
immutable tree receipt and performs a no-reparse promotion only after
`SourceFileId` freeze. Closure identities and impl generic parameters have exact
activation points. Definition facts include target-dependent namespace and
declaring scope; scope parents and semantic owners are total; match is an
explicit break target; label targets terminate at block or loop scopes; and
closure free-variable facts exclude globals, imports, functions, and types.

The export-surface and module-interface codecs now allocate every locally owned
closed tag, define map and sorted-record encoding, and publish independent
byte/hash framing oracles. RFC 0008 uses only RFC 0004 visibility envelopes,
states the observable absent-versus-invisible diagnostic distinction, removes
implicit dependency-scope name lookup, and requires the checker to index
signatures only through already-bound canonical targets. Architecture gates
reject every downstream rebinding entry point.

These changes are a response, not an approval. Every returned owner must review
the current bytes again before RFC 0004 may enter `REVIEW`.

### 2026-07-11 Spec And Verification Return

Spec review found that callable-only label identity could not represent
module-level labels accepted by the grammar, successful label references did
not fit the name-binding result algebra, control-transfer user failures had no
registered diagnostics, and implicit package visibility contradicted Chapters
13 and 23. Verification returned the draft because verifier rejection branches,
the scope/label byte oracle, typed diagnostic argument tests, architecture gate,
focused commands, orphan policy, and coverage threshold were not executable
contracts. It also found that the downstream rebinding rule added `ir-backend`
impact without routing that required owner.

### 2026-07-11 Spec And Verification Response

`LabelId` now has a closed module-or-callable owner, `BoundLabel` represents
successful references, block/loop/match targets are total, and
`ZOM3020-ZOM3022` plus existing undefined/duplicate diagnostics cover every
control failure. Non-exported module entries are module-private; every distinct
module, including one in the same package, sees only explicit exports. RFC 0008
uses the same rule.

The binding verifier now returns a closed failure sum with an exhaustive
condition-to-invariant table and one-mutation negative oracle. Scope and label
allocation have a field-level codec, all-kind framing fixture, byte length, and
SHA-256 oracle. Typed diagnostic tests cover every argument and escape case.
The proposed `check-binder-architecture.py`, compile-negative target, exact
CTest commands, orphan checks, coverage preset, baseline threshold script, and
70% new-file floor are mandatory acceptance evidence. `ir-backend` is now a
required owner for the verified-consumer boundary.

These edits invalidate every earlier hash-specific entry approval. All owners
must review the new stable bytes.

### 2026-07-11 IR Consumer Boundary Return And Response

The newly routed `ir-backend` owner returned the first downstream gate because
it allowed HIR, MIR, LIR, and backend code to include verified binder metadata
directly, could reject legitimate HIR AST lowering, omitted LIR and the current
`irgen` prototype from acceptance wording, and lacked per-layer compile tests.

The revised contract contains an exact dependency matrix. The checker and
checked-module builder may consume verified binding metadata; HIR consumes only
`VerifiedCheckedModule`; MIR consumes verified HIR; LIR consumes verified
executable MIR plus target facts; backend consumes verified LIR. The disposable
`irgen` exception may read only immutable verified frontend facts until RFC 0010
deletes it. Compile-negative and positive fixtures cover every edge and preserve
legal HIR traversal of immutable AST and checked facts. Checker and driver paths
are now explicit Repository Impact rows.

### 2026-07-11 Entry Review Approval

The `binder-checker`, `error-system`, `module-system`, `ir-backend`,
`spec-audit`, and `verification` owners independently approved RFC 0004 hash
`98f4a6b22ebfa1e3f05a67b092b8164bbac24621c0d4b8c58d111a6707bd4620`
for `DRAFT -> REVIEW`. Cross-RFC checks used RFC 0008 hash
`86a9a37ee852cad1d45226d5ff2a3da9c519ca6f41798fa1322507afbde42b83`.
The RFC check, format check, diff check, English-only check, and all three
independent byte/hash oracle recomputations passed.

These are entry approvals only. They do not populate the proposal's
`approvers` field, record an acceptance decision, authorize implementation, or
claim that the proposed tests and architecture gates exist. The review manager
must still approve the governance transition.

### 2026-07-11 Review Manager Authorization And Transition

The `rfc` review manager authorized the atomic `DRAFT -> REVIEW` transition for
proposal hash
`98f4a6b22ebfa1e3f05a67b092b8164bbac24621c0d4b8c58d111a6707bd4620`,
RFC 0008 cross-reference hash
`86a9a37ee852cad1d45226d5ff2a3da9c519ca6f41798fa1322507afbde42b83`,
and tracker hash
`2e114bfc2ea5e955b770b4fcaed5a06173e40dbc2556c9dc758dc32c03ffc79f`.
The proposal frontmatter, RFC index, status history, and this owner table now
record `REVIEW` atomically. `approvers` remains empty, the decision and
implementation remain `TBD`, and no implementation is authorized by this
transition.

### 2026-07-11 Formal Acceptance Review Return

Formal semantic and invariant review returned REVIEW proposal hash
`d09271721673f2bd428d33bd474738ffad0dfd7a753fd6a9a1dedd0053869ca3`.
The single-module input contained already-filtered surfaces but assigned the
same verifier ownership of global SCC detection and missing-versus-invisible
classification. Mixed import/re-export and module-alias cycles had no total
classification, ambiguous module paths had no registered diagnostic, the
identity failure algebra named an undefined type, and `ParsedModuleReceipt`
lacked an independent byte oracle. Entry approvals do not cover these formal
review findings.

### 2026-07-11 Formal Acceptance Review Response

The proposal now separates one global `ModuleGraphVerifier` from the
per-module `BindingInputVerifier`. The global verifier alone receives the
complete graph, resolves module paths, emits exact missing/ambiguous/cycle
diagnostics, classifies every mixed SCC deterministically, and publishes only
acyclic private-constructor graph slices. In topological order, the binding
input verifier receives each slice plus complete unfiltered dependency
surfaces, distinguishes absence from invisibility, and only then publishes
requester-filtered views and resolved binding edges. RFC 0008 implements the
same ownership and scheduling direction.

The response also adds `ZOM3023-ZOM3024`, corrects the identity invariant type,
and fixes the complete 105-byte parsed-module receipt oracle with SHA-256
`7a4ab18a31387244311bd2a1b1472350536140c89532ce64240d7670d5a20b8e`.
The repaired formal-review proposal hash is
`449f911b7415f010e7a6aac28bf2fd230df3e427715f7dd83d9a95e3a93234b9`;
coordinated RFC 0008 hash is
`dfa32ab93b45a5ab926758a8acbd353582c13ea1a6fcf290427f918f1e6057c1`.
Every acceptance owner must review these exact bytes; this response is not an
approval.

### 2026-07-11 Exact-Hash Acceptance Review Approval

Formal semantic and invariant re-review approved RFC 0004 proposal hash
`26bcc9dd95f5abbf623dd39af0cf6bd3ae2de9ed6be89649465803609c8af5cd`.
The reviewed cross-RFC surfaces were RFC 0008 hash
`4a299be3aa1c89d61bfeb679edcf96636e506d0d752997f0853040e4a9a0a67a`,
RFC 0010 hash
`b5abd8a1f282e787bfbdf258fb0a4e5ff4a9e95607a91603c7db3160190fbe17`,
and RFC 0012 hash
`39b7a9edfd5112b9f72fce569ffab1d274c94c957bd6106f6c9158d23b46a982`.

Semantic review approved the binder/checker, error-system, module-system, and
spec-audit surfaces. Invariant review approved the IR consumer and verification
surfaces. The reviews independently recomputed the 105-, 43-, 68-, 97-, 80-,
and 327-byte oracles and confirmed the closed environment codec, receipt
issuer/revision binding, requester and target membership, SCC classification,
visibility filtering, and no-rebinding boundary. `scripts/check-rfc.py`, parser
coverage, lexer architecture, AST coverage, and `git diff --check` pass.

The `rfc` governance owner remains pending. This technical approval does not
populate proposal frontmatter, record an acceptance decision, or authorize
implementation until governance approves the tracker and atomic transition.

## Owner Review Checklist

| Owner | Review State | Blocking Surface |
|---|---|---|
| `rfc` | Approved for REVIEW | Governance, scope, owner routing, evidence, and atomic status transition |
| `binder-checker` | Approved for REVIEW | Exact identity inventory, verifier, scopes, activation, and complete facts |
| `error-system` | Approved for REVIEW | Exhaustive diagnostic producers, invariants, ordering, and safe arguments |
| `module-system` | Approved for REVIEW | Resolved module aliases, imports, exports, visibility, revisions, and provenance |
| `ir-backend` | Approved for REVIEW | Verified binding consumer boundary and executable no-rebinding gate |
| `spec-audit` | Approved for REVIEW | Labels, control transfers, module-private exports, syntax, and ownership boundaries |
| `verification` | Approved for REVIEW | Verifier negatives, byte oracles, diagnostics, commands, architecture, and coverage gates |

No owner approval is recorded by this table. Approval is recorded only in RFC
0004 frontmatter after every blocking review item is resolved.

## Acceptance Review

| Owner | Decision | Exact reviewed surface |
|---|---|---|
| `rfc` | Approved at `26bcc9dd...` | Governance, dependency legality, owner parity, decision, and atomic transition |
| `binder-checker` | Approved at `26bcc9dd...` | Global graph slice boundary, complete binding input, facts, verifier, and identities |
| `error-system` | Approved at `26bcc9dd...` | Unique cycle, path, member, visibility, ambiguity, binder, and invariant diagnostics |
| `module-system` | Approved at `26bcc9dd...` | Complete graph verification, topological surfaces, requester filtering, revisions, and provenance |
| `ir-backend` | Approved at `26bcc9dd...` | Verified consumer boundary and no downstream rebinding |
| `spec-audit` | Approved at `26bcc9dd...` | Chapter 13 syntax, module-private visibility, labels, control transfer, and ownership boundaries |
| `verification` | Approved at `26bcc9dd...` | Exact hashes, byte oracles, negative matrices, architecture gates, and test commands |

## Decision Record

Decision: ACCEPTED.

On 2026-07-11, every required owner approved RFC 0004 proposal hash
`26bcc9dd95f5abbf623dd39af0cf6bd3ae2de9ed6be89649465803609c8af5cd`.
The accepted design freezes the deterministic binder, verified global module
graph, resolution environment, requester-filtered export surface, binding fact,
diagnostic, and no-rebinding contracts. Implementation remains `TBD`; the next
legal transition is `ACCEPTED -> IMPLEMENTING` only when the direct replacement
series is named and starts.

The retained direction is global module-graph verification, context-wide
identity freeze, per-module declaration collection, then source-ordered
reference resolution against complete verified inputs. Formal acceptance is
complete; the implementation tracker below is authoritative for remaining
landing work.

## Implementation Tracker

RFC 0004 entered `IMPLEMENTING` on 2026-07-12. Existing binder code remains
evidence about current behavior, not proof that the accepted contract is
implemented. Only completed rows with executable evidence count toward
landing.

| Slice | State | Required evidence |
|---|---|---|
| Dependency-free root verifier spine | Complete | Commit `05d12af5`; `ModuleGraphVerifier`, private `VerifiedModuleGraphView`, `BindingInputVerifier`, private `VerifiedBindingInput`, focused sanitizer tests, and positive plus adversarial binder architecture gates |
| Parsed-module and frozen-inventory provenance | Complete | Commits `46839dcd` and `1b863942`; normative `ParsedModuleReceipt` oracle, immutable source promotion, exact single-module identity projection, ten focused sanitizer cases, and thirteen architecture mutations |
| Retained parser token provenance | Complete | Commit `6036f93d`; single-use successful-parser capability, same-source and same-buffer admission, exact EOF and ordered-range validation, owned canonical token text, exact escaped-keyword spans, identifier-prefix rejection, 268 parser cases, 82 binder cases, 105 unit CTests, full sanitizer build, and adversarial architecture mutations |
| Dependency-free binding metadata publication | Complete | Commit `1745926f`; immutable `VerifiedBindingMetadata` and `VerifiedExportSurface`, private candidate authority, closed verification results, production allocation and surface codecs, registered invariant diagnostics, emitted `ZOM3001`, twenty focused sanitizer cases, and adversarial architecture mutations |
| Deterministic scope allocation | Complete | Commits `9373d0e7`, `3e38e063`, `765cf8e1`, and `a75f0937`; frozen impl identities, all ten accepted scope kinds, schema-preorder allocation, exact parents, semantic owners and source spans, checked index overflow, production verifier cutover, bodyless-function range repair, twenty-five focused sanitizer cases, and adversarial architecture mutations |
| Module, type, and impl skeleton facts | Complete | Commits `632d1669` through `b5692a75`; exact declaration and pattern sites, canonical `DefinitionFact` and `ImplBindingFact` ordering, module/type/impl scope maps, direct impl members, module constant pattern leaves, private and declaration-export surfaces, typed `ZOM3003-ZOM3010` duplicate failures with `ZOM3017` notes, NFC collision coverage, thirty-five focused sanitizer cases, and adversarial architecture mutations |
| Scope-owned generic and ordinary callable parameter facts | Complete | Commits `4434b909`, `aecfea09`, `1d640fe5`, and `593f4b64`; `GenericList` facts for scope-owning type parameter lists, `ParameterList` facts for direct function, method, and extern parameters, exact function-scope ownership, value-namespace placement, duplicate generic and parameter source diagnostics with `ZOM3017` notes, module-surface and direct-impl-member exclusion, focused sanitizer coverage, and adversarial architecture mutations |
| Special callable identity and parameter facts | Complete | Commits `115445a5` and `570f6a82`; constructor and destructor `DeclaredDefinitionName` identities without fabricated lexical names, value-namespace `DefinitionFact` publication, type- or impl-owned function scopes, `ParameterList` facts, ordinary-binding exclusion, focused sanitizer coverage, and adversarial architecture mutations |
| Closure identity, generic, and parameter facts | Complete | Commits `6749c23c` and `da426edc`; anonymous function-expression and lambda identities, value-namespace `ExpressionIntroduction` facts, closure-owned generic and parameter facts, ordinary-binding and surface exclusion, focused sanitizer coverage, and adversarial architecture mutations |
| Loop and match pattern facts | Complete | Commits `37a2b8d7` and `69454c7d`; `PatternBindingSite` provenance, value-namespace `LoopPattern` and `MatchPattern` facts, exact loop and match-arm scopes, ordinary-binding and surface exclusion, focused sanitizer coverage, and adversarial architecture mutations |
| Dependency-free lexical body binding | Complete | Commits `c1b27a9e`, `88bd4452`, and `ce12fb34`; independently owned frozen key projections, source-ordered local and parameter activation, lexical value and type resolution, exact failed-name and shadow facts, complete lexical-site census, eighty focused Binder cases, sixteen frozen-registry cases, and adversarial architecture mutations |
| Dependency-free unlabeled control-transfer facts | Complete | Commit `3ea452ce`; nearest loop and match targets, callable-boundary rejection, exact escaped-keyword failures, typed `ZOM3020-ZOM3021`, full target and fact codecs, independent verifier reconstruction, ninety-four focused Binder cases, 106 unit CTests, 1,082 lit CTests, full sanitizer build, and adversarial architecture mutations |
| Canonical label declaration facts | Complete | Commits `17575e9b` and `2d04166e`; generalized retained-token lookup, sealed module-or-callable `LabelId`, owner-local schema-preorder allocation, immediate statement edges, flattened block-or-loop targets, exact declaration-token provenance, deterministic duplicate facts with `ZOM3010` and `ZOM3017`, allocation codecs, independent verifier reconstruction, 107 focused Binder cases, full sanitizer build, and adversarial architecture mutations |
| Dependency-free explicit labeled control-transfer facts | Complete | Commit `da0e4958`; active-ancestor lookup, innermost canonical selection, function and closure boundaries, no implicit fallback, paired `BoundLabel` and explicit control facts, exact retained reference failures, typed `ZOM3022`, full codecs, foreign-context checks, independent verifier reconstruction, 116 focused Binder cases, three diagnostic-adapter cases, all 1,251 CTests, and adversarial architecture mutations |
| Dependency-free value deferred-member facts | Complete | Commits `cc1ef743` and `c3d14f40`; schema-backed dot, optional, and qualified access, closed `DeclaredDefinitionName` member spelling, exact base, source, value namespace, and direct-call generic arguments, paired top-level and inline facts, deterministic codecs, independent verifier reconstruction, all 1,253 CTests, full sanitizer build, and adversarial architecture mutations |
| Dependency-free inferred closure free-variable facts | Complete | Commit `a8f04055`; one dense row per frozen closure, capturable-only successful local-value references, original-site nested propagation, named-function boundary rejection, expanded-key and source ordering, complete codecs, foreign-context checks, independent verifier reconstruction, 127 focused Binder cases, all 1,253 CTests in 886.75 seconds, the grammar oracle in 886.45 seconds, full sanitizer build, format/include/RFC checks, and positive plus adversarial architecture gates |
| Complete module resolution input | Blocked by RFC 0012 and RFC 0008 | Authoritative package resolution, production semantic-context fingerprint, verified resolution environment, and resolution receipts |
| Complete binding facts and surfaces | Pending | Imports, re-exports, module aliases, local exports, visibility envelopes, prelude and module-member resolution, explicit capture-list name binding, qualified, module, and associated members, receiver, `ThisExpr`, and `Self` binding, current-surface completion, remaining codecs, and verifier negatives; RFC 0007 owns final capture places and modes |
| Production binder cutover | Pending | Session integration, no downstream rebinding, deletion of raw binder inputs and binder-owned module resolution, and all acceptance gates |

The first slice is intentionally fail-closed. It accepts only a single frozen
root module whose syntax independently proves that no module-resolution receipt
is required. Imports, foreign re-exports, module aliases, or non-zero graph
edges must produce an invariant failure until their complete verified inputs
exist. Impl identities are now frozen for deterministic scope ownership, while
complete impl binding facts remain pending. The slice does not call or wrap the
current `Binder` and does not add a compatibility entry point.

The completed slice publishes the full accepted invariant fact shape and an
actual fatal `ZOM9956` producer. Six focused unit cases cover successful frozen
input, missing, additional, and foreign definitions, unresolved module syntax,
impl rejection, invalid requesters, stale fingerprints, fixed revision bytes,
and diagnostic projection. `scripts/check-binder-architecture.py --check`, its
six-mutation `--self-test`, and the registered `binder-architecture` and
`binder-architecture-negative` CTests pass. This evidence closes only the
dependency-free spine; it does not claim dependency surfaces, complete binding
facts, or production binder cutover.

The second slice removes raw `ast::Tree`, `SourceFileId`, and
`DefinitionIdentityMap` values from `BindingInputCandidate`. Parser output is
now moved into `UnbrandedParsedModule`, bound to the exact expanded
`SourceFileKey`, source digest, byte length, generated AST schema fingerprint,
deterministic schema dump, and every node's canonical byte range, then promoted
to `VerifiedParsedModule` only after the frozen source registry reproduces the
same snapshot and receipt. The normative 105-byte receipt preimage reproduces
SHA-256 `7a4ab18a31387244311bd2a1b1472350536140c89532ce64240d7670d5a20b8e`.

`FrozenDefinitionInventoryVerifier` independently walks that verified tree,
reconstructs every complete `DefinitionKey` and `ImplKey` from its kind, name,
parent path, source site, and sibling ordinal, and publishes the private
`FrozenDefinitionInventoryView` only when the frozen registries and tree-local
definition map agree exactly. `ModuleGraphVerifier` and
`BindingInputVerifier` now consume these verified values rather than reopening
the raw inputs. Ten focused sanitizer cases cover exact receipt bytes, exact
tree binding, stale source content and length, cross-source ranges, successful
promotion, missing, additional, wrong-kind, and foreign-context definitions,
missing and mismatched impl identities, graph revision, and diagnostic
projection. The architecture
gate's thirteen mutations reject public constructors, foreign publication,
foreign producer calls, raw candidate fields, missing build wiring, forbidden
layering, and compatibility facades.

Commit `6036f93d` extends the verified parsed-module boundary with retained
lexer provenance. `Parser::takeTokenSnapshot` is available once after a
successful parse and never after a failed parse. `ParsedModuleVerifier` accepts
only that capability for the matching source manager and buffer, validates the
complete token sequence, and stores owned canonical text with exact raw ranges.
Focused evidence covers escaped-at-start `break`, escaped-in-middle `break`,
escaped `continue`, identifier-prefix rejection, wrong-kind lookup, source and
range rejection, and the single-use capability. The normative
`ParsedModuleReceipt v0` preimage remains unchanged because the retained token
table is derived from the receipt-bound immutable source by the private
successful-parser capability.

On the frozen implementation, the complete sanitizer matrix passes 1,250 of
1,250 tests in 987.15 seconds; the full grammar oracle passes in 986.41 seconds.
Lexer architecture, parser coverage, AST conformance coverage, RFC validation,
format, `git diff --check`, and the focused Binder architecture gates also pass.
This slice establishes trustworthy dependency-free Binder inputs only. Scope
allocation is now published by the later deterministic arena slice; name
bindings, complete definition and impl facts, import and export surfaces,
module aliases, and production Binder cutover remain incomplete. This slice
does not unblock the RFC 0008 multi-module graph.

The third slice publishes the accepted metadata boundary without exposing its
mutable construction authority. `VerifiedBindingInput` now retains the exact
semantic context, package, crate, module, and semantic-context fingerprint used
to verify the input. The frozen definition inventory carries the canonical
definition key, complete semantic name, optional binding name, and source span
needed to construct binding facts without reopening parser or registry state.

The dependency-free metadata builder accepts complete collision-free module,
type, and impl skeletons. `ScopeArenaBuilder` publishes module, function,
closure, type, impl, block, loop, match, match-arm, and unsafe-block scopes in
generated schema preorder, with exact nearest parents, inherited semantic
owners, node-to-scope facts, source containment, and checked indices. Body
identifier resolution and every later activation family fail closed without
publishing partial name facts. Same-source spans whose byte end exceeds the
verified source snapshot are rejected as `InvalidSourceRange` rather than
passing source-key-only validation.

The verifier compares the production 3,227-byte allocation dump with SHA-256
`2c5b3604e7bb003b11cff64d1b19af3405ab1940b4379846faba3a05754a9cb6` and the
production export-surface revision
`1764a287bf612ee8a648563f8f525b36ef5e7de5f8238a8c97194bd99796722b`.
It rejects foreign context handles, cross-source and same-source out-of-bounds
spans, missing and additional facts, malformed scope ancestry, stale surfaces,
and unsupported fact families. `ZOM9922-ZOM9926` are registered and mapped by the
closed invariant diagnostic adapter; alias-cycle and invalid-emitter producers
remain part of the pending complete binding-facts slice.

The sanitizer build completed all 222 targets. The focused Binder executable
passed 20 of 20 cases, and the Binder architecture positive and mutation suites
passed. The full grammar oracle passed in 590.01 seconds. The first full CTest
attempt exposed one unrelated socketpair timing failure in the vendored HTTP
suite; that exact test then passed four consecutive runs. With the already-passed
grammar and socketpair cases excluded, the remaining matrix passed 1,248 of
1,248 tests in 385.28 seconds. Format, RFC, lexer architecture, parser coverage,
AST conformance coverage for 865 corpus inputs, and `git diff --check` also pass.
This evidence closes only dependency-free metadata publication. Imports,
re-exports, aliases, labels, control transfers, captures, complete visibility,
multi-module resolution, and production Binder cutover remain pending.

The deterministic scope series freezes standalone and marker impl identities,
routes all `ScopeId` construction through the internal arena, encodes impl
owners with their canonical `ImplKey`, and removes the hard-coded three-scope
verifier. The parser anchors a bodyless function's synthetic block at its
terminating semicolon so the child range is contained by the function range.
The complete grammar suite passed in 553.15 seconds. After regenerating the
eighteen affected AST snapshots, those eighteen lit cases plus AST coverage
passed 19 of 19; the sanitizer build, twenty-five focused Binder cases, Binder
architecture positive and negative suites, parser unit tests, parser coverage,
lexer architecture, RFC validation, format, and `git diff --check` also pass.
Labels and the combined scope-plus-label production oracle remain owned by later
binding-fact slices.

The skeleton-fact series preserves exact declaration or pattern provenance
through inventory freeze, including variable declarator, match-arm, and for-in
introducers plus canonical schema paths. `BindingSkeletonBuilder` is the sole
authority for collision-free module, type, and impl bindings. It orders facts
by expanded RFC 0011 keys, orders scope maps by namespace and canonical name,
publishes direct impl members, and excludes type and impl members from module
surfaces. Direct declaration exports carry exact export provenance and form the
complete external subset. Duplicate NFC-equivalent bindings retain the first
source occurrence and produce kind-specific `ZOM3003-ZOM3010` source failures
with an attached `ZOM3017` note through the typed identifier adapter.

The later generic activation slice publishes `GenericList` facts only for
generic lists whose declaration owns an accepted scope. It keeps generic
parameters out of module surfaces and direct impl member lists, and rejects
duplicate generic parameters with the registered source diagnostic plus its
previous-declaration note. The named callable parameter slice publishes
`ParameterList` facts only for direct function, method, and extern parameters.
Each parameter is a value-namespace binding in its exact function scope, is
excluded from module surfaces and direct impl member lists, and receives the
existing duplicate-parameter diagnostic and `ZOM3017` note. The builder proves
the direct AST owner before publication, so a parameter cannot be attached to
an unrelated function scope.

The special callable slice preserves RFC 0011 `DeclaredDefinitionName` identity
for `init` and `deinit` without fabricating a `SemanticIdentifier` or ordinary
lexical binding. A constructor or destructor publishes one value-namespace
`DefinitionFact`, owns an exact function scope below its type or impl body, and
activates its direct parameters through `ParameterList`. Its reserved name does
not enter a `ScopeBindingEntry`, module surface, or export surface. The frozen
inventory permits an absent lexical binding only for these two closed definition
kinds; every other declared definition remains required to have a
`SemanticIdentifier`. Target-dependent module aliases, imports, and re-exports
remain blocked on verified resolution input.

The closure slice publishes anonymous `FunctionExpression` and
`LambdaExpression` identities through `ExpressionIntroduction` without
fabricating a lexical name. Each closure owns a closure scope; its direct
generic and parameter facts activate in that scope through `GenericList` and
`ParameterList`. A closure has no ordinary binding, module surface seed, or
export surface entry. Source-ordered local activation remains separate because
it requires body binding rather than expression introduction.

The loop and match pattern slice preserves each frozen `PatternBindingSite`
and selects its activation from the pattern introducer. A `ForInStatement`
leaf becomes a `LoopPattern` value binding in the loop scope; a `MatchArmStmt`
leaf becomes a `MatchPattern` value binding in the match-arm scope. Neither
enters a module or export surface. Source-ordered block declarators remain
separate because their bindings activate only after each initializer.

The dependency-free lexical body-binding slice moves source-ordered local facts
out of the module skeleton and into an internal `BodyBindingBuilder` that runs
after deterministic scope allocation and skeleton publication. It seeds module
and generic definitions, walks callable signatures before parameter defaults,
activates each parameter only after its own default, activates block locals only
after their initializers, and activates loop and match patterns only after their
scrutinee or iterable. Successful references publish canonical `BoundName`
facts; lookup failures publish deterministic `Failed` facts through the typed
diagnostic adapter; lexical shadowing records the exact prior canonical target.

Reference routing is explicit for value and type sites, including value-namespace
type queries, type-namespace dynamic marker paths, object-literal shorthand, and
optional struct-pattern type paths. Qualified paths fail closed until verified
module-member inputs exist. `BindingVerifier` independently reconstructs the
complete lexical-site census, merges source failures in schema-preorder, rejects
foreign or malformed targets, and encodes bound-name, failed-name, and shadow
facts through owned constant-time frozen key projections. The focused sanitizer
executables pass eighty Binder cases and sixteen frozen-registry cases; the
Binder and identity architecture positive and mutation suites also pass.

This slice is limited to dependency-free lexical value and type binding. The
production frontend still invokes the existing `Binder`; module aliases,
imports, re-exports, local exports, visibility filtering, prelude and
module-member lookup, explicit receivers, `ThisExpr`, `Self`, labels, control
transfers, closure free-variable facts, deferred members, and the final
metadata and surface codecs remain open.

The lexical body-binding evidence series passed sanitizer configure and build,
the eighty-case Binder executable, the sixteen-case frozen-registry executable,
and the Binder and identity architecture positive and negative mutation suites.
The final sanitizer-backed matrix passed 1,250 of 1,250 tests in 1,284.92
seconds, with the complete grammar oracle accounting for 1,087.51 seconds. RFC
validation, format and include checks, lexer architecture, parser coverage, AST
conformance coverage for 865 corpus inputs, and `git diff --check` also pass.

The complete sanitizer-backed landing gate passed 1,250 of 1,250 tests in
654.78 seconds. The gate also passed format, RFC validation, parser coverage,
lexer architecture, and `git diff --check`; the ANTLR grammar matrix accounted
for 654.68 seconds of the test duration.

The generic and named-callable parameter evidence series passed the complete
sanitizer-backed matrix again: 1,250 of 1,250 tests in 949.56 seconds, with the
ANTLR grammar oracle accounting for 948.43 seconds. It also passed sanitizer
configure and build, format, RFC validation, `git diff --check`, the Binder
architecture positive gate, and its negative mutation matrix. The production
implementation is recorded in `4434b909`, `aecfea09`, `1d640fe5`, and
`593f4b64`.

The special-callable evidence series completed the 1,250-test
sanitizer-backed serial CTest matrix without a CTest failure record. It also
passed sanitizer configure and build, format, RFC validation, `git diff
--check`, the Binder architecture positive gate, and its negative mutation
matrix. The production implementation is recorded in `115445a5` and
`570f6a82`.

The closure evidence series passed sanitizer configure and build, the focused
binding unit test, the Binder architecture positive gate, and its negative
mutation matrix. The production implementation is recorded in `6749c23c` and
`da426edc`.

The closure evidence series then passed the complete sanitizer-backed serial
matrix: 1,250 of 1,250 tests in 2,634.87 seconds, with the ANTLR grammar oracle
accounting for 820.04 seconds. The same run passed all five IR conformance
cases and the complete Binder architecture positive and negative gate set.

The pattern evidence series passed sanitizer configure and build, the focused
binding unit test, the Binder architecture positive gate, and its negative
mutation matrix. The production implementation is recorded in `37a2b8d7` and
`69454c7d`.

The dependency-free unlabeled control-transfer slice runs after lexical body
binding and publishes one source-range `ControlTransferFact` for each valid
`break` or `continue`. `break` selects the nearest loop or match scope;
`continue` skips match scopes and selects the nearest loop. Function and closure
boundaries terminate lookup. A statement without a target publishes only one
failed resolution whose primary is the exact retained raw keyword token.
`ZOM3020-ZOM3021` are registered with these executable producers.

`BindingVerifier` independently rebuilds the scope arena, walks parents without
calling `ControlTransferBuilder`, and enforces nearest-target semantics,
canonical fact order, full node/kind/target/source encoding, foreign-context and
source-range rejection, and the exact success-fact versus failed-resolution
XOR. Commit `3ea452ce` passed the 94-case focused Binder executable, all 106
sanitizer-backed unit CTests, all 1,082 lit CTests, sanitizer configure and full
build, format and include checks, RFC validation, the Binder architecture
positive gate, and its adversarial mutation suite. Parser coverage, generated
AST schema, AST conformance coverage, lexer architecture, and the 874.48-second
ANTLR grammar matrix also pass.

The canonical label declaration slice assigns every label an owner-local
schema-preorder `LabelId`, records its immediate statement edge, flattens nested
labels to one block-or-loop `LabelTarget`, and retains the exact raw declaration
identifier at ordinal zero. Later duplicates retain their IDs and facts while
publishing deterministic `ZOM3010` primaries with attached `ZOM3017` notes. The
allocation dump, candidate codec, foreign-context checks, source-range checks,
and independent label oracle cover every field. Commits `17575e9b` and
`2d04166e` passed the 107-case focused Binder executable, sanitizer configure
and full build, format and include checks, RFC validation, and the Binder
architecture positive and adversarial mutation gates.

The explicit labeled control-transfer slice traverses label statement subtrees
with an active stack, resets that stack at function and closure boundaries, and
searches canonical names from innermost to outermost without implicit fallback.
Success publishes a paired `BoundLabel` resolution and
`ExplicitLabelControlTarget`; missing or inactive labels publish `ZOM3001` at
retained ordinal one, and `continue` to a block publishes `ZOM3022` at the same
exact token. The independent verifier reproduces the recursive active-label
oracle, success/failure XOR, source provenance, emitter sites, foreign-context
rules, and complete codecs without calling `ControlTransferBuilder`. Commit
`da0e4958` passes the 116-case focused Binder executable, the three-case typed
diagnostic adapter executable, sanitizer configure and full build, format and
include checks, the Binder architecture positive gate, and its adversarial
mutation suite. The complete CTest preset passes all 1,251 tests, including
1,082 lit tests and the grammar conformance oracle. Coverage includes every
loop form, blocks, nested and module-owned labels, escaped and NFC-equivalent
names, duplicate-label diagnostics with active lookup,
forward/sibling/completed and cross-closure rejection, no-fallback failures,
malformed success and failure pairs, and mixed global diagnostic ordering. The
production frontend cutover remains a separate pending slice.

The dependency-free value deferred-member slice gives every
`MemberExpression` an explicit dot, optional, or qualified access kind and
restricts member spelling to the closed `DeclaredDefinitionName` domain. Dot
and optional access publish one canonical `DeferredMemberFact` in both the
top-level sequence and the node binding, with the exact base, member name,
value namespace, full expression source, and direct-call type arguments.
Qualified access fails closed until a verified module or associated-member
context is available.

`BindingVerifier` independently reconstructs every expected member fact from
the AST, rejects missing, additional, reordered, malformed, and divergent
top-level versus inline facts, and encodes every field. Commits `cc1ef743` and
`c3d14f40` pass sanitizer configure and full build, format and include checks,
RFC validation, AST generation, parser coverage, lexer architecture, AST
conformance coverage, the Binder architecture positive gate, and its complete
adversarial mutation suite. The complete CTest preset passes all 1,253 tests in
1,107.29 seconds; the ANTLR grammar conformance oracle passes in 1,106.89
seconds.

The dependency-free inferred closure slice publishes one dense row for every
frozen function-expression or lambda identity, including verified empty rows.
It consumes only successful local value bindings to parameters, locals, and
pattern bindings. The original reference site propagates through every crossed
closure and stops at the callable owning the target; unrelated named-function
boundaries fail closed. Module declarations, functions, types, and a closure's
own parameters or locals do not become free variables. Explicit capture clauses
remain fail-closed until verified capture-list name binding exists; RFC 0007
owns final capture places and modes.

`BindingVerifier` independently rebuilds the scope arena and expected capture
triples without calling `ClosureFreeVariableBuilder`. It verifies the dense
closure census, exact targets and sites, nested propagation, expanded
`DefinitionKey` ordering, source-span plus schema-preorder site ordering,
deduplication, context ownership, and the complete closure, target, and site
codec. Commit `a8f04055` passes 127 focused Binder cases, sanitizer configure
and full build, format and include checks, RFC validation, the Binder
architecture positive gate, and the complete adversarial mutation suite. The
complete CTest preset passes all 1,253 tests in 886.75 seconds; the ANTLR grammar
conformance oracle passes in 886.45 seconds. Coverage includes dense empty rows,
capturable and non-capturable targets, two-level original-site propagation,
explicit capture-list rejection, a real unrelated named-function boundary,
every row/target/site mutation class, foreign contexts, and every codec field.
