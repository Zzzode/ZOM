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
`R29-12D` to the `BindModuleSkeleton` dependency edge. It passed the
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
| `R29-12A` | `binder-checker` with `verification` review | `R29-11` | Prepare and review RFC 0027 `S1`; do not land independently. | Schema inventory review | Pending |
| `R29-12B` | `binder-checker` with `module-system` review | `R29-12A` | Prepare and review RFC 0027 `S2`; do not land independently. | Stable fact review | Pending |
| `R29-12AB` | `binder-checker` with `module-system` and `verification` review | `R29-12A`; `R29-12B` | Assemble and land one buildable schema-plus-facts transaction. | Focused native and mutation gates | Pending |
| `R29-12C` | `binder-checker` with `verification` review | `R29-12AB` | Execute RFC 0027 `S3` as one bounded codec, wire-oracle, and mutation-test commit. | Codec and mutation tests | Pending |
| `R29-12D` | `error-system` with `binder-checker` and `verification` review | `R29-12AB` | Execute RFC 0027 `S6` as one canonical Binder diagnostic-fact commit. | Diagnostic fact native tests | Pending |
| `R29-13A` | `module-system` with `runtime-memory` review | `R29-12C`; `R29-12D` | Revise and approve the RFC 0028 query-type partition. | Type, lifetime, and format review | Pending |
| `R29-13B` | `module-system` with `binder-checker`, `error-system`, and `verification` review | `R29-13A` | Add identity-site provenance, stable identity admission, and the five descriptor failure contracts. | Owner-focused source review | Pending |
| `R29-13C` | `verification` | `R29-13B` | Add token, result, provenance, mapping, verifier, race, private decoder, and CTest compile-fail coverage. | Native and architecture tests | Pending |
| `R29-14` | `module-system` with all source owners | `R29-13C`; RFC 0028 `R28-13G` | Assemble and land the corrected RFC 0028 atomic runtime source transaction as the sole landing authority. | Focused sanitizer build and tests | Pending |
| `R29-15` | `verification` | `R29-14` | Run the complete RFC 0028 and RFC 0029 verification plans. | Full native, coverage, and Release gates | Pending |
| `R29-16` | `spec-audit` | `R29-15` | Publish only production-backed current design. | Current-state evidence audit | Pending |
| `R29-17` | `rfc` | `R29-16` | Audit evidence and synchronize truthful statuses. | RFC and evidence audit | Pending |
