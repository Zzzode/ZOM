---
audit: error-system
date: 2026-06-23
scope: error values, propagation operators, raises clauses, panic boundaries, and diagnostics
method: multi-expert adversarial review with confirm/refute validation
findings: 64
language: en
status: normalized-english-index
---

# ZOM Error System Audit Report

This report is the English normalized index of the accepted audit findings for this area. It keeps the finding numbers, severity distribution, and actionable titles in a form that can be reviewed and grepped consistently. Detailed evidence should be regenerated in English with the audit workflow before a finding is used as a merge-blocking decision.

## Severity Summary

| Severity | Count |
|---|---:|
| Critical | 2 |
| High | 32 |
| Medium | 20 |
| Low | 7 |
| Info | 3 |

## Finding Index

### Critical

1. ?!` Error propagation operator lexer not split, parser not consumed
2. Checker as a whole is empty and all type level checks are not performed

### High

3. !!` Forced unwrapping operator lexer segmented correctly but parser was not consumed at all
4. RaisesClause has SyntaxKind in kinds.h but no independent node in the AST layer, semantically embedded in ReturnTypeNode.errorType
5. The raises clause does not really enter the type system: FunctionTypeSymbol No field, Binder does not access errorType
6. ?! The error propagation operator lexer does not recognize that the parser is not consumed, and the error propagation chain syntax entry is broken
7. Async concurrency model is fully preserved, async fn raises (E)<Output=Result<T,E> is not unified with Future > any language level
8. Raises not captured Behavior undefined at top level (main return Result or force try?)
9. Error variant naming conflicts No convention, common names such as Io/Parse have serious cross-crate conflicts
10. Error C ABI bridge across FFI is completely missing, errno/GetLastError has no automatic mapping
11. The `error` declaration field can only be parsed as a statement and cannot carry an enum style variant of the canonical example
12. Type of propagation chain promotion rule missing: subset/subtype/implicit conversion three choice is not determined
13. The never/bottom type is not landed, the empty error set cannot be expressed
14. !!` Failure semantics only one word implies panic, no behavioral contract
15. Zom language side panic trigger path and semantics 100% unspecified
16. Panic has no bottom/never type, the type system cannot express divergence
17. Backtrace capture is completely undefined, there is no default capture and no opt-in, errors are not debuggable
18. Completely lacking the defer mechanism and not preserving the form of defer if err
19. Standing for three types of classic pits: avoiding Go verbosity and Java failures, close to Rust but better at type information retention
20. The early-return semantics of `?!` are not formalized, and the deconstruction/RAII execution guarantee is missing
21. Missing "Library Code vs. Application Code" boundary guidelines: when to use raises, when to use panic, when to use user Result enum
22. Completely missing Panic/Unwind semantics, Never/Bottom type and panic safety model
23. <T,E> Bi-directional intermodulation of the Result and raises functions Undefined bridging interface
24. Error Context/Stack Capture/Wrapping Mechanism Completely Missing
25. Interaction between RAII/destructor and panic propagation is not guaranteed - whether the destructor chain can be guaranteed to be called in the panic path
26. Box <dyn Error> or anyhow:: Error equivalent does not exist, unifying error types across crate is inconvenient (resolved 2026-06-25: formalized in spec/chapters/03-types.md § X Existential Types and spec/chapters/09-interfaces.md § 9– § 10)
27. OOM and stack overflow classification not defined - underlying implementation overloaded (retryable) vs failed (non-recoverable) semantics are referenced, but language side is not mapped
28. Error combinators (map_err/and_then/or_else/try_join/try_for_each/inspect_err) No language/library support
29. The return type of `!!`/`panic`/`unreachable` is undetermined, the influence type is inferred and matched exhaustively
30. Raises semantics is "type annotation + value return" (Rust style), not "stack expansion + exception object", but the specification is not explicitly declared in one sentence
31. ?:` ErrorDefault default source ambiguity: with `T?` null default, Zig error-union default, Swift `try?` interaction is undefined
32. The standard library is completely missing, and Result/Option/Error is not built-in, resulting in no shared foundation from scratch.
33. Basic triangular relations not normalized: four representations (T?/T raises E<T,E>/Result/user enum Result)
34. There is no unified error trait, each error enum is written separately, and there is no unified processing entrance across the crate.

