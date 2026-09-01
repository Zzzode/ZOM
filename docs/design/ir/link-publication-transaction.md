# Link Publication Transaction and Capability Shapes (RFC 0043 refinement, PARTIALLY IMPLEMENTED)

Status: PARTIALLY IMPLEMENTED
  - Landed: D2 (VerifiedSysroot); the D3 / D3b input-open, exec-by-descriptor,
    and transaction-root snapshot foundation; and D4 (transaction-owned output
    candidate).
  - PROPOSED, pending adversarial review: D1 (publication transaction). A first
    D1 draft was rejected 2026-08-29 with seven blockers; the section below is the
    revised proposal and is NOT yet approved.
  - Proposed, pending implementation: D5.
Owner: ir-backend
Feeds: RFC 0043 "Platform Link And Executable Publication"

> This note is part contract, part landed design. The D2 sysroot capability, the
> D3/D3b input-snapshot + exec-by-descriptor foundation, and D4 (transaction-owned
> output candidate) have landed across several reviewed commits; D1 (publication
> transaction) is a PROPOSED contract under adversarial review (a first draft was
> rejected 2026-08-29) and D5 (consuming operation) remains proposed; neither is
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

### D1. Publication visibility and atomicity: manifest-last commit marker (PROPOSED, pending adversarial review)

> Status: PROPOSED. A first D1 draft was rejected on 2026-08-29 with seven
> blockers (compound rename+fsync crash states, journal-delete ordering, journal
> established too late, an outcome type that could not express a snapshot-only
> debt, missing manifest<->candidate live binding, non-exclusive final renames,
> and an unspecified journal format). This section is the revised proposal and is
> not yet approved; no D1 code is authorized until it passes review.

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

**Trust boundary.** On Unix, the output directory is accepted only when its
owner is the effective user and neither group nor other principals can write it.
All processes under that effective user are one OS trust principal: the journal
checksum proves canonical record integrity and chain continuity inside that
boundary, but does not claim to authenticate against a malicious same-UID
process that already has authority to replace the user's executable and
manifest. INV-3 and INV-6 cover independent protocol-following publishers and
external-principal races. Final consumers still reverify the manifest and
executable because publication never grants immunity from later same-principal
tampering.

- **INV-1 Sole commit marker.** A consumer accepts the executable only when the
  final `.zom-artifact` exists AND its own codec verifies AND its recorded
  executable digest and byte count match the on-disk executable AND its
  `LinkPlanId` matches. A bare `app` with no valid manifest is never a published
  artifact.
- **INV-2 Commit order.** Durably commit journal stage `Started` → stage the
  manifest temp and fsync it → exclusive `renameat2(RENAME_NOREPLACE)` the
  executable → fsync the directory → durably commit `ExecCommitted` → exclusive
  `renameat2(RENAME_NOREPLACE)` the manifest (the commit point) → fsync the
  directory → durably commit `ManifestCommitted`. The manifest is never visible
  before the executable, and a rename+dir-fsync is a compound step whose
  durability is recorded by the following journal-stage commit (see "D1 operation
  shape" for the full algorithm).
- **INV-3 Owner token.** Every temporary and every potential orphan carries a
  non-colliding transaction identity. Cleanup removes only objects this
  transaction can prove it owns; it never deletes an object identified only by a
  public final path.
- **INV-4 Crash-recovery matrix.** The state after a crash is defined by the last
  durably-committed `JournalStage` plus the actual final entries (the full table
  is in "D1 operation shape … crash-recovery matrix"). The recogniser decides
  committed-vs-orphan solely from disk — a complete, verifying manifest means
  published — never by a prior in-memory return value.
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
  repair; it is never consumed in the meantime). Option B constraint: a leftover
  `.zomlink-<token>` transaction root with NO durable journal (a crash before the
  `Started` stage) has no ownership proof beyond its token name, so recovery MUST
  retain it (explicit-repair-only) and MUST NOT auto-remove it. D1's owner-safe
  recovery guarantee holds only from `Started` onward; the future `zomc` repair
  command's acceptance criteria include surfacing these pre-`Started` roots for
  operator action and never deleting one on token name alone.
