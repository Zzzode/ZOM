# Chapter 13 — Modules and Imports

> **Normative**
>
> This chapter defines the source-language module, import, and export surface.
> Chapter 23 defines export and member-visibility syntax.

## 13.2 Module Declarations

A source file may begin with one module declaration. If present, it must be the
first source item after the shebang and outer attributes. The declaration name
is one identifier, not a qualified path.

```zom
module geometry;
```

The block form supplies the module's source items inline:

```zom
module geometry {
    export fun distance() -> f64 { 0.0 }
}
```

The alias form associates the declared module name with a qualified module
path:

```zom
module geometry = math::geometry;
```

Only the alias form may carry `export` directly:

```zom
export module geometry = math::geometry;
```

Module declarations do not nest. A file cannot contain two module
declarations. The declaration is optional. When a package target or an import
selects a source module path, a semicolon or block declaration must match that
path's final segment exactly. A mismatch produces `ZOM3026` and the source does
not receive a module identity.

## 13.3 Module Paths

Imports and re-exports use `::` between module-path segments. A namespace
import contains at least two segments. A selected-symbol import or re-export
may use a one-segment base followed by `::{...}`.

Path segments are case-sensitive identifiers. String paths, wildcard imports,
range imports, and `from` clauses are outside the grammar.

The canonical forms are:

```zom
import math::geometry;
import math::geometry as geo;
import math::geometry::{Point, distance as metric};

export { Point, metric as distance };
export math::geometry::{Point};
```

Import and export declarations are module items. They may appear directly in a
source file or in an inline module body. They cannot appear in a function body,
block statement, loop body, match arm, or any other statement list. This keeps
module dependencies and the exported interface independent of control flow.

`.` is member-access syntax and is not a module-path separator in an import or
re-export clause.

## 13.9 Diagnostic Ownership

The authoritative diagnostic registry is the included
`compiler/diagnostics/diagnostics-*.def` files. The registered
binder diagnostics relevant to the current import/export implementation are:

| Code | Name | Meaning |
|---|---|---|
| `ZOM3001` | `UndefinedIdentifier` | A local export or reference has no binding |
| `ZOM3010` | `DuplicateIdentifier` | Two declarations or imports claim one local name |
| `ZOM3011` | `CircularImport` | An import dependency forms a cycle |
| `ZOM3012` | `ImportModuleNotFound` | An imported module cannot be resolved |
| `ZOM3013` | `ImportMemberNotFound` | A selected import is absent from the target export scope |
| `ZOM3014` | `CircularReexport` | A re-export dependency forms a cycle |
| `ZOM3015` | `ReexportModuleNotFound` | A re-export target module cannot be resolved |
| `ZOM3016` | `ReexportMemberNotFound` | A selected re-export is absent from the target export scope |
| `ZOM3023` | `ImportModuleAmbiguous` | An import or module alias resolves to multiple modules |
| `ZOM3024` | `ReexportModuleAmbiguous` | A re-export resolves to multiple modules |
| `ZOM3026` | `ModuleDeclarationNameMismatch` | A source declaration differs from its selected module path |

The parser emits `ZOM2096`
(`ImportOrExportDeclarationRequiresModuleScope`) when an import or export
declaration appears in statement context. This chapter does not reserve numeric
bands or define codes that are absent from the registry. New module diagnostics
require a registry definition, typed emission, and a conformance test in the
same change.

## 13.10 Grammar

```ebnf
ModuleDeclaration ::=
    'module' Identifier ';'
  | 'module' Identifier '{' ModuleItem* '}'
  | 'export'? 'module' Identifier '=' ModuleAliasPath ';'

ModuleItem ::=
    OuterAttributeList ModuleItemDeclaration
  | OuterAttributeList Statement
  | Statement

ModuleItemDeclaration ::=
    ImportDeclaration
  | ExportDeclaration
  | Declaration

ModuleAliasPath ::= Identifier ('::' Identifier)+
QualifiedModulePath ::= Identifier ('::' Identifier)+
GroupBasePath ::= Identifier ('::' Identifier)*

ImportDeclaration ::= 'import' ImportClause ';'
ImportClause ::=
    QualifiedModulePath ('as' Identifier)?
  | GroupBasePath '::' '{' ImportSpecifierList? '}'

ImportSpecifierList ::= ImportSpecifier (',' ImportSpecifier)* ','?
ImportSpecifier ::= Identifier ('as' Identifier)?

ExportDeclaration ::=
    'export' Declaration
  | 'export' '{' ExportSpecifierList? '}' ';'
  | 'export' GroupBasePath '::' '{' ExportSpecifierList? '}' ';'

ExportSpecifierList ::= ExportSpecifier (',' ExportSpecifier)* ','?
ExportSpecifier ::= Identifier ('as' Identifier)?
```

The parser, ANTLR grammar, AST schema, conformance corpus, and this grammar must
accept and reject the same surface.
