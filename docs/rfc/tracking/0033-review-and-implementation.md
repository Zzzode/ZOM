# RFC 0033 Review And Implementation Tracker

## Discussion Record

### 2026-07-28 R30-12H Review Rejection

The first stable-header candidate added at least 601 changed source lines
across the facts header, facts source, and native fact test. Independent
Binder and verification reviews rejected it against RFC 0030's 400-line
review-patch limit.

The same reviews found that aggregate tests used empty parameter sequences and
therefore did not exercise owner, site, ordinal, or position invariants. Binder
review also found that callable parameter admission did not require receiver
names to be absent and ordinary parameter names to be present.

No source approval is retained from the rejected candidate.

## Decision Record

Accepted by `rfc`, `binder-checker`, and `verification` against exact proposal
SHA-256 `3fc78517c36a5794e01bcaca2dcca8d2a616a04b8737f2e2225282a47eea0422`
and tracker SHA-256
`62750bddda02554197b623f5e667b5e749a583679dac1b137d1ff76e10d452e2`.
The synchronized acceptance transaction is
`rfc0033-accept-20260728-3fc78517`.

The transaction synchronizes RFC 0030, its tracker, RFC 0033, its tracker, and
the RFC index. It changes no source, schema, CMake file, native test, gate,
landing allowlist, implementation-series base, or pending
`query-types.{h,cc}` file. Source review may resume at RFC 0030
`R30-12H-A`; no implementation task is declared complete by this decision.

## Review Tracker

| Task | Owner | Depends On | Deliverable | Verification | Status |
|---|---|---|---|---|---|
| `R33-01` | `rfc` | None | Complete RFC 0033, tracker, and RFC index row. | `python3 scripts/check-rfc.py` | Complete |
| `R33-02` | `rfc` | `R33-01` | Review governance, prior art, direct replacement, and synchronization scope. | Exact-hash review | Complete |
| `R33-03` | `binder-checker` | `R33-01` | Review entity boundaries, callable invariants, aggregate admission, and dependency order. | Exact-hash review | Complete |
| `R33-04` | `verification` | `R33-01` | Review line accounting, populated-sequence mutations, exact files, and final atomicity. | Exact-hash review | Complete |
| `R33-05` | `rfc` | `R33-02`; `R33-03`; `R33-04` | Record one unchanged proposal hash, tracker hash, and every owner decision; prepare synchronized RFC 0030, tracker, RFC 0033, tracker, and index overlays while RFC 0033 remains REVIEW. | RFC, English-only, internal-versioning, format, and diff gates | Complete; proposal `3fc78517c36a5794e01bcaca2dcca8d2a616a04b8737f2e2225282a47eea0422`, tracker `62750bddda02554197b623f5e667b5e749a583679dac1b137d1ff76e10d452e2` |
| `R33-06` | `rfc` | `R33-05` | Accept and publish one design-only synchronization transaction. | Local, upstream, and remote SHA parity | Complete; transaction `rfc0033-accept-20260728-3fc78517` |
| `R33-07` | `rfc` | `R33-06` | Authorize source review to resume at RFC 0030 `R30-12H-A`. | Acceptance transaction audit | Complete |

## Implementation Tracker

| Task | Owner | Depends On | Deliverable | Verification | Status |
|---|---|---|---|---|---|
| `R33-11` | `binder-checker` with `verification` review | `R33-07`; RFC 0030 `R30-12G` | Implement and approve RFC 0030 `R30-12H-A`, `R30-12H-B`, and `R30-12H-C` in dependency order without an intermediate commit or push. | RFC 0033 Test Plan and per-task 400-line accounting | Pending |
| `R33-12` | `rfc` | RFC 0030 `R30-15` | Synchronize truthful evidence and move RFC 0033 to LANDED only after the atomic `R29-12AB` transaction is published. | RFC and SHA audit | Pending |

The `R33-07` design gate is satisfied. Source review may resume at RFC 0030
`R30-12H-A`; no source implementation is declared complete by this tracker.
