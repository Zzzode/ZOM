# RFC 0027 Review And Implementation Tracker

## Discussion Record

### 2026-07-27 RFC 0025 Implementation Blocker

Implementation mapping found that the accepted Binder query inventory lacked a
complete stable fact model. `BindModuleSkeleton` could not derive the complete
body-owner set during authority staging, `MaterializeModuleSkeleton` omitted
implementation-site provenance, and the first production materialization
prototype used a frozen-registry lookup plus a prohibited session ledger.

RFC 0027 defines the stable Binder schema, exact descriptor read sets,
semantic-context-arena-owned append-only identity interner, capability lease
closure, and atomic production-root replacement required before core role
bootstrap can continue.

### Review Candidates

The first complete review candidate,
`d940570bc31876f1b5b0580ef936bb8b93fcfe4925c21ac3cff2c5e19f7ed44c`,
was rejected before the complete required-owner review finished. Review found
incomplete implementation scope, header inputs, identity authority, lifetime,
failure, synchronization, and verification contracts. No approval is
retained.

The second complete review candidate,
`8abe36d734937cf2b51a7f28cc2d3e03a746f0ac96d4e76eb08e96d8630b74b4`,
was rejected after all nine required-owner scopes independently reviewed the
exact unchanged hash. Findings covered contextual owner-body keys, the three
session transactions, fact and allocation completeness, diagnostic
duplication, codec and descriptor closure, graph materialization authority,
Checker and IR lease lineage, synchronization deletion scope, routing,
coverage, and the Release benchmark protocol. No approval is retained.

The third complete review candidate,
`def9c2f82597c4c70a39622a2e6182d26b1fbb70589cdc7eda00c5ccc8f38c4e`,
was rejected after all nine required-owner scopes independently reviewed the
exact unchanged hash. Findings covered projection-domain ambiguity, capability
result closure, missing Binder facts and provenance, undefined context and
transaction codecs, diagnostic emitter and payload closure, synchronization
deletions, manifest-owner conflicts, oversized IR tasks, fixed merge-base
evidence, and missing registered gates. No approval is retained.

The fourth complete review candidate,
`11bc26e10d27407a4f0987a5283953023b6f1e3449923bf1118a2ddecfbdeee6`,
was rejected after all nine required-owner scopes independently reviewed the
exact unchanged hash. Findings covered syntax-root identity, materialized
binding loss, handle-free context authority, transaction dependency order,
cross-owner synchronization, and exact verification ownership. No approval is
retained.

The fifth complete review candidate,
`e6e70449f4029345af520b67620dd3522c1290362d535e7c058bd2e5962dd925`,
was rejected after all nine required-owner scopes independently reviewed the
exact unchanged hash. Findings covered the missing live verified-package
request input to the transaction verifier and contradictory fixed-report
ownership and completion order. No approval is retained.

The sixth complete review candidate,
`e2f4ba5eb777d3d70b8eb3ad75b18f5169afc61a83d989ccc61fc9d5d022f435`,
was approved without findings by all nine required-owner scopes. Acceptance
transaction `rfc0027-accept-20260727-e2f4ba5e` binds that exact proposal hash
to the synchronized RFC, tracker, and routing tree.

### 2026-07-27 Implementation Series Base

`products/zomlang/tests/coverage/implementation-series-base.txt` records clean
committed tree
`109947943519ec2d380a3e8d71813b40bc68bde5` as the immutable implementation
series base. The exact forty-lowercase-hex-plus-newline record names an
ancestor of the base-recording commit. `R27-12B` is complete and source
implementation is authorized in the dependency order below.

### 2026-07-27 RFC 0029 Acceptance Synchronization

Acceptance transaction `rfc0029-accept-20260727-8d393a0c` binds RFC 0029
proposal SHA-256
`8d393a0c6c00a7fad9ef086d3d25f5ed44300041afa9e1e1a4af5d68830fd3e7`.
The synchronized design fixes the complete contextual Binder keys, adds the
identity-syntax-site inventory and stable-identity-admission capabilities,
closes the five provenance capability failure contracts, and orders the
stable Binder foundation before query-runtime work. RFC 0027 remains
`ACCEPTED`; completed tasks `R27-12B` and `R27-17A` remain complete.

### 2026-07-28 RFC 0031 Acceptance Synchronization

Acceptance transaction `rfc0031-accept-20260728-c25fcb18` binds RFC 0031
proposal SHA-256
`c25fcb18e503ac214a8e92c925fa88108a915c2b15c94409dfecb88b3d9a63d5`
and tracker SHA-256
`d64e7791ed2e2a488c5f57bc07ac341ccfc37d37c220c85131e2c9e846fb8d0d`.
The synchronized design makes the stable-binding schema a hand-authored
canonical inventory, fixes descriptor-parameterized capability rows and their
dual owner-task alias checks, records the complete-context ownership matrix, and
assigns the comprehensive canonical-package schema mutation test to RFC 0030
`R30-13`. RFC 0027 remains `ACCEPTED`; completed Q3 task `R27-17A` remains
complete and its production files and existing tests are not reopened.

### 2026-07-29 R27-15 Preflight Scope Correction

Implementation preflight found that the accepted stable-header contract
requires `NamedDefinitionInventoryEntry` to retain a complete typed
`DefinitionIdentityRecord` and `DefinitionBodyDisposition`, while the `S4`
through `S5` exact file set does not include the inventory, its codec, its
query provider and verifier, or its live consumers. The implementation scope
therefore requires the dependency split recorded below.

This correction adds `S3A` as typed definition and implementation inventory
authority plus production-caller cutover and `S3B` as the selected-source
query derivation and architecture proof. It also fixes the body, activation,
visibility, receiver, empty-list, parameter, failure, and scope-role
classification contracts consumed by `S4`, `S4A`, and `S5`. The three header
tasks use dedicated staging-header classes, closed borrowed producer inputs,
and full-context verifier selection while RFC 0018 identity normalization
retains its distinct classes. No source task is marked complete.

The user-designated independent approver accepted exact pre-evidence Git diff
SHA-256
`9af2ae8a4610f578ef14f3975277ba52eda4497cef2843ee7e699fb264d5e756`.
Transaction `rfc0027-header-scope-20260729-9af2ae8a` records that approval and
authorizes only the dependency-ordered `R27-14A` through `R27-15E` source
tasks. It does not claim additional required-owner decisions or complete an
implementation task.

### 2026-07-30 Complete-Context Foundation Dependency Correction

