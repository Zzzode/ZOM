---
rfc: 2
title: Parser Architecture
type: compiler
status: REVIEW
author: ZOM Compiler Team
review-manager: rfc
required-owners: [rfc, lexer-parser, error-system, binder-checker, module-system, spec-audit, verification]
approvers: []
created: 2026-06-30
updated: 2026-06-30
area: compiler
requires: [1]
supersedes: []
superseded-by: []
discussion: docs/rfc/0002-parser-architecture.md#status-history
decision: TBD
implementation: TBD
tracking-issue: docs/rfc/0002-parser-architecture.md#acceptance-criteria
---

# RFC 0002: Parser Architecture

## Summary

Replace the current range-scanning parser with a grammar-shaped recursive
descent parser that owns syntax validation, deterministic recovery, AST
construction, and conformance verdict alignment for the ZOM language grammar.

## Motivation

The parser is the trust boundary between source text and every later compiler
phase. The current implementation tokenizes the file, scans token ranges, emits
a small set of token-pattern diagnostics, and then builds an AST even when core
grammar productions were never recognized. This creates a fail-open frontend:
source that should be rejected can flow into AST dumping, binding, and later
compiler phases.

The repository evidence shows the scale of the problem:

- The grammar reference contains 257 direct EBNF left-hand-side productions,
  while the parser contains 28 `parse*` entry points.
- The schema defines 124 AST node kinds, while the parser references 55 of
  those kinds.
- At the start of this RFC work, direct execution of
  `zomc compile --dump-ast` disagreed with 180 grammar-oracle verdicts; 162 of
  those were reject cases accepted by the compiler.
- After the first fail-closed, AST schema verification, lexer-literal, and
  stale lit expectation repairs, the AST coverage guard still reports 84 real
  verdict mismatches: 63 reject cases accepted by the compiler and 21 accept
  cases rejected by the compiler.
- The parser coverage map records 206 syntactic productions and 35 lexical
  productions that must stay aligned with the grammar reference and C++ parser.
- Accepted AST dumps previously contained missing required structure, such as
  an alias target printed as `null`; the parser architecture must prevent those
  trees from being published.

This blocks parser work, AST lit review, binder correctness, diagnostics
quality, and any serious claim that the implementation supports the language
specification.

## Goals

- Make the parser fail closed: syntax errors must prevent a public AST from
  being returned.
- Shape parser functions around the grammar in
  `docs/spec/chapters/17-grammar-reference.md`.
- Implement expression parsing with a Pratt or precedence-climbing core that
  covers assignment, conditional, binary, prefix, postfix, member, call,
  optional-chain, `new`, `super`, and `import(...)` expressions.
- Implement type parsing with explicit union, intersection, postfix, atom,
  function type, tuple type, object type, type query, dyn type, generic
  argument, and bound parsing.
- Implement declaration, statement, pattern, attribute, modifier, import,
  export, and module-item parsing as first-class grammar productions.
- Define the authoritative grammar source and require every executable grammar,
  parser coverage map, and corpus oracle to be checked against it.
- Enforce AST schema invariants during parser construction.
- Centralize syntax recovery so invalid input produces diagnostics without
  creating structurally invalid AST nodes.
- Define the parser boundary for trivia, comments, doc comments, and lossless
  concrete syntax so formatter and IDE work do not accidentally depend on
  compiler AST internals.
- Make conformance grammar verdicts the acceptance oracle for AST lit tests.
- Add automated drift checks between grammar productions, parser entry points,
  AST node construction, diagnostics, and conformance coverage.

## Non-Goals

- This RFC does not define binder name-resolution behavior.
- This RFC does not define type-checking behavior.
- This RFC does not define IR lowering or code generation.
- This RFC does not require an incremental IDE parser.
- This RFC does not require a public lossless concrete syntax tree.
- This RFC does not introduce a second parser implementation.
- This RFC does not preserve the current range-scanning parser as a selectable
  mode.

## Prior Art

Clang uses a hand-written recursive descent parser integrated with precise
source locations, diagnostics, and recovery. ZOM should copy the explicit token
cursor, grammar-shaped parse functions, and local recovery discipline. ZOM
should not copy Clang's tight parser and semantic-analysis coupling; ZOM needs
parser output to remain syntax-only so binder and checker ownership stays
clear. Reference:
<https://clang.llvm.org/docs/InternalsManual.html>.

Rust's compiler parser uses explicit parse results, token-tree handling, and
syntax recovery paths that report structured diagnostics before later compiler
phases run. ZOM should copy the principle that parse failure is a typed outcome
and that recovery must not silently accept malformed syntax. ZOM should keep
AST storage schema-backed instead of adopting Rust's AST shape. Reference:
<https://rustc-dev-guide.rust-lang.org/the-parser.html>.

Swift separates parsing and syntax representation through a syntax tree that
can preserve source structure and diagnostics. ZOM should copy the clean phase
boundary and the rule that parser diagnostics are source-positioned. ZOM does
not need Swift's full green-tree incremental model for this RFC. Reference:
<https://github.com/swiftlang/swift-syntax>.

Go's `go/parser` exposes a deterministic parse API with position information
and an error list. ZOM should copy the simple contract that parse callers can
distinguish a complete syntax tree from a parse that reported errors. ZOM
should not copy Go's intentionally small grammar surface; ZOM's grammar needs
separate expression, type, pattern, and declaration parsers. Reference:
<https://pkg.go.dev/go/parser>.

Tree-sitter demonstrates the value of corpus-driven parse tree expectations and
small, reviewable fixtures. ZOM should copy corpus discipline and snapshot
review, but should not use Tree-sitter as the compiler's primary parser because
ZOM already needs a C++ parser wired directly to schema-backed AST payloads and
diagnostic IDs. Reference:
<https://tree-sitter.github.io/tree-sitter/creating-parsers>.

Roslyn and Swift syntax both demonstrate a mature split between compiler
semantics and lossless syntax infrastructure. ZOM should copy the boundary:
the compiler parser may keep enough source attachment for diagnostics and
attributes, while a future formatter or IDE parser can own a separate lossless
tree. ZOM should not force lossless trivia into the schema-backed compiler AST
in this RFC. References:
<https://learn.microsoft.com/en-us/dotnet/csharp/roslyn-sdk/get-started/syntax-analysis>
and <https://github.com/swiftlang/swift-syntax>.