- **INV-8 Persistent owner proof after rename.** Once a token-named temporary is
  renamed to the public final `app`, the token is no longer in the filename, so
  "no manifest" alone does NOT authorise deleting that `app` (it could be a
  user's or a competitor's file). The transaction retains a persistent ownership
  proof, chosen as follows:
    - Primary: a **transaction journal** durably committed at stage `Started`
      (written, fsynced, atomically installed by no-replace rename, then journal-dir
      fsynced) as D1's FIRST durable action — before any final-directory manifest
      temporary exists and before any final-path rename — recording the owner token,
      both final paths, and the staged object's stable file identity (device +
      inode) from the commit-point re-check. Recovery deletes an orphan `app` only
      when the journal proves this transaction created it AND the final path still
      resolves to that same stable file identity.
    - Cleanup never performs a path check followed by a separate unlink. It
      atomically moves the public entry with `RENAME_NOREPLACE` into the
      transaction root under a closed quarantine name, then checks the claimed
      inode against the journalled identity. An owned inode is removed only as
      part of root cleanup. A mismatching competitor is restored with another
      no-replace rename, or retained in quarantine with an explicit cleanup debt
      if restoration loses a race.
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
slice has a fixed target. It is a PROPOSED contract under adversarial review, not
approved and not landed code.

**Operation shape.** D1 is one consuming operation that takes the D4
`LinkedOutputCandidate` by move (so the still-live transaction root and its
output handle are the sole authority for the bytes being published) plus the
built manifest, and returns an explicit three-way outcome whose `RecoveryRequired`
branch reuses D5's approved `LinkRecoveryRequired` algebra so a snapshot-only debt
and a publication-pair debt are each expressible:

```
publishLinkedOutput(
  candidate: Moved<LinkedOutputCandidate>,
  manifest:  Moved<ExecutableArtifactManifest>,
) -> PublicationOutcome

PublicationOutcome =
    Published(PublishedExecutableArtifact)   // manifest committed + durable + verified
  | RecoveryRequired(LinkRecoveryRequired)   // an un-droppable debt; see below
  | Rejected(PublicationRejection)           // failed before any final-path effect; cleanup succeeded

LinkRecoveryRequired =
    SnapshotRecoveryRequired {               // no public final path was ever touched,
      primary:    PublicationRejection,      //   but the snapshot root could not be discarded
      obligation: SnapshotCleanupObligation }
  | PublicationRecoveryRequired {            // a public final entry may exist (orphan or
      primary:    Maybe<PublicationRejection>,//  ambiguous-durability); never blind-deleted
      obligation: PublicationRecoveryObligation }
```

Every non-ambiguous failure keeps its cause traceable: `PublicationRecoveryRequired`
carries a `primary` that is `Some(PublicationRejection)` whenever an ordinary
failure (a manifest-staging fault, a lost no-replace-rename race, an fsync error,
or a cleanup failure that left a publication debt) is what produced the
obligation, and `None` only for the genuinely primary-less INV-5 states — a pure
post-`ManifestCommitted` durability ambiguity, or a cleanup debt after a fully
committed pair — where there is no failure "cause" to preserve, only a durability
outcome to adjudicate (blocker 3). The nested snapshot debt, when the transaction
root also could not be discarded, lives inside the `PublicationRecoveryObligation`
(below), so one value carries every un-droppable obligation at once.

Taking the candidate by move means D1 consumes the transaction root: on every
branch it is responsible for discarding the still-live root (via the candidate's
`discardAndCleanup`) once the final pair is committed or the attempt is
abandoned. The candidate is moved in exactly once and cannot be published twice.
`Rejected` is used ONLY when no public final path was created or renamed AND the
snapshot root was discarded cleanly; a rejection before any final-path effect
whose root discard failed is `RecoveryRequired(SnapshotRecoveryRequired{...})`, not
a forged publication obligation (blocker 4).

