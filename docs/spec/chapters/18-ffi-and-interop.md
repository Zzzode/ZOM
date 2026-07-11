# Foreign Declarations And Unsafe Blocks

This chapter specifies the foreign-declaration and unsafe-block syntax currently
implemented by the frontend. The compiler does not yet type-check foreign calls,
validate FFI-safe types, link foreign symbols, lower a foreign calling convention,
or emit executable code.

## 18.1 Extern Blocks

An extern block groups foreign function and variable declarations:

```ebnf
ExternBlockDeclaration ::= 'extern' AbiLiteral? '{' ExternItem* '}'
AbiLiteral             ::= '"C"' | '"Cdecl"' | '"system"' | '"zom-cdecl"'
ExternItem              ::= ExternFunctionDeclaration
                          | ExternVariableDeclaration
ExternFunctionDeclaration ::= 'fun' Identifier FunctionSignature ';'
ExternVariableDeclaration ::= 'variable' Identifier ':' TypeExpression ';'
```

When `AbiLiteral` is omitted, the AST records `Cdecl`. The parser maps `"C"`
and `"Cdecl"` to `Cdecl`, `"system"` to `Stdcall`, and `"zom-cdecl"` to
`ZomNative`. These are frontend AST facts only; no backend calling-convention
contract exists.

An unknown ABI literal is rejected with `ZOM2091 UnknownExternAbi`.

```zom
extern "C" {
    fun read(fd: i32, buffer: str, length: u64) -> i64;
    variable errno: i32;
}
```

Extern functions require a semicolon and do not have a body. Extern blocks do
not accept type aliases, constants, static declarations, linkage attributes, or
an `unsafe` prefix.

## 18.2 Unsafe Block Expressions

An unsafe block is an expression containing a block body:

```ebnf
UnsafeBlockExpression ::= 'unsafe' BlockStatement
```

```zom
let value = unsafe {
    compute_value()
};
```

The AST records `UnsafeBlockExpr`. The current checker recognizes unsafe context
for the implemented raw-pointer operations. This syntax does not yet establish a
foreign-call safety contract because foreign-call checking is not implemented.

## 18.3 AST Contract

The current AST uses these nodes:

| Node | Purpose |
|---|---|
| `ExternBlock` | ABI plus the ordered extern item list |
| `ExternDecl` | Foreign function name, ABI, parameters, return type, and raises type |
| `ExternVarDecl` | Foreign variable name, type, ABI, and mutability fact |
| `UnsafeBlockExpr` | Unsafe block body |

Every accepted extern item is represented by one of these nodes. The frontend
does not create placeholder nodes for unsupported foreign declarations.

## 18.4 Current Boundary

The following behavior is not part of the implemented language contract:

- foreign symbol resolution or linkage;
- C-compatible aggregate layout or representation attributes;
- variadic calling-convention lowering;
- FFI-safe type or ownership validation;
- panic behavior across foreign boundaries;
- foreign exports, name mangling, or binary emission.

These behaviors require accepted architecture and executable implementation
evidence before they can become normative.