R27-19 preflight found that the accepted compilation-unit membership read set
requires `CompleteCompilationContextAuthorityInput`, while the implementation
plan assigned that input's value, codec, descriptor, and verifier to T1 after
I2 and M1. A contextual roots key cannot prove that it is the session's
complete root set, so omitting the input read or substituting a partial-root
inference is forbidden.

The synchronized correction inserts I1A before I2. I1A owns the canonical
input-entry type, complete-context authority value and codec, registered input
descriptor, independent verifier, schema ownership, native mutation coverage,
and deletion of the test-only shadow authority. T1 continues to own the
transaction producer that installs the verified input and the staging, final,
and sealed session publication sequence.

The user-designated independent approver accepted exact pre-evidence Git diff
SHA-256
`1214413eef714da5727a705d68bb9872d47ea78b28b18600ac158c87db63ac61`.
Transaction `rfc0027-context-foundation-20260730-1214413e` records that
approval, synchronizes the eight-document correction, authorizes `R27-18A`
before `R27-19`, and completes no source implementation task.

### 2026-07-30 Complete-Context Atomic Landing Correction

Independent source review found that the foundation split still allowed the
production descriptor to land before its static final verifier could demand
the graph, SCC, authority, readiness, and transaction-witness inputs required
by RFC 0028. It also left the query-test complete-context shadow outside the
same landing scope.

The synchronized correction preserves the schema ownership tuple
`I1A/T1/I1A/I1A` but makes I1A, I2, and M1 prepare-only partitions. T1 is the
sole atomic landing authority for their union, the complete static verifier,
the three transactions, the query-test migration and both shadow deletions,
and the staging, final, and sealed snapshots. The schema record producer names
the I1A `fromVerified` construction boundary; T1 remains the provider that
installs the verified input. No source task is marked complete.

The user-designated independent approver accepted exact pre-evidence Git diff
SHA-256
`b25aef908d13395fce59151e6e31a9fea2f11f788fdd2806d17fa378b99d8821`.
Transaction `rfc0027-context-atomic-20260730-b25aef90` records that approval
and authorizes only the T1 atomic landing transaction after all three
prepare-only partitions pass review.

## Decision Record

Accepted by `task-router`, `rfc`, `module-system`, `binder-checker`,
`runtime-memory`, `error-system`, `ir-backend`, `spec-audit`, and
`verification` against exact proposal hash
`e2f4ba5eb777d3d70b8eb3ad75b18f5169afc61a83d989ccc61fc9d5d022f435`.
The synchronized acceptance transaction is
`rfc0027-accept-20260727-e2f4ba5e`. Immutable implementation-series base
`109947943519ec2d380a3e8d71813b40bc68bde5` is recorded and source
implementation is authorized.

Transaction `rfc0030-accept-20260728-4ed0e6b8` established the implementation
order. `S1`, `S2`, and `S3` landed through the exact atomic `R29-12AB`
allowlist at commit `8885782747e4c863cefcb0d069bc4569cefce9aa`. RFC 0042
replaces the incomplete six-path diagnostic set and lands the complete current
source-only wire cutover through `R29-12D`. Query-runtime work begins at
`R29-13A` only after that transaction passes. `S6` moves to the live
Source-and-Module producer transaction `R29-13B`, and the corrected runtime
source transaction lands through `R29-14`.

Transaction `rfc0031-accept-20260728-c25fcb18` establishes the accepted
metamodel consumed by RFC 0030 `R30-11`, binds both reviewed hashes, preserves
completed Q3 production authority, and places the additional package-schema
mutation test in `R30-13`.

Transaction `rfc0027-header-scope-20260729-9af2ae8a` binds the corrected typed
inventory, staging-header input, independent-verifier, source partition, and
RFC 0025 dependency contracts to the user-designated independent approval
above. RFC 0027 remains `ACCEPTED`; implementation resumes only at
`R27-14A`.

Transaction `rfc0027-context-foundation-20260730-1214413e` binds the
complete-context foundation correction to the independently approved exact
pre-evidence Git diff above. RFC 0027 remains `ACCEPTED`; implementation may
resume at `R27-18A` before `R27-19`.

Transaction `rfc0027-context-atomic-20260730-b25aef90` binds the
complete-context atomic landing correction to the independently approved
exact pre-evidence Git diff above. I1A, I2, and M1 have preparation authority
only; T1 is their sole source landing authority.

Transaction `rfc0027-materializer-context-20260730-4c760c36` binds the
materializer context, unique revision and fingerprint construction,
production inventory, provenance read, and canonical rejection-order
correction to the independently approved exact two-document pre-evidence Git
diff below. I2 and M1 remain prepare-only; T1 remains their sole atomic landing
authority; T2A retains production resource integration.

Transaction `rfc0027-membership-comparison-20260730-50920a7e` binds
descriptor-owned complete canonical authority comparison to the independently
approved exact two-document pre-evidence Git diff below. I2 owns the eight
comparison implementations; T1 owns runtime use and mutation coverage; both
remain prepare-only before the sole atomic landing.

Transaction `rfc0027-materializer-partitions-20260730-2038e76b` binds the
sequential M1A through M1E preparation split to the independently approved
exact two-document pre-evidence Git diff below. M1 is a review-only join,
every partition remains prepare-only, and T1 remains the sole atomic landing
authority.

Transaction `rfc0027-materializer-pimpl-20260730-ef91a1a3` binds the M1A1
through M1A3 Pimpl preparation split to the independently approved exact
two-document pre-evidence Git diff below. M1A remains a review-only join,
every partition remains prepare-only, and T1 remains the sole registration,
build, test, final-publication, and landing authority.

Transaction `rfc0027-materializer-provider-20260730-233922df` binds the M1C1
through M1C3 provider preparation split to the independently approved exact
two-document pre-evidence Git diff below. M1C remains a review-only join,
every partition remains prepare-only, and T1 remains the sole registration,
build, test, final-publication, and landing authority.

Transaction `rfc0027-materializer-acquisition-20260730-b29b5965` binds the
M1C1A and M1C1B acquisition preparation split to the independently approved
exact two-document pre-evidence Git diff below. M1C1 remains a review-only
join, every partition remains prepare-only, and T1 remains the sole
registration, build, test, final-publication, and landing authority.

Transaction `rfc0027-materializer-verifier-20260730-d44bcd5a` binds the M1D1
through M1D3 verifier preparation split to the independently approved exact
two-document pre-evidence Git diff below. M1D remains a review-only join,
every partition remains prepare-only and independent from provider helpers,
and T1 remains the sole registration, build, test, final-publication, and
landing authority.

### 2026-07-30 Atomic Build-Wiring Correction

