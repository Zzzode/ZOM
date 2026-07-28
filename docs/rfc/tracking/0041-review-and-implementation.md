# RFC 0041 Review And Implementation Tracker

## Discussion Record

### 2026-07-28 Owner-Body Preflight Return

RFC 0030 `R30-12Q-B` completed exact-hash source approval at:

- `stable-binding-codec.h`
  `c3c6d26bd1f9d469da563c5b01bd6fc71fece7f5d4585a0ab20bc9f68d4126a5`;
- `stable-binding-codec.cc`
  `33adc65a465ee60170090b63e7fd6fd08d2bc18f4f78d972fcb2a77250c2da9f`;
- `stable-binding-facts-test.cc`
  `4c566c513bda9b06a15b6ed70daefba1fe9ee26c4f69ef1de28c61118d53e457`.

The approved fact predecessors are:

- `stable-binding-facts.h`
  `c721f59631a1dddc57275e8834ac100c2dc605d33c0782a206d196ba5572e682`;
- `stable-binding-facts.cc`
  `35b8eebb98b7a179899f5513e0b9161d422e0505078a4b2e1ea1b2f77044a79d`.

Preflight implemented the complete public and private value surface of
`R30-12R` without tests. The fact header and implementation already required
507 additions, beyond the 400-line review limit. The experiment was withdrawn
through exact inverse patches. All three files match the approved predecessor
hashes above.

No owner-body source candidate, approval, commit, or publication exists.

### 2026-07-28 Aggregate Value-And-Smoke Return

The complete `R41-15F1` preflight candidate passed strict C++23 ASan and UBSan
syntax compilation but changed 428 lines:

- `stable-binding-facts.h`: 43 additions, candidate SHA-256
  `3ddfa77cab61767adb24e86f71ca68661f7ab4d963a7d757354376cd5e0323ea`;
- `stable-binding-facts.cc`: 288 additions, candidate SHA-256
  `d8ec89024e508fda29c80368d133ad0814f69d73ee56ea59f3f90cdb6c1be97f`;
- `stable-binding-facts-test.cc`: 97 additions, candidate SHA-256
  `a4baa2641d1cb4b80e0a4f36c45f26faaedb3aa8e66188d4c3031f2c48bd12a1`.

The candidate was not submitted for source approval. The exact files were
restored to the approved `R41-14D` predecessor tuple:

- `stable-binding-facts.h`
  `d885d5e3da4d850419c63975d56ebeda616a42b2feba4fc7f4d7456935b7648d`;
- `stable-binding-facts.cc`
  `c504da86a1c7e65ced8e7367de24fc4016def2ac21265a63a97e9fe3a9b23708`;
- `stable-binding-facts-test.cc`
  `979e059cca39a563310d10d6221e80949f15640bd1bb8abcc7236d118e811893`.

The proposed amendment replaces `R41-15F1` with `R41-15F1A` for the complete
production value plus move-only compile evidence and `R41-15F1B` for populated
production-built smoke evidence. It changes no stable contract, schema,
source publication boundary, or later adversarial evidence requirement.

## Decision Record

Accepted by `rfc`, `binder-checker`, and `verification` against exact
proposal SHA-256
`ad3efd803fa993daa12d427b401eeef224a4f48d9eaf214a99eeeead4d0b3855`,
this tracker SHA-256
`0d2601e633cb32e8942bfc11e5506ff61af487733af62eda41cf2efe24078de9`,
RFC 0030 tracker SHA-256
`4820d5ea1d43a9f404e88db715375a8165936bec015e8061f66fac48c9a938fb`,
RFC 0037 SHA-256
`f32dc9d8de584921f94121bf165e781100e60cf806e5475f2d8213b950058e00`,
and RFC 0037 tracker SHA-256
`2b21628645997bbfb40e8df66a36678dc2b6423a91dfff9207a8894ddefc2b09`.

The design-only transaction is `rfc0041-accept-20260728-ad3efd80`. Its
recorded repository baseline is
`85ffad8bc404ca7793e6d8f16b3ba6204aa752a2`. It changes no source, schema,
CMake, implementation-series base, landing scope, or atomic source
publication boundary. Source review is authorized to resume at `R41-11A`.

The aggregate review amendment was approved by `rfc`, `binder-checker`, and
`verification` against RFC 0041 SHA-256
`a46d2775aa1c7153326eb986229d1dfd995be3e1fa146939ac33620fef29c34a`,
this tracker SHA-256
`50d0a0d1bdfdb6aa4317768c06c1e0f784a910233f9a6f27797f35e5fdc810e9`,
and RFC 0030 tracker SHA-256
`1e4d956eda086df4165681a56cfb7af382a77a804b0296e84a74425c7b9f672f`.
Transaction `rfc0041-amend-aggregate-20260728-a46d2775` replaces
`R41-15F1` with `R41-15F1A` and `R41-15F1B`. The transaction records source
restoration to the exact `R41-14D` predecessor and changes no stable contract,
schema, implementation status, landing scope, or atomic source publication
boundary. Source review resumes at `R41-15F1A`.

## Review Tracker

