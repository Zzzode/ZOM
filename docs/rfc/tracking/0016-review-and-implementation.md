# RFC 0016 Review And Implementation Tracker

This document records discussion, decisions, and implementation evidence for
RFC 0016. It does not approve the proposal. RFC 0016 frontmatter remains
authoritative for status and approvers. The repaired proposal is `DRAFT` at
exact SHA-256
`58dd8a11c7deb8f1646f11427628fab5b49333ac41bd398f9c60e47a377fcaa2`.
The preceding DRAFT proposal at exact SHA-256
`a0f0160d17ddf150e2b2ebc2d718b2d16d054a1ddf1b0d5dc371da7a0542a571`
was returned by cycle-6 review because root PID release, complete event-12 exit
histories, exhaustive native-vfork mutation guards, exact syscall-548 input
and altstack validation, scalar sigreturn reads, typed RSEQ digest preimages,
mandatory exec notifications, and inner-operation trace roots were incomplete.
No locked approval carries forward.
The preceding DRAFT proposal at exact SHA-256
`df285fca875bed17353a6ad28c971aef544a47f0199c0f738c9782c363af648b`
was returned by cycle-5 review because sigreturn conflated its first two reads;
event 12 collapsed committed diversion into landing; syscall 548, dynamic RSEQ
image lifecycle, exec/PID exit ordering, and native-vfork writer classification
were incomplete. No locked approval carries forward.
The preceding DRAFT proposal at exact SHA-256
`cef456e70268ee35dc60076bd513caf50dfe3a613c2201bde56e26a2ec4e72ed`
was returned by cycle-4 review for the controlled-guest, pending-signal,
handler-entry, event-12, native-vfork writer/snapshot, digest-preimage, and KAT
closure gaps recorded below. No locked approval carries forward.
The preceding DRAFT proposal at exact SHA-256
`c84ac0098399886b996fc4bf47f954eb18a1cb1235b5f6393e00248470aee2e7`
was returned by cycle-3 author review because vfork used the wrong memory
architecture; signal lock and decision evidence was collapsed; frame,
forced-signal, sigreturn, exec-holder, event-12, and event-11 semantics were
incomplete; and composite KATs lacked complete canonical bytes. No locked
approval carries forward.
The preceding DRAFT proposal at exact SHA-256
`a6fd158218e49de2427764c51cdbeb7e75f7df322ae41cd86962bc5406b6b74e`
was returned by cycle-2 locked review because its mapping KAT crossed the
write fence; robust misalignment attempted a wake; `get_signal`, frame,
sigreturn, exec, output/vfork, nonlocal escape, and event-11 authority remained
incomplete; and dependent KAT and coverage evidence was stale. No locked
approval carries forward.
The preceding DRAFT proposal at exact SHA-256
`2ca1b35178c8a98740040c98be28e50b0937500018ca75b79aab938fea4570d5`
was returned by locked Linux-neighborhood reviews because mapping and signal
digests lacked complete typed preimages, restart and non-local-escape closure
was incomplete, non-leader exec did not reproduce the exact PID-list exchange,
child fatal transport and inherited-stop handling were wrong, and futex wake
failures lost raw results and phases. Adjacent review also found that frame
setup and sigreturn validation failures were incorrectly terminal instead of
forcing a new SIGSEGV episode. No locked approval carries forward.
The preceding DRAFT proposal at exact SHA-256
`a505398f0a8e59f4eca980183836492b3e9cac62baa66c382dd4350c59181491`
was returned by three fresh audits because clear-child-TID lifecycle, child
first return, retained PID-slot occupancy, exact configuration records,
primitive output proof, and restart-chain closure were incomplete. No locked
approval carries forward.
The preceding DRAFT proposal at exact SHA-256
`040a653ababbe0dbae547aeebc3c1a2d0920e864e8cc177efc2d36b0dbccdd67`
was returned by audit B and the subsequent Linux-neighborhood/configuration
pre-reviews because set-TID copy faults, requested-ID occupancy, clone restart
attempts and late effects, and the complete outer futex cleanup lifecycle were
not closed. No locked approval carries forward.
The preceding DRAFT proposal at exact SHA-256
`c44ad2bf1252932d4ca82cfb1565f50e74bbe7d6982c765a4bc45a56589b1f83`
was returned by audit C because clone3 set-TID capture lost signed `pid_t`
values before later validation, and robust-list futex faults lost the already
attempted same-iteration next-read result. No locked approval carries forward.
The preceding DRAFT proposal at exact SHA-256
`fe0faaab33dd1a692ebb04f0cd293aa94037b8f7eceb4f40081fa7f381d6a7c1`
was returned by two independent audits because clone3 did not represent the
extension-tail inspection that precedes common-prefix copy, and a registered
robust-list head could not represent faults during its three ordered pre-walk
reads. No locked approval carries forward.
The preceding DRAFT proposal at exact SHA-256
`27869e83b756db43bee6ccfa2c7099deabd17345e574bcf9cbcdb5d8f9ef847f`
was returned because clone3 narrowed the copied U64 `exit_signal` before Linux
validation, and robust-futex cleanup omitted `ROBUST_LIST_LIMIT=2048`, exact
walk termination and cycle behavior, and the early-return-versus-pending
ordering. No locked approval carries forward.
The preceding DRAFT proposal at exact SHA-256
`0fa25aaca261aa66986ec62b38e75b8f5202323c87c908491d36ff6faf61060c`
was returned because it placed every regular-file write at its requested
current or explicit offset and therefore contradicted Linux v6.8 `O_APPEND`,
including positional writes and the `pwritev2(-1)` current-position route. No
locked approval carries forward.
The preceding DRAFT proposal at exact SHA-256
`38fd6272a0d0cb7a4f83317278ca1586e06f0b1010da0eb815ffb0b6fee16121`
was returned because it normalized `preadv2`/`pwritev2` raw offset `-1` as an
ordinary explicit offset, losing the Linux v6.8 current-position route,
open-description advancement, raw syscall identity, and signed offset
evidence. No locked approval carries forward.
The preceding DRAFT proposal at exact SHA-256
`6f49edbb8ef1f5d2e327f5a4f53923eb5945c3d192dc18d0d7a4fd7d7d094777`
was returned because scalar and vectored read/write plus transfer effects had
no Linux v6.8 `MAX_RW_COUNT` raw/effective-count boundary and could exceed
signed return and U31 progress domains. No locked approval carries forward.
The preceding DRAFT proposal at exact SHA-256
`d9ab1538dea9556569a15ceb13993538b283a7daf37907ae4a35342a187bdaf2`
was returned because it clamped the unsigned getdents ABI count to `INT_MAX`
instead of modeling a valid Linux v6.8 conversion boundary, and it required
alignment padding to be zero even though `filldir` and `filldir64` leave those
bytes unchanged. No locked approval carries forward.
The preceding DRAFT proposal at exact SHA-256
`b4947fd3d9e555659d0b138284e4b7dd12c22ac1a70d6c128160f7489a1bb76c`
was returned because endpoint completion duplicated per-chunk write effects;
the custom ptrace stops and same-mm output staging were not concrete; exec-mm
clear-child-TID, robust pending cleanup, and reparenting were incomplete; and
getdents and clone3 still lacked exact ABI boundaries. No locked approval
carries forward.
The preceding DRAFT proposal at exact SHA-256
`769593d09c3e0f2eaf9c1d89387fb199fb2c96a9a50a7d6a9512015fd30b41d5`
was returned because blocking operations lacked entry/progress/completion
closure and coherent partial-pipe and futex-requeue ordering; synthetic ptrace
parking and restart behavior was not implementable; resumed same-mm peers could
mutate syscall staging; numeric identity reuse, multithreaded exec, robust
futex cleanup, SIGCHLD policies, getdents bounds and types, and clone3 partial-
size and parent semantics were incomplete. No locked approval carries forward.
The preceding DRAFT proposal at exact SHA-256
`5e3cf506d4d213e85539898d26ff212c69255079a8fc070395f9c2f6e9c1ae33`
was returned by two exact-hash reviews because the RFC 0006 snapshot had
drifted; blocking calls lacked distinct entry/completion order and deterministic
queues; peer freezing could deadlock; clone3, thread exit, TID, sharing,
execveat/at-family, getdents, and readlink facts were incomplete; and ptrace
signal, group-stop, restart, interrupt-stop, and capability-preflight contracts
were open. No locked approval carries forward.
The preceding DRAFT proposal at exact SHA-256
`de756f20a27420853d7fdeccf2f71492837deb95a4bd8c1d88314897b4ed4d6f`
was returned because clone/clone3 `CLONE_VFORK`, including glibc 2.39
`posix_spawn`, bypassed the then-current vfork path; endpoint capacity, occupancy, and
writable-space authority was absent; open/stat/link/metadata and host-key
requests lost behavior-changing inputs; ptrace pairing contradicted Linux 4.8
seccomp-stop ordering; and a committed report could not bind the commit that
contained it without self-reference. No locked approval carries forward.
The preceding DRAFT proposal at exact SHA-256
`e7c88f755ceff08d90cbbca20d68de2d03d388170ceb76e226a5666d6d755fbb`
was returned because pipe and socket stream data flow and zero-copy transfers
had no closed source/destination authority algebra; the executable syscall and
host-observation partitions were incomplete; controlled-memory evidence depended on
unavailable per-instruction exits or unspecified setup/fd ownership; and
`normalized_events_sha256` had no exact canonical binary codec or exhaustive
oracle and mutation corpus. No locked approval carries forward.
The preceding DRAFT proposal at exact SHA-256
`2b9e3729ee102993d2537dba9368e36a118902b8ce88729a87fa6ad05fc1b83e`
was returned because close failures were replayed through the ordinary
success-or-failure algebra even when the fd had been consumed or its state was
unknown; referenced requests, events, outcomes, and platform mappings remained
open; the host-input broker had no enforceable vDSO, auxiliary-vector,
commpage, Mach, or direct-CPU mechanism; pipe, socketpair, close-range, fcntl,
and vfork transitions were incomplete; and header coverage still lacked exact
object-level LLVM attribution and nonempty final-target membership. No locked
approval carries forward.
The preceding DRAFT proposal at exact SHA-256
`f1a24c14b3cbea3daf12616ece9dcb90c39e304229cffc9fad7674124f600a89`
was returned by locked review because fallible filesystem events required
success-only descriptor, object, and metadata fields; clone/thread/descriptor
sharing and file-mapping lifecycle replay were incomplete; clock, random, host
identity, `uname`, `sysctl`, CPU, and system-capability inputs were open on both
platforms; and LCOV required every compiler source to have one compile record,
which is false for headers included by multiple translation units. No locked
approval carries forward.
The preceding DRAFT proposal at exact SHA-256
`a1f8cc3e8b5d01e817604abe86cb08ddd835d7d00216d716670c3b89983dd43c`
was returned by locked audit A because the trace did not close metadata,
directory, symlink, mapping, content-mutation, and path-mutation events or
prove unique producers, final paths, and write-after-read; its Mach-O
`LC_RPATH` stack traversed root-to-loader instead of dyld's loader-to-root
order; it left the glibc loader, cache, hwcaps, `NODEFLIB`, multiarch, and
default-directory algorithm undefined; and Prior Art omitted mature hermetic
build, provenance, reproducibility, supply-chain attestation, and
dual-revision coverage designs. No locked approval carries forward.
The preceding DRAFT proposal at exact SHA-256
`3f6a4e1a831454faacf12fb7022b8c24b71dc1b4cf784e41fcc6e18b6320823d`
was returned by locked audit A because cross-run identity included raw stream
digests, same-test comparability incorrectly required executable-byte equality,
configure and build descendants and inputs were not closed, and Mach-O token
and run-path resolution was incomplete. No locked approval carries forward.
The preceding `REVIEW` proposal at exact SHA-256
`713bc7ce30df9e99a537cc94f87e646df4ef18fa1d96d02e90724c46cf90d7c0`
was returned by the task-router owner because its verification contract omitted
per-file and baseline coverage gates. Every approval on it is void.
The preceding metadata-identical `DRAFT` proposal at exact SHA-256
`2d9e22738f4b57550a8fb16597c4f5b5da0247c7e7cf8553f1100d0b5c83604a`
received three independent locked-audit approvals before the status
transition.
Exact REVIEW snapshot
`1dc2d86b82db95aaf71796bfba7ef2ae95c3e97edfcd092f3d1a75d608e9f79b`
was returned by the runtime-memory owner, and every approval on it is void.
Exact REVIEW snapshot
`1f6c75cc50c828aa7801c0826a5ff0d429a8ce80ca3f30d82fa42241a7ff3b63`
was returned by the RFC owner, and every approval on it is void. Implementation
is blocked until the repaired exact DRAFT passes three fresh locked audits, a new exact
REVIEW snapshot receives every required-owner approval, and the RFC moves to
`ACCEPTED`.

## Discussion Record

### 2026-07-16 Design Intake

The target-registry implementation audit found that the production
`VerifiedTargetSelection` does not carry the semantic-context fingerprint
required by RFC 0010, and `zomc` verifies target records before
`CompilerSession` freezes semantic identity. The audit also found manual target
projection, incomplete target and registry codec evidence, unbound registry
lifetime, no exact package-to-backend panic mapping, incomplete strategy-pair
validation, and no LLVM discovery contract in the build or CI configuration.

The proposed direct replacement retains the exact registry snapshot through
the package session, issues target tokens only after the corresponding semantic
context freezes, validates target facts with LLVM, retains exact data-layout
bytes as identity, and binds the accepted target and registry hash domains with
their complete preimages.

### 2026-07-17 Draft Adversarial Repair

The draft was repaired after an adversarial review found that deterministic
fingerprint equality alone was not a live-session authority boundary, the LLVM
admission contract did not prove backend compatibility, the package-session
records left ownership implicit, and the registry oracle covered only outer
framing.

The repaired proposal now requires one private
`FrozenSemanticContextAuthority` containing both `SemanticContextBrand` and
`SemanticContextFingerprint`. `VerifiedPackageSessionInput` owns the verified
request, exact target-registry snapshot, resolution output, build-script plan,
and sorted unique source snapshots by value. `VerifiedTargetSelection` owns its
five fields by value, including the canonical specification, and preparatory
and final sessions receive distinct live brands even when deterministic
fingerprints are byte-equal.

Registry construction now has only the closed `InvalidFact` and
`CanonicalCodecMismatch` issues, mapped to fatal `ZOM9957` and `ZOM9958`
diagnostics. Context-bound selection returns RFC 0010's
`IrOperationResult<VerifiedTargetSelection>` directly and maps revision,
missing-profile, invalid-fact, unsupported-capability, and codec failures into
the RFC 0010 algebra without a second selection-failure type.

LLVM admission is pinned to LLVM 22 and requires typed `TargetRegistry` lookup,
`MCSubtargetInfo` CPU and feature validation, `TargetMachine` construction,
`isCompatibleDataLayout`, and a known matching object format. The complete
initial backend set is X86 and AArch64. The exact eleven LLVM components,
Homebrew macOS source, signed `apt.llvm.org` Linux source, CPU and feature
grammar, and all count, byte, overflow, and preimage limits are normative.
`UnknownObjectFormat` is rejected.

Codec evidence now distinguishes the 111-byte codec-only target oracle and the
52-byte outer-registry-framing oracle from production admission. It adds
189-byte LLVM-22-admitted `Unwind` and `Abort` target preimages, their target
IDs, the complete 197-byte profile record, and the complete 248-byte registry
preimage and revision. Production and structurally independent test encoders
must reproduce the complete set.

This repair changes only the proposal and this tracker. RFC 0016 remains
`DRAFT`; it records no owner approval and authorizes no implementation.
The repaired proposal snapshot SHA-256 is
`11fbb96f0d938f7e31060366ec49cc2848a8105f3b32ee099b475234ef06ea6c`.

### 2026-07-17 Locked Draft Audit Return

Three independent audits returned exact draft snapshot
`11fbb96f0d938f7e31060366ec49cc2848a8105f3b32ee099b475234ef06ea6c`.
The snapshot had five blocking contract families:

- it invented `RFC0012::VerifiedBuildScriptPlan` and
  `RFC0012::ResolvedPackageSourceSnapshot` instead of preserving the exact RFC
  0012 preparation and final handoffs, build results, generated views, and
  snapshot cleanup;
- target selection never checked runtime panic capability, so its normative
  `Unwind` entry could publish a token while the live runtime was abort-only,
  contrary to RFC 0006 and RFC 0012;
- the 197-byte profile record had no standalone normative preimage or digest;
- `find_package(LLVM 22 REQUIRED CONFIG)` was incompatible with the required
  LLVM 22.1.8 CMake version file, and the rolling Homebrew `llvm` formula was
  not a pinned dependency; and
- unversioned LLVM main documentation did not bind the version-sensitive
  `TargetRegistry`, `MCSubtargetInfo`, `TargetMachine`, triple, data-layout, or
  CMake contracts to LLVM 22.

### 2026-07-17 Package, Runtime, Oracle, And LLVM Repair

The repaired draft wraps the exact RFC 0012 `VerifiedBuildPreparationInput`
and `VerifiedFinalPackageSessionInput` records, moves one registry and one
runtime capability snapshot through both handoffs, and preserves the source
views, build-plan and build-result maps, generated views, key-set proof, and
snapshot `finish()` contract.

It binds RFC 0006 and adds a single generated runtime capability authority.
The initial `{ zom-v1 -> {Abort} }` manifest has a 59-byte canonical preimage
and revision
`329ee4445e2a6e4fa84ac05c481d7643759a7b37dea4b306d7b075b0d6284739`.
Registry construction retains its live brand and deterministic revision.
Target selection uses two exhaustive panic conversions, checks runtime support
before lowering, and publishes a seven-field token only after the exact
thirteen-step verifier succeeds.

The proposal now publishes the exact 197-byte profile preimage and SHA-256
`a5d3e5b0806c3bf8b73d3e1bb6d3c76f6575f63da6a980a68ca0441c3e6b87df`.
An independent extraction reproduced all seven 59/111/189/189/52/197/248-byte
blocks and their declared digests. The LLVM API baseline is the official
`llvmorg-22.1.8` source tag. CMake uses unversioned config discovery through an
explicit `LLVM_DIR` and then requires exact `LLVM_PACKAGE_VERSION == 22.1.8`;
CI uses `llvm@22`, `llvm-22-dev`, and fixed runner labels.

`python3 scripts/check-rfc.py` and `git diff --check` pass. The exact repaired
draft snapshot submitted for fresh locked audit is
`2fe4d553abce6651013a02e74466e2ddc29e9f223e6479966458e3e31ef8429b`.

### 2026-07-17 Phase Authority And Brand-Issuance Audit Return

Three locked audits returned exact draft snapshot
`2fe4d553abce6651013a02e74466e2ddc29e9f223e6479966458e3e31ef8429b`.
Preparatory host compilation was incorrectly required to compare its token
with a final wrapper that did not yet exist. Runtime capability brand issuance
had no uniqueness, exhaustion, wraparound, or non-reuse contract. Runtime
capability construction also reused target-registry-specific issue and
diagnostic names before a registry existed. Finally, the declared Homebrew
`llvm@22` formula URL returned HTTP 404 because the current official source
page is the `llvm` formula and `llvm@22` is its install selector.

The repaired draft makes every consumer compare against its current phase
wrapper: the preparation wrapper for build-script host work and the final
wrapper for final host or target work. It defines one process-global serialized
runtime capability factory, private construction, exact nonzero monotonic
issuance through `UINT64_MAX`, atomic exhaustion without wrap or reuse, and
test-only boundary and concurrency proof. Exhaustion returns `InvalidFact`
before snapshot publication.

Both pre-context builders now use the correctly owned
`TargetAuthorityConstructionIssue`. `ZOM9957 TargetAuthorityInvariant` and
`ZOM9958 TargetAuthorityCanonicalCodecMismatch` name the shared authority
layer rather than a registry that may not exist. The Homebrew source link names
the live official `llvm` formula while CI retains `brew install llvm@22` and
the exact `22.1.8` version gate.

The exact repaired draft snapshot submitted for a fresh locked audit is
`e7bd70bcd74cb2d84a0fb1901a96bfdac74dd08a5c9bb3976d16d275cf13f764`.

### 2026-07-17 Exhaustion Fixture Audit Return

Two locked audits returned exact snapshot
`e7bd70bcd74cb2d84a0fb1901a96bfdac74dd08a5c9bb3976d16d275cf13f764`
for one shared off-by-one blocker. The factory state stores `lastIssued`, so a
fixture starting at `UINT64_MAX - 1` could issue only `UINT64_MAX` and could not
prove the promised final two issuances. The repaired fixture sets
`lastIssued = UINT64_MAX - 2`, proves issuance of `UINT64_MAX - 1` followed by
`UINT64_MAX`, then proves repeated exhaustion, no wrap, no reset, no reuse, and
concurrent uniqueness. The exact repaired draft snapshot is
`76774bffff698bf3a84111566ccf0b8ea568a7fdd4f5e72828078d79421cac09`.

### 2026-07-17 Formal Review Entry

Three locked audits approved exact repaired DRAFT snapshot
`76774bffff698bf3a84111566ccf0b8ea568a7fdd4f5e72828078d79421cac09`.
They reproduced all five bound proposal hashes, all seven canonical vectors and
digests, LLVM 22.1.8 APIs and live probes, phase-local package authority,
runtime brand exhaustion, shared diagnostics, owner routing, and repository
gates. The metadata-only transition to `REVIEW` produced exact proposal
SHA-256
`e4df8edba990564e4718484e36285bdbc084cd70ef0ea4fdea2c37cc32de813d`.
All eight required owners must approve this same hash; any return voids every
approval and requires a new draft and review snapshot.

### 2026-07-17 Formal Review Return And Routing Repair

