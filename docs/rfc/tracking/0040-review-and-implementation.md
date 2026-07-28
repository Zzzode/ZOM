# RFC 0040 Review And Implementation Tracker

## Discussion Record

### 2026-07-28 N-E/O-E Dependency Rejection

Approved `R30-12O-D` completed import and alias codecs. The next local-export
fact stores `BindingNameKey`, whose constructor is private and whose public
surface cannot admit decoded namespace and name components.

Friendship expansion, an unchecked constructor, a projection substitute, and
a test-only factory are rejected. Source work pauses after the approved O-D
candidate and resumes through this RFC.

## Decision Record

Accepted by `rfc`, `binder-checker`, and `verification` against exact proposal
SHA-256
`e007151b20d9803a71a8441a7b6c8ca3934c2d976c3a7cfb67ed94d13a51fd9e`
and tracker SHA-256
`71d89625e1f4fc821df55b39b8d95958031480249a8d07c940a2b829f39c15c0`.
The design-only transaction is `rfc0040-accept-20260728-e007151b`, with
repository baseline `985f0def870819b39936bc0dad50abeb9dceec11`.
It changes no source, immutable base, approved cumulative candidate, pending
`query-types.{h,cc}`, or RFC 0038.

## Review Tracker

| Task | Owner | Depends On | Deliverable | Verification | Status |
|---|---|---|---|---|---|
| `R40-01` | `rfc` | None | Complete RFC 0040, tracker, dependency synchronization, and RFC index row. | `python3 scripts/check-rfc.py` | Complete |
| `R40-02` | `rfc` | `R40-01` | Review governance, prior art, status truth, and atomic boundary. | Exact-hash review | Complete |
| `R40-03` | `binder-checker` | `R40-01` | Review closed namespace admission and stable consumer boundary. | Exact-hash review | Complete |
| `R40-04` | `verification` | `R40-01` | Review exact files, line cap, pre-registration evidence, and final gates. | Exact-hash review | Complete |
| `R40-05` | `rfc` | `R40-02`; `R40-03`; `R40-04` | Record one unchanged proposal and tracker hash plus owner decisions. | RFC and repository gates | Complete; proposal `e007151b20d9803a71a8441a7b6c8ca3934c2d976c3a7cfb67ed94d13a51fd9e`, tracker `71d89625e1f4fc821df55b39b8d95958031480249a8d07c940a2b829f39c15c0` |
| `R40-06` | `rfc` | `R40-05` | Accept and publish one design-only transaction. | Local, upstream, and remote SHA parity | Complete; transaction `rfc0040-accept-20260728-e007151b` |
| `R40-07` | `rfc` | `R40-06` | Authorize source review at `R40-11`. | Acceptance transaction audit | Complete; transaction `rfc0040-accept-20260728-e007151b` |

## Implementation Tracker

| Task | Owner | Depends On | Deliverable | Verification | Status |
|---|---|---|---|---|---|
| `R40-11` | `binder-checker` with `verification` review | `R40-07`; RFC 0030 `R30-12O-D` | Add validated `BindingNameKey::from` in `binding-metadata.{h,cc}` plus closed namespace assertions in `stable-binding-facts-test.cc`; at most 400 changed source lines. | C++23 ASan and UBSan `-Werror -fsyntax-only` plus exact-hash review; executable test after `R30-13` | Pending atomic publication; exact-hash candidate approved |
| `R40-12` | `binder-checker` with `verification` review | `R40-11` | Resume RFC 0030 `R30-12N-E` and `R30-12O-E`. | RFC 0030 exact-file reviews | Pending atomic publication; `R30-12N-E` and `R30-12O-E` exact-hash candidates approved |
| `R40-13` | `rfc` | RFC 0030 `R30-15` | Move RFC 0040 to LANDED after atomic publication. | RFC and SHA audit | Pending |

The `R40-07` design gate is satisfied. `R40-11`, `R30-12N-E`, and
`R30-12O-E` completed exact-hash source approval in the cumulative tree.
Their source remains uncommitted and awaits RFC 0030 `R30-15`; no source
implementation is declared landed. RFC 0037 amendment transaction
`rfc0037-amend-20260728-25caf4b9` synchronizes this progress without changing
the RFC 0040 source or publication boundary.
