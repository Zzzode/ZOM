# RFC 0047 Review And Implementation Tracker

## Discussion Record

### 2026-09-05 Standalone Architecture Intake

The current repository has no standalone diagnostics architecture RFC. RFC
0017 defines query purity and diagnostic facts, RFC 0042 lands the source-fact
cutover, and feature RFCs define producer-specific failures. The directory,
adapter, policy, rendering, IDE, and verification rules remain distributed
across implementation, agent rules, and architecture gates.

The intake audit found a concrete production discontinuity: Source and Module
facts can cross query boundaries, but the generic materializer admits only
source-syntax diagnostics. Other subsystems retain direct-emission adapters or
session-local mappings. The RFC therefore treats current adapters as migration
inputs and proposes one canonical collector and materializer rather than
standardizing the current file count.

Prior-art review used fixed upstream revisions of rustc, rust-analyzer, Salsa,
LLVM/Clang, Swift, and Roslyn. The common evidence supports structured values,
late source resolution, pure incremental providers, immutable publication, and
separate output consumers. No single upstream architecture is copied wholesale.

### 2026-09-05 Compiler Incident Decision

The design retires every registered and planned `ZOM9900-ZOM9999` invariant
code. `ZOMxxxx` is restricted to user- or operator-actionable diagnostics. Each
subsystem retains its exact internal invariant algebra and projects it to a
sanitized `CompilerIncident` with an opaque 128-bit fingerprint. CLI output uses
the fixed `internal compiler error` record and existing failure status `1`; IDE
and LSP surfaces report request failure rather than a source diagnostic. No
umbrella `ZOM9900`, compatibility alias, or replacement public ICE namespace is
created.

### 2026-09-05 Required-Owner Review Round One

All ten non-governance required owners reviewed proposal
`8a959037ccce5ad479b0afa0c8ad55ab8bbfd2866382fe8db4806f643fe8676d`
and tracker
`7399169261c5def66d0c5a31d55d743788072d19648eda5faa1e032b5948f142`.
Every owner returned `OBJECT`; no approval from that snapshot carries forward.
The findings covered source-sink transactions and budgets, typed Checker
arguments and path bounds, query determinism and cache transparency, snapshot
leases and pre-package manifests, output and recovery failures, ownership cause
completeness and consumer memory, IDE per-document publication, cross-RFC/spec
replacement scope, generated inventories, fuzz/performance reproducibility, and
routing/exact-scope governance. The revised snapshot must be reviewed afresh by
every required owner.

### 2026-09-05 Required-Owner Review Round Three

All eleven required owners reviewed proposal
`02ad334c1174f797832e3492811a5def006581c81994e10c82d068dc70dbf73c`,
routing
`c68410c7bbc387c021badd1833440d7036ccd312088c30d3bba7cabc3307609f`,
and tracker
`be06f97f8b938c126653ade516c1c6f6e97a749a09f6000e190d72dca67c3f90`.
`task-router`, `error-system`, `module-system`, and `runtime-memory` approved.
`rfc`, `lexer-parser`, `binder-checker`, `ir-backend`, `tooling-lsp`,
`spec-audit`, and `verification` objected. The snapshot was returned; no approval
carries forward. The blocking findings were inconsistent tracker status, absent
Binder and Checker production projector routes, missing LinkPlan cause-split
files, unspecified source-root capacity failure, unlisted current-design catalog
claims, incomplete document-version input-frontier routes, and missing
query-descriptor schema repair authority.

### 2026-09-05 Required-Owner Review Round Four

All eleven required owners approved the unchanged proposal
`279023f70a27166a9e902f38cabeeab1e50d8da6546a1b32a49b393c424d3d8a`,
routing
`d53fdf81ef7cd682d0f49ee0d891b0d5318d151ef67738cc9f97171d67b09276`,
and tracker
`3d0a38bc9966795031d141e5e6a8420a83a61c791343dbb7533d5aa9d4a03303`.
The review found no unresolved blocker. These approvals authorize the RFC
decision only; no product implementation or cutover occurred during review.

## Decision Record

RFC 0047 is accepted on the unanimous fourth-round owner review of the exact
snapshot recorded above. No approval carried forward from a returned snapshot.
Implementation may proceed only through the frozen routing DAG and its required
verification; no product implementation or cutover has started.

## Current Review Snapshot

| Field | Value |
|---|---|
| Status | `ACCEPTED` |
| Proposal SHA-256 | `279023f70a27166a9e902f38cabeeab1e50d8da6546a1b32a49b393c424d3d8a` |
| Routing SHA-256 | `d53fdf81ef7cd682d0f49ee0d891b0d5318d151ef67738cc9f97171d67b09276` |
| Review manager | `rfc` |
| Required owner review | All eleven required owners approved the unchanged fourth review snapshot |
| Implementation | Authorized by the accepted design and frozen routing; not started |

