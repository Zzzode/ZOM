# RFC 0030 Review And Implementation Tracker

## Discussion Record

### 2026-07-28 R29-12AB Preflight

Independent Binder and verification reviews rejected the prepared RFC 0027 S1
candidate. The schema stated that later S1 work would add partitions, omitted
three accepted diagnostic tags, had no repository consumer, and named tests
that were not implemented or registered.

Verification review also found that the accepted exact-file boundary excluded
the Binder source list, focused ztest, schema gate, and architecture-gate
updates required to prove that `stable-binding-facts.cc` compiles and that the
schema is consumed. Existing Binder gates pass without observing the new
files. RFC 0030 closes that landing-evidence gap before source implementation
continues.

### 2026-07-28 RFC 0031 Schema Model Closure

The `R30-11` audit found that the prepared inventory could not express complete
canonical-sum and runtime-sum ownership, split descriptor ownership, generic
capability failures, or the four canonical package-compilation-request
records. RFC 0031 closed those design gaps before schema implementation.

All required owners approved RFC 0031 proposal SHA-256
`c25fcb18e503ac214a8e92c925fa88108a915c2b15c94409dfecb88b3d9a63d5`
and tracker SHA-256
`d64e7791ed2e2a488c5f57bc07ac341ccfc37d37c220c85131e2c9e846fb8d0d`.
Transaction `rfc0031-accept-20260728-c25fcb18` satisfies the design blocker.
`R30-11` remains pending implementation and may now resume because RFC 0031
`R31-09` is satisfied by that transaction.

### 2026-07-28 RFC 0032 Stable Definition Routing Ledger Closure

The `R30-12C` review found that the authority session retained only
`DefinitionKey`, while the accepted contextual definition key requires
`StableDefinitionQueryKey { module, definition }`. Removed and moved
definitions cannot recover their prior module from a digest or the next
projection.

All required owners approved RFC 0032 proposal SHA-256
`1d519846566992156b16986fc5c75602af403254fce70f48cfb65af9983a6d72`
and tracker SHA-256
`b685d88db1e5c2eef13e97ede1e5c085959d2446e39fd07fe5baac0bf7b2ecbf`.
Transaction `rfc0032-accept-20260728-1d519846` adds the session header to
`R30-12D` and the exact landing set and requires a complete stable-key ledger.
`R30-12C` may resume; no implementation row is completed by this transaction.

### 2026-07-28 RFC 0033 Stable Header Review Partition Closure

Independent Binder and verification reviews rejected the first `R30-12H`
candidate because its three exact files added at least 601 changed source
lines against a 400-line cap. The candidate also used empty aggregate
parameter sequences and omitted the callable position-name invariant.

All required owners approved RFC 0033 proposal SHA-256
`3fc78517c36a5794e01bcaca2dcca8d2a616a04b8737f2e2225282a47eea0422`
and tracker SHA-256
`62750bddda02554197b623f5e667b5e749a583679dac1b137d1ff76e10d452e2`.
Transaction `rfc0033-accept-20260728-3fc78517` replaces `R30-12H` with three
bounded cumulative review tasks and changes no source or final landing scope.

### 2026-07-28 RFC 0034 Stable Header Dependency Review Closure

The RFC 0033 `R30-12H-A` candidate had a 430-line net lower bound before its
missing callable wrong-key mutation. The accepted graph also placed populated
aggregate tests before the parameter codecs required by the production
canonical-sequence builder.

All required owners approved RFC 0034 proposal SHA-256
`098023480fb5d84ef5c29b8e10151c687b896ac7d586f9217ee8370c6e966210`
and tracker SHA-256
`4db534a78efe41bcca93bc9207f851c57337d38688690feaf03374605d3d69f3`.
Transaction `rfc0034-accept-20260728-09802348` supersedes RFC 0033, replaces
the unworkable fact-and-codec review graph, and changes no source or final
landing scope.

### 2026-07-28 RFC 0037 Stable Module Skeleton Review Partition Closure

The `R30-12N` preflight found that eleven Pimpl records and 64 schema fields
could not receive complete invariant and native-test review within 400 changed
lines. It also found that `BoundModuleSkeleton` stored
`CanonicalSequence<StableFailedLookupFact>` before the accepted task graph
defined the complete failed-lookup type and codec.