## Guide-Level Explanation

Contributors will read and modify parser code by grammar area. A syntax change
will normally touch the grammar reference, the corresponding parser function,
AST schema or accessors when needed, and conformance tests.

For example, adding a postfix operator will require these coordinated changes:

- Add or confirm the token in the lexer and token metadata.
- Add the operator to the postfix suffix grammar.
- Add one branch in the postfix loop.
- Add AST schema support if the operator needs a new node.
- Add positive and negative conformance fixtures.
- Add precedence-focused AST lit coverage.

Invalid syntax will no longer produce a public AST dump with missing children.
If a source file violates syntax, `zomc compile --dump-ast` exits non-zero with
parser diagnostics. Recovery may continue internally to find more diagnostics,
but no recovered tree is published to binder, checker, or AST conformance
output.

The parser architecture will be organized by syntax domain rather than by
token-range helpers:

```mermaid
flowchart TD
  Source["Source buffer"] --> Lexer["Lexer"]
  Lexer --> Cursor["TokenCursor"]
  Cursor --> Parser["ParserContext"]
  Parser --> Decls["Declaration parser"]
  Parser --> Stmts["Statement parser"]
  Parser --> Exprs["Expression parser"]
  Parser --> Types["Type parser"]
  Parser --> Patterns["Pattern parser"]
  Decls --> Builder["TreeBuilder"]
  Stmts --> Builder
  Exprs --> Builder
  Types --> Builder
  Patterns --> Builder
  Builder --> Verify["AST schema verifier"]
  Verify --> Tree["ast::Tree"]
  Parser --> Diags["DiagnosticEngine"]
```

## Reference-Level Design

### Public Parser Contract

`parser::Parser::parse()` remains the public parse entry point and returns
`zc::Maybe<ast::Tree>`.

The contract changes as follows:

- If lexing or parsing emits an error diagnostic, `parse()` returns
  `zc::none`.
- If parsing succeeds, the returned `ast::Tree` has a valid root and passes the
  AST schema verifier.
- Parser recovery may build temporary nodes only if they are not returned after
  an error.
- Required AST fields must never be published as null.
- Optional AST fields may be empty only when marked optional in the schema.

`basic::performParse()` and `CompilerDriver::parseSources()` continue to use
the diagnostic engine as the phase error state, but they no longer depend on
the parser accidentally emitting a diagnostic for every invalid token pattern.
The parser itself owns grammar failure.

Internally, parsing is modeled as a typed result even though the public API
remains `zc::Maybe<ast::Tree>`:

| Internal state | Meaning | Public result |
|---|---|---|
| `Complete` | The source matched the grammar, no syntax error was emitted, and the AST verifier passed. | `ast::Tree` |
| `RecoveredWithErrors` | Recovery found additional diagnostics after a syntax error. The tree is internal only. | `zc::none` |
| `Aborted` | EOF, error-budget exhaustion, or an invariant failure stopped parsing. | `zc::none` |

Callers must not infer syntax success from a non-empty partially built
`TreeBuilder`. The only publication gate is the final parser result after
diagnostic checks and AST schema verification.

### Authoritative Grammar Source

The normative human grammar for this RFC is
`docs/spec/chapters/17-grammar-reference.md`. The compiler parser, executable
grammar artifacts, and conformance metadata must be treated as derived or
checked artifacts relative to that chapter.

The implementation must add a machine-checkable parser coverage map at:

```text
products/zomlang/compiler/parser/parser-coverage.yml
```

The coverage map is the bridge between prose EBNF and C++ parser functions.
It does not replace the grammar reference. It records which parser function
implements each syntactic production and which productions are intentionally
inlined into a parent production.

`docs/spec/ZomParser.g4` is allowed to remain an executable grammar oracle, but
it must not become an independent source of truth. Any conflict between
`17-grammar-reference.md`, `ZomParser.g4`, `parser-coverage.yml`, corpus
metadata, and C++ parser behavior is a spec-alignment failure.

The intended flow is:

```mermaid
flowchart TD
  Grammar["17-grammar-reference.md"] --> Coverage["parser-coverage.yml"]
  Grammar --> Antlr["ZomParser.g4"]
  Grammar --> Corpus["grammar expectations"]
  Coverage --> Parser["C++ parser"]
  Antlr --> GrammarRunner["grammar oracle runner"]
  Corpus --> VerdictGuard["AST verdict guard"]
  Parser --> VerdictGuard
```

### Parser Modules

The implementation should split `products/zomlang/compiler/parser/` by
syntax domain:

| Module | Responsibility |
|---|---|
| `token-cursor.*` | Indexed token access, save/restore marks, token labels, EOF handling. |
| `parser-context.*` | Shared parser state, diagnostics, recovery, `TreeBuilder`, source ranges. |
| `declaration-parser.*` | Module items, declarations, imports, exports, attributes, modifiers. |
| `statement-parser.*` | Blocks, statement lists, control flow, labels, match statements. |
| `expression-parser.*` | Pratt or precedence-climbing expression parser. |
| `type-parser.*` | Type expressions, type parameters, type arguments, bounds. |
| `pattern-parser.*` | Binding patterns and match patterns. |
| `parser-recovery.*` | Synchronization sets and recovery helpers. |
| `parser.cc` | Public `Parser` facade and top-level orchestration. |

The file split is required for maintainability. It is not a public API.

Module dependencies are one-directional:

```mermaid
flowchart TD
  Facade["parser.cc facade"] --> Context["ParserContext"]
  Context --> Cursor["TokenCursor"]
  Context --> Recovery["parser-recovery"]
  Context --> Factory["AstFactory"]
  Decls["declaration-parser"] --> Context
  Decls --> Stmts["statement-parser"]
  Decls --> Types["type-parser"]
  Decls --> Patterns["pattern-parser"]
  Stmts --> Context
  Stmts --> Exprs["expression-parser"]
  Stmts --> Patterns
  Exprs --> Context
  Exprs --> Types
  Types --> Context
  Patterns --> Context
  Factory --> Builder["ast::TreeBuilder"]
```

