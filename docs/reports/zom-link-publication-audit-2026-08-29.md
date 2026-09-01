---
audit: link-publication
date: 2026-08-29
scope: RFC 0043 D1 recoverable manifest-last publication transaction in commit 9442ffdc
method: three-dimension adversarial review plus local confirm/refute pass
runtime: Codex multi-agent local checkout review
workflowId: local-link-publication-20260829
knownRuntimeIssues: limited to three parallel dimension reviewers by the local concurrency budget
relatedReports: []
findings: 10
language: en
status: closed
---

# ZOM Link Publication Audit

## Executive Summary

Commit `9442ffdc` is not an acceptable completion of RFC 0043 D1. The audit
retained ten correctness and verification findings: five High, four Medium, and
one Low. The most serious defects can delete a competitor's public-path entry,
demote a committed executable to an orphan, or return `Published` after the
published pair no longer verifies.

Three bounded defects are repaired in the current worktree: a
`ManifestCommitted` pair with a missing or invalid manifest is retained for
explicit repair; a later-stage journal without `Started` is no longer ignored;
and recovery factories reject a verified value masquerading as a rejection.
The owner-safe deletion protocol, syscall fault seam, literal crash matrix, and
D5-only publication authority remain open and block D1 acceptance.

| Severity | Count | Open | Repaired In Worktree |
|---|---:|---:|---:|
| High | 5 | 0 | 5 |
| Medium | 4 | 0 | 4 |
| Low | 1 | 0 | 1 |

## Findings

### LP-001 - High - Public-path check-then-unlink can delete a competitor

**Files:** `compiler/ir/executable-publication.cc:361-379`,
`compiler/ir/executable-publication.cc:982-985`,
`compiler/ir/executable-publication.cc:1397-1405`

`unlinkIdentityChecked` verifies `(dev, ino)` with `fstatat` and then performs a
separate `unlinkat`. A competitor can replace the directory entry between those
operations, causing the replacement to be deleted. The transaction-root cleanup
has the analogous check-then-remove race. The existing replacement test changes
the path before the check and therefore proves only stale-path rejection, not an
identity-conditional delete.

**Status:** repaired in the current worktree. Public executable and manifest-temp
cleanup atomically claims the entry into the transaction root with
`RENAME_NOREPLACE`, verifies the claimed inode, and restores or retains a
competitor instead of deleting it. Transaction-root cleanup uses the same
token-derived quarantine protocol before removing contents. Recovery recognizes
an already-quarantined root after a crash. Focused tests cover a competitor app
captured after manifest-rename rejection and a competitor transaction root.

### LP-002 - High - A competitor journal stage can be adopted and deleted

**Files:** `compiler/ir/executable-publication.cc:382-415`,
`compiler/ir/executable-publication.cc:1039-1043`,
`compiler/ir/executable-publication.cc:1076-1080`

When a stage install loses `RENAME_NOREPLACE`, `commitJournal` returns no ownership
proof. The caller probes the final stage path, adds it to `journalLeaves`, and
later unlinks it blindly. A competitor-created journal file is therefore adopted
and deleted by this transaction.

**Status:** the competitor-deletion defect is repaired in the current worktree.
A visible failed stage is never adopted into the transaction's owned cleanup
set; the operation preserves the stage, returns publication recovery, and
explicit tests cover collisions at `Started` and `ManifestStaged`. A fully
closed syscall-level journal-commit outcome remains part of LP-008.

### LP-003 - High - The journal checksum is not publication ownership proof

**Files:** `compiler/ir/executable-publication.cc:382-407`,
`compiler/ir/executable-publication.cc:1194-1218`

The journal uses an unkeyed SHA-256 checksum. A peer with write access to the
shared output directory can construct a checksum-valid `Started` record naming a
victim executable and its public `stat` identity. Recovery accepts arbitrary
token bytes from disk and can use the forged record to authorize deletion.
Random token names prevent accidental collisions but do not authenticate a
record discovered after a crash.

**Status:** repaired in the current worktree. The RFC and design note now define
the Unix effective user as the publication trust principal. Publication and
recovery require an output directory owned by the effective user and reject
group- or other-writable directories before accepting any journal. Focused
tests prove both entry points fail closed and preserve final entries. The
checksum is explicitly corruption and hash-chain evidence, not authentication
against a malicious same-UID process that already controls the user's artifacts.

### LP-004 - High - Publication can mint `Published` after post-rename drift

**Files:** `compiler/ir/executable-publication.cc:1086-1144`

The last pair verification occurs before the manifest rename. After the
`ManifestRenamed` checkpoint, a competitor can replace or modify either final
entry. The operation can still commit the journal, remove it, and mint
`PublishedExecutableArtifact` without a final pair verification.

**Status:** repaired in the current worktree. The transaction reverifies the
final pair after `ManifestCommitted` becomes durable and before releasing the
journal or minting `Published`. A manifest-drift regression test also proves the
post-rename recovery branch consumes the private root explicitly and preserves a
structured cleanup obligation when cleanup cannot complete.

