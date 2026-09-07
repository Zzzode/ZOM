---
title: RFC Tracker Consistency Audit
date: 2026-09-07
area: governance
scope: docs/rfc/tracking/*.md against the production tree
trackers-audited: 41
trackers-clean: 27
status: findings-open
---

# RFC Tracker Consistency Audit (2026-09-07)

Q4 close requirement 7 states that live design notes and RFC trackers must match
production code at quarter close. The RFC 0047 diagnostics cutover audited only
diagnostics-related tracker claims. This audit covers the remaining trackers.

RFC 0047 and RFC 0002 are excluded because both were rewritten deliberately on
2026-09-06.

## Resolved By This Audit

Present-tense claims asserting diagnostic codes that the cutover unallocated
were corrected in `40c34c34`, covering RFC 0004, 0006, 0008, 0011, and 0012.
RFC 0025 stage `R25-09C` was recorded as stale in `1972e320`.

## Correction To An Earlier Finding

An initial pass reported that RFC 0025 stage `R25-03` was `Complete` against
artifacts with no git history at all, which would have made the completion claim
unverifiable in principle. That conclusion was wrong and is withdrawn.

`frozen-registry.h`, `identity-dump.{h,cc}`, and
`semantic-identity-registry-set.{h,cc}` did exist, under
`products/zomlang/compiler/identity/`. Commit `f89d74b6` later removed the
`zomlang/` prefix and hoisted subsystems to the top level, so a path-scoped
history query against `compiler/identity/...` returns nothing. Commit `eb1033ef`
deleted the files, and RFC 0027 stage `R27-29` records that deletion as its own
completed deliverable on 2026-08-06.

`R25-03` therefore cites artifacts that were real when it was written and were
later deliberately removed by another RFC. That is ordinary cross-RFC staleness,
not a fabricated completion claim. The lesson for future audits is that
path-scoped history is unreliable across a tree-wide move; search by symbol with
`git log -S` before concluding an artifact never existed.

## Open Findings By Owner

### RFC 0025 (dominant risk)

RFC 0025 accounts for the large majority of unresolvable citations. Its
`Complete` rows cite artifacts removed by RFC 0027, and several pending stages
plan against artifacts RFC 0047 deleted, so they cannot be executed as written:

- `R25-03`, `R25-03T`, `R25-02BA`, `R25-02B`, `R25-03CT`, `R25-02P` cite removed
  identity-registry artifacts, the deleted module-graph diagnostic adapter,
  `PackagePipelineFailure`, and `VerifiedModuleGraphVerifier`. The live type is
  `VerifiedModuleGraphInputVerifier`.
- `R25-07`, `R25-07T`, `R25-08T`, `R25-09D`, `R25-09E`, `R25-12A`, `R25-02C`,
  `R25-05G` plan against paths that do not resolve.
- `R25-13C` and `R25-13` are blocked on deleting two documents that are already
  gone, so they can never close as written.

Recommended disposition: a single rewrite pass owned by RFC 0025, not line
fixes. The `R25-03` versus `R27-29` relationship should be stated explicitly so
neither row reads as contradicting the other.

### Test-directory reorganization fallout

Twenty-eight cited paths named files that only moved during the source-grouping
refactors, and twenty-three cited CTest targets were pure directory-prefix
renames, so `compiler-session-test` became `session-compiler-session-test` and
`ir-failure-test` became `diagnostics-ir-failure-test`. All were corrected in
place on 2026-09-07.

No checker guards this class. One was written and then removed: documentation
consistency is not something this repository gates on, and a checker that scans
prose is the kind of source-text scan RFC 0047 already rejected as architecture
evidence. The correction stands on its own; a future reorganization will need
the same manual pass.

The remaining thirty-seven citations name genuinely deleted artifacts and are
left alone, since a tracker may record work whose files a later RFC removed.

### Missing verification scripts

`scripts/check-coverage-thresholds.py`, `scripts/check-ir-diagnostic-boundary.py`,
and `scripts/check-rfc0007-architecture.py` are cited as verification commands
but do not exist. Referenced from RFC 0024, RFC 0004, and the RFC 0006 tracker.

### Other

- `docs/rfc/tracking/0011-review-and-implementation.md` cites
  `semantic-type-canonicalization-test`, which has no source file and no
  registered target; `semantic-type-store-test` exists.
- RFC 0027 and RFC 0043 cite `ir-diagnostic-adapter-test` in dated evidence
  rows. Those rows are legitimate history, but the target is now
  `diagnostics-ir-capability-failure-projector-test`, so the named gate cannot
  be re-run as written.

## Clean Trackers

27 of 41 audited trackers carry no findings: 0005, 0007, 0009, 0013 through
0020, 0022, 0024, 0026, 0030 through 0035, 0037, 0039 through 0042, 0044, 0045.
