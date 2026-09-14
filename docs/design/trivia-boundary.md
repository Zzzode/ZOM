# Syntax Tree and Trivia Boundary

Updated: 2026-09-14

## Authority And Status

| Field | Value |
|---|---|
| Authority | Non-normative compiler implementation guide |
| Coverage | Current parser-token AST boundary and the landed CST trivia layer |
| Governing decisions | [RFC 0002](../rfc/0002-parser-architecture.md) (LANDED), RFC 0023 (IDE snapshots, IMPLEMENTING), RFC 0044 (formatter, IMPLEMENTING) |
| Production implementation | [`compiler/lexer/`](../compiler/lexer/), [`compiler/cst/`](../compiler/cst/), [`compiler/ast/schema.yml`](../compiler/ast/schema.yml) |
| Tooling consumers | [`compiler/format/`](../compiler/format/), [`compiler/ide/`](../compiler/ide/), `zomc fmt` |

Reference: RFC 0002 (Parser Architecture), D-06.

## Overview

ZOM follows the Roslyn and SwiftSyntax split between a semantic compiler tree
and a byte-covering tooling layer:

- **Compiler AST**: schema-backed, typed, fail-closed. Contains grammar
  syntax, source ranges, and explicitly retained directive/attribute
  metadata. Comments and whitespace never appear as AST nodes.
- **CST lexeme/recovery layer (landed)**: a byte-covering partition of the
  source into significant lexemes plus whitespace and comment trivia, with a
  recoverable syntax result. This is the layer the formatter and IDE services
  consume.

The parser does not build the AST directly: it emits a construction event
stream and lexeme/recovery streams, and `ParseSyntaxVerifier` independently
replays the events into a fresh `ast::TreeBuilder` and schema-verifies the
result before promotion.

## What the Compiler AST Retains

| Category | Examples | How stored |
|---|---|---|
| Syntax nodes | `FunctionDeclaration`, `LetStatement`, `BinaryExpression`, call nodes | Typed `ast::Node` with `SyntaxKind` in fixed-width schema records |
| Source ranges | Every node and token carries a half-open `SourceRange` | Range accessors on nodes and tokens |
| Token values | Identifier names, literal text, string contents | Interned string/bigint tables |
| Token flags | Preceding newline | `TokenFlags::PrecedingLineBreak` |
| Attributes | `#[...]` and `@[...]` metadata | Parsed attribute nodes with registered consumers |
| Test directives | `@zom-expect-error`, `@zom-ignore` | Extracted from comment text by the lexer directive hook and retained as diagnostic-control metadata |

## What the Compiler AST Does NOT Retain

| Category | Examples | Notes |
|---|---|---|
| Whitespace | Spaces, tabs, blank lines | Trivia in the CST lexeme stream, never an AST node |
| Comments of every form | `// ...`, `/// ...`, `/* ... */`, `/** ... */` | Uniformly scanned as comments; trivia in the CST, never an AST node |
| Formatting trivia | Indentation, alignment, line length | Owned by the formatter through the lexeme stream |

There is no doc-comment AST node. The lexer scans `///` and `//` through the
same comment path; no special doc-comment token or syntax kind exists, and no
documentation-generation consumer reads comments. Documentation comments are an
open language/tooling gap, not a retained feature.

## The CST Trivia Layer (landed)

`compiler/cst` owns the lossless byte-covering view:

- `TriviaKind` partitions non-significant bytes into `Whitespace`,
  `LineComment`, and `BlockComment`, attached around significant lexemes;
- the lexeme stream and recovery stream (`RecoverableSyntaxTree`,
  `MissingToken`, `MissingSubtree`, `SkippedTokens`, retained `Invalid` bytes)
  are built at parse time and verified as a byte partition;
- `compiler/format` formats source over the lexeme stream and its Doc IR,
  shipped as `zomc fmt` and `zomc fmt --check` (RFC 0044);
- IDE services project tokens and outlines over published semantic snapshots
  and adapt editor documents over the same parse/cst path (RFC 0023).

## Token Shape

Every parser token stores its `SyntaxKind`, a half-open `SourceRange`, its
canonical value where applicable, and `TokenFlags` (including
`PrecedingLineBreak` on the following token). Significant tokens form the
parser `TokenStream`; comments and whitespace are consumed into the CST trivia
partition instead.

## Source Range as the Bridge

AST nodes, tokens, and CST lexemes carry byte ranges into the same source
buffer, which is the bridge between the semantic tree and the lossless layer:
diagnostics point through ranges, the formatter edits around trivia ranges,
and nothing stores raw source pointers.

## Attributes and Directives

Attributes (`#[...]`, `@[...]`) carry retained metadata for registered
semantic consumers and are grammar syntax. Test-directive comments
(`@zom-expect-error`, `@zom-ignore`) are extracted from comment text by the
lexer directive hook for diagnostic conformance control. Neither mechanism is
a general doc-comment facility.

## Recovery

Recovery frames operate over token kinds and positions; the recovery stream
records skipped and missing syntax while trivia remains attached to lexemes.
The independently replayed AST never contains trivia.

## Known Gaps

- No doc-comment syntax, AST node, or documentation-generation consumer
  exists; adding one is a language/RFC decision.
- The formatter covers the admitted lexeme/token surface; full IDE
  text-synchronization is coupled to crate resolution and remains open under
  RFC 0023.
- Trivia is retained for the parse of one source snapshot; incremental trivia
  reuse across edits is part of the editor-document adapter work and is not a
  persisted trivia store.

## Verification

| Check | Evidence |
|---|---|
| No comments or whitespace in AST nodes | No doc/comment syntax kind in `schema.yml`; comments take the uniform lexer comment path |
| Byte-covering trivia partition | CST lexeme/recovery builders and their partition verification tests |
| Newline flag available | `TokenFlags::PrecedingLineBreak` set by the lexer |
| Directives retained separately | Lexer directive hook plus directive conformance tests |
| Formatter consumes trivia | `zomc fmt` tests over the lexeme stream (RFC 0044) |
