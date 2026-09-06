# RFC 0047 Review And Implementation Tracker

## Discussion Record

RFC 0047 was created after a repository-wide diagnostic-path audit and review of
Rust, Salsa, rust-analyzer, Clang, Swift, and Roslyn. Review established the
following durable decisions:

- user-actionable diagnostics and compiler incidents are separate rails;
- query-safe canonical facts are semantic results, not emission side effects;
- one sealed request-level fact set feeds all normal consumers;
- provenance is resolved atomically against the matching retained snapshot;
- direct-emission adapters and mutable engine policy are migration inputs, not
  architectural extension points; and
- internal compatibility layers are not permitted.

The accepted review snapshot also specified a parallel YAML catalog, generated
artifacts, an exact 483-path routing ledger, and RFC-specific Python scanners,
fuzz drivers, benchmark runners, and landing-scope checks. Implementation review
found that these mechanisms duplicate the existing `.def` catalog and do not
prove C++ dependency or runtime data-flow properties. The implementation
contract was therefore narrowed, without changing the accepted semantic model,
to compile-time C++ validation, native tests, sanitizer builds, and actual CLI
and IDE behavior. No RFC-specific source-text architecture checker is required.

## Decision Record

The proposal was accepted after all required owner reviews. Implementation began
with the incident rail and the removal of public invariant diagnostics. The RFC
is now `LANDED`.

The implementation uses the current `.def` catalog as the sole source of public
diagnostic metadata. It does not introduce the proposed YAML mirror, generated
catalog files, or source-scanning architecture gates. This correction follows
the RFC's stated goals and the project rule to prefer the simplest complete
design over speculative infrastructure.

## Review Findings

| Finding | Resolution | Status |
|---|---|---|
| Public `ZOM99xx` values represented compiler invariants | Added the bounded internal incident rail and migrated active invariant families without an umbrella public code | Resolved |
| Query-adjacent direct emission can differ between cold and warm execution | Canonical facts are query values and no production direct emitter remains | Resolved |
| Engine deduplication uses only code and source location | The collector uses occurrence identity and rejects conflicting payloads through `DiagnosticCollectionFailure::ConflictingOccurrence` | Resolved |
| Materialization and consumer APIs expose mutable presentation objects | `materializeDiagnosticFacts` returns one immutable all-or-nothing `ResolvedDiagnosticBatch`, and `applyDiagnosticPolicy` computes a display view without modifying it | Resolved |
| IDE projection can degrade invalid provenance to a range-less diagnostic | Invalid provenance fails the whole materialization closed and publishes no batch | Resolved |
| YAML generation duplicated the live `.def` catalog | `.def` remains the sole source and gains compile-time validation | Resolved by design correction |
| Python text scans were treated as architecture proof | Compiled boundaries, native tests, and product behavior are the required evidence | Resolved by design correction |
| Exact path routing became stale as implementation progressed | Routing now records owning areas, dependencies, completion criteria, and evidence rather than frozen paths | Resolved by design correction |

## Implementation Tracker

Each stage must leave one compiled behavior for the migrated slice. A producer
must never send the same occurrence through both old and new paths. Deletion of
the old API is part of the stage that removes its last production caller.

