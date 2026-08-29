# Link Publication Transaction and Capability Shapes (RFC 0043 refinement, PARTIALLY IMPLEMENTED)

Status: PARTIALLY IMPLEMENTED
  - Landed: D2 (VerifiedSysroot); the D3 / D3b input-open, exec-by-descriptor,
    and transaction-root snapshot foundation; and D4 (transaction-owned output
    candidate).
  - Proposed, pending implementation: D1, D5.
Owner: ir-backend
Feeds: RFC 0043 "Platform Link And Executable Publication"

> This note is part contract, part landed design. The D2 sysroot capability, the
> D3/D3b input-snapshot + exec-by-descriptor foundation, and D4 (transaction-owned
> output candidate) have landed across several reviewed commits; D1 (publication
> transaction) and D5 (consuming operation) remain proposed and are not
> implemented. It closes the design questions a 2026-08-29 adversarial review
> raised against the first link-driver implementation before the remaining
> slices land. Per `docs/design/ir/README.md` authority order, an approved shape
> lands in RFC 0043 first and only then in code; this file is the reviewable
> proposal, and the "Implementation order" section marks which steps are done.

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
    - **Commit-point re-check (not delegated to D5).** Immediately before the
      journal write and executable rename, D1 re-derives the output's exact file
      identity (`StableFileIdentity`: `(dev, ino)`), `digest`, `size`, regular-file
      shape, and `st_nlink == 1` from the *same read-only handle the*
      `LinkedOutputCandidate` *holds* — not from a fresh path open, and not by
      trusting the D4-stage or D5-inspection snapshot. The journal records exactly
      this commit-point identity, so recovery compares the final path against the
      value proven at the instant of rename. D5's inspection re-check (format,
      architecture, symbols, and its own digest/size/identity/link-count read)
      happens earlier in the chain and does **not** stand in for this commit-point
      re-check: an inspection that passed does not prove the object is unchanged
      and singly-linked at the rename instant, so D1 re-proves it from the held
      handle or fails closed.
  Required test: after the executable rename, a competitor replaces/occupies the
  path; cleanup must not delete the competitor's object.

#### D1 operation shape, recovery obligations, and journal lifecycle

This subsection pins the D1 contract into an implementable shape so the code
slice has a fixed target. It is approved design, not landed code.

**Operation shape.** D1 is one consuming operation that takes the D4
`LinkedOutputCandidate` by move (so the still-live transaction root and its
output handle are the sole authority for the bytes being published) plus the
built manifest, and returns an explicit three-way outcome:

```
publishLinkedOutput(
  candidate: Moved<LinkedOutputCandidate>,
  manifest:  Moved<ExecutableArtifactManifest>,
) -> PublicationOutcome

PublicationOutcome =
    Published(PublishedExecutableArtifact)          // manifest committed + verified
  | RecoveryRequired(PublicationRecoveryObligation) // ambiguous/failed, must not be dropped
  | Rejected(PublicationRejection)                  // failed, cleanup succeeded, nothing published
```

Taking the candidate by move means D1 consumes the transaction root: on every
branch it is responsible for discarding the still-live root (via the candidate's
`discardAndCleanup`) once the final pair is committed or the attempt is
abandoned. The candidate is moved in exactly once and cannot be published twice.

**The two recovery obligations are distinct typed values; never conflated.**

- `SnapshotCleanupObligation` (D3b/D4, already landed) concerns the
  transaction-private `.zomlink-<token>/` root: "this transaction-owned tree could
  not be removed." It carries the transaction id, canonical parent, exact
  directory identity, plan id, and the cleanup failure kind/stage. It never names
  a public final path.
- `PublicationRecoveryObligation` (D1, new) concerns the public final pair
  (`app`, `app.zom-artifact`): "a final-path publication reached an ambiguous or
  un-rolled-back state." It carries the owner token, both final paths, the
  committed-point `StableFileIdentity` recorded in the journal, the journal
  location, and a `PublicationRecoveryKind` (see the crash matrix). It never
  deletes by public path without a journal + exact-identity match.

