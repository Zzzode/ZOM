---
audit: language-design
date: 2026-06-23
scope: language design, parser/type-system alignment, semantic foundations
method: multi-expert adversarial review with confirm/refute validation
findings: 64
language: en
status: normalized-english-index
---

# ZOM Language Design Audit Report

This report is the English normalized index of the accepted audit findings for this area. It keeps the finding numbers, severity distribution, and actionable titles in a form that can be reviewed and grepped consistently. Detailed evidence should be regenerated in English with the audit workflow before a finding is used as a merge-blocking decision.

## Severity Summary

| Severity | Count |
|---|---:|
| Critical | 4 |
| High | 20 |
| Medium | 25 |
| Low | 11 |
| Info | 4 |

## Finding Index

### Critical

1. Type check stage not achieved at all: all types of safety commitments are empty
2. Type checker not implemented as a whole, all semantic checks are empty
3. Type inference not implemented at all, let/const default type annotation no fallback
4. The naked type name pattern `when Point = >` completely overlaps with the IdentifierPattern syntax, there is semantic ambiguity in binding vs. type matching

### High

5. Enumerated struct variants` Name {fields} `and methods within the enumeration appear in the example, but EBNF and parser only support tuple variants and explicit values
6. ?! Error propagation operator declared in canonical syntax but parser not implemented
7. Completely missing interface (trait) to achieve consistency checking
8. ErrorDefault operator `?:` is semantically dependent on invisible whitespace (adjacent tokens), sharing the same token sequence with the ternary condition
9. The use of naked `T - > U` syntax in the function type example contradicts the bracketed form required by EBNF
10. IsSubtypeOf is determined to be "naked name equal", and the same nominal type across modules is incorrectly regarded as equivalent
11. The generic where clause and the associated type declaration are both missing at both ends of the specification and implementation
12. The error syntax used by the test file error-declarations.zom is neither supported by parser nor compliant with the specification
13. The weak modifier appears in the example, but the syntax specification has no modifier entry and no type node
14. Macros have no syntax reservation at all, the next addition will be breaking change
15. Character literal syntax is declared in the specification but parser is not implemented
16. The raises clause is defined as a comma list in EBNF, but is a `|` union form in examples and implementations
17. Pattern matching exhaustive check is completely missing and ADT's commitment to safety fails
18. Joint Type/Optional Type/Error Type Triple Semantic Overlap, No Normalization
19. The Interface system has only symbolic shells: no members, no implementation checks
20. Any and never special types are declared in the specification but are missing at both the AST and Symbol layers
21. Semantic contradiction: if is only a statement but match is an expression, conditional expressions are not uniform
22. Closure capture syntax CaptureClause exists but capture semantics are completely unspecified
23. Checker unit test false positive: named type check but only parse is actually called
24. The weak modifier appears in the specification memory management example, but the parser is not accessed

### Medium

25. VoidExpression and AwaitExpression are declared in AST, factory complete, but parser is never created
26. Keyof type operator specification exists but no AST/parsing/implementation
27. Error handling operator `?!`/`!!`/`?:` Mixed in priority list, but grammatically in two completely different hierarchies
28. Built-in types such as any/never/char/i16 for specification declarations are missing at the AST layer
29. Single! suffix (NonNullExpression) exists in the parser but the specification is not declared
30. Comma expression exists but no description, semantic ambiguity with statement semicolon separator
31. The var keyword is introduced in the declaration section, but neither EBNF nor parser support it
32. Relational operator `is`/`in`/`instanceof` is documented but not listed in EBNF and not registered in parser's priority table
33. Match discriminant enforces parentheses in conflict with expression-sentence boundary readability
34. CharacterLiteral and single quotation mark StringLiteral use the same quotation mark `'... '`, there is a fundamental ambiguity at the literal level
35. Specified memory security guarantees (boundary check, null security, use-after-cleanup) are not implemented
36. The raises clause accepts the TypeList in the spec, but the parser reads only one type; at the same time, there is a tension between the union type semantics of raises and the positioning of the error type
37. The test file error-handling-operators.zom triggers a false positive rather than an error of design intent
38. Implicit conversion rules contradict the specification: Spec claims Strong/No implicit, code hard-coded i32→ f32 broadening
39. Function subtype (parameter inversion/return covariance) is not implemented, overloaded resolution has no basis
40. The keyword policy is generally sound, but the `type` overlaps with the `alias` function, and the `_` identifier as a wildcard is legally conflicting
41. C/FFI interoperability is one of the design goals, but there is no normative syntax position and implementation route
42. The match statement uses the block form in the document example, but the EBNF and parser only accept the = > arrow form
43. The struct field modifier `mutable` is used in the example, but EBNF and parser only recognize `mutating` (method level)
44. Joint/Optional Type Normalization (T | null-equivalent T?) Not at all
45. The numerical boost matrix is only hard-coded i32- > f32, and the remaining numerical boosts are missing
46. The function scope of var coexists with the block scope of let/const - the shadowing rule is completely undefined
47. Memory model has only value/reference binary and weak keywords, missing use-after-cleanup/loop detection/move semantics
48. Value type vs reference type is clearly defined, but copy/move/assign semantic details are missing
49. The short-circuit range of the optional chain is not defined by the specification, and there is a semantic ambiguity between the entire chain vs. a single segment.

### Low

50. Interface can be inherited using implements, but the specification only allows extends
51. Leading federation/crossover type | T and & T exist in parser but specification not declared
52. The typeof operator has two entries (type layer vs. expression layer) with different expressiveness and semantics
53. Comma-separated multivariate declarations and comma expressions have the potential for syntactic ambiguity in forward evolution
54. The raises clause only supports a single error type, and the specification requires TypeList
55. Control flow statement full coverage, but switch syntax does not exist
56. The async/await word retention strategy is reasonable, but the Future/Task type and scheduler interface need to take up space early
57. Evolutionary dependency on nominal types for advanced features such as GADTs/linear types/effect systems + extensibility of the current trait architecture
58. The design position of the reflection and compilation calculation is completely blank
59. 5 Body nodes (ClassBody/InterfaceBody, etc.) have names in ast-nodes.def
60. Diagnostic codes for multiple future reserved words are predefined, but the semantic test is an empty directory

### Info

61. Predefined type table missing i16, inconsistent with keyword list, EBNF and implementation
62. The abstract modifier exists in the parser but the specification Modifier manifest is not listed
63. The overall retention strategy is excellent, leaving plenty of room for future evolution
64. Generic erasure vs. monomolization strategy not explicitly selected, impacting performance/interoperability

## Follow-Up Policy

- Treat Critical and High findings as release-blocking until the specification, implementation, and tests agree.
- Do not add compatibility shims for findings that identify obsolete syntax or dead APIs; delete the obsolete surface and update callers.
- When a finding is fixed, update the relevant spec chapter and compiler tests in the same change.
- Regenerate full audit evidence in English when this index is no longer sufficient for review.
