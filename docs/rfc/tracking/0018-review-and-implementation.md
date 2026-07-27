# RFC 0018 Review And Implementation Tracker

This document records discussion, exact-snapshot owner review, decisions, and
implementation evidence for RFC 0018. The RFC frontmatter remains authoritative
for status and approvers.

## Discussion Record

### 2026-07-18 Implementation Discovery

RFC 0017 stable identity implementation completed build producer, generated
source, module, and plan-derived crate identity. Work on named definition,
implementation, and semantic module-resolution keys stopped because the
accepted proposal did not define the enclosing-owner wire sum, overload digest
domain, or resolution-policy bytes. Implementing those fields without review
would make the implementation the correctness authority.

RFC 0018 is a bounded follow-up that closes those preimages without changing
the accepted query runtime, memo, projection, diagnostic, persistence, or
concurrency architecture.

### 2026-07-18 Formal Review Entry

RFC 0018 entered formal review at proposal SHA-256
`09f82f5d44162a56f66dcd9a979adc62f8872593a6dd09bd286b481973341b70`.
`python3 scripts/check-rfc.py` passes for all eighteen proposal RFCs. All eight
required owners must approve this exact snapshot; any normative proposal edit
voids every approval and requires a new exact snapshot.

### 2026-07-18 First Formal Review Return

Every required owner returned exact snapshot
`09f82f5d44162a56f66dcd9a979adc62f8872593a6dd09bd286b481973341b70`.
The blocking findings were:

- recursive owner records expanded exponentially instead of using compact
  owner digests with separately retained collision records;
- callable, generic, structural type, ABI, and implementation header schemas
  did not close tags, alpha-normalization, collection normalization, or the
  frozen syntax inventory;
- duplicate definition and implementation candidates had no revision-local
  pre-admission sites or unambiguous primary and secondary diagnostic contract;
- duplicate generic bounds were incorrectly rejected instead of normalized for
  identity while retaining `W1204` source occurrences;
- module dependency-kind tags and requester/ancestor/crate-root lookup were
  under-specified;
- RFC 0004, RFC 0008, and RFC 0011 still exposed contradictory normative
  records without an explicit replacement boundary; and
- Repository Impact used overlapping globs that contradicted the owner
  manifest, including `binder/module-*` and `driver/CMakeLists.txt`.

No approval survives the return. The proposal is back in `DRAFT` for one
coordinated repair and must receive a new exact-snapshot review.

### 2026-07-18 First Repair Locked Audit Return

Three independent audits returned repaired DRAFT snapshot
`bd2a117492c4d4fecce24990c63be291b13a77106992c831f842098d947817e8`.
The linear owner, module kind, ancestry lookup, tracked alias/prelude,
normative replacement, duplicate-bound severity, and primary provenance
repairs passed. Remaining blockers were:

- the RFC 0011 sequence count is 64-bit, so owner growth is `8 + 33 * n`;
- receiver, destructor, ABI, where-relation, async, and unsafe alternatives
  exceeded or contradicted current accepted frontend semantics;
- callable and implementation inline bounds and where clauses did not share one
  alpha-normalized obligation set;
- overload digests lacked retained complete-header collision authority;
- stable definition kind and namespace admission remained under-specified;
- header-schema generation, path ownership, complete AST inventory, literal
  codecs, and invalid constant-expression behavior were not reviewable;
- duplicate-bound occurrence sites and rejected-candidate occurrence keys were
  incomplete; and
- multi-candidate diagnostics did not preserve RFC 0004 first-declaration-only
  secondary semantics or the complete `ZOM4017` payload.

The next DRAFT repair must pass a new locked audit before review re-entry.

### 2026-07-18 Third Locked DRAFT Audit Return

The task-router, rfc, lexer-parser, and spec-audit owners returned exact DRAFT
snapshot `ad785c2103383744296b67f2cbe4036ff1c8472701df2019f477c5140d522287`.

The 64-bit owner framing, receiver tag and destructor corrections, current ABI
header, unified obligation set, and schema-generator routing passed. Remaining
blockers were:

- removing `TypeParameter` left RFC 0005 without a stable semantic-type
  replacement;
- selected import identity remained dependent on target namespace;
- mutating bare-`this` normalization was absent and the AST did not retain the
  modifier;
