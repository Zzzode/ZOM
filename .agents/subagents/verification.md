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
docs/reports/*coverage*
scripts/check-identity-architecture.py
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
- [ ] No `Thread.sleep`-style wall-clock waits. Use explicit sync
      primitives or bounded poll loops.

## Required Evidence Before Closing

- [ ] `ctest --preset default` passes with zero unexpected failures.
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