The review request distributes this tracker's SHA-256 out of band because a
document cannot contain a stable digest of itself. Any tracker edit invalidates
the distributed digest and requires a new exact-snapshot review request.

## Review Tracker

| Task | Owner | Depends On | Deliverable | Verification | Status |
|---|---|---|---|---|---|
| `R47-01` | `rfc` | None | Complete the initial RFC, tracker, prior-art citations, and RFC index row. | `python3 scripts/check-rfc.py` | Complete; draft authored and structural gate passed |
| `R47-01A` | `rfc` with `error-system`, `module-system`, and `verification` design audits | `R47-01` | Resolve the typed catalog, production root/provenance inventory, compiler incident, query failure, and performance-protocol design blockers. | Independent architecture and governance readiness review | Complete; contracts frozen without recording owner approval |
| `R47-02` | `error-system` | `R47-01` | Review catalog, fact algebra, collector, materializer, policy, invariant rail, incident fingerprint and rendering, `ZOM99xx` retirement, and deletion scope. | Exact proposal review with findings | Complete; approved fourth exact snapshot |
| `R47-03` | `lexer-parser` | `R47-01` | Review the typed source sink, recovery, provenance, and work budgets. | Exact proposal review with findings | Complete; approved fourth exact snapshot |
| `R47-04` | `binder-checker` | `R47-01` | Review typed issue inventories, projector exhaustiveness, related records, and source-visible failure coverage. | Exact proposal review with findings | Complete; approved fourth exact snapshot |
| `R47-05` | `module-system` | `R47-01` | Review query roots, cache semantics, provenance origins, session ownership, package diagnostics, invariant request outcomes, and ICE IDE/LSP behavior. | Exact proposal review with findings | Complete; approved fourth exact snapshot |
| `R47-06` | `ir-backend` | `R47-01` | Review HIR/MIR/LIR/backend issue projection and invariant termination. | Exact proposal review with findings | Complete; approved fourth exact snapshot |
| `R47-07` | `runtime-memory` | `R47-01` | Review Ownership issue projection, event-local causal suppression, memory bounds, and safety. | Exact proposal review with findings | Complete; approved fourth exact snapshot |
| `R47-08` | `tooling-lsp` | `R47-01` | Review authoritative and recovery batch projection, document-version freshness, LSP conversion, and cancellation. | Exact proposal review with findings | Complete; approved fourth exact snapshot |
| `R47-09` | `spec-audit` | `R47-01` | Review language-code ownership, current-versus-proposed claims, and design/spec documentation boundaries. | Exact proposal review with findings | Complete; approved fourth exact snapshot |
| `R47-10` | `verification` | `R47-01` | Review test matrix, generated inventories, negative gates, fuzzing, differential checks, performance, and exact cutover scope. | Exact proposal review with findings | Complete; approved fourth exact snapshot |
| `R47-10A` | `task-router` | `R47-01` | Review contributor-rule synchronization and formally assign `compiler/cst/**`, `compiler/ide/**`, `compiler/lsp/**`, `.codex/rules/**`, and diagnostics verification scripts before implementation. | Manifest, routing, atomic task, and exact proposal review | Complete; approved fourth exact snapshot |
| `R47-10C` | `rfc` | `R47-01A` | Draft `docs/rfc/tracking/0047-implementation-routing.yml` with one literal path, closed status, primary owner, atomic task, reviewers, and dependencies for every existing or planned file in the cutover. | Routing schema validation, complete current-tree census, planned-file census, single publication-sink DAG proof, line-budget/deletion-exemption validation, and mutation self-test | Complete; 483-path, 200-task candidate passed architecture, governance, and task-router readiness audits without implying owner approval |
| `R47-10B` | `verification` | `R47-01` | Freeze the six-case benchmark corpus, Release build and exact-machine protocol, 5+21 samples, comparable-case p50/p95/MAD ratios, candidate-only absolute and scaling limits, semantic-delta oracles, RFC 0023 absolute limit, proposed runner paths, and commands. | Exact protocol review against RFCs 0017 and 0023 | Complete; protocol recorded in RFC, implementation evidence remains `R47-21` |
| `R47-11` | `rfc` | `R47-01A`; `R47-10B` | Freeze one proposal and tracker snapshot and enter `REVIEW`. | Proposal and tracker SHA-256 plus `python3 scripts/check-rfc.py` | Complete; REVIEW snapshot frozen after independent architecture and governance readiness review |
| `R47-11A` | `rfc` | `R47-10C` | Resolve round-one objections, transition the revised RFC and index from `DRAFT` to `REVIEW`, append status history, bind the proposal hash into routing, record proposal/routing hashes in the tracker, and distribute the tracker hash out of band. | Exact hashes of all three review artifacts plus `python3 scripts/check-rfc.py` | Complete; revised REVIEW snapshot frozen after independent architecture and governance readiness audits |
| `R47-11B` | `rfc` | `R47-10C` | Resolve second-round objections and execute the next `DRAFT` to `REVIEW` three-artifact freeze transaction. | Exact hashes plus RFC and routing checks | Complete; third REVIEW snapshot frozen for unchanged-snapshot owner review |
| `R47-11C` | `rfc` | `R47-10C` | Resolve third-round objections and execute the next `DRAFT` to `REVIEW` three-artifact freeze transaction. | Exact hashes plus RFC and routing checks | Complete; fourth REVIEW snapshot frozen for unchanged-snapshot owner review |
| `R47-12` | `rfc` | `R47-02`; `R47-03`; `R47-04`; `R47-05`; `R47-06`; `R47-07`; `R47-08`; `R47-09`; `R47-10`; `R47-10A`; `R47-11C` | Conduct unchanged-snapshot owner review without fabricated signoff. | Exact hashes of RFC, tracker, and routing artifact | Complete; all eleven required owners approved the fourth exact snapshot |
| `R47-13` | `rfc` | `R47-12` | Record the decision and, only with complete approvals and no blocker, move `REVIEW` to `ACCEPTED`. | RFC, tracker, routing allowlist, and repository gates | Complete; accepted after unanimous fourth-round review |

