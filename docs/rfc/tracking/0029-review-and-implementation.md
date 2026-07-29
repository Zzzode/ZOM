# RFC 0029 Review And Implementation Tracker

## Discussion Record

### 2026-07-27 RFC 0028 Implementation Audit

The RFC 0029 review baseline is
`c9f31c1e5c66f930c196cabcd5a526839930a02b`; local, upstream, and
`origin/develop` matched before review work began.

Preparation of RFC 0028 `R28-13A` found a direct conflict between the accepted
process-global generation allocator and the repository ban on mutable global
state. Production capability tracing also found that five live descriptors had
no exact failure-alternative contract, two contextual keys lacked their
module-qualified stable key, and two stable-identity source errors had no
canonical upstream `DiagnosticFact` owner.

The audit rejected a hidden function-local singleton, opaque byte relabelling,
runtime-failure downgrades, permissive key-failure sets, and a compatibility
constructor. RFC 0029 is the complete replacement contract required before the
atomic runtime source transaction can continue.

RFC 0029 implementation is authorized only through the dependency-ordered
tracker after the synchronized acceptance transaction.

### Review Candidates

The first complete review candidate,
`3e4fe3509b5c28649f852f43e410fe1433d034923eeaf9966e396fdd894a1873`,
was withdrawn before approval because the named-item and owner-body read orders
could observe a semantic projection before the typed capability that owns its
source or key failure, and the stable-identity admission candidate witness was
not yet fully specified. No approval is retained.

The second complete review candidate,
`8e6d03bc0cde18b6374e4e4e205f591969a10388778a8c97eabcdec951987ba1`,
was withdrawn before approval because it collapsed RFC 0027 `S1`, `S2`, `S3`,
and `S6` into one ambiguous schema transaction, left two typed-child reads
after opaque semantic projections, and did not close the complete diagnostic
identity and provenance of stable-identity admission failures. No approval is
retained.

The third complete review candidate,
`14ba382df1747a7c9079494793b4bfa18810e0e5b23e25da982d65acad44ae1b`,
was withdrawn before approval because `S1` still had an independent landing
edge that would publish an unconsumed schema. No approval is retained.

The fourth complete review candidate,
`139308472b76487fae45fbc5989428400ccfe95c0812d086badd2e5b2b2d943c`,
was rejected because `QueryRequestResult` lacked a type-erased successful
capability publication alternative and the owner-body key rejection depended
on a `NoBody` value that `OwnerBodySyntaxQuery` does not publish. No approval is
retained.

The fifth complete review candidate,
`34539126b2c01e23a3c9ea5244738a4708c4979922540a1e896bd588e59548c0`,
was rejected because identity-syntax-site provenance depended on an admission
capability that is not published on source rejection, RFC 0026 was absent from
the synchronization set, and request-decoder mismatch branches lacked an exact
private test seam and CTest compile-fail mechanism. No approval is retained.

The sixth complete review candidate,
`17662f09973681450e9ceb4847bf364848d052debc6d8230639214fc55daf016`,
passed the structural, English-only, internal-versioning, format, and diff
gates after adding independent identity-site provenance, RFC 0026
synchronization, real-object decoder mismatch seams, and exact CTest
compile-fail infrastructure. It was rejected because its nonempty site witness
could not represent a legal empty module and its `SourceRange` field had no
stable canonical codec. No approval is retained.

The seventh complete review candidate,
`69418656dae9f716ed305cc6509a22d8b6639287b33ca1594d897f5a6892c450`,
replaces the site witness with a canonical sequence of stable `SourceSpan`
values, freezes zero sites as the legal empty-module representation, adds the
empty-module provider, verifier, witness, and provenance tests, and fixes the
negative-compile fixture's compiler mode and non-linking boundary. It passed
the structural, English-only, internal-versioning, format, and diff gates. It
was rejected because it did not define safe `SourceSpan` reconstruction through
the retained immutable source snapshot and assigned test files in `R29-13B`
without `verification` review ownership. No approval is retained.