I2 and M1 each create one production translation unit, but the accepted T1
exact file set omitted `products/zomlang/compiler/driver/CMakeLists.txt`.
Following the plan literally would leave both reviewed sources outside the
driver library while still allowing T1 to claim a buildable atomic closure.

The correction assigns T1 the exact additive source rows for
`active-identity-membership-query.cc` and
`materialized-module-graph-query.cc`. The preparation partitions remain
non-landing work and cannot add those rows independently. W2 retains only its
later post-T2C deletion and final-wiring scope in the same build file; it
cannot defer, replace, or remove the two T1 rows.

The user-designated independent approver accepted exact two-document
pre-evidence Git diff SHA-256
`d36467858da78500b5cc5bfa47dc350d932ac45bd91a0e8fd97aaa0cdeee58a5`.
Transaction `rfc0027-atomic-build-wiring-20260730-d3646785` records that
approval and changes no source-task completion state.

### 2026-07-30 Membership Preparation Partition Correction

The accepted I2 row combined the common result and codec surface, four
topology memberships, complete-root readiness, and four authority-backed
identity memberships into one source task. That scope cannot satisfy the
repository rule that each source task remain approximately 400 changed source
lines or less.

The correction splits preparation into I2A through I2G with explicit
symbol-level outcomes and sequential review. I2 becomes a review-only join
over the same four production files. Every partition remains non-landing work,
and T1 remains the sole source transaction for the approved union.

The user-designated independent approver accepted exact two-document
pre-evidence Git diff SHA-256
`872a079129e4f8b74967169252d394a099f3ee9edb02aa4a08e042a25cc000de`.
Transaction `rfc0027-membership-partitions-20260730-872a0791` records that
approval and changes no source-task completion state.

### 2026-07-30 Parameter Authority Discovery Correction

I2F2 preflight found that contextual generic- and callable-parameter keys
retain only the routed module and stable parameter digest. The accepted narrow
read sets require the exact active owner before demanding owner membership and
headers, but no reverse owner projection or queryable per-parameter authority
input existed. A full module scan would violate the accepted read set. The
same transaction payload also carried implementation authority records
without naming their input descriptor.

The correction makes all four authority sequences in
`ContextualIdentityAuthorityInputPayload` queryable through exact contextual
inputs. I2E1 owns `ActiveImplementationAuthorityInput`; I2F1 owns
`ActiveGenericParameterAuthorityInput`; and I2G1 owns
`ActiveCallableParameterAuthorityInput`. Their structural verifiers run
before commit. Membership providers probe the exact authority input first,
follow only the owner and headers named by that record, and read complete-root
readiness only when authority is absent or contradictory. I2G3 registers the
four authority inputs, readiness input, and eight membership descriptors.
T1 installs their complete union atomically. No source task is completed by
this correction.

The user-designated independent approver accepted exact two-document
pre-evidence Git diff SHA-256
`bf68f2a58bfaf000a11be8e5e06ab12fc44e3e9e45f0baf43a9045bb9e8821e6`.
Transaction `rfc0027-parameter-authority-20260730-bf68f2a5` records that
approval and changes no source-task completion state.

### 2026-07-30 Materializer Context And Inventory Scope Correction

M1 preflight found four accepted-scope omissions. First, the final-sealed
materialized graph must store its exact `DatabaseRevision` and semantic-context
brand, issue four typed global handles, and independently reverse-expand those
handles, while `CapabilityQueryContext` exposes neither snapshot revision nor
a statically typed view of the arena-owned resource. Second, T1 registers
`MaterializeModuleGraph` but its exact file set omitted the production query
descriptor schema. Third, the repository's only `ModuleGraphRevision` and
`SemanticContextFingerprint` values have no public canonical-digest
construction boundary for witness decoding. Fourth, the complete read set did
not lock a unique multi-child rejection order and one summary omitted the
retained per-module provenance capability. Following the accepted plan would
therefore produce an unbuildable M1 source, an unregistered production
descriptor, or demand-order-dependent failure publication.

The correction makes every active-membership descriptor declare its exact
global-key projection and complete-authority validator. M1 owns a narrow
four-domain `ModuleGraphIdentityMaterializationResources` interface and the
matching active-materialization specializations. T1 atomically adds the
read-only `CapabilityQueryContext::snapshotRevision()` and checked
`semanticContextResources<Resource>()` accessors, the production descriptor
row, test-inventory migration, test resource, build rows, unique
graph-revision and fingerprint construction boundaries, canonical total demand
and rejection order, and final session publication. T2A later makes the
arena-owned compiler-session resource implement the same interface before
production demands the materializer. There is no descriptor-name dispatch,
generic service locator, duplicate revision type, second interner, session
lookup, fallback resource, or independently landable query-runtime surface.

The user-designated independent approver accepted exact two-document
pre-evidence Git diff SHA-256
`4c760c36bfab2a8b977b2185efb99a885a0513ffdf6ac73dfc02418897b3348a`.
Transaction `rfc0027-materializer-context-20260730-4c760c36` records that
approval and changes no source-task completion state.

### 2026-07-30 Membership Record Comparison Correction

I2 static-contract preparation found that
`CapabilityQueryContext::materializeActive` compares the descriptor's complete `Record` with
`operator!=`. Complete identity authorities such as
`DefinitionIdentityRecord` intentionally expose canonical encoding rather than
general equality operators. The accepted template therefore cannot
instantiate for all eight membership descriptors.

The correction makes each descriptor own
`sameAuthority(leftRecord, rightRecord)` over complete canonical record bytes.
The generic runtime calls that operation before descriptor-owned authority
validation. No identity record gains a query-runtime-only operator, no record
is truncated to its digest key, and no descriptor-name dispatch or generic
codec registry is introduced. T1 already owns the runtime template and query
mutation tests; I2 owns the eight descriptor implementations. Both remain
prepare-only before the T1 atomic landing.

The user-designated independent approver accepted exact two-document
pre-evidence Git diff SHA-256
`50920a7e5e0aa1cb60b29b96e541c5e48ce9c015a1b7e37b51aaa597ac23b058`.
Transaction `rfc0027-membership-comparison-20260730-50920a7e` records that
approval and changes no source-task completion state.

### 2026-07-30 Materializer Preparation Partition Correction

M1 preflight mapped the accepted materializer to five independent source
concerns: public runtime values and the four-domain resource boundary; stable
witness construction and codecs; canonical-order provider execution;
independent verifier reconstruction; and failure, schema, permission, and
descriptor contracts. Implementing that union as one source task would exceed
the repository's approximately 400 changed-source-line limit and would prevent
symbol-level review of the provider/verifier independence boundary.

