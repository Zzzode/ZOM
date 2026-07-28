# RFC 0031 Review And Implementation Tracker

## Discussion Record

### 2026-07-28 R30-11 Ownership Audit

Independent Binder, module-system, error-system, and verification audits
rejected the prepared schema candidate. The candidate could not express
standalone sum ownership, runtime-only sum ownership, digest ownership, split
input ownership, or the accepted RFC 0028 owner of
`ModuleDependencyProvenance`. It also omitted four canonical package-request
records and referenced the undefined `BindingVisibilityResult` type.

The review determined that filling the gaps by inference would contradict the
accepted owner boundaries. RFC 0031 therefore closes the schema metamodel
before `R30-11` resumes.

### 2026-07-28 First Candidate Returned

The first complete candidate used proposal SHA-256
`a77f99d3d5071d0689bf732f13e442aba63601c34533d6238bb3d9542f239273`
and tracker SHA-256
`5da8b6b967b4f046e9603f07baf4bceb29c2a1cfe997419eded7405085455e12`.
Binder, module-system, and error-system owners rejected it. The candidate
assigned dependency provenance to the wrong runtime transaction, moved
complete-context work into completed Q3, treated descriptor-dependent
capability failures as unconditional, left canonical sum ownership
ambiguous, and omitted complete package-row and diagnostic ownership
contracts. No approval was retained.

### 2026-07-28 Second Candidate Returned

The second complete candidate used proposal SHA-256
`24dd0e7d07794859820b76e2229bf2bd07c97efe64d05505bc881f3ecd948293`
and tracker SHA-256
`fbc6cb7c7443a902cd81c03dd8ed45f033f31ccc3f3932fbc26875b4f3e011de`.
Binder, module-system, and verification owners rejected it. The candidate
mixed descriptor-parameterized results with payload-parameterized capability
rows, omitted failure-set authority, used an oversized Q3 byte bound, relied
on incomplete Q3 mutation tests, left field ordinals implicit, and required
the schema gate to infer tracker completion state. The proposal changed after
the error-system approval, so no approval was retained.

### 2026-07-28 Third Candidate Returned

The third complete candidate used proposal SHA-256
`0636525e25f1c125764d855363918dca716d2371a9101ad34c6e2f049040d2dc`
and tracker SHA-256
`707f885413be27544a113845acb49783ebdef5272231e1a222a1e3a113f72c68`.
Binder and verification owners rejected it. The summary misstated the number
of newly admitted implementation partitions, the schema gate had conflicting
responsibility for premature artifacts, and the generic capability runtime
sum fixed conditional payloads to Binder-owned types instead of binding them
from each descriptor's failure alternatives. No approval was retained.

### 2026-07-28 Fourth Candidate Returned

The fourth complete candidate used proposal SHA-256
`a0747a0189cb458744bee4b5ac6521054953b6849594a3852da4c982a6430c65`
and tracker SHA-256
`d90fdbecfa46ac8da96817b9b90dfb91717a31ff2c32dbbd49c701d7a8715776`.
Binder and verification owners approved the technical contract but rejected
the governance record because the third return and fourth review appeared
twice with conflicting text. The module-system owner also rejected the missing
capability payload column and a task-uniformity statement that contradicted
the dependency-provenance test split. No approval was retained.

### 2026-07-28 Fifth Candidate Returned

The fifth complete candidate used proposal SHA-256
`4b11a58ec21b453d1d72a9fb98b9e4fa640838e3559b01df3456062e49843539`
and tracker SHA-256
`32b596df6361f0676cebebb751057b47795672bcff5b4dfec0da5717b42a9898`.
The verification owner rejected a compile consumer that referenced all five
future capability descriptors during `R30-13`. Contract rows now remain inert,
and each descriptor task compiles the equality check for only its owned rows.
No approval was retained after the proposal changed.

### 2026-07-28 Sixth Candidate Returned

The sixth complete candidate used proposal SHA-256
`e2cc39eeae753686cfd3ee0897cc298c874cad2597b0e242c2f2459ac3585e38`
and tracker SHA-256
`00e2bd8e5be7b712f3c293183ea41f0c2ef93ce94e038851b62f2c82912caf04`.
The RFC owner rejected incomplete best-practice prior art, missing
runtime-memory ownership for the generic capability result and lease
lifetime, and acceptance language that bound only the proposal hash. No
approval was retained after the proposal changed.

### 2026-07-28 Seventh Candidate Returned

The seventh complete candidate used proposal SHA-256
`0bcfe45c51bf9ac8f27cadbb132578087892c7bf189a400f9c8787069ed2cde1`
and tracker SHA-256
`101c376782f4ce67ee11072bbe0d4fccf2ede3b6b7da81cf687dcda4d9f37115`.
The runtime-memory owner rejected descriptor-owner compile checks that bound
the capability alias but not the failure-alternative alias. Each descriptor
task and the final architecture gate now bind both aliases to the owned schema
row. No approval was retained after the proposal changed.

### 2026-07-28 Eighth Candidate Returned After Overlay Audit