- the frozen AST schema admitted producerless ABI, where-relation, and string
  variants and could not distinguish character from string literal syntax; and
- Repository Impact omitted affected Binder, parser, and generated paths.

No approval survives this DRAFT audit. A new exact snapshot is required before
formal review re-entry.

### 2026-07-18 Fourth Locked DRAFT Audit Return

The rfc, lexer-parser, binder-checker, module-system, and ir-backend owners
reviewed exact DRAFT snapshot
`eabcca5521c7bc24b939641d672687c67e1f1aa0e1d4ae0cb5ee4a04f7c46518`.
The dedicated header wire schema, direct AST authority, parameter-key owner and
position sums, pre-resolution import namespace slots, delayed typed impl
diagnostics, RFC synchronization list, and build ownership passed.

Remaining blockers were:

- subordinate key stability text ignored transitive owner-key replacement;
- parameter-keyed projection queries had no complete RFC 0017 query contract;
- optional trait and inherent-implementation diagnostics had no language
  producer;
- final exact-impl collision diagnostic occurrences lacked a complete root,
  phase, owner, emitter, occurrence, and provenance tuple;
- the semantic type implementation path was missing from Repository Impact;
  and
- the AST contract still needed one authoritative method-mode, path-root,
  literal-kind, and dynamic-principal representation.

No approval survives this DRAFT audit. The proposal requires another exact
snapshot after all listed contracts are repaired.

### 2026-07-18 Fifth Locked DRAFT Audit Approval

The rfc, lexer-parser, binder-checker, module-system, and ir-backend review
groups approved exact DRAFT snapshot
`d2bfeabe400ac14df4fb5d680d05fc6427757569558e5a64bcddf4fdc84bcec1`.
The audit closed every blocker retained from the prior four snapshots. This
approval authorizes formal review entry only; it is not an RFC owner approval.

### 2026-07-18 Formal Review Re-entry

RFC 0018 entered formal review at proposal SHA-256
`3c1c9c3091bd83b84389b3357f5da5e2baaf27a275f68c85507618dce85f66f9`.
All nine required owners must approve this exact snapshot. Any normative RFC
edit voids the complete approval set and returns the proposal to DRAFT.

### 2026-07-18 Second Formal Review Return

The task-router, rfc, error-system, module-system, and ir-backend owners
reviewed exact proposal snapshot
`3c1c9c3091bd83b84389b3357f5da5e2baaf27a275f68c85507618dce85f66f9`.
The task-router, module-system, and ir-backend contracts passed. The rfc and
error-system owners returned the snapshot because definition-collision and
`W1204` diagnostic occurrences encoded only `moduleSyntaxPath` rather than the
complete `IdentitySyntaxSiteKey`. Equal structural paths in distinct source
files could therefore deduplicate incorrectly.

No approval survives the return. The proposal is back in `DRAFT`. The repair
must encode the complete site, including `SourceFileKey`, for both occurrence
families and require cross-source mutation coverage before a new exact-snapshot
review.

### 2026-07-18 Sixth Locked DRAFT Audit Return

The rfc, error-system, and task-router owners audited exact DRAFT snapshot
`348571de775ba654e3b31578dea1ad345b042175c17aa273cc00035b2623ee3c`.
Occurrence uniqueness, cross-source mutation coverage, and routing passed. The
rfc and error-system owners returned the snapshot because the definition
secondary and both `W1204` locations did not freeze the complete
`DiagnosticLocation::Source(DiagnosticProvenanceKey::IdentitySyntaxSite(...))`
variant. The repair now specifies all definition-collision, duplicate-bound,
and exact-implementation-collision primary and secondary locations with that
closed constructor. Other owner audits of the snapshot were invalidated when
the normative repair changed its hash.

### 2026-07-18 Seventh Locked DRAFT Audit Return

The rfc, error-system, task-router, lexer-parser, binder-checker, and spec-audit
owners audited exact DRAFT snapshot
`e0dbffe74f2b6662566b28e5f4b33a42e84320dd895816216383a85522466017`.
Diagnostic location construction, cross-source occurrence identity, and
routing passed. The snapshot returned because dynamic principal syntax was not
restricted to the parser-produced named form, overload headers retained
producerless variance and ordinary parameter-mode fields, the identity
diagnostic emitter enum included a non-diagnostic pending record, and the
authoritative dynamic AST repair was not synchronized with the type and grammar
specification chapters. All findings are repaired in the next exact snapshot;
other owner audits were invalidated when the normative proposal changed.