The eighth complete review candidate,
`88bedf93676c42a073b51f09c34c9ad42dd8d31328a52e8eaa5023b144f1d276`,
defines descriptor-private span reconstruction through the retained
`ImmutableSourceSnapshot`, adds independent source, digest, bounds, and ordinal
mutation tests, and adds `verification` review ownership to `R29-13B`. It
passed the structural, English-only, internal-versioning, format, and diff
gates. It was rejected because it forwarded a source rejection from semantic
`ModuleBodySyntaxQuery`, required an inventory decoder mismatch branch that
cannot be independently reached under one immutable inventory per database,
and named C++20 instead of the repository's configured C++23 mode. No approval
is retained.

The ninth complete review candidate,
`f7abb18dd8ba86936eb24710377f6aa0a361727635a0871edf675c6d6d55df14`,
removes the semantic-query source-rejection bridge, maps that semantic failure
to `InvariantViolation`, removes the unreachable inventory decoder coordinate
and its fixture, and binds negative compilation to the repository C++23 mode.
It passed the structural, English-only, internal-versioning, format, and diff
gates and received exact-hash approval from every required owner. Acceptance
integration then found duplicate RFC 0027 task authority and a missing complete
RFC 0028 partition-join dependency on the sole runtime landing task. The
candidate was not accepted, and no approval is retained.

The tenth complete review candidate,
`2d4a489de371537b65ef30c664abf571ad65e15cd23e79f237882f78326af3bb`,
removes duplicate RFC 0027 schema task authority and makes RFC 0029 `R29-14`
the sole runtime landing authority after the complete RFC 0028 `R28-13G`
partition join. It passed the structural, English-only,
internal-versioning, format, and diff gates. It was rejected because one
sentence still named `R28-14` as an atomic source transaction and
`BindModuleSkeleton` did not depend on the diagnostic schema transaction. No
approval is retained.

The eleventh complete review candidate,
`8d393a0c6c00a7fad9ef086d3d25f5ed44300041afa9e1e1a4af5d68830fd3e7`,
names `R29-14` consistently as the sole source transaction and adds
`R29-12D` to the current source-diagnostic dependency edge. It passed the
structural, English-only, internal-versioning, format, and diff gates and
received exact-hash approval from every required owner without a P0 or P1
blocker.

## Decision Record

On 2026-07-27, `task-router`, `rfc`, `module-system`, `binder-checker`,
`runtime-memory`, `error-system`, `spec-audit`, and `verification` approved
proposal SHA-256
`8d393a0c6c00a7fad9ef086d3d25f5ed44300041afa9e1e1a4af5d68830fd3e7`.

Transaction `rfc0029-accept-20260727-8d393a0c` synchronized RFCs 0017 through
0020 and 0025 through 0028, their trackers, the RFC index, and affected routing
ownership. The transaction accepts token database identity, the closed request
result and capability failure algebra, independent identity-site provenance,
stable-identity admission diagnostics, complete contextual keys, exact
descriptor failure/read contracts, and one canonical dependency and landing
authority. It does not claim implementation.

Transaction `rfc0030-accept-20260728-4ed0e6b8` establishes the foundation
order. RFC 0027 `S1`, `S2`, and `S3` remain bounded review partitions and land
only through the exact RFC 0030 `R29-12AB` allowlist. Commit
`8885782747e4c863cefcb0d069bc4569cefce9aa` published that foundation.
RFC 0042 replaces the withdrawn diagnostic execution set and assigns
`R29-12D` only the live source cutover. `R29-13A` depends on both published
transactions, and `R29-13B` owns the later live RFC 0027 `S6`
Source-plus-Module expansion. The immutable implementation-series base remains
`109947943519ec2d380a3e8d71813b40bc68bde5`.

### RFC 0031 Synchronized Acceptance