Domain parsers may call each other only along grammar ownership boundaries:
declarations may call statements, types, and patterns; statements may call
expressions and patterns; expressions may call types only in grammar contexts
that require a type. Lower-level modules must not call declaration parsing.
This prevents accidental statement-end or source-file scanning from re-entering
inside expression and type parsing.

### Syntax Tree And Trivia Boundary

The schema-backed `ast::Tree` is the compiler syntax tree. It is not a lossless
concrete syntax tree.

The compiler parser must preserve:

- source ranges for all AST nodes
- token ranges where diagnostics and later parser checks need them
- attributes and doc comments that have language semantics
- enough delimiter and separator location information for precise diagnostics

The compiler parser must not preserve ordinary whitespace, non-doc comments, or
formatting trivia in AST payloads. Those belong to a future lossless syntax
tree owned by formatter and IDE work.

This boundary keeps parser correctness focused on language syntax while still
leaving a clean future path for tooling.

### Token Cursor

`TokenCursor` owns no source text. It references the lexed token vector and
provides these operations:

- `peek(offset)` returns a token kind without consuming.
- `token(offset)` returns the token for diagnostics and source ranges.
- `at(kind)` checks the current token kind.
- `eat(kind)` consumes only when the current token matches.
- `expect(kind, diagnostic)` consumes the expected token or emits a diagnostic.
- `mark()` and `rewind(mark)` support bounded syntactic lookahead.
- `position()` returns the current token index.
- `isAtEnd()` recognizes EOF.

Every parse loop must prove progress. Recovery helpers must consume at least
one token before retrying.

### Parse Results

Internal parse functions return explicit results:

- Required grammar functions return `zc::Maybe<ast::NodeId>`.
- Optional grammar functions return `ast::NodeId`, where an empty id means the
  optional production was not present.
- List grammar functions return an AST list node or a `NodeList` wrapper,
  depending on the schema field being populated.

Node construction helpers must validate required children before calling
`TreeBuilder::makeNode`. A missing required child after an error returns
`zc::none` from the current required production.

### AST Construction Boundary

`ast::TreeBuilder` is a low-level storage builder. Parser code must not scatter
raw payload word writes through every grammar function after this RFC is
implemented. The parser owns an `AstFactory` layer with one construction helper
per published syntax node that the parser can create.

Each factory helper must:

- receive already parsed child `NodeId`, `NodeList`, `IdentId`, and scalar
  values
- reject missing required children before building the node
- set optional fields only when the schema marks them optional
- attach the source range from consumed tokens
- encode enum values through generated schema constants
- return `zc::Maybe<ast::NodeId>` for nodes with required children

Only `AstFactory` and generated AST support code may call
`TreeBuilder::makeNode` directly for parser-owned nodes. Grammar functions call
factory helpers. This rule makes the parser reviewable: grammar functions show
which production matched, while factory helpers show how schema invariants are
encoded.

The generated AST schema remains the source of field layout. The factory layer
is not a second schema and must not duplicate field offsets beyond generated
constant names.

### Grammar-Shaped Entry Points

The parser must provide entry points for these production families:

- `parseSourceFile`
- `parseModuleItem`
- `parseDeclaration`
- `parseModifierList`
- `parseImportDeclaration`
- `parseExportDeclaration`
- `parseStatement`
- `parseBlockStatement`
- `parseExpression`
- `parseAssignmentExpression`
- `parseTypeExpression`
- `parsePattern`
- `parseAttribute`

Lexical-only EBNF productions do not need parser functions, but every
syntactic EBNF production must either have a direct parser function or be
documented in a local parser coverage map as being inlined into a named parent
production.

### Parser Coverage Map

`parser-coverage.yml` must have one entry per syntactic EBNF production in
`17-grammar-reference.md`.

Each entry uses this shape:

```yaml
productions:
  AssignmentExpression:
    parser: parseAssignmentExpression
    status: direct
    ast:
      - BinaryExpr
      - AssignmentExpr
    tests:
      - products/zomlang/tests/conformance/corpus/04-expressions
  ArgumentList:
    parser: parseArguments
    status: inlined
    parent: Arguments
    ast:
      - CallExpression
    tests:
      - products/zomlang/tests/conformance/corpus/04-expressions
```

Allowed `status` values are:

| Status | Meaning |
|---|---|
| `direct` | The production has a parser function with the listed name. |
| `inlined` | The production is implemented inside the listed parent parser function. |
| `lexical` | The production is handled entirely by the lexer and has no parser entry. |
| `rejected` | The production was removed from the accepted grammar and must not appear in `17-grammar-reference.md`. |

`rejected` is a temporary reconciliation state only. An accepted RFC cannot
leave `rejected` entries for productions that remain in the grammar reference.

The coverage checker must fail when:

- a syntactic EBNF production has no map entry
- a `direct` parser function is missing
- an `inlined` parent parser function is missing
- a production is mapped to an AST node kind that no parser path can construct
- a mapped test path does not exist
- the map references a production that no longer exists

The parser rewrite is not complete until this checker is clean.

### Ambiguity Resolution

The parser must resolve known grammar ambiguities locally and document each
resolution in code near the relevant parser function. These rules are part of
the parser contract:

| Ambiguity | Resolution |
|---|---|
| Generic arguments versus relational `<` and `>` | Parse type arguments only in syntactic contexts that admit type arguments; expression parsing treats `<` and `>` as relational unless left-hand-side grammar has committed to a type-argument-capable form. |
| Tuple type versus parenthesized type | A parenthesized type with a top-level comma is a tuple type; without a comma it is a grouped type unless the grammar explicitly requires a tuple. |
| Function type versus parenthesized type | A parameter clause followed by `->` commits to function type parsing. |
| Object literal versus block statement | At statement head, `{` parses as a block. Object literals parse only in expression contexts. |
| Attribute attachment | Leading `#[...]` at statement-list item head attaches to the following declaration or statement, not to an expression statement. |
| `import` declaration versus `import(...)` expression | `import` at module item head parses as declaration; `import` after expression grammar has started parses as import call. |
| `as` cast versus identifier-like syntax | `as`, `as?`, and `as!` parse only in relational/cast expression position. Line-break and statement-head rules remain diagnostics. |
| Match pattern expression fallback | Pattern alternatives are attempted before expression patterns; expression pattern fallback cannot consume tokens that would start a more specific pattern form. |

