# `verification` — Formal-ish Verification & Test Coverage

## Mission

Own ZOM's test strategy, coverage gates, fuzz and stress harnesses, lit
test hygiene, XFAIL expiration, and the adversarial verification of
bug fixes before they are considered closed.

## Use When

Route here when **any** of these are true:

- A test fails, is flaky, or needs to be written.
- Coverage dropped below baseline, or a new source file has < 70% line
  coverage.
- `XFAIL` tests need triage (are they still failing? can they be
  promoted? can we just delete them?).
- Another subagent escalates here for a "prove this fix works / doesn't
  regress / doesn't open a new bug."
- Writing a stress / fuzz / adversarial reducer against the lexer,
  parser, or binder.
- Adding or changing the incremental-query architecture gate, edit corpus,
  cache adversaries, projection execution assertions, or benchmark baseline.
- Adding or changing the query descriptor inventory generator or descriptor
  architecture gate.
- Adding or changing query-runtime request-result alternatives, private
  real-object decoder mismatch tests, deterministic one-shot race gates, or
  CTest negative-compilation fixtures.
- Adding or changing the source-backed core-library architecture gate.
- Requesting the user-facing report of "this is safe to ship."
- Verifying every cast mode and its parser, checker, MIR, panic, and negative
  invariant matrix, including `as!` failure behavior.

Do **not** route here when:
- The fix is trivial and the owning subagent already has the right lit
  test in their PR. Route here only for the *verification pass* after
  the fix is written, or if the testing strategy itself is complex.

## Owns

```
zomlang/tests/**
zomlang/tests/cmake/expect-compile-failure/CMakeLists.txt
zomlang/tests/compile-fail/query-runtime/**
zomlang/tests/unittests/compiler/query/query-test-specs.h
zomlang/tests/unittests/compiler/query/query-concurrency-test.cc
examples/**
.github/workflows/**
README.md
docs/reports/*coverage*
scripts/check-identity-architecture.py
scripts/check-ir-architecture.py
scripts/check-incremental-query-architecture.py
scripts/check-binder-architecture.py
scripts/check-stable-binding-schema.py
scripts/check-landing-scope.py
scripts/check-checker-architecture.py
scripts/check-compiler-session-architecture.py
scripts/check-impl-source-architecture.py
scripts/check-package-architecture.py
scripts/check-lexer-architecture.py
scripts/check-parser-coverage.py
scripts/check-format.py
scripts/check-english-only.py
scripts/check-spec-alignment.py
scripts/check-rfc.py
scripts/check-no-internal-versioning.py
scripts/codegen/gen_ast.py
scripts/codegen/gen_package_oracles.py
scripts/generate-canonical-header-syntax-schema.py
scripts/generate-query-descriptor-schema.py
scripts/check-query-descriptor-architecture.py
scripts/run-incremental-query-benchmarks.py
scripts/check-diagnostic-coverage.py
scripts/check-lit-exec-root.py
scripts/check-core-library-architecture.py
scripts/check-core-library-spec-alignment.py
scripts/codegen/gen_core_library_inventory.py
zomlang/tests/coverage/diagnostic-reservations.json
zomlang/tests/coverage/rfc-0030-stable-binding-landing-files.txt
scripts/run-rfc0016-coverage.py
scripts/check-rfc0016-coverage.py
scripts/check-ownership-architecture.py
scripts/run-ownership-coverage.py
scripts/check-ownership-coverage.py
scripts/check-diff-hygiene.py
scripts/check-ownership-determinism.py
zomlang/tests/coverage/ownership-exemptions.json
zomlang/tests/coverage/ownership-coverage-baseline.json
zomlang/tests/coverage/ownership-determinism-baseline.json
zomlang/tests/performance/incremental-query-corpus.json
zomlang/tests/performance/incremental-query-baseline.json
cmake/utils/common.cmake
cmake/utils/coverage.cmake
cmake/utils/unittests.cmake
```

## Review Checklist (applies to every PR this subagent touches)

- [ ] Every `XFAIL` test has a finding / ticket link. No orphans.
- [ ] No test asserts purely on human-readable English diagnostic text;
      every negative test asserts the `ZOMxxxx` code first.
- [ ] No "flaky tests" list. If a test is flaky and the root cause is
      not fixed within 48h → delete the test per Rule #4.
- [ ] FileCheck style: `CHECK-NEXT` for adjacency, `CHECK-SAME:` for
      same-line; `CHECK-DAG` only when order is truly arbitrary and a
      comment explains why.
- [ ] Regenerated lit tests have been eyeballed by a human diff.
- [ ] New compiler source files have ≥ 70% line coverage or an explicit
      "FFI boundary / unreachable" exemption.
