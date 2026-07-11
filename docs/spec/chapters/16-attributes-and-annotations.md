# Chapter 16 - Attributes and Marker Uses

## 16.1 Scope

An attribute is source metadata attached to a module item, a non-expression
block item, or a function parameter. This chapter defines the parsed attribute
surface, AST retention, placement rules, and the marker-implementation syntax
recognized by the compiler.

Attributes do not introduce declarations, execute code, rewrite syntax, or
re-enter binding. A semantic phase may consume a specifically registered
attribute path. Every other well-formed qualified attribute remains inert
syntax metadata.

## 16.2 Outer Attribute Grammar

ZOM supports outer attributes only:

```ebnf
OuterAttributeList ::= OuterAttribute*

OuterAttribute ::= '#' '[' AttributeEntry
                       (',' AttributeEntry)* ','? ']'

AttributeEntry ::= AttributePath AttributePayload?

AttributePayload ::= '(' AttributeInput? ')'
                   | '=' Expression

AttributeInput ::= AttributeInputItem
                   (',' AttributeInputItem)* ','?

AttributeInputItem ::= IdentifierName '=' AttributeInputValue
                     | AttributeInputValue

AttributeInputValue ::= Expression
                      | '{' AttributeInput? '}'

AttributePath ::= IdentifierName ('::' IdentifierName)+
                | BuiltinSingleSegmentAttribute

BuiltinSingleSegmentAttribute ::= 'inline'
                                | 'deprecated'
                                | 'cold'
                                | 'repr'
```

`IdentifierName` includes identifiers and keyword spellings accepted as path
segments. `::` is the only attribute-path separator. A period in an attribute
path is rejected.

The `#` and `[` tokens must be byte-adjacent. Whitespace or a comment between
them does not begin an attribute. `#![...]` is not an attribute form and is
rejected by lexical and parser diagnostics.

An empty attribute body is invalid. Several entries may appear in one bracket
pair, and several bracket pairs may be stacked:

```zom
#[audit::trace, lint::allow("unused")]
#[route::register(get("/items"), priority = 10)]
fun handler();
```

Attribute arguments are parsed as expressions. A braced nested input preserves
grouping for inputs such as:

```zom
#[schema::field(name = "point", options = { packed = true })]
fun decode();
```

## 16.3 Placement

### 16.3.1 Module and Block Items

An outer attribute list may prefix a module item or a non-expression item in a
block. The parser stores the list on the containing `StatementListItem`.

Valid targets include:

- imports and exports;
- functions and named type declarations;
- aliases and value declarations;
- standalone impl declarations and extern blocks; and
- block or control-flow statements represented as non-expression statement
  items.

An attribute list cannot prefix a bare expression statement. It also cannot
attach directly to a module declaration, match arm, type expression, pattern,
or expression operand.

```zom
#[api::entry]
export fun run() {}

fun example() {
    #[trace::scope]
    if ready { run(); }
}
```

The following form is rejected because `value` is an expression statement:

```zom
fun example() {
    #[trace::value]
    value;
}
```

### 16.3.2 Function Parameters

A function parameter may carry an outer attribute list immediately before the
parameter:

```zom
fun consume(#[zom::param::move] this, #[ffi::nonnull] ptr: *const u8);
```

The parser stores the list on `FunctionParameterDecl.attrs`.

### 16.3.3 Type Members and Enum Variants

Attributes are not accepted on class, struct, interface, error, or impl
members, or on enum variants. These nodes do not have attribute storage in the
AST schema. The parser rejects the attribute at its `#` token instead of
discarding it.

## 16.4 AST Representation

The parsed representation is:

```text
AttributeList {
  attrs: [Attribute],
}

Attribute {
  path: AttributePath,
  args: [Expression],
}

AttributePath {
  segments: [IdentId],
  leading: AttributePathLeading,
}
```

The recursive parser currently produces `leading = None`. Attribute argument
expressions are ordinary immutable AST nodes. Source order is preserved in
both the attribute list and argument list.

