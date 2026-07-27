# RFC 0032 Review And Implementation Tracker

## Discussion Record

### 2026-07-28 R30-12C Authority Cutover Audit

The contextual key implementation review found that the accepted
`ContextualDefinitionKey` payload requires a complete
`StableDefinitionQueryKey`, while
`ActiveDefinitionAuthorityProjectionState` retains only `DefinitionKey`
values.

The retained digest cannot recover the owning module for a definition that is
absent from the next projection. A module scan cannot recover a removed module
and would use the destination module for a moved definition. The review
returned `R30-12C` and blocked `R30-12D` until the session ledger and exact
landing set are corrected by RFC.

### 2026-07-28 First Candidate Withdrawn

The first review candidate used proposal SHA-256
`1a5e341fdf982b3f8724756cbd051862423922cba4dce0144e53093e8e2b094e`
and tracker SHA-256
`24cc359ccaae5d1b7330f30d348e8277e868d959317b5183a54f379a7c2fa7e6`.
Author self-review withdrew it before owner decisions because it described
the total `StableDefinitionQueryKey::from` factory as fallible. No approval
was retained.

### 2026-07-28 Second Candidate Returned

The second review candidate used proposal SHA-256
`7e7cb711a7e944530fb0c4dcbcfcc00c74c5eabff9ea96405102cb6ca2753605`
and tracker SHA-256
`cb158fbcc3f2c497f2db78fd2cd57652e03a84d23025c7e2c8727c4f611c56aa`.
The RFC and Binder owners approved it, but the verification owner returned an
inconsistent mutation contract: the architecture gate listed six forbidden
forms while its self-test and acceptance criteria named smaller subsets. The
proposal changed to require independent mutation coverage for all six forms,
so no approval was retained.

### 2026-07-28 Third Candidate Accepted

The third review candidate used proposal SHA-256
`1d519846566992156b16986fc5c75602af403254fce70f48cfb65af9983a6d72`
and tracker SHA-256
`b685d88db1e5c2eef13e97ede1e5c085959d2446e39fd07fe5baac0bf7b2ecbf`.
The `rfc`, `module-system`, `binder-checker`, and `verification` owners
approved both unchanged hashes.

## Decision Record

Accepted by `rfc`, `module-system`, `binder-checker`, and `verification`
against exact proposal SHA-256
`1d519846566992156b16986fc5c75602af403254fce70f48cfb65af9983a6d72`
and tracker SHA-256
`b685d88db1e5c2eef13e97ede1e5c085959d2446e39fd07fe5baac0bf7b2ecbf`.
The synchronized acceptance transaction is
`rfc0032-accept-20260728-1d519846`.

The transaction synchronizes RFC 0030, its tracker, RFC 0032, its tracker, and
the RFC index. It changes no source, CMake, test, gate, implementation-series
base, or pending `query-types.{h,cc}` file. RFC 0030 `R30-12C` may resume, and
the corrected `R30-12D` is authorized only after `R30-12C` review completes.
No implementation task is declared complete by this decision.

## Review Tracker

| Task | Owner | Depends On | Deliverable | Verification | Status |
|---|---|---|---|---|---|
| `R32-01` | `rfc` | None | Complete RFC 0032, tracker, and RFC index row. | `python3 scripts/check-rfc.py` | Complete |
| `R32-02` | `rfc` | `R32-01` | Review governance, prior art, synchronization, status discipline, and direct replacement. | Exact-hash review | Complete |
| `R32-03` | `module-system` | `R32-01` | Review complete-key retention, refresh erasure, failure atomicity, and authority-session ownership. | Exact-hash review | Complete |
| `R32-04` | `binder-checker` | `R32-01` | Review stable routing-key construction, layering, and contextual-key contract. | Exact-hash review | Complete |
| `R32-05` | `verification` | `R32-01` | Review removal, rename, movement, failure, architecture-mutation, and exact-allowlist evidence. | Exact-hash review | Complete |
| `R32-06` | `rfc` | `R32-02`; `R32-03`; `R32-04`; `R32-05` | Record one unchanged proposal hash, one unchanged tracker hash, and every owner decision; prepare synchronized RFC 0030, tracker, and index overlays while RFC 0032 remains REVIEW. | RFC, English-only, internal-versioning, format, and diff gates | Complete; proposal `1d519846566992156b16986fc5c75602af403254fce70f48cfb65af9983a6d72`, tracker `b685d88db1e5c2eef13e97ede1e5c085959d2446e39fd07fe5baac0bf7b2ecbf` |
| `R32-07` | `rfc` | `R32-06` | Accept and publish one design-only synchronization transaction. | Local, upstream, and remote SHA parity | Complete; transaction `rfc0032-accept-20260728-1d519846` |

## Implementation Tracker

| Task | Owner | Depends On | Deliverable | Verification | Status |
|---|---|---|---|---|---|
| `R32-11` | `module-system` with `binder-checker` and `verification` review | `R32-07`; RFC 0030 `R30-12C` | Execute the corrected RFC 0030 `R30-12D` authority cutover, including the session header, complete-key ledger, caller migration, and native tests; do not land independently. | RFC 0032 Test Plan | Pending |
| `R32-12` | `verification` | `R32-11`; RFC 0030 `R30-13` | Enforce complete-key ledger and corrected allowlist architecture mutations. | Architecture check and self-test | Pending |
| `R32-13` | `rfc` | RFC 0030 `R30-15` | Synchronize truthful implementation evidence and move RFC 0032 to LANDED only after the atomic `R29-12AB` transaction is published. | RFC and SHA audit | Pending |

The `R32-07` design gate is satisfied. RFC 0030 `R30-12C` may resume;
`R30-12D` remains dependency-ordered after it and implements `R32-11`. No
source implementation is declared complete by this tracker.