### 2026-07-18 Eighth Locked DRAFT Audit Return

All required owners audited exact DRAFT snapshot
`9942ac9a6498dfd4be3c0d5b1cd005f03499480f97d0a7e437b5178ab66c784d`.
The lexer-parser, binder-checker, spec-audit, error-system, and task-router
contracts passed. The rfc owner returned the snapshot because the canonical
overload header omitted generic variance while the semantic AST and handwritten
parser could still produce distinct `GenericTypeParam.variance` values. Those
sources could therefore compare equal under stable identity.

No approval survives the return. The proposal remains in `DRAFT`. The repair
deletes the producerless semantic AST field and parser values, keeps rejected
`in` and `out` spellings outside semantic identity, and requires executable
coverage proving that neither annotation can publish a canonical key.

### 2026-07-18 Ninth Locked DRAFT Audit Return

All required owners audited exact DRAFT snapshot
`2243ac9a7ecbbf8b8a32d96d8f939c7d51197443680bdca4218a3fcdb9aa41e2`.
The task-router, rfc, error-system, lexer-parser, binder-checker, and spec-audit
contracts passed. The module-system, ir-backend, and verification owners
returned the snapshot because header identity merged dynamic arrays with
slices, module resolution named tracked ancestry and catalog dependencies
without closed query descriptors, and exact-implementation diagnostics tried
to obtain a module from digest-only `ImplKey`.

No approval survives the return. The proposal remains in `DRAFT`. The repair
adds a distinct dynamic-array wire variant, defines exact requester-ancestry
and module-catalog-bucket input queries, roots exact-implementation diagnostics
at `first.module`, and requires inequality, shielding, and cross-module
verifier regressions.

### 2026-07-18 Tenth Locked DRAFT Audit Return

All required owners audited exact DRAFT snapshot
`b7189dd7e33b14d7d20c8158a4a3cd9b41e4e61a6305f3d06f06ea002d25b007`.
The task-router, rfc, error-system, and ir-backend contracts passed. The
lexer-parser, binder-checker, spec-audit, module-system, and verification
owners returned the snapshot because the array identity model retained the
unspecified postfix `T[N]` producer while Chapters 03 and 17 did not define a
consistent dynamic-array, slice, and fixed-array grammar, and because the two
new module-graph input queries omitted durability and input execution policy.

No approval survives the return. The proposal remains in `DRAFT`. The repair
defines the sole accepted array forms as `T[]`, `[T]`, and `[T; N]`, deletes
the postfix-length AST producer, requires `T[N]` rejection before identity
admission, and freezes both module-graph inputs as cheap, providerless,
cycle-free `Low` durability inputs.

### 2026-07-18 Eleventh Locked DRAFT Audit Return

All required owners audited exact DRAFT snapshot
`fd66fae2fd2238f2adbf91f953267543d5396fb670cf51f895e754e1149fe2d1`.
The task-router, rfc, error-system, lexer-parser, binder-checker, ir-backend,
and spec-audit contracts passed. The module-system and verification owners
returned the snapshot because `RequesterModuleAncestry` required every lexical
prefix to be active even though the verified module graph intentionally admits
source-less structural intermediate prefixes, and because the test plan did
not name the sparse-ancestry mutation or exact architecture-gate commands.

No approval survives the return. The proposal remains in `DRAFT`. The repair
requires only the requester and declared crate root to be active, keeps strict
intermediate prefixes as non-materializable structural `ModuleKey` records,
leaves active membership to exact catalog buckets, and adds the missing
sparse-ancestry mutations and four explicit architecture-gate invocations.

### 2026-07-18 Locked DRAFT Approval And Formal Review Re-entry

All nine required owners approved exact repaired DRAFT snapshot
`65b168eb96e04e24ab3cfd1955589d5088a66b1750cb2d3298b232dc93f74361`:
task-router, rfc, lexer-parser, binder-checker, module-system, error-system,
ir-backend, spec-audit, and verification. The snapshot passed the RFC,
identity-architecture, incremental-query-architecture, and scoped diff gates.

These DRAFT approvals authorize transition to `REVIEW` only. They are not
formal REVIEW approvals and do not authorize acceptance or implementation.
The proposal now enters a new exact-snapshot formal review; any normative edit
voids all formal-review results.