`StatementListItem.attrs` owns attributes for module and block items.
`FunctionParameterDecl.attrs` owns parameter attributes. A declaration node
does not infer attributes by scanning preceding source text.

Binder, checker, and IR facts refer to the retained attribute node or to a
validated semantic fact derived from it. They never store a pointer into a
temporary parser structure.

## 16.5 Attribute Path Policy

A user-defined or tool-defined attribute path contains at least two segments.
This keeps attribute names in explicit namespaces and prevents a local
declaration from changing the meaning of a short attribute spelling.

The four single-segment built-ins are closed:

- `inline`;
- `deprecated`;
- `cold`; and
- `repr`.

Adding another single-segment attribute requires a parser change, an AST or
semantic consumer, a registered diagnostic contract, and conformance tests in
the same change.

Qualified attributes are syntactically accepted even when no semantic phase
consumes their path. Acceptance guarantees only AST retention. It does not
grant code generation hooks, runtime reflection, or package
loading.

## 16.6 Registered Semantic Consumers

### 16.6.1 Move Receiver

The checker recognizes exactly `zom::param::move` on a `this` parameter. It
marks a method receiver as consuming for dynamic object-safety analysis.

The attribute has no effect on a non-`this` parameter. Duplicate occurrences
do not create additional consumption events.

### 16.6.2 Unavailable Conditional Compilation

The exact attribute path `zom::cfg` is rejected with
`ZOM2090 ConditionalCompilationUnavailable`. The compiler has no conditional
predicate environment, source-selection phase, or AST-removal contract. A
future conditional-compilation design requires an accepted RFC and must add its
syntax, semantic phase, diagnostics, build inputs, and conformance matrix in one
change.

## 16.7 Marker Paths

A marker path is a type-system path used in dynamic types, generic bounds, and
marker impl declarations:

```ebnf
MarkerPath ::= QualifiedPathOrIdent
```

Marker paths are not attribute invocations. They reuse `AttributePath` AST
storage because both are ordered identifier paths, but semantic resolution is
owned by the binder and checker.

Examples:

```zom
fun send<T: std::marker::Sendable>(value: T);

let object: dyn Drawable + std::marker::Sendable;
```

## 16.8 Marker Impl Declarations

A negative marker impl has an explicit `!` and may use a short or qualified
path:

```ebnf
NegativeMarkerImpl ::= 'unsafe'? 'impl' TypeParameters? '!'
                       MarkerImplPath 'for' TypeExpression
                       WhereClause? (';' | StructBody)
```

A positive marker impl is selected syntactically only when its qualified path
contains the segment `marker`:

```ebnf
QualifiedMarkerImpl ::= 'unsafe'? 'impl' TypeParameters?
                        QualifiedMarkerPath 'for' TypeExpression
                        WhereClause? (';' | StructBody)
```

Other positive impl heads use the standalone interface-impl grammar from
Chapter 9. The semantic resolver determines their interface meaning.

```zom
impl !Shared for Buffer;
unsafe impl std::marker::Sendable for DeviceHandle;
```

There is no source form that declares a new marker. Marker identities are
resolved from compiler and standard-library definitions already present in the
semantic context.

## 16.9 Diagnostics and Conformance

A stray `#` emits registered `ZOM2057 DanglingHash` with
`ZOM2058 DanglingHashHelp`. Other malformed attribute shapes use the registered
lexer and parser diagnostics appropriate to the failing token. This chapter
allocates no numeric diagnostic codes.

Conformance covers:

- qualified and built-in single-segment paths;
- rejection of period-separated and unknown short paths;
- `#` and `[` adjacency;
- empty, stacked, multi-entry, nested, and trailing-comma inputs;
- module-item and parameter attachment;
- rejection on expression statements, match arms, type members, enum variants,
  module declarations, types, patterns, and expression operands;
- `zom::param::move` checker consumption; and
- negative and qualified marker impl disambiguation.

Every attribute or marker semantic added to the compiler requires parser or AST
coverage, a registered diagnostic contract, checker tests, and `.zom`
conformance evidence in the same change.