Transaction `rfc0031-accept-20260728-c25fcb18` synchronizes RFC 0029 to the
accepted stable schema metamodel at proposal SHA-256
`c25fcb18e503ac214a8e92c925fa88108a915c2b15c94409dfecb88b3d9a63d5`
and tracker SHA-256
`d64e7791ed2e2a488c5f57bc07ac341ccfc37d37c220c85131e2c9e846fb8d0d`.
The overlay makes the complete RFC 0031 metamodel the `S1` contract, assigns
the generic descriptor-dependent runtime sum to `R29-13A`, assigns its tests
and reusable staged dual-alias mutations to `R29-13C`, and adds only the
comprehensive package-request schema mutation test to the exact `R29-12AB`
set. It changes no source, schema, CMake, test, gate, implementation-series
base, or implementation status.

### 2026-07-29 R29-14 Exact-Scope Correction

Implementation review found that the exact landing union omitted four
production callers required by the accepted direct replacement.
`products/zomlang/compiler/binder/binding-input.cc` must consume the
unversioned query publication result. `module-body-syntax.h`,
`module-body-syntax-producer.cc`, and `module-body-syntax-verifier.cc` must
accept stable identity admission at the Binder-owned producer and independent
verifier boundary.

The correction adds exactly those four files to `R28-13F` and `R29-13B`. It
does not change the semantic contract, create independent landing authority,
add compatibility behavior, or complete an implementation task.

Transaction `rfc0028-r29-14-scope-20260729-521d82c7` binds this correction to
exact four-document candidate manifest SHA-256
`521d82c731dee0a4b262e937d5578651850446eebfe7448a71a39cb63fc8e086`.
`R29-14` remains the sole source landing authority, and every existing
dependency and owner-review gate remains mandatory.

### 2026-07-29 R29-15 Incremental-Query Baseline Preparation

RFC 0027 requires a replacement baseline because the fixed corpus identity
changed. Commit `0c01c39fa5a3e883732e2f040a61fb555b6e54b4` removed the
internal `schema_version` field from `incremental-query-corpus.json`, changing
its manifest SHA-256 from
`d88257b16c8dc6beb8e3eedf404b53aa5795eb91518a724f16161c72bf909969` to
`89a664bec2f5f1020e252edb88cf9e5a19812c67ee4ed53b64db7a3d6bf1f87f`.
The R29-14 implementation series changed the two corpus source files to
SHA-256 `11c75cc94164aaec34a7e8e467d45a825d1673d3cfe3840033fb7387465ba0d7`
and `b325ac2f246c9a22997098d30ea913ece36bc1fe6abf495a06a263512fc11a9d`;
the resulting combined corpus identity is
`6c5b785dd885a67951c88c74063682bb67c441bb49281578d7b8a44fe526ba26`.

Verification reviewed baseline candidate SHA-256
`98527688526d40159d3faa6a0248b214d5a3a1c6f5634bd24c35c35453db2d9d`,
recorded at clean committed revision
`e9ccb17fab8ba8ec471afa9512e8b13e6f30fcc5` with the recorded Release,
compiler, machine, and eight-worker metadata, five warmups, and twenty-one
measured samples. Binder projection median is `180760000 ns`, MAD
`2670000 ns` (`1.477%`), and peak RSS `5931008 bytes`; module projection
median is `26552000 ns`, MAD `268000 ns` (`1.009%`), and peak RSS
`2949120 bytes`. Aggregate elapsed is `207312000 ns` and aggregate peak RSS
is `8880128 bytes`.

The previous aggregate baseline was `361396000 ns` and `7897088 bytes`, so
elapsed changed by `-42.64%` and peak RSS by `+12.45%`. The elapsed change is
plausible because the Binder benchmark executable now contains eighteen tests
rather than twenty-three after obsolete query surfaces were removed; the RSS
change is plausible because the current fixture constructs the semantic-context
arena and production descriptor inventory. This baseline preparation is not
Release comparison evidence and does not complete `R29-15`. After the approved
baseline is committed, `R29-15` must clean-build Release from that exact commit
and run mandatory `--compare` mode against the committed baseline.

### 2026-07-29 Implementation Closure