All required owners approved RFC 0037 proposal SHA-256
`ed0b9170c813e42cf02e8a719886ce47aadec5cfbe2ddb788e24572c7243319e`
and tracker SHA-256
`063ea9961caa03d957b976a24d7f4bc9f7489dbdd0442e48b825975d1467470e`.
Transaction `rfc0037-accept-20260728-ed0b9170` replaces `R30-12N` through
`R30-12Q` with bounded dependency-ordered reviews and changes no source,
immutable base, or final landing scope.

The first `R30-12N-F` candidate subsequently consumed the entire 400-line
allowance for the complete aggregate value and local admission logic.
Verification returned it because the same task could not also contain the
required adversarial matrix. RFC 0037 amendment tasks `R37-08` through
`R37-10` split that review into `R30-12N-F1` and aggregate evidence work. A
subsequent evidence review proved that the complete reachable matrix and
successful scale proof also require separate `R30-12N-F2A` and
`R30-12N-F2B` tasks; the aggregate codec remains blocked until all three
aggregate reviews complete.

### 2026-07-28 RFC 0039 Export Surface Revision Admission Closure

The `R30-12O-D` preflight found that the accepted stable alias record carries
an `ExportSurfaceRevision`, while the owning Binder type exposes no public
operation that reconstructs the typed identity from its decoded digest.
`computeFramed(...)` requires a complete canonical preimage that is not part
of the alias record.

RFC 0039 inserts a bounded Binder-owned digest-admission prerequisite, adds
`binding-metadata.{h,cc}` to the same cumulative atomic landing set, and
preserves `R30-15` as the only source commit and push.

## Decision Record

Accepted by `task-router`, `rfc`, `module-system`, `binder-checker`,
`error-system`, and `verification` against exact proposal SHA-256
`4ed0e6b885abc87a1c4251855780cf115a85b3623b1d46f774a4b664110f7b6b`
and tracker SHA-256
`31eb9abae5aa70465a8408e05130263a75cea4ca91c0ada8ac673d238c2664f9`.
The synchronized acceptance transaction is
`rfc0030-accept-20260728-4ed0e6b8`.

RFC 0031 acceptance transaction `rfc0031-accept-20260728-c25fcb18`
synchronizes this tracker to proposal SHA-256
`c25fcb18e503ac214a8e92c925fa88108a915c2b15c94409dfecb88b3d9a63d5`
and tracker SHA-256
`d64e7791ed2e2a488c5f57bc07ac341ccfc37d37c220c85131e2c9e846fb8d0d`.
It establishes the complete schema entity, macro, task, ownership,
capability-failure, and verification model without changing implementation
status.

RFC 0032 acceptance transaction `rfc0032-accept-20260728-1d519846`
synchronizes this tracker to proposal SHA-256
`1d519846566992156b16986fc5c75602af403254fce70f48cfb65af9983a6d72`
and tracker SHA-256
`b685d88db1e5c2eef13e97ede1e5c085959d2446e39fd07fe5baac0bf7b2ecbf`.
It establishes the complete authority-session routing ledger and corrected
exact landing set without changing implementation status.

RFC 0033 acceptance transaction `rfc0033-accept-20260728-3fc78517`
synchronizes this tracker to proposal SHA-256
`3fc78517c36a5794e01bcaca2dcca8d2a616a04b8737f2e2225282a47eea0422`
and tracker SHA-256
`62750bddda02554197b623f5e667b5e749a583679dac1b137d1ff76e10d452e2`.
It establishes the three bounded stable-header review tasks without changing
source semantics, exact landing scope, or implementation status.

RFC 0034 acceptance transaction `rfc0034-accept-20260728-09802348`
synchronizes this tracker to proposal SHA-256
`098023480fb5d84ef5c29b8e10151c687b896ac7d586f9217ee8370c6e966210`
and tracker SHA-256
`4db534a78efe41bcca93bc9207f851c57337d38688690feaf03374605d3d69f3`.
It supersedes RFC 0033 and establishes the executable stable-header review
graph without changing source semantics, exact landing scope, or
implementation status.

RFC 0037 acceptance transaction `rfc0037-accept-20260728-ed0b9170`
synchronizes this tracker to proposal SHA-256
`ed0b9170c813e42cf02e8a719886ce47aadec5cfbe2ddb788e24572c7243319e`
and tracker SHA-256
`063ea9961caa03d957b976a24d7f4bc9f7489dbdd0442e48b825975d1467470e`.
It establishes the bounded module-skeleton review graph and authorizes
implementation to resume at `R30-12N-A` without changing source semantics,
exact landing scope, or implementation status.

