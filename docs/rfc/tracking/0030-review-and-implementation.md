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

## Decision Record

Accepted by `task-router`, `rfc`, `module-system`, `binder-checker`,
`error-system`, and `verification` against exact proposal SHA-256
`4ed0e6b885abc87a1c4251855780cf115a85b3623b1d46f774a4b664110f7b6b`
and tracker SHA-256
`31eb9abae5aa70465a8408e05130263a75cea4ca91c0ada8ac673d238c2664f9`.
The synchronized acceptance transaction is
`rfc0030-accept-20260728-4ed0e6b8`.

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
| `R30-11` | `binder-checker` with `error-system` and `verification` review | `R30-09` | Close the canonical schema, partitions, diagnostic tags, and mappings. | Schema check and self-test | Pending |
| `R30-12A` | `binder-checker` with `module-system` and `verification` review | `R30-11` | In `stable-binding-facts.{h,cc}` and `stable-binding-facts-test.cc`, implement the seven `Stable*QueryKey` routing types and canonical sequence shells; at most 400 changed source lines. | Stable key ownership and move-only tests | Pending |
| `R30-12B` | `binder-checker` with `verification` review | `R30-12A` | In `stable-binding-codec.{h,cc}` and `stable-binding-facts-test.cc`, implement the seven stable-key codecs, sequence builder core, and key wire oracles; at most 400 changed source lines. | Stable key wire and mutation tests | Pending |
| `R30-12C` | `module-system` with `binder-checker` and `verification` review | `R30-12B` | In `driver/contextual-binding-key.{h,cc}` and `active-definition-authority-query-test.cc`, implement `ContextualBodyOwnerKey`, `ContextualCompilationUnitKey`, `ContextualCrateKey`, `ContextualSourceKey`, `ContextualModuleKey`, `ContextualDefinitionKey`, `ContextualImplementationKey`, `ContextualGenericParameterKey`, and `ContextualCallableParameterKey` plus their codecs; at most 400 changed source lines. | Context ownership, payload, and wire tests | Pending |
| `R30-12D` | `module-system` with `binder-checker` and `verification` review | `R30-12C` | In `active-definition-authority-query.{h,cc}`, `active-definition-authority-session.cc`, `active-definition-authority-query-test.cc`, and `active-definition-authority-session-test.cc`, delete the two query-specific contextual declarations and migrate authority callers; at most 400 changed source lines. | Authority caller-cutover tests | Pending |
| `R30-12E` | `module-system` with `binder-checker` review | `R30-12D` | In `named-item-query.{h,cc}`, migrate every contextual definition caller; at most 400 changed source lines. | Named-item caller-cutover tests | Pending |
| `R30-12F` | `module-system` with `binder-checker` review | `R30-12E` | In `owner-body-query.{h,cc}`, delete the query-specific body-owner declaration and migrate body, module, and definition contextual callers; at most 400 changed source lines. | Owner-body caller-cutover tests | Pending |
| `R30-12G` | `module-system` with `binder-checker` review | `R30-12F` | In `compiler-session.cc`, migrate the remaining contextual callers and prove zero references to the removed declarations; at most 400 changed source lines. | Compiler-session caller-cutover and zero-reference tests | Pending |
| `R30-12H` | `binder-checker` with `verification` review | `R30-12G` | In `stable-binding-facts.{h,cc}` and `stable-binding-facts-test.cc`, implement `DefinitionBodyDisposition`, `ImplementationSourceForm`, `ScopeRole`, `StableHeaderSite`, and the four stable header records; at most 400 changed source lines. | Header fact tests | Pending |
| `R30-12I` | `binder-checker` with `verification` review | `R30-12H` | In `stable-binding-codec.{h,cc}` and `stable-binding-facts-test.cc`, implement the matching header codecs and wire oracles; at most 400 changed source lines. | Header wire and mutation tests | Pending |
| `R30-12J` | `binder-checker` with `verification` review | `R30-12I` | In `stable-binding-facts.{h,cc}` and `stable-binding-facts-test.cc`, implement `StableScopeOwnerKey`, `StableNodeSyntaxRoot`, and `StableBindingTargetKey`; at most 400 changed source lines. | Scope, syntax-root, and target fact tests | Pending |
| `R30-12K` | `binder-checker` with `verification` review | `R30-12J` | In `stable-binding-codec.{h,cc}` and `stable-binding-facts-test.cc`, implement the matching scope, syntax-root, and target codecs and wire oracles; at most 400 changed source lines. | Closed-sum wire and mutation tests | Pending |
| `R30-12L` | `binder-checker` with `verification` review | `R30-12K` | In `stable-binding-facts.{h,cc}` and `stable-binding-facts-test.cc`, implement `BinderQueryOwner`, `BinderKeyFailureKind`, `BinderKeyFailure`, and `BinderQueryResult<T>`; at most 400 changed source lines. | Owner, failure, and result-algebra tests | Pending |
| `R30-12M` | `binder-checker` with `verification` review | `R30-12L` | In `stable-binding-codec.{h,cc}` and `stable-binding-facts-test.cc`, implement the matching owner, failure, and result codecs and wire oracles; at most 400 changed source lines. | Failure and result wire mutation tests | Pending |
| `R30-12N` | `binder-checker` with `verification` review | `R30-12M` | In `stable-binding-facts.{h,cc}` and `stable-binding-facts-test.cc`, implement `StableScopeFact`, `StableNodeScopeFact`, `StableDeclarationFact`, `StableImplementationOccurrenceFact`, both parameter-declaration facts, `StableImportFact`, `StableModuleAliasFact`, `StableReexportStep`, `StableLocalExportFact`, and `BoundModuleSkeleton`; at most 400 changed source lines. | Module-skeleton fact tests | Pending |
| `R30-12O` | `binder-checker` with `verification` review | `R30-12N` | In `stable-binding-codec.{h,cc}` and `stable-binding-facts-test.cc`, implement the matching module-skeleton codecs and wire oracles; at most 400 changed source lines. | Module-skeleton wire mutation tests | Pending |
| `R30-12P` | `binder-checker` with `verification` review | `R30-12O` | In `stable-binding-facts.{h,cc}` and `stable-binding-facts-test.cc`, implement `StableFailedLookupOutcome`, `StableFailedLookupFact`, `StableExportedBinding`, `StableExportedBindingQueryKey`, and `StableScopeNameBucketQueryKey`; at most 400 changed source lines. | Lookup and projection fact tests | Pending |
| `R30-12Q` | `binder-checker` with `verification` review | `R30-12P` | In `stable-binding-codec.{h,cc}` and `stable-binding-facts-test.cc`, implement the matching lookup and projection codecs and wire oracles; at most 400 changed source lines. | Lookup and projection wire mutation tests | Pending |
| `R30-12R` | `binder-checker` with `verification` review | `R30-12Q` | In `stable-binding-facts.{h,cc}` and `stable-binding-facts-test.cc`, implement `StableBodyScopeFact`, `StableBodyNodeScopeFact`, `StableOwnerLocalBindingFact`, `StableResolutionFact`, `StableDeferredMemberFact`, `StableSelfOwner`, `StableSelfTypeFact`, `StableThisBindingFact`, and `StableShadowTargetFact`; at most 400 changed source lines. | Owner-body scope and resolution tests | Pending |
| `R30-12S` | `binder-checker` with `verification` review | `R30-12R` | In `stable-binding-codec.{h,cc}` and `stable-binding-facts-test.cc`, implement the matching owner-body scope and resolution codecs and wire oracles; at most 400 changed source lines. | Owner-body scope wire mutation tests | Pending |
| `R30-12T` | `binder-checker` with `verification` review | `R30-12S` | In `stable-binding-facts.{h,cc}` and `stable-binding-facts-test.cc`, implement `StableLabelKey`, `StableLabelTarget`, `StableLabelFact`, `StableControlTarget`, `StableControlTransferFact`, `StableClosureFact`, `StableClosureFreeVariable`, `StableClosureFreeVariableFact`, `StableExplicitCaptureMode`, `StableExplicitCaptureBindingFact`, and `StableExplicitClosureCaptureFact`; at most 400 changed source lines. | Control and closure fact tests | Pending |
| `R30-12U` | `binder-checker` with `verification` review | `R30-12T` | In `stable-binding-codec.{h,cc}` and `stable-binding-facts-test.cc`, implement the matching control and closure codecs and wire oracles; at most 400 changed source lines. | Control and closure wire mutation tests | Pending |
| `R30-12V` | `binder-checker` with `verification` review | `R30-12U` | In `stable-binding-facts.{h,cc}` and `stable-binding-facts-test.cc`, implement the `BoundOwnerBody` aggregate; at most 400 changed source lines. | Complete owner-body aggregate tests | Pending |
| `R30-12W` | `binder-checker` with `verification` review | `R30-12V` | In `stable-binding-codec.{h,cc}` and `stable-binding-facts-test.cc`, implement the `BoundOwnerBody` codec and wire oracles; at most 400 changed source lines. | Complete owner-body wire mutation tests | Pending |
| `R30-12X` | `binder-checker` with `verification` review | `R30-12W` | In `stable-binding-facts.{h,cc}` and `stable-binding-facts-test.cc`, implement `ModuleBindingAllocationPlan` and `OwnerAllocationRange`; at most 400 changed source lines. | Allocation fact and overflow tests | Pending |
| `R30-12Y` | `binder-checker` with `verification` review | `R30-12X` | In `stable-binding-codec.{h,cc}` and `stable-binding-facts-test.cc`, implement allocation codecs and wire oracles; at most 400 changed source lines. | Allocation wire mutation tests | Pending |
| `R30-13` | `verification` with `binder-checker` review | `R30-12Y` | Add build, ztest, schema, CTest, architecture, exact-allowlist, and landing-scope wiring. | Focused native and mutation gates | Pending |
| `R30-14` | `verification` | `R30-13` | Assemble only the exact landing set in an isolated clean worktree; prove worktree scope, run focused plus complete gates, explicitly stage the allowlist, and prove index scope. | RFC 0030 Test Plan | Pending |
| `R30-15` | `binder-checker` with all affected owners | `R30-14` | Land and publish the atomic `R29-12AB` transaction containing S1, S2, and S3. | Local, upstream, and remote SHA parity | Pending |
| `R30-16` | `error-system` with `binder-checker` and `verification` review | `R30-15` | Land `R29-12D` with the canonical Binder diagnostic facts, exact diagnostic native test, CTest ownership, and diagnostic coverage gates. | Diagnostic fact test and diagnostic coverage check plus self-test | Pending |
| `R30-17` | `rfc` | `R30-16` | Synchronize truthful tracker state and resume `R29-13A`. | RFC and evidence audit | Pending |

No implementation task begins before `R30-09`.