**The two recovery obligations are distinct typed values; never conflated.**

- `SnapshotCleanupObligation` (D3b/D4, already landed) concerns the
  transaction-private `.zomlink-<token>/` root: "this transaction-owned tree could
  not be removed." It carries the transaction id, canonical parent, exact
  directory identity, plan id, and the cleanup failure kind/stage. It never names
  a public final path.
- `PublicationRecoveryObligation` (D1, new) concerns the public final pair
  (`app`, `app.zom-artifact`): "a final-path publication reached an orphan or
  ambiguous-durability state." It carries the owner token, both final paths, the
  journalled commit-point `StableFileIdentity`, the journal location, the last
  durably-committed `JournalStage`, and an optional nested
  `SnapshotCleanupObligation`. It never deletes by public path without a journal +
  exact-identity match. It is always returned wrapped in a
  `PublicationRecoveryRequired`, which adds the `Maybe<PublicationRejection>`
  primary so an ordinary failure's cause is never lost.

**Manifest ↔ candidate live-binding verification (before any journal or rename).**
A moved-in manifest is not trusted to match the candidate. Before D1 writes any
journal or stages any temporary, it independently verifies at least: the
manifest's final destination equals `candidate.plan().outputPath()`; the
manifest's recorded executable digest and byte count equal the values re-derived
in the commit-point re-check from the candidate's SAME held handle; the manifest's
`LinkPlanId` equals `candidate.plan().id()`; and the manifest's target/toolchain
identity and ordered input digests equal the plan's. A mismatch rejects under
`ExecutablePublication` with `InvalidFact` (or `CanonicalCodecMismatch` for a
malformed manifest encoding) before anything is written — the transaction root is
discarded and nothing on the final path is touched (blocker 5).

**The journal is a durable, staged, append-by-replace anchor.** The journal is
the transaction's persistent owner proof and is established as D1's FIRST durable
action, before any final-directory manifest temporary exists (blocker 3). Its
stage is a closed enum; the transaction advances it only by writing a fresh
**immutable per-stage record** and atomically installing it under a distinct
stage-named path with `RENAME_NOREPLACE`, never by mutating or overwriting a
prior record (blocker 3 — a single-path replace has no identity-conditional
atomicity, so a competitor swapped between two stage commits would be clobbered):

```
JournalStage =
    Started          // owner claimed; RejectExisting + step-1 pre-checks done.
                     //   records root identity, candidate output identity,
                     //   the manifest-temp path FORMULA, and the expected manifest digest.
                     //   No manifest temp exists yet, so no manifest-temp identity is recorded.
  | ManifestStaged   // manifest temp created + file-fsynced + final-dir-fsynced;
                     //   records the manifest temp's captured exact identity AND the
                     //   COMMIT-POINT re-derived output digest/size/StableFileIdentity
                     //   (re-read from the candidate's same held handle just before this).
                     //   Exec rename is permitted only after this stage is durable.
  | ExecCommitted    // <root>/output-candidate has been renamed to `app`.
  | ManifestCommitted// manifest temp has been renamed to `app.zom-artifact` (the commit point).
```

Each stage is a separate immutable record file `journal.<token>.<stage>` (an
owner-token-derived name), created `PRIVATE` + exclusive `O_NOFOLLOW`, then
`fsync(file)` → `renameat2(RENAME_NOREPLACE)` into place → `fsync(journal-dir)`.
Only after that directory fsync returns is a stage "durably committed." No stage
record is ever overwritten. Each record uses a domain-separated canonical codec
with an explicit version and a checksum, and records the fields listed for its
stage plus the owner token, both final paths, the `LinkPlanId`, its own
`JournalStage`, and a **`previousJournalId`** — the canonical digest of the
immediately-preceding stage record (empty for `Started`) — forming a hash chain.
Recovery selects the highest stage that is complete, checksum-valid, AND
whose `previousJournalId` chain links unbroken back to `Started`: it verifies the
hash chain, not merely that the enum values / filenames are contiguous, so a
mixed or partial generation of records can never be assembled into a forged
"highest stage." A record that is malformed, truncated, fails its checksum, breaks
the chain, or whose recorded identity does not match the on-disk object NEVER
authorises a delete — recovery retains the object and reports it for explicit
repair (blocker 7).