The correction splits preparation into M1A through M1E in that dependency
order. Every partition edits only the existing two M1 files, remains
prepare-only, and requires review before the next partition. M1 becomes a
review-only join with no new file ownership. T1 remains the only transaction
that may add query-runtime accessors and factories, register the descriptor,
add the driver source row, install fixtures and mutation tests, publish the
final authority, and land the complete I1A/I2/M1 union. The split creates no
partial registration, compatibility path, alternate provider, shared
provider/verifier builder, or independently landable materializer.

The user-designated independent approver accepted exact two-document
pre-evidence Git diff SHA-256
`2038e76b96a580da827456c466d7c1a0ec9aff67b133a26fe040a3e197f46df8`.
Transaction `rfc0027-materializer-partitions-20260730-2038e76b` records that
approval and changes no source-task completion state.

### 2026-07-30 Materializer Pimpl Partition Correction

M1A source review found that the initial preparation exposed concrete storage
for the two largest materialized graph values. Both contain multiple move-only
authorities and vectors and therefore require Pimpl under the repository C++
contract. Applying Pimpl to the complete public value family, including the
explicitly instantiated identity-entry template, would also make the original
M1A task exceed the approximately 400 changed-source-line limit.

The correction splits M1A into M1A1 public Pimpl declarations, M1A2 private
storage and basic value operations, and M1A3 resource, materialization,
interner-failure, and permission implementation. M1A becomes a review-only
join. Every non-trivial public M1 value has only `struct Impl; zc::Own<Impl>`
storage; template implementation remains in the `.cc` file with only the four
approved explicit M1 instantiations. M1B may add witness construction and
codecs but cannot expose storage. All three partitions remain prepare-only;
T1 remains the sole registration, build, test, and landing authority.

The user-designated independent approver accepted exact two-document
pre-evidence Git diff SHA-256
`ef91a1a3a81ef9d8ed84335bc3f9e0629b8b1442d302a55972bd210fdeca56a3`.
Transaction `rfc0027-materializer-pimpl-20260730-ef91a1a3` records that
approval and changes no source-task completion state.

### 2026-07-30 Materializer Provider Partition Correction

M1C implementation preflight found that canonical ordered acquisition and
typed child-failure forwarding, four-domain membership materialization and
reverse expansion, and final witness and candidate publication cannot fit
within one approximately 400 changed-source-line task without obscuring the
accepted rejection-order boundary.

The correction splits M1C into M1C1 ordered acquisition through provenance,
M1C2 membership admission and reverse expansion, and M1C3 stable witness,
revision, handle-edge, and candidate publication. M1C becomes a review-only
join. M1C2 cannot publish a candidate, M1C3 cannot issue new reads or bypass
M1C2 materialization, and no partition can register, build, test, or land
independently. T1 remains the sole atomic landing authority.

The user-designated independent approver accepted exact two-document
pre-evidence Git diff SHA-256
`233922df720e961d72a3fd2700885896bf6c19d35e41db09f99690c0a4278b47`.
Transaction `rfc0027-materializer-provider-20260730-233922df` records that
approval and changes no source-task completion state.

### 2026-07-30 Materializer Provider Acquisition Partition Correction

Detailed M1C1 call-surface preflight found two independently reviewable
workflows: complete-context, fingerprint-prerequisite, active-domain, graph,
and SCC acquisition; and per-module selected-source, dependency, resolution,
prelude, parse, provenance, and typed capability-failure acquisition.
Together they exceed one approximately 400 changed-source-line task.

The correction splits M1C1 into M1C1A for the first workflow and M1C1B for
the second. M1C1 becomes a review-only join. M1C1A cannot issue per-module
dependency or capability reads. M1C1B cannot repeat, reorder, or replace the
approved M1C1A reads and cannot materialize identities. M1C2 depends on the
complete M1C1 review. All partitions remain prepare-only; T1 remains the sole
registration, build, test, final-publication, and landing authority.

The user-designated independent approver accepted exact two-document
pre-evidence Git diff SHA-256
`b29b596572a00266acd539f7846421ba44136b7b0cf6579f0b2a1a7b493e5cd5`.
Transaction `rfc0027-materializer-acquisition-20260730-b29b5965` records that
approval and changes no source-task completion state.

### 2026-07-30 Materializer Verifier Partition Correction

M1D implementation preflight found that independent canonical acquisition,
per-module resolution and capability reconstruction, four-domain
materialization, reverse expansion, revision reconstruction, and candidate
comparison require more than one approximately 400 changed-source-line task.
Keeping them in one task would obscure the required separation from provider
graph, edge-order, candidate, fingerprint, and revision helpers.

The correction splits M1D into M1D1 for exact context through SCC acquisition,
M1D2 for per-module resolution through provenance and independent fingerprint
reconstruction, and M1D3 for membership materialization, reverse expansion,
independent revision and graph-edge reconstruction, handle-edge validation,
and candidate comparison. M1D becomes a review-only join. M1D2 cannot repeat
or reorder M1D1 reads, M1D3 cannot issue earlier reads or use provider helpers,
and no partition can register, build, test, or land independently. T1 remains
the sole atomic landing authority.

The user-designated independent approver accepted exact two-document
pre-evidence Git diff SHA-256
`d44bcd5ac164f61355507f710886ec645aab6a0c498b6bf43a9a442d35231251`.
Transaction `rfc0027-materializer-verifier-20260730-d44bcd5a` records that
approval and changes no source-task completion state.

### 2026-07-30 Final-Seal Test Responsibility Correction

T1 preflight found that the generic `QueryDatabase` race and phase-precedence
tests require deliberately selectable key, value, witness, and verifier
outcomes. Coupling those runtime state-machine tests to the complete production
driver graph would obscure the precedence contract, while retaining the
same-name test descriptor would leave a second production authority.

The correction deletes both descriptors that shadow the production fully
qualified name. Generic `QueryDatabase` tests use the distinct
`query::test::TestCompleteContextInput` descriptor, unique domain
`test.input.complete-context`, and the first test-only inventory slot
immediately after the complete production prefix. `QueryCapability`
final-sealed integration and
driver session final-seal tests use the real production descriptor and valid
production authority, input set, and witness. The generic fixture enters no
production schema or library and has no alias, verifier injection seam, or
fallback.

The user-designated independent approver accepted exact four-document
pre-evidence Git diff SHA-256
`f6c041551684ac722a7b4e12682d963f65f01cd4557cc06ca0faaa5f07879437`.
Transaction
`rfc0027-final-seal-test-boundary-20260730-f6c04155` records that approval
without completing a source task.

### 2026-07-30 Transaction Ownership Scope Correction

