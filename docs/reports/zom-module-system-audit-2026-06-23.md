---
audit: module-system
date: 2026-06-23
scope: packages, modules, imports, exports, visibility, dependency resolution, and symbol topology
method: multi-expert adversarial review with confirm/refute validation
findings: 62
language: en
status: normalized-english-index
---

# ZOM Module System Audit Report

This report is the English normalized index of the accepted audit findings for the module system. It keeps the finding numbers, severity distribution, and actionable titles in a form that can be reviewed and grepped consistently. Detailed evidence should be regenerated in English with the audit workflow before a finding is used as a merge-blocking decision.

## Severity Summary

| Severity | Count |
|---|---:|
| Critical | 5 |
| High | 25 |
| Medium | 23 |
| Low | 7 |
| Info | 2 |

## Finding Index

### Critical

1. No cross-compilation-unit symbol merge or visibility-check architecture exists
2. Package and crate boundaries plus manifest metadata are completely undefined
3. No module scope is created for SourceFile or ModuleDeclaration; all symbols flatten into global scope
4. No topological sort or cycle detection exists; parallel bindSources conflicts with dependency ordering
5. Import binding is an empty shell: the parser accepts imports but the binder ignores their semantics

### High

6. No module path resolver exists, so imports cannot trigger automatic source loading
7. SourceFile does not create a module scope, so all symbols flatten into Global
8. as-alias semantics for import and export renaming are absent from the symbol layer
9. External dependency loading, version resolution, and workspace concepts do not exist
10. Fine-grained visibility levels such as pub(super), pub(crate), and pub(path) are missing across lexer, grammar, and semantics
11. Ambiguous bare-name imports have no AmbiguousImport diagnostic or implementation path
12. Package and crate boundaries plus manifest support are missing
13. Module path to source-file mapping is not implemented
14. Module names are not mapped into nested symbol scopes such as math to geometry
15. ModuleSymbol is forward-declared but never defined
16. The Export symbol flag is defined but never written
17. Parallel binding shares SymbolTable without locking and creates a data-race risk
18. Import paths only support dotted identifiers and lack package-root, relative, and bare-package path models
19. The mapping from module symbol paths to source files is unspecified
20. sealed, final, and open extensibility visibility flags are defined but lack lexical, grammar, and writer support
21. The driver has no dependency graph or topological scheduling for binding
22. Symbol::isPublic and FieldSymbol::isPublic conflict, making default visibility uncertain
23. Re-export interaction with original visibility is unchecked by the binder
24. Visibility checking is absent from both binder and checker, effectively leaving the API surface open
25. Parallel driver binding fundamentally conflicts with module dependency ordering
26. Core name-lookup semantics such as two-phase lookup, hard conflicts, and shadowing are not implemented in the binder
27. Top-level public/private/protected semantics are undefined while the parser also accepts export
28. resolveQualified scans scope names globally and loses parent-child hierarchy constraints
29. Missing root-module conventions leave anonymous compilation-unit semantics ambiguous
30. Core module-domain diagnostic codes are undefined

### Medium

31. Parser-level top-level placement constraints for import/export/module are incomplete and nested forms may be silently accepted
32. The Forward flag is defined but unused, leaving no cross-module recursive type handling
33. Cycle-dependency rules are missing from both spec and implementation
34. Parser-level uniqueness checks for module declarations are missing
35. No incremental compilation placeholder exists; the driver supports only full rebuilds
36. Missing package boundaries prevent meaningful discussion of cycle granularity and cross-package dependencies
37. The semantic boundary of public/private/protected on top-level declarations is unclear
38. Conditional compilation and platform-specific code organization are not designed
39. Top-level public/private/protected semantics are allowed by the parser but absent from the spec and unclear relative to export
40. ReservedInModule is commented out and MultipleDefaultExports has no references
41. Two import forms are hard-coded as mutually exclusive, losing namespace plus named mixed-import combinations
42. Cycle-dependency rules are undefined in both spec and implementation
43. Inline or nested module syntax is missing, limiting multi-module organization inside one file
44. Future syntax space for conditional imports and attribute-style import/export annotations is not reserved
45. The package keyword is lexed but has no grammar entry, leaving crate, package, and manifest layers undefined
46. Cross-crate trait orphan rules are not designed because crate/package boundaries are undefined
47. import/export/module declarations lack context-level restrictions and can still be parsed in nested scopes
48. Fine-grained member visibility lacks syntax for enum variants, interface methods, and function-parameter defaults
49. Forward declarations and incomplete references for cross-module types are undefined
50. Qualified names such as foo::bar::baz, self::, super::, and ::root are undefined in spec and implementation
51. Scope shadowing and name-lookup order rules are missing
52. Shadowing rules are undocumented and architecturally contradictory for imports versus local declarations
53. Wildcard import is explicitly excluded from v1 but lacks an evolution path and orphan-rule reservation

### Low

54. Symbol::Impl contains a redundant unused visibility bitfield
55. There is no design location for package ABI or stable symbol export artifacts
56. The examples directory has no .zom sources demonstrating the module system
57. The diagnostic system lacks symbol-resolution-specific error codes, weakening name-lookup failures
58. Directory/file name collision rules are undefined for one-directory-one-module versus one-file-one-module models
59. The equivalence of implicit and explicit re-export is unclear in the spec
60. Module initialization order is unspecified, leaving a pre-codegen SIOF-style risk

### Info

61. Wildcard import/export and default export are consistently unsupported, but the non-goal list should add negative tests
62. export * and export * as X are not implemented and wildcard specifiers are not explicitly forbidden by EBNF

## Follow-Up Policy

- Treat Critical and High findings as release-blocking until the specification, implementation, and tests agree.
- Do not add compatibility shims for findings that identify obsolete syntax or dead APIs; delete the obsolete surface and update callers.
- When a finding is fixed, update the relevant spec chapter and compiler tests in the same change.
- Regenerate full audit evidence in English when this index is no longer sufficient for review.