New syntax that introduces ambiguity must add a row to this table before the
RFC can be accepted.

### Expression Parser

Expressions are parsed with a Pratt or precedence-climbing parser. The operator
table is centralized and maps each operator token to:

- precedence
- associativity
- AST node kind
- AST operator enum value
- operand grammar requirements

The implementation must cover these groups:

| Grammar area | Required behavior |
|---|---|
| Assignment | Right-associative `=` and compound assignments. |
| Conditional | `cond ? then : else` with correct binding. |
| Error default | `?:` as its own operator token. |
| Null coalesce | `??` with lower precedence than logical OR. |
| Binary | Logical, bitwise, equality, relational, shift, additive, multiplicative, exponentiation. |
| Cast | `as`, `as?`, and `as!` in relational grammar position. |
| Prefix | `+`, `-`, `!`, `~`, `typeof`, prefix update. |
| Postfix | `?!`, `!!`, postfix update. |
| Left hand side | Member, subscript, call, optional chain, `new`, `super`, `import(...)`. |
| Primary | Literals, identifiers, `this`, arrays, objects, function expressions, parenthesized expressions. |

Postfix parsing must use a loop after left-hand-side parsing. The loop must
consume all postfix suffixes defined by the grammar. The parser must not rely
on a top-level binary-operator scan to discover nested expression structure.

The expression parser uses the following binding-power table unless the
normative expression chapter changes first:

| Level | Operators or forms | Associativity |
|---|---|---|
| 1 | assignment `=`, `+=`, `-=`, `*=`, `/=`, `%=`, `**=`, `<<=`, `>>=`, `>>>=`, `&=`, `^=`, `|=`, `??=` | right |
| 2 | conditional `? :`, error default `?:` | right |
| 3 | null coalesce `??` | right |
| 4 | logical OR `||` | left |
| 5 | logical AND `&&` | left |
| 6 | bitwise OR `|` | left |
| 7 | bitwise XOR `^` | left |
| 8 | bitwise AND `&` | left |
| 9 | equality `==`, `!=`, `===`, `!==` | none |
| 10 | relational `<`, `<=`, `>`, `>=`, `is`, `as`, `as?`, `as!` | none |
| 11 | shift `<<`, `>>`, `>>>` | left |
| 12 | additive `+`, `-` | left |
| 13 | multiplicative `*`, `/`, `%` | left |
| 14 | exponentiation `**` | right |
| 15 | prefix `+`, `-`, `!`, `~`, `typeof`, prefix `++`, prefix `--` | unary |
| 16 | postfix `?!`, `!!`, postfix `++`, postfix `--`, call, member, subscript, optional chain | postfix |
| 17 | primary forms | atom |

Non-associative levels must reject chains such as `a < b < c` unless the
grammar explicitly adds a chained-comparison production. Error recovery may
continue after reporting the chain, but the parser must not silently rebracket
the source into a binary tree.

The Pratt implementation must encode left and right binding power in a single
table used by both parser code and focused unit tests. Hand-coded precedence
special cases are allowed only for syntax that is not an operator table entry,
such as delimited primary expressions and function expressions.

### Type Parser

Type parsing uses grammar levels rather than top-level token searches:

- `parseUnionType`
- `parseIntersectionType`
- `parsePostfixType`
- `parseAtomType`
- `parseTypeReference`
- `parseTupleType`
- `parseObjectType`
- `parseFunctionType`
- `parseTypeQuery`
- `parseDynType`
- `parseTypeParameters`
- `parseTypeArguments`
- `parseWhereClause` when the grammar keeps it

The type parser owns the ambiguity between grouped types, tuple types, function
types, and parameter clauses. It must emit syntax diagnostics when a required
type is missing; it must not construct declaration nodes with null type targets.

The type parser is a recursive descent parser with this shape:

```mermaid
flowchart TD
  TypeExpression --> UnionType
  UnionType --> IntersectionType
  IntersectionType --> PostfixType
  PostfixType --> AtomType
  AtomType --> TypeReference
  AtomType --> TupleType
  AtomType --> ObjectType
  AtomType --> FunctionType
  AtomType --> TypeQuery
  AtomType --> DynType
  TypeReference --> TypeArguments
  FunctionType --> TypeParameters
  FunctionType --> ParameterTypes
  FunctionType --> RaisesType
```

Postfix type suffixes are parsed in a loop, not by scanning the last token of a
range. `T?`, `T??`, and `T[]` are distinct suffix forms with explicit AST
encoding. If the schema represents double optional as a flag, the parser must
set that flag in one node; if the schema changes to nested optionals, the RFC
must be updated before implementation proceeds.

Object type members are syntax nodes, not anonymous list elements. Each member
records the property name, type, mutability marker, optional marker, and source
range. Method signatures may be added only when `17-grammar-reference.md`,
`ZomParser.g4`, AST schema, parser coverage, and conformance fixtures agree on
the accepted grammar.

### Declaration And Statement Parser

Declarations and statements are parsed from FIRST sets, not by statement-end
range scanning.

The declaration parser must implement:

- module declarations
- imports and import specifiers
- exports and export specifiers
- variable declarations
- constants
- functions
- classes, structs, interfaces, enums, and errors
- member lists and member declarations
- marker and implementation declarations when they remain in the accepted
  grammar
- attributes and modifier lists

The statement parser must implement:

- blocks and statement lists
- empty statements
- expression statements
- variable statements
- if, match, while, do-while, for, and for-in
- break, continue, return, debugger
- labels with the grammar's FOLLOW restrictions

Match parsing must build match arms and patterns. It must not publish a match
statement with an empty arm list when the source contains arms.

Declaration and statement entry uses FIRST-set classification. The classifier
must skip only syntactically valid outer attributes and declaration modifiers
that are allowed before the candidate item. It must not skip arbitrary unknown
tokens to find a later keyword.