- [ ] Changed repository text artifacts contain no CJK characters; the
      project-native English-only gate and its mutation self-test pass.
- [ ] Every defined diagnostic is either emitted or bound to an active RFC
      tracker, every production emission is defined, and every emitted
      diagnostic is asserted by a test; both diagnostic coverage gate modes
      pass.
- [ ] No `Thread.sleep`-style wall-clock waits. Use explicit sync
      primitives or bounded poll loops.
- [ ] Lit execution roots are stable build-local paths. No runner creates
      per-invocation directories under the source tree or overrides lit's
      test-unique `%t` substitution.
- [ ] Incremental-query changes pass from-scratch equivalence, exact provider
      execution-set assertions, worker permutations, cache adversaries, and the
      reviewed benchmark protocol.
- [ ] Query-runtime decoder mismatch tests use only independently reachable
      real database, revision, and descriptor coordinates; no memo-field
      mutator or unreachable inventory coordinate is accepted.
- [ ] Query-runtime compile-fail cases use the reusable CMake fixture, inherit
      the configured compiler, include roots, and C++ mode, compile as
      `STATIC_LIBRARY`, and require the exact forbidden symbol.
- [ ] Query-runtime race tests use explicit per-database one-shot state and
      condition synchronization, with no sleep, callback, global registry, or
      verifier replacement.
- [ ] Stable-binding foundation changes compile both fact and codec sources,
      execute every fixed wire oracle through native ztest, register the four
      exact RFC 0030 CTest targets, pass positive and mutation schema modes,
      and pass Binder architecture checks.
- [ ] RFC 0030 `R29-12AB` worktree and staged-index path/status sets equal the
      accepted allowlist. The immutable implementation-series base remains
      unchanged, and unrelated staged, unstaged, or untracked files are
      rejected.

## Required Evidence Before Closing

- [ ] `ctest --preset default` passes with zero unexpected failures.
- [ ] `python3 scripts/check-diagnostic-coverage.py --check` and
      `python3 scripts/check-diagnostic-coverage.py --self-test` pass.
- [ ] `python3 scripts/check-english-only.py --check --base-file
      zomlang/tests/coverage/implementation-series-base.txt` and
      `python3 scripts/check-english-only.py --self-test` pass.
- [ ] `python3 scripts/check-diff-hygiene.py --check` and
      `python3 scripts/check-diff-hygiene.py --self-test` pass.
- [ ] `python3 scripts/check-ownership-coverage.py --self-test` passes; when
      the coverage preset is built, `python3 scripts/run-ownership-coverage.py`
      followed by `python3 scripts/check-ownership-coverage.py` passes with no
      per-file baseline regression and no aggregate baseline regression.
- [ ] `python3 scripts/check-ownership-determinism.py --check --zomc <zomc>`
      and `python3 scripts/check-ownership-determinism.py --self-test` pass;
      repeated processes produce byte-identical ownership diagnostics and the
      recorded baseline hashes match.  The worker-count 1/2/4/8 permutation
      matrix is enforced at the unit-test level for the query/driver rail
      (`active-definition-authority-session-test.cc`) and the ownership rail
      revision determinism (`ownership-event-overlay-test.cc`); a CLI
      worker-count control is a compiler change outside this gate.
- [ ] `python3 scripts/check-spec-alignment.py --check --report
      <build-local-report>` and
      `python3 scripts/check-spec-alignment.py --self-test` pass.
- [ ] `python3 scripts/check-lit-exec-root.py --check` and
      `python3 scripts/check-lit-exec-root.py --self-test` pass.
- [ ] `python3 scripts/check-incremental-query-architecture.py --check` and
      `python3 scripts/check-incremental-query-architecture.py --self-test`
      pass.
- [ ] `python3 scripts/check-stable-binding-schema.py --check`,
      `python3 scripts/check-stable-binding-schema.py --self-test`, and
      `python3 scripts/check-landing-scope.py --self-test` pass when the
      RFC 0030 foundation is in scope.
- [ ] Coverage report (when the task is coverage-gated) confirms no
      regressions vs. the last baseline.
- [ ] For bug fixes: regression test fails on `HEAD^`, passes on HEAD.
- [ ] For fuzz / stress runs: exact command + seed + duration +
      iterations attached to the PR / report so anyone can reproduce.

## Block Conditions (auto-escalate to `escalation-to` when hit)

- A test failure reproduces but root cause is genuinely in the spec
  contract (the "correct" behavior is ambiguous, not the code) →
  escalate to `spec-audit` to resolve the contract first.
- Verification uncovers a drift that the owning subagent believed was
  already fixed → escalate to `spec-audit` for a targeted five-way
  sweep, then re-verify.