T1 implementation preflight found that its exact file list omitted the owner
of the first required transaction. The live
`VerifiedCoreDistributionInputTransaction` is declared and implemented in
`core-library-query-provider.{h,cc}` and covered by
`core-library-query-provider-test.cc`. Leaving those files outside T1 would
force either a second wrapper transaction or a witness-only follow-up
revision, both of which violate the accepted atomic transaction contract.

The corrected scope adds those three existing files to T1. The transaction is
replaced directly with the caller-supplied expected revision, complete
canonical payload validation, same-revision witness installation, closed
`InputCommitResult`, and native stale-revision, mutation, witness, and
failure-atomicity coverage. No source task is complete.

The user-designated independent approver accepted exact four-document
pre-evidence Git diff SHA-256
`d0979738a664312a018922acc7d13fe8aa3fb5efe705c806cc3cef58a3ef7539`.
Transaction `rfc0027-transaction-ownership-20260730-d0979738` records the
correction without completing a source task.

### 2026-07-30 Transaction Witness Inventory Correction

T1 schema preflight found that the production prefix already ended at ordinal
55 while the final-authority contract required transaction-witness inputs.
Keeping the generic complete-context fixture at ordinal 56 would leave no
production descriptor for those inputs.

The corrected contract adds three static production transaction-witness input
descriptors at ordinals 56 through 58, one for each session transaction. Every
descriptor uses the complete context roots as key and the exact canonical
payload digest as value, and each transaction installs only its own witness.
The generic complete-context fixture moves to ordinal 59, which remains the
first test-only slot; every later test ordinal advances by three. The final
verifier probes the three descriptor types in canonical transaction order.
No tagged key, aggregate witness input, or runtime descriptor dispatch exists.
T1 also owns `scripts/generate-query-descriptor-schema.py`; its check and
self-test reject every individual witness-row mutation and a test tail that
does not begin at ordinal 59.
No source task is complete.

The user-designated independent approver accepted exact four-document
pre-evidence Git diff SHA-256
`ddd640c83235ff8d178b615f8a532f7179588b21d477ae58fe293f0ba5e87b60`.
Transaction `rfc0027-transaction-witness-inventory-20260730-ddd640c8`
records the correction without completing a source task.

## Implementation Tracker