### LP-005 - High - D1 is publicly callable without the required D5 proof

**Files:** `compiler/ir/executable-publication.h:236-247`,
`compiler/ir/executable-manifest-codec.h:171-177`

Any caller can build a pure-data verified manifest and call `publishLinkedOutput`
without proving executable format, target architecture, entry symbol, runtime
symbols, or unresolved-reference closure. This bypasses the D5 boundary while
still minting the downstream `PublishedExecutableArtifact` authority.

**Status:** repaired in the current worktree. `publishLinkedOutput` moved out of
the public IR header into `executable-publication-internal.h`, and the IR
architecture gate plus two new negative fixtures confine the D1 entry point and
its link-publication attorney to their owning implementation files. D5 will be
the production consumer.

### LP-006 - Medium - `ManifestCommitted` with a missing manifest deleted the executable

**Files:** `compiler/ir/executable-publication.cc:1385-1406`

The generic executable-only recovery branch applied to every durable stage. A
`ManifestCommitted` chain with a missing manifest was treated as an unpublished
orphan and the committed executable was deleted. The D1 matrix classifies this as
external tampering and requires retention plus explicit repair.

**Status:** repaired in the current worktree with a focused regression test.

### LP-007 - Medium - Missing `Started` could hide a broken later-stage chain

**Files:** `compiler/ir/executable-publication.cc:1194-1256`

Recovery enumerated only `journal.*.started`. With `Started` missing, a remaining
later-stage record and no final entries or transaction root produced `Clean`,
contradicting the broken-chain catch-all.

**Status:** repaired in the current worktree; any orphan journal or journal-temp
artifact now produces `ExplicitRepairRequired`, with a focused regression test.

### LP-008 - Medium - Initial journal sync failure can lose a durable debt

**Files:** `compiler/ir/executable-publication.cc:382-407`,
`compiler/ir/executable-publication.cc:938-940`

If the `Started` rename succeeds and the following directory sync fails,
`commitJournal` returns none. The caller uses the pre-journal rejection path and
can return ordinary `Rejected` while a visible or durable journal remains. Failed
temporary cleanup is also ignored.

**Status:** repaired in the current worktree. D1 now has a per-call, internal
fault seam for journal and manifest writes, file and directory syncs, installs,
temporary cleanup, and journal-chain removal. A failed `Started` directory sync
returns publication recovery with the installed record; a journal write plus
temporary-cleanup failure retains the temp and never returns ordinary
`Rejected`; final-manifest sync and committed-pair journal cleanup faults preserve
the pair and return the correct primary-less recovery shape.

### LP-009 - Medium - The documented crash matrix is not tested as a total function

**Files:** `tests/unittests/compiler/ir/invoke-linker-test.cc:803-847`

The test named "total function" follows nine checkpoints in one successful
linear run. It does not construct most documented `(highest durable stage,
actual entries)` combinations, catch-all states, broken/forked chains, or syscall
failures. It also expects `Published` for `ExecCommitted` plus both final entries
while the design table says `RecoveryRequired`.

**Status:** repaired in the current worktree. The checkpoint matrix covers every
durable transition; direct persisted-state tests cover `Started + app`, a
replacement `ManifestStaged` temp, an invalid `ExecCommitted` manifest,
`ManifestCommitted` manifest loss, checksum corruption, a missing `Started`
record with a later stage, and two valid `Started` chains for the same target.
The operation/recovery distinction is explicit: the in-flight publish call
returns outcome-pending after an ambiguous manifest commit, while the later
recovery entry may return `Published` only after re-opening and verifying INV-1.

### LP-010 - Low - Recovery obligations accepted a verified primary value

**Files:** `compiler/ir/executable-publication.h:89`,
`compiler/ir/executable-publication.cc:763-772`

`PublicationRejection` aliases the full `IrOperationResult` and can therefore hold
a verified artifact. The recovery factories previously accepted that invalid
primary without checking it.

**Status:** repaired in the current worktree by enforcing a rejection-only
runtime invariant in both factories. Replacing the alias with a closed
rejection-only type remains a possible API hardening step.

## Verification Gaps

- Journal-free recovery has no trusted expected `LinkPlanId` authority.

## Action Order

| Priority | Action | Blocking reason |
|---|---|---|
| P1 | Bind journal-free consumer admission to trusted plan identity in D5 | Satisfy the `LinkPlanId` portion of INV-1 |

## Audit Verdict

All ten audit findings are repaired and verified in the current worktree. The
sanitizer build, D1 focused tests, architecture checks and negative fixtures,
RFC and format checks, English-only and diff-hygiene gates, HIR integration,
package-session integration, and all six long ownership serial tests are green.
The ownership event-overlay suite completed in 1912.77 seconds under sanitizers,
validating its corrected 3600-second budget. D1 is closed. KR5.3 remains partial
because D5 must consume the internal D1 entry point and bind journal-free
artifact admission to the expected plan identity.
