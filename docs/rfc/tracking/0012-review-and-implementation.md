# RFC 0012 Review And Implementation Tracker

## Discussion Record

This local tracker records entry review, formal review, and implementation
evidence for RFC 0012. Entry review validated the manifest, resolver, trust,
materialization, build-script, diagnostic, and conformance contracts against the
live repository before the RFC entered `REVIEW`.

### 2026-07-11 Entry Review Return

The first module-system, error-system, and verification entry reviews returned
the draft. They independently identified the same blocking classes:

- the claimed closed manifest listed tables but not their complete keys, types,
  defaults, source-form exclusions, workspace rules, or build-script inputs;
- resolver records, SemVer constraints, feature requests, target selections,
  canonical output bytes, and permutation generation were not closed;
- registry trust, signing keys, release-record encoding, manifest/archive/tree
  digests, VCS subdirectories, and external local paths had no byte-exact oracle;
- one broad resolver failure sum incorrectly claimed ownership of manifest,
  lock, materialization, and post-resolution build failures, while nested issue
  enums, provenance, note diagnostics, safe display arguments, and pre-freeze
  ordering were undefined;
- archive and sandbox limits lacked exact algorithms and negative matrices;
- the implementation-language statement disagreed with repository build and
  coding-policy evidence, affected path owners were incomplete, and the
  performance threshold had no executable fixture or measurement contract.

### 2026-07-11 Draft Revision Response

The proposal now defines one minimal closed manifest and explicitly rejects
profile, lint, metadata, workspace-inheritance, target-predicate, and extension
surfaces. It adds exact normalized records, dependency source matrices, SemVer
constraint grammar, feature activation domains, target selections, RFC 0011
handoffs, canonical `ResolutionOutput` bytes, deterministic permutation keys,
Ed25519 trust records, source-tree hashing, a lossless lock source key, secure
materialization limits, a complete build-script execution key and capability
contract, component-owned failure sums, exhaustive issue enums, diagnostic
provenance, registered primary and note diagnostics, typed safe rendering,
pre-freeze ordering, a dependency baseline tied to the repository build,
complete path ownership, and an executable algorithmic performance fixture.

These edits answer the written blockers but do not constitute owner approval.
Every required owner must re-review the revised draft before any `REVIEW`
transition.

### 2026-07-11 Cross-Owner Re-Review Return

Module-system re-review found that the final handoff retained generated views
without the build-plan-node to output-record relation required to construct
ordinary crate keys and assign exported semantic environment. Runtime-memory
review found that read-only directory handles did not prove immutable content,
the execution key omitted the compiled source closure and executable identity,
the sandbox adapter and IPC/limit contract were open, nondeterminism detection
did not gate every cache miss, compressed decoder limits were absent, and C
library handles had no `zc` ownership contract. IR-backend review found no exact
package CLI mapping, no semantic-to-backend target equality proof, an incorrect
C++ baseline statement, and an unresolved vendored/system dependency boundary.

### 2026-07-11 Cross-Owner Revision Response

The draft now freezes a total build-plan-node to complete output-record and
generated-view map, derives final crate keys and exported environment from that
map, and moves owning digest-verified snapshots through the session handoffs.
It covers preparatory semantic context and executable image identity in every
build execution key, double-executes every cache miss, specifies the initial
Linux namespace/seccomp/cgroup adapter and canonical IPC, adds compressed,
decoder, header, and metadata limits, and defines RAII wrappers and fault
injection for every C-library boundary. It also replaces direct-source compile
with an exact package CLI, defines the complete RFC 0010 backend target record
and semantic projection invariant, reconciles root C++23 build mode with the
repository C++20-compatible coding policy, and pins all five dependencies as
minimal vendored static inputs with no system fallback.

These revisions require fresh module-system, runtime-memory, ir-backend, and
verification review; they are not self-approval.

### 2026-07-11 Focused Re-Review Response