The `task-router` review returned exact REVIEW snapshot
`e4df8edba990564e4718484e36285bdbc084cd70ef0ea4fdea2c37cc32de813d`.
The preceding `rfc` and `module-system` approvals are void. The returned
snapshot incorrectly assigned build and CI implementation to a routing-only
owner, omitted `CMakePresets.json`, `README.md`, `AGENTS.md`, and exact
repository-gate ownership, and lacked isolated configure failures for unset
`LLVM_DIR`, wrong package version, `llvm-config` disagreement, missing
components, and missing X86 or AArch64 inventory.

The repaired draft assigns top-level and compiler CMake integration to
`ir-backend`, CI workflows and configure-contract gates to `verification`, and
updates the manifest to cover every named path. It requires one positive
configure census plus repository-owned stable failures for all five negative
families. No approval survives this return; a new exact DRAFT snapshot must
pass locked audit before a fresh REVIEW transition.

The first locked routing audit returned DRAFT snapshot
`b3c3e52bded485aac7ab45d227e2805d43af4f27cb842649ab9c818aa1f9b1b6`
because its recursive compiler-CMake glob reassigned subsystem build files to
`ir-backend`, and the manual routing view omitted the named developer and
architecture documentation surfaces. The next repair narrows LLVM build
ownership to the top-level, compiler-root, IR, and backend paths, retains each
subsystem's own CMake ownership, registers the otherwise uncovered basic and
trace build files, and adds explicit `README.md`, `AGENTS.md`, and
`docs/design/**` routes. The exact repaired DRAFT hash is recorded after the
repository checks pass:
`29a2b4afd00007d26c601b86e22697ae97f9d2255408e69deddf45f4a9527b43`.

### 2026-07-17 Formal Review Re-entry

Three independent locked audits approved exact repaired DRAFT snapshot
`29a2b4afd00007d26c601b86e22697ae97f9d2255408e69deddf45f4a9527b43`.
They verified exact path ownership, the positive LLVM census and five negative
configure families, all five bound RFC hashes, phase-local package authority,
runtime and target registry lifetime, and the repository enforcement matrix.
The transition to `REVIEW` changes proposal process metadata only. All eight
owners must approve the new exact REVIEW hash recorded below; no approval from
the returned snapshot survives. The exact REVIEW proposal SHA-256 is
`179318bb7d4f2d191e884e58349a6c9f2fef9e6730621546ef1a08c50cdca101`.

### 2026-07-17 Second Formal Review Return

The `task-router` owner returned exact REVIEW snapshot
`179318bb7d4f2d191e884e58349a6c9f2fef9e6730621546ef1a08c50cdca101`
because `products/zomlang/compiler/ir/**` appeared under both `ir-backend` and
`runtime-memory` implementation rows. The in-flight `module-system` approval
and every other approval on that snapshot are void.

The repaired DRAFT makes `ir-backend` the sole implementation owner for target
registry, LLVM validation, compiler-side panic mapping, and target profile
work. `runtime-memory` exclusively owns the generated runtime capability
registry and runtime query. It also separates RFC document ownership from
routing-governance ownership so every Repository Impact path has one
authoritative primary owner. A new exact DRAFT hash is recorded after gates and
must pass locked audit before another formal review:
`d337664db535fc5edd3e5cd97a169688936cd51d6cb4458289a6f6f01a6e0739`.

The next locked routing audit returned that exact DRAFT snapshot because the
`products/zomlang/runtime/**` impact glob also covered concurrency-owned task,
async, actor, channel, and scheduler paths excluded from `runtime-memory` by
the manifest. The repair names only `panic-capabilities.def`, `panic.h`,
`panic.cc`, and the runtime `CMakeLists.txt`. No concurrency path is in scope.
A fresh exact DRAFT hash is recorded after the repository gates pass:
`3d07a96701900e3c1a9a15fe989cc31b1b75cd168a9f85b38c1b9058e8281632`.

### 2026-07-17 Third Formal Review Re-entry

Three independent locked audits approved exact repaired DRAFT snapshot
`3d07a96701900e3c1a9a15fe989cc31b1b75cd168a9f85b38c1b9058e8281632`.
They verified one authoritative primary owner for every impact path, the exact
runtime panic surface without concurrency overlap, the LLVM positive and
negative configure matrix, all five bound proposal hashes, RFC 0012 wrapper
ownership and cleanup, runtime and target authority, and repository gates.
The metadata-only `REVIEW` transition produces a new exact proposal hash. All
eight owners must approve that same hash; no prior approval survives. The exact
REVIEW proposal SHA-256 is
`f9b6329ac950e0a9a03f1705bf6f59121c7977ce8a2d108dc8deca39e750e1f7`.

### 2026-07-17 Owner-Prompt Routing Audit Return And Repair

Two locked audits approved exact repaired DRAFT snapshot
`e665834819cc79cbd862e60314f175fdc4306e1c6308f48bb4914c8ab64b49f3`,
but the independent task-router audit returned it because three owner prompts
did not reproduce the manifest path census. The `ir-backend` prompt omitted
top-level CMake, presets, and compiler basic/trace CMake paths; the
`verification` prompt omitted workflows, README, and the IR architecture gate;
and the `spec-audit` prompt omitted design documentation. The RFC also did not
name those prompt files in Repository Impact or require executable consistency
across the manifest, manual routing matrix, `AGENTS.md`, owner prompts, and the
impact table.

The repair synchronizes all three prompt `Owns` blocks and the `AGENTS.md`
summary with the manifest and manual routing matrix. It assigns the three
prompt files to routing governance, adds a one-owner routing-consistency gate
with path-family negative fixtures to Operational Readiness, Acceptance, and
the Test Plan, and records the complete prompt surface in Repository Impact.
No approval survives the return. The exact repaired DRAFT proposal SHA-256
submitted for a fresh locked audit is
`4498d7397b4bfd2dc7f95b06894e7887057164582ebe7f4fc7b7ec723ed70219`.

### 2026-07-17 Panic-Algebra Audit Return And Repair

A locked audit returned exact DRAFT snapshot
`4498d7397b4bfd2dc7f95b06894e7887057164582ebe7f4fc7b7ec723ed70219`
because its panic mapping referenced nonexistent
`RFC0012::PackagePanicStrategy` and `RFC0010::BackendPanicStrategy` type names.
RFC 0012 declares an anonymous `Abort | Unwind` field domain on
`RegisteredTargetSelection.panicStrategy`, while RFC 0010 declares the closed
backend `PanicStrategy` algebra.

The repaired proposal gives the RFC 0012 field domain the exact overlay name
`PackageSelectionPanicStrategy`, preserves its field position and tags, and
maps it directly to RFC 0010 `PanicStrategy`. It removes both nonexistent names
and explicitly keeps the package and backend types distinct. No approval
survives the return. The exact repaired DRAFT proposal SHA-256 submitted for a
fresh locked audit is
`5f2396cb209931321c6693cc55ae471f6914398f01ff2f510f4cfbe909aad566`.

### 2026-07-17 Fourth Formal Review Re-entry

Three independent locked audits approved exact repaired DRAFT snapshot
`5f2396cb209931321c6693cc55ae471f6914398f01ff2f510f4cfbe909aad566`.
They reproduced the five bound RFC hashes, the RFC 0012 package-selection
panic field and its mapping to RFC 0010, all seven canonical vectors, the
thirteen-step failure precedence, LLVM 22.1.8 build and CI requirements, and
the single-owner routing census. The metadata-only transition to `REVIEW`
produced exact proposal SHA-256
`80a795ae4a800080e753401bb3662741b221b9ce6550a280cd710b912eae84bb`.
All eight required owners must approve this exact hash; any return voids every
approval and requires a new repaired draft and review snapshot.

### 2026-07-17 Phase-Binding Formal Review Return And Repair

The `module-system` owner returned exact REVIEW snapshot
`80a795ae4a800080e753401bb3662741b221b9ce6550a280cd710b912eae84bb`.
The `task-router` and `rfc` approvals and every other review result on that
snapshot are void. The returned contract exposed one generic verifier over an
arbitrary `RegisteredTargetSelection`, did not encode preparation-host,
final-host, and final-target authority as distinct APIs and proof types, did
not require exact complete package-selection equality at consumers, and did
not make target-bound wrapper construction and association private and
single-producer.

The repaired DRAFT makes both target-bound wrappers private-constructor and
atomically produced by the target-bound package-session implementation.
Preparation exposes only `verifyHostSelection` bound to its exact request host.
Final input exposes separate host and target methods bound to their exact
request fields. Three unrelated closed proof types prevent cross-phase and
host/target exchange. The shared thirteen-step verifier is private and accepts
no caller-selected phase or registered selection. Every consumer first checks
complete package-selection equality against its phase-authorized request field;
mismatch is `InputRevisionMismatch` before every other consumer check.
Architecture and unit-test requirements cover wrapper construction, raw
verifier exposure, preparation target issuance, phase swaps, host/target swaps,
and complete-selection mismatch. The exact repaired DRAFT proposal SHA-256 is
`8293553957c294f553d6a75fedf6dff95e1bb3421c1daa9ce02d83d1ab2ff9fa`.

### 2026-07-17 Phase-Context Locked Audit Return And Repair

A locked audit returned exact DRAFT snapshot
`8293553957c294f553d6a75fedf6dff95e1bb3421c1daa9ce02d83d1ab2ff9fa`.
Although selection fields and proof types were phase-specific, all three
wrapper methods still accepted the same generic
`FrozenSemanticContextAuthority`. Final issuance could therefore mint a
statically final proof from a retained preparatory authority and defer rejection
until a consumer. The test plan did not cover wrong-phase context at issuance.

The repair introduces unrelated private-constructor
`FrozenPreparatorySemanticContextAuthority` and
`FrozenFinalSemanticContextAuthority` capabilities. `CompilerSession` is their
sole producer and consumes the generic authority into exactly one phase only
after the corresponding preparatory or final identity freeze. Preparation and
final wrapper methods accept only their phase authority. The verifier-only
corruption fixture proves wrong-phase issuance returns
`InputRevisionMismatch` before request-field selection or token publication,
and architecture fixtures reject generic authority parameters, public phase
construction or conversion, and alternate producers. The exact repaired DRAFT
proposal SHA-256 is
`e282011006333ccb0bcc6aaaa4ddd8ce04d78ff527c0296a7f36c189c8a36b83`.

### 2026-07-17 Final-Authority Ordering Locked Audit Return And Repair

A locked audit returned exact DRAFT snapshot
`e282011006333ccb0bcc6aaaa4ddd8ce04d78ff527c0296a7f36c189c8a36b83`.
Its phase authority API was sealed correctly, but the sole-producer prose
required definition and impl freeze before final authority construction while
the total session order constructed that authority before freezing definition
and impl identity. No implementation could satisfy both requirements.

The repair selects the non-circular RFC 0011 boundary. `CompilerSession`
constructs the final phase authority exactly after final package, crate,
source-content, and module freeze completes the semantic-context fingerprint.
It retains that authority while the same context freezes definition and impl
registries, then issues final host and target proofs. Architecture, acceptance,
and ordering tests reject phase-authority construction before source/module
freeze and target-proof issuance before definition/impl freeze. The exact
repaired DRAFT proposal SHA-256 is
`92c7249b4b1187af417bfdc3fbb9cd60d2a0f1905422ad736b8033e871b2abb8`.

### 2026-07-17 Closed-Phase Negative-Proof Locked Audit Return And Repair

A locked audit returned exact DRAFT snapshot
`92c7249b4b1187af417bfdc3fbb9cd60d2a0f1905422ad736b8033e871b2abb8`.
The phase types and non-circular order were valid, but the test plan also
required a privileged corruption fixture to wrap a preparatory generic
authority as the final closed type and then detect the phase mismatch at
runtime. Because the generic authority intentionally contains no phase tag or
parallel expected identity, such a bypass would erase the only discriminant
and could not produce the specified runtime result.

The repair keeps the stronger closed API and removes the impossible bypass.
Wrong-phase issuance has no runtime result because it is not a member of the
callable algebra. Compile-negative API tests and architecture fixtures prove
generic, opposite-phase, converted, caller-constructed, and alternate-producer
authorities cannot reach an issuance method. Runtime failure precedence now
governs only representable calls, beginning with complete authorized-selection
equality. The exact repaired DRAFT proposal SHA-256 is
`32a96841ea0b2d441616d382e3587bcf88e2d6e1c7e9f17b8c7aee480cfdf1fb`.

### 2026-07-17 Same-Phase Association Locked Audit Return And Repair

A locked audit returned exact DRAFT snapshot
`32a96841ea0b2d441616d382e3587bcf88e2d6e1c7e9f17b8c7aee480cfdf1fb`.
Wrong-phase calls were unrepresentable, but any final wrapper could still accept
a final authority created while holding another wrapper, and the same gap
existed for preparation. That representable call could combine one context with
another package selection because neither the wrapper nor phase authority
carried an association identity.

The repair adds one opaque process-local `TargetPackageSessionBrand`. It is
issued only after complete preparation association validation, owned by the
preparation wrapper, moved unchanged into the final wrapper, and copied into
phase authorities only by `CompilerSession` while holding that exact wrapper.
Exact brand equality is the first issuance check. A same-phase authority swap
returns `InputRevisionMismatch` before request selection or the thirteen-step
algorithm. The private monotonic factory has explicit exhaustion, final-two,
non-reuse, and concurrent-uniqueness tests; its brand is absent from canonical
identity, target tokens, diagnostics, and persistence. Compile and architecture
fixtures reject constructors, alternate issuers, reissuance, and omitted brand
checks. The exact repaired DRAFT proposal SHA-256 is
`2bb340f106c3ef5a3fe2e7e9dce5f954339ee4bd4096b9160b213c08fc7b9c46`.

### 2026-07-17 Atomic Final-Handoff Locked Audit Return And Repair

A locked audit returned exact DRAFT snapshot
`2bb340f106c3ef5a3fe2e7e9dce5f954339ee4bd4096b9160b213c08fc7b9c46`.
Its record contract made final-wrapper construction the sole consumer of the
target-bound preparation wrapper, but its session order first published a
standalone RFC 0012 `VerifiedFinalPackageSessionInput` and only then consumed
the outer preparation wrapper. RFC 0012 final construction itself must move the
preparation-owned `SourceViewStore`, so those two steps required either a
partial move or an unbound final-value interval and violated cleanup ownership.

The repair merges those steps into one private atomic transition. After build
result and key-set proof, it consumes the complete target-bound preparation
wrapper, moves its exact nested request, resolution, source views, and build plan
to construct the RFC 0012 final record, and publishes that record only inside
the target-bound final wrapper with the same registry, runtime snapshot, and
package-session brand. No standalone or caller-supplied final input and no
partially moved preparation state exist. Failure injection covers validation,
key-set proof, nested construction, every authority move, and final publication;
all retained snapshots finish, and cleanup failure preserves RFC 0012
precedence. The exact repaired DRAFT proposal SHA-256 is
`e2cae27ff2ad0ef512e31b67ec6190f67a46a0143e6072764309e57879b47cd3`.

### 2026-07-17 Transition Ownership Locked Audit Return And Repair

A locked audit returned exact DRAFT snapshot
`e2cae27ff2ad0ef512e31b67ec6190f67a46a0143e6072764309e57879b47cd3`.
It placed authoritative build-result, generated-view, and key-set validation
before the private transition consumed the target-bound preparation wrapper,
while simultaneously requiring those failures to occur in transition-owned
state with complete snapshot cleanup. The transition could not clean up source
snapshots it did not yet own.

The repair makes build execution produce only one untrusted candidate result
map. The private transition first consumes both that complete map and the
complete target-bound preparation wrapper. Only after owning every source and
generated snapshot does it validate outputs, generated views, and the exact
plan/result key-set relation; construct the exact verified result map and nested
RFC 0012 final record; and publish the outer target-bound final wrapper.
Architecture and unit tests reject pre-transition authoritative validation and
inject every validation and publication failure under transition ownership.
The exact repaired DRAFT proposal SHA-256 is
`6962c02c117371218925129b20f3cfffb48d8c5daa7d605db3ec84fb48766d04`.

### 2026-07-17 Phase And Atomic-Handoff Locked Draft Approval

Three independent locked audits approved exact DRAFT snapshot
`6962c02c117371218925129b20f3cfffb48d8c5daa7d605db3ec84fb48766d04`.
They independently reproduced all five bound RFC hashes and `check-rfc.py`
16/16, and approved:

- unrelated preparatory/final context authorities and preparation-host,
  final-host, and final-target proof types;
- exact wrapper/request selection binding and representable failure precedence;
- opaque same-wrapper package-session association, monotonic exhaustion, and
  same-phase swap rejection;
- the non-circular source/module fingerprint, phase authority,
  definition/impl freeze, and target-proof order; and
- the single private atomic prep-to-final transition that owns both complete
  inputs before validation, publishes no standalone nested final value, and
  closes every failure through RFC 0012 snapshot cleanup.

The proposal may re-enter exact-hash formal review. No approval from any prior
REVIEW snapshot survives; all eight required owners must review the new REVIEW
hash.

### 2026-07-17 Per-Node Preparatory Context Locked Audit Return And Repair

One locked audit approved exact DRAFT snapshot
`a2a4605f338ce50bd11006e44a5452a580cb6f1d3073f3707f2dcdcfbf400ee4`,
but an independent locked audit returned it because the guide and flow placed a
single preparatory context and host proof outside the repeated build-plan node
loop. That presentation permitted reuse across the complete DAG despite the
normative per-node context rule. No locked approval carries forward.

The repair moves context creation and freeze, generic-authority consumption,
same-brand host verification, execution, and result retention into one repeated
per-node loop. Each plan node receives a distinct context brand, authority, and
host proof; the latter two are single-node capabilities. Acceptance, unit, and
architecture gates reject cross-node reuse. The exact repaired DRAFT proposal
SHA-256 is
`ba82d4060ff9c075fa17327de85256879c00790266c3e7bdbc6d6d2065b1987b`.

### 2026-07-17 Node-Key Linear Transition Locked Audit Return And Repair

The module-system locked audit returned exact DRAFT snapshot
`ba82d4060ff9c075fa17327de85256879c00790266c3e7bdbc6d6d2065b1987b`.
The guide required per-node contexts, but the normative transition retained one
global current node and phase without a key-indexed one-shot state machine. It
did not make host verification consume the exact node-bound authority, did not
make execution consume the resulting node-bound proof, and did not close every
cross-node, reuse, or phase mismatch before selection through RFC 0012 cleanup.
No locked approval carries forward.

The repair gives every exact plan key one immutable
`Pending | Authorized | Executed` entry. `verifyHostSelection` consumes the
authority carrying that exact `BuildPlanNodeKey` for the sole
`Pending -> Authorized` edge; execution consumes the proof carrying the same key
for the sole `Authorized -> Executed` edge. Cross-node, reused, skipped,
repeated, stale, closed-state, or otherwise phase-inconsistent calls return
pre-selection `InputRevisionMismatch`, irreversibly close the transition, and
call RFC 0012 `finish()` on every retained snapshot. `SnapshotCleanupFailed`
overrides the initiating failure. The normative API, guide, Mermaid flow,
session order, failure precedence, acceptance criteria, architecture gates, and
tests state the same contract. RFC 0016 remains `DRAFT`; this repair starts no
locked audit. The exact repaired proposal SHA-256 is
`963e70acdb13a3b702e8bbf87936ef6ecb34e3ee05d2c349d868e7cbe4b98753`.

### 2026-07-17 Build-DAG And Post-Freeze Formal Review Return And Repair

The module-system owner returned exact REVIEW snapshot
`72acabf30be04821880320991f6356b49721d215a2e8c8e963fb2e74f700e178`.
Waiting for a complete untrusted result map before verification made RFC 0012
multi-node build-plan execution impossible because each dependent execution
key requires already-verified predecessor output keys. The same snapshot also
allowed final wrapper methods to accept the final semantic authority that
exists before definition and impl freeze. Every approval on that snapshot is
void.

The repair adds one private transition state that consumes the complete
preparation wrapper before any execution, verifies and retains each predecessor
result internally, exposes no intermediate authority, and publishes only after
complete key-set proof. It also makes definition and impl freeze consume the
pre-freeze final semantic authority and return a distinct
`FrozenFinalIssuanceAuthority`; final wrapper methods accept only that
post-freeze capability. The exact repaired DRAFT proposal SHA-256 is
`a2a4605f338ce50bd11006e44a5452a580cb6f1d3073f3707f2dcdcfbf400ee4`.

### 2026-07-17 Formal Review Re-entry After Complete Phase-Flow Approval

The metadata-only transition produced exact REVIEW proposal SHA-256
`72acabf30be04821880320991f6356b49721d215a2e8c8e963fb2e74f700e178`.
The normative design is byte-identical to approved DRAFT snapshot
`58e15302eb30355f040987e24746195334c6964e4f9b29a74d6858277aca715f`
apart from the status transition and its status-history entry. All eight
required owners must review this exact REVIEW snapshot. Any return voids every
approval on it and restores the proposal to `DRAFT` before repair.

### 2026-07-17 Formal Review Re-entry After Atomic-Handoff Approval

The metadata-only transition produced exact REVIEW proposal SHA-256
`1f6c75cc50c828aa7801c0826a5ff0d429a8ce80ca3f30d82fa42241a7ff3b63`.
The normative design is byte-identical to approved DRAFT snapshot
`6962c02c117371218925129b20f3cfffb48d8c5daa7d605db3ec84fb48766d04`
apart from the status transition and its status-history entry. All eight
required owners must review this exact REVIEW snapshot. Any return voids every
approval on it and restores the proposal to `DRAFT` before repair.

### 2026-07-17 RFC 0012 Overlay-Scope Formal Review Return And Repair

The RFC owner returned exact REVIEW snapshot
`1f6c75cc50c828aa7801c0826a5ff0d429a8ce80ca3f30d82fa42241a7ff3b63`
because the bound-proposal table named only registered target selection,
request ownership, and panic mapping for RFC 0012 while the normative design
also overlaid final-handoff ownership and cleanup. Every approval on that
snapshot is void.

