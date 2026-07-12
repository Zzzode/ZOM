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

### 2026-07-12 Runtime Descriptor Allocation Correction

Production launcher implementation found that the accepted descriptor policy
was not executable. The proposal closed descriptors 0 through 2, occupied fixed
descriptors 3 through 7, and admitted runtime file operations only on descriptors
8 through 15. Linux `openat2`, like `open`, returns the lowest unused descriptor,
so the first admitted runtime open necessarily returned 0 and was immediately
trapped by the accepted seccomp filter. The stated minimum `RLIMIT_NOFILE` of 8
also could not make descriptor 8 available.

The corrected contract admits exactly one runtime-tracked regular file on
descriptor 0, keeps descriptors 1 and 2 closed, and retains fixed descriptors 3
through 7. The trusted runtime must close descriptor 0 before another open. This
preserves the closed-world capability proof, keeps the accepted descriptor-limit
range, and follows the kernel's deterministic lowest-unused-descriptor rule.
The symbolic filter tests and all four generated BPF hash oracles are regenerated
with this correction. The prior exact-hash acceptance record remains historical
evidence for `39b7a9ed...`; the corrected proposal bytes require fresh exact-hash
review before RFC 0012 can move from `IMPLEMENTING` to `LANDED`.

### 2026-07-12 Build Result Integrity Closure

Cache and session implementation exposed one missing typed producer: stale
execution-key bytes, stale output-record bytes, a plan/result key mismatch, and
a generated inventory or digest mismatch all need one disjoint failure before
the value can enter the final package session. The corrected proposal adds
`BuildResultIntegrityViolation` and defines the complete untrusted cache
candidate and frozen result-set relations. Cache hits now reverify the exact
execution-key bytes, exact output-record bytes, source and environment facts,
generated inventory, generated file bytes, UTF-8 validity, and exported values
before reuse.

The implementation adds canonical acyclic build plans with duplicate,
dangling-predecessor, and cycle rejection; stable predecessor-first execution;
cache-hit revalidation; atomic cache publication after byte-equal double
execution; and `CompilerSession` ownership of the plan-to-result transition.
The session verifies every node belongs to the resolved graph, every build
target matches its preparatory key, and every result key matches its node before
publishing the exact frozen result set. These corrected proposal bytes require
fresh module-system, error-system, runtime-memory, verification, and RFC review.

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

The Zstandard, libsodium, and libarchive portable C source lists are now reduced
to the direct wrapper link closures recorded below.

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

The cryptographic boundary now compiles one exact 32-source portable libsodium
closure as a static library. `SodiumRuntime` has explicit caller-owned
initialization, SHA-256, and Ed25519 verification; it exposes no singleton,
secret-key, signing, key-generation, or generic primitive surface. Five
sanitizer tests cover initialization, the accepted SHA-256 vector, accepted and
rejected Ed25519 signatures, invalid key and signature widths, and repeatable
caller-owned construction.

The archive boundary now compiles one exact 21-source portable libarchive C11
closure as a static library with only the no-filter and tar readers enabled.
`ArchiveReader` is a move-only Pimpl whose `.cc` file immediately owns the only
`archive*` through `zc::Own<archive, ArchiveDisposer>`. It admits exactly one
POSIX ustar stream, requires regular files, rejects links and every special
entry, validates canonical relative UTF-8 paths, counts headers, path bytes,
and per-file padding against the metadata limit, enforces file and aggregate
limits with overflow checks, rejects trailing tar bytes, and forwards typed
source and destination failures without exposing libarchive state or text.
Five sanitizer tests cover fragmented input, successful extraction, link and
special-entry rejection, header/file/metadata limits, trailing data, and typed
failure forwarding. The vendored dependency checker now freezes both exact C
source closures in `vendor-manifest.json`. The complete sanitizer build, all
115 unit tests, and the exact-current default matrix pass 1,209/1,209 with zero
failures in 572.24 seconds; the complete grammar oracle passes in 572.10
seconds.

