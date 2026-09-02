# RFC 0011 Review And Implementation Tracker

This document is the local discussion and tracking record for RFC 0011. It
does not approve the proposal. RFC status, approvers, and the recorded decision
remain authoritative in the proposal frontmatter.

## Discussion Record

### 2026-07-10 Dependency-Cycle Resolution

RFC 0004 needs module-qualified definition identity, RFC 0005 needs a shared
context brand for semantic type handles, RFC 0008 needs binding and checked
facts to publish module interfaces, and RFC 0010 consumes all three. Assigning
identity to any one of those consumers created a hidden dependency cycle.

RFC 0011 extracts only the identity foundation. It distinguishes runtime
context branding from deterministic fingerprints, introduces package and crate
target identity, and defines deterministic module, definition, impl, and source
keys without approving session, type, interface, or IR semantics.

### 2026-07-10 Foundation Implementability Review

The first draft still had two hidden dependency cycles. A nested
`DefinitionKey` required its parent `DefId` before the definition registry had
issued any handles, and `ImplKey` required a normalized semantic type head from
RFC 0005. `ModuleKey` likewise mentioned a parent `ModuleId` during module-key
collection.

The draft now uses canonical structural module and definition paths while keys
are collected. Parent handles are derived only after registry freeze. `ImplKey`
identifies the declaration site and contains no resolved interface or semantic
type facts. Canonical child-key encoding expands parent keys instead of local
numeric slots. Syntax expansion and a complete prebinding definition inventory
must finish before definition and impl registries freeze; IR-only entities do
not receive `DefId` or `ImplId`.

### 2026-07-10 Governance And Source-Key Review

Governance review found missing `lexer-parser` and `error-system` ownership,
an ambiguous `SourceFileKey` that could depend on an unissued `ModuleId`, and
overlapping `DefId` allocation claims in RFC 0004. It also found that the
fingerprint's canonical byte encoding was not implementable from the prose.

The draft now gives source identity an independent pre-module freeze phase,
requires child keys and spans to expand `SourceFileKey` rather than numeric
slots, specifies the canonical byte encoding and SHA-256 fingerprint domain,
adds parser/AST and diagnostic owners, and makes the RFC 0011 registries the
only issuers of `DefId` and `ImplId`. RFC 0004 now owns only definition-key
inventory plus `ScopeId` and `LabelId` allocation.

### 2026-07-10 Required-Owner Blocking Review

Independent `rfc`, `lexer-parser`, `module-system`, `binder-checker`,
`error-system`, `ir-backend`, `spec-audit`, and `verification` reviews all
blocked entry into `REVIEW`. The structural RFC checker was green, but the
proposal still allowed per-crate `ModuleId` slot ambiguity, duplicated registry
URL identity, undefined fingerprint and generated-source constituents,
unmapped declaration-producing AST kinds, anonymous generated names, an
undefined brand issuer, untyped invariant diagnostics, and no executable
serialization or dump oracle. Spec review also found that the parser discards
accepted module-declaration forms and that draft identity/session contracts had
leaked into normative chapters.

The draft has been revised with one context-global registry per identity tag,
an explicit process-root brand factory, a single semantic type store, closed
package/target/options/build-output/source keys, normative tag and field order,
fixed codec vectors, selected-versus-rejected module records, an exhaustive
declaration producer matrix, anonymous definition names, typed
`IdentityInvariant` facts mapped to `ZOM9910-ZOM9921`, and a byte-exact
canonical identity dump. The RFC now requires a schema-and-live-producer
architecture gate during implementation; that script does not exist yet and is
not recorded as current evidence. Cross-RFC and normative-document alignment
remain required before re-review.

### 2026-07-10 Identity Owner-Blocker Repair

The draft now fingerprints exact package and crate dependency edges, separates
workspace-relative paths from package-relative paths, gives impl members a
structural `ImplPathSegment`, excludes online `SemanticTypeId` slots from
determinism claims, defines all invariant codes and fatal termination behavior,
retains complete facts before diagnostic grouping, specifies byte-exact dump
and fixed permutation matrices, and adds composite package and dependency-edge
codec vectors. RFC 0008 now maps every parsed module declaration form exactly,
and RFC 0012 emits the dependency and path records consumed here. Unproduced
enum, FFI, marker-declaration, positional-struct, and macro AST
placeholders were deleted rather than counted as producer coverage.