**Canonical ordered commit algorithm.** Because a rename and its directory fsync
are a compound step, the algorithm's durability points are the per-stage journal
commits and the final-directory fsyncs, not a single step counter (blocker 1). D1
performs:

1. Pre-checks (no durable effect yet): verify RejectExisting for BOTH final paths
   with a no-follow `tryLstat` (INV-1/INV-6); run an initial commit-point read
   (digest/size/exact `StableFileIdentity`/regular/`st_nlink == 1` from the
   candidate's SAME held handle) and the manifest live-binding verification. Any
   failure rejects before anything durable is written.
2. Durably commit `Started` (record root identity, candidate output identity, the
   manifest-temp path formula, and the expected manifest digest).
3. Stage the manifest into a transaction-owned temporary in the final directory:
   create it, `fsync` the file, `fsync` the final directory, then open it with a
   no-follow read-only handle and capture its exact identity and digest from that
   handle.
4. Re-derive `digest`, `size`, exact `StableFileIdentity`, regular-file shape, and
   `st_nlink == 1` from the candidate's SAME held handle AGAIN, and re-verify the
   manifest live binding against these fresh values (blocker 4 — the step-1 read
   does not substitute; the object could have changed between step 1 and here).
   Durably commit `ManifestStaged` recording the manifest temp's exact
   identity/digest and this commit-point output identity/digest/size.
5. **Immediately before the executable rename**, after the `ManifestStaged`
   journal-dir fsync has returned, do a final lightweight full re-check from the
   candidate's held handle and the manifest temp's held handle: the output
   identity/digest/size and the manifest temp identity/digest MUST still equal the
   values recorded in `ManifestStaged`. If anything differs, ABORT (discard root +
   journal chain; nothing on the final path touched) — D1 never renames bytes that
   drifted from what the journal committed. Only on an exact match, rename
   `<root>/output-candidate` → `app` with an exclusive `renameat2(RENAME_NOREPLACE)`
   (a platform without an exclusive no-replace rename fails closed) so a competitor
   that created `app` after step 1 is never clobbered; `fsync` the final directory;
   durably commit `ExecCommitted`.
6. Before the manifest commit, re-verify BOTH sides — because between step 5 and
   here the public `app` could be modified or replaced in place and the manifest
   temp could drift (blocker 2). From the candidate's original held output handle,
   recompute `digest`/`size`/`dev`/`ino`/`st_nlink == 1`; with a no-follow check,
   confirm the final `app` entry still resolves to that SAME exact identity; and
   re-check the manifest temp's identity/digest through its held handle. All three
   MUST equal the `ManifestStaged`/`ExecCommitted` records and the manifest. If
   anything drifted, do NOT rename the manifest (the final manifest path is never
   created): `app` is already this transaction's public orphan, so return
   `RecoveryRequired(PublicationRecoveryRequired{ primary:
   Some(ExecutablePublication rejection), obligation })` carrying the journal
   stage + identity — never a plain `Rejected` — and `app` is handled only by the
   journal + exact-identity recovery rules. Only on an exact match, rename the
   manifest temp → `app.zom-artifact` using `renameat2(RENAME_NOREPLACE)` (**the
   commit point**); `fsync` the final directory; durably commit `ManifestCommitted`.
7. Cleanup, in this order: discard any residual manifest temp, then discard the
   snapshot root via the candidate; ONLY after both succeed, delete the journal
   stage records (highest-to-lowest) and `fsync` the journal directory (blocker 2 —
   the journal chain is released last, so a crash during cleanup still leaves a
   durable owner proof). If any cleanup step fails, the journal chain is retained
   and the outcome carries a recovery obligation.

If the executable no-replace rename (step 5) loses to a competitor, D1 rolls back
only its own transaction (discard root + journal chain) and touches neither the
competitor's `app` nor the final manifest path. If the manifest no-replace rename
(step 6) loses to a competitor, the transaction's own `app` is rolled back only
when the journal + exact-identity match proves D1 created it; the competitor's
manifest is never deleted (blocker 6).

**INV-4 crash-recovery matrix — a TOTAL function of (highest durable stage, actual
final entries).** The recogniser decides from disk alone — the highest complete,
checksum-valid, prior-chain-consistent stage plus what actually exists at the two
final paths (INV-1: a published artifact requires a complete, verifying manifest) —
never from an in-memory return value. Every combination not matched by a specific
row below hits the catch-all (blocker 5):

| Highest durable stage | Final-path entries | Verdict | Recovery action |
|---|---|---|---|
| none (no valid chain) | leftover `.zomlink-<token>` root only | unknown ownership (token name is not proof) | **explicit-repair-only: NEVER auto-remove** (Option B — a pre-`Started` crash has no durable owner record; see below) |
| `Started` | neither `app` nor manifest; a token-named manifest temp may exist | not published, owner-proved for the root | discard the snapshot root + journal chain (both owner-proved by exact identity). A manifest temp is **retained for explicit repair, NOT auto-deleted**: at `Started` the journal holds only its path formula + expected digest, not its exact identity, so the on-disk temp cannot be proven to still be this transaction's (blocker 1 — path/digest never authorises a delete) |
| `Started` | `app` matches recorded candidate identity; no manifest | orphan executable | atomically quarantine `app` into the owner-matching transaction root, verify the claimed inode, then remove it through root cleanup; any token-named manifest temp is retained for explicit repair because no identity is recorded yet |
| `ManifestStaged` | neither `app` nor manifest (temp may exist) | not published, owner-proved | atomically quarantine the identity-matching manifest temp into the transaction root, then clean the root and journal chain; a mismatching entry is restored or retained for explicit repair |
| `ManifestStaged` | `app` matches; no manifest | orphan executable (crash between exec rename and its ExecCommitted stage) | atomically quarantine and verify `app` plus the manifest temp, then clean the root and chain |
| `ExecCommitted` | `app` matches; no manifest | orphan executable (definite: crash before manifest rename) | atomically quarantine and verify `app` plus the manifest temp, then clean the root and chain |
| `ExecCommitted` | `app` matches + manifest present | **publishedness-ambiguous for the original publish operation** (manifest rename may have landed before ManifestCommitted became durable) | the original operation returns `RecoveryRequired(PublicationRecoveryRequired{ primary: None, obligation })`; the explicit recovery entry reopens both files, applies INV-1, and returns `Published` only when the pair verifies |
| `ManifestCommitted` | `app` + verifying manifest, both match | published | discard residual temp + snapshot root, then delete journal chain; a cleanup failure reports a non-ambiguous obligation, never touching the committed pair |
| `ManifestCommitted` | manifest missing / does not verify / identity mismatch | competitor or external tampering | fail closed: retain, report for explicit repair |
| **any other combination** (stage/entry mismatch, identity mismatch, broken or forked journal chain) | — | undecidable | **fail closed: retain ALL, report for explicit repair; NEVER infer a delete** |

Two rows correspond to the rename recovery windows where a rename may have landed
but its following stage commit did not become durable. Only ONE of them is
*publishedness*-ambiguous for the in-flight publish call: `ExecCommitted` + both
entries present. That call returns `RecoveryRequired`; a later explicit recovery
call resolves the ambiguity from disk and may return `Published` after a full
INV-1 verification. The `app`-only rows are NOT ambiguous — they are a definite,
unpublished orphan (INV-1: no manifest means not published); they are resolved by
an atomic quarantine-and-verify cleanup, not a blind path delete.

**Option B — pre-`Started` roots are explicit-repair-only.** A crash at the D1
entry, during the pre-checks, or during the initial commit-point read leaves the
D4 `.zomlink-<token>` root on disk with NO durable journal, so recovery has only a
token name and cannot prove ownership. Per the repository's fail-closed discipline
D1 therefore does NOT auto-remove such a root: the `none` matrix row retains it and
routes it to explicit repair (the same recovery entry point as INV-7). D1's
owner-safe recovery guarantee holds from `Started` onward. The rejected
alternative (Option A) is to make D4 durably write a root-owner record at
candidate creation so even a pre-`Started` crash is auto-recoverable; it is
rejected here because it would reopen the already-landed, already-reviewed D4
slice for a marginal recoverability gain, and a leftover pre-`Started` tree is a
bounded, explicitly-repairable artifact under the output directory. If a future
requirement makes unattended pre-`Started` cleanup necessary, it lands as its own
D4 slice, not as a silent token-name delete here.

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

**Inspection authority in the plan.** D5 must not infer an expected machine from
an opaque target digest or from the host. `VerifiedLinkPlan` therefore owns one
closed `ExecutableInspectionProfile` and folds it into `LinkPlanId`:

```text
ExecutableInspectionProfile {
  objectFormat: Elf | MachO,
  machine: X86_64 | AArch64,
  pointerWidthBits: 64,
  requiredRuntimeSymbols: StrictlySortedUnique<AsciiSymbol>,
  runtimeReferenceDomain: AsciiSymbol,  // canonical raw prefix: __zom_ (ELF) / ___zom_ (Mach-O)
}
```

`ElfDriver` is valid only with `Elf`; `MachODriver` only with `MachO`. The entry
symbol remains the plan's separate single required symbol. Every symbol name -
the required runtime symbols, the runtime reference domain, and the entry - is
the target's raw symbol-table spelling (Mach-O adds a leading underscore, so a C
`zom` entry is `_zom` and runtime imports are `___zom_*`), the same bytes passed
to `ld -e`. `runtimeReferenceDomain` is derived strictly from `objectFormat` and
is never empty or caller-chosen. D5 checks the entry and every runtime symbol as
defined symbols, and rejects any undefined symbol whose raw name is in the
runtime reference domain as an unresolved runtime reference. This replaces
the current insufficient shape where the plan carries only a target identity,
driver kind, and entry symbol: those fields cannot independently prove the
output's machine or runtime-symbol closure.