The repair explicitly names build-result validation ownership,
preparation-to-final transition timing, final-record publication, and
retained-snapshot cleanup in the RFC 0012 row and in the normative overlay
authority. It also rewrites the stale decision-record sentence about the
already-completed preceding audit cycle as historical fact. The exact repaired
DRAFT proposal SHA-256 is
`b0e02f63daaacb4ce39224ba22a51e544a23ea369d5ff683fe9d79a77c8f9849`.

### 2026-07-17 Guide-Level Phase-Authority Locked Audit Return And Repair

One locked audit approved exact DRAFT snapshot
`b0e02f63daaacb4ce39224ba22a51e544a23ea369d5ff683fe9d79a77c8f9849`,
but an independent locked audit returned it because the guide prose and
Mermaid flow still showed a generic frozen context authority reaching final
verification directly. That contradicted the normative private wrapper API,
which requires unrelated preparatory and final authority types and
wrapper-selected exact request fields. No locked approval carries to the
repaired snapshot.

The repair shows the preparatory freeze and authority-consumption path, the
preparation wrapper's exact host selection, the private atomic final
transition, and the unrelated final freeze and authority-consumption path. It
contains no caller-selected registered selection and no generic authority
accepted by a target-bound wrapper. The exact repaired DRAFT proposal SHA-256
is
`ff480f776780861a9174cb63185c2a514895bc7ef33e34cca753266ccc67874a`.

### 2026-07-17 Final-Freeze Guide Locked Audit Return And Repair

A locked audit returned exact DRAFT snapshot
`ff480f776780861a9174cb63185c2a514895bc7ef33e34cca753266ccc67874a`
because the guide and flow reached final host and target verification directly
after constructing the final phase authority. They omitted the normative final
definition and impl identity freeze that must complete before any final target
proof is issued.

The repair inserts definition and impl identity freeze after final phase-
authority construction and before either final wrapper method. The guide and
flow also state that each wrapper first checks exact same-wrapper brand equality
before selecting its own request field. The exact repaired DRAFT proposal
SHA-256 is
`58e15302eb30355f040987e24746195334c6964e4f9b29a74d6858277aca715f`.

### 2026-07-17 Complete Phase-Flow Locked Draft Approval

Three independent locked audits approved exact DRAFT snapshot
`58e15302eb30355f040987e24746195334c6964e4f9b29a74d6858277aca715f`.
They independently reproduced all five bound RFC hashes and `check-rfc.py`
16/16, and approved the complete preparatory and final phase-authority flow,
the final definition and impl freeze order, exact same-wrapper brand first
checks, wrapper-selected request fields, the private atomic transition and
cleanup, the declared RFC 0012 overlay boundary, and all failure precedence.

The proposal may re-enter exact-hash formal review. No approval from any prior
REVIEW snapshot survives; all eight required owners must review the new REVIEW
hash.

### 2026-07-17 Key-Bound Transition Locked Approval And Formal Review Re-entry

Three independent locked audits approved exact DRAFT snapshot
`963e70acdb13a3b702e8bbf87936ef6ecb34e3ee05d2c349d868e7cbe4b98753`.
Each audit independently reproduced all five bound RFC hashes, all seven
canonical vector lengths and SHA-256 digests, and `check-rfc.py` 16/16. They
approved the exact `BuildPlanNodeKey`-bound consuming authority and proof,
`Pending -> Authorized -> Executed` one-shot state machine, internally retained
executed predecessor results, post-definition/impl-freeze final issuance,
complete mismatch and cleanup precedence, runtime capability mapping, LLVM
22.1.8 admission and provenance, and single-owner routing census.

The metadata-only transition changed the frontmatter status and appended one
status-history row. It produced exact REVIEW proposal SHA-256
`1dc2d86b82db95aaf71796bfba7ef2ae95c3e97edfcd092f3d1a75d608e9f79b`.
All eight required owners must review this exact snapshot. Any return voids
every approval on it and restores the proposal to `DRAFT` before repair.

### 2026-07-17 Runtime Capability Ownership Formal Review Return And Repair

The runtime-memory owner returned exact REVIEW snapshot
`1dc2d86b82db95aaf71796bfba7ef2ae95c3e97edfcd092f3d1a75d608e9f79b`.
Its generated two-argument runtime query was callable without the verified
runtime snapshot, and the mutable runtime capability factory lacked an explicit
process-root owner, injection boundary, and prohibition on singleton or static
issuers. Every approval on that REVIEW snapshot is void.

The repaired draft introduces a closed `RuntimeAbiProfileId`, limits the
definition-file-derived oracle to private factory construction, exposes later
capability lookup only through the exact verified snapshot, and makes one
explicit `CompilerProcessAuthorityRoot` the sole by-value non-static owner and
injector of both authority factories. The exact repaired DRAFT proposal
SHA-256 is
`d10f39f72fd44455ad750fa393a78bfb7e0492f6e792f3794dc9922e2a6ca12b`.

### 2026-07-17 Typed Runtime ABI Association Locked Audit Return And Repair

The first fresh locked audit returned exact DRAFT snapshot
`d10f39f72fd44455ad750fa393a78bfb7e0492f6e792f3794dc9922e2a6ca12b`.
Although admission alone mapped the raw runtime ABI name to the closed typed
ID, the target registry retained no target-to-typed-ID association. Selection
would therefore have needed an illegal raw-name remap or an unspecified side
table. No locked approval carries forward.

The repair retains one independently reconstructed, sorted unique, complete
`TargetSpecId -> RuntimeAbiProfileId` association inside the registry snapshot,
keeps it outside existing codec domains, makes admission reject every malformed
association, and requires selection to consume the retained typed ID directly.
Mutation and architecture gates reject missing, additional, duplicate, swapped,
wrong-target, wrong-profile, and selection-time remapping paths. The exact
repaired DRAFT proposal SHA-256 is
`2d9e22738f4b57550a8fb16597c4f5b5da0247c7e7cf8553f1100d0b5c83604a`.

### 2026-07-17 Typed Runtime ABI Association Locked Audit Approval

Three independent locked audits approved exact DRAFT proposal SHA-256
`2d9e22738f4b57550a8fb16597c4f5b5da0247c7e7cf8553f1100d0b5c83604a`.
Each audit reproduced all five bound-RFC hashes, all seven normative codec
vectors, the complete typed runtime-ABI association closure, process-root
factory ownership, snapshot-bound runtime capability queries, phase and DAG
authority, cleanup precedence, LLVM 22.1.8 component and backend inventory,
and the 16/16 RFC gate. No audit changed repository files. The proposal then
made the metadata-only transition to `REVIEW`; the exact REVIEW SHA-256 is
`713bc7ce30df9e99a537cc94f87e646df4ef18fa1d96d02e90724c46cf90d7c0`.

### 2026-07-17 Coverage Gate Formal Review Return And Repair

The task-router owner returned exact REVIEW proposal SHA-256
`713bc7ce30df9e99a537cc94f87e646df4ef18fa1d96d02e90724c46cf90d7c0`.
The implementation created compiler sources under the RFC impact census, but
acceptance criterion 21, the test plan, and Repository Impact did not require
the verification owner's 70% per-file line-coverage threshold or a complete
exemption, aggregate baseline non-regression, or a repository-owned coverage
report. Every approval on that snapshot is void.

The repaired DRAFT makes the exact changed-compiler-source census part of the
coverage gate, requires at least 70% line coverage per inventoried file, closes
exemptions over exact uncovered lines, substitute evidence, owner approval, and
expiry, rejects aggregate regression against a same-toolchain and
same-test-set baseline, and assigns the checker script and evidence report to
the verification owner. The exact repaired DRAFT SHA-256 is
`da891985b88615911d221a93d7f146239b69314ed1da7a1dbab1ba68ff0b32e2`.

### 2026-07-17 Closed Coverage Contract Locked Audit Return And Repair

The first fresh locked audit returned exact DRAFT proposal SHA-256
`da891985b88615911d221a93d7f146239b69314ed1da7a1dbab1ba68ff0b32e2`.
The census and aggregate denominator still depended on implementation
judgment, the checker accepted no baseline LCOV or frozen test-set identity,
the exemption and report had no closed machine schema or exhaustive mutation
matrix, and the new checker path had no owner in the live routing manifest.
No locked approval carries forward.

The repair fixes the baseline to the future RFC acceptance commit, defines the
two-detached-worktree and dual-LCOV procedure, commits a hash-bound closed test
manifest and empty exemption file, defines exact Git census, LCOV arithmetic,
aggregate membership, JSON exemption and report schemas, mechanical expiry,
and the complete positive and negative self-test matrix. It assigns the runner
and checker consistently to `verification` in the manifest, manual matrix,
owner prompt, AGENTS summary, and Repository Impact. The exact repaired DRAFT
proposal SHA-256 is
`189323da32eab5964f7e186000b6a1ba6c4dc2fec37a6686c8ec0e1716425af2`.

### 2026-07-17 Executable Coverage Contract Locked Audit Return And Repair

The first locked audit returned exact DRAFT proposal SHA-256
`189323da32eab5964f7e186000b6a1ba6c4dc2fec37a6686c8ec0e1716425af2`.
The frozen baseline could only call a fail-soft CMake target that reran CTest
and selected ambient LLVM tools, the exemption approval embedded its own
unconstructible commit hash, deletions had no census command, the JSON evidence
and digest preimages were conceptual rather than closed, and the acceptance
commit allowed non-governance paths. The other in-flight audits were stopped;
no locked approval carries forward.

The repair makes pre-acceptance coverage plumbing an explicit governance
prerequisite, limits CMake to collision-free instrumentation and a deterministic
object manifest, and assigns the one fail-hard CTest/JUnit plus explicit
`llvm-profdata`/`llvm-cov` execution to the external runner. It defines the exact
metadata-only acceptance raw diff, complete NUL-delimited `ACDMRT` census,
cross-boundary copy/rename algebra, RFC 8785 JSON encoding, exact file/value/
stream digest preimages, all environment/test/report fields, and a constructible
two-commit approval artifact keyed by the exemption request digest. Coverage
CMake plumbing now has one verification owner. The exact repaired DRAFT
proposal SHA-256 is
`a007fe9ee8bbc300368f09aff429b8d5a4ca65b419e17d75f35753f4be1b1cf3`.

### 2026-07-17 Raw Coverage Evidence Locked Audit Return And Repair

Two fresh locked audits returned exact DRAFT proposal SHA-256
`a007fe9ee8bbc300368f09aff429b8d5a4ca65b419e17d75f35753f4be1b1cf3`.
The checker trusted parsed JUnit and coverage summaries without receiving the
same-pass raw JUnit, profiles, profdata, objects, LCOV, or actual build commands;
Git and repository configuration were ambient; exact copies and renames could
cross the coverage boundary; and source hashes and deletion counts were not
normatively derived from Git blobs and LCOV rows. The third in-flight audit was
stopped and no locked approval carries forward.

The repair adds explicit, hash-bound Git configuration and tool provenance, a
closed raw-artifact schema, same-pass JUnit reparsing, independent profdata and
LCOV reproduction, exact Ninja command capture and required compile/link flag
verification, and complete raw evidence digests in the final report. It rejects
every copy or rename with exactly one eligible endpoint and derives exemption,
current, and deletion source hashes plus deletion counts from exact Git blobs
and LCOV rows. The exact repaired DRAFT proposal SHA-256 is
`ae911a14826c7c6e910be5d577c12baf8cedf85307b6c6664bbb059817cc6986`.

### 2026-07-17 Hermetic Execution And CMake Model Locked Audit Return And Repair

The next locked audit returned exact DRAFT proposal SHA-256
`ae911a14826c7c6e910be5d577c12baf8cedf85307b6c6664bbb059817cc6986`.
The runner did not close process working directories or the inherited
environment; baseline and current could use different effective preset and
build configuration; the proposed Ninja transitive-command grammar could not
represent archive steps or custom commands containing shell control operators;
Git includes, replacement objects, alternates, grafts, shallow history, and
finite rename/copy detection remained open; compiled source was not linked
one-to-one to its Git blob, compilation record, object graph, and LCOV `SF`;
and compiler resource directories, the SDK/sysroot, LLVM CMake files, program
interpreters, and dynamic-library closure were not bound. No locked approval
carries forward.

The repaired DRAFT runs from a sealed `--bare --no-local` Git clone with one
ref, minimal configuration, no replacement or alternate object authority, and
unlimited `-l0` rename/copy detection. Every subprocess receives an exact new
environment and prescribed cwd. The committed test set uses explicit
configure, build, and test command objects with no presets, and baseline and
current must have byte-identical effective CMake configuration. Raw CMake
cache, File API codemodel-v2, compilation database, response files, combined
and per-object LCOV, compiler resource trees, SDK tree, LLVM CMake tree,
interpreters, and transitive dynamic dependencies are hash-bound and
independently reproduced. The checker proves each compiler LCOV source against
one exact compilation record and Git blob and connects its coverage executables
through the File API target graph. The exact repaired DRAFT proposal SHA-256 is
`58c6e6f7d77b38421ac96f8d96e655b2501ef796f2fcc78e81e8ce25ba64e274`.

### 2026-07-17 Execution Authority And Object Membership Locked Audit Return And Repair

Locked audit number one returned exact DRAFT proposal SHA-256
`58c6e6f7d77b38421ac96f8d96e655b2501ef796f2fcc78e81e8ce25ba64e274`.
The macOS dependency contract required absent dyld shared-cache images to be
regular files; sealed-Git ingress rejected ordinary safe remote, branch, user,
and tool configuration and contradicted its own local clone; JUnit bound only
test names and results while live CTest and lit used unbound absolute and bare
Python plus child executables; and compilation records named object paths
without binding object bytes or membership in a final executable. No locked
approval carries forward.

The repaired DRAFT treats a missing macOS load path as legal only when the
explicit `dyld_info` authority independently reproduces every architecture
image UUID and dependent closure from the dyld shared cache. It records and
rejects the dangerous invoking-repository config projection, permits exactly
one local ingress clone, proves raw object equality, and forbids remote access
after sealing while accepting ordinary non-semantic config. A private tool farm
exposes only hash-bound Python, shell, and tool names. Raw CTest JSON binds each
test's complete definition, and `eslogger` or `strace` raw process evidence
binds the complete normalized interpreter, script, and descendant executable
closure with retained-test equality. Coverage objects now carry exact size and
SHA-256; raw archive members and platform link maps independently prove each
contributing object reaches a hashed coverage executable. The exact repaired
DRAFT proposal SHA-256 is
`3f6a4e1a831454faacf12fb7022b8c24b71dc1b4cf784e41fcc6e18b6320823d`.

### 2026-07-17 Hermetic Phase And Semantic Identity Locked Audit Return And Repair

Locked audit A returned exact DRAFT proposal SHA-256
`3f6a4e1a831454faacf12fb7022b8c24b71dc1b4cf784e41fcc6e18b6320823d`.
It found four blockers: raw stdout/stderr stream digests participated in the
baseline/current environment identity; retained tests required identical
executable SHA-256 values instead of identical definitions, selection, and
semantic roles; only CTest descendants were traced while configure/build
`execute_process`, compiler children, Ninja custom commands, absolute tools,
and their input authority remained open; and macOS dynamic closure did not
define per-loader resolution for `@rpath`, `@loader_path`,
`@executable_path`, and ordered `LC_RPATH`, including Homebrew LLVM. No locked
approval carries forward.

The repaired DRAFT keeps every raw stream, executable, script, dynamic image,
argv, cwd, environment, and input byte in independent per-run evidence while
restricting cross-run environment equality to parsed facts and same-test-set
equality to selection, complete CTest definitions, and semantic execution-role
graphs. Configure, build, and test now each run under a closed sandbox and
loss-detecting process/input trace that classifies every executable and read
against Git, tool, SDK, compiler-resource, LLVM, dynamic, platform, or unique
generated authority. The macOS dependency algorithm parses architecture-
specific load commands and each loader's ordered run-path stack, records every
candidate and selected filesystem or dyld-cache image, and applies without a
Homebrew-specific shortcut. The exact repaired DRAFT proposal SHA-256 is
`a1f8cc3e8b5d01e817604abe86cb08ddd835d7d00216d716670c3b89983dd43c`.

### 2026-07-17 Filesystem Event And Platform Loader Locked Audit Return And Repair

Locked audit A returned exact DRAFT proposal SHA-256
`a1f8cc3e8b5d01e817604abe86cb08ddd835d7d00216d716670c3b89983dd43c`.
It found four blockers. The process trace did not observe `stat`, `access`,
directory enumeration, `readlink`, `mmap`, write, create, truncate, rename,
link, unlink, or platform-equivalent operations and therefore could not prove
unclassified-input rejection, one producer, final path identity, or
write-after-read. The Mach-O run-path stack traversed root-to-loader instead
of dyld's loader-to-root linked stack. Linux did not define glibc
`DT_RPATH`/`DT_RUNPATH`, dynamic-token, `ld.so.cache`, hwcaps, `NODEFLIB`,
multiarch, or default-directory behavior. Prior Art did not compare the
coverage contract with mature hermetic build, process provenance,
reproducible-build, supply-chain attestation, or dual-revision coverage
systems. The other in-flight audit was stopped and no approval carries
forward.

The repaired DRAFT defines a closed cross-platform event algebra with exact
Linux syscall and macOS Endpoint Security mappings, negative path and
directory inputs, object-version replay, unique producer and sealing rules,
complete path histories, final-path proof, and write-after-read rejection.
It follows dyld 1122's loader-to-root `LoadChain` traversal while preserving
each image's Mach-O load-command order. It adds a closed glibc 2.39 model bound
to the exact interpreter, package receipt, diagnostics, auxiliary vector,
cache, hwcaps, multiarch/default directories, token expansions, RPATH/RUNPATH
transitivity, `NODEFLIB`, candidate order, and independent `--list`
corroboration, rejecting every other loader model. Prior Art now records the
adopted and rejected parts of Bazel, Nix, in-toto, SLSA, Reproducible Builds,
LLVM source coverage, and `diff-cover`. The exact repaired DRAFT proposal
SHA-256 is
`f1a24c14b3cbea3daf12616ece9dcb90c39e304229cffc9fad7674124f600a89`.

### 2026-07-17 Outcome, Mapping, Host Input, And Header Coverage Return And Repair

Locked review returned exact DRAFT proposal SHA-256
`f1a24c14b3cbea3daf12616ece9dcb90c39e304229cffc9fad7674124f600a89`.
It found four blockers. Failed open, `stat`, `access`, create, rename, unlink,
and related events still required success-only descriptor, object, metadata, or
replacement fields. Process and mapping evidence did not close clone flags,
thread groups, `CLONE_FILES`, `CLONE_VM`, inheritance, stable mapping identity,
remap/protect/sync/unmap, private/shared behavior, fork, sealing, or mapping
write-after-read. Both platform contracts left clock, time, random, `uname`,
host identity, `sysctl`, CPU, and system-capability observations ambient.
Finally, the LCOV relation incorrectly required headers to own one compilation
record instead of proving their one-to-many contributing translation-unit and
object closure. No locked approval carries forward.

The repaired DRAFT replaces fallible event records with a closed discriminated
`TraceOutcome`; failed outcomes retain only their exact request and platform
error and cannot mutate replay state or fabricate success facts. It defines
complete clone/thread/descriptor sharing, address-space and stable mapping-key
versions, private/shared mapping and mutation-lease semantics, inheritance,
remap/protect/sync/unmap, exit, seal, read-before-seal, and write-after-read,
with positive and negative startup fixtures.

One hash-bound deterministic host-input broker now mediates or rejects every
Linux and macOS clock, time, entropy, auxiliary-vector, vDSO/commpage, `uname`,
host identity, CPU, affinity, resource-limit, `sysctl`, Mach, and system-
capability path. Raw observations remain per-run evidence, while the broker
policy and canonical capability authority must be byte-equal across baseline
and current. Coverage source evidence now assigns every compiler `.cc` source
one primary compilation record and gives headers and other included sources a
complete deduplicated one-to-many translation-unit/object, build-trace,
Git-blob, link-membership, and per-object-LCOV contribution set. Included
sources cannot enter the `.cc` census or aggregate.

RFC 0016 remains `DRAFT`; this repair records no approval and authorizes no
implementation. The exact repaired proposal SHA-256 submitted for fresh locked
review is
`2b9e3729ee102993d2537dba9368e36a118902b8ce88729a87fa6ad05fc1b83e`.

### 2026-07-17 Close, Controlled Execution, Descriptor, And Object Coverage Return And Repair

Fresh locked audit returned exact DRAFT proposal SHA-256
`2b9e3729ee102993d2537dba9368e36a118902b8ce88729a87fa6ad05fc1b83e`.
The ordinary failed-outcome rule was unsound for `close`: Linux late errors
consume the fd, while the Darwin contract could not prove whether an
error-preserving close completed. Several named event, request, success,
failure, and platform mappings were still placeholders. The broker relied on
no implementable mechanism for vDSO, auxiliary-vector, commpage, Mach, direct
CPU, or user-mode library reads. Descriptor creation and mutation omitted
exact pipe, socketpair, duplicate, close-range, and fcntl partitions; `vfork`
had no enforceable shared-page mechanism. Header LCOV still used target-level
inference instead of an exact compilation-object export and could collapse
duplicate reads or claim no exact final target. No locked approval carries
forward.

The repaired DRAFT introduces the dedicated three-state `FdCloseOutcome`.
Linux zero closes, `EBADF` preserves, and every other Linux errno closes with a
diagnostic; the Darwin conformance normalizer treats non-`EBADF` errors as
`StateUnknown` and rejects before another instruction. It fully defines the
trace scalar, descriptor, mapping, host-request, success, failure, and event
algebras plus the exhaustive fixed-Linux raw-operation table.

Positive coverage now runs only in a hash-bound `ubuntu-24.04-x86_64` KVM
microVM with a fixed guest kernel, vDSO disabled, deterministic auxiliary
vector and capability snapshot, trapped CPUID/TSC, no entropy or wall-clock
device, immutable executable mappings, and a seccomp/ptrace lifecycle monitor.
Darwin emits an exact unsupported-platform oracle before creating a worktree,
guest, or child. The Linux fd model closes pipe/pipe2/socketpair flags, all
duplicate variants, close-range modes, access and descriptor/status flags, and
their failures. The then-current `vfork` design suspended the parent and used
a separate monitor protocol to retain shared-memory effects.