### 2026-07-18 Formal Review Return

All required owners audited exact REVIEW snapshot
`c2ca364699af5c2166e37649f5949791d6789dc851f672b6fbfa5c27c569ce11`.
The task-router, rfc, error-system, module-system, ir-backend, and verification
owners approved. The lexer-parser, binder-checker, and spec-audit owners
returned the snapshot, so it is not accepted and no approval survives.

The proposal required generic parameter bound conjunctions such as
`T: A + B` to produce ordered obligations and per-member duplicate-warning
sites, while the authoritative AST retained only one `GenericTypeParam.bound`
and both parsers rejected the conjunction. The review also found contradictory
generic marker-implementation and multi-principal `dyn` prose in Chapters 09
and 12. The proposal returns to `DRAFT` until one bound-list AST, independent
producer/verifier extraction, parser and grammar support, member-level
mutations, and the remaining specification drift are closed.

### 2026-07-18 Twelfth Locked DRAFT Audit Return

The task-router, rfc, lexer-parser, binder-checker, module-system,
error-system, ir-backend, and spec-audit owners approved exact repaired DRAFT
snapshot `37f4c99f096c3ec6acffb1a63169504d37488b0ae71f78f9728a9ac0ab8e7062`.
The verification owner returned it because the Test Plan did not explicitly
name the ordinary `A + B` parser, AST, grammar, and diagnostic rejection; the
AST codegen, parser-coverage, AST conformance, grammar conformance, and
impl-source gates; or architecture mutations that restore a singular bound,
substitute `IntersectionTypeExpr`, merge the associated-type list, or remove
the negative fixture. Direct schema generation also selected a Python without
PyYAML, so the proposal lacked one reproducible project-native dependency
entry.

No approval survives the return. The repaired DRAFT now freezes those exact
negative cases and mutation gates, names the native CTest entry points, and
requires conformance CMake to select one Python interpreter and probe PyYAML
before registering schema-dependent tests.

### 2026-07-18 Repaired Locked DRAFT Approval

All nine required owners approved exact repaired DRAFT snapshot
`e3b388a4b4258cc0f80441601136f0497ab2741c2037120c2e04fc83a5526b79`:
task-router, rfc, lexer-parser, binder-checker, module-system, error-system,
ir-backend, spec-audit, and verification. The snapshot passed RFC, AST schema,
parser-coverage, impl-source, identity, incremental-query, AST conformance,
ANTLR grammar, sanitizer build, unit, lit, format, and scoped diff gates.

These DRAFT approvals authorize transition to `REVIEW` only. They do not count
as formal REVIEW approvals and do not authorize stable-wire implementation.
The proposal now enters a new exact-snapshot formal review; any normative edit
voids every formal-review result.

### 2026-07-18 Formal Review Re-entry After Bound-List Closure

RFC 0018 entered formal review at exact proposal SHA-256
`a5a211b4d52093aa2ba151f7144493d27e014b62ef76c701359b69e310d4a7cb`.
All nine required owners must approve this exact snapshot. Any normative RFC
edit voids the complete formal-review approval set and returns the proposal to
`DRAFT`.

### 2026-07-18 Formal Review Return After Bound-List Closure

All nine required owners reviewed exact proposal snapshot
`a5a211b4d52093aa2ba151f7144493d27e014b62ef76c701359b69e310d4a7cb`.
The task-router, rfc, binder-checker, module-system, error-system, ir-backend,
and verification owners approved the snapshot. The lexer-parser and spec-audit
owners returned it because Chapter 06 assigns positive marker implementations
without `unsafe` to checker diagnostic `ZOM4091`, while the recursive parser,
ANTLR grammar, and Chapter 17 admitted inconsistent subsets based on marker
path length.

No approval survives the return. The proposal is back in `DRAFT`. The repair
requires one admission path for positive short and qualified bodyless marker
candidates: both parsers retain the AST candidate, and signature checking owns
`ZOM4091` and suppresses valid marker or coherence publication.

### 2026-07-18 Marker Admission Locked DRAFT Audit Return

