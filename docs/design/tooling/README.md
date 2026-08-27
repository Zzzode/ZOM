# ZOM Toolchain Product Design Notes

This directory holds non-normative product-design notes for the ZOM toolchain
ecosystem: the front-end command (`zomc`), the language
server, the formatter, and the debugger. These notes derive product boundaries
from mature industry practice; they do not define contracts.

## Authority

These notes are non-normative. When sources disagree, use this order:

1. `docs/spec/chapters/` defines user-observable language behavior.
2. Accepted and landed RFC decisions define approved contracts; their status
   records whether implementation is complete.
3. The production code and project-native tests establish what is implemented.
4. These notes synthesize those sources, survey industry practice, and expose
   open product decisions.

Existing directories, RFCs, and code are **inputs, not constraints**. This
repository follows radical refactoring and delete-useless-things-immediately
(see `/AGENTS.md` and `.codex/rules/design-principles.md`): a placeholder
product directory or a prior routing decision does not bind the design. The
recommended shape is derived from best practice, and directories that best
practice does not call for are removed rather than filled.

An `ACCEPTED` or `IMPLEMENTING` RFC is not evidence that a product exists in the
repository.

## What exists on disk (verified 2026-08-27)

| Top-level dir | State |
|---|---|
| `zomlang/` | The compiler, runtime, tools, and the single front-end CLI `zomc`. Real and substantial. |
| `core/` | The `.zom` core library, source-backed. Real `src/`. RFC 0025 (IMPLEMENTING). |

`zomc` is one binary (`zomlang/utils/zomc/zomc.cc`) built on
`zc::MainBuilder` with `compile` and `run` subcommands already registered. The
best-practice recommendation grows this one binary into ZOM's `cargo`/`go`
rather than adding sibling binaries.

The empty `zomcrate/` and `zomforge/` placeholder directories were **removed**
on 2026-08-27, and the redundant `products/` shell was dropped so `zomlang/` and
`core/` sit at the repository root (the LLVM/Swift role-as-top-level layout).
Best practice (Cargo/Go/SwiftPM) is a single front-end command, so separate
package-manager and build-tool binaries are not part of the design. "Crate"
remains only as the ecosystem noun for a ZOM package.

The compiler driver at `zomlang/compiler/driver/package/` already
implements manifest parsing, deterministic resolution, the canonical lockfile,
digest-verified source snapshots, a sandboxed build-script runtime, and the
canonical package-compilation request. The package and build engines are
libraries the compiler links; `zomc` subcommands are the user-facing surface.

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
| [Toolchain Product Ecosystem](product-ecosystem.md) | Industry survey + best-practice product boundaries: one unified `zomc` command (compile/run/build/test/add/fmt), the language server, the formatter, and the debugger |