The implementation series is complete:

- `R29-13A`, `R29-13B`, `R29-13C`, and the atomic `R29-14` source
  transaction landed through
  `d83eed927ad782963dc49a143b4dab48cb857f85`;
- Binder final-seal failure verification and Release validation corrections
  landed through `89d0c5e1ea1087bdf27c4d8745ed86de8363b842` and
  `e9ccb17fab8ba8ec471afa9512e8b13e6f30fcc5`;
- `R29-15` completed sanitizer, full native, architecture, generated inventory,
  determinism, coverage, format, English-only, internal-versioning, and
  Release evidence; the approved baseline and exact-commit comparison landed
  at `cd94cf6bc220158114125d151658aa88c1db335c`;
- the Release comparison passed with aggregate elapsed ratio `0.989103` and
  aggregate peak-RSS ratio `1.000000`;
- changed production compiler files met the recorded per-file coverage
  threshold; and
- `R29-16` published the production-backed query-runtime and compiler-contract
  design at `598fa6d6a7b4d2ea7ed4f1d61e321c07c624e83c`.

`R29-17` closes this RFC in the current synchronized status transaction.
RFC 0028 `R28-16A` is now the next unblocked query-runtime task. The current
CompilerSession does not yet publish a final-sealed production root; that
downstream boundary remains explicit in the current design and is not claimed
by RFC 0029.

### 2026-07-30 Complete-Context Input Ownership Correction

RFC 0027 R27-19 preflight found that the active compilation-unit membership
must read `CompleteCompilationContextAuthorityInput` before T1 is reachable.
The accepted RFC 0029 ownership table instead assigned the descriptor,
verifier, and tests to T1, creating a dependency cycle through I2 and M1.

The correction adds `I1A` to the closed task vocabulary and assigns the
complete-context input descriptor, value, codec, verifier, and tests to I1A.
T1 remains the provider because its session transaction installs that input.
This design-only ownership correction does not reopen any landed RFC 0029
runtime task or change its implementation evidence.

The user-designated independent approver accepted exact pre-evidence Git diff
SHA-256
`1214413eef714da5727a705d68bb9872d47ea78b28b18600ac158c87db63ac61`.
Transaction `rfc0027-context-foundation-20260730-1214413e` synchronizes this
ownership correction with RFCs 0027, 0030, and 0031.

## Implementation Tracker