The eighth complete candidate used proposal SHA-256
`5f5e43ecbc62278727b710523210058aefa335594c02e12787951aa993b17e1f`
and tracker SHA-256
`9f8a04d51d91de94b96f5dfdd9b7e4e3262fdcf72c4bc9bb8a92bba67cab9802`.
The `rfc`, `module-system`, `binder-checker`, `error-system`,
`runtime-memory`, and `verification` owners approved both unchanged hashes.
The post-overlay RFC audit then rejected publication because current RFC prose
still named a removed internal visibility type. The proposal changed, so no
approval was retained.

### 2026-07-28 Ninth Candidate Accepted

The ninth complete candidate used proposal SHA-256
`c25fcb18e503ac214a8e92c925fa88108a915c2b15c94409dfecb88b3d9a63d5`
and tracker SHA-256
`d64e7791ed2e2a488c5f57bc07ac341ccfc37d37c220c85131e2c9e846fb8d0d`.
The `rfc`, `module-system`, `binder-checker`, `error-system`,
`runtime-memory`, and `verification` owners approved both unchanged hashes.

## Decision Record

Accepted by `rfc`, `module-system`, `binder-checker`, `error-system`,
`runtime-memory`, and `verification` against exact proposal SHA-256
`c25fcb18e503ac214a8e92c925fa88108a915c2b15c94409dfecb88b3d9a63d5`
and tracker SHA-256
`d64e7791ed2e2a488c5f57bc07ac341ccfc37d37c220c85131e2c9e846fb8d0d`.
The synchronized acceptance transaction is
`rfc0031-accept-20260728-c25fcb18`.

The transaction synchronizes RFCs 0019 and 0027 through 0031, trackers
0027 through 0031, and the RFC index. It changes no source, schema,
implementation-series base, CMake, test, or gate file. RFC 0030 `R30-11` may
resume from the accepted schema metamodel; no source implementation is
declared complete by this decision.

## Review Tracker

| Task | Owner | Depends On | Deliverable | Verification | Status |
|---|---|---|---|---|---|
| `R31-01` | `rfc` | None | Complete RFC 0031, tracker, and index row. | `python3 scripts/check-rfc.py` | Complete |
| `R31-02` | `rfc` | `R31-01` | Review governance, prior art, synchronization, and status discipline. | Exact-hash review | Complete |
| `R31-03` | `binder-checker` | `R31-01` | Review record, sum, enum, visibility, and stable schema ownership. | Exact-hash review | Complete |
| `R31-04` | `module-system` | `R31-01` | Review Q3, T1, R30-13, R29-13A, R29-13C, R28-16A, R28-16B, input, and capability ownership. | Exact-hash review | Complete |
| `R31-05` | `error-system` | `R31-01` | Review complete diagnostic sum ownership and fixed tag closure. | Exact-hash review | Complete |
| `R31-06` | `runtime-memory` | `R31-01` | Review generic capability-result shape, conditional payload ownership, lease lifetime, and staged capability and failure-alternative equality checks. | Exact-hash review | Complete |
| `R31-07` | `verification` | `R31-01` | Review gate implementability, mutation coverage, failure-set authority, and exact landing-scope enforcement. | Exact-hash review | Complete |
| `R31-08` | `rfc` | `R31-02`; `R31-03`; `R31-04`; `R31-05`; `R31-06`; `R31-07` | Record one unchanged proposal hash, one unchanged tracker hash, and all owner decisions; prepare synchronized RFC and tracker overlays while RFC 0031 remains REVIEW. | RFC, English-only, versioning, format, and diff gates | Complete; proposal `c25fcb18e503ac214a8e92c925fa88108a915c2b15c94409dfecb88b3d9a63d5`, tracker `d64e7791ed2e2a488c5f57bc07ac341ccfc37d37c220c85131e2c9e846fb8d0d` |
| `R31-09` | `rfc` | `R31-08` | Accept and publish one design-only synchronization transaction. | Local, upstream, and remote SHA parity | Complete; transaction `rfc0031-accept-20260728-c25fcb18` |

## Implementation Tracker

| Task | Owner | Depends On | Deliverable | Verification | Status |
|---|---|---|---|---|---|
| `R31-11` | `binder-checker` with all affected owner review | `R31-09` | Resume RFC 0030 `R30-11` and replace the complete unlanded schema candidate with the accepted metamodel. | RFC 0030 schema check and mutation self-test | Complete; published through `8885782747e4c863cefcb0d069bc4569cefce9aa` |
| `R31-12` | `binder-checker` with `error-system` and `verification` review | RFC 0042 acceptance | Remove the unimplemented diagnostic sums, enums, arguments, mappings, fields, code expectations, and S6 task token from the schema and reusable gate; retain the live Binder result diagnostic payload and bounds. | Stable-binding-schema check and self-test | Pending through RFC 0042 `R42-14A` |

The `R31-09` design gate and `R31-11` source publication are satisfied.
`R31-12` is the direct cleanup required before runtime work resumes; future
diagnostic schema rows land only with their live RFC 0029 or RFC 0025
producers.