| Task | Owner | Depends On | Deliverable | Verification | Status |
|---|---|---|---|---|---|
| `R47-20` | `task-router`, `rfc` | Acceptance | Align RFC, contributor rules, and routing with native C++ evidence | RFC, English, and diff checks | Complete |
| `R47-21` | `module-system`, producer owners | Acceptance | Add bounded incident descriptors and migrate every active invariant family while deleting its `ZOM99xx` entry | Focused incident, owner, session, CLI tests | Complete |
| `R47-22` | `error-system` | `R47-20` | Strengthen the `.def` catalog, facts, and codecs with compile-time and runtime fail-closed validation | Diagnostic catalog/fact/codec ztests | Complete |
| `R47-23` | `error-system` | `R47-22` | Add occurrence-aware collection, deterministic ordering, immutable resolved batches, and explicit policy | Collector, materializer, ordering, budget ztests | Complete |
| `R47-24` | `lexer-parser` | `R47-23` | Replace source RAII emission with typed source fact construction and preserve parser transaction semantics | Lexer/parser unit, recovery boundary, lit tests | Complete |
| `R47-25` | `binder-checker`, `runtime-memory` | `R47-23` | Replace Binder, Checker, borrow, and ownership emission adapters with exhaustive fact projectors | Projector, query reuse, ownership, lit tests | Complete |
| `R47-26` | `module-system`, `ir-backend` | `R47-23` | Replace package, driver, module-interface, IR capability, backend, and CLI emission adapters with facts or typed operational failure | Session, package, IR/backend, CLI tests | Complete |
| `R47-27` | `error-system`, `tooling-lsp` | `R47-24`; `R47-25`; `R47-26` | Make the sealed resolved batch the sole terminal and IDE consumer input; reject stale or invalid provenance | Terminal/IDE parity, stale, cancellation tests | Complete |
| `R47-28` | Producer owners, `error-system` | `R47-27` | Delete `DiagnosticEngine`, mutable state, RAII emitter, in-flight diagnostic, direct adapters, and dead tests | Clean build and zero production caller audit | Complete |
| `R47-29` | `spec-audit`, `rfc` | `R47-28` | Publish current diagnostics design documentation and synchronize affected current contracts | RFC, English, documentation review | Complete |
| `R47-30` | `verification` | `R47-29` | Run sanitizer build, full CTest, conformance, format, RFC, and representative CLI/IDE workflows | Recorded command results | Complete |
| `R47-31` | `rfc` | `R47-30` | Audit acceptance criteria and move RFC 0047 to `LANDED` | Full diff and evidence review | Complete |

`docs/rfc/tracking/0047-implementation-routing.yml` is a planning aid for these
stages. It is not a source allowlist or an architecture proof. The current tree,
build graph, public headers, native tests, and product behavior are authoritative.

## Published Evidence

- `2f1d5cc6` added the dependency-minimal bounded compiler incident rail.
- `60999305` migrated identity invariants to incidents.
- `75526808` migrated Checker invariants to incidents.
- `805ba066` migrated Binder invariants to incidents.
- `b372b5b1` migrated module-graph invariants to incidents.
- `2a0f8bf3` migrated module-interface invariants to incidents.
- `b237bd5d` removed dead package invariant diagnostics.
- `ece45e05` migrated IR invariants to incidents.
- The active `.def` catalog contains no `ZOM99xx` entry after those migrations,
  and `static_assert(isValidCatalog())` in `compiler/diagnostics/core/diagnostic-info.h`
  enforces code and name uniqueness, allocation, placeholder correctness,
  argument arity, and `ZOM9900-ZOM9999` exclusion at compile time, so a
  successful build is direct evidence for that acceptance criterion.

## Completion Verification

The following was verified against one coherent tree during the completion pass.

### Product Behavior

- A successful package `zomc compile --check` workflow exits zero with no output.
- The unsupported unwind path reports exactly
  `error: operational failure [target]: panic-unwind-unsupported` and exits one.
- The frontend-only binary path reports exactly
  `error: operational failure [backend]: binary-emission-unavailable` and exits
  one.

### Governance And Architecture Gates

`check-format`, `check-rfc`, `check-english-only`, `check-no-internal-versioning`,
`check-stable-binding-schema`, `check-diagnostic-coverage` (199 defined, 167
emitted, 32 tracked reservations, and 4 of 4 negative fixtures),
`check-parser-coverage`, `check-ownership-architecture`,
`check-ownership-determinism`, `check-compiler-session-architecture` (35 of 35
negative fixtures), `check-identity-architecture`, and `git diff --check` all
pass.

### Defects Found And Fixed During Completion

Each was reproduced independently before being fixed.

- `facts-dispatch-facts-test` failed because the cutover replaced a hand-written
  Binder emission step that emitted only `StableMissingLookupOutcome` and
  silently dropped namespace-mismatch and ambiguous outcomes. The projector
  correctly emits all three, surfacing a genuine `ZOM3002 SymbolNamespaceMismatch`
  for `Holder` in the value namespace. The test source was stale: `Holder` is a
  class and binds in the type namespace, and call-syntax construction is an
  explicit REJECT case in the conformance corpus. Fixed by using brace
  construction and correcting the requirement count, because a struct literal is
  not a dispatch site. A sweep of all nine fixture users found exactly one
  newly surfaced lookup, confirming a one-off test-data defect.
