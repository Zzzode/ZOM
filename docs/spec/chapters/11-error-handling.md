# Chapter 11 - Error Effects

> **Normative**
>
> This chapter defines the source-level `raises` effect and the checked
> postfix error operators. It does not define target layout, runtime panic
> strategy, cleanup lowering, process exit behavior, or a standard-library
> error interface.

## 11.1 Function Error Effects

A function-like declaration may attach one `raises` clause after its return
type:

```zom
fun read() -> str raises IoError;

class Resource {
    init() raises InitError { }
    deinit() raises CleanupError { }
    fun refresh(this) -> bool raises IoError | ParseError { true }
}
```

Function types and lambda expressions use the same clause:

```zom
alias Reader = () -> str raises IoError;
let parse = (text: str) -> i32 raises ParseError => 0;
```

The grammar is:

```ebnf
RaisesClause ::= 'raises' TypeExpression
```

The clause contains one type expression. A union error set therefore uses the
ordinary union-type operator:

```zom
fun load() -> Data raises IoError | ParseError;
```

Comma-separated and empty `raises` clauses are syntax errors.

The semantic function type stores the return type and the optional raises type
as distinct components. Function-type equality includes both components. A
function type with a raises effect is not equal to the otherwise identical
function type without that effect.

## 11.2 Raising Calls

A call to a function without a raises effect has the declared return type.
A call to a function with return type `T` and raises type `E` has an error-union
expression type whose success component is `T` and whose residual component is
`E`. Its canonical value type is the ordinary normalized union `T | E`, while a
checked error-union role fact retains the success and residual components. That
role is expression metadata and does not change commutative union-type identity.

A direct raising call produces the role fact. Parentheses and checked
coercions preserve it. A binding or assignment preserves it only when the value
has one unambiguous incoming role; a control-flow join preserves it only when
all incoming values have identical success and residual types. An ordinary
union construction or a join with different roles has no error-union role.

This source-level rule does not select an error-union tag, byte layout, calling
convention, runtime symbol, or unwind strategy.

## 11.3 Postfix Error Operators

`?!` and `!!` are postfix operators:

```ebnf
PostfixSuffix ::= '?!' | '!!' | '++' | '--'
```

They are applied after the call/member/index chain forming their operand and
before prefix, cast, arithmetic, conditional, or assignment operators.
Repeated postfix suffixes associate from left to right.

Both operators require a checked error-union role fact. The canonical union's
sort order does not determine success or residual meaning. Applying either
operator to an ordinary union or a non-union emits the registered non-error-
union checker diagnostic. A role fact with missing, overlapping, or
non-canonical components is an internal invalid semantic input.

### 11.3.1 Propagation With `?!`

For an operand with success type `T` and residual type `E`, `expr?!` has type
`T`.

The containing function must have an explicit raises effect that accepts every
residual alternative. A residual is accepted when it equals or is a subtype of
the declared raises type, or when it equals or is a subtype of one alternative
in a declared raises union.

```zom
fun fetch() -> i32 raises IoError { 0 }

fun load() -> i32 raises IoError {
    return fetch()?!;
}
```

Using `?!` outside a function with a compatible raises effect emits
`ZOM4025 ErrorPropagateOutsideRaises`.

### 11.3.2 Forced Unwrap With `!!`

For an operand with success type `T`, `expr!!` has type `T`. The error branch is
a panic edge rather than a recoverable return edge. The source span of `!!` is
retained for panic metadata by lowering paths that support the surrounding
checked source shape.

This chapter does not define panic formatting, backtraces, unwind behavior,
abort behavior, or FFI containment.

## 11.4 Diagnostics

The authoritative definitions live in
`products/zomlang/compiler/diagnostics/defs/diagnostics-checker.def`.

| Code | Name | Condition |
|---|---|---|
| `ZOM4025` | `ErrorPropagateOutsideRaises` | A `?!` residual is not accepted by the containing raises effect |
| `ZOM4026` | `ErrorUnwrapNonUnion` | `!!` is applied to a non-union type |
| `ZOM4032` | `ErrorPropagateNonUnion` | `?!` is applied to a non-union type |
| `ZOM4033` | `ErrorUnionEmpty` | A postfix error operator receives an empty union |

Parser errors for malformed raises clauses or postfix syntax use registered
`ZOM20xx` diagnostics.

## 11.5 Conformance

The conformance suite covers:

- single and union raises clauses on functions, methods, constructors,
  destructors, function types, and lambdas;
- rejection of empty and comma-separated raises clauses;
- `?!` with a compatible raises effect;
- `?!` outside a raises effect and on a non-union operand;
- `!!` on union and non-union operands;
- postfix AST precedence.

Claims about cleanup graphs, multi-layer IR, runtime panic entry points,
unwinding, native code generation, and FFI boundaries require their own
implemented and verified contracts before they may appear in this normative
chapter.
