# ZOM Core Library

This directory contains the compiler-supplied, source-backed `core` library.
It is not a package, has no release identity, and is compiled from the exact
source inventory under `src/`.

## Source Surface

The admitted source tree contains exactly these modules:

- `src/core.zom` declares the `core` root module.
- `src/core/marker.zom` declares the public `Copy` and `Linear` interfaces.
- `src/core/prelude.zom` publicly re-exports those marker interfaces.

The inventory generator and source-admission verifier authenticate the paths,
contents, role declarations, and distribution digest. A source change must
update the generated inventory through the normal CMake target and preserve the
closed distribution contract in `zomlang/compiler/source/`.

## Contribution Boundary

Core declarations belong in ZOM source under `src/`; do not add duplicate C++
runtime declarations, compiler-built source-less definitions, or a package
manifest. The current library is declaration-only. Allocation, ownership,
executable-body, and broader API support require their own accepted compiler
and language contracts before the source inventory can grow.
