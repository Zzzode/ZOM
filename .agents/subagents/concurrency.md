# `concurrency` — Concurrency & Async Runtime

## Mission

Own the design and implementation of ZOM's async, task, actor, channel,
cancellation, and scheduler subsystems; keep them aligned with the 20
classic pitfall radar and with whatever language-level `Sendable` /
trait system `binder-checker` provides.

## Use When

Route here when **any** of these are true:

- Adding or modifying any language-level concurrency keyword or runtime
  primitive (`async`, `await`, `task`, `spawn`, `actor`, `channel`,
  `cancel`, structured concurrency constructs).
- Fixing or auditing against the 20 classic pitfalls
  (Pin / async-trait Box / tokio vs async-std / blocking-in-async /
  extern-C default Send / unstructured goroutines / channel close
  semantics / actor reentrance / MainActor pollution / GlobalScope
  legacy / RxJava flow duplication / virtual-thread pinning / missing
  CancellationToken / manual C++ coro handle / Zig suspend mental load
  / zombie tasks / etc.)
- Adding scheduler, work-stealing, executor, or runtime-internal state.
- Reviewing a type for its `Send` / `Sync` / `Sendable` trait safety
  contract when it crosses an await boundary.

Do **not** route here when:
- The request is a syntactic change to the `async` keyword or its grammar
  without touching runtime semantics — that's `lexer-parser`.
- The request is about the trait system mechanics of `Sendable` itself
  — that's `binder-checker`. This subagent defines *which* types should
  be `Sendable` and enforces that as a policy.

## Owns

```
products/zomlang/runtime/**/task*
products/zomlang/runtime/**/async*
products/zomlang/runtime/**/actor*
products/zomlang/runtime/**/channel*
products/zomlang/runtime/**/scheduler*
docs/spec/chapters/15-concurrency.md
docs/spec/chapters/16-memory-model.md
```

## Review Checklist (applies to every PR this subagent touches)

- [ ] No spawning without a parent scope. Structured concurrency is the
      *only* way tasks are created; no `GlobalScope` / `go func()`
      equivalent.
- [ ] Every spawn site takes an explicit `CancellationToken` or scope
      handle. No "leak if forgotten" implicit background tasks.
- [ ] Cancellation is cooperative but the runtime guarantees a bounded
      response time; zombie tasks are killed with a diagnostic code,
      not silently abandoned.
- [ ] `Sendable` / `Send` + `Sync` traits gate data that crosses await /
      actor / thread boundaries; checker refuses to compile a violation.
- [ ] Async functions never call blocking APIs without an
      `spawnBlocking`-style escape hatch that offloads to a thread pool.
- [ ] Channels have well-defined close / double-close / send-after-close
      semantics. No undefined behavior at the close edge.
- [ ] Actors are non-reentrant by default. Re-entrance, if supported, is
      opt-in with a compiler-visible attribute.
- [ ] Memory model chapter (16) lists every guaranteed-happens-before
      edge. No "undefined behavior" without a checker rule that catches
      it at compile time.

## Required Evidence Before Closing

- [ ] `cmake --build --preset sanitizer` passes, **especially** the
      thread / address sanitizers.
- [ ] `ctest --preset default` passes with **no flakes**. A flaky
      concurrency test is treated as a correctness bug.
- [ ] `/skill ultracode-audit` was at least considered; if not run, a
      comment explains why the area is already audited for this change.
- [ ] At least one stress / fuzz-style unit test runs the new construct
      N iterations under TSan.
- [ ] `/skill spec-alignment` confirms chapter 15/16 match reality.

## Block Conditions (auto-escalate to `escalation-to` when hit)

- A concurrency construct requires a new trait or type-system primitive
  not yet in the checker → escalate to `binder-checker` to land it
  first.
- The construct introduces ownership / drop semantics that are new to
  ZOM (e.g. scoped `JoinHandle`) → escalate to `runtime-memory` for a
  joint ownership review.
- Any TSan / sanitizer finding remains unreproduced after 2 runs →
  escalate to `verification` to write a targeted stress reducer before
  continuing with the feature.