| Entry family | FIRST tokens or forms |
|---|---|
| Module item declaration | `module`, `import`, `export`, `let`, `const`, `fun`, `class`, `struct`, `interface`, `enum`, `error`, `alias`, accepted declaration modifiers, outer attributes |
| Statement | `{`, `;`, `let`, `if`, `match`, `while`, `do`, `for`, `break`, `continue`, `return`, `debugger`, identifier label forms, expression FIRST set |
| Expression statement | expression FIRST set when no declaration or statement-specific FIRST set matches |
| Member declaration | accepted member modifiers, `let`, `const`, `fun`, identifier property forms, outer attributes |

When two entries share a prefix, the parser must commit only after enough
bounded lookahead proves the grammar family. For example, an identifier at
statement head becomes a label only when followed by a top-level colon in label
position; otherwise it starts an expression statement.

### Pattern Parser

Pattern parsing must cover:

- wildcard patterns
- identifier patterns
- tuple patterns
- structure patterns
- array patterns
- `is` patterns
- expression patterns
- enum patterns
- rest patterns if retained by the AST schema

The parser must distinguish binding patterns from match patterns where the
grammar requires different subsets.

### Attributes And Modifiers

Attributes and modifiers are parsed before declarations and statement-list
items. The parser must enforce the grammar's attribute attachment rules through
parse context, not through a post-token scan.

Modifier parsing must produce one canonical representation for downstream
binder and checker phases. Duplicate or position-invalid modifiers are syntax
diagnostics when the grammar can decide them locally; semantic-only restrictions
remain in binder or checker.

The parser is responsible for modifier spelling, ordering rules that are purely
syntactic, duplicate detection, and attachment to the node that directly owns
the modifier list. Binder and checker are responsible for semantic legality,
such as whether a syntactically valid modifier is meaningful on a particular
symbol after name and type information are known.

Attributes follow the same ownership rule: the parser attaches an outer
attribute list to the immediately following declaration or statement-list item
that the grammar permits. A parser diagnostic is emitted when an attribute
appears before an item that cannot own it. The parser must not attach an
attribute to a later item after skipping an invalid intervening token.

### Diagnostics And Recovery

The parser uses synchronization sets by grammar context:

| Context | Synchronization tokens |
|---|---|
| Source file | declaration and statement FIRST sets, EOF. |
| Declaration | `;`, `}`, declaration FIRST sets, EOF. |
| Statement | `;`, `}`, statement FIRST sets, declaration FIRST sets, EOF. |
| Expression | `,`, `;`, `)`, `]`, `}`, `:`, EOF. |
| Type | `,`, `;`, `)`, `]`, `}`, `=`, `->`, EOF. |
| Pattern | `,`, `)`, `]`, `}`, `=>`, `if`, EOF. |

Recovery must report one primary diagnostic at the error site, skip to a
context-appropriate synchronization token, and resume only when the next parse
function can make progress.

Recovery is part of the parser contract, not an afterthought. The
implementation must define these recovery classes explicitly:

- expected-token failure for required delimiters, separators, and keywords
- unexpected-token failure at production entry
- delimited-group failure for `()`, `[]`, `{}`, and generic argument lists
- expression recovery after a missing operand or invalid postfix suffix
- type recovery after a missing type atom, invalid bound, or invalid type
  argument
- pattern recovery after a missing binding, invalid destructuring form, or
  invalid match arm head

Single-token insertion and deletion are allowed only when bounded lookahead
proves that the parser can preserve the surrounding production and make
progress. These repairs must emit diagnostics, must be recorded in the recovery
trace, and must not fabricate a public required AST child unless the enclosing
production can still be completed with a semantically meaningful node.

The public AST does not contain error nodes. Recovery markers are internal
parser state used to suppress cascaded diagnostics and to decide whether a
partial subtree may be published. If a required child is missing after
recovery, the enclosing production fails and the parser returns `zc::none`.

Diagnostics must be deduplicated by diagnostic ID, source location, and
recovery context. The parser must stop after EOF or after the configured parse
error budget is exhausted. The initial budget is defined in the parser context,
with `100` syntax errors as the default unless implementation evidence supports
a smaller value.

Every recovery path has a progress invariant: it must either consume at least
one token, return to a caller that consumed at least one token, or abort the
current file. Recovery loops that neither consume nor abort are implementation
bugs.

The parser must not use a broad token-pattern diagnostic pass as the primary
syntax validator. Targeted preflight checks are allowed only for lexical
interactions that cannot be expressed by parser productions.

Recovery state is observable in tests through diagnostics and optional debug
traces, not through public AST nodes. The implementation must keep a small
internal recovery frame:

| Field | Purpose |
|---|---|
| `context` | Current grammar recovery context. |
| `anchor` | Token index where the primary diagnostic was emitted. |
| `syncSet` | Token kinds that may end recovery for this context. |
| `consumed` | Whether recovery consumed at least one token. |
| `suppressedUntil` | Token index used to suppress cascaded diagnostics. |

Recovery frames are pushed only around grammar boundaries that have a clear
FOLLOW set. Expression and type subparsers must not install source-file-level
recovery because that would hide local grammar bugs by skipping too far.

```mermaid
stateDiagram-v2
  [*] --> ParseProduction
  ParseProduction --> BuildNode: production matched
  ParseProduction --> EmitDiagnostic: expected token missing
  EmitDiagnostic --> Recover
  Recover --> ParseProduction: sync token found and progress possible
  Recover --> AbortFile: EOF or unrecoverable context
  BuildNode --> VerifyNode
  VerifyNode --> [*]: required fields present
  VerifyNode --> AbortFile: internal invariant failure
  AbortFile --> [*]
```

### AST Schema Verification

After a successful parse, the parser runs an AST schema verifier before
returning the tree. The verifier checks:

- root exists and is `SourceFile`
- every referenced `NodeId` exists
- every required `NodeId` field is present
- every `NodeList` range is valid
- every enum word is within its declared domain
- every child matches the schema cast target when the schema declares one

The primary design goal is to make invalid AST construction impossible through
parser APIs. The verifier is the backstop. In debug and sanitizer builds, a
verifier failure is a compiler-internal assertion because it means the parser
violated its own construction contract. In normal execution, the compiler must
emit a dedicated internal parser-invariant diagnostic, suppress public AST
publication, and return `zc::none`.