### 2026-07-10 Module Declaration Preservation Repair

The recursive parser and AST schema now preserve every accepted leading module
form as a typed `ModuleDeclaration`: root declaration, inline root, and alias.
The node stores the declared name, optional alias target, inline statement-list
items, and exported-alias state. `export module ... = ...;` is classified as the
source file's leading module instead of a top-level export statement. The old
single-path payload and every caller were deleted.

The sanitizer build, all 259 parser unit tests, all 51 Chapter 13 AST tests,
all 50 Chapter 13 grammar verdicts, parser coverage, AST coverage, RFC checks,
format checks, and diff checks pass. This resolves the parser information-loss
blocker; it does not resolve the remaining normative-spec or architecture-gate
blockers.

### 2026-07-10 Semantic And Invariant Owner Re-Review

The `module-system` and `binder-checker` owners approved entry to `REVIEW`
after RFC 0011 became the direct owner of `TargetName` and `DependencyAlias`,
crate edges preserved their complete package-edge provenance, and RFCs 0008 and
0012 consumed those closed identities without a dependency cycle. The
`error-system` and `verification` owners independently approved the exact
`ZOM9910-ZOM9921` mapping, deterministic fact handling, byte-exact dump,
permutation matrix, and all codec vectors, including the 406-byte
`CrateDependencyEdgeKey` fixture. These are entry-gate approvals only; they do
not accept the RFC.

The `ir-backend` owner, with an advisory `runtime-memory` review, approved entry
after the RFC made the semantic type store context-global, kept registry brands
only on store-local identities, expanded every persisted identity to canonical
keys, defined exact module-interface revision encoding, and excluded object
addresses, allocation order, and online handle slots from observable identity.
This is also an entry-gate approval only.

### 2026-07-11 RFC Owner Entry Re-Review

The `rfc` owner approved entry to `REVIEW` after independently rechecking
frontmatter, index and transition legality, required-owner parity, prior art,
closed questions, rollout and rollback, the acyclic cross-RFC ownership graph,
and every fixed codec oracle. The owner also confirmed that unsupported macro
producers and identity kinds no longer remain in the proposal or live AST
surface. This approval does not accept the RFC.

The `task-router` owner initially blocked entry because the implementation path
did not assign complete ownership for AST schema and generated files, the
identity architecture gate, or compiler build wiring. The routing manifest,
agent contracts, routing matrix, and Repository Impact now assign those paths
to `lexer-parser`, `verification`, and `ir-backend`, respectively. The owner
approved entry after re-review. This approval does not accept the RFC.

### 2026-07-11 Parser And Specification Entry Re-Review

The `lexer-parser` owner approved entry after the recursive parser stopped an
inline module at its matching closing brace, preserved the following top-level
declaration, removed the unproduced marker-declaration helper, and aligned the
declaration inventory. The labeled-statement contract is now structural in
both grammars: labels target loops, blocks, or another label, and negative
fixtures cover declarations, expressions, and an intervening outer attribute.

The `spec-audit` owner approved entry after the macro placeholder surface and
empty inventory were removed, Chapters 5, 6, 9, 12, 16, and 17 were reconciled
with both parsers, grammar inventory documentation was updated to 793 verdicts,
and focused label and module regressions passed. These are entry-gate approvals
only; neither owner accepts the RFC by approving entry to focused review.

### 2026-07-11 Acceptance Review Return And Repair

The separate acceptance review did not approve RFC 0011. Owner reviews found
that `PackageName`, `ResolvedVersion`, `SortedFeatureSet`, target components,
environment names, definition names, and `CanonicalUrl` still required
implementation invention. They also found that decomposed Unicode identifier
spellings could be rejected as encoder invariants, query credentials could
enter package identity, unnamed declaration parameters had no definition
identity, and the proposed builtin marker and five synthetic-definition roles
had no live producer.

The proposal now defines every canonical text field through a named strong
scalar domain. Source identifiers normalize to NFC at semantic-name
construction while original bytes remain available for diagnostics; encoder
invariants apply only after that source boundary. `FeatureName`, semantic
versions, target components and features, environment names, path segments,
module segments, and definition names have exact validation and encoding rules.
`CanonicalUrl` is a closed `https`/`ssh` RFC 3986 profile that rejects user
information, queries, and fragments and has fixed normalization vectors.