## Implementation Tracker

No product edit is authorized while RFC 0047 is under `DRAFT` or `REVIEW`. `R47-10C` must
freeze the exact path/status/owner/task allowlist before acceptance. All product
tasks operate in one cumulative integration tree and publish one atomic cutover;
no task below authorizes a partial production path. A task's owner writes only
its listed path family; named reviewers do not share write ownership. Each task
must be split before acceptance if its reviewed added/modified source diff would
exceed 400 lines. One deletion-only task may remove one larger obsolete file
under the RFC's explicit whole-file-deletion exemption.
The routing YAML is the sole implementation dependency graph and completion
authority. The rows below are area milestones: `R47-22` means every `R47-22*`
file task is complete, and similarly for later prefixes. A milestone dependency
does not add an edge to every same-prefix file task; only each routing entry's
`depends_on` field controls execution. This prevents summary rows from creating
cycles with intentionally late deletion tasks.

| Task | Owner | Depends On | Deliverable | Verification | Status |
|---|---|---|---|---|---|
| `R47-20` | `task-router` | `R47-13` | Update `.codex/subagents/{manifest.yaml,README.md,task-router.md,binder-checker.md,error-system.md,lexer-parser.md,module-system.md,spec-audit.md,tooling-lsp.md,verification.md`, `AGENTS.md`, and affected `.codex/rules/*.md` with the exact ownership assignments in RFC 0047, including `compiler/basic/incident/**`, `docs/package-system.md`, and `docs/plan/**` ownership in the manifest, routing matrix, and owner instructions. | Routing manifest, English-only, and RFC checks | Authorized; not started |
| `R47-21` | `verification` | `R47-20`; frozen `R47-10C` routing allowlist | Add `diagnostics-architecture.yml`, independent architecture census, semantic/performance manifests, and their check/self-test drivers under `scripts/**` and `tests/**`; make no compiler product edit and do not change path ownership. | Every new checker and mutation self-test | Pending |
| `R47-22` | `error-system` | `R47-21` | Replace `compiler/diagnostics/**` and `compiler/checker/checker-source-diagnostics.def` with the catalog, typed arguments, fact/codec, request-level incident fingerprint/rendering, collector, materializer, policy, terminal/test consumer, and validators; delete diagnostics-owned engine/emitter/state and 9xxx definitions. | Focused diagnostic unit, codec, incident rendering, renderer, and fuzz tests | Pending |
| `R47-23` | `module-system` | `R47-22` | Add the dependency-minimal `compiler/basic/incident/**` contract and replace `compiler/query/**`, `compiler/source/**`, `compiler/identity/**`, `compiler/driver/**`, and `compiler/binder/module-*` failure/root/provenance/session paths, including deterministic incident aggregation, invocation/pre-package manifest roots, leases, package/build-script projectors, and output-recovery propagation. | Query, incident inventory, provenance, package, session, cache, and permutation tests | Pending |
| `R47-24` | `lexer-parser` | `R47-23` | Replace `compiler/lexer/**`, `compiler/parser/**`, `compiler/ast/**`, and `compiler/cst/**` diagnostic emission with the typed source sink, exact transaction/budget semantics, canonical source paths, and CST rebinding. | Source sink, recovery 99/100/101, CST, parser unit, and lit tests | Pending |
| `R47-25` | `binder-checker` | `R47-24` | Replace diagnostics under `compiler/binder/**` excluding `compiler/binder/module-*`, `compiler/checker/**` excluding `compiler/checker/checker-source-diagnostics.def`, and `compiler/type/**`, preserving all typed argument alternatives and 4,096-component local paths; delete owner-local adapters. | Binder/Checker projector, codec, query, and lit tests | Pending |
| `R47-26` | `runtime-memory` | `R47-25` | Replace `compiler/ownership/**` diagnostic projection and suppression with complete cause retention and `OwnershipEvent` suppression keys; delete ownership adapters. | Ownership cause, suppression, limit, permutation, and sanitizer tests | Pending |
| `R47-27` | `ir-backend` | `R47-26` | Replace `compiler/{hir,mir,lir,ir,backend}/**` and `utils/zomc/**` diagnostic projection, split `ZOM6008` causes, preserve recovery obligations, and delete IR/backend adapters. | IR/backend/output/recovery/CLI tests | Pending |
| `R47-28` | `tooling-lsp` | `R47-27` | Replace diagnostic projection under `compiler/ide/**`, `compiler/lsp/**`, `tools/ide/**`, `tools/lsp/**`, and `editors/**` with per-document authority, witness-bound publication, versioned suppression, and bounded DTO/LSP staging. | IDE/LSP differential, stale, cancellation, UTF-16, cross-document, and hostile tests | Pending |
| `R47-29` | `spec-audit` | `R47-28`; owner-specific `R47-29L`, `R47-29E`, `R47-29M`, and `R47-29R` spec edits | Audit the affected owner-edited `docs/spec/**`, then synchronize non-tooling `docs/design/**` and `docs/overview.md` to the landed catalog and current behavior. | Spec alignment and English-only checks | Pending |
| `R47-30` | `tooling-lsp` | `R47-28` | Synchronize `docs/design/tooling/**` to the landed IDE/LSP behavior. | Tooling design audit and English-only check | Pending |
| `R47-31` | `rfc` | `R47-29`; `R47-30` | Synchronize the exact RFC/tracker set named by RFC 0047, preserving historical rows and replacing active contracts only. | Zero stale normative mapping census and RFC check | Pending |
| `R47-32` | `verification` | `R47-31` | Run the complete architecture, coverage, fuzz, performance, sanitizer, unit, lit, conformance, format, English, versioning, and exact-scope matrix in the isolated cumulative worktree. | Full RFC 0047 Test Plan | Pending |
| `R47-33` | `error-system` | `R47-32` | Publish the already verified cumulative atomic product cutover without editing its contents. | Local, integration, remote, generated-hash, and exact-scope parity | Pending |
| `R47-34` | `rfc` | `R47-33` | Audit landed evidence, update status and current docs, and close the tracker. | RFC status and evidence audit | Pending |

