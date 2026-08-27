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

The proposal now separates one global `VerifiedModuleGraphVerifier` from the
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

### RFC 0025 Acceptance Synchronization

On 2026-07-25, the accepted RFC 0025 proposal at SHA-256
`4f4085c176a9f391115e12170da93af899e350fa92440d5a51577692faf8bad0`
atomically synchronized RFC 0004's toolchain-core search root, structural
catalog admission, finalized prelude identity, reserved-root source failure,
typed diagnostic adapter, precedence, and suppression contract. RFC 0004
remains `IMPLEMENTING`. Product implementation and executable evidence remain
tracked by RFC 0025's `R25` tasks; this decision note marks no implementation
slice complete.

On 2026-07-11, every required owner approved RFC 0004 proposal hash
`26bcc9dd95f5abbf623dd39af0cf6bd3ae2de9ed6be89649465803609c8af5cd`.
The accepted design freezes the deterministic binder, verified global module
graph, resolution environment, requester-filtered export surface, binding fact,
diagnostic, and no-rebinding contracts. Implementation is active under the
dependency-ordered replacement series recorded below; `LANDED` remains blocked
until the direct production cutover and every acceptance gate complete.

The retained direction is global module-graph verification, context-wide
identity freeze, per-module declaration collection, then source-ordered
reference resolution against complete verified inputs. Formal acceptance is
complete; the implementation tracker below is authoritative for remaining
landing work.

### RFC 0025 Acceptance Synchronization Evidence

- Acceptance authority is bound to RFC 0025 proposal SHA-256
  `4f4085c176a9f391115e12170da93af899e350fa92440d5a51577692faf8bad0`.
- `python3 scripts/check-rfc.py` and scoped `git diff --check` passed for this
  documentation transaction.
- Target-root, dependency-alias, source-root, ordering, suppression,
  no-publication, exact diagnostic, argument, and anchor evidence remains
  assigned to RFC 0025's `R25` tasks.

## Implementation Tracker

RFC 0004 entered `IMPLEMENTING` on 2026-07-12. Existing binder code remains
evidence about current behavior, not proof that the accepted contract is
implemented. Only completed rows with executable evidence count toward
landing.