A single `publishLinkedOutput` call can owe at most one of each: it may finish
with a `PublicationRecoveryObligation` (the final-path step was ambiguous) AND,
independently, fail to remove the snapshot root (a `SnapshotCleanupObligation`).
`PublicationRecoveryObligation` therefore carries an optional nested
`SnapshotCleanupObligation`, so a caller receives both un-droppable obligations
in one value and a recovery step adjudicates each against its own proof (journal
+ final-path identity for the publication pair; transaction-id + directory
identity for the snapshot root). D5's `LinkRecoveryRequired` wraps exactly these:
`SnapshotRecoveryRequired { primary, obligation }` for a pre-publication failure
whose only debt is the snapshot root, and the publication obligation for the
post-manifest-rename ambiguous window.

**Canonical ordered commit algorithm.** D1 performs exactly these steps, in this
order; each fsync is mandatory and a failure at step *n* is classified by the
crash matrix as if a crash occurred just after step *n-1*:

1. Re-check RejectExisting for BOTH final paths with a no-follow `tryLstat`
   (INV-1/INV-6); either already present rejects before anything is written.
2. Commit-point re-check: re-derive the output `digest`, `size`, exact
   `StableFileIdentity` (`dev`/`ino`), regular-file shape, and `st_nlink == 1`
   from the candidate's SAME held handle (INV-8); a mismatch fails closed.