The manifest admission slice now exposes a move-only Pimpl parser and result,
the complete closed `ManifestIssue` discriminants, exact original-byte failure
spans, and a header-only toml++ target compiled without exceptions. The toml++
standard-library and borrowed-node surface is confined to one `.cc` adapter and
recorded as the sole repository `std::` exception. Admission rejects a BOM,
invalid UTF-8, TOML syntax failures, unknown tables and keys, wrong types,
missing package/workspace roots, invalid edition and identity scalars,
non-canonical paths, dependency source conflicts, invalid SemVer comparator
syntax, invalid VCS selectors, forbidden optional dependency domains, invalid
feature edges, missing or non-optional dependency activation, duplicate
canonical features and edges, and local feature cycles. Seven sanitizer test
groups cover the successful minimal package and those rejection families with
one exact key-span oracle. The complete sanitizer build and all 75 compiler
unit tests pass. The exact-current default matrix passes 1,210/1,210 with zero
failures in 630.16 seconds; the complete grammar oracle passes in 630.05
seconds.

This is an admission checkpoint, not the completed normalized-manifest slice.
The first normalized-model layer now implements host-path-free
`DiagnosticDocumentPath`, `InputDocumentKey`, bounded `ManifestSpan`, manifest
diagnostic anchors, `PackageManifest`, canonically sorted unique
`WorkspaceManifest`, provenance-bearing `TargetManifest`, and
`CanonicalTargetManifest`. Package, workspace, and canonical target codecs have
fixed byte and SHA-256 vectors; closed enum and span bounds have negative tests.
The parser now retains the exact input-document digest, package record, and
sorted workspace members instead of temporary strings and counts. The feature
model implements all three closed edge variants, provenance-bearing edge
records, canonical edge sorting, duplicate rejection, fixed SHA-256 vectors,
and sorted `FeatureManifest` records. The parser now retains those records
instead of only their count.

The normalized dependency layer now implements the closed registry, VCS, and
local source constraints; revision, tag, and branch selectors; all three
dependency domains; origin-bearing and origin-free dependency requirements;
and canonical requirement sorting. The parser retains target, development, and
build dependency records with aliases, required package names, source
constraints, requested feature sets, default-feature and optional flags, and
exact manifest provenance. `SemVerConstraint` parses the accepted comparator
grammar into an intersection of arbitrary-width SemVer bounds, retains sorted
unique prerelease cores, represents empty intersections without rejecting the
manifest, and encodes only normalized intervals rather than source text. Fixed
byte and SHA-256 vectors cover both a normalized constraint and an origin-free
dependency requirement. Focused sanitizer tests cover caret and tilde bounds,
arbitrary-width increments, comparator intersection, empty intersections,
SemVer prerelease ordering, grammar rejection, all dependency domains, all
source kinds, and parser retention.

The target layer now retains explicit library, binary, test, benchmark,
example, and build-script records with default names and paths, canonical byte
ordering, and manifest provenance. Build-script inputs, outputs, environment,
and exported environment names are validated, sorted, and deduplicated; the
build-script source is automatically included in the normalized input set.
`CanonicalBuildScriptManifest`, `CanonicalFeatureManifest`, and the complete
`CanonicalManifestRecord` remove every document and diagnostic anchor while
preserving the accepted declaration order. Fixed codec vectors and permutation
tests prove that TOML key order, target order, dependency order, feature order,
whitespace, and document digest do not affect canonical manifest bytes.

The structural P0 manifest model is complete. The next normalization boundary
must expand workspace members and prove the complete workspace collision and
permutation matrix. Resolver, lock, complete source materialization and snapshot
ownership, build-script sandboxing, package CLI cutover, and CompilerSession
handoff remain open. RFC 0012 is therefore `IMPLEMENTING`, not `LANDED`.

P1 now injects an immutable, canonically sorted
regular-file inventory into manifest normalization. Explicit and defaulted
targets must name inventoried regular files; build-script sources and declared
inputs must also be present. Exact `src/lib.zom` and `src/main.zom` inventory
entries create implicit package targets only when the package name admits a
target name. Target kind/name collisions and cross-kind path collisions reject
the normalized manifest. Inventory ordering, duplicate rejection, implicit
target derivation, missing paths, target collisions, path collisions, and
build-input admission have sanitizer coverage. Workspace normalization expands
the exact declared member set, rejects missing members and nested workspaces,
and detects duplicate package names with the first canonical member retained as
related provenance. Member-order permutations produce identical normalized
workspace bytes. The P1 normalization boundary is complete.