Each coverage-mapped compilation object is passed as the sole `llvm-cov`
`BIN`. Translation-unit LCOV, exact compile execution, Git-blob reads, object
bytes, archive/link-map membership, and a sorted nonempty subset of exact final
targets form one contribution. Repeated observations retain their minimum
sequence and distinct count. The test matrix now includes positive and
negative close, descriptor, vfork, controlled-execution, Darwin-preflight,
object-attribution, duplicate-read, and one-to-many header oracles.

RFC 0016 remains `DRAFT`, `approvers` remains empty, and `Decision` remains
`TBD`. The exact repaired proposal SHA-256 submitted for fresh locked review is
`e7c88f755ceff08d90cbbca20d68de2d03d388170ceb76e226a5666d6d755fbb`.

### 2026-07-17 Endpoint, Syscall, Controlled-Execution, And Codec Return And Repair

Fresh exact-hash review returned DRAFT proposal SHA-256
`e7c88f755ceff08d90cbbca20d68de2d03d388170ceb76e226a5666d6d755fbb`.
Pipe and Unix-stream descriptors had no byte-stream state, so reads, writes,
partial results, EOF, and zero-copy source/destination authority could not be
replayed. The raw table omitted executable calls required by the declared
CMake/Ninja/CTest/Python/glibc process trees and collapsed distinct time and
resource queries into an open host result. The controlled executor depended on
per-instruction CPUID/exception exits that KVM does not provide and did not
define ownership and closure of the then-current vfork memory mechanism.
Finally, normalized-event hashing had no canonical scalar, record, union,
arbitrary-byte, signed-integer, field-order, or stream encoding. No locked
approval carries forward.

The repaired DRAFT gives each pipe one versioned FIFO and each socketpair two
directional FIFOs. Scalar and vectored reads/writes carry exact bytes, offsets,
versions, EOF, and no-effect failures. `copy_file_range`, `sendfile`, and
`splice` are one atomic event whose exact byte prefix and source/destination
arms replay partial success or no-effect failure. Poll/select/epoll readiness,
wait/reap, brk, seek, signals, futex, process identity, and other admitted
runtime-local calls now have closed request/result algebras.

The executable syscall table is an exhaustive default-deny partition for the
hash-bound tool images. Every clock/time/resource/CPU/filesystem request has an
exact matching host-value variant and raw ABI re-encoding rule. The executor
uses pre-run `KVM_SET_CPUID2`; CPUID does not claim an exit. A hashed guest
#GP/#UD handler emulates deterministic TSC and rejects direct entropy and other
host-observing instructions. Seccomp `RET_TRACE` plus ptrace supplies real
entry/exit stops. The repaired draft specified an initial monitor-owned vfork
memory-evidence protocol. That protocol is historical only and is absent from
the current RFC.

The normalized trace now uses a complete version-one big-endian binary codec
for every scalar, record, sequence, optional, enum, union, outcome, and all 39
event tags. It publishes scalar and failure vectors plus a 60-byte stream whose
SHA-256 is independently reproducible, retains each normalized binary stream,
and requires an exhaustive per-type/per-variant oracle and mutation corpus.

RFC 0016 remains `DRAFT`, `approvers` remains empty, and `Decision` remains
`TBD`. The exact repaired proposal SHA-256 submitted for fresh locked review is
`de756f20a27420853d7fdeccf2f71492837deb95a4bd8c1d88314897b4ed4d6f`.

### 2026-07-17 Vfork, Endpoint, Request, Ptrace, And Evidence Return And Repair

Fresh exact-hash review returned DRAFT proposal SHA-256
`de756f20a27420853d7fdeccf2f71492837deb95a4bd8c1d88314897b4ed4d6f`.
Clone and clone3 requests carrying `CLONE_VFORK` still entered the ordinary
`CLONE_VM` branch, so the fixed glibc 2.39 `posix_spawn` implementation escaped
the declared vfork safety mechanism. Endpoint FIFOs had bytes and versions but
no capacity, occupancy, writable low-water, or deterministic partial-write and
readiness authority. `openat2.resolve`, stat flags/masks, hard-link source
versions, typed metadata values, and exhaustive CPU/auxiliary/sysconf pairings
were not retained. The ptrace prose also assumed every seccomp event preceded
an entry/exit pair, contrary to Linux 4.8 ordering. Finally, repository-owned
reports were required to record exact current HEAD while being committed into
that same HEAD, an impossible self-reference. No locked approval carries
forward.

The repaired DRAFT classifies `CLONE_VFORK` before `CLONE_VM`. Raw `vfork`, the
glibc clone fallback, and clone3 with `CLONE_CLEAR_SIGHAND` and optional pidfd
all entered one then-current vfork path that preserved raw and effective stack,
flag, signal, and result inputs and tracked parent-visible changes including
glibc `args.err`, proves clean parent bytes unchanged, and suspends the
parent through exec/exit. Unsupported cgroup-sharing forms are denied with the
already isolated microVM and ordinary `posix_spawn` as the declared-tool
alternative.

Each endpoint channel now carries fixed capacity, exact occupancy, atomic-write
limit, writable low-water, and complete before/after state. Broker rules derive
`EAGAIN`, partial writes, `FIONREAD`, `F_GETPIPE_SZ`, poll/select/epoll output,
and zero-copy counts without ambient kernel queue state. Typed open, stat,
link, and metadata requests preserve every behavior-changing field; a closed
table pairs every CPU, auxiliary-vector, and sysconf key with one value variant
and exact snapshot value. The binary codec and mutation corpus include all new
records and event tag 40.

The ptrace monitor now uses `PTRACE_CONT` from idle, treats
`PTRACE_EVENT_SECCOMP` as the one entry-equivalent stop, assigns explicit
global/thread call and stop ordinals, and requires one paired syscall-exit after
the `PTRACE_SYSCALL` continuation chain except for the exact sigreturn and
process-exit alternatives. Successful exec still requires its exit after the
exec event.

Coverage JSON and Markdown now live only below the external output root. A
canonical evidence manifest binds both report preimages to source commit `S`,
`S^{tree}`, the workflow revision/run, and the complete evidence root; CI
uploads and attests it. Any merge or source/tool/workflow change reruns the
entire gate. The later `LANDED` transition is one status-only commit whose
parent is the attested `S`, so no report ever contains its own commit identity.

RFC 0016 remains `DRAFT`, `approvers` remains empty, and `Decision` remains
`TBD`. The exact repaired proposal SHA-256 submitted for fresh locked review is
`5e3cf506d4d213e85539898d26ff212c69255079a8fc070395f9c2f6e9c1ae33`.

### 2026-07-17 Ordering, Clone3, Thread Exit, Ptrace, And ABI Return And Repair

Two independent exact-hash reviews returned DRAFT proposal SHA-256
`5e3cf506d4d213e85539898d26ff212c69255079a8fc070395f9c2f6e9c1ae33`.
The bound RFC 0006 bytes no longer matched the recorded hash. A single call
ordinal and final outcome could not preserve blocking entry versus completion
order, and holding every address-space peer until a blocked result sealed could
deadlock the peer that made the result possible. Clone3 omitted structure size,
set-TID array, cgroup, and extension bytes. Execveat and at-family requests lost
operation, directory fd, raw path, or flags. Getdents/readlink output ABIs,
thread-only versus group exit, last-thread promotion, clear-child-TID/futex,
descriptor/mapping lifetime, wait/SIGCHLD, CLONE_THREAD/FS/SYSVSEM, and parent/
child TID lifecycles were incomplete. The ptrace machine did not close signal
delivery, suppression/reinjection, group stops, restart chains, interrupt-stop
classification, or fixed-kernel/UAPI capability preflight. No locked approval
carries forward.

The repaired DRAFT binds the exact current RFC 0006 snapshot. Every normalized
operation carries entry and completion ordinals; every blocking-capable event
carries an immediate or parked disposition backed by a structural FIFO wait
queue, ticket, park, wake, and reason. Peer suspension is limited to bounded
entry and completion snapshots. Running peers are interrupted with
`PTRACE_INTERRUPT`, must report the matching interrupt-created
`PTRACE_EVENT_STOP`, are restored to their prior state immediately after the
snapshot, and remain runnable while only the caller is parked.

Clone3 now retains every supplied ABI version field, structure size, set-TID
pointer/count/values, cgroup, and extension byte. Clone success distinguishes a
new process from a new thread and binds address-space, descriptor, filesystem,
System V semaphore, signal-disposition, TLS, pidfd, parent/child TID, and clear-
child-TID ownership. Exit normalization distinguishes thread exit, last-thread
promotion, exit-group, fatal group death, per-thread clear-child-TID/futex, and
last-owner descriptor/mapping retirement, then links the exact wait and SIGCHLD
transition.

Typed requests now retain execve/execveat and every admitted at-family ABI
identity. Directory reads retain getdents versus getdents64, count, raw record
bytes and lengths, inode/type/name, signed `d_off`, and cursor transitions.
Symlink reads retain readlink versus readlinkat, base, raw path, buffer size,
returned prefix, full length, and truncation. The ptrace state machine now
normalizes signal transitions, group-stop/listen/continue, all restart classes,
and bounded peer-interrupt stops. A fixed guest-kernel/UAPI authority plus
positive and negative probes covers every required `PTRACE_GET_SYSCALL_INFO`
form, seccomp ordering, successful-exec exit, signal decision, group stop,
restart, and interrupt stop; mismatch emits one typed unsupported oracle before
manifest execution.

RFC 0016 remains `DRAFT`, `approvers` remains empty, and `Decision` remains
`TBD`. The exact repaired proposal SHA-256 submitted for fresh locked review is
`769593d09c3e0f2eaf9c1d89387fb199fb2c96a9a50a7d6a9512015fd30b41d5`.

### 2026-07-17 Operation Progress, Broker, Lifecycle, And ABI Return And Repair

A fresh exact-hash review returned DRAFT proposal SHA-256
`769593d09c3e0f2eaf9c1d89387fb199fb2c96a9a50a7d6a9512015fd30b41d5`.
Entry and completion ordinals did not encode intermediate state, so partial
blocking pipe commits and futex requeue ticket migration could not be replayed
without contradictory queue rules. The ptrace design relied on synthetic exit
parking and fabricated interruption/restart behavior. Its user-address staging
could be mutated by resumed same-mm peers. Numeric PID/TID identity and reuse,
multithreaded exec de-threading, robust-futex owner death, SIGCHLD ignore and
no-child-wait policy, stop/continue waits, getdents count narrowing and type
coverage, clone3 partial-size zero-fill, and `CLONE_PARENT` policy were not
closed. No locked approval carries forward.

The repaired DRAFT adds one entry, zero or more typed progress records, and one
completion around every admitted raw operation. Progress records seal queue
enrollment, partial pipe write commits, wake, stable-ticket futex requeue, and
robust owner-death effects; completion binds the exact ordered semantic-event
digest. The queue policy is now strict FIFO except for explicit futex-bitset
ineligibility, typed child/readiness eligibility, targeted cancellation, and
sleep deadlines.

Synthetic ptrace parking is removed. Pointer-bearing or blocking calls use a
hash-bound fixed-kernel broker with kernel-owned immutable requests and bounce
outputs, while clone and exec use a kernel lifecycle input-consumed hook.
Bounded peer snapshots resume before a broker wait. Real guest-kernel EINTR,
restart codes, restart blocks, a pre-handler signal-commit lifecycle stop,
signal delivery, `rt_sigreturn`, group stop, and SIGCONT transitions retain the
same operation key without fabricating a syscall exit, return register, or
restart state.

The process model now carries generation-bearing numeric process, thread,
process-group, and session identities at every ABI boundary. It specifies
zombie identity retention through terminal wait, auto-reap release, stable
pidfd generation binding, the fixed complete thread-sharing bundle,
multithreaded exec sibling destruction and leader-TID reset, robust futex
`OWNER_DIED` writes and wakes, descriptor-table unshare and close-on-exec,
signal-disposition and alternate-stack reset, child terminal/stop/continue
transitions, explicit `SIG_IGN`, `SA_NOCLDWAIT`, and `SA_NOCLDSTOP` behavior,
and exact wait consumption. Getdents records the raw register, narrowed and
`INT_MAX`-bounded count plus all fixed-UAPI `d_type` values. Clone3 records the
supplied prefix and Linux zero-fill through VER2; nonzero extension bytes and
`CLONE_PARENT` are denied. The exact codec oracle is now a 566-byte
entry/semantic/completion stream.

RFC 0016 remains `DRAFT`, `approvers` remains empty, and `Decision` remains
`TBD`. The exact repaired proposal SHA-256 submitted for fresh locked review is
`b4947fd3d9e555659d0b138284e4b7dd12c22ac1a70d6c128160f7489a1bb76c`.

### 2026-07-17 Linux 6.8 Endpoint, Lifecycle, Mapping, And ABI Return And Repair

A fresh exact-hash review returned DRAFT proposal SHA-256
`b4947fd3d9e555659d0b138284e4b7dd12c22ac1a70d6c128160f7489a1bb76c`.
`EndpointWriteSuccess` duplicated mutations already committed by progress
records and did not define replay across interleaved reads. The three custom
ptrace lifecycle stops had no event numbers, wait-status or event-message
codec, ownership boundary, or exact resume requests. Kernel bounce output did
not bind destination addresses to live mapping generations or prevent
unmap/remap/replacement races. Exec omitted Linux `mm_release` clear-child-TID
zero/wake behavior when a non-thread `CLONE_VM` peer retained the mm.

Robust cleanup processed `list_op_pending` in the wrong order, omitted the
non-PI owner-zero wake-without-write case, and treated a wake attempt as proof
that a waiter existed. Parent death did not bind a guest-init/subreaper key or
transfer wait and SIGCHLD ownership. Getdents collapsed unsigned-long legacy
`d_off` and signed `s64 d_off64` into one type and widened U16 `d_reclen`.
Clone3 omitted the `PAGE_SIZE`/`E2BIG` and
`MAX_PID_NS_LEVEL`/`EINVAL` boundaries and did not close controlled set-TID
cardinality. No locked approval carries forward.

The repaired DRAFT binds those rules to the official Linux v6.8 source tag.
Endpoint writes now mutate state only through contiguous
`EndpointWriteCommitted` progress; the final summary is effect-free and binds
the ordered progress digest plus completion-time state. The fixed guest UAPI
defines ptrace events 8, 9, and 10, option bits `0x100`, `0x200`, and `0x400`,
raw wait statuses, a version/kind/48-bit ring-ordinal `GETEVENTMSG` codec,
monitor-only ring ownership, `NONE` syscall-info, and exact
`PTRACE_SYSCALL`/`PTRACE_CONT` resumes.

Every output range now acquires a mapping-version and pinned-page lease from
entry through commit/cancel. Overlapping unmap, remap, and fixed replacement
queue behind it, and completion revalidates address-to-page identity before
copying. Exec records `mm_users`, zero-store and shared-key wake attempts when
the count exceeds one, or disarm-only at one. Robust cleanup snapshots pending,
skips it during list traversal, handles it last, distinguishes owner death,
pending owner-zero wake-without-write, and owner mismatch, and records actual
woken tickets independently of attempts.

The process hierarchy now binds guest init and the phase subreaper by
`ProcessKey`, selects the Linux v6.8 reaper, transfers unconsumed wait authority,
and applies the new parent's SIGCHLD/auto-reap policy to an existing zombie.
Directory results are separate legacy and 64-bit records with exact unsigned or
signed offsets, U16 lengths, raw bytes, and typed cursors. Clone3 has an
undecoded pre-copy rejection arm, exact size validation, decoded namespace
depth validation, and a controlled policy admitting only zero set-TID entries.
The updated process-exit oracle is 595 bytes with semantic-event digest
`5489128ba31d2326305d3b0cf781f479dff89027f6200bbd72075ab49ee050e8`
and stream digest
`d746dc7fdfb2bc1d70f59215d6e31af366da23613cb4bdbd48643cfce3254960`.

RFC 0016 remains `DRAFT`, `approvers` remains empty, and `Decision` remains
`TBD`. The exact repaired proposal SHA-256 submitted for fresh locked review is
`d9ab1538dea9556569a15ceb13993538b283a7daf37907ae4a35342a187bdaf2`.

### 2026-07-17 Linux 6.8 Getdents Count And Padding Return And Repair

An independent exact-hash re-review returned DRAFT proposal SHA-256
`d9ab1538dea9556569a15ceb13993538b283a7daf37907ae4a35342a187bdaf2`
with two blockers. First, `effectiveCount=min(abiCount, INT_MAX)` changed a raw
unsigned ABI argument instead of defining a legal boundary before Linux v6.8
stores it in signed `getdents_callback.count`. Second, the record contract
required alignment padding to be zero even though `fs/readdir.c` writes only
the declared fields, name plus NUL, and legacy final-byte type. No locked
approval carries forward.

The repaired DRAFT does not clamp. Counts through `INT_MAX` are admitted with
exact U31 equality; counts above it produce `ControlledExecutor/PolicyDenied`
before descriptor lookup, directory iteration, destination capture, or lease
acquisition. Count-zero fixtures distinguish the Linux v6.8 nonempty-directory
`EINVAL` result from an empty-directory zero result, both without writes. New
normative request oracles cover admitted `INT_MAX` and denied `0x80000000`,
including raw high-register bits.

Directory success now seals the exact pre-call destination bytes, ordered
mapping-contained destinations, returned length, after bytes, and an MSB-first
kernel write mask. Legacy and 64-bit masks reproduce the precise
`unsafe_put_user` and `unsafe_copy_dirent_name` destinations; every unmasked
alignment byte retains its pre-call value. Output leases write-protect pinned
destination pages and deny same-mm tracee stores until completion, while the
broker commits only masked bytes. Executable mask oracles use one-character
24-byte records with payloads `ff ff f1` and `ff ff f8` and prove prefilled
`a5 5a c3` padding survives.

The 595-byte process-exit oracle is unaffected and still has semantic digest
`5489128ba31d2326305d3b0cf781f479dff89027f6200bbd72075ab49ee050e8`
and stream digest
`d746dc7fdfb2bc1d70f59215d6e31af366da23613cb4bdbd48643cfce3254960`.
The admitted and denied count-request digests are
`a658f8489834acd29718112c5d6fd785f1a6e3d614888be71cf905abfc791fc4`
and `96dcc72734eeb2f3ce7e1b5821ae38225d911d039b8c2351f1d4159c71b4c871`;
the legacy and 64-bit mask digests are
`57ea2c2a532dcd3b4723f18a3f76913e7f24704ed65ef5675475410630189988`
and `6ac511198b2ecddb90647c74b5b747d8e2e3e373e30b50bf8c15842dff1bd7c2`.

RFC 0016 remains `DRAFT`, `approvers` remains empty, and `Decision` remains
`TBD`. The exact repaired proposal SHA-256 submitted for fresh locked review is
`6f49edbb8ef1f5d2e327f5a4f53923eb5945c3d192dc18d0d7a4fd7d7d094777`.

### 2026-07-17 Linux 6.8 MAX_RW_COUNT Return And Repair

An independent exact-hash re-review returned DRAFT proposal SHA-256
`6f49edbb8ef1f5d2e327f5a4f53923eb5945c3d192dc18d0d7a4fd7d7d094777`
because scalar and vectored read/write plus transfer requests retained only one
raw U64 byte count. Endpoint and file effects used that unbounded count
directly, so a broker result could exceed Linux v6.8 `MAX_RW_COUNT`, signed
`ssize_t`, and the U31 endpoint-progress domains. No locked approval carries
forward.

The repaired DRAFT defines `LinuxMaxRwCount=0x7ffff000` from the fixed x86-64
Linux v6.8 `INT_MAX & PAGE_MASK` contract and `LinuxIovMax=1024`. Scalar
requests retain raw U64 count, reproduce raw-range `access_ok`, and derive one
U31 effective count. Vectored requests retain raw and effective length vectors,
reproduce the single-iovec effective-range versus multi-iovec raw-range
validation order, truncate exactly one final element when necessary, and
require a zero effective suffix. Input capture, output leases, file/endpoint
effects, return values, committed bytes, and progress counts use only the
effective prefix and are bounded by U31 and `ssize_t`.

The transfer audit applies Linux's single `MAX_RW_COUNT` reduction to
`sendfile` and policy-denies above-bound `copy_file_range` and `splice` before
descriptor, offset, queue, file, or endpoint effects. The viable alternative
is a sequence of bounded calls. Three exact canonical request oracles cover
scalar `MAX_RW_COUNT`, scalar `MAX_RW_COUNT+1`, and raw iovecs
`[MAX_RW_COUNT-1,2]` reduced to `[MAX_RW_COUNT-1,1]`. Their byte lengths are
43, 43, and 75 and their SHA-256 values are respectively
`868f653f0f1374b4661bcd19a2492c35f708ac7f2de21c9f9f9e989ec9af5aea`,
`94202c2679dc098bf49435669dec7c69f322353128702603b38192c79336b088`,
and
`ca24a8ba3b28ee30cae33674b7c0a6ffceea24d1a2ca114c79a0c4395a49e6af`.
Mutation fixtures alter every raw/effective count, cardinality, shortened
element, zero suffix, effect/result length, transfer boundary, and progress
bound.

The previously closed endpoint-summary, ptrace, same-mm lease,
clear-child-TID, robust-futex, reparenting, getdents, clone3, and 595-byte
process-exit oracle contracts are unchanged. RFC 0016 remains `DRAFT`,
`approvers` remains empty, and `Decision` remains `TBD`. The exact repaired
proposal SHA-256 submitted for fresh locked review is
`38fd6272a0d0cb7a4f83317278ca1586e06f0b1010da0eb815ffb0b6fee16121`.