### Medium

35. Raises clause broken chain: parser - > FunctionTypeSymbol has no raises field at all, Binder ignores ReturnTypeNode.errorType
36. ScanStartOfDeclaration` leaks ErrorKeyword and StructKeyword
37. There is no implementation basis for derive (Error) and # [from] automatic conversion, and the derive/proc macro mechanism completely retains the unlanded
38. The specification priority table puts`?! `/`!! `/`?: `On the same level (Level 17 Error Handling), but the actual implementation overlaps with Postfix and differs significantly from each other
39. Optional` as reserved word undocumented declaration resulting in specification example error
40. ?:` ErrorDefault and Ternary `?:` Distinction Dependency `?` Adjacency to `:`, there is a real ambiguity edge situation
41. Single `!` suffix NonNullExpression is an out-of-specification propagation
42. Test attributes should_panic/throws Test attributes have no language-level support at all
43. !! Forced unpacking operator lexer correctly cut out but parser not consumed, unwrap semantics missing
44. The try/catch/throw/finally preserves words that contradict the canonical philosophy, and the catch block deconstructs semantics completely undefined
45. Missing documentation rule for Zig style error set subset relationship (`<:`)
46. !!` with prefix `!` The two applications are forcibly merged at the lexer layer, resulting in a 'double logic non' that cannot be abbreviated
47. Error union type has no normalization rules defined (commutative/associative/nested flattening)
48. Missing defer/scope-exit mechanism, no interaction between cleanup code and error path
49. Joint error demotion and error inheritance system interaction for `raises` clause is undefined
50. No native support for error stacks, source chains, context wrapping (context/map_err)
51. Typed throws are implemented as superficial grammatical sugar plus union types through raises clauses with sufficient precision but lacking inferred/erased dual modes
52. Missing anyError/erased error/boxed-dyn-error existence and form definition (resolved 2026-06-25: formalized in spec/chapters/03-types.md § X Existential Types and spec/chapters/09-interfaces.md § 9– § 10)
53. Error variant auto downcast (Swift typed do/catch case level capability) not modeled at all
54. Panic isolation and FFI boundary policies are completely undefined, and third-party library panic can lead to global UB

### Low

55. The underlying C + + zc library (host) has a mature two-stage error propagation (can recover Exception vs Fatal Fault:: fatal), which can be used as a reference for Zom language design.
56. The specification body prohibits try/catch/throw, but the vocabulary and kinds registered keywords, which is semantically contradictory
57. Insufficient reservation for forward evolution path: try-with-resources/typed catch/catch-when no syntax placeholder
58. Error value display/formatting built-in mechanism missing, `Display`/`Debug` trait not landed
59. Panic cross-thread/await point propagation undefined - JoinHandle/JoinError abstraction does not exist
60. Operator provides early-return for equivalent Rust question mark, but missing non-local return scope isolation at try_blocks level
61. Partial moves/wrong branch retains ownership - semantically uncommitted, influenced by GC/arc strategy but still designable

### Info

62. Panic`/`unreachable`/`abort` is neither a keyword nor a standard library declaration, an unrecoverable error mechanism is undefined
63. 2025 Modern Error System Essential Checklist Score: 3/10
64. Avoid the biggest pain point of Zig error set without payload, error variant naturally comes with field

## Follow-Up Policy

- Treat Critical and High findings as release-blocking until the specification, implementation, and tests agree.
- Do not add compatibility shims for findings that identify obsolete syntax or dead APIs; delete the obsolete surface and update callers.
- When a finding is fixed, update the relevant spec chapter and compiler tests in the same change.
- Regenerate full audit evidence in English when this index is no longer sufficient for review.
