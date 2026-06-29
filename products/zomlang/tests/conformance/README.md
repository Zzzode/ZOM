# ZomLang Conformance Tests

`conformance/` is the language source corpus and runner orchestration area.
Source fixtures describe language behavior; runner layers decide which compiler
surface is being checked.

## Runner Layers

| Layer | CTest label | Source today | Oracle |
|---|---|---|---|
| Grammar | `conformance-grammar` | `conformance/grammar/**/*.zom` | ANTLR ACCEPT/REJECT headers |
| AST | `conformance-ast` | `language/**/*.zom` | lit/FileCheck `RUN:` and `CHECK:` lines |

Future layers should reuse the same source corpus where possible:

| Layer | Intended oracle |
|---|---|
| Diagnostics | Stable diagnostic codes and spans |
| E2E | Program output, exit status, or runtime behavior |
| Binder | Symbol, scope, and import/export facts |

## Direction

The corpus should converge on shared fixture metadata instead of separate
source trees per runner. A fixture can then say which layers apply:

```yaml
spec: "05-statements"
tags: [statements, loops]
expect:
  grammar: accept
  ast: accept
  diagnostics: []
  e2e: skip
```

This change is intentionally staged. The current CMake wiring already exposes
grammar and AST checks under common conformance labels while preserving the
existing physical fixture locations.