IR-backend approved the revised entry contract. Module-system requested one
canonical identity clarification; the draft now defines
`PreparatoryBuildScriptKey.buildDependencies` as the sorted unique direct
Build-domain provider package keys, leaving recursive closure in the semantic
context and execution key. Runtime-memory requested three final closures; the
draft removes runtime build-dependency handles in favor of static linking,
defines explicit idempotent `finish()` state machines with `noexcept` destructor
fallbacks, and enforces integral-second CPU limits with `RLIMIT_CPU` while using
cgroup CPU accounting only for post-reap classification.

The module-system and runtime-memory changes require focused re-review before
their owner states can change.

### 2026-07-11 Focused Re-Review Approval

Module-system approved the direct-provider build-dependency identity rule and
confirmed that the final plan-to-result map remained total. Runtime-memory
approved the static-link boundary, handle-free IPC, explicit teardown state
machines, and `RLIMIT_CPU` enforcement without cgroup polling. IR-backend's
approval remains valid. A subsequent diagnostic self-audit added
`InvocationFailure`, `InvocationIssue`, and registered `ZOM7016` so CLI request
normalization cannot escape through raw command text; error-system and
verification must review that addition.

### 2026-07-11 Diagnostic And Verification Return

Error-system returned the first invocation revision because pre-request failures
could not construct the complete-request digest, target capability/invariant IDs
were generic, build-script issue producers were not disjoint, snapshot cleanup
ownership disagreed with the family producer table, and collision variants
lacked exclusive conditions. Verification returned the draft because the test
plan did not enumerate every invocation issue, final handoff mismatch,
pathless materialization failure, vendored-manifest corruption, or exact sandbox
IPC/resource/teardown boundary.

### 2026-07-11 Diagnostic And Verification Response

`PackageInvocationKey.requestDigest` is now optional and absent for every
pre-request failure. Unsupported unwind is exactly `ZOM6006`; compiler-owned
target profile failures use registered fatal `ZOM9904` and a closed invariant
enum. Closed runtime response tags, materialization and build-script
variant-to-producer tables, explicit snapshot-finish forwarding, and mutually
exclusive raw/NFC/case-fold collision rules eliminate producer ambiguity. The
test plan now enumerates all invocation variants, handoff mismatches, path
presence rules and filesystem faults, every vendored-manifest mutation, and the
full IPC, status, CPU, priority, replay, publication, and teardown matrices.

Error-system and verification must re-review the current revision; this record
does not promote their states.

### 2026-07-11 Diagnostic And Verification Approval

Error-system approved the optional invocation digest, exact `ZOM6006` and
`ZOM9904` projections, closed runtime status tags, exhaustive disjoint producer
tables, snapshot forwarding, collision priority, and safe renderer boundary.
Verification requested one final declared-output matrix, then approved after the
test plan distinguished `MissingOutput`, every `InvalidGeneratedSource` input,
and `OutputTreePolicyViolation` with exact `ZOM7011` and no materialization-family
leakage. Spec-audit separately approved the proposal-only boundary and current
normative-spec state.

### 2026-07-11 Formal Acceptance Review Return

Formal semantic and invariant review returned REVIEW proposal hash
`1b86541395a7a612c722034b844ee16023dcb51024560427d45fde64c523aace`.
RFC 0012 had defined a second `TargetSpecId` codec incompatible with RFC 0010,
mapped VCS selector equivocation to a registry fact that required unavailable
package/version data, omitted target capability from its claimed closed failure
sum, and duplicated empty/duplicate target-selection failures across invocation
and resolver families. The Linux sandbox prose prohibited the exit, input,
output, and IPC operations needed to run a script, did not close its syscall and
argument policy, and had no deterministic observation path for
`FileDescriptorLimit`. No prior owner record was bound to the current proposal
hash.

### 2026-07-11 Formal Acceptance Review Response

The package layer now publishes only an immutable
`RegisteredTargetSelection` containing registry revision, profile name,
RFC 0011 semantic projection, and recognized panic strategy. RFC 0010 alone
maps that selection to `CanonicalTargetSpec`, owns the v1 codec and
`TargetSpecId`, and produces `ZOM6009`, `ZOM9947`, or `ZOM9949` without package
wrapping. A normalized invocation request is distinct from the manifest-verified
package request; missing and duplicate selections exist only in
`InvocationFailure`, while unknown workspace targets and features exist only in
`ResolverFailure::TargetSelectionInvalid`.