| Task | Owner | Depends On | Deliverable | Verification | Status |
|---|---|---|---|---|---|
| `R41-01` | `rfc` | None | Complete RFC 0041, tracker, RFC 0030 and RFC 0037 synchronization, and RFC index row. | `python3 scripts/check-rfc.py` | Complete |
| `R41-02` | `rfc` | `R41-01` | Review partition boundaries, prior art, status truth, and atomic publication. | Exact-hash review | Complete; approved tuple recorded above |
| `R41-03` | `binder-checker` | `R41-01` | Review fact families, local invariants, aggregate boundaries, and dependency order. | Exact-hash review | Complete; approved tuple recorded above |
| `R41-04` | `verification` | `R41-01` | Review exact files, line caps, evidence partitions, predecessor accounting, and final gates. | Exact-hash review | Complete; approved tuple recorded above |
| `R41-05` | `rfc` | `R41-02`; `R41-03`; `R41-04` | Record one unchanged reviewed tuple and every owner decision. | RFC, English-only, internal-versioning, format, and diff gates | Complete; transaction `rfc0041-accept-20260728-ad3efd80` |
| `R41-06` | `rfc` | `R41-05` | Accept and publish one design-only synchronization transaction. | Local, upstream, and remote SHA parity | Complete; transaction `rfc0041-accept-20260728-ad3efd80` |
| `R41-07` | `rfc` | `R41-06` | Authorize source review to resume at `R41-11A`. | Acceptance transaction audit | Complete; transaction `rfc0041-accept-20260728-ad3efd80` |

## Implementation Tracker

| Task | Owner | Depends On | Deliverable | Verification | Status |
|---|---|---|---|---|---|
| `R41-11A` | `binder-checker` with `verification` review | `R41-07`; RFC 0030 `R30-12Q-B` | Implement body scope and body node-scope facts. | Fact invariants, exact hashes, and 400-line cap | Pending |
| `R41-12A` | `binder-checker` with `verification` review | `R41-11A` | Implement matching scope codecs and independent wire oracles. | Wire mutations, exact hashes, and 400-line cap | Pending |
| `R41-11B` | `binder-checker` with `verification` review | `R41-12A` | Implement owner-local binding and resolution facts. | Fact invariants, exact hashes, and 400-line cap | Pending |
| `R41-12B` | `binder-checker` with `verification` review | `R41-11B` | Implement matching binding and resolution codecs. | Wire mutations, exact hashes, and 400-line cap | Pending |
| `R41-11C` | `binder-checker` with `verification` review | `R41-12B` | Implement deferred-member facts. | Fact invariants, exact hashes, and 400-line cap | Pending |
| `R41-12C` | `binder-checker` with `verification` review | `R41-11C` | Implement the deferred-member codec. | Populated sequence wire mutations and 400-line cap | Pending |
| `R41-11D` | `binder-checker` with `verification` review | `R41-12C` | Implement stable self-owner, self-type, and receiver facts. | Closed variants, routing invariants, and 400-line cap | Pending |
| `R41-12D` | `binder-checker` with `verification` review | `R41-11D` | Implement matching self and receiver codecs. | Closed-tag wire mutations and 400-line cap | Pending |
| `R41-11E` | `binder-checker` with `verification` review | `R41-12D` | Implement shadow-target facts. | Target and ownership invariants and 400-line cap | Pending |
| `R41-12E` | `binder-checker` with `verification` review | `R41-11E` | Implement the shadow-target codec. | Wire mutations and 400-line cap | Pending |
| `R41-13A` | `binder-checker` with `verification` review | `R41-12E` | Implement label keys, targets, and facts. | Closed variants, relation tests, and 400-line cap | Pending |
| `R41-14A` | `binder-checker` with `verification` review | `R41-13A` | Implement matching label codecs. | Wire mutations and 400-line cap | Pending |
| `R41-13B` | `binder-checker` with `verification` review | `R41-14A` | Implement control targets and transfer facts. | Closed variants, relation tests, and 400-line cap | Pending |
| `R41-14B` | `binder-checker` with `verification` review | `R41-13B` | Implement matching control codecs. | Wire mutations and 400-line cap | Pending |
| `R41-13C` | `binder-checker` with `verification` review | `R41-14B` | Implement closure and free-variable facts. | Populated sequences, ownership tests, and 400-line cap | Pending |
| `R41-14C` | `binder-checker` with `verification` review | `R41-13C` | Implement matching closure codecs. | Wire mutations and 400-line cap | Pending |
| `R41-13D` | `binder-checker` with `verification` review | `R41-14C` | Implement explicit capture modes and facts. | Closed modes, populated captures, and 400-line cap | Pending |
| `R41-14D` | `binder-checker` with `verification` review | `R41-13D` | Implement matching explicit-capture codecs. | Wire mutations and 400-line cap | Pending |
| `R41-15F1A` | `binder-checker` with `verification` review | `R41-14D` | Implement the complete `BoundOwnerBody` value and move-only compile evidence. | Pimpl, factory, indexes, accessors, exact hashes, and 400-line cap | Pending |
| `R41-15F1B` | `binder-checker` with `verification` review | `R41-15F1A` | Add populated production-built aggregate smoke evidence in the native test only. | Every component family, every accessor, exact test predecessor, and 400-line cap | Pending |
| `R41-15F2A` | `binder-checker` with `verification` review | `R41-15F1B` | Add structural and relational aggregate evidence in the native test only. | Exact test predecessor and 400-line cap | Pending |
| `R41-15F2B` | `binder-checker` with `verification` review | `R41-15F2A` | Add ownership, canonical multiplicity, accessor, and scale evidence in the native test only. | Exact test predecessor and 400-line cap | Pending |
| `R41-16` | `binder-checker` with `verification` review | `R41-15F2B` | Implement the complete `BoundOwnerBody` codec and independent aggregate oracle. | Complete wire mutations and 400-line cap | Pending |
| `R41-17` | `rfc` | RFC 0030 `R30-15` | Synchronize truthful evidence and move RFC 0041 to `LANDED`. | RFC and SHA audit | Pending |

Source remains cumulative and uncommitted. RFC 0030 `R30-15` is the only
source commit and push.
