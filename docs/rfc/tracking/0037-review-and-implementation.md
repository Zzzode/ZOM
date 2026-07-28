# RFC 0037 Review And Implementation Tracker

## Discussion Record

### 2026-07-28 R30-12N Preflight Return

RFC 0030 `R30-12M` completed exact-hash source review at:

- `stable-binding-codec.h`
  `969bbfc5b49c80b9a416d261acd87d77e3f25876c30a774f26cb57466a89e82f`;
- `stable-binding-codec.cc`
  `6e0f3ae39fed586a51b49e1374cd6453962fc4005fb4a628f02b140df2e2d165`;
- `stable-binding-facts-test.cc`
  `78fb8a8a28086922a502801c696c84b1d404e51dd9349b147852f11d93ad03ef`.

Binder, error-system, and verification approved the candidate at exactly 400
monotonic added lines from the approved `R30-12L` predecessor.

The next accepted task, `R30-12N`, combines eleven Pimpl records, 64 schema
fields, local invariants, and native evidence under the same 400-line cap.
Preflight returned the task before source changes because its public and
private value surfaces consume the budget before complete tests.

Preflight also found that `BoundModuleSkeleton` stores
`CanonicalSequence<StableFailedLookupFact>`, while accepted task `R30-12P`
does not define `StableFailedLookupFact` until after the aggregate task. The
aggregate cannot be implemented and tested against an incomplete value type.

No `R30-12N` source candidate or approval exists.

## Decision Record

Accepted by `rfc`, `binder-checker`, and `verification` against exact proposal
SHA-256
`ed0b9170c813e42cf02e8a719886ce47aadec5cfbe2ddb788e24572c7243319e`
and tracker SHA-256
`063ea9961caa03d957b976a24d7f4bc9f7489dbdd0442e48b825975d1467470e`.
The synchronized design-only transaction is
`rfc0037-accept-20260728-ed0b9170`. Its recorded repository baseline is
`6b92cfc65bf6c19dfc1591c8abe345d21aa28cda`.

The transaction synchronizes RFCs 0030, 0036, and 0037, their trackers, and
the RFC index without changing source, schema, CMake, native tests, the
landing allowlist, the immutable base, the approved `R30-12M` candidate, or
pending `query-types.{h,cc}`. Publication completes `R37-07` and authorizes
source review to resume at `R30-12N-A`.

RFC 0039 acceptance transaction `rfc0039-accept-20260728-de7ab2aa`
synchronizes the inserted `R39-11` dependency without changing the approved
RFC 0037 fact and codec contracts or their atomic publication boundary.

## Review Tracker

| Task | Owner | Depends On | Deliverable | Verification | Status |
|---|---|---|---|---|---|
| `R37-01` | `rfc` | None | Complete RFC 0037, tracker, RFC 0030 synchronization, and RFC index row. | `python3 scripts/check-rfc.py` | Complete |
| `R37-02` | `rfc` | `R37-01` | Review direct replacement, prior art, status truth, and atomic boundaries. | Exact-hash review | Complete |
| `R37-03` | `binder-checker` | `R37-01` | Review Pimpl partitions, stable invariants, failed-lookup dependency, and aggregate admission order. | Exact-hash review | Complete |
| `R37-04` | `verification` | `R37-01` | Review line caps, exact files, mutation evidence, predecessor accounting, and final native gates. | Exact-hash review | Complete |
| `R37-05` | `rfc` | `R37-02`; `R37-03`; `R37-04` | Record one unchanged proposal hash, tracker hash, and every owner decision; prepare the synchronized acceptance overlay. | RFC, English-only, internal-versioning, format, and diff gates | Complete; proposal `ed0b9170c813e42cf02e8a719886ce47aadec5cfbe2ddb788e24572c7243319e`, tracker `063ea9961caa03d957b976a24d7f4bc9f7489dbdd0442e48b825975d1467470e` |
| `R37-06` | `rfc` | `R37-05` | Accept and publish one design-only synchronization transaction. | Local, upstream, and remote SHA parity | Complete; transaction `rfc0037-accept-20260728-ed0b9170` |
| `R37-07` | `rfc` | `R37-06` | Authorize source review to resume at RFC 0030 `R30-12N-A`. | Acceptance transaction audit | Complete; transaction `rfc0037-accept-20260728-ed0b9170` |

## Implementation Tracker

| Task | Owner | Depends On | Deliverable | Verification | Status |
|---|---|---|---|---|---|
| `R37-11` | `binder-checker` with `verification` review | `R37-07`; RFC 0030 `R30-12M`; RFC 0039 `R39-11` before `R30-12O-D`; RFC 0040 `R40-11` before `R30-12N-E` | Implement and approve all RFC 0037 replacement tasks in strict dependency order without an intermediate commit or push. | Per-task 400-line accounting and RFC 0030 Test Plan | Pending |
| `R37-12` | `rfc` | RFC 0030 `R30-15` | Synchronize truthful evidence and move RFC 0037 to LANDED only after the atomic `R29-12AB` transaction is published. | RFC and SHA audit | Pending |

Source review reached approved cumulative `R39-11` and `R30-12O-D`
candidates. RFC 0040 `R40-07` is satisfied; `R40-11` must complete before
`R30-12N-E`. The
candidates remain uncommitted, and no source implementation is declared
landed by this tracker.