| Slice | State | Required evidence |
|---|---|---|
| Dependency-free root verifier spine | Complete | Commit `05d12af5`; `VerifiedModuleGraphVerifier`, private `VerifiedModuleGraphView`, `BindingInputVerifier`, private `VerifiedBindingInput`, focused sanitizer tests, and positive plus adversarial binder architecture gates |
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
| Dependency-free unlabeled control-transfer facts | Complete | Commit `3ea452ce`; nearest loop and match targets, callable-boundary rejection, exact escaped-keyword failures, typed `ZOM3020-ZOM3021`, full target and fact codecs, test-only differential reconstruction, ninety-four focused Binder cases, 106 unit CTests, 1,082 lit CTests, full sanitizer build, and adversarial architecture mutations |
| Canonical label declaration facts | Complete | Commits `17575e9b` and `2d04166e`; generalized retained-token lookup, sealed module-or-callable `LabelId`, owner-local schema-preorder allocation, immediate statement edges, flattened block-or-loop targets, exact declaration-token provenance, deterministic duplicate facts with `ZOM3010` and `ZOM3017`, allocation codecs, test-only differential reconstruction, 107 focused Binder cases, full sanitizer build, and adversarial architecture mutations |
| Dependency-free explicit labeled control-transfer facts | Complete | Commit `da0e4958`; active-ancestor lookup, innermost canonical selection, function and closure boundaries, no implicit fallback, paired `BoundLabel` and explicit control facts, exact retained reference failures, typed `ZOM3022`, full codecs, foreign-context checks, test-only differential reconstruction, 116 focused Binder cases, three diagnostic-adapter cases, all 1,251 CTests, and adversarial architecture mutations |
| Dependency-free value deferred-member facts | Complete | Commits `cc1ef743` and `c3d14f40`; schema-backed dot, optional, and qualified access, closed `DeclaredDefinitionName` member spelling, exact base, source, value namespace, and direct-call generic arguments, paired top-level and inline facts, deterministic codecs, test-only differential reconstruction, all 1,253 CTests, full sanitizer build, and adversarial architecture mutations |
| Dependency-free inferred closure free-variable facts | Complete | Commit `a8f04055`; one dense row per frozen closure, capturable-only successful local-value references, original-site nested propagation, named-function boundary rejection, expanded-key and source ordering, complete codecs, foreign-context checks, test-only differential reconstruction, 127 focused Binder cases, all 1,253 CTests in 886.75 seconds, the grammar oracle in 886.45 seconds, full sanitizer build, format/include/RFC checks, and positive plus adversarial architecture gates |
| Dependency-free explicit closure-capture and receiver facts | Complete | Commit `0e5a6d3f`; complementary `explicitClosureCaptures` rows including `use []`, exact source-ordered capture tokens, capturable-target and explicit-boundary enforcement, unique leading receiver syntax with no default value, special receiver `DefId(Parameter)` handling outside `BindingNameKey`, parser-provenance recognition of the bare-receiver `Self` expansion, `ThisExpr`, full codec and foreign-context checks, test-only differential reconstruction, focused positive plus negative tests, 190 focused Binder cases, all 123 unit CTests, all lit CTests, full sanitizer build, format/RFC/architecture gates, and positive plus adversarial architecture gates |
| Binder verifier execution boundary | Complete | Commit `0e5a6d3f`; the production path is split across orchestration, canonical codec, structural and definition-fact validation, independent capture, context, and control semantic validators, and private publication; `BindingVerifier` never invokes `BindingBuilder` or constructs expected metadata; the authoritative seventeen-sequence fact schema drives storage, accessors, stable sequence tags, domain mutation inventory, and executable test ownership; production semantic validators and test-only oracle components call no producer algorithm; the focused sanitizer Binder executable passes 190 cases; all 123 unit CTests, all lit CTests, full sanitizer build, format/RFC checks, and the Binder schema plus positive and adversarial architecture gates pass |
| Block-scope named-function alignment | Complete | Commit `0e5a6d3f`; immediate-block skeleton activation, before-and-after declaration lookup, block ownership, module-surface exclusion, duplicate and shadow participation through the canonical scope map, and named-function capture-boundary coverage; the 164-case Binder executable, 1,089 lit CTests, normative Chapter 6 wording, full sanitizer build, and format/RFC/architecture gates cover the contract |
| Module-item import and export syntax alignment | Complete | Commit `0e5a6d3f`; the C++ parser and normative ANTLR grammar distinguish module-item declarations from block declarations, block `import` and `export` emit `ZOM2096`, the former block-import positive contract is removed, reserved words are rejected as qualified module-path segments, and paired AST plus grammar negative fixtures cover all forms; 274 parser cases, 54 Chapter 13 AST plus grammar cases, full sanitizer build, and parser-coverage plus format/RFC gates pass |
| Module-owned block-local classification | Complete | Commit `0e5a6d3f`; syntax-slot-owned module-item versus lexical placement replaces the inherited boolean and classifies plain-block, `while`, C-style `for`, `for-in`, match-arm, and unsafe-block declarations as `Local`; all seven inventory cases pass under sanitizers; full sanitizer build and format/RFC/architecture gates pass |
| Frozen identity projection without raw Binder authority | Complete | Commit `0e5a6d3f`; `FrozenDefinitionInventoryVerifier` reconstructs canonical keys directly from verified parse provenance, resolves frozen registry handles without `DefinitionIdentityMap`, and checks the exact current-module definition and impl census while permitting context-global registries; all four focused inventory cases pass under sanitizers; full sanitizer build and identity-architecture plus format/RFC gates pass |
| Dependency-free current-surface completeness | Complete | Commit `0e5a6d3f`; production structural verification proves an exact one-to-one projection from canonical module-scope local bindings to visible entries before validating export subset and revision; missing, additional, reordered, malformed, stale, and foreign surface cases are covered by the 164-case Binder executable; full sanitizer build, all 123 unit CTests, all lit CTests, and the adversarial architecture gate pass |
| Closed member-visibility facts | Complete | Commit `0e5a6d3f`; `DefinitionFact` retains `Public`, `Private`, or `Protected` only for supported member declaration kinds; interface defaults resolve to public, other supported bodies default to private, the production verifier checks structural presence and domain mutation tests cover exact semantics; the focused Binder executable, full sanitizer build, all 123 unit CTests, and the adversarial architecture gate cover semantic and structural mutations |
| Public verified Binder coordinator | Complete | Commit `0e5a6d3f`; `runBinding` is the sole public build-and-verify entry point; mutable candidates, `BindingBuilder`, `BindingVerifier`, and the differential oracle remain Binder-internal; closed publication and rejection behavior passes the focused sanitizer test, full sanitizer build, all 123 unit CTests, and the adversarial architecture gate |
| RFC 0008 module input handoff | Complete | `CompilerSession` admits digest-verified snapshots, freezes the global graph and structural resolutions, schedules Binder work dependency-first, passes only verified parsed modules, inventories, graph views, and completed dependency surfaces to `runBinding`, then atomically stages canonical signature and canonical module interface publication |
| Import, export, module-alias, and prelude publication | In review | `ImportBindingProjector` publishes selected imports, module aliases, local and foreign re-exports, visibility envelopes, revisions, and provenance from verified graph inputs and completed surfaces; prelude projection is wired but the session currently supplies an empty prelude set |
| Module and qualified resolution | In progress | Module-namespace lookup and selected imported-member resolution are active; requester-authorized typed member lookup and associated-member completion remain owned by RFC 0005 and RFC 0009 |
| Cross-module codecs, verifier closure, and surfaces | Complete | Binder-local foreign targets, aliases, imports, exports, dependency spans, provenance, exact current surfaces, and revisions are encoded and structurally verified; RFC 0015 supplies the single canonical signature, canonical coherence, and `VerifiedModuleInterface` canonical publication rail with no prior revision producer or consumer |
| Production binder cutover | Complete | Commit `0e5a6d3f`; `CompilerSession` calls only `runBinding`; the raw Binder, `DefinitionIdentityMap`, compiler symbol rail, AST `BindingMetadata`, polymorphic type rail, and AST-to-IR lowering entry are deleted; full sanitizer build, all 123 unit CTests, all lit CTests, format/RFC checks, and all architecture gates pass |