Producerless identities were deleted. Marker classification belongs to a real
`Interface` definition and primitive types belong to `SemanticTypeStore`; no
builtin marker `DefId`, builtin source origin, synthetic definition role, or
synthetic anonymous-name role remains. Definition and impl codec fixtures were
recomputed after removing the synthetic-role byte. The declaration grammar now
requires a named parameter or explicit `this`, while structural function-type
parameters remain unnamed type components and receive no `DefId`.

Cross-RFC review also found that preparatory build-script graphs omitted the
host-compiled transitive target dependencies of build-dependency libraries.
RFC 0008 now defines the complete closure. RFC 0009 still published a mutually
incompatible REVIEW contract based on `TypeId`, `SymbolId`, names, AST impl
nodes, early vtable slots, and `ErrorTarget`; it was legally moved to
`RETURNED`. None of these repairs is an acceptance approval. Every required
owner must re-review the repaired proposal before frontmatter approvers or a
decision can be recorded.

## Owner Review Checklist

| Owner | Review State | Blocking Surface |
|---|---|---|
| `task-router` | Approved for REVIEW | AST producer inventory, architecture gate, compiler build wiring, and dependency routing re-reviewed |
| `rfc` | Approved for REVIEW | Governance, ownership graph, implementation path, rollout, rollback, and codec oracles re-reviewed |
| `lexer-parser` | Approved for REVIEW | Preserved module forms, source boundaries, declaration producers, marker cleanup, and structural label targets re-reviewed |
| `module-system` | Approved for REVIEW | Package, target, build-output, dependency-edge, source, duplicate-module, and global registry contracts re-reviewed |
| `binder-checker` | Approved for REVIEW | Anonymous definitions, producer mapping, closed manifest-name identities, and the one semantic type issuer re-reviewed |
| `error-system` | Approved for REVIEW | Typed invariant facts, exact fatal code allocation, redaction, and pre-freeze ordering re-reviewed |
| `ir-backend` | Approved for REVIEW | Brand issuers, semantic type identity, fingerprint inputs, generated sources, and module-interface revision encoding re-reviewed |
| `spec-audit` | Approved for REVIEW | Normative parser surface, macro deletion, module boundaries, label targets, and conformance inventory re-reviewed |
| `verification` | Approved for REVIEW | Codec vectors, dump grammar, proposed architecture gate, and fixed permutation matrix re-reviewed |

This table records only the `DRAFT -> REVIEW` entry gate. RFC acceptance
approval is recorded in frontmatter only after every required owner completes
the separate acceptance review.

## Acceptance Review

| Owner | Decision | Final reviewed surface |
|---|---|---|
| `task-router` | Approved | Required-owner parity, repository routing, generated files, architecture gate, dependency graph, and status transition |
| `rfc` | Approved | Governance, unique acceptance criteria, active dependency chain, prior art, rollout, rollback, and atomic decision record |
| `lexer-parser` | Approved | Named declaration parameters, structural function-type parameters, AST schema, Unicode name boundary, grammar verdict, and AST coverage |
| `module-system` | Approved | Package, crate, source, module, URL, resolver input, build-script host closure, freeze order, and dependency isolation |
| `binder-checker` | Approved | Definition and impl identity, producer inventory, semantic type issuer, marker identity, Unicode redeclaration ordering, and diagnostic provenance |
| `error-system` | Approved | Identity invariant algebra, `ZOM9910-ZOM9921`, credential redaction, Unicode user-error split, and `ZOM3017` note contract |
| `ir-backend` | Approved | Context and registry brands, semantic/store-local handles, fingerprints, generated source identity, codec boundaries, and HIR/MIR/LIR consumption |
| `spec-audit` | Approved | Scalar domains, Unicode, idempotent URL normalization, producer deletion, cross-RFC consistency, fixed vectors, and normative grammar alignment |
| `verification` | Approved | Independent codec and hash recomputation, permutation plan, architecture gate, 794 grammar inventory, focused regressions, and 1178-test full suite |

All approvals apply to the repaired final revision. No blocking objection or
open question remains. Acceptance freezes the design contract only; it does not
claim that `compiler/identity`, the architecture gate, or any dependent
implementation exists.

## Decision Record

Decision: ACCEPTED.