P2 now registers the complete `ZOM7001-ZOM7017`, `ZOM7091-ZOM7093`, and
`ZOM9905-ZOM9906` package diagnostic family. Manifest and workspace failures
carry a complete `ManifestFailure` with canonical primary and related anchors.
The typed package diagnostic adapter admits only digest-verified documents,
renders host-path-free identifiers, escapes all source bytes through
`SanitizedSourceView`, and maps original byte spans onto the escaped buffer.
All 24 `ManifestIssue` variants emit `ZOM7001`; duplicate workspace package
names attach exactly one `ZOM7093` note. Focused sanitizer tests cover every
issue token, invalid UTF-8 escaping, offset projection, digest rejection, safe
document names, and primary/related diagnostic placement. The P2 manifest
diagnostic boundary is complete.

P3 now streams one bounded Zstandard frame directly into the POSIX ustar
reader and writes admitted regular files into a factory-provided fresh private
directory. Source paths are normalized to NFC and rejected with the required
duplicate, Unicode-collision, and Unicode 15.1 full-case-fold collision
priority on every host. Incremental SHA-256 produces sorted `SourceTreeFile`
records and the domain-separated source-tree digest without buffering complete
archives or files. Registry archives and two-pass VCS/local directory copies
publish only `DigestVerifiedSourceSnapshot` values; every read rechecks file
type, link count, length, and digest, and verified copies independently
reproduce the complete destination inventory. Explicit `finish()` and the
noexcept retry path own cleanup to completion. Fault-injection tests cover
fresh-directory creation, destination creation/write/sync, partial cleanup,
cleanup retry, and mutation between source passes. `PackageBaseKey`, VCS
selector records, VCS/local package records, canonical registry trust maps,
signing-key identities, and complete Ed25519-verified registry release records
bind resolver inputs to the verified manifest and source-tree digests. Unicode
table regeneration, format, sanitizer build, and all 85 unit executables pass.
The P3 source-admission and source-record boundary is complete.

P4 now has normalized SemVer interval intersection and direct release-membership
queries, plus additive feature expansion over both provenance-bearing and
canonical manifests. The first cross-package resolver slice enforces the
single-version coordinate rule, chooses the greatest eligible non-yanked
release, backtracks when a later dependency invalidates that choice, keeps
target and preparatory-build feature activations separate, expands optional
dependency feature edges to a fixed point, validates dependency library
providers, emits canonical package dependency edges, collapses byte-identical
edges, and reports canonically ordered constraint and cycle evidence. Registry,
VCS, and local verified source records have direct resolver adapters; mutable
VCS selectors remain explicit accepted-selector inputs. Sanitizer tests cover
highest-version selection, backtracking, yanked releases, target/build feature
separation, missing libraries, canonical dependency cycles, deterministic
conflict bytes, and all 256 RFC permutation seeds. Conflict failures now publish
the domain-separated, content-addressed incompatibility derivation DAG rather
than an iterator trace. The release performance gate generates the exact RFC
edge fixture and resolves 10,000 packages, 40,000 candidate releases, and
50,000 edges in 7.26 seconds; its wrapper enforces the 1 GiB peak-RSS limit and
the executable enforces the 40,000-decision limit. The checked-in
`pubgrub-scenarios-v1.json` corpus covers greatest-version selection,
content-addressed no-version derivation, backtracking, yanked releases, and
separate activation domains; an independent schema/hash oracle binds its exact
SHA-256 outputs to the C++ replay tests. The P4 dependency and feature resolver
boundary is complete.

P5 now implements the closed `VerifiedLockGraph` and `LockPackageRecord`
models, canonical package and edge ordering, duplicate and dangling-edge
validation, and the exact canonical TOML writer. The reader validates UTF-8 and
the closed schema, decodes lowercase canonical source and package-key bytes for
registry, VCS, and local sources, reconstructs every strong scalar and feature
set, checks all redundant fields and digests, resolves edge targets, and then
requires byte-for-byte writer reproduction. The checked-in three-source golden
file has a fixed SHA-256 oracle. Durable updates use the zc replacement-file
primitive, file sync, atomic commit, and directory sync; injected failures at
all five stages prove pre-commit preservation and post-commit reporting.
Locked replay compares the verified current graph, checks registry trust, visits
each package and edge once, and records zero resolver invocations. Corruption,
round-trip, release-build, and fault-injection tests pass. The P5 lockfile
boundary is complete.