### 2026-07-17 Linux 6.8 Preadv2/Pwritev2 Minus-One Return And Repair

Independent exact-hash review returned DRAFT proposal SHA-256
`38fd6272a0d0cb7a4f83317278ca1586e06f0b1010da0eb815ffb0b6fee16121`.
The request model made every `pread*`/`pwrite*` operation carry an unsigned
explicit offset. That erased the raw signed `-1` sentinel and incorrectly
classified `preadv2`/`pwritev2` `pos == -1` as positional I/O. Linux v6.8
instead preserves the v2 syscall identity and routes exactly that value through
`do_readv`/`do_writev`, using and advancing a regular file's open-description
offset. Ordinary positional calls with negative offsets and v2 values below
`-1` fail with `EINVAL` before descriptor lookup. No locked approval carries
forward.

The repaired algebra retains `DataIoRawSyscall`, the exact raw U64 offset bits,
and their signed I64 interpretation independently. It derives a nullable closed
`DataIoOffsetMode`: absent only for an invalid negative positional request,
`CurrentOpenDescriptionOffset` for ordinary current-position calls and v2
`-1`, and `ExplicitOffset(value)` for nonnegative positional calls. Exact
bit/value equality is mandatory. A current-position regular-file success emits
and applies the exact before/after open-description offset; an explicit success
records an unchanged open-description offset; a pipe or Unix-stream current
call records `StreamWithoutOffset`. Failed calls emit no result or effect.

The exact oracle bundle contains:

- current and explicit offset modes: 2 and 10 bytes, SHA-256
  `b413f47d13ee2fe6c845b2ee141af81de858df4ec549a58b7970bb96645bc8d2`
  and
  `41077b968c124baa7850033e4caf25386c44be205b21ae5661ff2ac555d6a42e`;
- `Pwritev2(-1)`, ordinary `Pwritev(-1)`, and `Pwritev2(5)` requests: 75,
  73, and 83 bytes, SHA-256
  `33d14fc07e9c77219502338706b20c4fc40130e469cb6a0617bc865ff49e3c48`,
  `b8d9fdaf72dd5779fff4423fcf2b8633aa8a99db5d171d347a9e5370baca1535`,
  and
  `39164550908b610cedb0770480819ef16acb2c61aa8a6717b46e8b14046c0ab6`;
- success result, current offset effect, explicit offset effect, stream offset
  effect, file-write effect, complete file-write success, `EINVAL` outcome,
  and operation entry: 12, 38, 54, 22, 86, 138, 22, and 183 bytes, SHA-256
  `c2dbaa077aa5e5fb36dfdb4f86205fe3a6b47cec7c4992ced3e4077333182bcb`,
  `c29776f6035d9dce9b7c497dc73b96001b11efda2c6674177142a6b3805629e8`,
  `b192a2f9728dd80a84517b585583c077c85998a4507899202ca9866018e9828f`,
  `d5fc64a39bdf246dc0a8c074211406e95e0ef124991b5ba0494ef6de687327aa`,
  `29b359f9cf23b80db7ae49289be807cc352ca5641baf5aab8d47677bb3787269`,
  `543f5f6369ad02842c3ef7120acf08fd306aa89476b4ff599d9b54ae1d8087ea`,
  `47dd8411578f3518bb6dbad101c92e69d97a3c616cf9e0b18730069ff33c974c`,
  and
  `7f7f01fd2f0f1230cddd77fed2804b256d93eb35bd7b9ba2f2f7802f592d50c2`.

Mutation evidence changes raw syscall tags, raw bits, signed values,
nullability, offset modes, entry request bytes and digest, success/failure
route, result count, open-description advancement, explicit-position
non-advancement, stream offset absence, file range/version, and payload. It
specifically rejects rewriting a v2 `-1` operation as `Readv`/`Writev`, giving
ordinary `Preadv`/`Pwritev` the special route, returning success for any other
negative positional offset, or attaching effects to `EINVAL`.

The closed `MAX_RW_COUNT`, transfer, endpoint-summary, ptrace, same-mm lease,
clear-child-TID, robust-futex, reparenting, getdents, clone3, and 595-byte
process-exit families remain enforced. RFC 0016 remains `DRAFT`, `approvers`
remains empty, and `Decision` remains `TBD`. The exact repaired proposal
SHA-256 submitted for fresh locked review is
`0fa25aaca261aa66986ec62b38e75b8f5202323c87c908491d36ff6faf61060c`.

### 2026-07-17 Linux 6.8 O_APPEND Return And Repair

Independent exact-hash review returned DRAFT proposal SHA-256
`0fa25aaca261aa66986ec62b38e75b8f5202323c87c908491d36ff6faf61060c`.
Although that snapshot retained the raw syscall and requested offset mode, its
file-write effect still placed every write at the requested current or
explicit position. Linux v6.8 propagates the open-file append flag into
`kiocb`, acquires the inode write lock, and then makes
`generic_write_checks` replace `ki_pos` with the locked `i_size`. This applies
to `write`, `writev`, `pwrite64`, `pwritev`, and `pwritev2`, including
`pwritev2(-1)`. No locked approval carries forward.

The repaired DRAFT retains the request's `CurrentOpenDescriptionOffset` or
`ExplicitOffset` mode and adds an independent `FileWritePlacement` decision.
Every nonempty append write emits one
`FileAppendSerializationAcquired` progress record for the exact bound open
description and object. Its ordinal is the FIFO serialization order and its
locked `objectSizeBefore` and `objectVersionBefore` are repeated by
`AppendAtEnd`. The actual effect range is
`[objectSizeBefore, objectSizeBefore + returnedBytes)`, its version edge begins
at that pre-write version, and the resulting object size is the range end.
Concurrent appends therefore have ordered successor sizes/versions and cannot
overlap.

A current-mode append publishes the actual range end as the new shared
open-description offset even when the requested current position differed from
EOF. An explicit append preserves its requested offset evidence and leaves the
shared open-description offset unchanged, while its actual file range is still
the serialized EOF range. Zero-effective-byte writes return before Linux's
append-placement branch, emit no append acquisition, select
`RequestedPosition`, and change no offset, object version, or data. A failure
after acquisition releases serialization authority without a file or offset
effect.

The append-aware codec bundle contains:

- `RequestedPosition` and `AppendAtEnd(record=2, size=10, version=9)`: 2 and
  26 bytes, SHA-256
  `b413f47d13ee2fe6c845b2ee141af81de858df4ec549a58b7970bb96645bc8d2`
  and
  `43033dc784547757ae4c9e7a988ab100cb7f29e1b00eb7685dba12e4209c9771`;
- the revised explicit offset effect, requested-position file effect, and
  requested-position complete success: 46, 88, and 140 bytes, SHA-256
  `3f0bf6a150bf33abd30670d74bdfb75e1a10b6e7956b3d22c13498d801419dc3`,
  `745cc989324b2e80a4d4b9d2cfcd6a2f8fcd3d30bdd1a7e8d35457ae34646887`,
  and
  `cf93eb2367d522287191f32301a1b6dc808398c7ef3b3dfd23e9e8b7b5c50aa1`;
- current append offset effect, append acquisition effect, complete progress
  record, and append file effect: 38, 82, 148, and 112 bytes, SHA-256
  `228993f021e39426ac03d0c95f62c8a308e5bbd8f9a59e30c2d19b7d3978204c`,
  `32a93d8456e1c77f96521b471a655180ed27fd8a339007fccb317351576944eb`,
  `9e82a5b8d9e7ff3e873c6ffd630c0591815dec312f7b5fe26d99d2e2739f1c2e`,
  and
  `44149b8d507f952c25462206bb3f8f353964fbec9ed753c63671e55ddf5707b9`;
- complete current-mode and explicit-mode append successes: 164 and 172
  bytes, SHA-256
  `97e24ff8c70c6d7b1c4fc6102b4c94abbb288aaafc46d64f412064814af96314`
  and
  `b726faf31e0e0a75791f84ddd457206b9f4bd51064971d067a9a969c7acf29a7`.

The executable fixture matrix covers every five write syscall tags, v2 `-1`,
explicit v2, independent and duplicated open descriptions, two concurrently
entered appends, partial success, failure, and zero bytes. Mutations alter the
append status, placement arm, description, serialization ordinal, pre-write
size/version, actual range, version edge, current actual-end update, explicit
shared-offset preservation, payload, and cross-operation order. An acquisition
without a matching operation or append effect, a duplicate acquisition, an
overlap, and every requested-position/append-position substitution reject.

The raw-offset, `MAX_RW_COUNT`, transfer, endpoint-summary, ptrace, same-mm
lease, clear-child-TID, robust-futex, reparenting, getdents, clone3, and
595-byte process-exit families remain enforced. RFC 0016 remains `DRAFT`,
`approvers` remains empty, and `Decision` remains `TBD`. The exact repaired
proposal SHA-256 submitted for fresh locked review is
`27869e83b756db43bee6ccfa2c7099deabd17345e574bcf9cbcdb5d8f9ef847f`.

### 2026-07-17 Clone3 Exit-Signal And Robust-Walk Return And Repair

Independent exact-hash review returned DRAFT proposal SHA-256
`27869e83b756db43bee6ccfa2c7099deabd17345e574bcf9cbcdb5d8f9ef847f`
and tracker SHA-256
`08e68a3129de3263f1a256b91ea5af199544c9bdf36054ac2ccfd6f56e498e9b`.
The clone3 request narrowed the copied U64 `exit_signal` to U31 before Linux
validation. Robust cleanup had no fixed 2048-visit bound or exact walk-
termination reason, rejected cycles instead of boundedly walking them, and
could not distinguish pending-last handling from Linux's early return before
pending. No locked approval carries forward.

The repaired DRAFT retains the complete raw U64 clone3 exit signal. Its closed
validation is `Valid(effectiveSignal)`, `InvalidHighBits`, or
`InvalidSignalNumber`. For the fixed x86-64 Linux v6.8 guest, high bits outside
`CSIGNAL=0xff` reject before low-byte number classification, while values zero
through `_NSIG=64` are valid. The exact post-copy order is set-TID size,
set-TID pointer/count shape, exit signal, cgroup, set-TID copy, then the ordered
`clone3_args_valid` flag and stack checks. The first failure wins; the high-bit
fixture `0x0000000100000011` is `EINVAL` with no effective `SIGCHLD` and no
child effect.

Robust cleanup now fixes `ROBUST_LIST_LIMIT=2048` and records one `visited`
entry per actual loop iteration without cycle deduplication. A 2047-node
acyclic list reaches the head; exactly 2048 nodes terminate at the limit even
when the last fetched next pointer is the head; and 2049 or more nodes retain
only the first 2048 visits. Cycles retain repeated visits and terminate at the
same limit. Each walk records head, limit, next-entry-read fault, or list-futex-
handling fault. Pending is handled exactly once after head or limit
termination, but not after either walk fault; a pending handling fault is
distinct from both outcomes.

The new exact codec bundle contains:

- valid `SIGCHLD`, high-bit-invalid, and number-invalid clone3 exit signals:
  14, 10, and 10 bytes, SHA-256
  `c8e9ec11e986e290da9649079fd489428c6cd23f688886332f56a849785c1f43`,
  `e0c1de214a16da12ae3090cb78ec254fae5867e4cfc70f775b1eee087b0795bc`,
  and
  `e1333474b13715b0bfc3888c60d067224a4d0d4af5922ef9b55e2d5922b59515`;
- the robust-list entry: 17 bytes, SHA-256
  `3467b4b841b20c2eb89d7042b2a1bf60128b7d6c545432adb60633ddca69b72f`;
- head, limit, next-entry-read-fault, and list-futex-handling-fault
  terminations: 2, 2, 19, and 19 bytes, SHA-256
  `b413f47d13ee2fe6c845b2ee141af81de858df4ec549a58b7970bb96645bc8d2`,
  `fcf0a6c700dd13e274b6fba8deea8dd9b26e4eedde3495717cac8408c9c5177f`,
  `5f800e209a47cc37bc63443590d305d6fdf19ce506bc4575213ade314542d99a`,
  and
  `d31b5b4cbe091fc0c6e91cda2cc18f072ebf20fdd9bf0799433b2f43b534cf2b`;
- absent, handled, not-processed-after-walk-fault, and handling-fault pending
  dispositions: 2, 27, 19, and 19 bytes, SHA-256
  `b413f47d13ee2fe6c845b2ee141af81de858df4ec549a58b7970bb96645bc8d2`,
  `27c2a912ea0549828244e582421797813e90c2702947a1ef1f1f19f8ce5cfb4a`,
  `5f800e209a47cc37bc63443590d305d6fdf19ce506bc4575213ade314542d99a`,
  and
  `d31b5b4cbe091fc0c6e91cda2cc18f072ebf20fdd9bf0799433b2f43b534cf2b`.

The executable matrix crosses valid and invalid clone3 signals with every
earlier and later validation class. It covers empty, 2047, 2048, 2049,
greater-acyclic, self-cycle, and multi-node-cycle robust lists; every
termination; and every pending disposition. Mutations alter raw signal bits,
validation/effective signal, validation precedence, visited length and order,
cycle duplicates, termination, pending position and disposition, effect index,
and effect order.

The append, raw-offset, `MAX_RW_COUNT`, transfer, endpoint-summary, ptrace,
same-mm lease, clear-child-TID, reparenting, getdents, and 595-byte process-exit
families remain enforced. RFC 0016 remains `DRAFT`, `approvers` remains empty,
and `Decision` remains `TBD`. The exact repaired proposal SHA-256 submitted for
fresh locked review is
`fe0faaab33dd1a692ebb04f0cd293aa94037b8f7eceb4f40081fa7f381d6a7c1`.

### 2026-07-18 Clone3 Copy-Phase And Robust Pre-Walk Snapshot Repair

Two independent exact-hash audits returned DRAFT proposal SHA-256
`fe0faaab33dd1a692ebb04f0cd293aa94037b8f7eceb4f40081fa7f381d6a7c1`
and tracker SHA-256
`13601d656d085700de61d641c5c0076026fe6b7a02d0ef9a1173c1abe1afb9d7`.
Linux v6.8 `copy_struct_from_user` inspects a clone3 extension tail before it
copies the common prefix, but the request algebra could not represent a
nonzero-tail `E2BIG` or distinguish a tail-read `EFAULT` from a prefix-copy
`EFAULT`. Registered robust-list cleanup also required a complete head
snapshot and therefore could not represent silent return from any of the three
ordered reads before the 2048-iteration loop. No locked approval carries
forward.

The repaired DRAFT records each successful aligned x86-64 word read by
`check_zeroed_user`, including its raw bits and significant mask. Its closed
inspection is not-present, all-zero, first-nonzero, or read-fault. Size checks
precede tail inspection; tail nonzero or fault precedes common-prefix copy;
prefix-copy fault precedes every decoded exit-signal and later validation.
Nonzero inspection returns `E2BIG`; tail and prefix faults are separately typed
`EFAULT` outcomes. Only a successful absent or all-zero inspection reaches
`Clone3Decoded`, whose prefix, zero fill, extension inspection, exact all-zero
extension, raw U64 exit signal, and remaining fields must agree.

Robust cleanup now makes null mean only an unregistered head. A registered
head records raw and masked pointer bits and performs the exact ordered reads
of `head->list.next`, `futex_offset`, and `list_op_pending`. Each possible
fault retains only the fields already read and terminates with zero visits,
zero effects, `PreWalkFault`, and `NotObservedBeforeWalk`. A complete snapshot
enters the unchanged loop contract: the exact 2047/2048/2049 boundaries,
repeated cycle visits, next-read-versus-futex-fault precedence, and pending-
after-walk rules remain intact.

The new exact codec bundle contains:

- the clone3 tail word: 24 bytes, SHA-256
  `fd948e9ce11c61931814e675e2452fb08d9806cea6f0add388cc6c2c5785529e`;
- not-present, all-zero, nonzero, and read-fault tail inspections: 2, 34, 34,
  and 42 bytes, SHA-256
  `b413f47d13ee2fe6c845b2ee141af81de858df4ec549a58b7970bb96645bc8d2`,
  `a3c1330d4f0a745efbe468b352a673f114c1c52ce3a0139630c813e981567e9f`,
  `ba034f2cde71b818192dad15c2cc3afc91398bb381e3ae4943bfa97e5f0f2cc2`,
  and
  `fa193b439b4f45c8a9d98b230a9e35b47dc78c94d2833f916506dcb547eed02a`;
- extension-nonzero, extension-read-fault, and prefix-copy-fault rejections:
  36, 44, and 46 bytes, SHA-256
  `0d71744d8f07ef41a43e0e6b20c0087e686b335f1b76fa3af13bd9b0f7fa2599`,
  `fb0e6fe921acf9b539a3202a30f0aac2235c02f96f1c59005bafed215e6d6d18`,
  and
  `580c4ca5a7b68bc62d3ec39893c7e854e1c53b9ba5e66df3594bd0e3f5970898`;
- the raw/masked robust-list pointer: 17 bytes, SHA-256
  `e18eb4f97235b31853af53bf4be0e02a0c5d98cd7489a15e404d94e07a93faf1`;
- head-next, futex-offset, and list-op-pending pre-walk faults: 2, 19, and 27
  bytes, SHA-256
  `b413f47d13ee2fe6c845b2ee141af81de858df4ec549a58b7970bb96645bc8d2`,
  `4db799ad4c2337c3d9fcca36c60144e3558d35948541318a5e5a9d79c78883ac`,
  and
  `008ed384b52015390a961bca6aa98db2ce602d484d400b071a5caeb7b5867f81`;
- failed-head and ready pre-walk snapshots: 4 and 44 bytes, SHA-256
  `76cc5805dab9b4eacefdb477f498020fd82bccdbc9c6a2d9ce10586ac85512b4`
  and
  `0fefde4f0f95dba574e8c658013f3a572b2ea5a2fcb2ed3f35eeba476894ab00`;
- pre-walk-fault and walk-head cleanup terminations: 2 and 4 bytes, SHA-256
  `b413f47d13ee2fe6c845b2ee141af81de858df4ec549a58b7970bb96645bc8d2`
  and
  `209425336127279cd1b301b5a1a159ffd74aaa96dd64dae5ba81c9f84f3e78c8`;
- not-observed-before-walk pending disposition and the complete failed-head
  cleanup: 2 and 33 bytes, SHA-256
  `9f1afa4dc124cba73134e82ff50f17c8f7164257c79fed9a13f5943a6acb8e3d`
  and
  `dd630e7eaf7faef92feba12ea213832765bb2dbbe0ee6a45e6246852d63ba00a`.

The executable matrix crosses both clone3 size failures, every tail state and
fault position, partial prefix copies, every decoded exit-signal class, and
all later validation classes. Robust fixtures distinguish unregistered and
registered heads, fault each pre-walk read, mutate every retained partial
field, and then rerun every existing empty/boundary/cycle/walk-fault/pending
case. Mutations alter word address, raw bits, masks, read order, fault phase,
copied prefix, failure precedence, raw/masked robust pointers, partial
snapshots, visits, effects, termination, and pending disposition.

The raw-U64 exit-signal, 2047/2048/2049/cycle/pending loop, append, raw-offset,
`MAX_RW_COUNT`, transfer, endpoint-summary, ptrace, same-mm lease,
clear-child-TID, reparenting, getdents, and 595-byte process-exit families
remain enforced. RFC 0016 remains `DRAFT`, `approvers` remains empty, and
`Decision` remains `TBD`. The exact repaired proposal SHA-256 submitted for
fresh locked review is
`c44ad2bf1252932d4ca82cfb1565f50e74bbe7d6982c765a4bc45a56589b1f83`.

### 2026-07-18 Signed Set-TID And Robust Next-Read Repair

Fresh exact-hash audit C returned DRAFT proposal SHA-256
`c44ad2bf1252932d4ca82cfb1565f50e74bbe7d6982c765a4bc45a56589b1f83`
and tracker SHA-256
`2374919a7a89d3475ebaba00c68c4670f6d9aef333c5397af9979f04e5c50a3f`.
`Clone3SetTidCapture` refined copied entries to positive numeric identities,
so it could not retain Linux's signed 32-bit `pid_t` values before later flag
and stack validation. `ListFutexHandlingFault` also discarded the result of
the next-pointer read already attempted in that same loop iteration, making a
successful next read plus futex fault indistinguishable from a next-read fault
plus futex fault. No locked approval carries forward.

The repaired DRAFT adds canonical signed `I32` and retains every successfully
copied set-TID entry in user-array order without early numeric-ID refinement.
Negative one, zero, and `INT32_MAX` remain exact evidence. Clone3 now replays
size, pointer/count shape, exit signal, cgroup, set-TID copy, flag, and stack
checks before the one-level namespace-depth and signed `alloc_pid` value
checks. A surviving requested PID allocation is policy-denied only after those
Linux checks. The closed precedence matrix crosses each signed boundary value
with a later flag error, a later stack error, and no later error, requiring the
flag, stack, and signed-value phases respectively while preserving
`Captured(values)` in all nine rows.

Robust walking adds
`RobustNextReadResult::Succeeded(RobustListPointer) | ReadFault` and stores it
in every `ListFutexHandlingFault`. A current-futex fault still outranks a
same-iteration next-read fault, but the evidence now retains which read result
occurred. Neither combination advances, decrements the limit, nor emits an
effect. Mutation fixtures change the result arm and every successful pointer
field, and reject dropping the payload or rewriting the double-fault case as
`NextEntryReadFault`.

The new exact codec bundle contains:

- `I32(-2)`: 4 bytes, SHA-256
  `bf906cd362964d265fdb27547a75d2ad2ce86cccec49cdc613764a77dc5f149d`;
