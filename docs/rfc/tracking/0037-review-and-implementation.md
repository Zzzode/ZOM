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

All required amendment owners approved RFC 0037 proposal SHA-256
`25caf4b94dd06953c27b1b09d8f07c4ca94f6b3c166618bc803620ebeb9f435a`,
this tracker SHA-256
`21faf1b30428ead842e03023a1714907e355bbf7662eecf1e9257f8823f79aee`,
RFC 0030 tracker SHA-256
`2351e98d1f6d73699487a4f1641f6808dd79bc8aae738f0a5e4fbe3cbec36530`,
and RFC 0040 tracker SHA-256
`e60b7cd2fb1a5c81c58df22845ba90f83ffb77e879b8247c51ccec8027d1da98`.
Amendment transaction `rfc0037-amend-20260728-25caf4b9` changes no source,
schema, CMake, immutable base, landing scope, or atomic publication boundary.
Its recorded repository baseline is
`693d03a23a63295453b0933b57e7193df9d63e5c`.

All required evidence-split amendment owners approved RFC 0037 proposal
SHA-256
`d4e18a120f655e37259684de516b5455cff7ae594e9448f372b8d61ddfc35a76`,
this tracker SHA-256
`a7474f044d81158fa7f5921206954a5118a187aa0211ffb23d0e1f66a238c58d`,
and RFC 0030 tracker SHA-256
`b02684fb967253f90109a4f206c7a4bc32e32209aea56f9e205ee663ccc09fba`.
Amendment transaction `rfc0037-amend-evidence-20260728-d4e18a12` changes no
source, schema, CMake, immutable base, landing scope, or atomic publication
boundary. Its recorded repository baseline is
`584528bf9b6534974b1c21fed9ccdca3cef11bba`.

### 2026-07-28 R30-12N-F Verification Return

The first complete `BoundModuleSkeleton` candidate used the entire 400-line
review allowance across the three exact fact files. Binder approved the value,
factory, local relation, indexing, and iterative graph contracts. Verification
confirmed compilation, formatting, schema, naming, and diff gates, then
returned the candidate because the remaining native test surface did not cover
every aggregate factory branch.

Its approved cumulative predecessor chain ended at:

- `R30-12N-E`: facts header
  `751a92e9c5e2f0654fa4114399981388324822c4024df289ead5c53aa66cd175`,
  facts implementation
  `1298ef333a558996ac13ae5c61e8573ab63fa46951c3738f9fa2d605be3b5722`,
  and test
  `5afc71ca26fb2aec511f73cab96ef2a31d045c8c9cbd28687c110ac0b8bb275f`;
- `R30-12O-E`: codec header
  `26a18856c90c79b80e19f445e17d283ea6d89dfc581405ced569146dbf95a2be`,
  codec implementation
  `a745234ea1eb23ec534a9eac5a1b4265132b9b35ac043a84083026c388c0dc3a`,
  and test
  `1415d5b51c90049295c28f82c42df6b8e2c095b9734c080c9b0371babe0b4dc5`;
- `R30-12P-A`: facts header
  `74de4b4a7d56775baf638fc2d0f07f719db560dd78cafa9b15d18ac445568c89`,
  facts implementation
  `23723fa5d15546630eedc71cc97f30916db40ace220da1d6ba9cb92b68c8755c`,
  and test
  `b966b0312bead09b02fcfd2eb1d70cda457d31fe7932d6a94a48d0ce04091060`;
- `R30-12Q-A`: codec header
  `b09a39832ee4bc0a013633199fa2fcfb255dab654e01c32b9770e70ade96d7bf`,
  codec implementation
  `260b40cb85230dcb4402db1293ca8aa01fec006bd6bc6099e9fd79d8a6f7d73c`,
  and test
  `178d9221a367630af579ee2aa4a5d1517fae6716330f3c51645e413c4e9cb83e`.

The returned candidate hashes are:

- `stable-binding-facts.h`
  `fe8f6e145a4500901ff3544df2ba2d06854a223de31f252883ef01611782d123`;
- `stable-binding-facts.cc`
  `6ac77794861e793dfccf0d605fb489519c8baf9cfeb4afe39e62e94e8e2ed1b3`;
- `stable-binding-facts-test.cc`
  `d4b564fd4a7e51a6186647c099f04c0963374ffdaea86e2239af920bac5bffec`.

No source approval, commit, or publication resulted from the returned review.
The source remains cumulative and uncommitted.

### 2026-07-28 R30-12N-F1 Correction And F2 Return

Binder and Verification approved the corrected `R30-12N-F1` candidate:

- facts header
  `fe8f6e145a4500901ff3544df2ba2d06854a223de31f252883ef01611782d123`;
- facts implementation
  `4f959f7a85ceec182c7b59579052fc26e9df3d449d6565e502cace69e6918220`;
- test
  `d4b564fd4a7e51a6186647c099f04c0963374ffdaea86e2239af920bac5bffec`.