P6 now has a closed, canonically encoded package compilation request with
sorted non-empty target selections, normalized feature sets, lock mode,
language options, and registry-issued host/target selections. Workspace
verification resolves package and target names, expands root features, derives
the complete RFC 0011 `PackageKey` and `CrateKey`, and hands selected roots to
`CompilerSession`. `zomc compile` is package-only: positional source arguments
are rejected, manifest discovery walks parent directories, explicit manifests
must be regular `Zom.toml` files, and the package, target, feature, lock,
target-profile, language, and panic flags enter the typed request. The real CLI
has an 11-case process test for every `InvocationIssue`, all emitted as
source-less `ZOM7016` without rejected argv or host-path disclosure. Corpus
tests use a package fixture adapter rather than a compiler compatibility path.
The RFC 0010 target registry now reproduces the fixed target-specification hash,
validates semantic projection against backend facts, binds package selections
to one registry revision, rejects unavailable panic capabilities as `ZOM6009`,
and supplies the verified layout token consumed by IR lowering. The session
owns the verified package request, host/target tokens, a package graph, and
digest-verified local snapshots. The local-workspace coordinator materializes
every member through the two-pass snapshot boundary, runs the deterministic
resolver for unlocked and update requests, emits canonical package and edge
lock records, atomically publishes `--update-lock`, and reconstructs a verified
resolution directly from `--locked` without invoking the solver. A two-member
workspace integration test proves dependency resolution, lock publication, and
zero-solver locked replay through the real CLI. The P6 package CLI and session
handoff boundary is complete; remote source acquisition remains an injected
registry/VCS service concern rather than a command-normalization path.

P7 now has the RFC 0011 `PreparatoryBuildScriptKey`, canonical
`BuildScriptOutputRecord`, domain-separated `BuildScriptOutputKey`, and the
complete RFC 0012 `BuildScriptExecutionKey`. All map-like fields sort by
canonical bytes and reject duplicate keys even when the colliding values or
digests differ. Trusted runtime identity is derived from exact object and
manifest bytes, rejects an empty object closure, duplicate object digests, and
runtime ABI mismatch, and participates in the execution-key encoding. Request
and response framing, exact contract input/environment/output/export checks,
all resource-limit invariants, closed response tags, and every build-script and
internal-invariant diagnostic display algebra are executable.

Every cache miss runs through two independently created sandbox adapters. Each
run is contract-verified and converted into a complete RFC 0011 output record;
publication requires byte-identical output records and re-read generated file
bytes. The `LinuxNativeSandboxV1` ownership state machine covers partial setup,
running, exited, and finished states, retains only failed teardown owners, and
has fault injection at every setup and teardown boundary. The checked-in
x86-64 and AArch64 bootstrap/runtime seccomp generators bind the audit
architecture, reject x32, enforce the fixed syscall and scalar-argument policy,
default to `SECCOMP_RET_TRAP`, and have fixed generated-byte SHA-256 oracles.
Host preflight rejects unsupported platforms and checks Linux namespaces,
cgroup v2, seccomp action availability, pidfds, timerfds, and `openat2` without
an unsandboxed fallback. Resource-plan and post-reap priority classifiers are
also executable.

The production Linux launcher now acquires user, mount, PID, and network
namespaces; installs private mounts, cgroup v2 limits, rlimits, fixed
descriptors, bootstrap seccomp, pidfd/timerfd observation, and explicit setup
failure signaling around a digest- and target-verified static PIE image. A
privileged Linux sanitizer integration test executes the real launcher through
canonical IPC and independently materializes the output snapshot. The trusted
runtime verifier decodes the complete ELF symbol and relocation inventories,
checks exact operation classification and required tags, rejects initializers,
and binds the verified object and manifest bytes into the runtime key.

Every cache miss is contract-checked and double-executed. Cache hits reverify
all key, record, generated inventory, byte, UTF-8, environment, and export
facts. Canonical build plans reject duplicates, dangling predecessors, and
cycles; `CompilerSession` executes their stable predecessor-first order and
publishes only an exact plan-key result set. Focused sanitizer tests cover the
cache, plan, final-result, session, policy, launcher, ELF, and manifest
boundaries. P7 implementation is complete.