| Task | Owner | Depends On | Deliverable | Verification | Status |
|---|---|---|---|---|---|
| `R27-01` | `rfc` | None | Complete RFC 0027 draft, tracker, and index row. | `python3 scripts/check-rfc.py` | Complete |
| `R27-02` | `task-router` | `R27-01` | Review path ownership, mandatory gates, escalation, and the ownership-analysis assignment. | Exact-hash review | Complete |
| `R27-03` | `rfc` | `R27-01` | Review completeness, prior art, exact-hash governance, synchronization, and implementation authorization. | Exact-hash review | Complete |
| `R27-04` | `module-system` | `R27-01` | Review query keys, exact read sets, session barriers, graph replacement, and production-root cutover. | Exact-hash review | Complete |
| `R27-05` | `binder-checker` | `R27-01` | Review stable Binder facts, body-owner closure, producers, verifiers, materializers, and Checker handoff. | Exact-hash review | Complete |
| `R27-06` | `runtime-memory` | `R27-01` | Review interner ownership, concurrency, handles, arenas, leases, teardown, ownership-overlay lineage, and ownership-analysis routing. | Exact-hash review | Complete |
| `R27-07` | `error-system` | `R27-01` | Review stable failure algebra, diagnostic facts, provenance, and unified materialization handoff. | Exact-hash review | Complete |
| `R27-08` | `ir-backend` | `R27-01` | Review downstream lease ownership through checked module, HIR, Built MIR, IR failure deletion, and build wiring. | Exact-hash review | Complete |
| `R27-09` | `spec-audit` | `R27-01` | Review current-state design claims, synchronized RFC overlays, and removal of obsolete architecture prose. | Exact-hash review | Complete |
| `R27-10` | `verification` | `R27-01` | Review mutation matrix, architecture gates, native tests, coverage, and benchmark protocol. | Exact-hash review | Complete |
| `R27-11` | `rfc` | `R27-02`; `R27-03`; `R27-04`; `R27-05`; `R27-06`; `R27-07`; `R27-08`; `R27-09`; `R27-10` | Record nine exact-hash approvals and prepare synchronized overlays for RFCs 0018, 0017, 0020, 0019, 0026, 0010, and 0025 plus all seven trackers while RFC 0027 remains REVIEW. | `python3 scripts/check-rfc.py` | Complete |
| `R27-12` | `task-router` | `R27-11` | Synchronize manifest and routing documentation, assign ownership analysis to `runtime-memory`, and assign `scripts/check-english-only.py` plus `scripts/check-spec-alignment.py` to `verification`. | RFC, English-only, and routing review | Complete |
| `R27-12A` | `rfc` | `R27-11`; `R27-12` | Read the completed routing tree without editing it, validate one tree state, record one transaction identifier and proposal hash in RFC metadata, then transition acceptance metadata atomically. | `python3 scripts/check-rfc.py` | Complete |
| `R27-12B` | `verification` | `R27-12A` | Record the clean committed accepted synchronization SHA as the immutable implementation-series base. | Frozen-base ancestry check | Complete; base `109947943519ec2d380a3e8d71813b40bc68bde5` is an ancestor of the recording commit |
| `R27-12C` | `rfc` | `R27-12B`; RFC 0029 `R29-11` | Synchronize complete contextual keys, identity-site provenance, stable-identity admission, exact typed provenance failures, and the corrected dependency order through transaction `rfc0029-accept-20260727-8d393a0c`. | `python3 scripts/check-rfc.py` | Complete |
| `R27-12D` | `rfc` | `R27-12C`; RFC 0031 `R31-09` | Synchronize the accepted hand-authored schema metamodel, direct visibility result, descriptor capability and failure aliases, Q3/`R30-13` test split, and complete-context ownership matrix through transaction `rfc0031-accept-20260728-c25fcb18`. | `python3 scripts/check-rfc.py` | Complete |
| `R27-14A` | `binder-checker` with `module-system` integration and `verification` review | RFC 0029 `R29-12AB`; RFC 0029 `R29-14` | Land typed complete definition and implementation inventory entries, exact codecs and mutation coverage, and direct production caller cutover. | Focused inventory and caller tests | Pending |
| `R27-14B` | `module-system` with `binder-checker` and `verification` review | `R27-14A` | Make the named-definition inventory query read selected parse provenance, derive body disposition independently in provider and verifier, and prove exact read ordering and failure behavior. | Focused query tests and `python3 scripts/check-binder-architecture.py --self-test` | Pending |
| `R27-15C` | `binder-checker` with `verification` review | `R27-14B` | Implement `StableDefinitionHeaderProducer` from the closed borrowed input record, retained parse provenance, and complete syntax-role matrix. | Definition-header producer tests and cross-input mismatch mutations | Pending |
| `R27-15D` | `binder-checker` with `verification` review | `R27-14B` | Implement `StableImplementationOccurrenceHeaderProducer` from the closed borrowed input record with complete identity, source-form, and scope-role coverage. | Implementation-header producer tests and cross-input mismatch mutations | Pending |
| `R27-15E` | `binder-checker` with `verification` review | `R27-15C`; `R27-15D` | Implement `StableHeaderVerifier`, independently select entries and sites from complete inventories and projections, synchronize schema provenance names, and cover equal occurrences without producer traversal reuse. | Header verifier, schema, caller-selected-entry, and disagreement mutations | Pending |
| `R27-17A` | `module-system` | `R27-12B` | Implement the handle-free canonical package request records, verified-request projection, exact codecs, and independent projection verifier. | Package request codec and projection tests | Complete; implementation `3039da5259be25b139954834f900a21b7b891fcf`, sanitizer unit tests 135/135, focused package request test 1/1, independent integration review approved |
| `R27-18` | `module-system` with required `runtime-memory` review | RFC 0029 `R29-14` | Replace frozen registries with the arena-owned eight-domain typed interner set. | Identity, concurrency, collision, and teardown tests | Pending |
| `R27-18A` | `module-system` with `verification` review | `R27-17A`; RFC 0029 `R29-14` | Prepare canonical input entries, `CompleteCompilationContextAuthority`, its exact codec, descriptor definition, producer-independent verifier, schema ownership and producer identity, and the full declared mutation matrix; do not register or land independently. | Complete-context codec, verifier, mutation, and schema review | Pending |
| `R27-19A` | `module-system` | `R27-15E`; RFC 0029 `R29-14`; `R27-18`; approved `R27-18A` preparation | Prepare `ActiveMembershipResult`, its closed codec and record-codec contracts, and `ActiveCompilationUnitMembership`; do not land independently. | Result, codec, and compilation-unit record review | Pending |
| `R27-19B` | `module-system` | approved `R27-19A` preparation | Prepare compilation-unit and crate membership descriptors with independent providers and verifiers; do not land independently. | Compilation-unit and crate membership review | Pending |
| `R27-19C` | `module-system` | approved `R27-19B` preparation | Prepare source and module membership descriptors with active-parent validation and independent providers and verifiers; do not land independently. | Source and module membership review | Pending |
| `R27-19D` | `module-system` | approved `R27-19C` preparation | Prepare complete-root readiness, the definition-authority input verifier, and inventory-backed definition membership; do not land independently. | Readiness, definition authority, and conditional absence review | Pending |
| `R27-19E1` | `module-system` | approved `R27-19D` preparation | Prepare the complete implementation membership record, exact codec, `ActiveImplementationAuthorityInput`, and structural input verifier; do not land independently. | Implementation record, codec, and input review | Pending |
| `R27-19E2` | `module-system` | approved `R27-19E1` preparation | Prepare implementation occurrence coverage and independent membership provider and verifier starting from the exact implementation-authority input; do not land independently. | Implementation occurrence and membership review | Pending |
| `R27-19E` | `module-system` | approved `R27-19E1` and `R27-19E2` preparations | Review the complete implementation membership union without source edits or independent landing. | Complete implementation membership review | Pending |
| `R27-19F1` | `module-system` | approved `R27-19E` preparation | Prepare implementation-generic authority, the definition/implementation owner sum, complete generic-parameter membership records, exact codecs, `ActiveGenericParameterAuthorityInput`, and its structural verifier; do not land independently. | Generic records, owner sum, codec, and input review | Pending |
| `R27-19F2` | `module-system` | approved `R27-19F1` preparation | Prepare definition-owned and implementation-owned generic-parameter membership providers and independent verifiers starting from the exact parameter-authority input and preserving equal-occurrence authority; do not land independently. | Generic owner and occurrence review | Pending |
| `R27-19F` | `module-system` | approved `R27-19F1` and `R27-19F2` preparations | Review the complete generic-parameter membership union without source edits or independent landing. | Complete generic membership review | Pending |
| `R27-19G1` | `module-system` | approved `R27-19F` preparation | Prepare the complete callable-parameter membership record, exact codec, `ActiveCallableParameterAuthorityInput`, and its structural verifier; do not land independently. | Callable record, codec, and input review | Pending |
| `R27-19G2` | `module-system` | approved `R27-19G1` preparation | Prepare callable-parameter membership provider and independent verifier starting from the exact parameter-authority input with receiver and position validation; do not land independently. | Callable header, receiver, and position review | Pending |
| `R27-19G3` | `module-system` | approved `R27-19G2` preparation | Prepare registration for all four contextual authority inputs, complete-root readiness input, and eight membership descriptors; do not land independently. | Complete authority and membership registration review | Pending |
| `R27-19G` | `module-system` | approved `R27-19G1` through `R27-19G3` preparations | Review the complete callable-parameter and registration union without source edits or independent landing. | Complete callable and registration review | Pending |
| `R27-19` | `module-system` with `verification` review | approved preparations `R27-19A` through `R27-19G` | Review the complete eight exact complete-record memberships and conditional readiness union without source edits or independent landing. | Complete membership, readiness, and mutation-inventory review | Pending |
| `R27-20` | `binder-checker` | RFC 0029 `R29-12AB`; RFC 0029 `R29-12D`; `R27-15E` | Implement `BindModuleSkeleton`, lookup projections, and independent verifier. | Skeleton and read-set tests | Pending |
| `R27-21` | `binder-checker` | `R27-20` | Implement contextual `BindOwnerBody` and independent traversal/verifier. | Body, capture, control, and source-failure tests | Pending |
| `R27-22` | `binder-checker` | `R27-21` | Implement the deterministic five-domain module allocation plan. | Overflow and reversed-demand tests | Pending |
| `R27-23A1` | `module-system` | RFC 0029 `R29-14`; RFC 0028 `R28-16`; approved `R27-19` preparation | Prepare public Pimpl declarations for every non-trivial materialized graph value, explicit identity-entry aliases, and the descriptor declaration; do not land independently. | Value-shape, opaque-layout, and header-boundary review | Pending |
| `R27-23A2` | `module-system` | approved `R27-23A1` preparation | Prepare private Pimpl storage, explicit identity-entry instantiations, move operations, destructors, clones, and basic accessors; do not land independently. | Pimpl, ownership, move-only, and accessor review | Pending |
| `R27-23A3` | `module-system` | approved `R27-23A2` preparation | Prepare the four-domain identity-resource interface, exact active-materialization specializations, closed interner-failure mapping, and four permissions; do not land independently. | Resource, permission, and interner-failure review | Pending |
| `R27-23A` | `module-system` | approved preparations `R27-23A1`; `R27-23A2`; `R27-23A3` | Review the complete M1A union without adding files or landing it. | Opaque value, resource, permission, and no-fallback review | Pending |
| `R27-23B` | `module-system` | approved `R27-23A` preparation | Prepare stable dependency and graph witness construction, exact codecs, canonical comparison, and candidate contract; do not land independently. | Codec, exact-consumption, closure, and revision review | Pending |
| `R27-23C1A` | `module-system` | approved `R27-23B` preparation | Prepare provider-only complete-context and fingerprint-prerequisite reads, active crate/source/module acquisition, and graph/SCC validation in canonical order; do not issue per-module dependency or capability reads and do not land independently. | Complete-context, fingerprint-input, active-domain, graph, and SCC review | Pending |
| `R27-23C1B` | `module-system` | approved `R27-23C1A` preparation | Prepare provider-only per-module selected-source, dependency-site, request, exact-resolution, configured-prelude, parse, and provenance reads plus exact typed child-failure forwarding in canonical order; do not materialize or land independently. | Module read-set, rejection-order, parse, and provenance review | Pending |
| `R27-23C1` | `module-system` | approved `R27-23C1A` and `R27-23C1B` preparations | Review the complete provider acquisition union without adding files or landing it. | Complete acquisition-order and typed-failure review | Pending |
| `R27-23C2` | `module-system` | approved `R27-23C1` preparation | Prepare provider-only four-domain membership admission, logical-const materialization, and reverse expansion in the accepted domain order; do not publish a candidate or land independently. | Membership, resource, domain-order, and reverse-expansion review | Pending |
| `R27-23C3` | `module-system` | approved `R27-23C2` preparation | Prepare provider-only stable witness, independently recomputed revision, handle-edge, and candidate publication from the approved ordered acquisition and materialization state; do not land independently. | Witness, revision, handle-edge, and candidate-publication review | Pending |
| `R27-23C` | `module-system` | approved `R27-23C1` through `R27-23C3` preparations | Review the complete provider union without adding files or landing it. | Complete read-set, rejection-order, membership, reverse-expansion, and publication review | Pending |
| `R27-23D1` | `module-system` | approved `R27-23C` preparation | Prepare verifier-only exact context, fingerprint-prerequisite, active-domain, graph, and SCC acquisition with independent canonical ordering; do not issue per-module reads, materialize, or land independently. | Independent context, graph, SCC, and order review | Pending |
| `R27-23D2` | `module-system` | approved `R27-23D1` preparation | Prepare verifier-only per-module resolution, reached prelude, parse, provenance, independent stable-edge ordering, and fingerprint reconstruction; do not materialize or land independently. | Independent module read-set, edge order, provenance, and fingerprint review | Pending |
| `R27-23D3` | `module-system` | approved `R27-23D2` preparation | Prepare verifier-only four-domain membership materialization, same-resource reverse expansion, independent revision and graph-edge reconstruction, handle-edge validation, and candidate witness comparison; do not issue earlier reads or land independently. | Independent membership, reverse, revision, edge, and candidate review | Pending |
| `R27-23D` | `module-system` | approved `R27-23D1` through `R27-23D3` preparations | Review the complete verifier union without adding files or landing it. | Complete independent reconstruction, demand-order, and mutation review | Pending |
| `R27-23E` | `module-system` | approved `R27-23D` preparation | Prepare source/key failure contracts plus schema-derived capability, failure, permission, and descriptor assertions; do not land independently. | Failure-envelope, schema, permission, and descriptor review | Pending |
| `R27-23` | `module-system` | approved preparations `R27-23A`; `R27-23B`; `R27-23C`; `R27-23D`; `R27-23E` | Review the complete M1 union without adding files or landing it. | Graph, provenance, membership projection, canonical authority comparison, reverse expansion, resource-interface, total rejection-order, provider/verifier independence, and read-set review | Pending |
| `R27-24` | `binder-checker` | `R27-22`; `R27-28A` | Implement materialized module skeleton and owner-body capabilities after the complete-context atomic landing. | Typed expansion and retained-child tests | Pending |
| `R27-25` | `binder-checker` | `R27-24` | Implement `VerifyBoundModule`, immutable aggregate storage, and failure projection. | Coverage, lineage, and failure tests | Pending |
| `R27-26` | `binder-checker` | `R27-25` | Migrate Checker consumers to `CheckerBoundModuleView`. | Focused Checker tests | Pending |
| `R27-26A` | `module-system` | `R27-26` | Migrate module-interface publication to the lease-owning Checker view. | Focused interface tests | Pending |
| `R27-27A` | `ir-backend` | `R27-26` | Migrate verified checked module to a retained bound-module lease. | Checked-module lineage tests | Pending |
| `R27-27B` | `ir-backend` | `R27-27A` | Migrate verified HIR to a retained bound-module lease. | HIR lineage tests | Pending |
| `R27-27C` | `ir-backend` | `R27-27B` | Migrate Built MIR to a retained bound-module lease. | MIR lineage tests | Pending |
| `R27-27D` | `runtime-memory` | `R27-27C` | Migrate the ownership overlay to a retained bound-module lease and exact destruction order. | Ownership lineage tests | Pending |
| `R27-28A` | `module-system` with `verification` and query-runtime review | RFC 0029 `R29-14`; RFC 0028 `R28-16`; approved preparations `R27-18A`, `R27-19`, and `R27-23` | Atomically land the complete-context value and descriptor, unique graph-revision and fingerprint construction boundaries, narrow capability-context revision and typed-resource accessors, memberships, readiness, graph and SCC witnesses, production and test descriptor inventories, their two exact driver CMake source rows, three static transaction-witness descriptors, complete static final verifier, three caller-revision-bound transactions with same-revision witnesses, query-test migration, both shadow deletions, full mutation matrix, and staging, final, and sealed snapshots; the first transaction is replaced directly in `core-library-query-provider.{h,cc}` with native coverage in `core-library-query-provider-test.cc`, and `scripts/generate-query-descriptor-schema.py` enforces all witness rows plus the ordinal-59 test-tail boundary. | Complete-context, transaction-result, stale-revision, witness, descriptor-generator check and self-test, canonical demand-order and authority-comparison, typed-resource invariant, final-seal, production/test query-inventory, mutation, session, and clean sanitizer gates | Pending |
| `R27-28B` | `module-system` | `R27-26A`; `R27-27D`; `R27-28A` | Make the arena-owned compiler-session semantic resource implement the approved module-graph identity-materialization interface, then implement the dependency-first production capability root over the RFC 0028 sealed snapshot and inherited admission contract. | Resource ownership, no-fallback, session architecture, and end-to-end tests | Pending |
| `R27-28C` | `module-system` | `R27-28B` | Implement surviving-lease and session teardown order. | Teardown tests | Pending |
| `R27-29` | `module-system` | `R27-28C` | Delete identity registry/freeze authority, session ledgers, and the session-owned handleful graph root. | Identity and session zero-reference gates | Pending |
| `R27-30` | `binder-checker` | `R27-28C` | Delete frozen Binder inventory, production batch binding, mirrors, detached clones, and non-owning bound-module input. | Binder and Checker zero-reference gates | Pending |
| `R27-31` | `ir-backend` | `R27-27C` | Delete Binder-to-IR failure conversion and non-owning checked/HIR/MIR bound-module references. | IR zero-reference gates | Pending |
| `R27-31A` | `runtime-memory` | `R27-27D` | Delete non-owning ownership-overlay bound-module references. | Ownership zero-reference gate | Pending |
| `R27-31B` | `binder-checker` | `R27-30` | Synchronize Binder and Checker build source lists. | Focused build | Pending |
| `R27-31C` | `module-system` | `R27-29` | Synchronize identity, query, graph, and session build source lists. | Focused build | Pending |
| `R27-31D` | `ir-backend` | `R27-31` | Synchronize compiler, checked-module, HIR, MIR, and IR build wiring. | Focused build | Pending |
| `R27-31E` | `runtime-memory` | `R27-31A` | Synchronize ownership-overlay build wiring. | Focused build | Pending |
| `R27-32` | `verification` | RFC 0029 `R29-12AB`; RFC 0029 `R29-12D`; `R27-14A` through `R27-31E`; RFC 0029 `R29-15` | Implement the English-only and specification-alignment gates under their synchronized routing ownership; add the exact IR diagnostic regression and unique installed-consumer fixture; then run the complete sanitizer, unit, lit, codegen, architecture, format, versioning, frozen-base coverage, English-only, install-consumer, spec-alignment, and Release compare matrix. | RFC 0027, RFC 0028, and RFC 0029 Test Plans | Pending |
| `R27-33A` | `spec-audit` | `R27-32` | Update current-state compiler design documentation from the landed production path and publish `docs/reports/zom-core-library-spec-alignment.md` with the report-producing core alignment command. | Spec and design audit plus fixed report publication | Pending |
| `R27-32A` | `verification` | `R27-33A` | Recompute and byte-compare the fixed report without writing it, then rerun English-only, RFC, format, versioning, and diff gates after every spec-audit edit. | Final write-free RFC 0027 Test Plan subset | Pending |
| `R27-33` | `rfc` | `R27-32A` | Audit all implementation, report, and final verification evidence, then perform the only synchronized RFC implementation-state transitions. | `python3 scripts/check-rfc.py` | Pending |

