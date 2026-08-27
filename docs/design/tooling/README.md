# ZOM Toolchain Product Design Notes

This directory holds non-normative product-design notes for the ZOM toolchain
ecosystem that lives under `products/` (the package manager `zomcrate`, the
build tool `zomforge`, the language server, the formatter, and the debugger).
These notes survey mature industry practice and recommend product boundaries;
they do not define contracts.

## Authority

These notes are non-normative. When sources disagree, use this order:

1. `docs/spec/chapters/` defines user-observable language behavior.
2. Accepted and landed RFC decisions define approved contracts; their status
   records whether implementation is complete.
3. The production code and project-native tests establish what is implemented.
4. These notes synthesize those sources, survey industry practice, and expose
   open product decisions.

An `ACCEPTED` or `IMPLEMENTING` RFC is not evidence that a product exists in the
repository. A `products/` directory that contains only a stub `CMakeLists.txt`
or `README.md` is a placeholder, not a shipped product.

## What exists on disk (verified 2026-08-27)

| `products/` dir | State | Owning RFC (if any) |
|---|---|---|
| `zomlang/` | The compiler, runtime, tools, and CLI (`zomc`). Real and substantial. | many |
| `zomcore/` | Core library, source-backed. Real `src/`. | RFC 0025 (IMPLEMENTING) |
| `zomcrate/` | Package manager. Placeholder: a one-line README and a `CMakeLists.txt`. | RFC 0012 covers the compiler-internal manifest/resolver, NOT this user-facing product |
| `zomforge/` | Build tool. Placeholder: an empty `CMakeLists.txt`, no README, no RFC. | none |

The compiler driver at `products/zomlang/compiler/driver/package/` already
implements manifest parsing, deterministic resolution, the canonical lockfile,
digest-verified source snapshots, a sandboxed build-script runtime, and the
canonical package-compilation request. This is a decisive fact for the product
boundaries below: much of what other ecosystems call the "package layer"
already lives in the compiler, so the tools should be thin layers over it.

## Sourcing caveat

The industry comparisons in these notes were gathered under restricted network
access: several research passes could not fetch pages live and instead cite
canonical primary-source URLs and established documentation. Structural and
algorithmic claims are reliable; a small number of verbatim quotes should be
re-verified against their URLs before being treated as exact. All ZOM-internal
claims were verified directly against files on disk.

## Documents

| Document | Purpose |
|---|---|
| [Toolchain Product Ecosystem](product-ecosystem.md) | Industry survey + recommended product boundaries for zomcrate, zomforge, the language server, the formatter, and the debugger; and the open decisions that must precede code |
