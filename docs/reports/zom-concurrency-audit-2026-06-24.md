---
audit: concurrency-system
date: 2026-06-24
scope: async/concurrency model, scheduler/runtime boundaries, memory model, cancellation, and diagnostics
method: multi-expert adversarial review with language benchmark comparison
findings: 44
language: en
status: normalized-english-index
---

# ZOM Concurrency System Audit Report

This report is the English normalized index of the accepted audit findings for this area. It keeps the finding numbers, severity distribution, and actionable titles in a form that can be reviewed and grepped consistently. Detailed evidence should be regenerated in English with the audit workflow before a finding is used as a merge-blocking decision.

## Severity Summary

| Severity | Count |
|---|---:|
| Critical | 0 |
| High | 18 |
| Medium | 19 |
| Low | 4 |
| Info | 3 |

## Finding Index

### High

1. The lexical layer is not recognized ErrorPropagate `?!` is a single token
2. The Spec priority table puts `?!` / `!!` at the same level as `?:`, which is inconsistent with EBNF
3. The join / try_join / select / race / race_ok combinators are all vacant at the language and standard library levels.
4. Await-point type safety gap: no Send/Sync trait, no static validation across await temporary references
5. No unsafe syntax escape hatch, concurrent unsafe API cannot be gated
6. AwaitExpression AST is implemented ahead of time but Parser is never constructed
7. The language-level memory model is completely undefined, and there is no provability of program behavior under multi-core
8. The state machine compilation strategy is completely unselected, and cross-await borrowing and self-referencing structures are blank risks
9. Unspecified data race semantics: UB or atomic read is not clear
10. The four boundaries of Future / Task / Promise / JoinHandle are completely undefined
11. Send / Sync or equivalent traits are completely missing, cross-thread safety and no static checks
12. Parser suffix loop is not consumed `?!` / `!!`, syntax definition and implementation are broken
13. Cancellation semantics (cooperative vs preemptive vs forced) are completely blank and guaranteed to be disconnected from RAII drop
14. The semantics of cancellation propagation and cancellation collaboration are completely undefined, and cancellation safety cannot be guaranteed statically.
15. The exception bubbling strategy within the scope is undefined (fail-fast vs collect-all is ambiguous)
16. Spawn_blocking / blocking call isolation pool is completely missing
17. The Atomic family (atomic_i32/atomic_ptr, etc.) is completely missing at the language level, and there is no zc packaging layer
18. Async interacts with error system raises syntax completely undefined

### Medium

19. Scheduling fairness: only depthFirst/breadthFirst two levels, no preemption, time slice rotation, tail delay budget
20. Async/await keyword classification three-layer drift
21. SymbolFlags Async/Generator bit and AwaitContext form two-way dead code
22. The language-level runtime directory is empty, without any scheduler/executor/task skeleton
23. The concept of language-level scope/nursery/task-group structured concurrency does not exist at all
24. AST/Binder has AwaitExpression and AwaitContext ahead of spread, but Parser is never generated and the semantic conditions are reversed.
25. Spawn/join/select is not included in the reserved word policy, and there is a risk of the identifier being occupied in the future.
26. The scheduling model is fixed to "single EventLoop per thread + explicit Executor delivery", no automatic thread pool/M:N/work-stealing
27. The default memory ordering policy is not declared, zc internal implementation tends to be acq-rel but there is no policy document
28. There may be architectural conflicts between the concurrency model of the zc library (per-thread EventLoop + Executor point-to-point delivery) and future language-level concurrency.
29. No task-local storage (TLS is misused as task local), and no Send/Sync cross-task security barrier
30. Three-layer drift: Lexical keyword → EBNF Modifier → isModifier() → SymbolFlags::Async link inconsistency
31. The zc executor model (per-thread EventLoop + point-to-point Executor) is not abstracted into a language-level Runtime interface
32. The defer/scope(exit)/cleanup structured exit primitive is missing, and the destruction order of the structured concurrent scope cannot be unified.
33. The concurrency primitive family is not defined at the language level nor in stdlib, and the completeness cannot be evaluated
34. The safety boundaries of unsafe operations (transmute/raw pointers) and concurrent interactions are not clear at all
35. Async and raises(E) error systems are not unified, `async fn f() -> T raises E` return value field is floating
36. No forced multi-threading mode, but there is no static check for "prohibiting cross-thread transfer in single-threaded runtime"
37. Zc scheduler is thread binding (EventLoop-per-thread + Executor point-to-point delivery), there is no global scheduling, and future language-level concurrency cannot automatically expand horizontally

### Low

38. The syntax reuse path of gen/yield generator and async/await is not planned in the future.
39. Startup entry: no main level Runtime startup macro/convention, bare EventLoop manual construction
40. Waker/Context mechanism: The zc library has a semantically equivalent implementation, but there is no explicit Waker type
41. Concurrent sections declare that "must land as a whole", but symbol-flags already have Async/Generator bits, there is a risk of fragmentation

### Info

42. The I/O reactor layer is complete: epoll/kqueue/IOCP + timer + external event loop bridging
43. Concurrent Diagnostic Code Zero Placeholder - None of NotSend/NotSync/AwaitOutsideAsync/CancelUnsafe/RaceCondition exists
44. Single-thread/multi-thread switchable: Explicitly switchable, but the granularity is "entire process-level manual configuration"

## Follow-Up Policy

- Treat Critical and High findings as release-blocking until the specification, implementation, and tests agree.
- Do not add compatibility shims for findings that identify obsolete syntax or dead APIs; delete the obsolete surface and update callers.
- When a finding is fixed, update the relevant spec chapter and compiler tests in the same change.
- Regenerate full audit evidence in English when this index is no longer sufficient for review.
