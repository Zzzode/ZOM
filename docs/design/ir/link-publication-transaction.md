# Link Publication Transaction and Capability Shapes (RFC 0043 refinement, PROPOSED)

Status: PROPOSED (design closure before implementation)
Owner: ir-backend
Feeds: RFC 0043 "Platform Link And Executable Publication"

> This note is a **proposed contract**, not current behavior. It closes the five
> design questions a 2026-08-29 adversarial review raised against the first
> link-driver implementation before any further implementation lands. Nothing
> here is implemented yet; the RFC 0043 reference-level design is updated to
> match once this note is approved. Per `docs/design/ir/README.md` authority
> order, the approved shapes then live in RFC 0043; this file is the reviewable
> proposal.

## Problem

The first link-driver slices (child-process primitive, toolchain discovery,
argv expansion, format verify, InvokeLinker spawn) each passed focused tests but
shared four unresolved contract gaps that leaf patches cannot close:

1. **Publication was not atomic.** Two independent renames (`app`, then
   `app.zom-artifact`) into a shared directory are not an all-or-neither
   transaction on POSIX, and a best-effort `tryRemove` rollback can itself fail
   or delete a concurrent file at the same final path.
2. **Discovery bound no identity to the read root.** `discoverToolchain` took a
   directory capability and a `sysroot` string as two independent inputs;
   nothing proved the bytes read from the capability were the bytes named by the
   recorded absolute path.
3. **The executed driver was not provably the digested driver.** Hashing by
   path then `execve` by path leaves a swap window, and only the driver (not the
   CRT / default-library / object / runtime inputs) was re-verified.
4. **The linker wrote the final path directly**, bypassing the executable
   verifier and manifest publication, so an unverified artifact was visible at
   the final path and could not be rolled back.

## Decisions

### D1. Publication visibility and atomicity: manifest-last commit marker

**Decision:** the `.zom-artifact` manifest is the sole commit marker of a
recoverable publication transaction. The executable and manifest remain siblings
in the caller's shared output directory (the external artifact layout is
unchanged), but atomicity is defined by a recoverable transaction, not by a
single filesystem rename.

Rejected alternative: a dedicated per-artifact directory published by one atomic
`replaceSubdir`. It is truly atomic, but it changes the external layout (the
executable would no longer sit directly in the requested output directory),
breaking the RFC 0043 guide-level contract (`app` in the output directory) and
every consumer's path expectation. The cost of that break outweighs the
simplicity gain.

Transaction contract:

- The transaction owns a unique token (a per-run temporary name prefix) and
  tracks exactly the paths it created.
- The executable is written to a transaction-owned temporary, fsynced, and
  committed to its final path first.
- The manifest is written to a transaction-owned temporary, fsynced, and
  committed to its final path second. **The manifest commit is the transaction
  commit point.**
- **Consumers never accept an orphan executable** — an executable with no
  sibling `.zom-artifact` manifest is not a published artifact. `zomc run` and
  any downstream consumer require the manifest to exist and to reference the
  executable digest.
- On any failure, rollback removes only the paths this transaction created
  (identified by the owner token), and a rollback failure is folded into the
  returned closed result rather than thrown or ignored. A crash between the two
  commits leaves an orphan executable that is (a) never consumed and (b)
  removable by a later run of the same target because it carries no manifest.
- Existing final paths are never replaced.

