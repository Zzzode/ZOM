# Modules and Imports

Zom v1 uses a pure static module system. Every module relationship is resolved at compile time, and the language does not support conditional imports, runtime imports, wildcard imports, or default exports in v1.

## Design Goals

- Keep module boundaries explicit and statically analyzable
- Make imported and exported names predictable for the compiler and IDE
- Prefer one consistent symbol-path model over mixed string-path and expression-based forms
- Make public API definition obvious at the declaration site
- Support re-export without forcing wrapper modules to duplicate declarations

## Core Model

- A source file is a module definition unit
- A module name is a dotted symbol path such as `math.geometry`
- Top-level declarations are module-private unless explicitly exported
- Imports bind module names or explicitly selected exported symbols into the current module scope
- Re-exports forward symbols from another module into the current module's public API

## Module Declaration

Use `module` to declare the canonical symbol path of a source file.

```zom
module math.geometry;
```

The `module` declaration, when present, must appear before all other top-level items. A file may omit `module`; such a file is still a valid compilation unit, but it does not declare a stable importable symbol-path name in the language specification.

## Module Names

Module names are symbolic, not string-based.

```zom
math
math.geometry
graphics.rendering.opengl
```

Zom v1 deliberately avoids string module specifiers such as `"math/geometry"` in the core language grammar. Build tools may map source files to modules, but the language-level import and export syntax always uses dotted symbol paths.

## Import Forms

Zom v1 supports two import forms.

### Import a Module Namespace

```zom
import math.geometry;
import math.geometry as geo;
```

- `import math.geometry;` binds the module namespace into the current scope under its final segment, `geometry`
- `import math.geometry as geo;` binds the same module namespace under the explicit alias `geo`

This form is appropriate when call sites should remain qualified:

```zom
import math.geometry as geo;

let p = geo.Point { x: 1.0, y: 2.0 };
let d = geo.distance(p, p);
```

### Import Selected Symbols

```zom
import math.geometry.{Point, distance};
import math.geometry.{Point as GeoPoint, distance};
```

- Each listed symbol must be exported by the target module
- `as` renames the imported binding locally
- A grouped import is equivalent to importing each listed symbol individually, but remains unambiguous in the grammar

This form is appropriate when local direct access is preferred:

```zom
import math.geometry.{Point as GeoPoint, distance};

let p = GeoPoint { x: 1.0, y: 2.0 };
let d = distance(p, p);
```

## Export Forms

Zom v1 supports declaration-site export, local export lists, and explicit re-export.

### Declaration-Site Export

```zom
export struct Point {
    x: f64,
    y: f64
}

export fun distance(p1: Point, p2: Point) -> f64 {
    let dx = p1.x - p2.x;
    let dy = p1.y - p2.y;
    return sqrt(dx * dx + dy * dy);
}
```

This is the primary way to define public API in Zom. The exported status is attached directly to the declaration, which keeps API boundaries visible in the source.

### Export Local Symbols

```zom
struct Point {
    x: f64,
    y: f64
}

fun distance(p1: Point, p2: Point) -> f64 {
    let dx = p1.x - p2.x;
    let dy = p1.y - p2.y;
    return sqrt(dx * dx + dy * dy);
}

export { Point, distance as calcDistance };
```

This form exports names that already exist in the current module scope. It is useful when a module wants to define declarations first and publish its API later as a single block.

### Re-Export Symbols from Another Module

```zom
export math.geometry.{Point, distance as calcDistance};
```

This form forwards selected symbols from another module into the current module's public API without requiring local wrapper declarations.

## Visibility Rules

- Top-level declarations are private to the module unless exported
- Imported names are available within the current module scope according to their import form
- Re-exported names become part of the current module's exported API
- Exporting a name does not duplicate the declaration; it only controls the module's public surface

## Name Resolution Rules

- `import module.path;` binds the last segment of the path unless an explicit alias is provided
- `import module.path.{A, B as C};` binds `A` and `C` in the current module scope
- `export {A};` requires that `A` already exists in the current module scope
- `export module.path.{A};` resolves `A` against the target module's exported symbols

## Conflict Rules

The following are compile-time errors:

- Importing a binding whose resulting local name conflicts with an existing top-level name
- Importing the same local name more than once without aliasing
- Exporting a local name that does not exist
- Re-exporting a symbol that the target module does not export
- Exporting two different symbols under the same public name

Use aliases to resolve conflicts explicitly.

```zom
import graphics.core.{Point as CorePoint};
import math.geometry.{Point as GeoPoint};
```

## Top-Level Placement Rules

- `module`, `import`, and `export` are top-level constructs
- `module` may appear at most once and must appear first when present
- `import` and export-list or re-export forms must appear at top level
- Declaration-site `export` applies only to top-level declarations

Zom v1 does not allow local imports inside functions or blocks.

## Non-Goals in v1

The following features are intentionally excluded from the v1 module design:

- Runtime or dynamic import
- Conditional import
- Wildcard import
- Wildcard re-export
- Default export
- Expression-based export such as exporting arbitrary property-access expressions

These exclusions keep the initial module system small, explicit, and amenable to static analysis.

## Examples

### Basic Module

```zom
module math.geometry;

export struct Point {
    x: f64,
    y: f64
}

export fun distance(p1: Point, p2: Point) -> f64 {
    let dx = p1.x - p2.x;
    let dy = p1.y - p2.y;
    return sqrt(dx * dx + dy * dy);
}

fun sqrt(value: f64) -> f64 {
    return value;
}
```

### Aggregator Module

```zom
module graphics;

export graphics.rendering.opengl.{OpenGLRenderer};
export graphics.rendering.vulkan.{VulkanRenderer};
```

### Mixed Import Style

```zom
module app.main;

import math.geometry as geo;
import graphics.{Renderer, SurfaceFormat};

let point = geo.Point { x: 0.0, y: 0.0 };
let format = SurfaceFormat.default();
```

## Grammar Summary

```ebnf
ModuleDeclaration ::= 'module' ModuleName ';'
ModuleName ::= Identifier ('.' Identifier)*

ImportDeclaration ::= 'import' ImportClause ';'
ImportClause ::= ModuleImportClause | NamedImportClause
ModuleImportClause ::= ModuleName ('as' Identifier)?
NamedImportClause ::= ModuleName '.' '{' ImportSpecifierList? '}'
ImportSpecifierList ::= ImportSpecifier (',' ImportSpecifier)* ','?
ImportSpecifier ::= Identifier ('as' Identifier)?

ExportDeclaration ::= 'export' Declaration
                    | 'export' ExportClause ';'
ExportClause ::= LocalExportClause | ReexportClause
LocalExportClause ::= '{' ExportSpecifierList? '}'
ReexportClause ::= ModuleName '.' '{' ExportSpecifierList? '}'
ExportSpecifierList ::= ExportSpecifier (',' ExportSpecifier)* ','?
ExportSpecifier ::= Identifier ('as' Identifier)?
```