- not-read, captured negative-one, captured zero, captured `INT32_MAX`, and
  ordered three-boundary set-TID values: 2, 14, 14, 14, and 22 bytes, SHA-256
  `b413f47d13ee2fe6c845b2ee141af81de858df4ec549a58b7970bb96645bc8d2`,
  `25e99230e02761790e17dff88176f53056996dc79a781cabbf462c16370a9a1b`,
  `ef3615c698f72f5590e632480b7b34a922b1dff6af69580295c26687017a23eb`,
  `f3c75a5f3f02331985682a919ba6cb5f5457da04841e29dc2e102b0be71f1a91`,
  and
  `598e3be68b7ba993f5a9eb39827c2e7f489ab11f33d512e2e0734f7d659b5262`;
- successful and faulted robust next-read results: 19 and 2 bytes, SHA-256
  `f8531604cc37e9344a450cc348bcb7b230bedaa6cc54666ec1418cc27062e1e5`
  and
  `fcf0a6c700dd13e274b6fba8deea8dd9b26e4eedde3495717cac8408c9c5177f`;
- list-futex-handling faults with a successful next read and a faulted next
  read: 38 and 21 bytes, SHA-256
  `3801f4875ea25e19397118b7ec714a9509d149156a23a84bd31cd33fb556087e`
  and
  `c25f5a0d1f5f718d087d4f3c31c74afbdeb8ff55a1708f5c4167c121baebb55e`.

The process-exit oracle contains no clone3 request and null robust cleanup, so
it remains exactly 595 bytes with SHA-256
`d746dc7fdfb2bc1d70f59215d6e31af366da23613cb4bdbd48643cfce3254960`.
All previous copy-phase, pre-walk, exit-signal, robust-boundary, append,
raw-offset, `MAX_RW_COUNT`, getdents, and retained-stream families remain
enforced. RFC 0016 remains `DRAFT`, `approvers` remains empty, and `Decision`
remains `TBD`. The exact repaired proposal SHA-256 submitted for fresh locked
review is
`040a653ababbe0dbae547aeebc3c1a2d0920e864e8cc177efc2d36b0dbccdd67`.

### 2026-07-18 Complete Clone And Futex Lifecycle Repair

Fresh exact-hash audit B returned DRAFT proposal SHA-256
`040a653ababbe0dbae547aeebc3c1a2d0920e864e8cc177efc2d36b0dbccdd67`
and tracker SHA-256
`f83b1fecf5e7f074a6d7c698bb5713ee9f339d6b16d7d21e1b14f9f4c0d63323`.
A set-TID `copy_from_user` fault collapsed every short copy to `NotRead`, so
arbitrary copied bytes were lost and complete `I32` elements could be
fabricated from a partial array. List and pending robust-futex handling faults
did not identify alignment, initial read, retry read, or unrecoverable write
phase and did not retain mismatch/retry observations. A valid requested TID
already occupying the bound numeric namespace could also reach executor policy
instead of Linux's `EEXIST`. No locked approval carries forward.

The repaired DRAFT adds
`Clone3SetTidCapture::CopyFault(cause, copiedPrefix)`. It
retains exactly the leading bytes reported copied by Linux, excludes
`copy_from_user` zero padding, permits every prefix length below the requested
array byte count, and never partially decodes `I32`. Empty, one-byte,
three-byte, four-byte, and final-byte-short fixtures prove that copy `EFAULT`
precedes all later flag, stack, value, occupancy, and policy decisions.

Both `ListFutexHandlingFault` and pending `HandlingFaultAfterWalk` now carry a
closed `RobustFutexHandlingFault`. Its arms distinguish misalignment with no
read, first `get_user` fault, retry `get_user` fault after a nonempty ordered
attempt sequence, and unrecoverable compare-exchange/write fault after an
optional sequence. Each retry observation retains the read word, derived
replacement, and compare-mismatch observed word, `-EAGAIN`, or recovered write
fault cause. The same-iteration next-read result remains independently bound
on list faults.

Requested set-TID allocation now consults the single live numeric-thread
task-ID occupancy authority at the exact serialized allocation point after
all value, child-reaper, and capability checks. Occupancy maps Linux
`idr_alloc` `ENOSPC` to `EEXIST`; only a valid free value reaches
`ControlledExecutor/PolicyDenied`. A paired fixture runs the same request while
its generation is live and after release, proving collision-before-policy.

The Linux-neighborhood follow-up found that those three local repairs were not
enough. `ProcessFork` is now a closed outer `CloneOperation`: every
`-ERESTARTNOINTR` attempt owns a fresh input-consumed ordinal, typed request,
canonical bytes, and digest; exact clone3 decode, argument, pre-allocation,
allocation, pidfd, cgroup, namespace-shutdown, fatal-signal, and controlled-
policy phases are byte-distinct. Resource and counter acquisition/unwind,
persistent PID cursor changes, null IDR reservation, pid-object publication,
task visibility, descriptor-table/`next_fd` reservation, pidfd writes,
parent-TID ignored faults, child wake, ptrace fork event, parent completion,
and vfork wait transitions are separate ordered effects. Child-TID first
return is tag-45 causal evidence, so no progress is appended to a sealed parent
operation.

The fixed kernel configuration is now executable authority rather than prose.
`ControlledExecutorAuthority` binds the embedded configuration digest, an
exact 15-member all-false `KernelSemanticFeatureVector`, and independent
`extract-ikconfig` and guest build-ID/`IS_ENABLED` oracle digests. Compat,
cgroup, security, audit, NUMA, performance-event, SysV-IPC, module, and kernel
fault-injection paths are compiled away and verified twice; remaining resource
failures retain exact phases and ledgers. All PI futex commands are denied by
masked base command, and every cleanup carries a task-local zero-count,
unchanged-epoch `PiStateEmptyProof`.

Robust handling faults now terminate only their native or compat inner walk.
Every exec and exit has an explicit outer `FutexExitCleanup` that records
`Ok -> Exiting -> Ok/Dead`, exit-mutex acquisition/release, native and compat
registration clearing, PI proof, and final state. Normal head/limit termination
retains the final successful next pointer; zero-visit ready heads preserve raw
head-versus-`head|1` PI evidence and still process a nonzero pending entry.
Recursive futex death is fatal trace loss, not a fabricated successful cleanup.

The new or changed exact codec bundle contains:

- not-read, three-byte copy-fault, captured negative-one, captured zero,
  captured `INT32_MAX`, and ordered three-boundary set-TID values: 2, 15, 14,
  14, 14, and 22 bytes, SHA-256
  `b413f47d13ee2fe6c845b2ee141af81de858df4ec549a58b7970bb96645bc8d2`,
  `2b46a5be7c9f7958cccc09f01c3e00cdc65715ed422cc1e3f14627be3d4e683a`,
  `f341a51e1109a84a353ddef81bdeb4d2348aa7d2fec3220e7dfd57a92386d13b`,
  `5f224161a5886bb91f2b20eaeb3dced60e7b33f3c3486dc37df4bc75f7adce5e`,
  `10ff9afdd4eda7b29eaa0198a4c0f9c54bddb32735a9e393d1224fcb131db0f5`,
  and
  `0e17ff2bc704494dfe1c9a784058e6bdedc8e5cecbd56bf11ac3989ddcc184e3`;
- compare-mismatch, atomic-retry, and recovered-write observations: 14, 10,
  and 10 bytes, SHA-256
  `5239a521f33456b5cb822c088e05f4914772f2085d5849a567d707eb99dcab95`,
  `ad8338303cd23a1265c1d44a7aaa4ea8dd7f450afe0296887cb7f536af7c1b59`,
  and
  `303a3059cd2ff8083c70bd280955bfa51314cda84cafdacb640d478a8657f929`;
- misaligned-no-read, initial-read-fault, retry-read-fault, and
  unrecoverable-write-fault values: 2, 2, 44, and 32 bytes, SHA-256
  `b413f47d13ee2fe6c845b2ee141af81de858df4ec549a58b7970bb96645bc8d2`,
  `fcf0a6c700dd13e274b6fba8deea8dd9b26e4eedde3495717cac8408c9c5177f`,
  `4f2553cca6fc49391471d5312b7eecf6503740d81a73b3ab38e26efa2fb691b5`,
  and
  `213b540f359c148c925789e1128e95b091eb62c9360732472cd95f3ca1c099cc`;
- list-futex faults with successful-next plus misalignment and faulted-next
  plus retry-read fault: 40 and 65 bytes, SHA-256
  `5a91d7f232687484da178076a7b46acbff69da3383fcc7ea99c4e4c4fe8445d1`
  and
  `229ccdd7da215548c40f526c2d41cbda72dc20740088ff4a7fbc9e6f22624a5c`;
- pending unrecoverable-write fault: 51 bytes, SHA-256
  `43ef4f83846c014c4d9e5bfd0f24f83bacce87755aed79c3d1c68a151dea491e`;
- Linux `EEXIST` failure: 22 bytes, SHA-256
  `a26222ed52061326aa92b48039224963d0fc54c5499de3e0af50189467ac718c`.
- injected-before-access, access-check, and raw-copy usercopy causes: two bytes
  each, SHA-256
  `b413f47d13ee2fe6c845b2ee141af81de858df4ec549a58b7970bb96645bc8d2`,
  `fcf0a6c700dd13e274b6fba8deea8dd9b26e4eedde3495717cac8408c9c5177f`,
  and
  `583c7dfb7b3055d99465544032a571e10a134b1b6f769422bbb71fd7fa167a5d`;
- raw-head, low-bit raw-head, and limit robust normal terminations: 19 bytes
  each, SHA-256
  `4c3ee8c8a45307f20efae3df6844d7eba250d9580023d9e32c59fefaf9ff6ee7`,
  `0552cd2c7930396db1b92a8678d98a238bfa612a4f4e1044c990925381112ad5`,
  and
  `6d1f9b8d83e4f007373b6eb627079f4c937fcbb5ced48d515d34f83e0e83bd9d`;
- all-false kernel feature vector and explicit empty-work exit futex cleanup:
  15 and 165 bytes, SHA-256
  `5322fecfc92a5e3248a297a3df3eddfb9bd9049504272e4f572b87fa36d4b3bd`
  and
  `fde83766a508ab059b3ff5f86547e79443c7701f582ba50d062faa0ffa9e6e36`.

The process-exit oracle still contains no clone3 request, but it now carries
the explicit empty-work outer futex cleanup. It is exactly 759 bytes with
SHA-256
`9ddd43465046fc9c89f7468994d0bf815dba7b764b5ddb95f40a14355b70d1af`;
its 457-byte semantic event has digest
`f0d67d50751b891b65206ce9819e6126acf1b00a3b52c023c04c489ed14628d7`.
All preceding trace families remain enforced. RFC 0016 remains `DRAFT`,
`approvers` remains empty, and `Decision` remains `TBD`. The exact repaired
proposal SHA-256 submitted for fresh locked review is
`a505398f0a8e59f4eca980183836492b3e9cac62baa66c382dd4350c59181491`.

### 2026-07-18 Primitive-Exact Lifecycle And PID-Slot Repair

Three fresh independent audits returned DRAFT proposal SHA-256
`a505398f0a8e59f4eca980183836492b3e9cac62baa66c382dd4350c59181491`
and tracker SHA-256
`d83aa86e082b268433da5213f7085a446f55cfadf82f4056db783674aa190603`.
The snapshot split exit and exec clear-child-TID state, admitted an impossible
x86-64 child terminal-before-schedule-tail arm, treated requested TID occupancy
as a live-thread-only map, left kernel configuration serialization ambiguous,
used an entry-time generic output-access proof for primitive clone writes, and
sealed restart attempts and the outer operation incorrectly. Subsequent
Linux-neighborhood pre-review showed that each issue reached deeper kernel
state. No approval carries forward, and this author repair is not an audit.

Exit and exec now share one `ClearChildTidCleanup`: `Unarmed`,
`DisarmedOnly(mmUsersBefore==1)`, or
`ZeroStoreAttemptedAndWake(mmUsersBefore>1)`. The zero store and futex wake are
independent. Wake success carries the exact resolved key and count; failure
carries errno, phase, and an optional already resolved key. Fixtures cover
single/last thread, early `exit_group` members, non-thread `CLONE_VM`, store
fault followed by successful wake, successful store followed by `EINVAL`, and
null/read-only/unmapped/misaligned `set_tid_address`. Futex exit cleanup
precedes clear-child-TID, and registration clearing precedes vfork release.

Every successful committed child now reaches the x86-64 schedule-tail hook.
The hook records the actual optional TID write before signal work; the parent's
fork/clone/vfork event may occur on either side of it. This returned snapshot
did not close inherited-child stop consumption correctly; the next repair
section supersedes that contract. Missing the schedule-tail hook is
`TraceLoss`; a fatal skip is not. Event 11, option bit
`0x00000800`, status `0x000b057f`, authenticated-ring kind four, and both
causal ordinal orders are explicit.

Numeric identity now uses one `PidSlotKey` per namespace/value/generation.
`Pid`, `Tgid`, `Pgid`, and `Sid` task-list holders carry `Live`, `ExitTrace`, or
`Zombie` lifecycle. A requested value returns Linux `EEXIST` for every
non-free slot, including waitable zombies, ptrace exit transfer, and surviving
group/session members after leader reap. Only four empty task lists permit
generation release. A pidfd-only reference preserves history but does not
occupy the IDR; fixtures require `PolicyDenied` after that release.

Kernel configuration is now a three-channel byte contract. The frozen v6.8
Makefile copies `KCONFIG_CONFIG` literally and runs `gzip -n -f -9`; the exact
embedded member is hashed. Raw `extract-ikconfig` stdout must equal the source
bytes and is hashed separately. The built-in hook reports build ID plus the 15
ordered `IS_ENABLED(CONFIG_*)` fields. Exact extract and hook record codecs,
field mapping, domain preimages, and vectors replace all ambiguous
canonicalization.

Clone writes now use `X86PutUser4Proof`, separate from a generic copy proof. The
proof records requested/effective address, `Identity | NegativeToAllOnes`,
stored or exact #PF/#GP result, before/intended/mask/after bytes, and hook ABI.
Because same-mm `MAP_FIXED` or `mprotect` may occur after syscall entry, output
mapping authority is acquired under a write-boundary mutation guard, not
inferred from entry. Full, prefix, and fault-without-lease arms bind that guard
and the primitive proof.

Every clone attempt has its own seal. An observed `-ERESTARTNOINTR` never seals
the outer operation. Each restart link carries a kernel restart token, complete
signal-frame digest, and multiprocess signal-fence install/sample/remove
evidence, then selects `Reentered`, typed `Abandoned`, or `PendingAtCut`.
Signal-handler syscalls are independent nested operations; reentry consumes a
fresh request snapshot and may see changed registers or memory. Only completed
or abandoned chains have an outer seal; pending-at-cut has neither a fabricated
outcome nor outer completion.

That snapshot's changed component-oracle bundle was invalidated by the later
boundary-exact and cycle-2 schema changes. It is intentionally not repeated;
the current exact component and composite KAT bundle appears in the cycle-3
repair entry below.

The process-exit oracle now encodes explicit empty-work futex cleanup before
`ClearChildTidCleanup::Unarmed`. It is exactly 760 bytes with SHA-256
`5926c57bf5a8f2bf0615ff660fe1021963e49ac78bacd05fef7064e70d261e44`;
its 458-byte semantic event has completion-preimage digest
`7475e9d9e66d9eb95efd15873576815869fa5f3f99ff05733689fa5a863d3d4a`.
RFC 0016 remains `DRAFT`, `approvers` remains empty, and `Decision` remains
`TBD`. The exact repaired proposal SHA-256 submitted for fresh locked review is
`2ca1b35178c8a98740040c98be28e50b0937500018ca75b79aab938fea4570d5`.

### 2026-07-18 Boundary-Exact Restart, Exec, Child, And Futex Repair

Locked Linux-neighborhood reviews returned DRAFT proposal SHA-256
`2ca1b35178c8a98740040c98be28e50b0937500018ca75b79aab938fea4570d5`
and tracker SHA-256
`73a12452ac058a9bb0178ab85b005d4e62aaee75cd4a526e7f58452aa7fbd624`.
The snapshot did not carry complete typed mapping, peer, sampled-signal, and
signal-policy digest preimages; it sealed a restart attempt from observation
rather than the actual boundary; it did not close direct, framed,
fatal-before-frame, non-local escape, or pending-at-cut restart paths. It also
did not reproduce non-leader `de_thread`, could lose a committed child ring
record when the standard stop was skipped by fatal delivery, required an
incorrect second inherited-child stop, and erased signed futex-wake failures.
Adjacent locked review found one further P0: `setup_rt_frame` and
`rt_sigreturn` failure force SIGSEGV and are not themselves abandonment. No
approval carries forward, and this author repair is not an audit.

Mapping authority and the same-mm peer fence are now versioned typed records.
The authority carries the raw mapping epoch, exact effective range, ordered
page intersections, and terminal classification. The fence carries the raw
peer-set epoch, complete writer/peer census, exact stop/resume state, and all
eight mutation classes, including `brk`, stack expansion, exec/exit retirement,
and fixed-kernel fault resolution. Mapping, peer, sampled-signal, and signal-policy
digests all name their complete version-one preimages under distinct domains;
no digest-only authority remains.

Every restarted clone attempt now consumes fresh input and has an optional
typed seal populated only at an actual completion, restart, or termination
boundary. `PendingAtCut` distinguishes a cut before the boundary from a live
direct or handler episode after it and never fabricates an outer completion.
Restart authority has closed `DirectNoHandler`, `HandlerFramed`, and
`FatalBeforeFrame` arms, exact Linux v6.8 signal-selection order, a complete
participant fence, nested handler operations, and event 12 for instrumented
non-local context restoration. Uninstrumented context escape remains pending
or rejects; a changed program counter is not abandonment evidence.

Handler authority is an ordered episode chain. A committed frame carries the
exact frame bytes and event-9 ordinal. A failed `setup_rt_frame` carries every
successful or partial region write, first fault, phase, error, and force
ordinal, with no event 9. A rejected `rt_sigreturn` carries its validation,
error, and force ordinal, with no event 10. Each failure must be followed by a
new fenced SIGSEGV selection. A SIGSEGV handler may build another frame or fail
recursively; only a later instrumented escape, exec, thread exit, or typed fatal
terminal edge abandons the restart. Otherwise the chain remains
`PendingAtCut`.

Non-leader exec now reproduces `exchange_tids` followed by separate TGID,
PGID, and SID `transfer_pid` records even when those identities alias. The
process slot never becomes free, is never reallocated, and retains its
generation. The displaced leader temporarily holds the caller's original TID
slot; release waits for its dead transition and surviving PGID/SID holders.
The former-TID message is projected in the tracer namespace, while ptrace and
every pidfd continue to target the surviving caller through the same process
identity.

The child schedule-tail hook publishes one authenticated ring record before
event 11. Event 11 itself is the sole authoritative first-return and inherited
initial stop; Linux v6.8 `ptrace_stop` consumes `JOBCTL_TRAP_STOP`, so the patch
does not re-arm it and no second stop is accepted. Fatal delivery before event
11 or after zero-signal continue but before the first user instruction uses two
distinct terminal arms bound to the same ring ordinal. `0x0080057f`, a zero
event message, re-arm, second stop, nonzero resume, and interrupt across either
closure window are negative mutations only.

Robust-list and clear-child wakes now retain a typed resolved key, exact signed
I32 return, Linux error, and alignment/key/queue phase. A robust owner-death
word remains committed when the ignored wake call fails. Pending owner zero
always attempts the wake without a write. Unexpected compare-exchange errors
retain the complete retry/read/replacement/error evidence instead of becoming
an impossible branch.

The returned snapshot's detailed oracle values were invalidated by cycle-2
schema changes and are intentionally not repeated. The current exact bundle is
recorded in the following repair entry.

The unchanged process-exit oracle remains exactly 760 bytes with SHA-256
`5926c57bf5a8f2bf0615ff660fe1021963e49ac78bacd05fef7064e70d261e44`;
its semantic completion preimage digest remains
`7475e9d9e66d9eb95efd15873576815869fa5f3f99ff05733689fa5a863d3d4a`.
RFC 0016 remains `DRAFT`, `approvers` remains empty, and `Decision` remains
`TBD`. The exact repaired proposal SHA-256 submitted for fresh locked review is
`a6fd158218e49de2427764c51cdbeb7e75f7df322ae41cd86962bc5406b6b74e`.

### 2026-07-18 Cycle-2 Linux Authority And Transcript Return And Repair

Cycle-2 locked review returned proposal SHA-256
`a6fd158218e49de2427764c51cdbeb7e75f7df322ae41cd86962bc5406b6b74e`
and tracker SHA-256
`839885aa97fad420204ad796ebd01fcbd50956347928cdce73d30017295fea99`.
No approval carries forward. This author repair is not an approval and does
not change the RFC status.

The returned mapping KAT placed its sample after guard/fence removal. The
repaired authority uses acquire/install record 1, mapping sample and
write-boundary-after record 2, and write-boundary-before/remove/release record
3. All dependent mapping, fence, guard, and proof hashes were independently
re-encoded. Robust-list misalignment is now only
`MisalignedNoRead`, with no word read or wake. The positive robust attempted
failure uses a resolved `SharedAnonymous` key and `QueueWake/EINVAL`; generic
alignment failure remains only for clear-child-TID and other independent wake
paths.

Signal restart authority is now an ordered Linux v6.8 `get_signal` transcript.
Every lock epoch has its own complete participant fence and state snapshot,
including task-work, uprobe, freezer, child-notification, jobctl/cgroup state,
queue/dequeue source and order, timer rearm, ptrace suppress/replace/requeue,
disposition, SA_ONESHOT, and the actual action. Ignored, default-ignore,
suppressed, unkillable, and job-control actions may continue. The only
terminals are exhausted, handler, and fatal; direct restart requires exhausted.
Multi-step KATs cover ignored then handler, job-control then queued forced
SIGSEGV handler, and forced SIGSEGV left queued while group exec wins fatally.