The task-router, rfc, and error-system owners audited exact repaired DRAFT
snapshot `84bd1f4de72c686759a6ace2037b51c18ca4e6de8bbbf941d11899bf9a7b3566`.
Routing passed. The rfc and error-system owners returned the snapshot because
RFC 0015 already owns marker classification and diagnostic precedence, a
bodyless behavior-interface candidate must emit `ZOM4089` before the unsafe
check, and the proposal did not close stable identity admission versus semantic
marker publication or the RFC 0017 diagnostic occurrence.

No approval survives this DRAFT audit. The repair now requires RFC 0015,
retains the admitted `Safe` `ImplKey` and `ImplId` for lineage, assigns
`ZOM4091` only after marker-only classification, suppresses marker,
module-interface, and coherence publication after that error, and freezes the
exact stable occurrence, provenance, precedence, positive controls, and
mutations.

### 2026-07-18 Duplicate Marker Locked DRAFT Audit Return

All nine required owners audited exact repaired DRAFT snapshot
`5b0ec4ff52ec6c2f2ea5a7b314e89b8f4d7586363f11560f31eab80630806fff`.
The task-router, rfc, lexer-parser, module-system, error-system, ir-backend, and
verification owners approved it. The binder-checker and spec-audit owners
returned it because the generic `PendingImplCollision` path waited for an
ordinary authority `ImplHead`, while marker candidates never publish ordinary
impl heads and RFC 0015 requires every source-shape-valid marker occurrence to
reach marker-local conflict grouping.

No approval survives this DRAFT audit. The repair separates ordinary pending
collisions from `PendingMarkerImplGroup`, retains every marker source occurrence
under one stable identity authority, routes each occurrence through RFC 0015
classification, and materializes marker-local `ZOM4017` without an ordinary
`ImplHead` or a fabricated second `ImplId`.

### 2026-07-18 Occurrence Binding And Mixed-Form DRAFT Audit Return

All nine required owners audited exact repaired DRAFT snapshot
`25577ff8a7c291e63ddfd1a91281c7cb6fe222dc7243b20206068bb6ebc22146`.
The task-router, rfc, error-system, and lexer-parser owners approved it. The
binder-checker, module-system, ir-backend, spec-audit, and verification owners
returned it because one shared `ImplId` did not provide independently verifiable
binding facts and scopes for later source occurrences, and equal identity
records could contain both ordinary `{}` and bodyless `;` forms without a
heterogeneous classification rule.

No approval survives this DRAFT audit. The repair replaces the one-to-one
`ImplId -> ImplBindingFact` bridge with occurrence-local handles, binding facts,
and scope owners under one stable authority. One source-form-neutral identity
group now feeds per-occurrence RFC 0015 classification; only classified
survivors enter ordinary or marker collision and publication stages.

### 2026-07-18 Occurrence Bridge Locked DRAFT Approval

All nine required owners approved exact repaired DRAFT snapshot
`58826663bacaf7dba9aca97c8a86d0a34549133af5b7f47560f73e0e4580e43b`:
task-router, rfc, lexer-parser, binder-checker, module-system, error-system,
ir-backend, spec-audit, and verification. The occurrence bridge closes the
one-stable-authority/many-source-occurrence Binder contract, mixed-form
classification, survivor publication, diagnostic lineage, implementation
ordering, and mutation inventory.

These DRAFT approvals authorize transition to `REVIEW` only. They do not count
as formal REVIEW approvals and do not authorize implementation. Any normative
proposal edit voids the next formal-review result.

### 2026-07-18 Formal Review Re-entry After Occurrence Bridge Closure

RFC 0018 entered formal review at exact proposal SHA-256
`bdcbee8761d5476822cbe5bb2548332ad36e4d5f507c38e74d06751c6f444379`.
All nine required owners must approve this exact snapshot. Any normative RFC
edit voids the complete formal-review approval set and returns the proposal to
`DRAFT`.

## Review Queue

| Owner | State | Required review evidence |
|---|---|---|
| `task-router` | Approved exact REVIEW snapshot | Routing and ownership approved |
| `rfc` | Approved exact REVIEW snapshot | Lifecycle and replacement boundary approved |
| `lexer-parser` | Approved exact REVIEW snapshot | Parser, AST, ANTLR, and source-form contract approved |
| `binder-checker` | Approved exact REVIEW snapshot | Occurrence facts, classification, and survivor streams approved |
| `module-system` | Approved exact REVIEW snapshot | Stable authority, occurrence materialization, and module wire approved |
| `error-system` | Approved exact REVIEW snapshot | Diagnostic identity, provenance, and precedence approved |
| `ir-backend` | Approved exact REVIEW snapshot | Build ownership and implementation ordering approved |
| `spec-audit` | Approved exact REVIEW snapshot | Cross-RFC and specification consistency approved |
| `verification` | Approved exact REVIEW snapshot | Positive, differential, and mutation evidence plan approved |