On 2026-07-11, every required owner approved RFC 0011 after the acceptance
review blockers were repaired and independently rechecked. The accepted design
owns semantic context branding, canonical scalar and key encoding, package,
crate, source, module, definition, impl and semantic-type handle boundaries,
deterministic fingerprints, and structured identity invariants. Implementation
was `TBD` at acceptance. The proposal later moved through the legal
`ACCEPTED -> IMPLEMENTING` transition when the coordinated direct-replacement
series began.

The current active acceptance order is RFC 0011; RFC 0012 and RFC 0004; RFC
0005; RFC 0008; then RFC 0010. RFC 0009 is excluded while `RETURNED`; its stale
dependency set is not an accepted design edge and must be recomputed when that
proposal returns to `DRAFT`. Review may overlap, but acceptance follows the
active `requires` graph.

## Implementation Tracker

Implementation started on 2026-07-11 after the explicit
`ACCEPTED -> IMPLEMENTING` transition. The completed first slice contains:

1. compiler and unit-test build wiring for `compiler/identity`;
2. the process-root `SemanticContextFactory` and one context-owned
   `RegistryBrandIssuer` claim;
3. shared thread-safe non-reusing registry-token allocation that prevents
   collisions even across contexts;
4. private-construction `ContextHandle<Tag>` and `StoreHandle<Tag>` value
   types whose equality includes the context and, where required, registry
   issuer; and
5. a self-contained SHA-256 implementation plus fixed-width big-endian,
   boolean, digest, byte-string, sequence-count, and optional-tag canonical
   encoding primitives; and
6. sanitizer unit coverage for invalid, foreign-context, foreign-registry,
   same-slot, malformed-issuer, and duplicate-issuer cases; and
7. a pinned Unicode 15.1.0 NFC normalization primitive with generated canonical
   combining-class, decomposition, composition-exclusion, and composition
   tables plus the complete official normalization conformance oracle; and
8. all eleven named canonical text domains, `ResolvedVersion`, and
   `SortedFeatureSet`, with source normalization, strict canonical admission,
   domain-specific validation, and canonical encoding; and
9. the closed credential-free `CanonicalUrl` profile with deterministic DNS,
   IPv4, RFC 5952 IPv6, port, percent-encoding, Unicode path, and dot-segment
   normalization; and
10. canonical relative paths, registry and VCS source identities, package and
    dependency-edge keys, compilation configuration, crate and crate-edge
    keys, source origins and files, modules, definitions, and impl keys with
    explicit structural validation and fixed codec vectors.

The first focused handle run exposed that independently constructed issuers for
one context could both allocate registry token one. The public issuer
constructor and per-issuer counters were deleted. Issuers are now claimed once
through the process-root factory and allocate through shared state. The
sanitizer build passes, `brand-test` and `handle-test` pass 2/2, and the
pre-slice full sanitizer inventory passed 1,183/1,183 after the default CTest
preset was corrected to use the sanitizer configure preset.

The encoding slice passes the standard empty and `abc` SHA-256 vectors and the
standard 56-byte padding-boundary vector, plus the RFC-fixed `A` byte
representation, empty sequence, and empty fingerprint-domain oracles. The
implementation uses `zc::ArrayPtr` at every byte and state boundary rather than
raw pointers. `brand-test`, `canonical-encoder-test`, and `handle-test` pass
3/3, all 58 sanitizer unit-test targets pass, and the exact-current-byte default
suite passes 1,186/1,186 in 1,273.13 seconds. This does not claim canonical
Unicode text, URL, scalar, map, closed-value, or composite-key encoding.

The Unicode slice implements full canonical decomposition, canonical combining
class reordering, blocked canonical composition, algorithmic Hangul
decomposition and composition, composition exclusions, malformed UTF-8
rejection, and NFC idempotence. Generated tables are reproducible from pinned
Unicode Character Database 15.1.0 inputs. The compiler table contains 922
non-zero combining-class entries, 2,061 fully expanded canonical
decompositions, and 941 composition pairs. The generated conformance oracle
deduplicates the complete `NormalizationTest.txt` NFC relation to 36,482
inputs; its framed expected-output SHA-256 is
`e2ab0b55ce326a724957b79efe63290de3c971a0aa5166cedec05eb77e448d5b`.
Both generators pass byte-for-byte `--check`, all four identity targets pass,
all 59 sanitizer unit-test targets pass, and the exact-current-byte default
suite passes 1,187/1,187 in 942.01 seconds with the complete grammar oracle
passing in 941.27 seconds. Unicode source provenance, exact input digests, and
Unicode License v3 are recorded under `thirdparty/unicode`. This proves the
normalization primitive only; the strong-scalar admission boundary and
`CanonicalEncoder` non-NFC rejection are not yet implemented.

