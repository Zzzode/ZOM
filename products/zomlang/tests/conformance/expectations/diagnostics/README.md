# Diagnostics Expectations

Diagnostic expectations are consumed by the `conformance-diagnostics` lit
runner. Each `.check` file must run the real compiler pipeline with
`zomc compile --check` and assert the user-facing diagnostic display.

Use the safe two-step failure pattern so FileCheck failures are not hidden by
shell pipeline negation:

```text
// RUN: cd %corpus && ! %zomc compile --check <path>.zom > %t 2>&1
// RUN: %FileCheck %s --input-file %t
```

Every negative diagnostic test must check more than the diagnostic ID: include
the severity/code line, the primary source location, the source line, and the
caret line. The FileCheck helper strips ANSI color codes before matching, so
expectations should match the displayed text directly. Use regex blocks only
for unstable path prefixes or other intentionally variable fields.
Use `CHECK-LITERAL` or `CHECK-NEXT-LITERAL` for source and caret lines whose
leading spaces matter.
For source-location lines, run the compiler from `%corpus` and match the
relative path literally, for example
`CHECK-LITERAL:   --> 04-expressions/example.zom:1:1`. Do not split the arrow
and path across `CHECK` / `CHECK-SAME`, and do not encode gutter or caret lines
as regexes.