## Decision Record

Accepted on 2026-07-18 after all nine required owners approved exact formal
REVIEW snapshot
`bdcbee8761d5476822cbe5bb2548332ad36e4d5f507c38e74d06751c6f444379`.
The decision adopts the stable definition, implementation, subordinate,
module-resolution, occurrence-binding, and diagnostic wire contracts in RFC
0018 as the mandatory closure for RFC 0017 implementation. Every producer,
verifier, codec, fixture, and caller uses those contracts.

### 2026-07-25 RFC 0025 Acceptance Synchronization

The RFC 0025 `R25-02` acceptance transaction is authorized by all twelve
required-owner approvals on exact proposal SHA-256
`4f4085c176a9f391115e12170da93af899e350fa92440d5a51577692faf8bad0`.
It synchronizes RFC 0018's current normative contract without changing this
RFC's `IMPLEMENTING` status.

| Binding | RFC 0025 Task Authority |
|---|---|
| Acceptance-time RFC synchronization | `R25-02` |
| Compilation-unit identity and transitive wire replacement | `R25-03` |
| Production package and crate-graph caller cutover | `R25-03C` |
| Identity mutation and fixed-vector evidence | `R25-03T` |
| Native transitive caller migration | `R25-03CT` |
| Contextual query-key and semantic-root replacement | `R25-07` |
| Crate-keyed parse option selection | `R25-07P` |
| Query, wire, invalidation, and no-fallback evidence | `R25-07T` |
| Final integrated evidence | `R25-15` |

The acceptance evidence is the exact 12/12 RFC 0025 approval set. Existing
RFC 0018 implementation evidence remains accurate for its completed stable-
identity and module-resolution slices, but the replacement encodings and
contextual roots complete only through the listed RFC 0025 tasks and gates.

### 2026-07-26 RFC 0026 Acceptance Synchronization

All four RFC 0026 required owners approved proposal SHA-256
`39df5d3f11dbdcb2e95056b1cd14fd5220a19688f31a3e3180230ad465a3f84d`.
The selected-module, dependency-site, request, failure, graph, SCC, cycle, and
ledger wire contracts plus standalone-versus-keyed validation now complete
through RFC 0026 tasks `R26-05` through `R26-09`.

### 2026-07-27 RFC 0027 Acceptance Synchronization

Acceptance transaction `rfc0027-accept-20260727-e2f4ba5e` synchronizes RFC
0018 to exact RFC 0027 proposal SHA-256
`e2f4ba5eb777d3d70b8eb3ad75b18f5169afc61a83d989ccc61fc9d5d022f435`.
The current contract defines the five stable Binder query keys, retains
complete authority records at every verification and materialization boundary,
and keeps implementation scopes and occurrence handles source-occurrence
specific. RFC 0018 remains `IMPLEMENTING`; completion evidence belongs to the
RFC 0027 implementation tracker.

## Implementation Tracker

| Slice | State | Required evidence |
|---|---|---|
| Proposal review | ACCEPTED | All owners approved exact formal REVIEW snapshot `bdcbee8761d5476822cbe5bb2548332ad36e4d5f507c38e74d06751c6f444379` |
| Normative RFC synchronization | Complete | RFC 0004/0005/0008/0011/0014/0015/0017 and required trackers use the accepted later-overlay contract |
| Configured Python and schema gates | Complete | Project-scope interpreter discovery, configure-time PyYAML fail-fast, zero bare CMake `python3` calls, focused build success, and 6/6 configured-Python CTests |
| Module resolution record keys | Complete | Policy and request keys plus requester-ancestry and catalog-path-bucket input values have fixed vectors, 10/10 focused identity tests, and positive and adversarial architecture gates |
| Module dependency kind ownership | Complete | Binder enum deleted; all production and test callers use the single identity enum; 5/5 focused tests |
| Canonical header wire schema | Complete | Generated inventory plus six generator mutations and 4/4 configured-Python CTests |
| Canonical record types | Complete | DefinitionIdentityRecord and ImplIdentityRecord retain the complete canonical authority, exact domain-separated key derivation, strict decoders, fixed vectors, invalid-record tests, and mutation gates |
| Definition and implementation identity | Complete | Independent AST producers and verifiers reconstruct complete records, owner chains, overload authority, duplicate-bound provenance, typed interner admission, and every production consumer on one path |
| Stable and owner-local inventory split | In progress | Stable inventories contain eligible named definitions and implementations, while parameter, binding, anonymous-owner, and implementation-occurrence identities use separate domains; persistent named-inventory and revision-local provenance query projections remain |
| Module resolution replacement | In progress | CompilerSession atomically stages exact ancestry, bucket, search-root, alias, and prelude inputs and demands ResolveModuleRequestQuery; direct batch resolution is deleted, while request derivation, site provenance, and reusable stale-input lifecycle remain to become tracked query families |
| Landing | Blocked by implementation | Sanitizer, full tests, architecture gates, format, and differential evidence |