RFC 0039 acceptance transaction `rfc0039-accept-20260728-de7ab2aa`
synchronizes this tracker to proposal SHA-256
`de7ab2aa3e571b39aa4c67a48ab32ca219c2f74241fc72dc4ae3c89ffc35cd1a`
and tracker SHA-256
`253766beefaee323618cc9a589ea015258d19cba16a1cf5e285c39c23b8d7e8b`.
It inserts typed export-surface revision admission before `R30-12O-D`,
expands the atomic landing set, and changes no source or implementation
status.

The earlier approvals against proposal SHA-256
`44f0ed68bdd3635e7ed736efcf2dfb2cef0a499c89733d1ab1334a89dce55151`
and tracker SHA-256
`6cad557ce001e5ac7a64acbb7dbd66236a07c76c3f73972f3023050621373913`
are retained as review history but did not authorize the expanded final tree.

RFC 0030 is the sole authority for the expanded `R29-12AB` landing set and
verification contract. RFC 0027 `S1`, `S2`, and `S3` remain bounded review
partitions but have one atomic landing. RFC 0027 `S6` remains the separate
`R29-12D` diagnostic transaction. Runtime work may resume at `R29-13A` only
after both transactions land and pass their native gates. The immutable
implementation-series base remains
`109947943519ec2d380a3e8d71813b40bc68bde5`.

## Review Tracker

| Task | Owner | Depends On | Deliverable | Verification | Status |
|---|---|---|---|---|---|
| `R30-01` | `rfc` | None | Complete RFC 0030, tracker, and index row. | `python3 scripts/check-rfc.py` | Complete |
| `R30-02` | `task-router` | `R30-01` | Review ownership, exact landing set, and gate routing. | Exact-hash review | Complete |
| `R30-03` | `rfc` | `R30-01` | Review governance, scope, prior art, and synchronized RFC impact. | Exact-hash review | Complete |
| `R30-04` | `module-system` | `R30-01` | Review contextual key ownership and duplicate-declaration cutover. | Exact-hash review | Complete |
| `R30-05` | `binder-checker` | `R30-01` | Review S1 authority, S2 type boundary, and bounded review partitions. | Exact-hash review | Complete |
| `R30-06` | `error-system` | `R30-01` | Review fixed identity-diagnostic tags and mappings. | Exact-hash review | Complete |
| `R30-07` | `verification` | `R30-01` | Review native tests, schema mutations, CMake wiring, isolation, and complete gates. | Exact-hash review | Complete |
| `R30-08` | `rfc` | `R30-02`; `R30-03`; `R30-04`; `R30-05`; `R30-06`; `R30-07` | Record one unchanged proposal hash and all owner decisions; prepare synchronized RFC 0025 through RFC 0030, affected tracker and index overlays, plus `.agents/subagents/manifest.yaml`, `task-router.md`, `verification.md`, and `binder-checker.md` while RFC 0030 remains REVIEW. | `python3 scripts/check-rfc.py` | Complete; proposal `4ed0e6b885abc87a1c4251855780cf115a85b3623b1d46f774a4b664110f7b6b`, tracker `31eb9abae5aa70465a8408e05130263a75cea4ca91c0ada8ac673d238c2664f9` |
| `R30-09` | `rfc` | `R30-08` | Accept one synchronized RFC 0025 through RFC 0030, affected tracker, index, and routing transaction. | RFC, English-only, versioning, format, and diff gates | Complete; transaction `rfc0030-accept-20260728-4ed0e6b8` |

## Implementation Tracker