The initial inspector is closed to little-endian ELF64 `ET_EXEC`/`ET_DYN` and
64-bit Mach-O `MH_EXECUTE`, for x86-64 and AArch64 only. It validates every
header, load-command, section, symbol-table, and string-table range before use.
Malformed bounds or symbol records are `InvalidFact`; format, machine, or
pointer-width mismatches are `InvalidAbi`. A successful linker exit and a magic
prefix alone are never verification evidence.

`LinkAndPublishOutcome` is exactly one of:

- `Published(PublishedExecutableArtifact)` — the manifest committed and verified;
- `RecoveryRequired(LinkRecoveryRequired)` — a recovery obligation that must not
  be silently dropped, where

  ```
  LinkRecoveryRequired =
      SnapshotRecoveryRequired { primary: LinkAndPublishRejection,
                                 obligation: SnapshotCleanupObligation }
    | PublicationRecoveryRequired { primary: Maybe<LinkAndPublishRejection>,
                                    obligation: PublicationRecoveryObligation }
  ```

  - the `SnapshotRecoveryRequired` branch pairs the preserved primary rejection
    with the `SnapshotCleanupObligation` when an ordinary failure (steps 2-5)
    could not discard the transaction root. `SnapshotCleanupObligation`
    deliberately does not copy the primary, so the primary is carried explicitly
    here rather than folded into the obligation;
  - the `PublicationRecoveryRequired` branch covers every state where a public
    final entry may exist (owner token, both final paths, the journalled
    commit-point identity, the last durable `JournalStage`, and an optional nested
    `SnapshotCleanupObligation`; see "D1 operation shape, recovery obligations, and
    journal lifecycle"). Its `primary` is `Some(rejection)` for an ordinary failure
    that produced a public orphan (a lost rename race, an fsync error, a
    pre-manifest-commit drift abort) and `None` for the genuinely primary-less
    INV-5 durability ambiguity. The pair may or may not be committed and MUST NOT
    be blind-deleted;
- `Rejected(LinkAndPublishRejection)` — an ordinary failure whose cleanup
  *succeeded*, carrying the RFC 0043 row for its phase (`LinkerInvocation` or
  `ExecutablePublication`).

`RecoveryRequired` is never conflated with `Rejected`: an ordinary failure whose
`discardAndCleanup` reports `Clean` is `Rejected(primary)`; an ordinary failure
whose discard reports `Obligated` is `RecoveryRequired(SnapshotRecoveryRequired{
primary, obligation})`; and the INV-5 pending outcome is
`RecoveryRequired(PublicationRecoveryRequired{ primary: None, obligation })`,
never destroying the possibly-committed pair. Steps:

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
preserved. A failure AFTER the executable rename (a public orphan already exists)
returns `RecoveryRequired(PublicationRecoveryRequired{ primary: Some(rejection),
obligation })`. Only a clean commit returns `Published`; only the INV-5 durability
window returns `RecoveryRequired(PublicationRecoveryRequired{ primary: None,
obligation })`. If RFC 0010's `IrOperationResult` cannot host a three-way outcome,
`LinkAndPublishOutcome` wraps it as shown rather than overloading a rejection, and
RFC 0043 records the outcome type.

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
4. **[landed]** Publication transaction with manifest-last commit + owner-safe
   quarantine (D1). `publishLinkedOutput` implements the full journal-staged
   transaction and total recovery matrix described by INV-1..INV-8:
   `publishLinkedOutput(Moved<LinkedOutputCandidate>, Moved<ExecutableArtifactManifest>)
   -> PublicationOutcome` (`Published` /
   `RecoveryRequired(LinkRecoveryRequired)` / `Rejected`), the ordered
   journal-staged commit (pre-checks → `Started` → stage manifest temp →
   `ManifestStaged` with a commit-point re-derivation → pre-rename re-check →
   executable `renameat2(RENAME_NOREPLACE)` → `ExecCommitted` → pre-commit re-check
   of both `app` and the manifest temp → manifest `renameat2(RENAME_NOREPLACE)`
   (commit point) → `ManifestCommitted` → cleanup with the journal chain released
   last), and the total-function crash matrix keyed by the highest durable
   `JournalStage` + actual final entries. Immediately before each final rename D1
   re-derives the output `digest`/`size`/`dev`/`ino`/`st_nlink == 1` from the
   candidate's SAME held handle and re-checks the on-disk entry identity; the
   D4-captured snapshot is not the final proof. The adversarial audit and
   sanitizer/architecture/recovery gates are closed.
5. **[landed in worktree]** `ExecutableInspectionProfile` is part of
   `VerifiedLinkPlan` and `LinkPlanId`; the 518-byte oracle
   (`54e60703e2ea42b6f0b45f616f41f3b417b298345edf5e8d6a79b5d5817c8dfd`) freezes
   the current profile-bearing plan, including the runtime reference domain. The
   bounded ELF64/Mach-O64 inspector verifies format, machine, width, entry, and
   runtime symbols, rejects unresolved runtime references and malformed symbol
   names, and bisects format/bitness mismatch (InvalidAbi) from structural
   corruption (InvalidFact). `linkAndPublish` consumes the plan
   through D3/D4, inspection, manifest verification, and D1. Linux ELF success,
   malformed image, machine mismatch, missing runtime symbol, and pure Mach-O
   fixtures are covered.

Each pending step lands as its own commit with a regression test that is red
before and green after, and is reviewed before the next. `LinkAndPublishRejection`
is defined, before D5 code, as a closed rejection type carrying only legal
`LinkerInvocation` / `ExecutablePublication` failure facts. `zomc run` stays
blocked and the RFC 0043 link-driver spine stays `[~] partial` until the chain is
complete.