The production Binder path accepts verified multi-module graph inputs and
dependency surfaces, then feeds canonical signature, canonical module interface, coherence
canonical, checked-fact, dispatch-fact, borrow-evidence, and semantic HIR publication.
Remaining restrictions are owned by their current Checker, query, ownership,
and IR RFCs rather than an RFC 0015 boundary.

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
`FrozenDefinitionInventoryView` only when the frozen registries contain the
exact definition and impl census for the current module. Unrelated definitions
from other modules in the same context are permitted. `VerifiedModuleGraphVerifier` and
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
`ParsedModuleReceipt canonical` preimage remains unchanged because the retained token
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
module-member inputs exist. The test-only differential harness compares a
candidate with a producer baseline and therefore is regression evidence, not an
independent verifier. Production `BindingVerifier` performs candidate-only
structural publication checks, rejects foreign or malformed targets, validates
the owned frozen key projections, and proves the complete lexical-site census.
The focused sanitizer
executables pass eighty Binder cases and sixteen frozen-registry cases; the
Binder and identity architecture positive and mutation suites also pass.

At this slice's checkpoint, the production frontend still invoked the existing
`Binder`; module aliases, imports, re-exports, local exports, visibility
filtering, prelude and module-member lookup, explicit receivers, `ThisExpr`,
`Self`, labels, control transfers, closure facts, deferred members, and the
final metadata and surface codecs remained open. Subsequent tracker rows record
the closure, label, control-transfer, deferred-member, receiver, and `ThisExpr`
implementation slices.

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