The scalar slice defines distinct move-only strong types for canonical path
segments, package names, target names, dependency aliases, feature names,
target component and feature names, semantic environment names, semantic
identifiers, module path segments, and declared definition names. Source
constructors NFC-normalize before validation; canonical constructors reject a
non-NFC input. Identifier domains reuse the live lexer identifier and reserved
keyword classification, while declared definition names admit the exact
grammar spellings `this`, `init`, `deinit`, `get`, and `set`. The ASCII domains
enforce the accepted RFC length, case, character, and keyword constraints.
`ResolvedVersion` validates complete
Semantic Versioning 2.0.0 text without integer-width limits or spelling
rewrites. `SortedFeatureSet` sorts by the actual length-prefixed encoded key
bytes and rejects duplicates rather than sorting by ordinary string order.
The sanitizer build, format, RFC, and diff gates pass, and all 61 unit targets
pass. A new exact-current-byte default suite remains pending after this scalar
slice.

The URL slice implements the accepted absolute hierarchical `https` and `ssh`
profile without using the broader HTTP URL parser. It rejects user information,
query and fragment data, opaque and relative forms, invalid DNS labels,
non-ASCII hosts, malformed IP literals, invalid or non-canonical ports,
malformed percent triplets, and invalid percent-encoded UTF-8. DNS and scheme
text are lowercased, IPv4 is rendered as dotted decimal, IPv6 follows RFC 5952
lowercase, leading-zero, longest-zero-run and first-run tie rules, default ports
are removed, and other ports are retained canonically. Path segments decode
only unreserved ASCII and percent-encoded Unicode, NFC-normalize Unicode,
uppercase percent triplets, preserve encoded reserved delimiters, remove dot
segments after decoding, and preserve significant empty and trailing segments.
Every normative RFC 0011 URL vector plus focused DNS, IP, port, Unicode,
delimiter, idempotence, and rejection cases passes. The sanitizer build and all
62 unit targets pass; RFC, format, and diff gates pass. The exact-current-byte
default suite remains pending after the scalar and URL slices.

The composite-key slice implements `CanonicalRelativePath`,
`CanonicalWorkspaceRelativePath`, closed-width SHA-1 and SHA-256
`VcsRevision`, `RegistryIdentity`, `CanonicalPackageSource`, `PackageKey`,
`PackageDependencyEdgeKey`, `CanonicalTargetSpecificationKey`,
`SemanticCompilerOptionsKey`, `BuildScriptOutputKey`, `CompilationConfigKey`,
`CrateKey`, `CrateDependencyEdgeKey`, all four `SourceOriginKey` variants,
`SourceFileKey`, `SourceSpan`, `ModuleKey`, `DefinitionKey`, and `ImplKey`.
Construction rejects unknown closed-enum discriminants, malformed revision
widths, invalid pointer widths, empty definition paths, an impl component at a
definition path tail, and source/module/crate ancestry mismatches. The fixed
canonical byte vectors are package 43, package edge 98, crate 154, crate edge
406, source file 240, module 412, definition 692, and impl 680 bytes, with
SHA-256 values `b0c7b4f55c7faf6d4522b3a6f81e979347436c782d29ad2eeaa09985479d40a6`,
`b4a6fdda29af9e3c0b0d6a21b062aa94be3315bc47bde3f432d46e85766b2751`,
`136b0e54d7750bc21ab3e1b5f7cd1f6046fa8f5bafab919c391444a869a6c537`,
`64fcca3d969d5d52c170d40a8a8db32005853856b61087719d003799c2c387a5`,
`f4198087783111e14911a0f550962f5c010ea2609edfdca47152907d74969102`,
`8ef9b8baabd646bf1a4640a8bd70af16e93bbe979229c21342cbebd0c429b91b`,
`3f9ea55ca0ce091341b59f3cd44b64962e9cf26f4c4e9c19815011a702432ca4`,
and `e71d00f88b11b9ee6bd0a5f2196f9c7506fbe28f341733df1e788cc192d23882`.
The complete sanitizer build, RFC, format, diff, and all three Unicode
generation checks pass, and all 66 unit targets pass. `SourceSpan` currently
enforces ordered offsets and structural source ancestry. An exact-current-byte
default suite remains pending after the scalar, URL, and composite-key slices.