The dedicated diagnostic is `ParserInvariantViolation`. It is reserved for
compiler-internal AST construction failures discovered after parser recovery
had an opportunity to reject the source. It must not be reused for ordinary
syntax errors. A source file that reaches this diagnostic represents a missing
parser production, a missing recovery check, or an AST construction bug.

Verifier failure is not a user syntax diagnostic by itself. User-facing syntax
diagnostics must be emitted at the production that failed. The verifier may
only report a parser invariant failure when no earlier recovery path prevented
an invalid tree from reaching the verifier.

### Conformance Integration

The grammar oracle remains the source of truth for accept/reject behavior while
this RFC keeps `ZomParser.g4` as an executable reference. The oracle must read
expected verdicts from conformance metadata, and AST tests must be checked
against those verdicts for every corpus path covered by
`parser-coverage.yml`.

AST lit tests must follow this rule:

- `expected: ACCEPT` maps to a normal AST `RUN:` line.
- `expected: REJECT` maps to `RUN: !`.
- AST-only tests require an explicit allowlist entry with a stated reason.

Failures after schema verification must be triaged by category before any
expectation file is regenerated:

| Category | Meaning | Required resolution |
|---|---|---|
| Golden-only drift | The parser accepts the source and emits a schema-valid AST, but the printed shape changed because the AST became more precise. | Regenerate the affected expectation after the responsible parser slice is complete. |
| Parser coverage gap | The source is grammar-valid, but the C++ parser emits `ParserInvariantViolation` or another parser diagnostic. | Implement the missing grammar production or fix recovery before touching the expectation. |
| Grammar verdict drift | The grammar oracle and C++ parser disagree on accept/reject. | Reconcile `17-grammar-reference.md`, `ZomParser.g4`, corpus metadata, and parser behavior. |
| Negative-test wiring drift | The corpus expects rejection, but the AST lit file still expects a public AST. | Convert the lit `RUN:` line to a non-zero parse expectation only after the grammar metadata proves the fixture is a reject case. |
| Unsupported accepted syntax | The corpus encodes syntax that is not in the accepted grammar but is still treated as positive coverage. | Remove or rewrite the fixture, or first update the accepted grammar through RFC/spec review. |

The parser implementation is complete only when the AST coverage guard reports
zero verdict mismatches against grammar expectations.

If `ZomParser.g4` is retained, CI must run a differential parse check between
the C++ parser and the grammar oracle. A mismatch is a blocking parser/spec
alignment failure unless an explicit AST-only allowlist entry explains why the
fixture is outside the grammar oracle's scope.

### Generated Parser Artifacts

`docs/spec/ZomParser.g4` may remain as a grammar reference and external grammar
runner input, but the compiler does not depend on generated ANTLR parser output
for this RFC.

If `ZomParser.g4` is retained, it must be checked by the same spec-alignment
workflow as `17-grammar-reference.md`. Divergence between the hand-written
parser and the grammar reference is a blocking issue.

The decision for this RFC is to retain `ZomParser.g4` as a differential oracle
until a separate RFC removes or replaces it. The C++ parser is still the
compiler parser. Generated parser output is not linked into the compiler
frontend.

### Implementation Status Ledger

Implementation status is tracked by parser slice, not by file count or
expectation churn:

| Slice | Status marker | Meaning |
|---|---|---|
| `not-started` | No implementation has landed for the slice. |
| `partial` | Some grammar forms work, but the slice exit evidence is not met. |
| `implemented` | The slice exit evidence passes locally and in CI. |
| `blocked` | The slice cannot progress until a named spec or schema decision is resolved. |

The ledger belongs in implementation tracking, not in ad hoc commit messages.
Each slice update must record:

- parser files changed
- grammar productions covered
- AST node kinds newly constructed
- conformance directories regenerated
- commands run
- remaining failures by category

AST expectation regeneration is valid evidence only when tied to one of these
slice updates.

## Repository Impact

| Area | Paths | Owner |
|---|---|---|
| RFC governance | `docs/rfc/0002-parser-architecture.md`, `docs/rfc/README.md` | `rfc` |
| Lexer and parser | `products/zomlang/compiler/lexer/**`, `products/zomlang/compiler/parser/**`, `products/zomlang/compiler/parser/parser-coverage.yml`, `products/zomlang/compiler/ast/kinds.h`, `docs/spec/ZomLexer.g4`, `docs/spec/ZomParser.g4`, `docs/spec/chapters/02-lexical-structure.md`, `docs/spec/chapters/04-expressions.md`, `docs/spec/chapters/17-grammar-reference.md` | `lexer-parser` |
| Diagnostics | `products/zomlang/compiler/diagnostics/**`, `docs/spec/chapters/11-error-handling.md` | `error-system` |
| Binder and checker contracts | `products/zomlang/compiler/binder/**`, `products/zomlang/compiler/checker/**`, `docs/spec/chapters/03-types.md`, `docs/spec/chapters/06-declarations.md`, `docs/spec/chapters/08-classes-and-structures.md`, `docs/spec/chapters/09-interfaces.md`, `docs/spec/chapters/10-enumerations.md`, `docs/spec/chapters/12-generics.md` | `binder-checker` |
| Driver and module boundary | `products/zomlang/compiler/driver/**`, `products/zomlang/compiler/symbol/**`, `docs/spec/chapters/13-modules-and-imports.md`, `docs/spec/chapters/21-package-model-and-manifest.md`, `docs/spec/chapters/23-visibility-ladder.md`, `docs/spec/chapters/24-module-resolution-algorithm.md` | `module-system` |
| Spec alignment | `docs/spec/**`, `docs/reports/*spec-alignment*` | `spec-audit` |
| Tests and verification | `products/zomlang/tests/**`, `examples/**`, `docs/reports/*coverage*` | `verification` |

## Security And Safety Impact

The parser is not a sandbox boundary, but it is a compiler safety boundary.
Fail-open parsing can feed invalid trees into later phases and trigger invalid
symbol, type, or code generation assumptions. This RFC reduces that risk by
requiring syntax errors to stop public AST publication.

