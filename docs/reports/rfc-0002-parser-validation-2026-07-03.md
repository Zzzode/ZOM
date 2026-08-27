---
title: RFC 0002 Parser Validation Report
date: 2026-07-03
status: REVIEW
owner: verification
---

# RFC 0002 Parser Validation Report

## Scope

This report records the local validation evidence for RFC 0002 parser
architecture review on 2026-07-03. It exists outside the RFC so unrelated test
failures can be tracked without weakening the RFC acceptance criteria.

## Parser Evidence

The parser-focused gates passed locally:

- `cmake --preset sanitizer`
- `cmake --build --preset sanitizer -j`
- `python3 scripts/codegen/gen_ast.py --check`
- `python3 scripts/check-rfc.py`
- `python3 tests/conformance/tools/check-ast-coverage.py`
- `python3 scripts/check-parser-coverage.py`
- `python3 scripts/check-format.py`
- `git diff --check`
- `ctest --test-dir build-sanitizer -R '^(parser-coverage|schema-verifier-test|recovery-test|parser-test|token-cursor-test|lit-03-types-|lit-04-expressions-|lit-05-statements-|lit-16-attributes-)' --output-on-failure`
- `ctest --test-dir build-sanitizer -R '^conformance-grammar$' --output-on-failure`

The affected parser and lit subset passed 276/276 tests. The full grammar
oracle passed in the sanitizer build.

## Default Preset Evidence

`ctest --preset default --output-on-failure` is green in the current checkout.
The full default-preset run passed 728/728 tests after the stable
`http-http-socketpair-test` failure was fixed in
`libraries/zc/unittests/http/http-test.cc`.

The failure was local to the zc HTTP test event-loop schedule, not the parser
or conformance surface. `HttpClient connection management` previously asserted
`count == 0` before the client-side EOF notification had been delivered by the
OS-backed socketpair. The test now drives the timeout close and client EOF
notification through separate event-loop turns.

Focused verification:

```bash
ctest --preset default -R '^(http-http-socketpair-test|http-http-test)$' --output-on-failure
ctest --test-dir build-sanitizer -R '^(http-http-socketpair-test|http-http-test)$' --output-on-failure
ctest --preset default --output-on-failure
```

The full default-preset run passed all 728 tests.

## RFC Status Impact

RFC 0002 must remain in `REVIEW` until:

- affected owners approve the review decision,
- `approvers` covers every `required-owners` entry,
- `decision` points to an accepted decision record,
- the accepted decision record is recorded in this RFC.