## Published Evidence

- Initial current-state audit: local source inspection on 2026-09-05; no product
  files changed and no build claimed.
- Prior-art revisions reviewed on 2026-09-05: rustc `0ed41eb4142d`,
  rust-analyzer `bf3e4a314123`, Salsa `65604afad5ff`, LLVM
  `8c536a50e6f0`, Swift `a87a6454b140`, and Roslyn `0e119d1bc176`.
- Independent architecture and governance reviews challenged the initial draft;
  every material finding was resolved before the `REVIEW` transition. The final
  readiness reviews found no remaining draft blocker. No owner approval was
  inferred from those reviews.
- The compiler-incident follow-up review required exact query propagation,
  fingerprint framing, operational-failure separation, RFC 0023 notification
  behavior, a producer-level disposition ledger, and synchronized prior RFC
  ownership. The accepted design incorporates those corrections, and the fourth
  exact-snapshot owner review approved them.
- The catalog-design audit froze partitioned YAML sources, six checked-in C++
  outputs, generated typed factories and producer inventory, strict validation
  and mutation rules, and exact generator commands. No product file was changed
  during review; the fourth exact-snapshot owner review approved the contract.
- The root-inventory audit froze request-local Invocation, Source, Package,
  BuildScript, Module, and Compilation roots, excluded non-live CoreLibrary and
  IDE recovery roots, and resolved the pre-`PackageRootSetKey` invocation gap.
  No product file was changed during review; the fourth exact-snapshot owner
  review approved the contract.
- The performance-design audit froze the six-case Release protocol, exact
  environment matching with separate measured-artifact identity, 5+21 sampling,
  p50/p95/MAD metrics, comparable-case 5 percent latency and 15 percent RSS
  thresholds, candidate-only absolute and scaling limits, exact semantic-delta
  oracles, and RFC 0023's 500 millisecond absolute IDE limit. The dedicated
  runner and pre-cutover baseline are implementation deliverables; no RFC 0047
  benchmark result is claimed yet.
- Exact-hash required-owner review and acceptance are complete on the fourth
  snapshot. Implementation evidence remains pending because implementation has
  not started.