Implementation must preserve memory safety by using existing `zc` ownership
types and avoiding raw owning pointers. Recovery loops must be progress-checked
so malformed input cannot cause infinite loops. Diagnostics must report source
spans from the current source manager without exposing unrelated buffers.

## Drawbacks And Risks

- The parser rewrite is larger than a sequence of local fixes.
- AST lit snapshots will churn when the parser starts preserving real syntax
  structure.
- Some spec text and grammar fixtures may be found inconsistent and must be
  reconciled before parser implementation can proceed.
- Binder and checker work may need short-term updates because more accurate AST
  nodes will expose assumptions hidden by missing parser coverage.
- Error recovery quality is easy to overfit to current fixtures; recovery must
  be validated with negative corpus breadth and fuzzing.
- Keeping a human-readable grammar as the normative source still requires the
  machine-checkable coverage map. If the map is not enforced in CI, grammar and
  implementation drift will return.
- The compiler AST is intentionally not a lossless CST. Formatter, refactoring,
  and IDE features will need a separate concrete syntax strategy instead of
  treating this AST as a universal syntax store.

The risk is acceptable because the current parser cannot enforce the language
grammar and already causes conformance verdict drift.

## Alternatives Considered

Continue patching the current range scanner. This is rejected because the
scanner has no grammar-shaped control flow, no required-child enforcement, and
no systematic recovery model. Adding more token patterns would increase hidden
acceptance paths.

Adopt ANTLR-generated C++ parser output as the compiler parser. This is rejected
for the primary compiler path because ZOM needs direct control over AST payload
construction, `zc` ownership, source ranges, and diagnostic IDs. ANTLR grammar
artifacts can remain useful as an external grammar oracle.

Use Tree-sitter as the compiler parser. This is rejected because Tree-sitter is
optimized for editor parsing and concrete parse trees, not direct construction
of ZOM's schema-backed AST and compiler diagnostics. Tree-sitter-style corpus
discipline remains valuable for tests.

Build a parser-combinator framework first. This is rejected because parser
combinators would introduce a new framework before the grammar contract is
stable. A direct recursive descent parser is simpler to audit against the
current EBNF.

Use a fully generated parser from a new grammar DSL. This is rejected for this
stage because it would add a generator, generated C++ ownership concerns, and a
new source of drift. The current need is to make the compiler parser match the
spec with minimal moving parts.

## Compatibility And Rollout

ZOM is pre-stability, so the rollout replaces the parser implementation in
place. There is no selectable parser mode.

The rollout order is:

1. Resolve all blocking open questions and move this RFC to `REVIEW`.
2. Land this RFC as the accepted parser architecture.
3. Reconcile `17-grammar-reference.md`, `ZomParser.g4`, lexer token metadata,
   AST schema, and grammar oracle fixtures.
4. Harden `parser-coverage.yml` and the CI guard that fails on unmapped grammar
   productions.
5. Introduce `TokenCursor`, `ParserContext`, recovery helpers, and AST schema
   verification.
6. Implement expression and type parsers first because they are shared by most
   declarations and statements.
7. Implement declarations, statements, patterns, attributes, modifiers, import,
   export, and module items.
8. Replace the current range-scanning parser entry path.
9. Regenerate AST lit checks from the grammar oracle.
10. Remove parser code that no longer has call sites.

Rollback cost is high after step 8 because AST snapshots and binder assumptions
will align with the new grammar-shaped tree. Before step 8, rollback is a
normal code revert.

AST expectation regeneration is intentionally late in the rollout. Regenerating
all lit files before parser coverage gaps are closed would lock in partial AST
shapes and hide missing grammar support. A parser slice may regenerate only the
fixtures whose sources parse successfully, pass AST verification, and belong to
the syntax family owned by that slice.

The current implementation work has started before formal RFC acceptance only
as drift repair for fail-open behavior and AST schema verification. That does
not change the acceptance bar: the parser architecture is not complete until
the coverage map, grammar-shaped parser split, recovery contract, and full
conformance gates are in place.

## Documentation And Teaching Plan

The implementation must update:

- `docs/spec/chapters/17-grammar-reference.md` for grammar decisions found
  during reconciliation.
- `docs/spec/chapters/04-expressions.md` for precedence and associativity
  alignment.
- `docs/spec/chapters/02-lexical-structure.md` for token and keyword alignment.
- `products/zomlang/tests/conformance/README.md` for parser, grammar, and AST
  verdict workflow.
- `docs/design/` with a parser architecture document after the implementation
  is accepted and underway.
- Parser coverage map documentation that explains `direct`, `inlined`,
  `lexical`, and `rejected` statuses.
- Contributor notes for ambiguity decisions, including generic arguments,
  grouped versus tuple syntax, object literals, casts, attributes, and match
  heads.
- Developer-facing parser comments only where they explain recovery or grammar
  ambiguity.

The teaching model for contributors is: update the grammar, update the parser
function for that production, update tests, then run the alignment and
conformance gates.

## Operational Readiness

Before landing implementation, CI must expose:

- RFC structure checks.
- C++ format checks.
- Parser unit tests.
- AST lit tests.
- Grammar conformance tests.
- AST coverage and verdict alignment checks.
- Parser coverage map checks.
- Spec alignment checks.
- Differential parser checks against `ZomParser.g4` when that file remains in
  the repository.
- Recovery stress tests for bounded diagnostics, EOF handling, and progress
  invariants.
- Sanitizer build and test coverage for parser changes.

Parser performance should be measured on the full conformance corpus. The
initial target is linear behavior in token count for accepted files, excluding
bounded syntactic lookahead.

## Acceptance Criteria

- `parser::Parser::parse()` returns `zc::none` whenever parser diagnostics emit
  an error.
- No public AST dump contains a required AST field printed as `null`.
- `products/zomlang/compiler/parser/parser-coverage.yml` exists and every
  syntactic production is mapped with a checked status.
- Every syntactic EBNF production is implemented or explicitly mapped to an
  inlined parser function.
- Every ambiguity-resolution row in this RFC has a positive or negative parser
  fixture.
- The expression parser covers every operator and associativity row in
  `04-expressions.md`.