The test-only control domain oracle consumes structurally validated candidate
scope facts and walks parents without calling `ScopeArenaBuilder` or
`ControlTransferBuilder` to check nearest-target semantics. Production
`BindingVerifier` enforces structural
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
and the test-only label domain oracle cover every field. Commits `17575e9b` and
`2d04166e` passed the 107-case focused Binder executable, sanitizer configure
and full build, format and include checks, RFC validation, and the Binder
architecture positive and adversarial mutation gates.

The explicit labeled control-transfer slice traverses label statement subtrees
with an active stack, resets that stack at function and closure boundaries, and
searches canonical names from innermost to outermost without implicit fallback.
Success publishes a paired `BoundLabel` resolution and
`ExplicitLabelControlTarget`; missing or inactive labels publish `ZOM3001` at
retained ordinal one, and `continue` to a block publishes `ZOM3022` at the same
exact token. The test-only control domain oracle reproduces the recursive
active-label rules, success/failure XOR, source provenance, emitter sites,
foreign-context rules, and complete codecs without calling `ScopeArenaBuilder`
or `ControlTransferBuilder`. Commit
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

The test-only context domain oracle reconstructs every expected member fact
from the AST without calling a member producer. The differential harness still
uses a production baseline and is not independent verification. Production
`BindingVerifier` rejects
structurally malformed or divergent top-level versus inline facts and validates
every encoded field. Commits `cc1ef743` and
`c3d14f40` pass sanitizer configure and full build, format and include checks,
RFC validation, AST generation, parser coverage, lexer architecture, AST
conformance coverage, the Binder architecture positive gate, and its complete
adversarial mutation suite. The complete CTest preset passes all 1,253 tests in
1,107.29 seconds; the ANTLR grammar conformance oracle passes in 1,106.89
seconds.

The dependency-free inferred closure slice publishes one dense row for every
frozen function-expression without a capture clause and every lambda identity,
including verified empty rows. It consumes only successful local value bindings
to parameters, locals, and pattern bindings. The original reference site
propagates through every crossed implicit closure and through explicit closures
without adding inferred rows for them, then stops at the callable owning the
target; unrelated named-function boundaries fail closed. Module declarations,
functions, types, and a closure's own parameters or locals do not become free
variables. RFC 0007 owns final capture places and modes.

The test-only closure domain oracle consumes structurally validated candidate
scope facts and reconstructs expected capture triples without calling
`ScopeArenaBuilder` or `ClosureFreeVariableBuilder`. Production
`BindingVerifier` structurally verifies the dense
closure census, exact targets and sites, nested propagation, expanded
`DefinitionKey` ordering, source-span plus schema-preorder site ordering,
deduplication, context ownership, and the complete closure, target, and site
codec. Commit `a8f04055` passes 127 focused Binder cases, sanitizer configure
and full build, format and include checks, RFC validation, the Binder
architecture positive gate, and the complete adversarial mutation suite. The
complete CTest preset passes all 1,253 tests in 886.75 seconds; the ANTLR grammar
conformance oracle passes in 886.45 seconds. Coverage includes dense empty rows,
capturable and non-capturable targets, two-level original-site propagation,
explicit-clause exclusion from inference, a real unrelated named-function
boundary, every row/target/site mutation class, foreign contexts, and every
codec field.

The explicit closure-capture and receiver slice is present in the current
implementation checkout and remains in review until a landing commit and the
complete gate record are attached above. Every function expression with a
capture clause publishes one expanded-`DefinitionKey`-ordered
`ExplicitClosureCaptureFact`, including an empty row for `use []`. Successful
items stay in AST source order and bind only parameters, locals, or pattern
bindings. By-value and by-reference items retain their exact identifier tokens;
`use [this]` retains its exact `ThisKeyword` and targets the enclosing special
receiver `DefId(Parameter)`.

