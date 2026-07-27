# RFC 0034 Review And Implementation Tracker

## Discussion Record

### 2026-07-28 R30-12H-A Review Rejection

Independent Binder and verification reviews rejected the RFC 0033
`R30-12H-A` candidate. Against the approved 156-line facts header, 91-line
facts source, and 239-line native test, the candidate had a 430-line net lower
bound before adding the missing callable wrong-key mutation. Additions plus
deletions cannot be lower than the net increase.

The same review found that populated aggregate parameter sequences cannot be
constructed in the accepted order. `StableBindingSequenceBuilder<T>` calls
`StableBindingCodec<T>::encode`, but RFC 0033 placed every matching codec after
both aggregate fact reviews.

No source approval is retained from the rejected candidate.

## Decision Record

Accepted by `rfc`, `binder-checker`, and `verification` against exact proposal
SHA-256 `098023480fb5d84ef5c29b8e10151c687b896ac7d586f9217ee8370c6e966210`
and tracker SHA-256
`4db534a78efe41bcca93bc9207f851c57337d38688690feaf03374605d3d69f3`.
The synchronized acceptance transaction is
`rfc0034-accept-20260728-09802348`.

The transaction synchronizes RFCs 0030, 0033, and 0034, their trackers, and
the RFC index. It changes no source, schema, CMake file, native test, gate,
landing allowlist, implementation-series base, or pending
`query-types.{h,cc}` file. RFC 0033 is superseded and source review may resume
at RFC 0030 `R30-12H-A1`; no implementation task is declared complete by this
decision.

## Review Tracker

| Task | Owner | Depends On | Deliverable | Verification | Status |
|---|---|---|---|---|---|
| `R34-01` | `rfc` | None | Complete RFC 0034, tracker, and RFC index row. | `python3 scripts/check-rfc.py` | Complete |
| `R34-02` | `rfc` | `R34-01` | Review governance, direct replacement, prior art, and synchronization scope. | Exact-hash review | Complete |
| `R34-03` | `binder-checker` | `R34-01` | Review fact/codec boundaries, sequence-builder dependency, invariants, and cumulative order. | Exact-hash review | Complete |
| `R34-04` | `verification` | `R34-01` | Review line accounting, exact files, mutation evidence, and atomic landing. | Exact-hash review | Complete |
| `R34-05` | `rfc` | `R34-02`; `R34-03`; `R34-04` | Record one unchanged proposal hash, tracker hash, and every owner decision; prepare the synchronized acceptance overlay. | RFC, English-only, internal-versioning, format, and diff gates | Complete; proposal `098023480fb5d84ef5c29b8e10151c687b896ac7d586f9217ee8370c6e966210`, tracker `4db534a78efe41bcca93bc9207f851c57337d38688690feaf03374605d3d69f3` |
| `R34-06` | `rfc` | `R34-05` | Accept and publish one design-only synchronization transaction. | Local, upstream, and remote SHA parity | Complete; transaction `rfc0034-accept-20260728-09802348` |
| `R34-07` | `rfc` | `R34-06` | Authorize source review to resume at RFC 0030 `R30-12H-A1`. | Acceptance transaction audit | Complete |

## Implementation Tracker

| Task | Owner | Depends On | Deliverable | Verification | Status |
|---|---|---|---|---|---|
| `R34-11` | `binder-checker` with `verification` review | `R34-07`; RFC 0030 `R30-12G` | Implement and approve all RFC 0034 replacement tasks in dependency order without an intermediate commit or push. | RFC 0034 Test Plan and per-task 400-line accounting | Pending |
| `R34-12` | `rfc` | RFC 0030 `R30-15` | Synchronize truthful evidence and move RFC 0034 to LANDED only after the atomic `R29-12AB` transaction is published. | RFC and SHA audit | Pending |

The `R34-07` design gate is satisfied. Source review may resume at RFC 0030
`R30-12H-A1`; no source implementation is declared complete by this tracker.