| Task | Owner | Depends On | Deliverable | Verification | Status |
|---|---|---|---|---|---|
| `R30-11` | `binder-checker` with `module-system`, `error-system`, `runtime-memory`, and `verification` review | RFC 0031 `R31-09` | Replace the complete unlanded schema candidate with the accepted RFC 0031 entity, macro, task, ownership, sum, input, capability, diagnostic, and package-record model. | Schema check and self-test | Pending |
| `R30-12A` | `binder-checker` with `module-system` and `verification` review | `R30-11` | In `stable-binding-facts.{h,cc}` and `stable-binding-facts-test.cc`, implement the seven `Stable*QueryKey` routing types and canonical sequence shells; at most 400 changed source lines. | Stable key ownership and move-only tests | Pending |
| `R30-12B` | `binder-checker` with `verification` review | `R30-12A` | In `stable-binding-codec.{h,cc}` and `stable-binding-facts-test.cc`, implement the seven stable-key codecs, sequence builder core, and key wire oracles; at most 400 changed source lines. | Stable key wire and mutation tests | Pending |
| `R30-12C` | `module-system` with `binder-checker` and `verification` review | `R30-12B` | In `driver/contextual-binding-key.{h,cc}` and `active-definition-authority-query-test.cc`, implement `ContextualBodyOwnerKey`, `ContextualCompilationUnitKey`, `ContextualCrateKey`, `ContextualSourceKey`, `ContextualModuleKey`, `ContextualDefinitionKey`, `ContextualImplementationKey`, `ContextualGenericParameterKey`, and `ContextualCallableParameterKey` plus their codecs; at most 400 changed source lines. | Context ownership, payload, and wire tests | Pending |
| `R30-12D` | `module-system` with `binder-checker` and `verification` review | `R30-12C`; RFC 0032 `R32-07` | In `active-definition-authority-query.{h,cc}`, `active-definition-authority-session.{h,cc}`, `active-definition-authority-query-test.cc`, and `active-definition-authority-session-test.cc`, delete the two query-specific contextual declarations, replace the authority ledger with complete `StableDefinitionQueryKey` values, and migrate authority callers; at most 400 changed source lines. | Authority caller-cutover, removal, rename, module removal, movement, and failure-atomicity tests | Pending |
| `R30-12E` | `module-system` with `binder-checker` review | `R30-12D` | In `named-item-query.{h,cc}`, migrate every contextual definition caller; at most 400 changed source lines. | Named-item caller-cutover tests | Pending |
| `R30-12F` | `module-system` with `binder-checker` review | `R30-12E` | In `owner-body-query.{h,cc}`, delete the query-specific body-owner declaration and migrate body, module, and definition contextual callers; at most 400 changed source lines. | Owner-body caller-cutover tests | Pending |
| `R30-12G` | `module-system` with `binder-checker` review | `R30-12F` | In `compiler-session.cc`, migrate the remaining contextual callers and prove zero references to the removed declarations; at most 400 changed source lines. | Compiler-session caller-cutover and zero-reference tests | Pending |
| `R30-12H-A1` | `binder-checker` with `verification` review | `R30-12G`; RFC 0034 `R34-07` | In `stable-binding-facts.{h,cc}` and `stable-binding-facts-test.cc`, implement `DefinitionBodyDisposition`, `ImplementationSourceForm`, `ScopeRole`, and `StableHeaderSite`; count additions plus deletions across all three files from the exact approved predecessor and allow at most 400 changed source lines. | Every closed tag, unknown-value rejection, both site variants, clone, and inequality tests | Pending |
| `R30-12H-A2` | `binder-checker` with `verification` review | `R30-12H-A1` | In the same three files, implement `StableHeaderGenericParameter`; count additions plus deletions from the exact approved predecessor hashes and allow at most 400 changed source lines. | Generic key-record, owner-site, ordinal, clone, and inequality tests | Pending |
| `R30-12H-A3` | `binder-checker` with `verification` review | `R30-12H-A2` | In the same three files, implement `StableHeaderCallableParameter`; count additions plus deletions from the exact approved predecessor hashes and allow at most 400 changed source lines. | Callable key-record, definition-site, position-name, clone, and inequality tests | Pending |
| `R30-12I-A` | `binder-checker` with `verification` review | `R30-12H-A3` | In `stable-binding-codec.{h,cc}` and `stable-binding-facts-test.cc`, implement enum, site, generic-parameter, and callable-parameter codecs; count additions plus deletions across all three files from the exact approved predecessor and allow at most 400 changed source lines. | Independent wire oracles and complete primitive and parameter mutations | Pending |
| `R30-12H-B` | `binder-checker` with `verification` review | `R30-12I-A` | In `stable-binding-facts.{h,cc}` and `stable-binding-facts-test.cc`, implement `StableDefinitionHeader`; count additions plus deletions from the exact approved predecessor hashes and allow at most 400 changed source lines. | Populated production-built generic and callable sequences plus complete definition-header mutations | Pending |
| `R30-12I-B` | `binder-checker` with `verification` review | `R30-12H-B` | In `stable-binding-codec.{h,cc}` and `stable-binding-facts-test.cc`, implement the `StableDefinitionHeader` codec; count additions plus deletions from the exact approved predecessor hashes and allow at most 400 changed source lines. | Definition-header wire, sequence, truncation, trailing-byte, and unknown-tag mutations | Pending |
| `R30-12H-C` | `binder-checker` with `verification` review | `R30-12I-B` | In `stable-binding-facts.{h,cc}` and `stable-binding-facts-test.cc`, implement `StableImplementationOccurrenceHeader`; count additions plus deletions from the exact approved predecessor hashes and allow at most 400 changed source lines. | Populated production-built generic sequence plus complete implementation-header mutations | Pending |
| `R30-12I-C` | `binder-checker` with `verification` review | `R30-12H-C`; RFC 0035 `R35-15` | In `stable-binding-codec.{h,cc}` and `stable-binding-facts-test.cc`, implement the `StableImplementationOccurrenceHeader` codec through the identity-owned `ImplIdentityRecord::decodeCanonical`; count additions plus deletions from the exact approved predecessor hashes and allow at most 400 changed source lines. | Implementation-header wire, sequence, truncation, trailing-byte, and unknown-tag mutations | Pending |
| `R30-12J` | `binder-checker` with `verification` review | `R30-12I-C` | In `stable-binding-facts.{h,cc}` and `stable-binding-facts-test.cc`, implement `StableScopeOwnerKey`, `StableNodeSyntaxRoot`, and `StableBindingTargetKey`; at most 400 changed source lines. | Scope, syntax-root, and target fact tests | Pending |
| `R30-12K` | `binder-checker` with `verification` review | `R30-12J` | In `stable-binding-codec.{h,cc}` and `stable-binding-facts-test.cc`, implement the matching scope, syntax-root, and target codecs and wire oracles; at most 400 changed source lines. | Closed-sum wire and mutation tests | Pending |
| `R30-12L` | `binder-checker` with `verification` review | `R30-12K` | In `stable-binding-facts.{h,cc}` and `stable-binding-facts-test.cc`, implement `BinderQueryOwner`, `BinderKeyFailureKind`, `BinderKeyFailure`, and `BinderQueryResult<T>`; at most 400 changed source lines. | Owner, failure, and result-algebra tests | Pending |
| `R30-12M` | `binder-checker` with `verification` review | `R30-12L`; RFC 0036 `R36-16` | In `stable-binding-codec.{h,cc}` and `stable-binding-facts-test.cc`, implement the matching owner, failure, and result codecs and wire oracles through the diagnostics-owned explicit limits API; at most 400 changed source lines. | Failure and result wire mutation tests plus the 4,097-fact Binder boundary | Pending |
| `R30-12N-A` | `binder-checker` with `verification` review | `R30-12M`; RFC 0037 `R37-07` | In `stable-binding-facts.{h,cc}` and `stable-binding-facts-test.cc`, implement `StableScopeFact` and `StableNodeScopeFact`; at most 400 changed source lines. | Scope and node-scope fact tests | Pending |
| `R30-12O-A` | `binder-checker` with `verification` review | `R30-12N-A` | In `stable-binding-codec.{h,cc}` and `stable-binding-facts-test.cc`, implement matching scope codecs and wire oracles; at most 400 changed source lines. | Scope wire mutation tests | Pending |
| `R30-12N-B` | `binder-checker` with `verification` review | `R30-12O-A` | In the fact files, implement `StableDeclarationFact` and `StableImplementationOccurrenceFact`; at most 400 changed source lines. | Declaration fact and relation tests | Pending |
| `R30-12O-B` | `binder-checker` with `verification` review | `R30-12N-B` | In the codec files, implement matching declaration codecs and wire oracles; at most 400 changed source lines. | Declaration wire mutation tests | Pending |
| `R30-12N-C` | `binder-checker` with `verification` review | `R30-12O-B` | In the fact files, implement both parameter-declaration facts; at most 400 changed source lines. | Parameter declaration fact tests | Pending |
| `R30-12O-C` | `binder-checker` with `verification` review | `R30-12N-C` | In the codec files, implement matching parameter-declaration codecs and wire oracles; at most 400 changed source lines. | Parameter declaration wire mutation tests | Pending |
| `R30-12N-D` | `binder-checker` with `verification` review | `R30-12O-C` | In the fact files, implement `StableImportFact` and `StableModuleAliasFact`; at most 400 changed source lines. | Import and alias fact tests | Pending |
| `R30-12O-D` | `binder-checker` with `verification` review | `R30-12N-D`; RFC 0039 `R39-11` | In the codec files, implement matching import and alias codecs and wire oracles through `ExportSurfaceRevision::fromDigest`; at most 400 changed source lines. | Import and alias wire mutation tests | Pending |
| `R30-12N-E` | `binder-checker` with `verification` review | `R30-12O-D`; RFC 0040 `R40-11` | In the fact files, implement `StableReexportStep` and `StableLocalExportFact` through validated `BindingNameKey` admission; at most 400 changed source lines. | Reexport and local-export fact tests | Pending |
| `R30-12O-E` | `binder-checker` with `verification` review | `R30-12N-E` | In the codec files, implement matching reexport and local-export codecs and wire oracles; at most 400 changed source lines. | Reexport and local-export wire mutation tests | Pending |
| `R30-12P-A` | `binder-checker` with `verification` review | `R30-12O-E` | In the fact files, implement `StableFailedLookupOutcome` and `StableFailedLookupFact`; at most 400 changed source lines. | Failed-lookup fact and closed-outcome tests | Pending |
| `R30-12Q-A` | `binder-checker` with `verification` review | `R30-12P-A` | In the codec files, implement matching failed-lookup codecs and wire oracles; at most 400 changed source lines. | Failed-lookup wire mutation tests | Pending |
| `R30-12N-F1` | `binder-checker` with `verification` review | `R30-12Q-A`; RFC 0037 `R37-10` | In `stable-binding-facts.{h,cc}` and `stable-binding-facts-test.cc`, implement the complete `BoundModuleSkeleton` value, local admission logic, linear indexes, iterative parent-graph validation, populated accessors, and bounded smoke evidence; at most 400 changed source lines. | Aggregate value, clone, inequality, foreign export, and module-body-owner presence tests | Pending |
| `R30-12N-F2A` | `binder-checker` with `verification` review | `R30-12N-F1`; RFC 0037 `R37-10D` | In `stable-binding-facts-test.cc` only, add reachable scope-graph, semantic-uniqueness, missing-scope, definition-owned parameter, alias, missing definition-body declaration, failed-lookup, and canonical implied-invariant evidence; at most 400 changed source lines. | Structural and relational module-skeleton adversarial tests | Pending |
| `R30-12N-F2B` | `binder-checker` with `verification` review | `R30-12N-F2A` | In `stable-binding-facts-test.cc` only, add remaining per-family ownership, remaining eleven sequence-accessor, implementation-owned generic, and reference-complete accepted deep-chain evidence; at most 400 changed source lines. | Ownership, sequence-accessor, and scale module-skeleton tests | Pending |
| `R30-12O-F` | `binder-checker` with `verification` review | `R30-12N-F2B` | In the codec files, implement the complete module-skeleton codec and wire oracle; at most 400 changed source lines. | Complete module-skeleton wire mutation tests | Pending |
| `R30-12P-B` | `binder-checker` with `verification` review | `R30-12O-F` | In the fact files, implement `StableExportedBinding`, `StableExportedBindingQueryKey`, and `StableScopeNameBucketQueryKey`; at most 400 changed source lines. | Projection fact and key tests | Pending |
| `R30-12Q-B` | `binder-checker` with `verification` review | `R30-12P-B` | In the codec files, implement matching projection codecs and wire oracles; at most 400 changed source lines. | Projection wire mutation tests | Pending |
| `R41-11A` | `binder-checker` with `verification` review | `R30-12Q-B`; RFC 0041 `R41-07` | In the fact files, implement body scope and body node-scope facts; at most 400 changed source lines. | Fact invariants and exact hashes | Pending |
| `R41-12A` | `binder-checker` with `verification` review | `R41-11A` | In the codec files, implement matching body scope codecs and independent wire oracles; at most 400 changed source lines. | Wire mutations and exact hashes | Pending |
| `R41-11B` | `binder-checker` with `verification` review | `R41-12A` | In the fact files, implement owner-local binding and resolution facts; at most 400 changed source lines. | Fact invariants and exact hashes | Pending |
| `R41-12B` | `binder-checker` with `verification` review | `R41-11B` | In the codec files, implement matching binding and resolution codecs; at most 400 changed source lines. | Wire mutations and exact hashes | Pending |
| `R41-11C` | `binder-checker` with `verification` review | `R41-12B` | In the fact files, implement deferred-member facts; at most 400 changed source lines. | Fact invariants and exact hashes | Pending |
| `R41-12C` | `binder-checker` with `verification` review | `R41-11C` | In the codec files, implement the deferred-member codec; at most 400 changed source lines. | Populated sequence wire mutations | Pending |
| `R41-11D` | `binder-checker` with `verification` review | `R41-12C` | In the fact files, implement stable self-owner, self-type, and receiver facts; at most 400 changed source lines. | Closed variants and routing invariants | Pending |
| `R41-12D` | `binder-checker` with `verification` review | `R41-11D` | In the codec files, implement matching self and receiver codecs; at most 400 changed source lines. | Closed-tag wire mutations | Pending |
| `R41-11E` | `binder-checker` with `verification` review | `R41-12D` | In the fact files, implement shadow-target facts; at most 400 changed source lines. | Target and ownership invariants | Pending |
| `R41-12E` | `binder-checker` with `verification` review | `R41-11E` | In the codec files, implement the shadow-target codec; at most 400 changed source lines. | Wire mutations and exact hashes | Pending |
| `R41-13A` | `binder-checker` with `verification` review | `R41-12E` | In the fact files, implement label keys, targets, and facts; at most 400 changed source lines. | Closed variants and relation tests | Pending |
| `R41-14A` | `binder-checker` with `verification` review | `R41-13A` | In the codec files, implement matching label codecs; at most 400 changed source lines. | Wire mutations and exact hashes | Pending |
| `R41-13B` | `binder-checker` with `verification` review | `R41-14A` | In the fact files, implement control targets and transfer facts; at most 400 changed source lines. | Closed variants and relation tests | Pending |
| `R41-14B` | `binder-checker` with `verification` review | `R41-13B` | In the codec files, implement matching control codecs; at most 400 changed source lines. | Wire mutations and exact hashes | Pending |
| `R41-13C` | `binder-checker` with `verification` review | `R41-14B` | In the fact files, implement closure and free-variable facts; at most 400 changed source lines. | Populated sequences and ownership tests | Pending |
| `R41-14C` | `binder-checker` with `verification` review | `R41-13C` | In the codec files, implement matching closure codecs; at most 400 changed source lines. | Wire mutations and exact hashes | Pending |
| `R41-13D` | `binder-checker` with `verification` review | `R41-14C` | In the fact files, implement explicit capture modes and facts; at most 400 changed source lines. | Closed modes and populated captures | Pending |
| `R41-14D` | `binder-checker` with `verification` review | `R41-13D` | In the codec files, implement matching explicit-capture codecs; at most 400 changed source lines. | Wire mutations and exact hashes | Pending |
| `R41-15F1` | `binder-checker` with `verification` review | `R41-14D` | In the fact files, implement the complete `BoundOwnerBody` value and bounded smoke evidence; at most 400 changed source lines. | Pimpl, factory, indexes, and accessors | Pending |
| `R41-15F2A` | `binder-checker` with `verification` review | `R41-15F1` | In the native test only, add structural and relational aggregate evidence; at most 400 changed source lines. | Exact test predecessor and line cap | Pending |
| `R41-15F2B` | `binder-checker` with `verification` review | `R41-15F2A` | In the native test only, add ownership, multiplicity, accessor, and scale evidence; at most 400 changed source lines. | Exact test predecessor and line cap | Pending |
| `R41-16` | `binder-checker` with `verification` review | `R41-15F2B` | In the codec files, implement the complete `BoundOwnerBody` codec and independent aggregate oracle; at most 400 changed source lines. | Complete wire mutations | Pending |
| `R30-12X` | `binder-checker` with `verification` review | RFC 0041 `R41-16` | In `stable-binding-facts.{h,cc}` and `stable-binding-facts-test.cc`, implement `ModuleBindingAllocationPlan` and `OwnerAllocationRange`; at most 400 changed source lines. | Allocation fact and overflow tests | Pending |
| `R30-12Y` | `binder-checker` with `verification` review | `R30-12X` | In `stable-binding-codec.{h,cc}` and `stable-binding-facts-test.cc`, implement allocation codecs and wire oracles; at most 400 changed source lines. | Allocation wire mutation tests | Pending |
| `R30-13` | `verification` with `binder-checker`, `module-system`, and `runtime-memory` review | `R30-12Y` | Add build, ztest, schema, CTest, architecture, exact-allowlist, and landing-scope wiring; add the comprehensive Q3 package-request mutation test; enforce staged capability and failure-alternative alias equality checks for implemented descriptors while future rows remain inert. | Focused native, schema mutation, Q3 mutation, and dual-alias architecture gates | Pending |
| `R30-14` | `verification` | `R30-13` | Assemble only the exact landing set in an isolated clean worktree; prove worktree scope, run focused plus complete gates, explicitly stage the allowlist, and prove index scope. | RFC 0030 Test Plan | Pending |
| `R30-15` | `binder-checker` with all affected owners | `R30-14` | Land and publish the atomic `R29-12AB` transaction containing S1, S2, and S3. | Local, upstream, and remote SHA parity | Pending |
| `R30-16` | `error-system` with `binder-checker` and `verification` review | `R30-15` | Land `R29-12D` with the canonical Binder diagnostic facts, exact diagnostic native test, CTest ownership, and diagnostic coverage gates. | Diagnostic fact test and diagnostic coverage check plus self-test | Pending |
| `R30-17` | `rfc` | `R30-16` | Synchronize truthful tracker state and resume `R29-13A`. | RFC and evidence audit | Pending |

