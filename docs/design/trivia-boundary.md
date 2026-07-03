# Syntax Tree and Trivia Boundary

## Overview

The ZOM compiler parser produces a **schema-backed syntax tree** that contains
language syntax only. Ordinary whitespace, non-doc comments, and formatting
trivia are **not** stored in AST payloads. This document defines the boundary
between what the compiler AST retains and what is left to a future lossless
syntax tree owned by formatter and IDE tooling.

Reference: RFC 0002 (Parser Architecture), D-06.

## Design Rationale

Roslyn (.NET) and SwiftSyntax demonstrate a mature pattern: split the compiler
semantic tree from a separate lossless syntax tree used by tooling. ZOM follows
this boundary:

- **Compiler AST**: schema-backed, typed, fail-closed. Contains syntax nodes,
  source ranges, and semantically-relevant attributes/doc-comments only.
- **Lossless syntax tree** (future): a full-fidelity tree that preserves every
  character of source, including whitespace, comments, and formatting. Owned by
  formatter, IDE, and refactoring tooling.

Forcing trivia into the compiler AST would:

1.  Inflate AST node counts and memory usage for every compilation.
2.  Complicate schema validation (trivia has no grammar role).
3.  Tie parser correctness to formatting detail, making both harder to evolve.

## What the Compiler AST Retains

| Category | Examples | How stored |
|---|---|---|
| Syntax nodes | `FunctionDecl`, `LetStmt`, `BinaryExpr`, `CallExpression` | Typed `ast::Node` with `SyntaxKind` |
| Source ranges | Every node and token carries a `SourceRange` | `rangeFor(start, end)` on nodes; `token.getRange()` on tokens |
| Token values | Identifier names, literal text, string contents | `token.getValue()` / `builder.internString()` |
| Token flags | Preceding newline | `TokenFlags::PrecedingLineBreak` |
| Doc comments | `///` and `/** */` comments attached to declarations | Stored as attribute nodes in the AST (semantic role) |
| Attributes | `#[...]` and `@[...]` metadata | Parsed as attribute nodes (semantic role) |
| Delimiter locations | `(`, `)`, `{`, `}`, `[`, `]`, `<`, `>`, `,`, `;`, `:` | Implicit in node source ranges; explicit when needed for diagnostics |

## What the Compiler AST Does NOT Retain

| Category | Examples | Notes |
|---|---|---|
| Ordinary whitespace | Spaces, tabs, blank lines | Not emitted as tokens by the lexer |
| Non-doc comments | `// ...` and `/* ... */` | Skipped by the lexer; not in token stream |
| Formatting trivia | Indentation, alignment, line length | No representation in AST |
| Comment body text (non-doc) | `// TODO: fix this` | Discarded; use doc comments for semantic metadata |

## Token Shape (L2P-04)

Every token stores:

- `SyntaxKind` — the lexical category
- `SourceRange` — half-open `[start, end)` byte offset in the source buffer
- `StringPtr value` — canonical text for identifiers and literals
- `TokenFlags` — bit flags including `PrecedingLineBreak`

Newline trivia is represented by `TokenFlags::PrecedingLineBreak` on the
**following** token. This preserves line-structure information for error
message formatting and statement boundary detection without storing whitespace
as first-class tokens.

Ordinary whitespace and non-doc comments are **not** emitted as tokens. The
lexer skips them during `lexNext()` and they never appear in the
`TokenStream`.

## Source Range as the Bridge

AST nodes and tokens carry `SourceRange` values that point back to the original
source buffer. This is the bridge between the syntax tree and trivia:

- **Diagnostics** use source ranges to point at relevant source text.
- **AST dumping** can reconstruct source context from ranges.
- **Future lossless tree** can use the same source buffer to attach trivia.

The parser never stores raw source pointers or string views in AST nodes. All
text access goes through `token.getValue()` (for identifiers/literals) or the
source manager (for diagnostics).

## Doc Comments and Attributes

Doc comments (`///`, `/** */`) and attributes (`#[...]`, `@[...]`) have
**semantic meaning** in ZOM:

- Doc comments feed documentation generation and IDE tooltips.
- Attributes control conditional compilation (`#[cfg(...)]`), visibility,
  alignment, and other compiler behavior.

These are parsed as first-class AST nodes and retained. They are **not**
trivia.

## Recovery and Trivia

Error recovery does not need trivia information:

- Recovery frames use token kinds and source positions, not whitespace.
- Sync sets are defined over grammar tokens, not formatting.
- Progress invariants are measured in token positions, not character offsets.

The `PrecedingLineBreak` flag is available to recovery heuristics that want to
prefer statement boundaries, but it is never required for correctness.

## Future: Lossless Syntax Tree

A future RFC will define a lossless syntax tree for formatter and IDE use.
This tree will:

- Preserve every source character (including whitespace and comments).
- Be built from the same lexer output but with trivia tokens enabled.
- Live in a separate library/module from the compiler AST.
- Share source ranges with the compiler AST for cross-referencing.

The compiler AST is **not** a stepping stone to the lossless tree. They are
separate artifacts with different design goals: correctness and schema
validation for the compiler, fidelity and incremental update for the tooling
tree.

## Verification

| Check | Evidence |
|---|---|
| No trivia in AST payloads | `ast::Node` stores `SyntaxKind`, `SourceRange`, and typed child refs only |
| Whitespace/comments not tokenized | Lexer `lexNext()` skips spaces and non-doc comments; they never appear in `TokenStream` |
| Newline flag available | `TokenFlags::PrecedingLineBreak` set by lexer on tokens following newlines |
| Source ranges on all nodes | `rangeFor(start, end)` used in every `make*` factory call |
| Doc comments retained | Parsed as attribute nodes in declaration and member parsing |