Receiver parameters publish `ParameterList` definition facts but have no
lexical `bindingName`, `BindingNameKey`, or ordinary scope entry. Body binding
uses a separate receiver slot, resolves `ThisExpr` through closure scopes, and
stops at named-function boundaries. The parser requires the receiver to be the
unique first parameter and rejects a receiver default value. The `Self` type
node synthesized for a bare receiver is recognized by its exact receiver-token
provenance and does not create a lexical type-name resolution. Every capturable
body reference crossing an explicit closure must occur in that closure's
capture row. Duplicate captures retain deterministic primary and
previous-declaration spans; undefined, wrong-namespace, non-capturable,
inaccessible, and missing-receiver items publish exact failed resolutions.

The test-only explicit-capture domain oracle consumes structurally validated
candidate scope facts and reconstructs the explicit-closure census, receiver
recognition, capture targets, duplicate diagnostics, and crossed-closure
exhaustiveness without calling `ScopeArenaBuilder` or the capture producer.
Production `BindingVerifier` proves structurally
that inferred and explicit rows partition all closures exactly once, validates
foreign semantic contexts, and encodes every closure, capture-list node and
span, item node and span, and target. Focused tests in the current worktree cover
positive value, reference, empty, receiver, nested-partition, and propagation
cases plus malformed row, list, item, target, source, resolution, partition,
duplicate, and foreign-context mutations. Complete sanitizer, architecture,
format, RFC, and default-suite evidence is still pending for this in-review
row; RFC 0007 continues to own semantic capture places and modes.

### 2026-07-18 Verifier Decomposition And Trust Boundary

The production verifier is decomposed by trust domain. `binding-verifier.cc`
sequences failures and publication, `binding-candidate-codec.cc` owns canonical
local record encoding, `binding-candidate-validator.cc` owns base structural
and cross-record validation, `binding-definition-fact-validator.cc` owns the
definition-fact domain, and dedicated capture, context, and control validators
independently reconstruct their semantic facts from verified inputs.
`binding-publication.cc` owns verified-object construction, while
`binding-builder.cc` remains the sole producer coordinator. Test-only semantic
checks remain split into focused differential domains and cannot publish
verified metadata.

`binding-fact-schema.def` is the authoritative inventory for seventeen fact
sequences. It defines record membership, publication, stable sequence tags,
domain ownership, mutation classes, and one executable mutation-test owner per
sequence. Macro expansions generate candidate storage, public accessors,
publication accessors, canonical sequence dispatch, and differential count
inventory. Record payloads and AST-sensitive semantic validators remain
handwritten so the verifier does not reproduce producer traversal from a shared
algorithm.

The architecture gate rejects producer headers and symbols from every
production verification component and every semantic oracle component. It also
mutation-tests validator staging, CMake composition, capture boundaries, and
canonical label and control ordering. The production capture validator rebuilds
explicit and inferred closure domains; the context validator rebuilds
contextual `Self` and receiver reachability; and the control validator rebuilds
label ownership, activation, targets, transfers, and failures. The sole
permitted producer call in the test oracle target is the differential harness's
baseline build; that byte comparison is regression evidence and never an
independent publication proof. The fact-schema gate additionally rejects tag,
domain, codec, accessor, mutation-inventory, CMake-boundary, and component-size
drift.

Current worktree verification uses the sanitizer preset. The focused
`binding-input-test` executable passes 190 cases, and the Binder architecture,
adversarial architecture, and fact-schema CTests pass. Full repository build,
lit, format, and landing evidence are tracked separately and remain required
before this in-review row can land.

### 2026-07-19 Full Repository Gate Verification And Slice Completion