3. Stage the manifest into a transaction-owned temporary in the final directory
   and fsync it. (The executable is already staged as the candidate's
   `<root>/output-candidate`; it is committed by rename from the transaction
   root, which shares the final directory's filesystem, so the rename is atomic.)
4. Write and fsync the **journal** entry (owner token, both final paths, the
   step-2 commit-point `StableFileIdentity`) BEFORE any final-path rename.
5. Rename the executable (`<root>/output-candidate` → `app`); fsync the final
   directory. **`app` is now visible but is not yet a published artifact** (no
   manifest).
6. Rename the manifest temporary → `app.zom-artifact` (**the commit point**);
   fsync the final directory.
7. Discard the journal entry (the transaction is complete), then discard the
   snapshot root via the candidate. A failure here is not ambiguous — the pair is
   committed — but a journal/root that could not be discarded is still reported as
   a (non-ambiguous) recovery obligation so it is never silently leaked.

**Journal lifecycle.** The journal entry exists only for the window between step
4 and step 7. Its sole purpose is to authorise deleting an orphan `app` during
recovery: recovery removes `app` only when a journal entry proves this
transaction created it AND `app` still resolves to the journal's recorded exact
identity. After a clean commit the entry is discarded (step 7); a leftover entry
found later means a crash between steps 4 and 7 and drives the recovery matrix.

**INV-4 crash-recovery matrix (pointwise, by the last completed step).** The
recogniser decides committed-vs-orphan solely by "does a complete, verifying
manifest exist" (INV-1), never by an in-memory return value:

| Crash after step | On-disk state | Recogniser verdict | Recovery action |
|---|---|---|---|
| 1-3 | no final pair; maybe a manifest temp + snapshot root (both token-owned) | not published | remove token-owned temporaries + snapshot root; no final path touched |
| 4 | journal exists; no final pair | not published | journal names no committed identity yet (or names the staged id but `app` absent) → discard journal; remove token-owned temporaries |
| 5 | `app` exists (orphan), no manifest; journal present | not published (orphan) | delete `app` only if journal proves ownership AND `app` matches the journal identity; else retain + report |
| 6 (pre-dir-fsync) | both files exist; final-dir durability unknown | **ambiguous** | `RecoveryRequired(PublicationRecoveryObligation)`; MUST NOT blind-delete; next open applies INV-1 |
| 7 | both files exist + durable (committed) | published | discard journal + snapshot root; if that fails, report a non-ambiguous cleanup obligation |

The step-6 row is the single genuinely ambiguous outcome (INV-5): the pair may or
may not be durable, so D1 returns `RecoveryRequired(PublicationRecoveryObligation)`
rather than an ordinary rejection, and never deletes the possibly-committed pair.
Every other row is decidable from disk alone.


logical visibility is atomic (nothing is a published artifact until the manifest
commits and verifies), while the physical file set may contain a crash orphan
that is never recognised as an artifact.**

### D2. VerifiedSysroot capability

**Decision:** discovery consumes one `VerifiedSysroot` capability that binds a
directory capability to its canonical absolute identity. There is no separate
`sysroot` string argument; the recorded absolute paths are derived from the
capability's own canonical identity, so the bytes read and the path recorded name
the same object by construction.

Shape (landed):

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
- **How the verified bytes reach the linker (landed choice).**
  Re-hashing an open handle is not enough: if the final argv still passes the
  original pathname, the TOCTOU is intact because the linker re-opens by name.
  D3b resolves this by **copying the verified bytes into the transaction's own
  immutable input directory** (the `.zomlink-<token>/` snapshot root that D4
  reuses as the unified transaction root) and passing those snapshot paths as the
  input argv tokens, so the linker reads the exact bytes the plan proved and no
  source pathname is re-opened. The **driver** itself is executed directly by its
  snapshot descriptor with `execveat(fd, "", ..., AT_EMPTY_PATH)` (above), so even
  the driver is never re-opened by name. The alternative of inheriting each input
  FD and passing `/proc/self/fd/N` was rejected for the Linux-first slice: the
  transaction-owned immutable copy is the single mechanism that also gives D4 its
  output-candidate parent, keeping one transaction root instead of two rails. A
  platform with no equivalent immutable-input or stable-exec mechanism does not
  silently fail closed while docs claim support: the RFC 0043 support matrix is
  updated so the doc and the implementation agree on which targets are supported.
  (The initial slice targets Linux; macOS support in the matrix is only claimed
  once its stable-handle path is implemented.)

### D4. Fresh transaction-owned output candidate

**Decision:** the linker writes only to a fresh output path *inside the
transaction root*, never to the final path. The snapshot tree built for the input
handles (D3b) is the unified link-transaction root: the driver, the input
snapshots, and the output candidate all live under one `.zomlink-<token>/`
directory and share a single transaction id, exact directory identity, and
cleanup obligation. The `-o` argument names `<root>/output-candidate`. There is
no second output-cleanup rail: a spawn failure, a nonzero exit, a missing output,
or an input-revision mismatch cleans *only the transaction root* and never
touches a public final path (the previous best-effort `tryRemove` of the final
path is removed). A pre-existing final path is still rejected up front (INV-1
no-clobber). This transaction never creates, replaces, or removes the public
final path before D1; when the path is initially absent (the INV-1 case that
proceeds), it remains absent until the D1 commit. Because the root sits under the
final output's parent directory, the eventual commit rename stays within one
filesystem and is atomic.

**D4-stage output invariant.** Before a candidate is returned, D4 confirms that
the `output-candidate` *directory entry* is a regular, non-empty file that this
transaction exclusively owns. An ordinary open follows a symlink and a subsequent
`fstat` would only see the target, so the symlink rejection is done at the
directory-entry level: D4 either opens the output with a no-follow open
(`O_NOFOLLOW` / the `openat2` equivalent), or `fstatat(rootFd, "output-candidate",
..., AT_SYMLINK_NOFOLLOW)`s the entry and confirms it is a regular file whose
exact `(dev, ino)` matches the held read-only handle's. Because a regular file
with matching identity can still be a hardlink to an inode reachable from an
external path (which could rewrite the same bytes in place), D4 additionally
requires the handle's `st_nlink == 1` — the transaction is the sole link to the
inode; otherwise it fails closed. (A future variant may instead copy the linker
result into an exclusive transaction-owned file and take the copy as the
candidate, but the Linux-first slice takes the single-link requirement.) D4 then
computes the output's `digest`, `size`, and exact file identity from that same
handle. A symlink, a directory, an empty file, a multiply-linked inode, or a
failure to capture the exact identity each fails closed. The output identity is a
distinct typed value — `StableFileIdentity`, not the directory-scoped
`StableDirectoryIdentity` — so a file identity is never confused with a directory
identity. No ELF/format, architecture, or symbol check happens here — those are
D5, which re-checks `digest`, `size`, exact identity, and link count from the
same output object before publishing.

**Ownership on success is a transfer, not a publication.** On a clean link,
`linkExecutable` does **not** call `finishAndCleanup`; instead it moves the
transaction-root ownership into a move-only `LinkedOutputCandidate` and returns
it. The candidate — not an orphan on disk — is the sole legitimate owner of the
still-live root. `linkExecutable` also **consumes the plan**: it takes the
`VerifiedLinkPlan` **by value** so the caller must move a move-only plan in and
cannot reuse it after the call on any branch (success or rejection), and so D5's
manifest step needs no second external context assembly. Its signature is:

```
linkExecutable(VerifiedLinkPlan plan /* by value: caller-moved */, filesystem)
    -> CleanupAwareOutcome<LinkedOutputCandidate>
```

Correspondingly, the `CleanupAwareOutcome::Complete` contract is widened from
"the snapshot tree was removed" to "there is no unaccounted cleanup obligation:
either the tree was removed, or its ownership was transferred into the verified
return capability." `RecoveryRequired` still means a cleanup that was attempted
and could not complete.

**`LinkedOutputCandidate` (move-only).** It carries:

- the transaction-root capability plus its token and exact directory identity;
- the moved-in `VerifiedLinkPlan` — its `LinkPlanId`, target/toolchain identity,
  input digests, and the final output request (the path the D1 transaction will
  publish to). The candidate is the single authority for the publication context;
  D5 does not re-assemble it externally.
- a transaction-owned **read-only handle** to `<root>/output-candidate`, which is
  the single physical authority for the linker output, plus the output's exact
  file identity as a typed `StableFileIdentity` — the `(dev, ino)` and link count
  captured from that handle (if the identity cannot be captured, D4 fails closed).
  D1's INV-8 journal records this stable file identity so a recovery step can
  prove the published final file is exactly the object this candidate staged. D4
  also computes the output's SHA-256 `digest` and `size` from that same handle
  (reading through it, cross-checking the read length against the captured size)
  and stores them on the candidate as an **inspection snapshot** — not a second,
  drift-able byte authority: a consumer that needs the bytes reads them through
  the handle, and D1/D5 re-compute the digest/size, exact identity, and link count
  from the same output object at their own checkpoints rather than trusting this
  snapshot.
- its consume/cleanup state, so it cannot be consumed twice.

The name `VerifiedLinkedExecutable` is removed. `LinkedOutputCandidate` is
deliberately not called "verified": `IrOperationResult<LinkedOutputCandidate>::
verified` means only that the link invocation passed the D4-stage invariants (the
driver ran, exited zero, and produced a regular non-empty output in the
transaction root). It does **not** assert that the output's format, machine
architecture, entry symbol, or required runtime symbols were checked — those are
the D5 `ExecutablePublication` checks, and only the post-inspection published
artifact is "Verified".

**Explicit consumption; the destructor is only a last resort.** D4 defines an
explicit consume seam that removes the transaction root and reports only the
resource-cleanup outcome — the candidate does not need to know the caller's
primary result type:

```
discardAndCleanup() && -> CleanupDisposition
CleanupDisposition = Clean | Obligated(SnapshotCleanupObligation)
```

The caller combines this disposition with its own primary rejection: a `Clean`
disposition yields `Rejected(primary)`, and an `Obligated` disposition yields
`RecoveryRequired` carrying both the primary rejection and the
`SnapshotCleanupObligation` (see D5's `LinkRecoveryRequired`). Keeping the primary
out of `discardAndCleanup` avoids threading an arbitrary `IrOperationResult<T>`
through a `CleanupAwareOutcome<T>` whose `T` would not match. A failed caller, and
the D3b/D4 tests, use this seam rather than the bare destructor; the destructor
remains a noexcept best-effort leak guard for a candidate that was neither
consumed nor transferred. After `discardAndCleanup` (or, from D5 on, the
verifier/publisher's consumption), the candidate is moved-from: its handle and
paths can no longer be read and it cannot be consumed again.

### D5. Consuming link -> inspect -> manifest -> publish operation

**Decision:** a single consuming operation chains the steps; intermediate
value types do not claim "Verified" before their checks run. Its result is an
explicit three-way outcome, not a plain success/failure — because INV-5's
post-manifest-rename sync ambiguity must not be forced into a `rejection`.

`linkAndPublish(Moved<VerifiedLinkPlan>, filesystem) -> LinkAndPublishOutcome`.
The `filesystem` argument provides both the input namespace (the plan's recorded
paths resolve under it) and the output namespace (the transaction root and the
final output live on it). The plan is moved in and forwarded to `linkExecutable`,
which moves it into the candidate. The D2 `VerifiedSysroot` is a toolchain-
discovery capability consumed *before* this operation — by the time a
`VerifiedLinkPlan` exists, every input has already been recorded with its path
and digest and is snapshotted under `filesystem`, so `linkAndPublish` reads no
sysroot and does not take one; re-passing it here would be an unconsumed second
authority. (If a later step is found to genuinely need a runtime/sysroot
capability, it is added back with an explicit statement of what it verifies and
which operation consumes it.)

`LinkAndPublishOutcome` is exactly one of:

- `Published(PublishedExecutableArtifact)` — the manifest committed and verified;
- `RecoveryRequired(LinkRecoveryRequired)` — a recovery obligation that must not
  be silently dropped, where

  ```
  LinkRecoveryRequired =
      SnapshotRecoveryRequired { primary: LinkAndPublishRejection,
                                 obligation: SnapshotCleanupObligation }
    | PublicationRecoveryObligation
  ```

  - the `SnapshotRecoveryRequired` branch pairs the preserved primary rejection
    with the `SnapshotCleanupObligation` when an ordinary failure (steps 2-5)
    could not discard the transaction root. `SnapshotCleanupObligation`
    deliberately does not copy the primary, so the primary is carried explicitly
    here rather than folded into the obligation;
  - the `PublicationRecoveryObligation` branch is the INV-5 ambiguous state after
    the manifest rename (owner token, both final paths, the journalled commit-point
    identity, and an optional nested `SnapshotCleanupObligation`; see "D1 operation
    shape, recovery obligations, and journal lifecycle"). The pair may or may not
    be committed and MUST NOT be blind-deleted, and it needs no primary;
- `Rejected(LinkAndPublishRejection)` — an ordinary failure whose cleanup
  *succeeded*, carrying the RFC 0043 row for its phase (`LinkerInvocation` or
  `ExecutablePublication`).

`RecoveryRequired` is never conflated with `Rejected`: an ordinary failure whose
`discardAndCleanup` reports `Clean` is `Rejected(primary)`; an ordinary failure
whose discard reports `Obligated` is `RecoveryRequired(SnapshotRecoveryRequired{
primary, obligation})`; and the INV-5 pending outcome is
`RecoveryRequired(PublicationRecoveryObligation)`, never destroying the possibly-
committed pair. Steps:

1. move in the `VerifiedLinkPlan`;
2. open + digest-verify every input handle (D3);
3. invoke the linker writing to `<root>/output-candidate` (D4), taking ownership
   of the resulting `LinkedOutputCandidate` (which now owns the plan);
4. inspect the candidate's output through its handle, re-computing digest/size
   from the same output object: format, machine architecture, entry symbol,
   required runtime symbols (RFC 0043 `ExecutablePublication` checks) — only here
   is the result named "verified";
5. build the manifest;
6. publish via the D1 transaction, which renames `<root>/output-candidate` to the
   final path (and may return the `RecoveryRequired` outcome per INV-5).

An ordinary failure in steps 2-5, or before the executable rename in step 6,
consumes the candidate via `discardAndCleanup()`: a `Clean` disposition returns
`Rejected(primary)`, and an `Obligated` disposition returns
`RecoveryRequired(SnapshotRecoveryRequired{primary, obligation})` with the primary
preserved. Only a clean commit returns `Published`; only the INV-5 window returns
`RecoveryRequired(PublicationRecoveryObligation)`. If RFC 0010's `IrOperationResult`
cannot host a three-way outcome, `LinkAndPublishOutcome` wraps it as shown rather
than overloading a rejection, and RFC 0043 records the outcome type.

## Implementation order

1. **[landed]** `zc` stable-exec-FD primitive (D3 mechanism) + `VerifiedSysroot`
   capability (D2), plus the D3b input-snapshot transaction root.
2. **[landed]** Fresh transaction-owned output candidate (D4, `3764b2cb` +
   `8faec9da`): `-o` writes `<root>/output-candidate` in the transaction root
   (never a public final path); `linkExecutable` takes the plan by value and, on
   success, transfers root ownership into `LinkedOutputCandidate`; failure cleans
   only the root through `discardAndCleanup`; `VerifiedLinkedExecutable` was
   replaced by `LinkedOutputCandidate`. The candidate captures - all from one
   no-follow (`O_NOFOLLOW`) read-only handle - the output's exact
   `StableFileIdentity` (`dev`/`ino` + link count), byte count, and SHA-256
   `outputDigest`, and enforces the D4-stage invariant (regular, non-empty,
   `st_nlink == 1`); the driver runs with the transaction root as its working
   directory. These are structural invariants only; format/architecture/symbol
   checks are D5.
3. **[landed]** `linkExecutable` opens and re-verifies all input handles and
   `execveat`s the driver handle (D3b), writes the transaction-root output
   candidate, and returns the candidate (D4).
4. **[pending]** Publication transaction with manifest-last commit + owner-token
   rollback (D1), replacing the best-effort `tryRemove` path. The full D1 contract
   is ratified (see "D1 operation shape, recovery obligations, and journal
   lifecycle" and INV-1..INV-8): `publishLinkedOutput(Moved<LinkedOutputCandidate>,
   Moved<ExecutableArtifactManifest>) -> PublicationOutcome`
   (`Published` / `RecoveryRequired(PublicationRecoveryObligation)` / `Rejected`),
   the seven-step ordered commit (RejectExisting → commit-point re-check → stage +
   fsync manifest → journal + fsync → executable rename + dir fsync → manifest
   rename (commit point) + dir fsync → discard journal + snapshot root), and the
   pointwise crash matrix. At the commit point D1 MUST re-derive the output
   `digest`/`size`/`dev`/`ino`/`st_nlink == 1` from the candidate's SAME held
   handle immediately before the journal write and rename; the D4-captured snapshot
   is not the final proof. Implementation is Pending.
5. **[pending]** The consuming `linkAndPublish` operation wiring 1-4 with the
   executable inspector and manifest (D5).

Each pending step lands as its own commit with a regression test that is red
before and green after, and is reviewed before the next. `LinkAndPublishRejection`
is defined, before D5 code, as a closed rejection type carrying only legal
`LinkerInvocation` / `ExecutablePublication` failure facts. `zomc run` stays
blocked and the RFC 0043 link-driver spine stays `[~] partial` until the chain is
complete.
