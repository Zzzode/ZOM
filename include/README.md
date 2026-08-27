# ZOM Public C++ API Headers

This directory holds the **public, exported** C++ headers for embedding the ZOM
compiler as a library. Headers placed here live under `include/zomlang/...` and
are the only headers an external consumer is expected to `#include`.

## Two-tier header model

ZOM deliberately splits headers into two tiers:

- **Public / exported headers — here, under `include/zomlang/`.** These form the
  stable surface for out-of-tree consumers. Prefix them with the `zomlang/`
  project namespace so an external `#include "zomlang/..."` resolves
  unambiguously against this include root (the LLVM/Swift `include/<project>/`
  convention).
- **Internal headers — co-located with their `.cc` under `zomlang/`.** Every
  header used only inside the compiler lives next to its implementation
  (`zomlang/compiler/lexer/lexer.h` beside `lexer.cc`). Internal code
  `#include "zomlang/compiler/..."` resolves against the repository root, which
  is also an include root. Internal headers are **not** mirrored here.

This mixed layout is intentional: exported API is centralized and versioned,
while internal headers stay co-located for fast navigation and edits. It is not
an inconsistency to reconcile.

## Current state

No public headers are exported yet. This directory is a reserved location for
the eventual C/C++ embedding API; it must not be populated with internal
headers. When the embedding surface is designed, its contract belongs in an RFC
before headers land here.