`R27-12B`, `R27-12C`, `R27-12D`, `R27-17A`, and all RFC 0029 dependencies
through `R29-17` are complete. Remaining implementation is authorized only in
the dependency order recorded above; RFC 0028 `R28-16` remains a prerequisite
for the graph-materialization path.
`R27-17A` remains the completed Q3 production task; RFC 0030 `R30-13` owns
only the additional comprehensive schema mutation test and exact landing
allowlist entry.

## RFC 0029 Acceptance Transaction Record

| Field | Value |
|---|---|
| Transaction | `rfc0029-accept-20260727-8d393a0c` |
| Proposal SHA-256 | `8d393a0c6c00a7fad9ef086d3d25f5ed44300041afa9e1e1a4af5d68830fd3e7` |
| RFC 0027 status before transaction | `ACCEPTED` |
| RFC 0027 status after transaction | `ACCEPTED` |
| Synchronized authority | Complete contextual keys, identity-site provenance, stable-identity admission, exact typed provenance failures, and schema-before-runtime ordering |
| Implementation authority | RFC 0030 `R30-11` through `R30-15` published at `8885782747e4c863cefcb0d069bc4569cefce9aa`; RFC 0042 replaces `R30-16`; runtime work then begins at `R29-13A` and lands through `R29-14` |