The RFC 0031, RFC 0032, RFC 0035, RFC 0036, RFC 0037, RFC 0039, and RFC 0040
design blockers are satisfied by transactions
`rfc0031-accept-20260728-c25fcb18` and
`rfc0032-accept-20260728-1d519846`, and
`rfc0035-accept-20260728-e79c292e`,
`rfc0036-accept-20260728-3bcf4ae9`, and
`rfc0037-accept-20260728-ed0b9170`, and
`rfc0039-accept-20260728-de7ab2aa`, and
`rfc0040-accept-20260728-e007151b`. None of these design transactions completes
a source task. Exact-hash source review subsequently approved RFC 0040
`R40-11`, then RFC 0030 `R30-12N-E`, `R30-12O-E`, `R30-12P-A`, and
`R30-12Q-A` in strict predecessor order. The first complete `R30-12N-F`
candidate received Binder approval and a Verification return at the hashes
recorded in the RFC 0037 tracker. Corrected `R30-12N-F1` subsequently completed
exact-hash approval at facts header
`fe8f6e145a4500901ff3544df2ba2d06854a223de31f252883ef01611782d123`,
facts implementation
`4f959f7a85ceec182c7b59579052fc26e9df3d449d6565e502cace69e6918220`,
and test
`d4b564fd4a7e51a6186647c099f04c0963374ffdaea86e2239af920bac5bffec`,
with 392 additions. The first F2 candidate at test hash
`9dd868623cdc30e103b33d09256faf721a5d72e7f41464f37632d204f3e7b790`
was returned and fully withdrawn. The live source is the approved F1 tuple and
remains cumulative and uncommitted, so the implementation rows remain pending
atomic publication through `R30-15`. RFC 0037 amendment transaction
`rfc0037-amend-20260728-25caf4b9` authorizes the aggregate value and evidence
review boundary. RFC 0037 amendment transaction
`rfc0037-amend-evidence-20260728-d4e18a12` authorizes the refined
`R30-12N-F2A` and `R30-12N-F2B` evidence reviews without landing source.

Exact-hash review then approved `R30-12N-F2A`, `R30-12N-F2B`, `R30-12O-F`,
`R30-12P-B`, and `R30-12Q-B` in strict predecessor order. The current approved
fact tuple is header
`c721f59631a1dddc57275e8834ac100c2dc605d33c0782a206d196ba5572e682`,
implementation
`35b8eebb98b7a179899f5513e0b9161d422e0505078a4b2e1ea1b2f77044a79d`,
and test
`4c566c513bda9b06a15b6ed70daefba1fe9ee26c4f69ef1de28c61118d53e457`.
The current approved codec tuple is header
`c3c6d26bd1f9d469da563c5b01bd6fc71fece7f5d4585a0ab20bc9f68d4126a5`,
implementation
`33adc65a465ee60170090b63e7fd6fd08d2bc18f4f78d972fcb2a77250c2da9f`,
and the same test hash. Owner-body preflight exceeded the review cap before
tests and was withdrawn byte-for-byte. RFC 0041 now carries the bounded
replacement review graph; it changes no source or atomic publication boundary.
