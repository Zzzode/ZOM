# ZOM Toolchain Product Ecosystem

Updated: 2026-08-27. Non-normative product-design note (see this directory's
README for authority and the sourcing caveat).

This note surveys how top-tier language toolchains build their surrounding
products - package manager, build tool, language server, formatter, debugger -
and recommends product boundaries for ZOM under `products/`. The recommendation
is derived from industry best practice alone. Existing directories, RFCs, and
code are treated as refactorable or deletable inputs, never as constraints.

## The decisive finding: one unified front-end command

Every mature, general-purpose language toolchain of the last decade exposes a
**single front-end command** that resolves dependencies, builds, tests, and
runs. Cargo (`cargo build/test/run/add/publish`), Go (`go build/test/run/get/
mod`), and SwiftPM (`swift build/test/run/package`) all do this. None of them
ships a separate user-facing "package manager binary" and "build tool binary".
Splitting those roles into two commands is rare and, where it exists, is a
symptom of retrofitting an older compiler, not a design anyone chooses fresh.

ZOM already matches this shape: `zomc` is a single binary built on
`zc::MainBuilder` with `addSubCommand("compile", ...)` and
`addSubCommand("run", ...)`. The best-practice path is to **grow `zomc` into
ZOM's `cargo`/`go`** - add `build`, `test`, `add`, `remove`, `update`, `fmt`,
and later `publish` as subcommands of the one binary - not to introduce
additional user-facing binaries.

Consequently there is no `zomcrate` binary and no `zomforge` binary in this
design. "Crate" survives only as the *ecosystem noun* for a ZOM package (as
"crate" is in Rust): the registry is a crate registry and `zomc add` fetches
crates. It is not a separate product or directory. The empty `products/zomcrate/`
and `products/zomforge/` placeholder directories have been removed because
best practice does not call for them.

The one argument that could justify a split - isolating the frequently-run,
never-networked local build path from the network-touching distribution path
(a blast-radius argument) - does not outweigh matching every peer language, and
can be satisfied inside one binary by confining all network access to the
distribution subcommands. It is recorded here as the road not taken, not adopted.

## Industry comparison at a glance

| Product | Best-practice model | ZOM decision |
|---|---|---|
| Package + build | Cargo, Go, SwiftPM: one command does resolve + build + test + run | One binary `zomc` with subcommands |
| Package resolution | Declarative manifest + lockfile + deterministic resolver | `Zom.toml` + byte-exact `Zom.lock` + deterministic resolver |
| Build engine | Content-addressed action cache (Go), unit graph (Cargo) | Thin scheduler over the compiler's incremental query layer |
| Language server | clangd, gopls reuse the compiler; rust-analyzer forked only because rustc was a batch compiler | Reuse the incremental query layer (clangd/gopls model) |
| Formatter | gofmt/Black zero-config; Prettier Doc-IR engine | Zero-config, one style, Doc-IR engine, `zomc fmt` subcommand |
| Debugger | Emit DWARF, reuse LLDB/GDB via DAP (Rust, Swift); nobody writes their own | Emit DWARF + reuse LLDB via `lldb-dap` |

## 1. Package management (a `zomc` capability, not a separate tool)

Mature winners: Cargo (declarative `Cargo.toml` + `Cargo.lock` + PubGrub
resolution + registry), Go modules (`go.mod`/`go.sum` + minimal version
selection + proxy/checksum-db, no central registry), SwiftPM (manifest-as-code,
the minority choice), pnpm (content-addressed store, the durable idea).

Best-practice choices for ZOM:

- **Declarative `Zom.toml`** (like Cargo/Go, not SwiftPM's executable manifest):
  no code execution to read a manifest.
- **Canonical byte-exact `Zom.lock`** with SHA-256 manifest + source-tree
  digests: at least as strong as Cargo/Go, and aligned with ZOM determinism.
- **Deterministic resolver with a single-version-per-coordinate rule** (like
  Dart/SwiftPM, unlike Cargo's multi-version): the right call for a language
  with coherence semantics. Go's minimal-version selection is the simpler
  alternative and is worth recording as the road not taken.
- **Signed registry + hardened extractor + sandboxed build scripts** for the
  distribution path.

Sequencing (industry lesson: you do not need a hosted registry to be useful):
ship **path + git/VCS dependencies + lockfile first**; the signed hosted
registry is a later milestone. Surface these as `zomc` subcommands: `new`/`init`,
`build`, `test`, `add`, `remove`, `update` (+ `--locked`); soon `run`, `check`,
`vendor`, `tree`; later `publish`/`search`/`login`/`yank`.

## 2. Build engine (a `zomc` capability)

Mature winners: Cargo (unit graph + fingerprints, build=package in one binary),
Go (`go build` content-addressed action cache, zero config), Bazel/Buck2
(hermetic sandbox + content-addressed remote cache - powerful but BUILD-file
overhead unjustified below monorepo scale), SwiftPM/llbuild (manifest ->
package graph -> low-level engine -> compiler driver).

Best-practice choices for ZOM:

- **Build model**: a thin scheduler owning the package-target unit/action graph,
  delegating all fine-grained incrementality to the compiler's incremental query
  database. Action keys are canonical SHA-256 identities (toolchain revision +
  compilation request + features + target registry revision + dependency keys) -
  Go's recursive action-ID model in ZOM's vocabulary. No Cargo-style mtime
  fingerprint layer; ZOM source snapshots are already content-digested.
- **Build files**: none. A closed-schema `Zom.toml` plus layout convention.
  Bazel's per-target files are unjustified for a single-language project whose
  compiler already knows its dependency graph.
- **Caching**: one content-addressed local cache (CAS + action-cache split),
  per-profile/per-triple isolation, toolchain revision in every key; a cache-key
  collision with differing outputs is a fail-closed error. Remote caching
  designed-for but deferred; no remote execution.
- **Build scripts**: execute only through a default-deny sandbox with
  declared-inputs-only rerun keys - structurally superior to Cargo's unsandboxed
  `build.rs`.
- **CLI**: `zomc build/test/run/clean/check`, all subcommands of the one binary.

## 3. Language server

Mature winners: rust-analyzer (separate salsa-based analyzer, because rustc was
not incremental/resilient/lazy), clangd and gopls (reuse the compiler/type
checker, add snapshotting on top).

Best practice for a compiler that is *already incremental*: **reuse the
compiler's incremental query layer** (clangd/gopls model), do not build a
rust-analyzer-style separate analyzer. The reasons rust-analyzer forked - a
batch compiler with no lazy, resilient, incremental core - do not apply to a
compiler designed with incremental queries from the start. The language server's
"semantic snapshots" should be leases over the compiler's query-DB snapshots -
gopls's Snapshot / rust-analyzer's Analysis, over ZOM's database.

Key design points:

- Map LSP document versions to query-DB inputs; cancel superseded leases; make
  sealed input-frontier validation (not prompt cancellation) the correctness
  boundary; rely on equality-based backdating for edit latency.
- Reconcile a fail-closed verified frontend with an IDE's need for partial
  results via **two disjoint authority rails**: one recoverable CST feeds both a
  recovery-free verifier (authoritative) and a non-authoritative IDE rail, with
  a permanent valid-source differential-equality gate. Do not weaken the
  compiler rail to serve the IDE.
- Transport: JSON-RPC over stdio, LSP lifecycle, incremental sync, negotiate
  UTF-8 position encoding (store UTF-8 bytes as authoritative, convert only at
  the adapter), `ContentModified`/`RequestCancelled` for stale/superseded.
- Product location: an IDE facade (no AST/HIR/`DefId` in public values) plus the
  only JSON-RPC/URI/UTF-16 component, both depending on the compiler as a
  library, never the CLI. v1: diagnostics + hover + goto-def + completion.

## 4. Formatter (a `zomc fmt` capability)

Mature winners: gofmt and Black (zero-config, one true style), Prettier (the
Wadler/Lindig Doc-IR algebra: `group`/`indent`/`line`/`softline`/`hardline`/
`ifBreak`/`fill` + width-driven `fits`), rustfmt and clang-format (configurable,
and paying for it - rustfmt needed a whole "style edition" mechanism to change a
default; clang-format carries hundreds of options).

Best-practice choices for ZOM:

- **Config**: zero-config, one fixed style (gofmt/Black). Fits ZOM's strict
  culture; hold the line against a config file.
- **Engine**: the Doc-IR algebra - a language printer emits intent, one generic
  printer owns all line-breaking, with a pinned target line width. More
  maintainable than rustfmt's per-node heuristics; simpler than clang-format's
  global penalty search; sufficient.
- **Input**: format from the lossless token/trivia stream, never the semantic
  AST/HIR. Comments are first-class trivia; reject on ambiguous comment
  attachment for v1. Gated on the language server's lossless snapshot.
- **Idempotence**: guarantee `format(format(x)) == format(x)`, enforced by an
  independent token-preservation verifier (ZOM's stronger analogue of Black's
  AST-equivalence self-check), tested with lit golden tests + ztest idempotence/
  round-trip/mutation suites.
- **Product**: one pure core library, exposed as `zomc fmt` / `zomc fmt --check`
  (a subcommand, not a separate binary), a CI format-check gate for `.zom`
  mirroring `scripts/check-format.py`, and format-on-save via the IDE facade.

## 5. Debugger

Overwhelming best practice, stated outright by rustc-dev-guide ("writing a
debugger from scratch... requires a lot of work... GDB and LLDB... can be
extended... This is the path that Rust has chosen") and by Swift (its compiler
is embedded inside an extended LLDB): **emit good DWARF debug info and reuse
LLDB/GDB; do not build a debugger.** Both reach IDE debugging through the Debug
Adapter Protocol via `lldb-dap`.

Best-practice choices for ZOM:

- **Debug-info generation**: emit DWARF via LLVM's DIBuilder during LLVM
  translation - a codegen responsibility layered onto the MIR -> LIR -> LLVM
  path. Downstream of object emission.
- **DAP**: a thin bridge reusing `lldb-dap`, plus ZOM-type pretty-printers (the
  `rust-lldb`/`rust-gdb` wrapper + Python printer model). The existing
  `tools/gdb`/`tools/lldb` scripts follow this shape for compiler internals and
  are to be repointed at ZOM *program* types once native output exists.
- **Sequencing**: debugging is the most downstream tool, blocked until native
  output exists (object emission + linking). Appropriate for a later quarter.

## Governance follow-ups

These notes are non-normative; turning them into contracts needs RFCs:

- A **toolchain-architecture RFC** that fixes the one-binary decision: `zomc` is
  ZOM's single front-end command (compile/run/build/test/add/fmt/...), there is
  no separate package or build binary, and the package/build engines live as
  libraries the compiler links. This supersedes any prior framing that assumed
  separate `zomcrate`/`zomforge` products and should note their removal.
- Formatter RFC: adopt the Doc-IR engine and a pinned target line width; expose
  as a `zomc fmt` subcommand.
- Language-server RFC: continue toward acceptance on the reuse-the-query-layer
  model.
- Debugger RFC: adopt "emit DWARF + reuse LLDB via lldb-dap" explicitly; keep it
  sequenced after native output.