Frame authority binds the frozen kernel/XSAVE configuration and exact Linux
x86-64 order. A committed frame and its
`RestartSignalCommitted` progress event carry the byte-identical
`RestartFrame`. Sigreturn carries the exact source frame and restores mask,
restart-block state, GPR/IP/SP/orig_ax, FP/XSAVE, and alternate
stack in kernel order. Accepted validation proves exact reentry or enumerated
modified diversion; rejected validation retains every partial read/effect and
the exact `force_sig`, `force_fatal_sig`, or recursive `force_sigsegv` result.
A queued forced SIGSEGV is later selection input and need not be selected
immediately.

Exec authority now uses ordered `PidHolderTransfer` records for the two PID
exchanges, TGID, PGID, SID, and final caller-original PID removal. It retains
each intermediate holder snapshot and the four legal process/PGID/SID alias
partitions. The caller-original nonleader TID slot cannot carry PGID or SID.
The exact sequence is leader EXIT_ZOMBIE, exchange, TGID/PGID/SID transfers,
task/sibling replacement, group-leader pivot, EXIT_DEAD, optional tracer-parent
wake, tasklist unlock, then `release_task` with ptrace unlink and final PID
release. Tracer authority is a `ThreadKey`, event messages name their PID
namespace, waiter wake/relink/unlink ordinals are explicit, and pidfd identity
remains unchanged.

The cycle-3 author repair supersedes every dynamic vfork, signal-transcript,
frame, sigreturn, non-local context, event-11, and dependent composite KAT
claim from this cycle-2 entry. Those superseded mechanisms and digest-only
composite summaries are intentionally absent. The current normative design
and complete canonical byte carriers are recorded only in the cycle-3 entry
below and in the exact current proposal.

The process-exit oracle is unaffected by these type paths. Independent
hexadecimal decode and hashing still produces exactly 760 bytes with SHA-256
`5926c57bf5a8f2bf0615ff660fe1021963e49ac78bacd05fef7064e70d261e44`.
RFC 0016 remains `DRAFT`, `approvers` remains empty, and `Decision` remains
`TBD`. The exact repaired proposal SHA-256 submitted for fresh locked review is
`c84ac0098399886b996fc4bf47f954eb18a1cb1235b5f6393e00248470aee2e7`.

### 2026-07-18 Cycle-3 Native-Vfork And Exact Kernel-Semantics Repair

Cycle-3 author review returned exact DRAFT proposal SHA-256
`c84ac0098399886b996fc4bf47f954eb18a1cb1235b5f6393e00248470aee2e7`.
The vfork mechanism did not use native shared-mm semantics; same-mm and
external-writer closure was incomplete; signal lock epochs were collapsed into
decisions; forced-signal, frame, sigreturn, exec-holder, event-12, and
event-11 evidence did not reproduce Linux v6.8 exactly; and affected composite
KATs were not independently reconstructable. No approval survived.

The repaired DRAFT deletes that vfork memory mechanism and executes exact
native `CLONE_VM | CLONE_VFORK` flags. One fixed-kernel atomic transaction
installs a new-`CLONE_VM` gate, enumerates and stops every task sharing the
caller mm, preserves each prior stop kind for exact restoration, and proves
empty `io_uring`, AIO, process-vm, proc-mem, non-controller ptrace,
controller-memory-write, output-lease, and pending-kernel-writer sets. Writable
shared mappings and new shared writes reject. Non-faulting fixed hooks retain a
complete unmapped/VMA/protection/backing-offset/page-population topology and
full page bytes before child execution and at normal completion or aborted
freeze. The completion hook is immediately before `complete_vfork_done`.
Exec supports both scheduler orders of VFORK_DONE and EXEC while preserving
completion-before-mm-switch-before-EXEC; exit resumes the EXIT stop before
robust and clear-child cleanup. A `TASK_KILLABLE` wait abort freezes the child,
emits no VFORK_DONE, removes no fence, and permits no further child progress.
Nested process creation, unsafe IPC/locks, participant fatal signals,
external writers, and fixed execution/snapshot budget overflow reject.

Signal replay now records every real siglock acquire/release/fence separately
from gap-free decisions. Epochs and decisions are many-to-many; zero-decision
epochs and decisions spanning epochs are explicit. Private synchronous entries
are a filtered view of the complete private queue, and every dequeue,
replacement, requeue, action flag/reset, continuation, and ptrace-not-reached
reason has exact pre/post evidence. Ignored, suppressed, default-ignore,
unkillable, and job-control steps cannot directly restart.

Frame construction follows get-sigframe FP/XSAVE, main frame,
handler-register, then handler-entry state order. Forced-signal evidence retains
`SI_KERNEL`, actions, masks, unkillable state, queues, and enqueue/merge;
`force_fatal_sig` changes disposition and pending state but is not terminal at
the force point. That snapshot retained mask and `uc_flags` read evidence;
cycle 5 later split their fault and install ordering. It also retained
`restart_block` disable, sigcontext copy, batched GPR restore, every
FP retry/pollution/reset, and swallowed only the declared
internal alternate-stack errors. Reentry is proven by restart-critical IP, AX,
six arguments, and the later real syscall entry, not full-context equality.

Exec holder evidence added all four lifecycle tags and complete intermediate
transfer sets; cycle 5 later corrected the ptrace stop and wait transitions to
their Linux ordering. Event 12 is fixed guest
syscall 548 with an exact 320-byte UAPI: it copies a kernel-owned target,
validates token/task/source frame, atomically commits registers, mask, and
alternate stack before the ring/event stop, and proves all 27 pre-CONT registers
with exact GETREGSET. Unsupported context transfers remain open and reject at
final admission. Event 11 now requires ordered
`PTRACE_GETREGSET(0x4204, NT_PRSTATUS=1, requested=216, raw=0, returned=216)`,
exact 27-register equality, NONE-24, event-message, optional siginfo, semantic
seal, and `PTRACE_CONT/0`.

The proposal embeds complete canonical hexadecimal bytes for all 19 affected
composite KATs, including both native-vfork exec event orders, the aborted
shared-mm freeze, aborted-wait join, atomic context commit, accepted/rejected
sigreturn, frame success/failure, many-to-many signal transcript, event 11,
and all PID-holder lifecycle states. Independent extraction decoded every
carrier, reproduced every declared length and SHA-256, and revalidated the
unchanged 760-byte process-exit stream at SHA-256
`5926c57bf5a8f2bf0615ff660fe1021963e49ac78bacd05fef7064e70d261e44`.
The stale-mechanism scan is empty and the authoritative RFC structural gate
passes all 16 proposals.

RFC 0016 remains `DRAFT`, `approvers` remains empty, and `Decision` remains
`TBD`. The exact repaired proposal SHA-256 submitted for fresh locked review is
`cef456e70268ee35dc60076bd513caf50dfe3a613c2201bde56e26a2ec4e72ed`.

### 2026-07-18 Cycle-4 Controlled-Guest And Closure Repair

Cycle-4 review returned exact DRAFT proposal SHA-256
`cef456e70268ee35dc60076bd513caf50dfe3a613c2201bde56e26a2ec4e72ed`.
The fixed guest still carried disabled signal-extension execution arms;
pending signal bits and sigqueue nodes were collapsed; handler entry, event-12
landing, vfork snapshot non-mutation, backing-object writer closure, and digest
preimages were incomplete; and the affected KAT bundle retained stale
component summaries. No approval survived.

The repaired DRAFT binds a 17-false semantic feature vector plus required
`CONFIG_MEMBARRIER=y` and `CONFIG_IKCONFIG=y`. The raw 574-byte configuration,
212-byte deterministic gzip member, extract record, hook record, linked-object
absence proof, CPUID result, startup `rseq`/`map_shadow_stack`/`ARCH_SHSTK_*`
negative probes, and empty successful-registration lists are one admission
contract. Any evidence enabling either compiled-out extension rejects; no
frame, sigreturn, context, fixture, mutation, or coverage support arm remains.

Signal pending state now carries independent bitsets and stable sigqueue nodes
with identity, flags, and `SIGQUEUE_PREALLOC`. Forced sends retain complete
before/after action, mask, unkillable, bitset, node, allocation, merge, and
signed-return state. `GFP_ATOMIC` allocation failure is
`InfoLostBitOnly`; an already-set bit has `MergedExistingBitOnly`; later private
or shared dequeue carries fixed synthesized `SI_USER(pid=0, uid=0)` bytes.
Handler entry is exact flags clear (`0x10500`), user-FPU initialization with
supervisor preservation and fpregs state, restore-mask clear, derived handler
mask, raw `SS_AUTODISARM` reset, and denied/TIF-false single-step closure.

Event 12 dispatches syscall 548 after common syscall-enter/seccomp through a
typed `NormalReturn | ContextCommitted` token. Success commits `orig_ax=-1`,
skips the normal RAX and syscall-specific exit stores, retains general
exit-to-user work, and publishes a final landing-or-diversion hook before
forced IRET. GETREGSET before CONT is necessary but does not substitute for
the byte-equal final landing hook.

Native vfork now uses complete descriptor-table and open-description universes,
typed writable-descriptor/object-lease/distinct-mm writer rows, future object
mutation gates, and denial of asynchronous writers, `userfaultfd`, GUP, and
DMA. Every file-backed VMA, including `MAP_PRIVATE`, owns a backing-object seal.
The non-faulting snapshot has only `Resident { PFN, bytes }`,
`Unpopulated { logical backing, bytes }`, and `Unmapped` branches; exact PTE,
RSS, residency, and fault state is unchanged, while swap/migration/poison/
marker/THP/hugetlb/KSM/DAX/PFNMAP and EOF/SIGBUS cases reject. Census,
schedule, normal shared-mm, and aborted shared-mm digests each name a distinct
typed preimage that excludes its digest field and is independently recomputed.

The proposal embeds complete hexadecimal carriers for all 24 affected
Cycle-4 composite KATs. Independent decoding and re-encoding must reproduce
every declared length and SHA-256, the four vfork domain digests, both private
and shared bit-only synthesis carriers, and the unchanged 760-byte process-exit
stream at SHA-256
`5926c57bf5a8f2bf0615ff660fe1021963e49ac78bacd05fef7064e70d261e44`.
RFC 0016 remains `DRAFT`, `approvers` remains empty, and `Decision` remains
`TBD`.
The exact repaired proposal SHA-256 submitted for fresh locked review is
`df285fca875bed17353a6ad28c971aef544a47f0199c0f738c9782c363af648b`.

### 2026-07-18 Cycle-5 Signal, RSEQ, Exit-Lifecycle, And Writer Repair

Cycle-5 review returned exact DRAFT proposal SHA-256
`df285fca875bed17353a6ad28c971aef544a47f0199c0f738c9782c363af648b`.
Linux v6.8 `rt_sigreturn` still combined the independent signal-mask and
`uc_flags` reads and implied a header validation that does not exist. Event 12
could not distinguish landing from committed signal diversion, syscall 548
did not close the full x86-64 precommit validation surface, and later restart
links could lose commit history. RSEQ disablement was a static startup summary
instead of a per-image runtime lifecycle. Exec sibling cleanup and PID ptrace
state transitions were ordered incorrectly, and native-vfork writer rows
overlapped object, address-space, mapping, and kernel-output authority. No
approval survived.

The repaired DRAFT gives signal-mask and `uc_flags` separate ordered read and
fault carriers, installs the mask only after both complete, and removes the
invented header check. Syscall 548 now validates the exact 15 writable GPRs,
non-LA57 user addresses, fixed CS/SS and live selector/base equality,
`FIX_EFLAGS=0x50dd5`, TF denial, `orig_ax`, exact unmaskable-free eight-byte
mask, and `do_sigaltstack` constraints entirely before an atomic effect.
Success is an internal `ContextCommitted` dispatch with no ordinary RAX or
syscall-specific exit, followed by event 12, general exit work, and forced
IRET. The four landing arms are `Landed`, `HandlerDiverted`,
`PendingBeforeLanding`, and `TerminatedBeforeLanding`; only `Landed` closes
atomic abandonment, while diversion creates `CommittedThenDiverted` and every
later link retains `priorCommits`.

RSEQ is dynamic per-run evidence. The initial image and every successful exec
generation perform exactly one glibc-origin syscall 334 with `(address, 32, 0,
0x53053053)`, return `ENOSYS`, and preserve the full buffer. Later image
requests and thread registrations are exhaustively empty. Forked processes
inherit the parent failure seal without another probe, pthreads add none,
failed exec creates no generation or probe, and direct/librseq origins are
policy-denied before raw entry.

Every seized EXIT stop now preserves `Live`; `exit_notify` alone performs
`Live -> Zombie`. Nonleaders take `Zombie -> ExitDead`, while only a
ptrace-reparented group leader may take `Zombie -> ExitTrace -> Zombie |
ExitDead`. Exec ordinary siblings carry the full kill, EXIT stop/resume,
exit-mm, robust, clear-child, mm-release, exit-notify, optional user-ptracer,
wait/unlink, ExitDead, release, notify-count, and de-thread-wake sequence. The
displaced leader carries the full prefix and `notify_count=-1` wake before
exchange with no ptracer wait. Every lifecycle transition contains exhaustive
per-slot Pid/Tgid/Pgid/Sid rows.

Native-vfork writers are partitioned by addressing semantics: object mutation
leases, three remote `AddressSpaceWriter` mechanisms, `MappingWriter` with
typed anonymous or file backing, and open kernel-output leases. Carriers cannot
overlap or omit backing identity; normal admission has every mutable set empty,
and same-object aliases require exact nonoverlapping object-range seals.

The proposal embeds and independently verifies all 34 complete composite KATs,
including three sigreturn failure/success branches, context precommit/commit/
diversion, RSEQ lifecycle, exec sibling termination, both PID ptrace exit
branches, and every distinct-mm writer mechanism/backing. The four vfork
domain digests reproduce, and the unchanged 760-byte process-exit stream still
has SHA-256
`5926c57bf5a8f2bf0615ff660fe1021963e49ac78bacd05fef7064e70d261e44`.

RFC 0016 remains `DRAFT`, `approvers` remains empty, and `Decision` remains
`TBD`. The exact repaired proposal SHA-256 submitted for fresh locked review is
`a0f0160d17ddf150e2b2ebc2d718b2d16d054a1ddf1b0d5dc371da7a0542a571`.

### 2026-07-18 Cycle-6 Root-Reachability And Kernel-Exact Repair

Cycle-6 review returned exact DRAFT proposal SHA-256
`a0f0160d17ddf150e2b2ebc2d718b2d16d054a1ddf1b0d5dc371da7a0542a571`.
Normal thread exit did not make the PID ptrace lifecycle and final
`release_task` sequence mandatory, and exec sibling or displaced-leader
destruction could omit ptracer notification and wake evidence. Event 12 did
not carry complete general-exit and signal-stop work for every landing arm,
its pending cut was clone-only, and its terminal arm did not bind the inner
completion to the enclosing process exit. The native-vfork entry gate did not
enumerate every future writer kind. Syscall 548 lacked a typed request copy,
the precise non-LA57 `TASK_SIZE_MAX`, complete Linux v6.8 altstack decisions,
and a dedicated inner trace root. Scalar sigreturn reads and RSEQ failure,
parent, and raw-entry digests were not independently typed. No approval
survived.

Every normal `ThreadExitRecord` now reaches a closed PID lifecycle. Direct
death records the ptracer wait and `Zombie -> ExitDead` transition, then orders
`release_task` entry, ptrace unlink, `__exit_signal`, notify-count change, and
any wake. Reparented leaders retain the `ExitTrace` wait branch. Ordinary exec
siblings and the displaced leader carry mandatory ptracer notification and
wake facts, the typed `ExitDead` transition, and the nested release effect in
kernel order.

Event 12 now embeds complete stop evidence and the four closed landing arms.
`Landed` and `HandlerDiverted` retain their general-exit and signal chains;
`PendingBeforeLanding` seals a sorted cut containing both clone and atomic
context operations; `TerminatedBeforeLanding` retains the terminal transcript,
signal episodes, process-exit sequence, and exact inner completion ordinal.
The syscall-548 request and 320-byte copy are typed for success and partial
fault, address checks use `0x00007ffffffff000`, and altstack validation follows
the exact Linux v6.8 unchanged, disable, flag, on-stack, minimum-size, and
dynamic strict-size branches. Failure and success each publish a tag-42 entry,
dedicated tag-46 `CloneAtomicContext` semantic event, and tag-44 completion;
an enclosing clone reference cannot substitute for that operation root.

Sigreturn signal-mask and `uc_flags` scalar reads now carry success values or
faults without fabricated bulk-read metadata. RSEQ publishes domain-separated
typed preimages for image failure, inherited parent failure, and exhaustive raw
entry census. The native-vfork future-mutation gate enumerates all 21 declared
kinds, denies remote process-VM, `/proc/pid/mem`, and ptrace writes through
typed address-space targets, and rejects omission, duplication, reordering,
post-census installation, and target-range mutation.

Independent decoding, semantic validation, re-encoding, and hashing reproduce
all 44 complete composite KATs, both tag-46 operation streams, all three RSEQ
domain digests, the exhaustive native-vfork gate, pending and terminal event-12
histories, and the rooted three-event process-exit stream. That stream is
exactly 1783 bytes with event tags `42, 3, 44`, semantic-event length 1481,
completion digest
`006eae446d90cdca0fdb93cb0188ea3f0330b3fc24a944c7c7dd99db071669c6`,
and SHA-256
`3e8b9a7fe1dcfb2753751809592606820f16d49e312f2e1bab022c8c7d8a4982`.

RFC 0016 remains `DRAFT`, `approvers` remains empty, and `Decision` remains
`TBD`. The exact repaired proposal SHA-256 submitted for fresh locked review is
`58dd8a11c7deb8f1646f11427628fab5b49333ac41bd398f9c60e47a377fcaa2`.

### Review Queue

| Owner | State | Required review evidence |
|---|---|---|
| `task-router` | Blocked pending locked audit | Exact LLVM 22.1.8 build and CI ownership, versioned platform package routing, owner-prompt consistency, component and backend census, coverage ownership, and final gate selection |
| `rfc` | Blocked pending locked audit | Exact bound proposal hashes, additive-overlay legality, prior art, owner completeness, repaired RFC 0016 proposal hash, and status transition |
| `module-system` | Blocked pending locked audit | Brand-and-fingerprint freeze authority, exact target-bound RFC 0012 preparation/final handoffs, key-bound one-shot preparatory transition, immutable registry and runtime snapshot lifetime, cleanup, phase-local consumer authority, and preparatory/final brand separation |
| `error-system` | Blocked pending locked audit | Closed shared target-authority `ZOM9957` and `ZOM9958` construction diagnostics, brand-exhaustion mapping, direct RFC 0010 selection-result mapping, and failure precedence |
| `ir-backend` | Blocked pending locked audit | Typed `TargetRegistry` lookup, `MCSubtargetInfo`, `TargetMachine`, structural data-layout compatibility, known object format, runtime-bound target selection, and consumer closure |
| `runtime-memory` | Blocked pending locked audit | Single runtime capability registry, brand and revision, exhaustive package/backend/runtime mapping, abort-only initial authority, strategy-pair invariant, runtime ABI profile, and immutable value ownership |
| `spec-audit` | Blocked pending locked audit | Cross-RFC 0006/0008/0010/0011/0012 consistency, design-document impact, and prompt ownership |
| `verification` | Blocked pending fresh locked audit | All prior gates plus separate scalar sigreturn mask/`uc_flags` successes and faults; typed syscall-548 request copy, exact non-LA57 `TASK_SIZE_MAX`, Linux v6.8 altstack branches, zero-mutation failure, internal commit, and dedicated tag-42/tag-46/tag-44 roots on failure and success; complete event-12 general-exit and signal-stop chains for landed, diverted, pending, and terminated arms; clone-plus-atomic pending cuts and inner-completion-to-process-exit terminal links; typed RSEQ failure, parent, raw-entry, and lifecycle digests; mandatory exec ptracer notifications, wake facts, ExitDead transitions, unlink, release, and `__exit_signal` order; all 21 native-vfork future-mutation kinds and guarded remote-write targets; all 44 complete composite KAT byte vectors and mutations; recomputed vfork and RSEQ domain digests; exact event-11 `0x4204/NT_PRSTATUS/216/raw0/216` query; 17-false plus MEMBARRIER/IKCONFIG configuration closure; both tag-46 operation streams; and the independently reproduced exact 1783-byte rooted process-exit stream at SHA-256 `3e8b9a7fe1dcfb2753751809592606820f16d49e312f2e1bab022c8c7d8a4982` |

## Decision Record

Decision: TBD