P8 now rejects every crate, source, module, definition, and impl identity whose
canonical ancestor is absent from the preceding frozen registry. A finalized
compilation root cannot retain one package while naming a crate from another
package. `VerifiedPackageSessionInput` directly replaces the three independent
request, target, and graph installation APIs: its fallible private-constructor
boundary verifies the complete request-root membership, canonical host and
target selections, registry revision, and bidirectional graph-to-snapshot
coverage before `CompilerSession` can mutate state or freeze package identities.
The real CLI resolves, verifies, and installs exactly one such move-only input.
Focused sanitizer and architecture-negative tests cover request/graph,
target-revision, snapshot/graph, ancestor, moved-from, and duplicate-install
failures without publishing partial session state.

Commit `e058db25` replaced the narrower resolver prototype with the accepted
`ResolutionOutput`. The resolver is now the only producer of final package
keys, complete verified package records, canonical package edges, separate
Target and Build feature sets, source-view keys, and the exact
`VerifiedLockGraph`. Registry, VCS, and local releases enter only through
verified record adapters carrying their manifest, source-tree, archive, and
signing metadata. Locked replay reconstructs the exact feature-domain closure
from current roots and verified releases without invoking version solving, then
requires byte-identical lock output. The CLI and `CompilerSession` consume this
output directly and no longer rebuild package keys or activation domains.

P8 remains open. Final and preparatory dependency crates, crate dependency
edges, complete build-script closures, and the production semantic-context
fingerprint are not yet derived from the authoritative resolution output. RFC
0008 owns those remaining session-wide crate-graph and fingerprint surfaces.

P9 now has an executable package architecture gate with eighteen adversarial
fixtures, a vendored-dependency mutation self-test, and a Linux-only privileged
sanitizer CTest registered behind an explicit fail-closed option. The cgroup v2
runner creates one delegated parent and supplies it to the real production
launcher; a privileged Linux AArch64 run built the sanitizer target and passed
the namespace, cgroup, seccomp, pidfd, timerfd, `openat2`, static-PIE, and output
verification path.

All resolver-owned manifests, constraints, analyses, records, canonical sort
buffers, outputs, feature expansion, and locked replay now use the explicit
session-owned memory resource. The architecture gate rejects default Vector,
canonical encoder, clone, encode, canonical-admission, and helper-call
fallbacks throughout that production closure. The release fixture resolves
10,000 packages, 40,000 candidate releases, and 50,000 edges, then performs an
exact zero-solver locked replay with one visit per package and edge. On the
current implementation it completes in 8.94 seconds, records 728,622,169 bytes
of injected peak-live allocation, returns the injected live count to zero after
destruction, and reaches 1,001,635,840 bytes peak RSS under the 1 GiB gate.

One generator now owns eight checked-in package oracles covering lock output,
PubGrub scenarios, canonical framing, source trees, and both seccomp
architectures. Its check mode rejects drift, and nine independent mutation
fixtures prove that changed or undeclared generated output is rejected. Exact
resolution-output framing and all 256 input permutations remain deterministic;
locked replay invokes the solver zero times and visits exactly 10,000 packages
and 50,000 edges.

Source materialization now retains each newly created file capability through
sync and independent SHA-256 readback instead of reopening the destination by
path. A directory-proxy regression rejects any such read-only reopen, while a
same-length corruption fixture proves that capability-based readback still
rejects changed bytes. The corrected composite stress run passed 400 additional
sanitizer compiler processes together with all 1,078 lit, diagnostics, and
grammar tests. The final sanitizer matrix passes 1,250 of 1,250 tests, including
the 1,443.86-second grammar conformance target. Format, RFC, package
architecture, generated-oracle, vendored-source, and release performance gates
all pass on the same implementation.

P9 remains open only for the final combined release rerun after P8, the RFC
0008 crate graph and semantic-context fingerprint, and RFC 0010 target
publication are complete. That rerun must include the privileged Linux sandbox
integration on the final implementation before RFC 0012 can transition to
`LANDED`.

## Decision Record

Decision: ACCEPTED.

On 2026-07-11, every required owner approved RFC 0012 proposal hash
`39b7a9edfd5112b9f72fce569ffab1d274c94c957bd6106f6c9158d23b46a982`.
The accepted design freezes the manifest, deterministic resolver, lock graph,
source materialization, registered target selection, build-script runtime,
sandbox, diagnostic, and verification contracts. The direct implementation
series has entered `IMPLEMENTING`; the checkpoints above record current
evidence without claiming completion.
