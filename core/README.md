# ZOM Core Library

The standard ZOM core library, shipped with the compiler and implicitly
available to every ZOM program. This package is the reference example of a
ZOM project: read its sources to learn ZOM module syntax, interface
declaration, enum declaration, function definitions, and re-export patterns.

## Project Structure

```
core/
  Zom.toml            Package manifest (name, version, edition, lib target)
  src/
    core.zom          Root module declaration
    core/
      marker.zom      Copy and Linear marker interfaces
      prelude.zom     Automatically imported re-exports
      math.zom        Integer arithmetic functions
      ordering.zom    Ordering enum for comparison results
```

## Modules

| Module | Purpose |
|---|---|
| `core` | Root module of the core library. |
| `core::marker` | Marker interfaces that control ownership semantics. `Copy` opts a type into implicit duplication; `Linear` requires exactly-one-use. |
| `core::prelude` | Re-exports the marker interfaces. Automatically imported into every ZOM program. |
| `core::math` | Integer arithmetic functions: `double`, `square`, `negate`, `add`, `sub`, `mul`. |
| `core::ordering` | The `Ordering` enum (`Less`, `Equal`, `Greater`) for total-order comparison results. |

## Adding Declarations

The core library is a regular ZOM package. To add a new declaration:

1. Create a `.zom` file under `src/` whose path matches the desired module
   path. For example, `src/core/option.zom` declares `module option;` and
   becomes `core::option`.
2. Add your declarations with `export` to make them visible to consumers.
3. If the declaration should be in the prelude, re-export it from
   `src/core/prelude.zom`.
4. Add the new file to `cmake/utils/core-library.cmake` (materialization
   command, `DEPENDS` list, custom target, and install rule) and to the
   expected-file lists in `tests/cmake/verify-core-source-install.cmake`
   and `tests/cmake/verify-core-library-install-consumer.cmake`.

No compiler changes are needed. The CLI reads `Zom.toml` to find the source
root and compiles every `.zom` file under it through the same pipeline as
user code.

## Relationship to the Compiler

The compiler discovers `Copy` and `Linear` by name in the checked
`core::marker` module during two-phase checking. Core modules are checked
first with an explicit-only marker policy (no auto-Copy obligations), then
the compiler constructs the real marker policy from the discovered marker
definitions and re-checks all modules. This means the core library is not
special-cased: it goes through parse, bind, and check exactly like user
code.

## Learning ZOM

The core library sources are intentionally small and well-commented. They
demonstrate:

- **Module declaration**: `module core;` in `src/core.zom`
- **Interface declaration**: `export interface Copy {}` in `src/core/marker.zom`
- **Enum declaration**: `export enum Ordering { Less, Equal, Greater }` in `src/core/ordering.zom`
- **Function definition**: `export fun double(x: i32) -> i32 { return x + x; }` in `src/core/math.zom`
- **Re-export**: `export core::marker::{Copy, Linear};` in `src/core/prelude.zom`

For the full language reference, see `docs/spec/`.
