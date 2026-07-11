# Concurrency Frontend Surface

## 15.1 Scope

The current language surface contains one concurrency expression,
`spawn`, and one concurrency statement, `suspend`. This chapter defines their
lexical, parser, and AST contracts.

The compiler does not currently define task types, capture transfer rules,
scheduling, cancellation, wake events, synchronization primitives, a memory
model, or runtime execution for these nodes. Parser acceptance alone does not
grant any of those semantics.

## 15.2 Spawn Expressions

### 15.2.1 Grammar

```ebnf
SpawnExpression ::= 'spawn' SpawnModifier*
                    (SpawnBlockBody | AssignmentExpression)

SpawnModifier ::= 'detached'
                | 'blocking'
                | 'priority' '(' SpawnPriority ')'

SpawnPriority ::= 'high' | 'low'

SpawnBlockBody ::= BlockStatement
```

Modifiers are contextual identifiers after `spawn`. They are separated by
whitespace; a comma is not part of the recursive-parser surface. The parser
recognizes only `detached`, `blocking`, and `priority(high|low)` as modifiers.
An unrecognized identifier begins the expression body instead.

Examples:

```zom
let task = spawn compute(1, 2);
let background = spawn detached { work(); };
let io = spawn blocking priority(high) read_sync(path);
```

`spawn` is an expression. When it appears in statement position, the containing
block's expression-statement and final-expression rules determine whether a
trailing semicolon is required.

### 15.2.2 AST Contract

Every accepted form produces `SpawnExpression` with:

- `mod_flags` bit zero set for `detached`;
- `mod_flags` bit one set for `blocking`;
- `priority` equal to `None`, `High`, or `Low`; and
- `body` containing a statement node.

A block body is stored as `BlockStmt`. A non-block expression body is wrapped
in `ExpressionStatement` so the `body` field has one stable statement shape.
Source order and source range are preserved by the ordinary AST contracts.

### 15.2.3 Rejected Forms

The parser rejects malformed modifier calls, including a missing priority
argument and any priority other than `high` or `low`. It also rejects a missing
body and expression bodies that do not satisfy the ordinary expression grammar.

## 15.3 Suspend Statements

### 15.3.1 Grammar

```ebnf
SuspendStatement ::= 'suspend' ';'
                   | 'suspend' 'until' Expression ';'
```

The semicolon is required in both forms. `until` is a contextual identifier in
this position. Any other token between `suspend` and the condition is rejected.

Examples:

```zom
suspend;
suspend until ready;
```

### 15.3.2 AST Contract

`suspend;` produces `SuspendStatement` with mode `Bare` and no condition.
`suspend until expression;` produces mode `Until` and stores the expression in
`until_cond`. The current parser never produces another suspend mode and always
stores zero in `on_timeout_ms`.

`suspend` is not an expression. Forms such as `let value = suspend;` are not in
the language grammar.

## 15.4 Reserved Concurrency Tokens

`async` and `await` are lexer keywords but are not accepted expression or
declaration forms. They do not produce concurrency AST nodes. `actor` and
`channel` are likewise reserved without a language-level concurrency type
contract.

The recursive parser emits registered parser diagnostics for these forms. The
grammar conformance runner also rejects them. No checker or runtime phase may
interpret a rejected spelling as concurrency semantics.

## 15.5 Semantic Boundary

The binder may traverse child syntax, but it publishes no task, scheduler, or
suspension facts for `SpawnExpression` or `SuspendStatement`. The checker does
not assign a task-handle type, enforce capture markers, or validate an event
contract. The runtime exposes no implementation contract derived from these
nodes.

Any semantic concurrency design changes the language contract and requires an
accepted RFC, registered diagnostics, checker facts, lowering ownership,
runtime behavior, and executable conformance in the same implementation.

## 15.6 Conformance

The current conformance corpus covers:

- block and expression spawn bodies;
- `detached`, `blocking`, and `priority(high|low)` AST retention;
- combinations and nested spawn expressions;
- malformed and unknown spawn modifier forms;
- bare and `until` suspend statements;
- required suspend semicolons and rejection of non-`until` clauses; and
- rejection of `async` and `await` source forms.

These tests prove frontend syntax and AST behavior only. They do not claim
concurrency execution or memory-model semantics.