The registry slice adds the process-claimed unique
`SemanticIdentityRegistrySet`, private construction for each tag registry,
canonical encoded-byte sorting, duplicate-key rejection without handle issue,
context-bound lookup, terminal invalidation after post-freeze mutation, and the
enforced package, crate, source, module, definition, and impl freeze schedule.
The module registry uses one global order across crates. Source collection now
owns `ImmutableSourceSnapshot`: it computes and retains the source-content
SHA-256, and `SourceSpan` plus `UnbrandedSourceRange` can only be constructed
through snapshot bounds validation. The context fingerprint consumes only
frozen registries, sorts package and crate edges plus source-content records,
rejects duplicate canonical inputs and two content records for one source key,
and passes both accepted RFC fixtures:
`aa36edfdf536f061cd028efd3cfe5003474aee9aa3ab39f294d3b42a95eaae5e`
and `20d2a8ab26a6a17066de900f472dab2e6222c949c6b01da507753822bc116eac`.

Identity failures now retain complete structured facts with the accepted phase,
kind, API-site, structural-key, optional validated range, and traversal-ordinal
fields. Sorting and adjacent diagnostic grouping follow the RFC order. The
registered fatal diagnostics `ZOM9910` through `ZOM9921` live in
`diagnostics-identity.def`; the adapter emits their occurrence counts without
inventing a source location. The exact `zom.identity` dump grammar is
implemented with canonical key order, all six always-present sections,
lowercase hex, and one final LF. The sanitizer build, format, RFC, and diff gates
pass, all 70 unit targets pass, and the exact-current-byte default sanitizer
suite passes 1,198/1,198 with zero failures in 687.73 seconds. The complete
grammar oracle passes in 687.59 seconds.

The subsequent hardening slice removes cross-factory brand collisions by using
process-wide constant-initialized atomic token allocators while retaining
explicit injected factory objects. Two independent factories cannot issue equal
context or registry brands. Injectable issue budgets prove exhaustion is
rejected before reuse. Same-slot foreign-context rejection now covers
`PackageId`, `CrateId`, `SourceFileId`, `ModuleId`, `DefId`, and `ImplId`.
Generated-source collection compares the origin's declared content digest with
the computed immutable snapshot digest and rejects a mismatch before issuing a
handle. `SourceManagerIdentityResolver` binds only byte-identical live buffers
and resolves only source-key, digest, and bounds-validated unbranded ranges.
The complete sanitizer build, format, RFC, and diff gates pass, and all 70 unit
targets pass after this hardening.

The session-owner slice directly replaces `CompilerDriver` with
`CompilerSession` across the driver library, CLI, and tests. The process-root
CLI owns `SemanticContextFactory`; each session claims one context brand and
the sole identity registry family for that context. The registered
CompilerSession architecture gate proves the exact driver surface, unique
process-root factory, sole registry claim path, and one scheduler. Six negative
fixtures reject the old driver, an alias, a wrapper, a second scheduler,
missing registry ownership, a second factory, and a raw session assertion. The
exact-current compiler default matrix passes 1,200/1,200 after the hardening
and session cutover in
793.44 seconds, including the complete grammar oracle in 793.14 seconds. Two IR
diagnostic architecture tests were registered afterward and pass in the
focused four-architecture-test run; the configured inventory is now 1,202.
The subsequent session-construction repair replaces raw brand/singleton
assertions with registered `ZOM9919` and `ZOM9920` fatal diagnostics. All 70
unit targets and all four architecture targets pass after that repair; a new
full 1,202-test run remains required before RFC completion.