### 2026-07-19 Narrow Module Input Progress

`RequesterModuleAncestry` now admits a non-empty requester-first chain whose
adjacent records are exact strict lexical parents. Structural intermediate
prefixes remain valid records without requiring catalog materialization.
`ModuleCatalogPathBucket` now admits an independently keyed absent or present
value and rejects a present module unless its crate and complete path equal the
bucket key.

`StructuralModuleResolver::freeze()` converts discovery candidates into those
admitted values and builds canonical module-key and catalog-bucket indexes.
`stageModuleResolutionQueryInputs()` projects the exact requester ancestry,
present and demand-specific absent buckets, per-crate search roots, dependency
aliases, and configured preludes into one input transaction. `CompilerSession`
then demands `ResolveModuleRequestQuery` for every semantic request and uses
the resolver only to validate the query result and issue its revision-local
receipt. The direct batch `StructuralModuleResolver::resolve()` authority is
deleted.

Evidence:

- the 10 module-resolution identity tests include fixed ancestry and present
  bucket digests, absent encoding, sparse ancestry, and rejection cases;
- focused sanitizer targets for module-resolution identity, dependency-request
  derivation, binding input, incremental module resolution, and CompilerSession
  build and pass;
- direct query tests cover exact candidates, missing-input failure, and
  unrelated-bucket red-green shielding; and
- identity, incremental-query, and CompilerSession architecture checks plus
  their adversarial self-tests reject batch resolution restoration, missing
  staging, missing demand, and exact-input drift.

### 2026-07-19 Canonical Header Producer Progress

The identity layer now admits `CanonicalImplHeader`, whose inline codec writes
the ordered generic parameters, polarity, safety, canonical trait reference,
self type, and sorted-unique obligations exactly as the suffix of
`ImplIdentityRecord`. It introduces no nested wire wrapper. Closed-tag
validation, complete-byte obligation normalization, cloning, accessors, and a
fixed fieldwise vector are covered by sanitizer tests.

The Binder now has three explicit AST-to-canonical production boundaries:

- `CanonicalHeaderTypeProducer` covers all sixteen RFC 0018 type variants and
  validates every lexical binder frame even when no type references it;
- `CanonicalDefinitionHeaderProducer` produces complete overload-header
  authority for functions, extern declarations, methods, and constructors; and
- `CanonicalImplHeaderProducer` produces canonical headers for standalone and
  marker implementations, including empty current-binder depth, alpha
  normalization, inline and where obligations, positive safe marker admission,
  qualified marker paths, and exact polarity and safety rules.

The architecture gate forbids `binding-verifier.cc` from calling any producer,
so the future verifier must independently walk and normalize AST structure. Its
adversarial inventory detects missing AST mappings, binder-depth collapse,
safe-positive marker rejection, obligation canonicalization loss, codec field
reordering, fixed-vector drift, and producer reuse from the verifier. Focused
sanitizer build and CTest evidence passes for the type, definition, impl, and
identity-layer canonical-header targets.

The completed record replacement constructs `DefinitionIdentityRecord` and
`ImplIdentityRecord` directly from expanded module identity, enclosing stable
owners, canonical headers, and normalized obligations. Independent verifier
reconstruction, syntax-site and duplicate-bound provenance, digest authority,
registry admission, and downstream consumers all use the same complete record
contract.
