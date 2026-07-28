# RFC 0042 Review And Implementation Tracker

## Discussion Record

### 2026-07-28 R30-16 Exact-Set Rejection

The post-R30-15 preflight traced the live diagnostic builder, canonical codec,
query values, Binder result codec, materializer, callers, native tests, and
coverage gate. The accepted six-path R30-16 set cannot replace the source-only
record, cannot express RFC 0017 occurrence or provenance, and cannot use
Binder identity types without reversing target ownership.

An independent error-system review rejected a parallel Binder payload and a
diagnostics-to-Binder dependency. Product edits remain paused until this RFC
corrects the atomic boundary and dependency graph.

## Decision Record

On 2026-07-28, `rfc`, `error-system`, `lexer-parser`, `module-system`,
`binder-checker`, and `verification` approved proposal SHA-256
`1c46e978b91941c9660cf2bc8a37d89fc0a0b726b13c752c4eb8c7afed533491`
and tracker SHA-256
`cac93b151aa985cd872cff03742397e7b8678e353f30e7fa15d182afef7c7cc2`.
No owner recorded a blocker.

Transaction `rfc0042-accept-20260728-1c46e978` accepts the source-only
canonical fact, provenance, materialization, current Binder-consumer, exact
landing-set, and verification contract. It assigns the live Source-plus-Module
expansion and RFC 0027 `S6` solely to RFC 0029 `R29-13B`, published through
`R29-14`. It authorizes no product edit outside the 51-path exact set.

## Review Tracker

| Task | Owner | Depends On | Deliverable | Verification | Status |
|---|---|---|---|---|---|
| `R42-01` | `rfc` | None | Complete RFC 0042, tracker, synchronized dependencies, and RFC index row. | `python3 scripts/check-rfc.py` | Complete; synchronized proposal, tracker, dependency RFCs, and index are ready for exact-hash review |
| `R42-02` | `error-system` | `R42-01` | Review the closed live source fact, draft, argument, provenance, codec, dead FixIt deletion, limits, and materializer. | Exact-hash review | Complete; approved exact proposal and tracker hashes |
| `R42-03` | `lexer-parser` | `R42-01` | Review deterministic source publication and parse reconstruction. | Exact-hash review | Complete; approved exact proposal and tracker hashes |
| `R42-04` | `module-system` | `R42-01` | Review lower-layer identities, provenance retention, query values, session handoff, and corrected dependency graph. | Exact-hash review | Complete; approved exact proposal and tracker hashes |
| `R42-05` | `binder-checker` | `R42-01` | Review current Binder result codec migration and deletion of unimplemented diagnostic schema inventory. | Exact-hash review | Complete; approved exact proposal and tracker hashes |
| `R42-06` | `verification` | `R42-01` | Review complete caller census, exact set, mutations, CTest ownership, and isolated gates. | Exact-hash review | Complete; approved 51 sorted unique paths and repository gates |
| `R42-07` | `rfc` | `R42-02`; `R42-03`; `R42-04`; `R42-05`; `R42-06` | Record one unchanged proposal and tracker hash plus all owner decisions. | RFC and repository gates | Complete; transaction `rfc0042-accept-20260728-1c46e978` |
| `R42-08` | `rfc` | `R42-07` | Accept and publish the synchronized design-only transaction. | Local, upstream, and remote SHA parity | Complete through transaction `rfc0042-accept-20260728-1c46e978` |

## Implementation Tracker

All source tasks operate in one cumulative uncommitted tree. No task below
authorizes a partial product commit.

| Task | Owner | Depends On | Deliverable | Verification | Status |
|---|---|---|---|---|---|
| `R42-11` | `verification` with all source owners | `R42-08` | Freeze the complete live-use census and exact landing allowlist; add scope mutations for missing, extra, renamed, deleted, staged, and unstaged paths. | Exact-hash review and landing-scope self-test | Pending |
| `R42-12A` | `error-system` with `module-system` review | `R42-11` | Replace the source fact declarations with current closed occurrence, provenance, argument, secondary, fact, limits, and source-provenance declarations. | Header compile and exact-hash review | Pending |
| `R42-12B` | `error-system` with `verification` review | `R42-12A` | Implement strict source fact, argument, secondary, and provenance codecs plus complete count, allocation, truncation, trailing, tag, range-kind, and re-encoding mutations. | Focused native codec tests | Pending |
| `R42-13A` | `error-system` with `lexer-parser` review | `R42-12B` | Replace `DiagnosticFactBuffer` with non-encodable `SourceDiagnosticDraftBuffer`, deterministic draft ordering, and exact primary, highlight, and child-note capture; delete the unused FixIt API. | Draft-buffer native tests and removed-symbol census | Pending |
| `R42-13B` | `lexer-parser` with `module-system` and `verification` review | `R42-13A` | Publish facts and provenance from parse inputs; migrate canonical parsed source, rejection values, independent reconstruction, fuzz, benchmarks, and callers. | Parse-source and mutation tests | Pending |
| `R42-14A` | `binder-checker` with `verification` review | `R42-12B` | Delete unimplemented diagnostic phase, emitter, argument, mapping, and code inventory from the stable-binding schema and synchronize its reusable gate and self-test. | Stable-binding-schema check and self-test | Pending |
| `R42-14B` | `binder-checker` with `error-system` review | `R42-14A` | Migrate the live Binder result limits and canonical sequence admission to the sole fact wire. | Stable-binding fact and codec tests | Pending |
| `R42-14C` | `error-system` with `module-system` review | `R42-13B`; `R42-14B` | Replace materialization and driver handoff with retained source-provenance resolution, token-range preservation, and no provider emission. | Materializer and compiler-session tests | Pending |
| `R42-14D` | `verification` with all source owners | `R42-14C` | Prove complete caller migration, removed source and FixIt contracts, exact scope, schema removal, native CTest discovery, and architecture self-test behavior. | Native and architecture checks plus self-tests | Pending |
| `R42-15` | `verification` | `R42-14D` | Assemble only the exact set in an isolated worktree, run focused and complete native gates, explicitly stage the allowlist, and prove index scope. | RFC 0042 Test Plan | Pending |
| `R42-16` | `error-system` with all affected owners | `R42-15` | Commit and publish the one atomic source cutover. | Local, upstream, and remote SHA parity | Pending |
| `R42-17` | `rfc` | `R42-16` | Synchronize landed evidence, resume RFC 0029 `R29-13A`, and move RFC 0042 to LANDED. | RFC and evidence audit | Pending |
