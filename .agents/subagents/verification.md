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
products/zomlang/tests/**
examples/**
.github/workflows/**
README.md
docs/reports/*coverage*
scripts/check-identity-architecture.py
scripts/check-ir-architecture.py
scripts/check-incremental-query-architecture.py
scripts/check-binder-architecture.py
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
products/zomlang/tests/coverage/diagnostic-reservations.json
scripts/run-rfc0016-coverage.py
scripts/check-rfc0016-coverage.py
scripts/check-ownership-architecture.py
scripts/run-ownership-coverage.py
scripts/check-ownership-coverage.py
products/zomlang/tests/coverage/ownership-exemptions.json
products/zomlang/tests/performance/incremental-query-corpus.json
products/zomlang/tests/performance/incremental-query-baseline.json
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

## Required Evidence Before Closing

- [ ] `ctest --preset default` passes with zero unexpected failures.
- [ ] `python3 scripts/check-diagnostic-coverage.py --check` and
      `python3 scripts/check-diagnostic-coverage.py --self-test` pass.
- [ ] `python3 scripts/check-english-only.py --check --base-file
      products/zomlang/tests/coverage/implementation-series-base.txt` and
      `python3 scripts/check-english-only.py --self-test` pass.
- [ ] `python3 scripts/check-spec-alignment.py --check --report
      <build-local-report>` and
      `python3 scripts/check-spec-alignment.py --self-test` pass.
- [ ] `python3 scripts/check-lit-exec-root.py --check` and
      `python3 scripts/check-lit-exec-root.py --self-test` pass.
- [ ] `python3 scripts/check-incremental-query-architecture.py --check` and
      `python3 scripts/check-incremental-query-architecture.py --self-test`
      pass.
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