The live-producer slice adds `binder::DefinitionInventory`, which walks the
schema-verified tree before symbol creation and records explicit module,
definition, and impl producers. It classifies module and block bindings,
recursive binding-pattern leaves, pattern-only bindings, declared definitions,
anonymous closure roles, and structural definition-or-impl parents without
issuing handles. The machine-readable
`compiler/identity/definition-producers.json` inventory is consumed by
`scripts/check-identity-architecture.py`. The registered positive gate compares
the AST schema, exact parser construction sites, prebinding handlers, closed
`DefinitionKind` and anonymous-role enums, the empty expansion producer set,
and exact phase-local allowlists for remaining `SymbolId` and pointer-derived
identity surfaces. Six negative fixtures reject a missing schema row, missing
parser producer, missing inventory handler, unknown identity kind, post-parse
semantic producer, and expanded old-identity surface. The focused inventory
unit target passes 3/3, both gate modes pass, the complete sanitizer build
passes, and all 71 unit targets pass.

`CompilerSession::parseSources()` now constructs the prebinding inventory in
the parse worker before publishing the corresponding AST. The session owns one
inventory per buffer behind its mutex boundary and exposes only an owning
`clone()` snapshot, not a mutable map or a reference that outlives the lock.
The session architecture gate requires both the owner and the collection site;
the focused session suite passes 14/14 and the combined session/inventory
positive and negative architecture slice passes 6/6. The complete sanitizer
build and all 71 unit targets pass after the session integration.
The exact-current default sanitizer matrix subsequently passes 1,208/1,208 at
repository commit `676abdaf3ee6bb4c0895c37f8851f7c588efb46f` with zero failures.

The six-registry session-integration slice now constructs package identity from
the verified resolution graph, freezes packages while installing that graph,
and retains only pre-build target selection in the compilation request.
`VerifiedPackageCompilationRequest::finalizeRoots()` constructs complete
`CrateKey` values only after the exact verified build-script result set exists;
targets without a build script finalize with an absent output key. Parsing a
build-script package before that result exists stops with the registered
package diagnostic instead of freezing a partial crate identity. The session
then freezes crates and immutable source snapshots before parsing, modules
after AST and definition-inventory publication, and definitions before impls.
Definition handles are published through `DefinitionIdentityMap` to binder and
symbol consumers. Focused package compilation, session package, and CLI
invocation tests pass 3/3 after the post-build crate-finalization repair.

The semantic-type integration slice gives `SemanticContextFactory` one
move-only `SemanticTypeStoreConstructionToken` claim per semantic context. A
final `CompilerSession` owns one pinned, append-only `SemanticTypeStore` and
`SemanticTypeId` is a context-branded `ContextHandle`. `TypeEnv`, checker,
borrow, constraint, query-cycle, dispatch, IR, error-union layout, lowering,
and dumps all borrow or consume that same store. Primitive, named, tuple,
reference, and canonical flattened union forms are interned by structural key;
lookup rejects invalid, foreign-context, and out-of-range handles. The old
`TypeInterner`, table-local `TypeId`, table-local `SymbolId`, and every
pointer-derived identity allowlist entry are deleted. The sanitizer build and
all 102 unit-test targets passed before the final crate-timing repair; the
three affected package/session/CLI targets passed after that repair.

The identity architecture gate now rejects an old symbol or type identity,
pointer-derived identity, a missing sole semantic type store, premature crate
finalization, duplicate or missing registry freeze sites, and a session phase
schedule other than package, crate, source, module, definition, then impl. Its
positive check and all ten negative fixtures pass. The three phase-local
identity allowlists are empty.

### 2026-07-12 Completion Audit

The completion audit checked every acceptance criterion against executable
evidence rather than the implementation narrative:

| Criteria | Completion evidence |
|---|---|
| 1-2, 17-18, 24 | `brand-test`, `handle-test`, `identity-invariant-test`, and `frozen-registry-test` prove private construction, process-wide issuer uniqueness, context and registry branding, foreign-handle rejection, exhaustion, and structured invariant facts. |
| 3-8, 19, 33 | `package-key-test`, `crate-key-test`, `build-script-key-test`, `build-script-execution-key-test`, and `compiler-session-package-test` prove canonical package and target categories, host/target separation, preparatory build-script keys, post-build final crate keys, configuration identity, and dependency-edge fingerprint inputs. |
| 9-14, 25-27, 34 | `definition-inventory-test`, `compiler-session-test`, `compiler-session-package-test`, `definition-key-test`, and both architecture gates prove source-before-module freezing, the complete global module registry, exhaustive live definition producers, structural definition and impl ancestry, re-export identity, and the exact package, crate, source, module, definition, impl schedule. |
| 15-16, 21, 28, 31, 35 | `canonical-encoder-test`, `canonical-values-test`, `semantic-context-fingerprint-test`, `identity-dump-test`, and `identity-invariant-test` prove fixed encodings and hashes, canonical ordering, explicit traversal ordinals, registered fatal diagnostics, deterministic dumps, and exact line grammar. |
| 20 | Same-slot and same-name tests in `handle-test`, `frozen-registry-test`, `package-key-test`, `crate-key-test`, and `definition-key-test` cover distinct contexts, packages, targets, modules, definitions, and impls. |
| 22 | `check-identity-architecture.py --check` and all ten negative fixtures prove that identity construction uses only the canonical key and registry contracts. |
| 23 | The sanitizer build passes; all 102 unit targets pass; the final default matrix passes 1,238/1,238 in 707.45 seconds; RFC, format, diff, compiler-session, IR diagnostic, identity, vendored-dependency, and repository hygiene gates pass. |
| 29-30 | `canonical-scalar-test`, `canonical-url-test`, `unicode-normalization-test`, and the pinned Unicode generator checks prove every scalar domain, duplicate and normalization policy, and the closed credential-free URL model. |
| 32 | `semantic-type-store-test`, `semantic-type-canonicalization-test`, type-environment, checker, borrow, dispatch, and IR tests prove one session-owned store and context-branded `SemanticTypeId` use across all consumers. |

The architecture audit confirms that all three identity allowlists are empty.
Constructor, destructor, and keyword-named method regressions prove
that every parser-admitted declared name can construct a canonical definition
key. Materialization failures now use the registered package diagnostic path,
and explicit snapshot completion leaves neither corpus-local nor system staging
directories after successful compilation.

Linux native sandbox integration passes 1/1 in the privileged Ubuntu 24.04
arm64 sanitizer environment with delegated CPU, memory, and process cgroup
controllers. Repository hygiene finds no CJK text, workspace-absolute paths,
credential markers, temporary files, or generated user presets in the complete
change set. RFC 0011 is therefore `LANDED`.
# RFC 0015 Accepted Overlay

RFC 0015 was approved at exact review SHA-256
`642836225d54f6fa28f8c27e9985972081dbd221c2e8f3e61a0aafd04fe9bb1e`.
Its accepted-file SHA-256 is
`9704d5651606e8a74034c8af4be5172b4007a6c9f0ee8ea2f5ee183223401c01`.
The overlay directly replaces the RFC 0011 impl-pattern sequence-width and
canonicalization contracts named by RFC 0015.

## RFC 0025 Acceptance Synchronization

### Decision Record Synchronization

On 2026-07-25, RFC 0025 received all 12 required-owner approvals at exact
proposal SHA-256
`4f4085c176a9f391115e12170da93af899e350fa92440d5a51577692faf8bad0`.
Its `R25-02` acceptance transaction adds the exhaustive unversioned
`CompilationUnitIdentity`, `Toolchain(Core)`, and `CoreFile` contracts while
retaining one canonical crate, module, definition, implementation, parameter,
and semantic-type identity family. RFC 0011 remains `LANDED`; that status
describes its existing implementation and is not evidence that the RFC 0025
replacement has landed.

### Implementation And Evidence Binding

| RFC 0025 Task | RFC 0011 Evidence Responsibility |
|---|---|
| `R25-03` | Replace the identity hierarchy, registries, codecs, context fingerprint, invariants, and dumps atomically. |
| `R25-03T` | Prove both compilation-unit alternatives, transitive bytes, ancestry, wrong-branch rejection, and no decode fallback. |
| `R25-03C` | Remove package-only crate access from package request and crate graph production. |
| `R25-03CT` | Migrate every remaining native identity caller before the first complete build. |
| `R25-07` | Replace package-only semantic query roots and publish contextual core query identity. |
| `R25-07T` | Prove the complete atomic identity and query cutover through native and architecture gates. |
| `R25-14` | Register gates that reject package-shaped core identity, obsolete accessors, and dual encodings. |
| `R25-15` | Supply clean-build, fixed-vector, determinism, architecture, and final-owner evidence. |

Only the RFC 0025 tracker may advance these replacement tasks. This tracker
records the accepted dependency without attaching new product evidence.