The in-review dependency-free slices were re-verified against the complete
repository gate matrix at HEAD `0e5a6d3f`. Two real build failures were
repaired before the matrix could run:

- `compiler/binder/graph/module-resolution.cc` was missing the
  `compiler/identity/canonical/canonical-decoder.h` include, leaving
  `identity::CanonicalDecoder` incomplete at the `decodeCanonical` call site.
- `compiler/query/query-database.h` was missing
  `zc/core/debug.h`, leaving `ZC_REQUIRE_NONNULL` undeclared in
  `TypedQueryResult::value` and the derived-kind provider/verify lambdas.
- `scripts/check-compiler-session-architecture.py` was out of sync with the
  driver surface: `incremental-module-resolution-query.{h,cc}` were not in
  `EXPECTED_DRIVER_FILES` and the `DRIVER_BUILD_MARKER` did not include the new
  source. The script was updated to list the new files and match the
  `CMakeLists.txt` `DRIVER_SRC` ordering.

Verified evidence at `0e5a6d3f`:

- `cmake --build --preset sanitizer`: 155 of 155 targets build cleanly.
- `ctest --preset default -L unittest`: 123 of 123 unit tests pass
  (including the 190-case `binding-input-test` executable).
- `ctest --preset default -L lit`: 4 of 4 lit suites pass.
- `scripts/check-format.py`: all changed files formatted correctly.
- `scripts/check-rfc.py`: 19 proposal RFCs pass.
- All architecture gates pass: `check-binder-architecture`,
  `check-checker-architecture`,
  `check-compiler-session-architecture`, `check-diagnostic-coverage`,
  `check-identity-architecture`, `check-impl-source-architecture`,
  `check-incremental-query-architecture`, `check-ir-architecture`,
  `check-lexer-architecture`, `check-package-architecture`,
  `check-parser-coverage`, and `check-lit-exec-root`.

The following dependency-free slices were promoted from `In review` to
`Complete` because their required evidence is now satisfied by the full
repository gate matrix: explicit closure-capture and receiver facts, Binder
verifier execution boundary, block-scope named-function alignment, module-item
import and export syntax alignment, module-owned block-local classification,
frozen identity projection without raw Binder authority, dependency-free
current-surface completeness, closed member-visibility facts, public verified
Binder coordinator, and production Binder cutover.

Slices that remain `In review` or `In progress` have external dependencies
that are not owned by RFC 0004: RFC 0008 module input handoff (combined
signature/interface scheduling remains open), import/export/module-alias and
prelude publication (the session supplies an empty prelude set until RFC 0024
configured-prelude integration lands), module and qualified resolution
(requester-authorized typed member lookup and associated-member completion
remain owned by RFC 0005 and RFC 0009), and cross-module codecs/verifier
closure/surfaces (the combined `VerifiedModuleInterface` codec and signature
authorization closure remain blocked by RFC 0015).

# RFC 0015 Accepted Overlay

RFC 0015 was approved at exact review SHA-256
`642836225d54f6fa28f8c27e9985972081dbd221c2e8f3e61a0aafd04fe9bb1e`.
Its accepted-file SHA-256 is
`9704d5651606e8a74034c8af4be5172b4007a6c9f0ee8ea2f5ee183223401c01`.
The overlay directly replaces the RFC 0004 impl source-shape and provenance
contracts named by RFC 0015.

# RFC 0018 Occurrence Bridge Overlay

RFC 0018 was accepted after all nine owners approved exact REVIEW SHA-256
`bdcbee8761d5476822cbe5bb2548332ad36e4d5f507c38e74d06751c6f444379`.
The Binder contract now uses one shared stable `ImplId` authority per equal
identity group and one independently verified `ImplOccurrenceId`, complete
`ImplSourceOccurrenceKey`, binding fact, and impl-body scope per source node.
`ScopeOwner::ImplOccurrence` retains tag `0x03` and expands to the complete
occurrence key in every Binder codec. Implementation evidence remains tracked
by RFC 0018.
