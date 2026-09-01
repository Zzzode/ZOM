---
rfc: 43
title: Platform Link And Executable Publication
type: compiler
status: IMPLEMENTING
author: ZOM Compiler Team
review-manager: rfc
required-owners: [rfc, ir-backend, module-system, runtime-memory, error-system, verification]
approvers: [rfc, ir-backend, module-system, runtime-memory, error-system, verification]
created: 2026-08-15
updated: 2026-08-30
area: compiler
requires: [6, 10, 12, 16, 21]
supersedes: []
superseded-by: []
discussion: docs/rfc/tracking/0043-review-and-implementation.md#discussion-record
decision: docs/rfc/tracking/0043-review-and-implementation.md#decision-record
implementation: docs/rfc/tracking/0043-review-and-implementation.md#implementation-tracker
tracking-issue: docs/rfc/tracking/0043-review-and-implementation.md#implementation-tracker
---

# RFC 0043: Platform Link And Executable Publication

## Summary

This RFC defines the post-object contract that turns one verified object artifact
and its verified runtime closure into one recoverably published executable whose
manifest commit is its sole visibility marker. It
adds an immutable link plan, a target-selected linker driver invocation, an
executable artifact manifest, and a host-compatibility rule for `zomc run`.
The first supported executable targets are Linux ELF and macOS Mach-O on
`x86_64` and `aarch64`.

The contract starts only after RFC 0021 has produced `VerifiedObjectArtifact`.
It does not define LIR, LLVM translation, object emission, source language
semantics, package resolution, or a general cross-platform runtime.

## Motivation

The compiler can select Linux ELF and macOS Mach-O target profiles, but it
cannot yet represent the deterministic set of objects, runtime artifacts,
linker arguments, and output destination needed to publish a runnable program.
Passing ad-hoc paths and ambient environment variables to a host linker would
make output depend on the current directory, search paths, and local toolchain
state. It would also blur the difference between producing a cross-target
executable and executing an executable for the host.

RFC 0021 deliberately stops at verified object emission. A separate contract
is needed to preserve its proof boundary while defining the next product-facing
stage. This RFC supplies that contract without authorizing backend work before
its dependencies and review gates are satisfied.

## Goals

- Define one private-construction, independently verified `LinkPlan` for a
  single executable target.
- Bind every link input to the exact target specification, object digest,
  runtime closure, toolchain identity, and output policy.
- Invoke a target-selected linker driver with an explicit argument vector and
  a sanitized environment.
- Publish an executable and its manifest as a recoverable transaction whose
  manifest commit is the sole visibility marker, so nothing is a consumable
  artifact until the manifest commits and verifies.
- Support Linux ELF and macOS Mach-O executable publication on `x86_64` and
  `aarch64` once their required object and runtime inputs exist.
- Permit `zomc run` only for a verified executable whose target is compatible
  with the current host.
- Make cross-target publication deterministic and never silently execute it.

## Non-Goals

- Defining LIR, LLVM IR translation, object emission, or object-file
  verification; RFC 0021 owns those stages.
- Adding Windows, COFF, WebAssembly, shared libraries, static libraries,
  dynamic loading, universal binaries, or remote execution.
- Discovering system libraries, accepting caller-provided raw linker flags,
  inheriting linker search paths, or supporting arbitrary linker scripts.
- Replacing the target authority, package resolver, runtime ABI, or diagnostic
  failure algebra owned by existing RFCs.
- Claiming that an executable can be emitted before the required verified
  object artifact and runtime closure exist.

## Prior Art