VCS selector identity now contains a safe selector digest and has a constructible
`VcsSelectorEquivocation` fact plus `ZOM7017`. The sandbox contract now fixes
bootstrap and runtime syscall numbers for x86-64 and AArch64, syscall arguments,
fixed descriptors, `openat2` containment flags, default-deny behavior, and
generated BPF drift checks. Runtime `EMFILE`/`ENFILE` is the closed
`FileDescriptorLimit = 0x06` response; setup exhaustion and generic child exit
remain disjoint.

The repaired formal-review proposal hash is
`2c95e9f747deb9c9d826ed0b3aa83c667dee2f6210e222fa41b3495c3640b35d`.
Coordinated RFC 0010 hash is
`373ca47a7f0d28734435819af0ba84ab948748c332587cb68aa664f1023e1959`.
Every acceptance owner must review these exact bytes; this response is not an
approval.

### 2026-07-11 Exact-Hash Acceptance Review Approval

Formal semantic and invariant re-review approved RFC 0012 proposal hash
`39b7a9edfd5112b9f72fce569ffab1d274c94c957bd6106f6c9158d23b46a982`.
The coordinated target and consumer surfaces were RFC 0004 hash
`26bcc9dd95f5abbf623dd39af0cf6bd3ae2de9ed6be89649465803609c8af5cd`,
RFC 0008 hash
`4a299be3aa1c89d61bfeb679edcf96636e506d0d752997f0853040e4a9a0a67a`,
and RFC 0010 hash
`b5abd8a1f282e787bfbdf258fb0a4e5ff4a9e95607a91603c7db3160190fbe17`.

Semantic review approved the module-system, error-system, and spec-audit
surfaces. Invariant review approved the IR/backend, runtime-memory, and
verification surfaces. The reviews independently recomputed the 43-byte
symbol-name codec oracle and the 46-, 50-, and 49-byte manifest oracles, then
confirmed complete ELF symbol representability, structural relocation targets,
one-symbol/one-operation classification, `Fail` handling, composite/helper
separation, bounded IPC, sandbox precedence, target ownership, and deterministic
failure projection. `scripts/check-rfc.py` and `git diff --check` pass.

The `rfc` governance owner remains pending. This technical approval does not
populate proposal frontmatter, record an acceptance decision, or authorize
implementation until governance approves the tracker and atomic transition.

## Owner Review

| Owner | State | Evidence |
|---|---|---|
| `rfc` | Approved for REVIEW | Governance review approved the template, dependency DAG, prior art, owner coverage, implementability, risk treatment, evidence plan, and atomic status transition. |
| `module-system` | Approved for REVIEW | Focused re-review approved direct Build-domain provider identity and the total immutable plan-to-result handoff. |
| `error-system` | Approved for REVIEW | Focused re-review approved invocation anchors, exact capability/invariant IDs, closed status tags, exhaustive producers, collision priority, and renderer safety. |
| `ir-backend` | Approved for REVIEW | Re-review approved the package CLI, target projection, executable identity, root build mode, pinned static dependencies, and CMake ownership. |
| `runtime-memory` | Approved for REVIEW | Focused re-review approved static linking, handle-free IPC, explicit idempotent teardown, `RLIMIT_CPU`, owning snapshots, bounded decoding, sandboxing, and FFI ownership. |
| `spec-audit` | Approved for REVIEW | Review confirmed package tooling remains proposal-only, deleted package chapters stay out of `order.txt`, Chapter 13 remains source-language-only, and the RFC dependency direction is coherent. |
| `verification` | Approved for REVIEW | Focused re-review approved all invocation, handoff, materialization, vendor, sandbox, declared-output, performance, and deterministic evidence matrices. |

## Acceptance Review