| Task | Owner | Depends On | Deliverable | Verification | Status |
|---|---|---|---|---|---|
| `R29-01` | `rfc` | None | Complete RFC 0029, tracker, and index row. | `python3 scripts/check-rfc.py` | Complete |
| `R29-02` | `task-router` | `R29-01` | Confirm owners, gates, and corrected dependency boundary. | Exact-hash review | Complete |
| `R29-03` | `rfc` | `R29-01` | Review completeness, prior art, governance, and synchronization. | Exact-hash review | Complete |
| `R29-04` | `module-system` | `R29-01` | Review token identity, request result, descriptor reads, and callers. | Exact-hash review | Complete |
| `R29-05` | `binder-checker` | `R29-01` | Review complete keys, key failures, stable admission, and schema order. | Exact-hash review | Complete |
| `R29-06` | `runtime-memory` | `R29-01` | Review token lifetime, locking, retained ownership, and teardown. | Exact-hash review | Complete |
| `R29-07` | `error-system` | `R29-01` | Review canonical diagnostic ownership and rejection verification. | Exact-hash review | Complete |
| `R29-08` | `spec-audit` | `R29-01` | Review synchronized claims and stale-authority deletion. | Exact-hash review | Complete |
| `R29-09` | `verification` | `R29-01` | Review native tests, negative compile gates, race seams, and architecture checks. | Exact-hash review | Complete |
| `R29-10` | `rfc` | `R29-02`; `R29-03`; `R29-04`; `R29-05`; `R29-06`; `R29-07`; `R29-08`; `R29-09` | Record exact-hash approvals and prepare synchronized acceptance. | `python3 scripts/check-rfc.py` | Complete |
| `R29-11` | `rfc` | `R29-10` | Accept one synchronized documentation transaction. | RFC and synchronization gates | Complete |
| `R29-12A` | `binder-checker` with `verification` review | `R29-11` | Prepare and review RFC 0027 `S1` as the complete RFC 0031 schema metamodel; do not land independently. | Complete schema metamodel and mutation review | Complete through `R29-12AB` |
| `R29-12B` | `binder-checker` with `module-system` review | `R29-12A` | Prepare and review RFC 0027 `S2`; do not land independently. | Stable fact review | Complete through `R29-12AB` |
| `R29-12AB` | `binder-checker` with `module-system` and `verification` review | `R29-12A`; `R29-12B`; RFC 0030 `R30-14` | Land the exact RFC 0030 allowlist as one buildable S1-plus-S2-plus-S3 transaction with contextual caller cutover. | Focused native, mutation, architecture, landing-scope, and SHA parity gates | Complete; commit `8885782747e4c863cefcb0d069bc4569cefce9aa` |
| `R29-12D` | `error-system` with `binder-checker`, `lexer-parser`, `module-system`, and `verification` review | `R29-12AB`; RFC 0036 `R36-16`; RFC 0042 acceptance | Execute RFC 0042 as one canonical diagnostic-fact and current source-wire cutover while retaining the diagnostics-owned explicit limits API. | RFC 0042 focused and complete gates | Complete; commit `58897c116cafe3463ec6a46ac3bbdd530ef991a5` |
| `R29-13A` | `module-system` with `runtime-memory` review | `R29-12AB`; `R29-12D` | Implement the generic descriptor-dependent `CapabilityDemandResult<Descriptor>` runtime sum with no codec and revise the RFC 0028 query-type partition. | Type, conditional alternative, lifetime, and format review | Complete through `d83eed927ad782963dc49a143b4dab48cb857f85` |
| `R29-13B` | `module-system` with `binder-checker`, `error-system`, and `verification` review | `R29-13A` | Add identity-site provenance, stable identity admission, the five descriptor failure contracts, and RFC 0027 `S6` as the atomic Source-plus-Module expansion with live factories, schema rows, mappings, `ZOM3028`, native tests, and static coverage. | Owner-focused source, schema, mapping, and diagnostic evidence review | Complete through `d83eed927ad782963dc49a143b4dab48cb857f85` |
| `R29-13C` | `verification` | `R29-13B` | Add generic runtime-sum coverage, reusable staged capability and failure-alternative alias mutations, token, result, provenance, mapping, verifier, race, private decoder, and CTest compile-fail coverage without referencing future descriptors. | Native, mutation, and architecture tests | Complete through `d83eed927ad782963dc49a143b4dab48cb857f85` |
| `R29-14` | `module-system` with all source owners | `R29-13C`; RFC 0028 `R28-13G` | Assemble and land the corrected RFC 0028 atomic runtime source transaction as the sole landing authority. | Focused sanitizer build and tests | Complete; commit `d83eed927ad782963dc49a143b4dab48cb857f85` |
| `R29-15` | `verification` | `R29-14` | Run the complete RFC 0028 and RFC 0029 verification plans. | Full native, coverage, and Release gates | Complete through `cd94cf6bc220158114125d151658aa88c1db335c` |
| `R29-16` | `spec-audit` | `R29-15` | Publish only production-backed current design. | Current-state evidence audit | Complete; commit `598fa6d6a7b4d2ea7ed4f1d61e321c07c624e83c` |
| `R29-17` | `rfc` | `R29-16` | Audit evidence and synchronize truthful statuses. | RFC and evidence audit | Complete in this synchronized status transaction |

All RFC 0029 implementation tasks are complete. RFC 0028 `R28-16A` is the
next unblocked query-runtime task.

Transaction `rfc0027-context-foundation-20260730-1214413e` records the
synchronized complete-context ownership correction. RFC 0029 remains
`LANDED`.