RFC 0043 reference-level text ("renames the manifest and executable into their
final destinations", "publishes both ... atomically") is corrected to this
recoverable manifest-last semantics; the word "atomic" in the two-file sense is
replaced by "recoverable transaction with the manifest as commit marker".

#### D1 verifiable invariants (acceptance criteria)

These are the invariants the implementation and its tests must establish. They
exist to keep "recoverable" from silently degrading back into "physical two-file
atomic".

- **INV-1 Sole commit marker.** A consumer accepts the executable only when the
  final `.zom-artifact` exists AND its own codec verifies AND its recorded
  executable digest and byte count match the on-disk executable AND its
  `LinkPlanId` matches. A bare `app` with no valid manifest is never a published
  artifact.
- **INV-2 Commit order.** Stage both files and fsync each temporary → rename the
  executable → fsync the directory → rename the manifest (the commit point) →
  fsync the directory. The manifest is never visible before the executable.
- **INV-3 Owner token.** Every temporary and every potential orphan carries a
  non-colliding transaction identity. Cleanup removes only objects this
  transaction can prove it owns; it never deletes an object identified only by a
  public final path.
- **INV-4 Crash-recovery matrix.** The state after a crash is defined pointwise:
  (a) no file; (b) executable only (orphan); (c) both files, directory-sync
  status unknown. The recogniser decides committed-vs-orphan solely by "does a
  complete, verifying manifest exist", never by a prior in-memory return value.
- **INV-5 Ambiguous post-manifest-rename sync failure.** A failure of the final
  directory sync after the manifest rename is the one genuinely ambiguous
  outcome. The operation must NOT both return an ordinary failure AND blind-delete
  the possibly-committed pair. It returns a distinct "recovery-required /
  outcome-pending" result, or defers the decision to the next open of the
  artifact (which applies INV-1/INV-4). If the current failure algebra cannot
  express this state, RFC 0043 gains the contract for it.
- **INV-6 RejectExisting under concurrency.** If a competitor creates the final
  manifest or executable between this transaction's two renames, this
  transaction neither clobbers nor deletes the competitor's file. Tests must
  cover this race.
- **INV-7 Recovery entry point.** The note names who scans for orphans and when
  (next publish to the same target, `zomc` startup, or explicit repair), and the
  safe retain-vs-remove rule for an orphan (an orphan with no manifest is
  removable only by a transaction that can prove ownership or by an explicit
  repair; it is never consumed in the meantime).
- **INV-8 Persistent owner proof after rename.** Once a token-named temporary is
  renamed to the public final `app`, the token is no longer in the filename, so
  "no manifest" alone does NOT authorise deleting that `app` (it could be a
  user's or a competitor's file). The transaction retains a persistent ownership
  proof, chosen as follows:
    - Primary: a **transaction journal** entry written and fsynced BEFORE the
      executable rename, recording the owner token, the final path, and the
      staged object's stable file identity (device + inode). Recovery deletes an
      orphan `app` only when the journal proves this transaction created it AND
      the final path still resolves to that same stable file identity.
    - The staged object's stable file identity is captured while the transaction
      holds the object open, and re-checked (via `fstatat`/equivalent) before any
      delete, so a competitor that replaced the path between rename and cleanup
      is never deleted.
    - A platform or filesystem that cannot provide a stable file identity fails
      closed: the orphan is retained, never blind-deleted, and reported for
      explicit repair.
  Required test: after the executable rename, a competitor replaces/occupies the
  path; cleanup must not delete the competitor's object.

The RFC 0043 goal line "Publish an executable and its manifest atomically, or
publish neither" is reworded to: **manifest-committed recoverable publication —
logical visibility is atomic (nothing is a published artifact until the manifest
commits and verifies), while the physical file set may contain a crash orphan
that is never recognised as an artifact.**

### D2. VerifiedSysroot capability

**Decision:** discovery consumes one `VerifiedSysroot` capability that binds a
directory capability to its canonical absolute identity. There is no separate
`sysroot` string argument; the recorded absolute paths are derived from the
capability's own canonical identity, so the bytes read and the path recorded name
the same object by construction.

Shape (proposed):

- `VerifiedSysroot::open(filesystem, canonicalAbsolutePath) -> Maybe<VerifiedSysroot>`
  opens the directory at the canonical path and retains both the open directory
  capability and the canonical identity. A non-absolute or non-normalized path,
  or a directory that cannot be opened, fails closed.
- Discovery reads inputs through the capability's directory and records
  `capability.identity() + "/" + relativePath`; the two can no longer diverge
  because there is no independent recorded path.

### D3. Stable input and executable handles

**Decision:** the bytes hashed, the bytes the linker reads, and the program
`exec`'d are the same kernel object, established by a stable open handle, not by
re-opening a pathname. Every link input (driver, CRT objects, default
libraries, object records, runtime records) is opened once, digest-verified
against the plan, and consumed through that same handle.

- The driver is executed with `execveat(fd, "", ..., AT_EMPTY_PATH)` on a
  handle opened `O_PATH`/`O_CLOEXEC`, matching the existing precedent in
  `compiler/driver/package/linux-native-sandbox-platform.cc:672`. A platform
  with no equivalent stable-exec primitive fails closed; it never falls back to
  a pathname re-open.
- This requires a small `zc` primitive: open a file as a stable handle and
  `execveat` it. It belongs in `zc` as a generic mechanism (like the
  child-process primitive), with the linker policy staying in the driver layer.
- Every non-driver input is opened once and re-hashed against its
  `LinkInputRecord` digest before the spawn; a mismatch is
  `InputRevisionMismatch` under `LinkerInvocation`.
- **How the verified bytes reach the linker (implementation prerequisite).**
  Re-hashing an open handle is not enough: if the final argv still passes the
  original pathname, the TOCTOU is intact because the linker re-opens by name.
  Before implementing D3, one of these is fixed and recorded here:
    - inherit each input FD into the child and pass `/proc/self/fd/N` (or the
      platform equivalent) as the argv path, so the linker reads the exact
      verified object; or
    - copy the verified bytes into the transaction's own immutable input
      directory (D4-owned) and pass those paths.
  The driver itself always uses `execveat` on its handle (above), independent of
  this choice. A platform with no equivalent FD-path or immutable-input
  mechanism does not silently fail closed while docs claim support: the RFC 0043
  support matrix is updated so the doc and the implementation agree on which
  targets are supported. (The initial slice targets Linux; macOS support in the
  matrix is only claimed once its stable-handle path is implemented.)

### D4. Fresh temporary output capability

**Decision:** the linker writes only to a transaction-owned fresh temporary
output, never to the final path. The `-o` argument names the temporary. Failure
cleanup operates only on the transaction's owner token, never on a public final
path (so it can never blind-delete a concurrent file). The final path is created
only by the D1 publication transaction after verification.

### D5. Consuming link -> inspect -> manifest -> publish operation

**Decision:** a single consuming operation chains the steps; intermediate
value types do not claim "Verified" before their checks run. Its result is an
explicit three-way outcome, not a plain success/failure — because INV-5's
post-manifest-rename sync ambiguity must not be forced into a `rejection`.

`linkAndPublish(plan, sysroot, freshTemp) -> LinkAndPublishOutcome`, where
`LinkAndPublishOutcome` is exactly one of:

- `Published(PublishedExecutableArtifact)` — the manifest committed and verified;
- `RecoveryRequired(PublicationRecoveryToken)` — the INV-5 ambiguous state after
  the manifest rename, carrying the owner token, target paths, and the recovery
  entry point; the pair may or may not be committed and MUST NOT be blind-deleted;
- `Rejected(IrOperationResult rejection)` — any ordinary failure, carrying the
  RFC 0043 row for its phase (`LinkerInvocation` or `ExecutablePublication`).

`RecoveryRequired` is never conflated with `Rejected`: a pending outcome does not
destroy the possibly-committed pair, and a rejection always destroys the temp.
Steps:

1. consume the `VerifiedLinkPlan`;
2. open + digest-verify every input handle (D3);
3. invoke the linker writing to the fresh temp (D4);
4. inspect the produced temp: format, machine architecture, entry symbol,
   required runtime symbols (RFC 0043 `ExecutablePublication` checks) — only
   here is the result named "verified";
5. build the manifest;
6. publish via the D1 transaction (which may return the `RecoveryRequired`
   outcome per INV-5).

An ordinary failure in steps 2-5, or before the executable rename in step 6,
destroys the temp and returns `Rejected`; only a clean commit returns
`Published`; only the INV-5 window returns `RecoveryRequired`. The current
`VerifiedLinkedExecutable` name is removed — the type that carries linker output
before inspection is renamed to a non-"Verified" name (e.g.
`LinkedOutputCandidate`), and only the post-inspection published artifact is
"Verified". If RFC 0010's `IrOperationResult` cannot host a three-way outcome,
`LinkAndPublishOutcome` wraps it as shown rather than overloading a rejection,
and RFC 0043 records the outcome type.

## Implementation order (after this note is approved)

1. `zc` stable-exec-FD primitive (D3 mechanism) + `VerifiedSysroot` capability (D2).
2. Fresh temporary output capability (D4).
3. Rework `linkExecutable` to open+verify all input handles and `execveat` the
   driver handle (D3), writing to the fresh temp (D4).
4. Publication transaction with manifest-last commit + owner-token rollback (D1),
   replacing the best-effort `tryRemove` path.
5. The consuming `linkAndPublish` operation wiring 1-4 with the executable
   inspector and manifest (D5).

Each lands as its own commit with a regression test that is red before and green
after, and is reviewed before the next. `zomc run` stays blocked and the RFC
0043 link-driver spine stays `[~] partial` until the chain is complete.