| Owner | Decision | Exact reviewed surface |
|---|---|---|
| `rfc` | Approved at `39b7a9ed...` | Governance, RFC 0011 dependency, owner parity, decision, and atomic transition |
| `module-system` | Approved at `39b7a9ed...` | Normalized/verified request split, package graph, resolver, lock, source records, and session handoffs |
| `error-system` | Approved at `39b7a9ed...` | Constructible failure sums, unique producers, safe rendering, target forwarding, and diagnostic registration |
| `ir-backend` | Approved at `39b7a9ed...` | Registered target selection boundary, RFC 0010 ownership, CLI, build identity, and CMake impact |
| `runtime-memory` | Approved at `39b7a9ed...` | Source admission, ownership, FFI, exact sandbox capabilities, limits, IPC, and teardown |
| `spec-audit` | Approved at `39b7a9ed...` | Proposal-only package boundary and cross-RFC semantic consistency |
| `verification` | Approved at `39b7a9ed...` | Exact hashes, codecs, security matrices, permutation gates, and executable evidence plan |

## Implementation Tracker

The direct package-input implementation series started on 2026-07-11 after the
accepted dependency boundary was populated. All five pinned upstream archives
were independently downloaded from their accepted release URLs and recorded
with tag, peeled commit where applicable, SPDX license, archive SHA-256,
compile-policy inventory, zero local-patch digest, every admitted extracted
path, byte size, per-file SHA-256, and a framed extracted-content SHA-256.

The admitted trees live only under
`products/zomlang/compiler/driver/package/vendor/**`:

- `toml++` v3.4.0 exposes its header-only include tree;
- `Neargye/semver` v0.3.1 exposes its header-only include tree;
- libsodium 1.0.22 exposes the portable `src/libsodium` tree without assembly
  sources outside that tree;
- libarchive 3.8.8 exposes the library source tree without tests or manual
  pages; and
- Zstandard 1.5.7 exposes only public headers plus common and decompression
  sources.

`products/zomlang/tests/tools/check-vendored-dependencies.py` regenerates or
verifies the canonical `vendor-manifest.json`. It rejects a missing license,
missing dependency root, non-regular entry, missing file, added file, changed
bytes, changed metadata, or non-canonical manifest rendering. The checker is a
registered conformance test and a mandatory dependency of the driver target,
so drift fails before driver compilation.

This source-admission slice does not claim that the portable C source lists are
already reduced to their final minimal link inventories or that package parsing
has begun.

The first portable C boundary is now executable. `zom_vendor_zstd` compiles the
exact twelve common and decompression C11 sources as a static library with
assembly, legacy frames, compression, dictionary building, and multithreading
disabled. `ZstdDecoder` is a move-only Pimpl whose `.cc` file immediately owns
the only `ZSTD_DCtx*` through `zc::Own<ZSTD_DCtx, ZstdContextDisposer>`. It sets
the exact maximum window before frame admission, verifies frame and working
memory bounds, counts compressed bytes with overflow checks before decoding,
streams through bounded owned chunks, accepts exactly one ordinary frame,
rejects skippable or trailing frames, and translates every library status at
the wrapper boundary into the closed RFC 0012 `MaterializationIssue`. No raw
zstd text or state escapes. Seven sanitizer tests cover one-byte input
fragmentation, success bytes, trailing frames, truncation, compressed and
working-memory limits, and typed source/sink failure forwarding. The complete
sanitizer build and all 72 unit targets pass.

The next slices must trim and compile the exact libsodium and libarchive static
source lists, implement `SodiumRuntime` and `ArchiveReader`, and then implement
the closed manifest records and parser. Resolver, lock, materialization,
build-script sandboxing, package CLI cutover, and CompilerSession handoff remain
open. RFC 0012 is therefore `IMPLEMENTING`, not `LANDED`.

## Decision Record

Decision: ACCEPTED.

On 2026-07-11, every required owner approved RFC 0012 proposal hash
`39b7a9edfd5112b9f72fce569ffab1d274c94c957bd6106f6c9158d23b46a982`.
The accepted design freezes the manifest, deterministic resolver, lock graph,
source materialization, registered target selection, build-script runtime,
sandbox, diagnostic, and verification contracts. Implementation remains `TBD`;
the next legal transition is `ACCEPTED -> IMPLEMENTING` only when the direct
implementation series is named and starts.