The candidate contains 392 monotonic additions from the approved `R30-12Q-A`
predecessor. It removes aggregate checks made unreachable by public fact
admission plus canonical uniqueness and retains the reachable module-scope and
module-body-owner presence requirements.

The first `R30-12N-F2` candidate used its full 400-line allowance at test hash
`9dd868623cdc30e103b33d09256faf721a5d72e7f41464f37632d204f3e7b790`.
Binder and Verification returned it because one collision fixture was not in
canonical order, several implementation-owned reference paths were absent, and
the deep chain was neither reference-complete nor admitted. The review also
proved that same-module scope and module-body multiplicity are rejected by
canonical sequence admission before the aggregate factory. The returned F2
candidate was withdrawn completely; the live test returned to the approved F1
hash above.

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
| `R37-08` | `rfc` | Returned RFC 0030 `R30-12N-F` verification | Split aggregate value and adversarial evidence reviews and synchronize RFC 0030, RFC 0037, and RFC 0040 trackers without changing source, schema, landing scope, or atomic publication. | RFC, dependency, exact-file, and progress-truth audit | Complete |
| `R37-09A` | `binder-checker` | `R37-08` | Review the aggregate contract, local admission boundary, and strict dependency order. | Exact-hash review | Complete; approved four-file tuple recorded above |
| `R37-09B` | `verification` | `R37-08` | Review the independent 400-line evidence budget and complete aggregate mutation matrix. | Exact-hash review | Complete; approved four-file tuple recorded above |
| `R37-09C` | `rfc` | `R37-08` | Review direct replacement, status truth, and unchanged atomic publication boundary. | Exact-hash review | Complete; approved four-file tuple recorded above |
| `R37-10` | `rfc` | `R37-09A`; `R37-09B`; `R37-09C` | Record the unchanged reviewed hashes, accept the amendment, and publish one design-only transaction. | RFC, English-only, internal-versioning, format, diff, and SHA parity gates | Complete; transaction `rfc0037-amend-20260728-25caf4b9` |
| `R37-10A` | `rfc` | Returned RFC 0030 `R30-12N-F2` verification | Split reachable relation evidence from ownership and accepted-scale evidence; synchronize RFC 0030 and RFC 0037 trackers. | RFC, dependency, exact-file, and progress-truth audit | Complete; reviewed tuple recorded above |
| `R37-10B` | `binder-checker` | `R37-10A` | Review reachable aggregate invariants, canonical implied invariants, and strict evidence dependency order. | Exact-hash review | Complete; approved tuple recorded above |
| `R37-10C` | `verification` | `R37-10A` | Review two independent 400-line test-only budgets and complete evidence coverage. | Exact-hash review | Complete; approved tuple recorded above |
| `R37-10D` | `rfc` | `R37-10B`; `R37-10C` | Record unchanged reviewed hashes, accept the evidence split, and publish one design-only transaction. | RFC, English-only, internal-versioning, format, diff, and SHA parity gates | Complete; transaction `rfc0037-amend-evidence-20260728-d4e18a12` |

## Implementation Tracker

| Task | Owner | Depends On | Deliverable | Verification | Status |
|---|---|---|---|---|---|
| `R37-11` | `binder-checker` with `verification` review | `R37-10D`; RFC 0030 `R30-12M`; RFC 0039 `R39-11` before `R30-12O-D`; RFC 0040 `R40-11` before `R30-12N-E` | Implement and approve all RFC 0037 replacement tasks, including separate `R30-12N-F1`, `R30-12N-F2A`, and `R30-12N-F2B` reviews, in strict dependency order without an intermediate commit or push, then hand off to RFC 0041 `R41-11A`. | Per-task 400-line accounting and RFC 0030 Test Plan | Pending |
| `R37-12` | `rfc` | RFC 0030 `R30-15` | Synchronize truthful evidence and move RFC 0037 to LANDED only after the atomic `R29-12AB` transaction is published. | RFC and SHA audit | Pending |

Source review completed exact-hash approval through RFC 0040 `R40-11`, all RFC
0037 replacement tasks, and RFC 0030 `R30-12Q-B`. The final approved hashes are
facts header
`c721f59631a1dddc57275e8834ac100c2dc605d33c0782a206d196ba5572e682`,
facts implementation
`35b8eebb98b7a179899f5513e0b9161d422e0505078a4b2e1ea1b2f77044a79d`,
codec header
`c3c6d26bd1f9d469da563c5b01bd6fc71fece7f5d4585a0ab20bc9f68d4126a5`,
codec implementation
`33adc65a465ee60170090b63e7fd6fd08d2bc18f4f78d972fcb2a77250c2da9f`,
and test
`4c566c513bda9b06a15b6ed70daefba1fe9ee26c4f69ef1de28c61118d53e457`.
The live source remains cumulative and uncommitted. RFC 0041 partitions the
next owner-body reviews and does not declare this source landed.
