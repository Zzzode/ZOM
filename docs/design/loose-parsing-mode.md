# Loose Parsing Mode Design

## Overview

The ZOM parser supports two modes of operation controlled by `ParseMode`:

- **Strict** (default): `parse()` returns `zc::none` if any error diagnostic was emitted.
  This is the fail-closed contract used by the compiler driver — a single syntax error
  prevents downstream passes from seeing a potentially malformed AST.

- **Loose**: `parse()` always returns a `zc::Maybe<ast::Tree>` containing the best-effort
  AST, even when errors were emitted. This supports IDE and language-server scenarios
  where partial results enable syntax highlighting, go-to-definition, and autocomplete
  on incomplete or broken code.

## API

```cpp
enum class ParseMode {
  Strict,  // Fail-closed: any error → zc::none
  Loose,   // Fail-open: returns partial AST with errors
};

Parser::Parser(..., ParseMode mode = ParseMode::Strict);
```

The mode is stored in `Parser::Impl::parseMode` and consulted at the end of
`Parser::parse()`:

```cpp
const bool hasErrors = impl->context.errorCount() != initialErrorCount;
if (hasErrors && impl->parseMode == ParseMode::Strict) {
  return zc::none;
}
return zc::mv(tree);
```

## Error Recovery Strategy

Both modes use the same boundary-first error recovery algorithm in
`consumeSourceElement`:

1. When a parse function fails (returns `zc::none`), the recovery routine
   identifies the nearest "safe" synchronization token.
2. Tokens are skipped until the synchronizer can resume parsing the next
   top-level construct.
3. The skipped region is noted in the diagnostic stream but does NOT produce
   an AST node (no `ErrorExpr` / `ErrorStmt` placeholder nodes exist yet).

### Current Limitation: No Error Nodes

The current implementation does **not** insert placeholder AST nodes for
failed parses. This means:

- In Loose mode, the returned tree simply omits the subtrees that failed to parse.
- IDE consumers must infer "missing" regions from diagnostic spans rather than
  walking dedicated error nodes.

### Future Direction: ErrorExpr / ErrorStmt

A future iteration will add `ErrorExpr` and `ErrorStmt` node kinds to the AST
schema. These will:

- Mark the exact span of skipped/recovered regions in the tree.
- Allow tree-walking consumers (formatters, highlighters) to handle error
  regions explicitly without consulting diagnostics.
- Enable better "partial expression" recovery (e.g., `foo(bar, )` → the
  missing argument becomes an `ErrorExpr` child rather than being dropped).

This requires:
1. Adding `ErrorExpr` and `ErrorStmt` entries to `schema.yml`.
2. Regenerating `node-payload.h`, `node-accessors.h`, etc. via `gen_ast.py`.
3. Modifying `consumeSourceElement` and individual parse functions to emit
   error nodes instead of returning `zc::none`.

## Diagnostic Classification

Diagnostics emitted during parsing are classified by severity:

| Severity | Strict Mode | Loose Mode |
|----------|-------------|------------|
| Error    | Causes `zc::none` | Still returns tree |
| Warning  | Returns tree    | Returns tree |
| Note     | Returns tree    | Returns tree |

The `LineBreakBeforeAsCast` diagnostic was reclassified from error to warning
as part of this effort — a confusing line break should not prevent compilation
or AST publication in either mode.

## Testing

Unit tests for the parser use Strict mode by default (via the test harness).
Loose mode should be tested with dedicated test cases that:

1. Feed intentionally broken input to the parser.
2. Assert that `parse()` returns a tree (not `zc::none`).
3. Verify the tree contains the expected well-formed subtrees.
4. Verify error diagnostics were emitted.

## References

- `products/zomlang/compiler/parser/parser.h` — `ParseMode` enum and constructor.
- `products/zomlang/compiler/parser/parser.cc` — `parse()` mode dispatch.
- `products/zomlang/compiler/parser/parser-impl.h` — `parseMode` member and recovery logic.
- `products/zomlang/compiler/parser/parser-recovery.cc` — boundary-first recovery stubs.