[Clang's toolchain documentation](https://clang.llvm.org/docs/Toolchain.html)
separates compilation, object production, and linking, and exposes a driver
mode that can show its command without running it. ZOM adopts the explicit
stage boundary and reproducible argument construction, but does not expose an
unbounded shell command or ambient tool search to user code.

[Rust target options](https://doc.rust-lang.org/stable/nightly-rustc/rustc_target/spec/struct.TargetOptions.html)
bind linker flavor, linker selection, and pre/post-link objects to a target
specification. ZOM likewise binds tool and runtime inputs to a selected target,
but begins with a closed two-format target set rather than a user-defined
target JSON surface.

[Rust code-generation options](https://doc.rust-lang.org/stable/rustc/codegen-options/)
distinguish linker flavors and self-contained linking. ZOM adopts the
distinction between a driver interface and a runtime closure, while requiring
the closure to be verified and recorded rather than inferred from host
defaults.

[LLD](https://lld.llvm.org/) provides format-specific ELF and Mach-O linkers.
ZOM uses a target-selected driver interface so the same verified plan can map
to the required platform linker flavor without treating ELF arguments as
Mach-O arguments.

[The Clang driver's `--sysroot` and macOS `-isysroot`](https://clang.llvm.org/docs/CommandGuide/clang.html)
locate a target root and SDK for headers, startup objects, and default
libraries instead of trusting host defaults. ZOM adopts an explicit sysroot and
SDK root, but records it in a verified closure and forbids the ambient
`SDKROOT`/search-path discovery Clang also permits.

[Rust's target sysroot and `cc`-crate linker discovery](https://rustc-dev-guide.rust-lang.org/backend/libs-and-metadata.html)
resolve the sysroot, startup objects, and a linker program per target. ZOM
copies the per-target binding of sysroot plus linker but pins those inputs to a
digest-verified closure rather than probing the environment for a compiler.

[Zig's bundled cross libc and sysroot model](https://ziglang.org/learn/overview/#zig-is-also-a-c-compiler)
ships a hermetic, reproducible set of libc, CRT, and headers per target so a
cross build never reads the host toolchain. ZOM keeps the hermetic, reproducible
goal and records every input digest, but does not vendor the platform SDK; it
binds an explicitly supplied, verified toolchain root.

The common pitfalls are accidental use of host libraries for a cross target,
non-reproducible output from inherited search paths, and executing a foreign
binary as if it were native. The closed target/runtime closure, sanitized
environment, and host-compatibility gate address these cases.

## Guide-Level Explanation

Once the prerequisite pipeline exists, `zomc compile app.zom --emit exe`
creates exactly one executable request. The compiler derives its object and
runtime inputs from the verified compilation session, constructs a link plan,
and invokes the selected linker without a shell. On success it publishes both
`app` and `app.zom-artifact` in the requested output directory as a recoverable
transaction whose manifest commit is the sole visibility marker.

`zomc run app.zom` first performs the same verified publication. It launches
the result only when the selected target's operating system, architecture,
object format, and execution ABI match the host. A Linux target may be
published from macOS when a valid cross linker and runtime closure are
available, but `run` rejects that result instead of attempting emulation.

```mermaid
flowchart LR
    object["Verified object artifact"] --> plan["Verified link plan"]
    runtime["Verified runtime closure"] --> plan
    target["Selected target and toolchain"] --> plan
    plan --> driver["Target linker driver"]
    driver --> verify["Executable verification"]
    verify --> publish["Recoverable manifest-committed publication"]
    publish --> run["Host-compatible zomc run"]
```

## Reference-Level Design

### Inputs And Link Plan

`ExecutableLinkRequest` is private to the final package compilation path. It
contains exactly one `VerifiedObjectArtifact`, one selected target authority,
one verified runtime closure, one package entry identity, and one normalized
output request. It has no public aggregate initializer and cannot be decoded
from CLI text or reconstructed from raw paths.

The request constructs `LinkPlan` only after an independent verifier proves:

1. the object target, object format, ABI manifest, and configuration key equal
   the selected target authority;
2. every runtime object and required system capability belongs to the same
   target, runtime ABI, and toolchain closure;
3. the entry object provides the sole selected package entry symbol;
4. object and runtime records are sorted by their complete canonical keys and
   contain no duplicate path, symbol, or role;
5. every file input is an already verified, immutable artifact with a recorded
   digest and byte count; and
6. the output path is normalized, is inside the requested output root, and
   uses `RejectExisting` publication semantics.

The verified plan stores the target specification identity, toolchain identity,
entry identity, ordered object records, ordered runtime records, output request,
and a `LinkPlanId`. `LinkPlanId` is SHA-256 over a domain-separated,
length-framed encoding of those complete records. It includes the normalized
output request and every recorded artifact path, while excluding ambient or
otherwise unrecorded host paths. The plan stores only closed structural
authorities; the target driver derives its canonical argument vector from those
fields. No raw or generic argument surface exists: there is no argument record
type, no argument sequence in the plan, and no argument bytes in the canonical
`LinkPlanId` preimage. A future target-selected link policy (for example a
static-versus-dynamic `LinkMode`, or a PIE policy) is introduced by an
authorized slice as its own closed structural field, validated with the
toolchain closure and folded into `LinkPlanId`; it is never revived as a generic
argument list.

The construction API is:

```text
planExecutable(
  request: Moved<ExecutableLinkRequest>,
  capability: Borrowed<const TargetRegistryCapability>,
) -> RFC0010::IrOperationResult<VerifiedLinkPlan>
```

Rejection consumes the request and publishes neither a plan nor a partial
executable. Link-plan construction runs in the new `LinkPlanConstruction` phase
defined under "Linker And Publication Failure Algebra" below. Input target,
ABI, digest, or capability disagreement selects `InputRevisionMismatch` or
`InvalidFact`. A missing or additional closure record selects
`MissingRequiredFact` or `AdditionalFact`. A malformed order, encoding, or
digest selects `CanonicalCodecMismatch`. A non-normalized or out-of-root output
path selects `OutputCreationFailed`. These are the RFC 0010 failure kinds
already defined for the object pipeline; this RFC reuses those kinds and binds
them to the new post-object phases it owns.

### Toolchain Discovery Record

The verified runtime closure and link plan bind their filesystem inputs through
one immutable `ToolchainClosure` record per selected target. The record is a
data contract only; this RFC defines its shape and provenance discipline, and
does not require object emission to exist to freeze that shape. It carries
exactly the following fields:

1. `targetSpecificationIdentity`: the RFC 0016 target specification identity the
   closure is bound to. A closure is valid for one target only.
2. `sysroot`: one normalized absolute target root directory. On Linux this is
   the sysroot; on macOS it is the SDK root supplied as the equivalent of
   Clang's `-isysroot`. The record never carries more than one root.
3. `linker`: one `LinkerDriver` alternative (`ElfDriver` or `MachODriver`), the
   normalized absolute path of the driver program, and that program's recorded
   executable digest and byte count.
4. `crtObjects`: the ordered, deduplicated set of startup and finalization
   objects the platform requires (for example the ELF `crt1`/`crti`/`crtn`
   family or the Mach-O equivalent), each as a normalized absolute path plus its
   recorded digest and byte count.
5. `defaultLibraries`: the ordered, deduplicated set of default system libraries
   the target link mode requires (for example the platform C runtime library),
   each recorded by canonical link name and, when linked from a fixed path
   inside `sysroot`, its digest and byte count.

The record carries no environment field. The link runs under a strictly empty
process environment (see "Linker Driver Invocation"), so there is no target-owned
variable set to record or fold into `LinkPlanId`. A future target-owned
environment set is out of scope here and would be introduced together with its
configuration source, toolchain discovery, and `LinkPlanId` codec fold in a
separate change, never as an unproduced record field.

Every path field must be normalized, absolute, and contained inside `sysroot`
except the `linker` program, whose parent is recorded and pinned. The record is
sorted by its complete canonical keys, contains no duplicate path, role, or
symbol, and is folded into `LinkPlanId` through the same domain-separated,
length-framed encoding as the object and runtime records.

The closure is *supplied*, not *ambient*. It is provided explicitly through the
package compilation configuration (an explicit target-toolchain configuration
key), mirroring RFC 0016's `LLVM_DIR` discipline: RFC 0016 rejects an unset root
and forbids any `PATH`, `CMAKE_PREFIX_PATH`, environment-hint, package-registry,
or sibling-install fallback for the build-host LLVM package (RFC 0016 "LLVM
build and CI contract"). RFC 0043 applies the identical fail-closed rule to the
*target* toolchain: an unset or non-existent `sysroot`, an absent or
digest-mismatched `linker`, a missing required CRT object, or any attempt to
resolve an input through `PATH`, `SDKROOT`, `LIBRARY_PATH`, `LD_LIBRARY_PATH`,
`DYLD_LIBRARY_PATH`, or a linker search variable rejects closure construction
before any tool runs. There is no host-default probe and no ambient search;
discovery of the concrete root is a configuration responsibility outside the
verified compiler, exactly as CMake supplies `LLVM_DIR` today.

### Linker Driver Invocation

The selected target authority names exactly one `LinkerDriver` alternative:
`ElfDriver` for Linux ELF or `MachODriver` for macOS Mach-O. The plan expands
that alternative to an ordered argument vector without invoking a shell,
performing glob expansion, interpreting response files, or reading user
configuration files. The driver program must be in the verified toolchain
closure and its executable digest must equal the planned record.

The process environment is strictly empty: the driver is spawned under an
empty-environment policy that inherits no variable from the parent process.
`PATH`, `LIBRARY_PATH`, `LD_LIBRARY_PATH`, `DYLD_LIBRARY_PATH`, `SDKROOT`, and
linker search variables are therefore never visible to the driver. The working
directory is the transaction root described below, so no input path can be
resolved relative to the current directory. Should a target ever require a
specific environment variable, that target-owned set would be introduced with
its own configuration source, toolchain discovery, and `LinkPlanId` codec fold;
until then the invocation environment is empty by construction.

The first implementation invokes the platform compiler driver rather than a
bare linker so that the target's startup objects and platform-required link
mode remain part of the recorded toolchain closure. The plan records no raw
user flags.

The link runs inside a single **unified transaction root**: the transaction-
private snapshot tree that already holds every re-verified input copy (the
input-side TOCTOU defense) is also the parent of the linker output. The driver,
the immutable input snapshots, and the output candidate all live under one
`.zomlink-<token>/` directory that shares one transaction id, one exact
directory identity, and one cleanup obligation. The linker's `-o` argument names
`<root>/output-candidate`; the linker never writes to a public final path and
never writes to a sibling temporary in the final directory. An unrecognized
driver result, nonzero exit status, missing output, output digest mismatch, or
input-revision mismatch cleans **only the transaction root** and never touches a
public final path (there is no best-effort final-path removal). A pre-existing
final path is still rejected up front (`RejectExisting`), and this transaction
never creates, replaces, or removes the public final path — the D1 publication
transaction is the sole writer of the final path. It does not publish a manifest
or executable claim.

### Executable Verification And Publication

The linker result is not a public artifact when the driver exits. It is a
`LinkedOutputCandidate`: a move-only capability that owns the still-live
transaction root, the moved-in `VerifiedLinkPlan`, and a transaction-owned
read-only handle to `<root>/output-candidate`. Before returning the candidate,
the link step confirms the output entry is a regular, non-empty file the
transaction exclusively owns: the entry is inspected without following a symlink
(`O_NOFOLLOW` / `fstatat(..., AT_SYMLINK_NOFOLLOW)`), its exact file identity
(`StableFileIdentity`: device + inode) is captured from the held handle, and the
handle's `st_nlink` must equal `1` so no external path can rewrite the inode in
place. A symlink, directory, empty file, multiply-linked inode, or an inability
to capture the exact identity fails closed. No format, architecture, or symbol
check happens at this stage; the candidate is deliberately not yet "verified".

The candidate is accepted only when an independent executable verifier checks
the output format, machine architecture, entry symbol, required runtime symbols,
and absence of unresolved ZOM runtime references against the verified link plan,
reading through the candidate's handle and re-computing the digest, byte count,
and exact identity from that same output object. The first supported checks are
ELF and Mach-O only. The verifier does not infer safety from a successful linker
exit status.

The `VerifiedLinkPlan` carries one closed `ExecutableInspectionProfile`; the
opaque target-specification digest alone is not inspection authority. The
profile contains the expected object format, one `ExecutableMachine`
(`X86_64` or `AArch64`), a 64-bit pointer width for the initial matrix, a
strictly ordered duplicate-free sequence of required runtime symbol names, and
the ZOM runtime ABI reference domain. The plan verifier requires `ElfDriver`
with ELF and `MachODriver` with Mach-O, and folds the complete profile into
`LinkPlanId`. The entry symbol remains the one separate required plan field and
is also checked as a defined symbol.

All symbol names in the profile and plan - the required runtime symbols, the
runtime reference domain, and the entry symbol - are recorded in the target's
raw symbol-table spelling, not the source-level spelling. The same raw bytes are
passed to the format-specific linker entry option (`ld -e`) and matched in the
symbol table, so the driver and inspector never disagree. ELF uses the C name
unchanged (a C `zom` entry stays `zom`, the runtime imports are `__zom_*`);
Mach-O's nlist prepends a leading underscore (a C `zom` entry appears as `_zom`,
the runtime imports as `___zom_*`). The runtime reference domain is that raw
prefix - `__zom_` on ELF, `___zom_` on Mach-O - derived strictly from the object
format; it is never empty and never caller-chosen, so the check can be neither
disabled nor misdirected.

ELF inspection requires a little-endian ELF64 `ET_EXEC` or `ET_DYN` image whose
`e_machine` matches the profile (`EM_X86_64` or `EM_AARCH64`), whose header and
section ranges are bounded by the candidate bytes, and whose symbol tables
contain defined entry and required runtime symbols. Mach-O inspection requires a
64-bit `MH_EXECUTE` image with matching `cputype`, bounded load commands and
`LC_SYMTAB`, and defined entry/runtime symbols in the bounded nlist/string-table
inventory. Every symbol record's name offset is bounded and NUL-terminated
inside the string table before any binding/type classification, regardless of
the symbol's role. A defined symbol matches the required set; an undefined
global/weak (ELF `SHN_UNDEF`) or external `N_UNDF` (Mach-O) symbol whose raw
name begins with the runtime reference domain is an unresolved runtime reference
and is rejected, while a non-domain external import (a C library symbol) is
allowed. An absent, undefined-runtime, duplicate, malformed, or out-of-range
required symbol fails `ExecutablePublication` with `InvalidFact`; a
format/machine/pointer shape mismatch uses `InvalidAbi`. A leading magic that
identifies a different supported object format, or the same format with the
wrong bitness or endianness, is an `InvalidAbi` shape mismatch; unrecognizable
or structurally broken bytes remain `InvalidFact`.

`PublishedExecutableArtifact` owns the normalized final destination, target
identity, executable digest, byte count, `LinkPlanId`, and the immutable
`ExecutableArtifactManifest`. The manifest is a canonical, domain-separated
encoding of that data plus the ordered input artifact digests and toolchain
identity. It is published beside the executable using the fixed suffix
`.zom-artifact`; the suffix is a product artifact name, not an internal
revision identifier.

Publication is a **recoverable transaction with the manifest as its sole commit
marker**, not a two-file atomic rename. The `output-candidate` staged in the
transaction root is renamed to the final executable path first; the manifest is
staged, fsynced, and renamed second, and the manifest rename is the commit
point. A consumer never accepts an orphan executable: an executable with no
sibling `.zom-artifact` manifest whose codec verifies and whose recorded digest,
byte count, and `LinkPlanId` match is not a published artifact. Immediately
before the executable rename, the transaction writes and fsyncs a journal entry
recording the owner token, the final path, and the output's exact
`StableFileIdentity`, digest, size, regular-file shape, and `st_nlink == 1`
re-derived from the candidate's *same* held handle at the commit instant (the
earlier inspection does not substitute for this commit-point re-check). Recovery
deletes an orphan final executable only when the journal proves this transaction
created it and the final path still resolves to that exact identity; a platform
that cannot provide a stable file identity fails closed and retains the orphan
for explicit repair. Existing final paths are never replaced. A failure before
the commit point cleans only the transaction root; the single genuinely
ambiguous state — a directory sync failure after the manifest rename — returns a
distinct recovery-required outcome and never blind-deletes the possibly
committed pair.

The operation surface is two steps. The link step produces the candidate and
consumes the plan:

```text
linkExecutable(
  plan: Moved<VerifiedLinkPlan>,
  filesystem: Borrowed<const Filesystem>,
) -> CleanupAwareOutcome<LinkedOutputCandidate>
```

and the consuming operation chains inspection, manifest construction, and the
D1 publication transaction, returning an explicit three-way outcome so the
post-manifest-rename ambiguity is never forced into a plain rejection:

```text
linkAndPublish(
  plan: Moved<VerifiedLinkPlan>,
  filesystem: Borrowed<const Filesystem>,
) -> LinkAndPublishOutcome
```

`LinkAndPublishOutcome` is exactly one of `Published(PublishedExecutableArtifact)`,
`RecoveryRequired(LinkRecoveryRequired)` (a snapshot-cleanup obligation paired
with the preserved primary rejection, or the ambiguous post-manifest-rename
token that must not be blind-deleted), or `Rejected` (an ordinary failure whose
cleanup succeeded). The input plan is consumed on every branch. The result owns
no repository pointer, mutable session state, or borrowed toolchain handle. The
D2 `VerifiedSysroot` is a toolchain-discovery capability consumed before a
`VerifiedLinkPlan` exists, so neither operation re-takes it.

### Host Execution

`zomc run` accepts only a newly produced `PublishedExecutableArtifact`. It
compares the artifact target with a host execution profile constructed from the
same target registry authority. The operating system, CPU architecture, object
format, pointer width, and required execution ABI capabilities must all match.
Otherwise `run` reports the existing target-selection diagnostic and does not
spawn a process. It never falls back to emulation, Rosetta, QEMU, an ambient
interpreter, or a different executable.

Arguments are passed as an argument vector. The program inherits only the
explicit runtime environment authorized by the package execution request. The
run command reports the child exit status without reclassifying it as compiler
success or failure.

### Linker And Publication Failure Algebra

RFC 0010's `IrFailurePhase` is a closed enum whose tags run `0x01` through
`0x10` and end at `ObjectEmission` and `FeatureBoundaryVerification`; its
`BackendOperation` enum ends at `EmitObject`, and its only object-stage
capability kind is `OutputCreationFailed`, defined as failure to create the
requested object output (RFC 0010 "IrFailurePhase"/"BackendOperation" and the
`ObjectEmission` failure row). None of those rows models a linker subprocess,
so no accepted upstream row covers a linker-process failure today.

RFC 0043 owns extending the failure algebra. Extending an accepted internal
contract by a new RFC is the sanctioned pattern: RFC 0010 states that new
stages register their own phases and rows rather than overloading unrelated
ones, and reserves `FeatureBoundaryVerification` as the *only* source-rejecting
seam so every other stage keeps using `IrOperationResult`. RFC 0043 stays inside
`IrOperationResult` and adds three closed phases at the next free tags,
`LinkPlanConstruction`, `LinkerInvocation`, and `ExecutablePublication`,
extending the tag range to `0x13`. It adds no new `IrFailureKind` and no new
diagnostic family; it reuses the existing kinds bound to new phases, and adds
one `BackendOperation` alternative `InvokeLinker` (tag `0x0b`) so a linker
subprocess failure carries a `Backend` site like every other backend operation.

The rows RFC 0043 adds are:

| Phase | Result and kinds | Owner / site | Detail |
|---|---|---|---|
| `LinkPlanConstruction` | `CapabilityRejected`: `OutputCreationFailed`; `IrInvariantRejected`: `InputRevisionMismatch`, `MissingRequiredFact`, `AdditionalFact`, `InvalidFact`, `InvalidAbi`, `CanonicalCodecMismatch` | `Session` / `{None, Backend}` | `None` |
| `LinkerInvocation` | `CapabilityRejected`: `OutputCreationFailed`; `IrInvariantRejected`: `InputRevisionMismatch`, `InvalidFact`, `CanonicalCodecMismatch` | `Session` / `{None, Backend}` | `None` |
| `ExecutablePublication` | `CapabilityRejected`: `OutputCreationFailed`; `IrInvariantRejected`: `MissingRequiredFact`, `InvalidFact`, `InvalidAbi`, `CanonicalCodecMismatch` | `Session` / `{None, Backend}` | `None` |

The three named linker-process failures map as: a linker subprocess that exits
nonzero, or that cannot be spawned from the verified closure, is
`OutputCreationFailed` under `LinkerInvocation` with a `Backend { operation:
InvokeLinker }` site (the requested link output could not be produced); a
missing or empty link output after a zero exit is `OutputCreationFailed` under
`LinkerInvocation`; and a malformed executable that fails format, machine,
entry-symbol, or runtime-symbol inspection is `InvalidFact` (or `InvalidAbi`
for an ABI-shape mismatch) under `ExecutablePublication`. The failing branch
consumes its input and cleans the transaction root (before the commit point) or
returns the recovery-required outcome (in the one ambiguous post-manifest-rename
window), matching the object pipeline's fail-closed discipline. RFC 0043
registers the corresponding `ZOM99xx` invariant diagnostics for these phases
through the existing driver mapping without creating a new diagnostic family.

## Repository Impact

| Area | Paths | Owner |
|---|---|---|
| RFC governance | `docs/rfc/**` | rfc |
| Object-to-executable pipeline and CLI | `compiler/lir/**`, `compiler/backend/**`, `compiler/ir/**`, `utils/zomc/**`, `compiler/CMakeLists.txt` | ir-backend |
| Package session, target capability, and artifact requests | `compiler/driver/**`, `compiler/identity/**` | module-system |
| Runtime closure and platform ABI records | `runtime/**`, `core/**`, `compiler/ownership/**` | runtime-memory |
| Failure materialization | `compiler/diagnostics/**` | error-system |
| Tests, architecture gates, and CI | `tests/**`, `scripts/**`, `.github/workflows/**` | verification |

## Security And Safety Impact

Linking and execution cross a process and filesystem boundary. The plan avoids
shell injection by using argument vectors, prevents ambient library injection
by sanitizing the environment, verifies every input digest, rejects output
replacement, and records the selected toolchain identity. A cross-target
artifact is never executed solely because it was published successfully.

The output directory is a Unix security boundary for publication journals. On
Unix, publication and recovery require a directory owned by the effective user
and reject any directory writable by group or other principals. The journal's
domain-separated checksum detects truncation, torn data, and accidental record
mixing; it is not an authentication mechanism against another process running
as the same effective user. Processes under one effective user are one trust
principal and can already read, replace, or execute that user's artifacts.
Concurrency guarantees therefore cover independent publishers following the
filesystem contract, while a malicious same-principal process is handled as
external tampering: final consumers reverify the manifest and executable and
never infer publication from a bare path.

The initial platform matrix is intentionally closed. Unsupported object
formats and target profiles are rejected before tool invocation. Later support
requires a single replacement update of the target, verifier, tests, and
documentation; it must not add a permissive fallback path.

## Drawbacks And Risks

- Verified runtime closure requires more toolchain metadata than a direct
  invocation of `cc`, increasing initial implementation work.
- Recoverable manifest-committed publication must handle platform-specific
  rename, fsync, and crash-orphan recovery details correctly.
- macOS SDK and linker availability differ across hosts, so the toolchain
  closure must be discovered and verified rather than assumed.
- The first target matrix intentionally excludes Windows and dynamic-linking
  product modes, which limits early user coverage.

## Alternatives Considered

Invoking `cc` with host-default arguments is rejected because it silently uses
host startup objects, libraries, and search paths for a cross target.

Calling a bare linker is rejected for the first implementation because startup
object and platform driver behavior would be unverified implicit inputs.

Publishing a raw executable path without a manifest is rejected because later
`run`, debugging, and distribution commands could not independently identify
its target, runtime closure, or producing plan.

Allowing `run` to attempt a foreign executable is rejected because a successful
link does not establish host executability.

## Compatibility And Rollout

This is a clean new post-object stage. The current `zomc run` rejection is
deleted only in the transaction that implements the verified executable path.
There is no compatibility flag, alternate command path, or legacy artifact
format. Existing `compile` behavior remains object-only until RFC 0021 and
this RFC are accepted and implemented.

The rollout order is target authority binding, runtime closure verifier, link
plan/verifier, driver invocation, executable verifier/publication, then the
host-compatible `run` cutover. Each stage deletes the superseded temporary
surface in the same change.

## Documentation And Teaching Plan

- Document executable outputs, target support, and host-only `run` behavior in
  the `zomc` user documentation.
- Add an IR design document after the production builder, independent verifier,
  publisher, and tests exist.
- Update the build and release documentation with Linux ELF and macOS Mach-O
  toolchain prerequisites after those platforms are implemented.
- Keep source-language specification unchanged because this RFC adds no syntax
  or semantic rule.

## Operational Readiness

CI must maintain native Linux and macOS lanes for each supported architecture
where available. Each lane records the selected toolchain identity and runs
the produced executable. Cross-target lanes publish artifacts and inspect them
without attempting execution. The compiler records structured linker stderr
only in the diagnostic payload, with output size limits and redaction of
unapproved host paths.

### CI Architecture Lane Matrix

RFC 0016 fixes the compiler build-host runner labels `macos-15` and
`ubuntu-24.04` and the code-generation backend set to LLVM `X86` and `AArch64`
(RFC 0016 "LLVM backend admission" and "LLVM build and CI contract"), but
commits to no native execution lane. RFC 0043 decides that matrix concretely
and minimally, grounded in what those fixed runners can natively execute and
the `X86`/`AArch64` target set:

| Lane | Runner | Native target | Mode |
|---|---|---|---|
| Linux x86_64 | `ubuntu-24.04` (x86_64) | `x86_64` ELF | Publish and **execute** |
| macOS arm64 | `macos-15` (Apple silicon, arm64) | `aarch64` Mach-O | Publish and **execute** |
| Linux aarch64 | `ubuntu-24.04` x86_64 host | `aarch64` ELF | Cross-publish and **inspect only** |
| macOS x86_64 | `macos-15` arm64 host | `x86_64` Mach-O | Cross-publish and **inspect only** |

The two execution lanes cover one native architecture on each supported
operating system: `x86_64` execution on the x86_64 `ubuntu-24.04` runner and
`aarch64` execution on the Apple-silicon `macos-15` runner. Together they run a
produced native executable on both `X86` and `AArch64` at least once, so the
full backend architecture set is exercised by execution without provisioning a
Linux `aarch64` execution runner that RFC 0016 does not fix. The remaining two
combinations are cross-published and inspected by the format-specific verifier,
never run, and `zomc run` must reject them before process creation. Adding a
native Linux `aarch64` execution runner later is a single additive lane change
and requires no fallback or emulation path.

RFC 0043 owns updating `.github/workflows/**` to add these lanes when it
implements; it does not require RFC 0016's build contract to change.

## Acceptance Criteria

- RFC 0016 is accepted and RFC 0021 reaches an accepted object-emission
  contract with an implementation pointer before any code from this RFC lands.
- Owners approve the complete Linux ELF and macOS Mach-O toolchain closure,
  driver argument, runtime artifact, and failure contracts.
- An independent verifier rejects every mutated target, digest, runtime,
  toolchain, argument, output-policy, and manifest record.
- Native Linux and macOS tests produce, inspect, and execute a minimal ZOM
  executable for each supported native architecture.
- Cross-target tests publish and inspect an artifact but prove `zomc run`
  rejects it before process creation.
- Linker failure, malformed executable, missing runtime input, and failed
  publication leave no final executable or manifest.
- Repeated equivalent builds produce byte-identical plans and manifests.
- Sanitizer, unit, lit, architecture, RFC, English-only, format, and full
  default CTest gates pass on all supported host lanes.

## Implementation Plan

1. Bind the accepted RFC 0016 target authority and RFC 0021 verified object
   artifact contract.
2. Implement verified runtime closure discovery and its independent verifier
   for the closed Linux ELF and macOS Mach-O matrix.
3. Implement canonical link-plan construction, independent verification, and
   mutation tests without invoking a linker.
4. Implement target-selected driver invocation into a unified transaction root
   with transaction-root-only cleanup.
5. Implement ELF and Mach-O executable inspection, manifest construction, and
   recoverable manifest-committed publication.
6. Replace the current `zomc run` rejection with the host-compatibility-gated
   execution path and update documentation and CI.

## Test Plan

- Build: `cmake --preset sanitizer`; `cmake --build --preset sanitizer`.
- Unit tests: link-plan codec/verifier, runtime closure, linker invocation,
  executable verifier, publication cleanup, and host-execution admission.
- Lit tests: compiler target selection, linker failure diagnostics, executable
  output requests, and cross-target run rejection.
- Conformance: minimal Linux ELF and macOS Mach-O executable fixtures plus
  format-specific artifact inspection.
- Generated files: regenerate target and artifact test fixtures only through
  checked-in project tooling.
- Architecture: target registry, IR, compiler-session, package, ownership,
  English-only, and internal-versioning checks.
- Complete tests: `ctest --preset default --output-on-failure` on native host
  lanes.
- Format: `python3 scripts/check-format.py`; `git diff --check`.
- RFC: `python3 scripts/check-rfc.py`.

## Open Questions

None

## Status History

| Date | Status | Notes |
|---|---|---|
| 2026-08-15 | DRAFT | Initial post-object link and executable-publication contract created from RFC 0021's explicit non-goal boundary. |
| 2026-08-27 | REVIEW | Authored the toolchain-discovery record, the linker and publication failure algebra extending RFC 0010, and the CI architecture lane matrix; cleared all three Open Questions and bound discussion/tracking links. |
| 2026-08-28 | ACCEPTED | All five dependency RFCs (0006, 0010, 0012, 0016, 0021) are IMPLEMENTING; verified the RFC 0010 failure-algebra extension (LinkPlanConstruction/LinkerInvocation/ExecutablePublication phases + InvokeLinker op) adds three phases past ObjectEmission with no name collision and invents no diagnostic code; all six required owners approved. Acceptance approves the design only; the native-executable acceptance-criteria evidence is a LANDED gate. implementation stays TBD; no IMPLEMENTING pointer (backend object emission and linking are unbuilt). |
| 2026-08-28 | IMPLEMENTING | First authorized slice landed as evidence (Implementation Plan step 3, "without invoking a linker"): the closed failure algebra was extended in code (`IrFailurePhase` LinkPlanConstruction/LinkerInvocation/ExecutablePublication = 0x11/0x12/0x13, `BackendOperation::InvokeLinker` = 0x0b), and `compiler/ir/link-plan-codec.{h,cc}` implements the `ToolchainClosure`/object/runtime/argument records, the domain-separated length-framed `LinkPlanId` codec, and the independent `LinkPlanVerifier` enforcing the six numbered invariants, each rejection mapped to a `LinkPlanConstruction` failure row. A deterministic minimal-plan oracle plus a fail-closed mutation matrix pass under the frontend sanitizer build (no linker, no filesystem, no LLVM linkage). Runtime-closure discovery, driver invocation, executable verification/manifest publication, and the host-gated `zomc run` cutover remain Pending. |
| 2026-08-29 | IMPLEMENTING | Contract refinement. Adversarial review of the D3b snapshot slice found that the free-form `LinkerArgumentRecord` surface let a verified plan carry raw paths, response-file tokens, and search-path flags, bypassing the input-snapshot discipline and contradicting this RFC's own Non-Goals. Because there is no production producer of the argument surface, the "Inputs And Link Plan" contract was changed to remove the generic argument surface entirely: the plan stores only closed structural authorities and the driver derives its canonical argument vector from them. The codec/verifier/oracle deletion is Slice 2 (Pending as of this row); this row records the approved contract, not yet its landed implementation. A future target-selected link policy (LinkMode, PIE) is added as its own closed structural field folded into `LinkPlanId`, never as a generic argument list. |
| 2026-08-29 | IMPLEMENTING | Argument-surface removal landed (`320a2a01`, D3b Slice 2). `compiler/ir/link-plan-codec.{h,cc}` no longer defines `LinkerArgumentRecord`, `ExecutableLinkRequest::argumentRecords`, `VerifiedLinkPlan::argumentRecords()`, the argument sequence in the codec preimage, or the argument verifier invariant; `compiler/ir/invoke-linker.cc` derives the driver argv from the closed structural fields only. The `link-plan-codec-oracle-test` oracle was regenerated from the live encoder: the preimage shrank from 503 to 469 bytes (the argument segment removed with no count placeholder) and the `LinkPlanId` became `8e9a5cf7...5d60ca74`. All IR unit tests pass under the sanitizer build; `check-rfc` and `check-format` pass. Runtime-closure discovery, driver invocation, executable verification/manifest publication, and the host-gated `zomc run` cutover remain Pending. |
| 2026-08-29 | IMPLEMENTING | Publication-and-output contract refinement (approved design, implementation Pending). A 2026-08-29 adversarial review of the first InvokeLinker slice found four unresolved gaps that leaf patches cannot close: non-atomic two-file publication with a best-effort `tryRemove` rollback, discovery binding no identity to the read root, an executed driver not provably the digested driver, and the linker writing the final path directly. The "Linker Driver Invocation" and "Executable Verification And Publication" sections are refined to: a unified transaction root (the D3b snapshot tree also parents the linker output at `<root>/output-candidate`, cleanup touches only the root), a move-only `LinkedOutputCandidate` that owns the still-live root, the moved-in plan, and a no-follow read-only output handle with a captured `StableFileIdentity` and `st_nlink == 1` sole-link proof; a two-step `linkExecutable -> CleanupAwareOutcome<LinkedOutputCandidate>` plus a consuming `linkAndPublish -> LinkAndPublishOutcome` (`Published` / `RecoveryRequired` / `Rejected`); and recoverable manifest-committed publication whose manifest commit is the sole visibility marker, with a pre-rename journal recording the commit-point identity re-derived from the candidate's same handle. `VerifiedExecutableArtifact` is renamed `PublishedExecutableArtifact`. The reviewable shape is `docs/design/ir/link-publication-transaction.md`; this row records the approved contract, not landed code (D4/D1/D5 remain Pending). |
| 2026-08-29 | IMPLEMENTING | D4 landed (`3764b2cb` + review-fix `8faec9da`): the transaction-owned output candidate. `linkExecutable` now takes the `VerifiedLinkPlan` by value (every branch consumes the moved plan) and writes the driver's `-o` to `<root>/output-candidate` inside the unified transaction root (the D3b snapshot tree), never a public final path; the driver's working directory is the transaction root. On success it transfers the still-live root into a move-only `LinkedOutputCandidate`; every rejection cleans only the transaction root through `discardAndCleanup` and never touches the final path (the prior best-effort final-path `tryRemove` is deleted). The candidate captures, all from one no-follow (`O_NOFOLLOW`) read-only handle, the output's exact `StableFileIdentity` (`dev`/`ino` + link count), byte count, and SHA-256 `outputDigest` (read+hashed through that handle with a read-length cross-check), and enforces the D4-stage structural invariant: regular file, non-empty, `st_nlink == 1`. INV-1 rejects any pre-existing final-path entry up front with a no-follow `tryLstat` (a following `exists` would miss a broken symlink). `VerifiedLinkedExecutable` was replaced by `LinkedOutputCandidate`; `VerifiedExecutableArtifact` was renamed `PublishedExecutableArtifact`. These are structural invariants only - format/architecture/entry-symbol/runtime-symbol inspection remain D5. Twenty-five `invoke-linker` unit tests (symlink/empty/directory/hardlink/missing output-candidate variants plus a broken-symlink final-path case, each red-before-green) pass under the sanitizer build; full sanitizer build, `check-format`, and the IR architecture gate pass. D1 (manifest-last recoverable publication + INV-8 commit-point re-check) and D5 (consuming `linkAndPublish`) remain Pending; `zomc run` stays blocked and the link-driver spine stays `[~] partial`. D1 MUST re-derive the output `digest`/`size`/`dev`/`ino`/`st_nlink == 1` from the candidate's same handle at its commit point; the D4-captured snapshot is not the final proof. |
| 2026-08-29 | IMPLEMENTING | D1 publication-transaction contract ratified (docs-first; implementation Pending). The design note's D1 section now pins the full manifest-last recoverable-publication contract into an implementable shape: the operation `publishLinkedOutput(Moved<LinkedOutputCandidate>, Moved<ExecutableArtifactManifest>) -> PublicationOutcome` with the three-way outcome `Published(PublishedExecutableArtifact)` / `RecoveryRequired(PublicationRecoveryObligation)` / `Rejected(PublicationRejection)`; a seven-step ordered commit (RejectExisting no-follow re-check for both final paths -> commit-point re-derivation of digest/size/dev/ino/regular/`st_nlink == 1` from the candidate's SAME held handle -> stage+fsync manifest temp -> journal write+fsync BEFORE any rename -> executable rename + final-dir fsync -> manifest rename (the commit point) + final-dir fsync -> discard journal + snapshot root); a journal lifecycle bounded to steps 4-7 whose sole authority is to permit deleting an orphan `app` under a journal + exact-identity match; a pointwise crash-recovery matrix keyed by the last completed step, with the post-manifest-rename pre-dir-fsync window as the single ambiguous outcome (INV-5) that returns `RecoveryRequired` and never blind-deletes; and a strict separation of the two typed recovery obligations - `SnapshotCleanupObligation` (transaction-private `.zomlink-` root) versus the new `PublicationRecoveryObligation` (public final pair, carrying the owner token, both final paths, the journalled commit-point `StableFileIdentity`, and an optional nested `SnapshotCleanupObligation`). This row records the approved contract only; no D1 code lands here. D5 (consuming `linkAndPublish` + executable inspector) also remains Pending; `zomc run` stays blocked and the link-driver spine stays `[~] partial`. |
| 2026-08-29 | IMPLEMENTING | Correction to the previous row. That row described the D1 publication-transaction contract as "ratified"/"approved" (`af61f534`); that was a premature, pre-review status - the contract had been committed for review but NOT yet reviewed, and a 2026-08-29 adversarial review then rejected it with seven blockers (rename+fsync compound-step crash states, journal-delete-before-root-cleanup ordering, journal established too late to cover steps 1-3, a three-way outcome that cannot express a snapshot-only cleanup debt, missing manifest<->candidate live-binding verification, non-exclusive final renames, and an unspecified journal format). The previous row's bytes are left immutable as an audit record; its "ratified/approved" wording is corrected here to **D1 contract PROPOSED, pending adversarial review**. No D1 code was or is authorized. The revised contract is being reworked in `docs/design/ir/link-publication-transaction.md`; a later row will record "approved" only after the revision passes review. D4 stays landed; D1/D5 Pending; `zomc run` blocked; spine `[~] partial`. |
| 2026-08-29 | IMPLEMENTING | D1 trust boundary clarified and enforced: on Unix the output directory must be owned by the effective user and must not be writable by group or other principals. Journal checksums are corruption and chain-integrity evidence inside that principal boundary, not authentication against a malicious same-UID process, which already has authority to mutate the user's artifacts. Publication and recovery fail closed on an untrusted directory; focused tests retain every final entry. |
| 2026-08-30 | IMPLEMENTING | D1 contract revised again (docs-only, still PROPOSED, pending re-review; no code). A second adversarial pass found five crash-consistency blockers, all addressed: (1) a crash before the first durable journal leaves a `.zomlink-<token>` root with no ownership proof - resolved by Option B (approved): such pre-`Started` roots are explicit-repair-only and NEVER auto-removed; D1's owner-safe recovery holds only from `Started` onward (INV-7 + the matrix `none` row + the future repair command's acceptance). (2) The journal stage set gains `ManifestStaged` between `Started` and `ExecCommitted`, removing the contradiction that `Started` recorded the identity of a not-yet-created manifest temp; `Started` records only the temp path formula + expected digest, and the manifest-temp exact identity plus the commit-point output identity are captured at `ManifestStaged`. (3) The journal is a chain of IMMUTABLE per-stage records `journal.<token>.<stage>` (each `PRIVATE`+`O_NOFOLLOW`+`RENAME_NOREPLACE`+dir-fsync, never overwritten) linked by a `previousJournalId` hash chain; recovery selects the highest complete, checksum-valid, chain-consistent stage and fails closed on a broken/forked chain - replacing the single-path replace, which had no identity-conditional atomicity. (4) The commit-point re-derivation moves to immediately before the executable rename (re-read from the candidate's same held handle, must still equal the `ManifestStaged` record, else abort) so drift during journal fsync never crosses the commit. (5) The crash matrix is a total function of (highest durable stage, actual final entries) with an explicit catch-all (any unlisted/identity-mismatched/broken-chain combination fails closed, retains all, explicit repair); the `app`-only rows are re-labelled definite unpublished orphans (not ambiguous), and only the `ExecCommitted`+both-entries row is publishedness-ambiguous. The previous-row description of a "ratified/approved" D1 contract was already corrected on 2026-08-29; this row records the revised PROPOSED contract, not approval. D4 landed; D1/D5 Pending; `zomc run` blocked; spine `[~] partial`. |