RFC 0016 is `DRAFT` at exact proposal SHA-256
`58dd8a11c7deb8f1646f11427628fab5b49333ac41bd398f9c60e47a377fcaa2`.
The preceding DRAFT SHA-256
`a0f0160d17ddf150e2b2ebc2d718b2d16d054a1ddf1b0d5dc371da7a0542a571`
was returned for the cycle-6 PID-release root, event-12 history,
native-vfork mutation-gate, syscall-548 validation and inner trace root,
sigreturn scalar-read, RSEQ digest, exec notification, and dependent KAT gaps
recorded above. No locked approval carries forward.
The preceding DRAFT SHA-256
`df285fca875bed17353a6ad28c971aef544a47f0199c0f738c9782c363af648b`
was returned for the cycle-5 sigreturn, event-12/syscall-548, dynamic RSEQ,
exec/PID lifecycle, native-vfork writer-partition, and dependent KAT gaps
recorded above. No locked approval carries forward.
The preceding DRAFT SHA-256
`cef456e70268ee35dc60076bd513caf50dfe3a613c2201bde56e26a2ec4e72ed`
was returned for the cycle-4 controlled-guest, signal-pending,
handler-entry, event-12, native-vfork, digest-preimage, and complete-KAT gaps
recorded above. No locked approval carries forward.
The preceding DRAFT SHA-256
`c84ac0098399886b996fc4bf47f954eb18a1cb1235b5f6393e00248470aee2e7`
was returned for the cycle-3 native-vfork, signal-lock, frame, sigreturn,
exec-holder, event-12, event-11, and complete-KAT gaps recorded above. No
locked approval carries forward.
The preceding DRAFT SHA-256
`a6fd158218e49de2427764c51cdbeb7e75f7df322ae41cd86962bc5406b6b74e`
was returned for the cycle-2 mapping, robust, signal/frame/sigreturn, exec,
output/vfork, nonlocal-escape, event-11, KAT, and coverage gaps recorded above.
No locked approval carries forward.
The preceding DRAFT SHA-256
`2ca1b35178c8a98740040c98be28e50b0937500018ca75b79aab938fea4570d5`
was returned for incomplete typed mapping/signal preimages, observation-time
restart seals, unclosed restart/non-local escape paths, incomplete non-leader
exec identity transfer, incorrect child fatal/initial-stop handling, erased
futex wake failures, and immediate abandonment on frame-setup or sigreturn
validation failure. No locked approval carries forward.
The preceding DRAFT SHA-256
`a505398f0a8e59f4eca980183836492b3e9cac62baa66c382dd4350c59181491`
was returned by three fresh audits for the clear-child-TID, first-return,
PID-slot, configuration, primitive-write, and restart-chain gaps recorded
above. No locked approval carries forward.
The preceding DRAFT SHA-256
`040a653ababbe0dbae547aeebc3c1a2d0920e864e8cc177efc2d36b0dbccdd67`
was returned by audit B and Linux-neighborhood/configuration pre-review for the
clone and futex lifecycle gaps recorded above. No locked approval carries
forward.
The preceding DRAFT SHA-256
`c44ad2bf1252932d4ca82cfb1565f50e74bbe7d6982c765a4bc45a56589b1f83`
was returned because clone3 set-TID capture could not retain signed 32-bit
`pid_t` values before later flag and stack validation, and robust-list
futex-handling faults omitted their already attempted same-iteration next-read
result. No locked approval carries forward.
The preceding DRAFT SHA-256
`fe0faaab33dd1a692ebb04f0cd293aa94037b8f7eceb4f40081fa7f381d6a7c1`
was returned by two independent audits because clone3 omitted the ordered
extension-tail inspection and distinct nonzero-tail, tail-read, and
prefix-copy failures, while registered robust cleanup omitted partial
pre-walk snapshots and their silent returns. No locked approval carries
forward.
The preceding DRAFT SHA-256
`27869e83b756db43bee6ccfa2c7099deabd17345e574bcf9cbcdb5d8f9ef847f`
was returned because clone3 discarded high bits from its copied U64
`exit_signal` before validation, and robust cleanup lacked Linux v6.8's exact
2048-visit limit, termination, cycle, and pending-after-walk behavior. No
locked approval carries forward.
The preceding DRAFT SHA-256
`0fa25aaca261aa66986ec62b38e75b8f5202323c87c908491d36ff6faf61060c`
was returned because regular-file write effects ignored Linux v6.8
`O_APPEND`, including its locked EOF placement for positional writes and
current-mode publication of the actual end. No locked approval carries
forward.
The preceding DRAFT SHA-256
`38fd6272a0d0cb7a4f83317278ca1586e06f0b1010da0eb815ffb0b6fee16121`
was returned because `preadv2`/`pwritev2` raw offset `-1` lost its raw syscall
and signed-bit identity and was treated as an explicit offset rather than the
Linux v6.8 current-position route with open-description advancement. No locked
approval carries forward.
The preceding DRAFT SHA-256
`6f49edbb8ef1f5d2e327f5a4f53923eb5945c3d192dc18d0d7a4fd7d7d094777`
was returned because scalar and vectored read/write plus transfer effects had
no Linux v6.8 `MAX_RW_COUNT` raw/effective-count boundary and could exceed
signed return and U31 progress domains. No locked approval carries forward.
The preceding DRAFT SHA-256
`d9ab1538dea9556569a15ceb13993538b283a7daf37907ae4a35342a187bdaf2`
was returned because getdents count clamping changed unsigned ABI input and
because Linux v6.8 alignment padding was incorrectly normalized to zero rather
than retained from the pre-call destination through an exact write mask. No
locked approval carries forward.
The preceding DRAFT SHA-256
`b4947fd3d9e555659d0b138284e4b7dd12c22ac1a70d6c128160f7489a1bb76c`
was returned because endpoint completion double-counted progress effects;
ptrace lifecycle events and output mapping leases were underspecified;
exec-mm clear-child-TID, robust pending cleanup, parent reparenting, typed
getdents layouts, and clone3 validation/cardinality were incomplete. No locked
approval carries forward.
The preceding DRAFT SHA-256
`769593d09c3e0f2eaf9c1d89387fb199fb2c96a9a50a7d6a9512015fd30b41d5`
was returned because operation progress, pipe partial commits, and futex
requeue migration were not closed; synthetic ptrace parking/restart and mutable
user staging were not implementable; numeric identity reuse, exec de-threading,
robust owner-death, SIGCHLD/wait transitions, getdents bounds/types, and clone3
partial-size/parent semantics were incomplete. No locked approval carries
forward.
The preceding DRAFT SHA-256
`5e3cf506d4d213e85539898d26ff212c69255079a8fc070395f9c2f6e9c1ae33`
was returned because its RFC 0006 binding had drifted; blocking entry versus
completion order and deterministic queues were absent; peer freezing could
deadlock; clone3/thread/TID/exit and execveat/at-family/getdents/readlink facts
were incomplete; and ptrace signal, group-stop, restart, interrupt-stop, and
capability-preflight contracts were open. No locked approval carries forward.
The preceding DRAFT SHA-256
`de756f20a27420853d7fdeccf2f71492837deb95a4bd8c1d88314897b4ed4d6f`
was returned because clone/clone3 vfork class, endpoint capacity/readiness,
behavior-changing filesystem and host requests, Linux 4.8 ptrace pairing, and
non-self-referential coverage evidence were incomplete. No locked approval
carries forward.
The preceding DRAFT SHA-256
`e7c88f755ceff08d90cbbca20d68de2d03d388170ceb76e226a5666d6d755fbb`
was returned because endpoint streams and zero-copy transfer authority were
open, the executable syscall and host-result partition was incomplete, the
KVM/controlled-memory design required unavailable interception or unspecified authority
ownership, and normalized events lacked an exact independently reproducible
binary codec. No locked approval carries forward.
The preceding DRAFT SHA-256
`2b9e3729ee102993d2537dba9368e36a118902b8ce88729a87fa6ad05fc1b83e`
was returned because close errors did not distinguish consumed, preserved, and
unknown fd state; trace and platform algebras remained open; host-input
interception had no enforceable direct-execution mechanism; fd and vfork state
transitions were incomplete; and header coverage did not prove exact
compilation-object and final-target membership. No locked approval carries
forward.
The preceding DRAFT SHA-256
`f1a24c14b3cbea3daf12616ece9dcb90c39e304229cffc9fad7674124f600a89`
was returned because failed events could fabricate success-only facts, clone
and mapping inheritance/lifecycle were incomplete, two-platform host inputs
were open, and header coverage lacked its one-to-many contributing-TU/object
closure. No locked approval carries forward.
The preceding DRAFT SHA-256
`a1f8cc3e8b5d01e817604abe86cb08ddd835d7d00216d716670c3b89983dd43c`
was returned by locked audit A for an incomplete filesystem event algebra and
producer/final-path/write-after-read proof, reversed Mach-O run-path stack,
undefined glibc loader algorithm and evidence, and incomplete hermetic,
provenance, reproducibility, attestation, and dual-revision coverage prior art.
No locked approval carries forward.
The preceding DRAFT SHA-256
`3f6a4e1a831454faacf12fb7022b8c24b71dc1b4cf784e41fcc6e18b6320823d`
was returned by locked audit A for cross-run raw-stream and executable-byte
coupling, incomplete configure/build process and input closure, and undefined
loader-specific Mach-O token and run-path resolution. No locked approval
carries forward.
The preceding DRAFT SHA-256
`58c6e6f7d77b38421ac96f8d96e655b2501ef796f2fcc78e81e8ce25ba64e274`
was returned by locked audit number one because macOS shared-cache images were
required to be filesystem dylibs, normal safe Git config and the ingress clone
were rejected, CTest definitions and Python/shell/child execution were ambient,
and compilation-object bytes and final-link membership were unbound. No locked
approval carries forward.
The preceding DRAFT SHA-256
`ae911a14826c7c6e910be5d577c12baf8cedf85307b6c6664bbb059817cc6986`
was returned by a locked audit because cwd and process environment were open,
effective baseline/current build configuration could drift, the Ninja shell
closure was not representable, Git object/configuration and rename-limit
semantics were ambient, compiled sources were not tied to Git blobs and LCOV
records, and the compiler resource, sysroot, LLVM CMake, interpreter, and
dynamic-dependency closure was unbound. No locked approval carries forward.
The preceding DRAFT SHA-256
`a007fe9ee8bbc300368f09aff429b8d5a4ca65b419e17d75f35753f4be1b1cf3`
was returned by locked audits because raw JUnit and coverage artifacts were not
checker inputs, LCOV was not reproducibly bound to its profiles, profdata, and
objects, actual build commands did not prove the declared coverage flags, Git
configuration was ambient, cross-boundary copies and renames could escape the
census, and source/count report fields had no exact Git/LCOV preimage. No locked
approval carries forward.
The preceding DRAFT SHA-256
`189323da32eab5964f7e186000b6a1ba6c4dc2fec37a6686c8ec0e1716425af2`
was returned by a locked audit because its frozen baseline used a fail-soft,
ambient-tool coverage target, its approval was self-referential, its deletion
census was absent, its JSON and digest contracts were open, and its acceptance
commit allowlist was incomplete. No locked approval carries forward.
The preceding DRAFT SHA-256
`da891985b88615911d221a93d7f146239b69314ed1da7a1dbab1ba68ff0b32e2`
was returned by a locked audit because its baseline, census, exemption,
machine-report, mutation, and live routing contracts were incomplete. No
locked approval carries forward.
The preceding REVIEW SHA-256
`713bc7ce30df9e99a537cc94f87e646df4ef18fa1d96d02e90724c46cf90d7c0`
was returned by the task-router owner because new compiler sources lacked an
enforced per-file coverage threshold or complete exemption, aggregate baseline
non-regression, and a verification-owned evidence report. Every approval on it
is void.
The metadata-identical DRAFT SHA-256
`2d9e22738f4b57550a8fb16597c4f5b5da0247c7e7cf8553f1100d0b5c83604a`
received three independent locked-audit approvals before the transition.
The preceding DRAFT SHA-256
`d10f39f72fd44455ad750fa393a78bfb7e0492f6e792f3794dc9922e2a6ca12b`
was returned by a locked audit because the registry did not retain the typed
runtime ABI association required by the snapshot-bound selection query. Its
locked approval is void.
The preceding REVIEW SHA-256
`1dc2d86b82db95aaf71796bfba7ef2ae95c3e97edfcd092f3d1a75d608e9f79b`
was returned by the runtime-memory owner because the runtime capability query
was snapshot-unbound and the mutable issuer lacked explicit process-root
ownership and singleton prohibition. Every approval on it is void.
The exact DRAFT SHA-256
`963e70acdb13a3b702e8bbf87936ef6ecb34e3ee05d2c349d868e7cbe4b98753`
received three independent locked-audit approvals before the metadata-only
transition.
The preceding DRAFT SHA-256
`ba82d4060ff9c075fa17327de85256879c00790266c3e7bdbc6d6d2065b1987b`
was returned by the `module-system` locked audit because its per-node guide had
no exact key-indexed, proof-consuming, one-shot transition or complete mismatch
cleanup precedence. Its locked approval is void.
The preceding DRAFT SHA-256
`a2a4605f338ce50bd11006e44a5452a580cb6f1d3073f3707f2dcdcfbf400ee4`
was returned by a locked audit because its guide placed one preparatory context
and proof outside the multi-node execution loop. Its locked approval is void.
The preceding REVIEW SHA-256
`72acabf30be04821880320991f6356b49721d215a2e8c8e963fb2e74f700e178`
was returned by `module-system` because its complete-map validation order made
multi-node build-plan execution impossible and its pre-definition/impl final
authority could already call final issuance. Every approval on it is void.
The preceding DRAFT SHA-256
`58e15302eb30355f040987e24746195334c6964e4f9b29a74d6858277aca715f`
received three independent locked-audit approvals before this metadata-only
transition.
The preceding DRAFT SHA-256
`ff480f776780861a9174cb63185c2a514895bc7ef33e34cca753266ccc67874a`
was returned because its guide omitted final definition and impl freeze before
final proof issuance.
The preceding DRAFT SHA-256
`b0e02f63daaacb4ce39224ba22a51e544a23ea369d5ff683fe9d79a77c8f9849`
was returned by a locked audit because its guide-level generic-authority flow
contradicted the normative phase-specific wrapper API. Its locked approval is
void.
The preceding REVIEW SHA-256
`1f6c75cc50c828aa7801c0826a5ff0d429a8ce80ca3f30d82fa42241a7ff3b63`
was returned by the RFC owner because its bound-proposal table did not declare
the complete RFC 0012 final-handoff overlay. Every approval on it is void. The
exact DRAFT SHA-256 before that review transition
`6962c02c117371218925129b20f3cfffb48d8c5daa7d605db3ec84fb48766d04`
received three independent locked-audit approvals before this metadata-only
transition. That audit cycle and transition are complete historical facts.
The preceding REVIEW proposal SHA-256
`80a795ae4a800080e753401bb3662741b221b9ce6550a280cd710b912eae84bb`
was returned by `module-system`; every approval on it is void. A subsequent
locked audit returned DRAFT SHA-256
`8293553957c294f553d6a75fedf6dff95e1bb3421c1daa9ce02d83d1ab2ff9fa`
because issuance still accepted a generic frozen context authority. The
next DRAFT SHA-256
`e282011006333ccb0bcc6aaaa4ddd8ce04d78ff527c0296a7f36c189c8a36b83`
was returned because its final phase-authority producer precondition was
circular with definition/impl freeze. The next DRAFT SHA-256
`92c7249b4b1187af417bfdc3fbb9cd60d2a0f1905422ad736b8033e871b2abb8`
was returned because it specified an impossible privileged runtime phase
corruption result despite making wrong-phase calls unrepresentable. The next
DRAFT SHA-256
`32a96841ea0b2d441616d382e3587bcf88e2d6e1c7e9f17b8c7aee480cfdf1fb`
was returned because same-phase authorities were not bound to their exact
package-session wrapper. The next DRAFT SHA-256
`2bb340f106c3ef5a3fe2e7e9dce5f954339ee4bd4096b9160b213c08fc7b9c46`
was returned because it published a standalone RFC 0012 final value before
consuming the target-bound preparation wrapper and its move-only source views.
The next DRAFT SHA-256
`e2cae27ff2ad0ef512e31b67ec6190f67a46a0143e6072764309e57879b47cd3`
was returned because it validated build results and key sets before the atomic
transition owned the preparation snapshots whose cleanup it promised. The
repaired phase-context-bound DRAFT passed three locked audits before the
returned REVIEW transition above. Earlier
verification returned proposal SHA-256
`f9b6329ac950e0a9a03f1705bf6f59121c7977ce8a2d108dc8deca39e750e1f7`
because non-empty invalid `LLVM_DIR` input could fall back to ambient package
discovery. Every approval on that snapshot is void. The repaired DRAFT now
requires canonical requested-directory equality, `NO_DEFAULT_PATH`, exact
package tool-prefix provenance, stable failure identifiers, and an isolated
ambient-fallback negative fixture. A further verification audit required exact
requested, resolved, CMake, install, tools, executable, reported-prefix, and
reported-CMake-directory equality plus positive and negative provenance
evidence. That snapshot passed two locked audits, but a task-router audit
returned it because the manifest path census was absent from three owner
prompts and no executable gate required all routing views to agree. The owner
prompts, governance paths, `AGENTS.md` summary, acceptance criteria, and test
plan were synchronized in that historical repair. Its exact DRAFT SHA-256 was
`5f2396cb209931321c6693cc55ae471f6914398f01ff2f510f4cfbe909aad566`.
Exact prior REVIEW snapshots
`e4df8edba990564e4718484e36285bdbc084cd70ef0ea4fdea2c37cc32de813d` and
`179318bb7d4f2d191e884e58349a6c9f2fef9e6730621546ef1a08c50cdca101`
were returned and every approval on them is void. No implementation is
authorized before a fresh exact REVIEW snapshot receives unanimous approval
and the RFC moves to `ACCEPTED`.

## Implementation Tracker

| Slice | State | Required evidence |
|---|---|---|
| LLVM build and CI contract | Blocked by RFC 0016 acceptance | Exact LLVM 22.1.8; canonical explicit `LLVM_DIR` with `NO_DEFAULT_PATH`; exact requested/resolved/`LLVM_CMAKE_DIR`, `LLVM_INSTALL_PREFIX`, `LLVM_TOOLS_BINARY_DIR`, `${LLVM_TOOLS_BINARY_DIR}/llvm-config`, `--prefix`, and `--cmakedir` provenance equality; exact `Core`, `Support`, `Target`, `TargetParser`, `MC`, `CodeGen`, `AsmParser`, `AsmPrinter`, `BitWriter`, `X86`, and `AArch64` components; X86/AArch64 inventory; `llvm@22` and `apt.llvm.org` sources; fixed runner labels; macOS/Linux positive provenance, component, and backend census; and stable negative fixtures for `ZOM-CMAKE-LLVM-DIR-REQUIRED`, `ZOM-CMAKE-LLVM-DIR-INVALID` with compatible ambient LLVM exposed, `ZOM-CMAKE-LLVM-PROVENANCE` across every forwarded-config and path/result mismatch, `ZOM-CMAKE-LLVM-VERSION`, `ZOM-CMAKE-LLVM-CONFIG-VERSION`, `ZOM-CMAKE-LLVM-COMPONENT`, and `ZOM-CMAKE-LLVM-TARGET` |
| Runtime capability authority | Blocked by RFC 0016 acceptance | Single generated registry, ABI-aware runtime query, process-global monotonic non-reusing brand factory with exhaustion tests, deterministic revision, 59-byte oracle, abort-only initial manifest, unsupported-unwind rejection, shared target-authority construction result, and no ambient or duplicate table |
| Target registry construction | Blocked by prior slice | Closed construction result; runtime brand/revision binding; typed `TargetRegistry`, `MCSubtargetInfo`, and `TargetMachine` admission; CPU/feature grammar and membership; structural layout compatibility; known object format; exact limits; strategy-pair invariant; and independent 111/189/189/52/197/248-byte oracle proof |
| Context-bound target selection | Blocked by prior slices | Seven-field by-value token; unrelated preparatory/final frozen-context authorities; exact `BuildPlanNodeKey` on each preparatory authority and proof; three unrelated preparation-host, final-host, and final-target proof types; wrapper-selected exact request fields; no public raw verifier; frozen context and runtime authorities; direct RFC 0010 result algebra with private RFC 0012 cleanup composition only; exact thirteen-step order; exhaustive panic mappings; capability, ID, and revision recomputation; and negative tests |
| Package-session and build-script ordering | Blocked by prior slices | Exact RFC 0012 preparation/final wrappers; private constructors and one atomic producer; moved registry and runtime snapshots; one key-indexed `Pending -> Authorized -> Executed` transition per node; consuming authority verification and proof execution; pre-selection mismatch closure; source views, build plan, build results, generated views, key-set proof, `finish()` and `SnapshotCleanupFailed` precedence; preparation exposing host issuance only; distinct preparatory/final context brands and fingerprints; and no pre-freeze token |
| Consumer and backend cutover | Blocked by prior slices | Exact phase-proof type and complete phase-authorized package-selection equality before context brand/fingerprint, runtime brand/revision, registry-revision, and target-ID checks; phase-swap rejection; reconstructed `TargetMachine`; structural data-layout compatibility; known object format; and deleted alternate authority paths |
| Controlled dual-revision coverage gate | Blocked by RFC 0016 acceptance | Every proposal gate, including exact mapping write-boundary authority; robust no-read/no-wake failure; complete lock-epoch/snapshot/decision `get_signal` replay with separate pending bits/stable nodes and bit-only allocation/merge/private/shared synthesis; forced-signal state; get-sigframe/frame/register/postamble order and single-step denial; separate scalar sigreturn reads, later effects, and restart-critical entry proof; typed syscall-548 request copy, exact address/selector/flags/altstack validation, zero-mutation failures, internal commit dispatch, dedicated inner operation roots, complete event-12 stop evidence, four landing arms, clone-plus-atomic pending cut, committed diversion, terminal process-exit linkage, and prior-commit continuity; typed dynamic RSEQ initial/exec/fork/thread lifecycle plus failure, parent, and raw-entry digest preimages; ptrace EXIT stop orthogonal to the complete Live/Zombie/ExitTrace/ExitDead lifecycle, per-slot/list rows, and mandatory notification/wake/unlink/release ordering for normal exit and exec destruction; typed output pin/content carriers; native-vfork atomic mm fence, disjoint writer closure, exhaustive 21-kind future-mutation gate, guarded remote-write targets, typed backing identities, all-file-backed seals, non-mutating topology/page snapshots, closed PTE matrix, release joins, aborted wait, budgets, and explicit digest preimages; full event-11 `GETREGSET(0x4204,1,216)`/NONE/event-message/seal/resume evidence; all 44 complete canonical composite bytes and mutations; 17-false plus MEMBARRIER/IKCONFIG raw/gzip/hook/object/shadow-stack configuration; explicit futex/PI-empty and robust final-next proofs; all filesystem, endpoint, I/O, wait, lifecycle, coverage, attestation, and repository checks; both tag-46 operation streams; and the exact 1783-byte rooted process-exit oracle at SHA-256 `3e8b9a7fe1dcfb2753751809592606820f16d49e312f2e1bab022c8c7d8a4982` |
| Documentation and repository gates | Pending | Design docs, trackers, manifest/README/AGENTS/owner-prompt routing consistency with negative fixtures, format, sanitizer, full CTest, architecture, determinism, RFC, and diff evidence |

Implementation states may change only after the RFC decision permits work and
the named evidence is attached.