## RFC 0030 Acceptance Transaction Record

| Field | Value |
|---|---|
| Transaction | `rfc0030-accept-20260728-4ed0e6b8` |
| Proposal SHA-256 | `4ed0e6b885abc87a1c4251855780cf115a85b3623b1d46f774a4b664110f7b6b` |
| Tracker SHA-256 | `31eb9abae5aa70465a8408e05130263a75cea4ca91c0ada8ac673d238c2664f9` |
| RFC 0027 status before transaction | `ACCEPTED` |
| RFC 0027 status after transaction | `ACCEPTED` |
| Synchronized authority | Exact build-visible S1-plus-S2-plus-S3 landing set, driver-owned contextual-key cutover, source-only RFC 0042 diagnostic prerequisite, and S6 live-producer ownership in `R29-13B` |
| Implementation authority | RFC 0030 `R30-11` through `R30-15` published at `8885782747e4c863cefcb0d069bc4569cefce9aa`; RFC 0042 is the replacement authority for `R30-16`; runtime work then begins at RFC 0029 `R29-13A` |

## RFC 0031 Acceptance Transaction Record

| Field | Value |
|---|---|
| Transaction | `rfc0031-accept-20260728-c25fcb18` |
| Proposal SHA-256 | `c25fcb18e503ac214a8e92c925fa88108a915c2b15c94409dfecb88b3d9a63d5` |
| Tracker SHA-256 | `d64e7791ed2e2a488c5f57bc07ac341ccfc37d37c220c85131e2c9e846fb8d0d` |
| RFC 0027 status before transaction | `ACCEPTED` |
| RFC 0027 status after transaction | `ACCEPTED` |
| Synchronized authority | Hand-authored RFC 0031 schema metamodel, direct visibility result, descriptor result/payload/failure closure, exact Q3 versus `R30-13` test ownership, and complete-context ownership matrix |
| Implementation authority | RFC 0030 `R30-11` through `R30-15` published under the accepted RFC 0031 metamodel at `8885782747e4c863cefcb0d069bc4569cefce9aa`; RFC 0042 replaces `R30-16`; runtime work then begins at RFC 0029 `R29-13A`; completed Q3 remains closed |