- Postfix parsing covers `?!`, `!!`, `++`, and `--` in the postfix loop.
- Type parsing covers union, intersection, postfix, tuple, object, function,
  type query, dyn, generic argument, and bound forms that remain in the grammar.
- Declaration parsing builds real members, variants, import specifiers, export
  specifiers, attributes, and modifiers.
- Match parsing builds arms and patterns.
- Direct execution of grammar oracle fixtures reports zero accept/reject
  mismatches.
- Differential grammar checks report zero verdict mismatches when
  `ZomParser.g4` remains enabled as an oracle.
- The implementation status ledger records every parser slice with evidence and
  remaining failures by category.
- `python3 products/zomlang/tests/conformance/tools/check-ast-coverage.py`
  reports zero verdict mismatches.
- Recovery tests prove bounded diagnostics, EOF termination, diagnostic
  deduplication, and progress invariants.
- The AST verifier rejects missing required fields and invalid child kinds in
  focused unit tests.
- The syntax tree and trivia boundary is documented in `docs/design/` or the
  parser README before implementation status moves beyond `IMPLEMENTING`.
- `python3 scripts/check-rfc.py` passes.
- `python3 scripts/check-format.py` passes.
- `cmake --build --preset sanitizer` passes.
- `ctest --preset default --output-on-failure` passes, except unrelated tracked
  failures explicitly documented outside this RFC.

## Implementation Plan

1. Resolve blocking open questions and assign required owner review.
2. Accept this RFC as the parser architecture.
3. Add parser architecture design notes under `docs/design/` for implementation
   details that must remain current after the RFC lands.
4. Reconcile the normative grammar, `ZomParser.g4`, AST schema, token metadata,
   and conformance verdict metadata.
5. Harden `parser-coverage.yml` and the parser coverage script that maps
   syntactic EBNF productions to parser functions or explicit inline mappings.
6. Add AST schema verification for required fields and child cast targets.
7. Add `TokenCursor`, `ParserContext`, diagnostic deduplication, and recovery
   helpers.
8. Implement expression parser and precedence tests.
9. Implement type parser and type conformance tests.
10. Implement pattern parser and match-pattern tests.
11. Implement declaration and statement parsers.
12. Implement imports, exports, module items, attributes, and modifiers.
13. Replace the current range-scanning parser path.
14. Regenerate AST lit expectations from the grammar oracle.
15. Remove parser functions and helpers with no call sites.
16. Run the full verification plan and move the RFC through implementation
    status when evidence is complete.

The implementation must be delivered as gateable slices:

| Slice | Scope | Exit evidence |
|---|---|---|
| 1. Publication contract | Diagnostic error-count snapshots, fail-closed `Parser::parse()`, AST schema verifier, internal invariant diagnostic. | Parser unit tests pass, `gen_ast.py --check` passes, and malformed focused fixtures no longer publish ASTs. |
| 2. Expression core | Pratt or precedence-climbing parser, prefix, postfix, calls, member access, indexing, object literals, function expressions, `new`, `super`, `import(...)`, casts, conditional, and error operators. | All `04-expressions` and expression-dependent `11-error` fixtures either pass or have documented grammar verdict drift. |
| 3. Type core | Union, intersection, tuple, object, function, postfix, type query, dyn, generic arguments, type parameters, defaults, and bounds. | All `03-types` and type-dependent `12-generics` fixtures pass without `ParserInvariantViolation`. |
| 4. Declarations and statements | Module items, declarations, members, blocks, control flow, labels, loops, match statements, and variable statements. | `05-statements`, `06-declarations`, `08-adt`, `09-interfaces`, and declaration-heavy generics fixtures pass. |
| 5. Patterns, attributes, modules, concurrency, macros | Binding and match patterns, attributes, import/export/module forms, accepted concurrency syntax, and macro syntax retained by the grammar. | `07-patterns`, `13-modules`, `15-concurrency`, `16-attributes`, and `21-macros` fixtures pass or are removed from accepted grammar coverage. |
| 6. Alignment and cleanup | `parser-coverage.yml`, drift checkers, expectation regeneration, unused parser helper removal, and design-doc update. | Full default preset passes, RFC checks pass, coverage guard reports zero verdict mismatches, and no parser implementation path relies on range-scanning fallbacks. |

## Test Plan

- Build: `cmake --preset sanitizer` and `cmake --build --preset sanitizer`.
- Unit tests: parser cursor, recovery, AST verifier, and focused lexer/parser
  token interaction tests through `ctest --preset default -R unittest`.
- Parser coverage: `python3 scripts/check-parser-coverage.py` validates
  `parser-coverage.yml` against `17-grammar-reference.md` and parser entry
  points.
- Ambiguity fixtures: positive and negative tests cover every
  ambiguity-resolution row in this RFC.
- Lit tests: AST conformance lit suite through
  `ctest --preset default -R conformance-ast --output-on-failure`.
- Conformance: grammar and AST coverage through
  `ctest --preset default -R conformance --output-on-failure`.
- Differential oracle: if `ZomParser.g4` remains, the C++ parser and grammar
  oracle must agree on accept/reject verdicts for the full conformance corpus.
- Generated files: `python3 scripts/codegen/gen_ast.py --check`.
- RFC: `python3 scripts/check-rfc.py`.
- Format: `python3 scripts/check-format.py`.
- Drift: spec-alignment inventory for lexer tokens, grammar productions,
  parser functions, AST node construction, modifiers, and diagnostics.
- Recovery: malformed inputs exercise missing delimiters, invalid nested
  groups, missing operands, missing types, missing patterns, EOF inside every
  delimiter kind, and repeated invalid tokens.
- AST verifier: focused unit tests construct invalid internal trees and verify
  that publication fails.
- Negative coverage: all grammar `REJECT` fixtures must exit non-zero through
  `zomc compile --dump-ast`.
- Positive coverage: all grammar `ACCEPT` fixtures must exit zero and produce a
  schema-valid AST dump.

## Open Questions

None.

## Status History

| Date | Status | Notes |
|---|---|---|
| 2026-06-30 | DRAFT | Initial draft. |
| 2026-06-30 | REVIEW | Completed parser architecture draft and opened required owner review. |