- The cutover deleted the parser production error budget. This was deliberate,
  not an omission: the cutover also added
  `SourceDiagnosticDraftBuffer retains facts beyond the presentation error
  budget`, which asserts that 101 produced facts are all retained and published.
  That test is the executable statement of this RFC's rule that display
  budgeting is not semantic suppression. LANDED RFC 0002 still required a
  100-error parser stop condition, so the two contracts conflicted directly.
  The conflict was resolved in favor of this RFC's retention contract, and
  RFC 0002 was amended on 2026-09-06 to remove both the parse error budget as a
  production stop condition and its deduplication-by-ID-and-location rule, which
  occurrence identity now supersedes. Production is bounded only by the
  fail-closed `kMaximumSourceFacts` ceiling. `recovery-test.cc` was rewritten to
  assert the retention contract and that ceiling rather than a production cap,
  and `check-parser-coverage.py` now anchors on the ceiling.
- `check-parser-coverage.py` read a deleted file and raised an uncaught
  `FileNotFoundError` instead of failing cleanly. Its three anchors did not
  survive the cutover. It now checks the fail-closed source fact ceiling and the
  collector's occurrence-conflict detection, which is this RFC's replacement for
  engine-local deduplication.
- `check-incremental-query-architecture.py` required an AGENTS.md owner summary
  string that this change set correctly rewrote when it added incident transport
  to the module-system row. The gate marker was updated to the current text.
- `check-ownership-architecture.py` still anchored two diagnostics on the
  session file after they moved to the ownership source projector. It now checks
  them at the projector and carries a matching negative self-test.
- Two gate-erosion regressions were repaired: the still-present
  `getIrIdentityInvariantFailures()` marker and its negative self-test were
  restored, and the checker diagnostic authority check was restored and
  repointed at the successor source projector.
- Fourteen ownership sources included `ir-capability-failure-projector.h`
  without using any symbol from it; they now include `ir-failure.h`, the header
  they actually depend on.
- The ownership determinism baseline predated the cutover and drifted for all
  three inputs. The pre-cutover byte stream was reconstructed and sha256-verified
  against every recorded hash, showing a presentation-only delta: colored output
  was previously unconditional even when piped, the summary line is new because
  the old one ran from a destructor that `_exit` skipped, and a misleading CLI
  usage-error trailer was dropped. No code, severity, message, path, position,
  note, ordering, or exit code changed, and diagnostics conformance passes
  unchanged, so the baseline was re-recorded.

### Outstanding

- RFC 0025 stage `R25-09C` is still `pending` and plans against artifacts this
  cutover deleted, including `compiler/diagnostics/diagnostics-core.def`,
  `core-library-diagnostic-adapter.*`, and `incremental-diagnostic-query.*`. It
  also plans to register `ZOM9907`, which the incident rail now forbids. RFC 0025
  is a separate `IMPLEMENTING` RFC and its tracker is outside this change set's
  authorized scope, so the row is recorded here for its owner rather than edited.
- `scripts/check-query-descriptor-architecture.py --check` fails with owner
  path-family and provenance-drift findings. The script and every file it flags
  are untouched by this change set, and the gate is not registered with CTest,
  so the failure is pre-existing and unrelated to this RFC. It is recorded here
  for its owner rather than fixed under this authorization.

### Final Result

The complete `ctest --preset default --output-on-failure` run passed with 328 of
328 tests and exit code zero against the final tree, in 4636 seconds. The
ownership determinism baseline was re-recorded against commit `2c07ca3a`, the
tree that produces the recorded bytes; all three output hashes were unchanged,
confirming the earlier drift was presentation-only.

## Completion Rule

RFC 0047 moves to `LANDED` only when every `R47-20` through `R47-30` stage is
complete, all RFC acceptance criteria have direct evidence, no production
direct-emission caller remains, and the full verification matrix passes.
