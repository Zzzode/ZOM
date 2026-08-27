# ZOM Toolchain Product Ecosystem

Updated: 2026-08-27. Non-normative product-design note (see this directory's
README for authority and the sourcing caveat).

This note surveys how top-tier language toolchains build their surrounding
products - package manager, build tool, language server, formatter, debugger -
and recommends product boundaries for ZOM's equivalents under `products/`. It
exists because ZOM already has `products/zomcrate/` and `products/zomforge/`
directories that are empty placeholders, plus architecture RFCs for the IDE
(0023), formatter (0044), and debugger (0045) that do not yet correspond to
shipped products. It surfaces the decisions that must be made before those
placeholders receive code.

## The unifying finding

ZOM's compiler is unusually self-aware: it already owns an incremental query
database with immutable snapshots and red-green reuse (RFC 0017), canonical
content-addressed identities and revisions (RFC 0011), a verified frontend, and
a full package layer in `compiler/driver/package/` (manifest, resolver,
lockfile, source snapshots, build-script sandbox, compilation request).

Every peer ecosystem had to *build* machinery that ZOM already has:

- rust-analyzer built a separate incremental analyzer because rustc was a batch
  compiler; ZOM already has incremental queries.
- Cargo built fingerprinting outside rustc because rustc's incrementality was
  opaque to it; ZOM's incrementality is session-owned and reusable.
- gopls and clangd added snapshotting on top of their type checkers; ZOM has
  snapshots natively.

Therefore the consistent recommendation across all five products is the same:
**each tool is a thin product layer that reuses the compiler's existing
capabilities, not a parallel reimplementation.** This is also the cheapest and
most determinism-preserving design, which fits the repository's fail-closed
culture.

## Industry comparison at a glance

| Product | Reuse-the-compiler model | Purpose-built / separate model | ZOM's fit |
|---|---|---|---|
| Package manager | Cargo, Go, SwiftPM bundle package+build in one command | (rare to split) | Driver already owns the package layer |
| Build tool | Cargo units, Go action cache, SwiftPM/llbuild | Bazel/Buck2 (explicit BUILD files, hermetic, remote) | Thin scheduler over the query DB; no BUILD files |
| Language server | clangd, gopls reuse the compiler | rust-analyzer (separate analyzer) | Reuse the query layer (clangd/gopls model); RFC 0023 already does this |
| Formatter | gofmt/Black zero-config; Prettier Doc-IR engine | rustfmt/clang-format configurable | Zero-config + Doc-IR; RFC 0044 already zero-config, add the engine |
| Debugger | Emit DWARF, reuse LLDB/GDB (Rust, Swift) | (essentially nobody builds their own) | Emit DWARF + reuse LLDB; downstream of native output |

## 1. Package manager - `zomcrate`

Mature winners: Cargo (declarative `Cargo.toml` + `Cargo.lock` + PubGrub-ish
resolution + registry), Go modules (`go.mod`/`go.sum` + minimal version
selection + proxy/checksum-db, no central registry), SwiftPM (manifest-as-code,
the minority choice), pnpm (content-addressed store, the durable idea).

RFC 0012 (IMPLEMENTING) has already made the mature choices and they check out
against the industry:

- **Declarative `Zom.toml`** (like Cargo/Go, not SwiftPM's executable manifest):
  correct and safe (no code execution to read a manifest).
- **Canonical byte-exact `Zom.lock`** with SHA-256 manifest + source-tree
  digests: stronger than Cargo/Go, aligned with ZOM determinism.
- **PubGrub resolver + single-version-per-coordinate rule**: defensible; the
  single-version rule (like Dart/SwiftPM, unlike Cargo's multi-version) is the
  right call for a language with coherence semantics. Go's minimal-version
  selection was the simpler alternative; the deterministic decision order in
  RFC 0012 neutralizes PubGrub's usual non-determinism concern. Worth recording
  MVS as the road not taken.
- **Ed25519-signed registry + hardened extractor + sandboxed build scripts**:
  stronger than crates.io.

v1 sequencing (industry lesson: you do not need a hosted registry to be useful):
ship **path + git/VCS dependencies + lockfile first**; the signed hosted
registry is a later milestone. CLI v1: `new/init`, `build`, `test`, `add`,
`remove`, `update` (+ `--locked`); soon `run`, `check`, `vendor`, `tree`; later
`publish`/`search`/`login`/`yank`.

## 2. Build tool - `zomforge`

Mature winners: Cargo (unit graph + mtime fingerprints, build=package in one
binary), Go (`go build` content-addressed action cache, zero config), Bazel/
Buck2 (hermetic sandbox + content-addressed remote cache - powerful but
BUILD-file overhead unjustified below monorepo scale), SwiftPM/llbuild
(manifest -> package graph -> low-level engine -> compiler driver).

Recommendation: `zomforge` is a **thin scheduler + cache + CLI over the
compiler**, not a reimplementation.

- **Build model**: own the package-target unit/action graph; delegate all
  fine-grained incrementality to the RFC 0017 query DB. Action keys are the
  canonical SHA-256 identities that already exist (toolchain revision +
  compilation request + features + target registry revision + dependency
  keys) - Go's recursive action-ID model in ZOM's vocabulary. Do not add a
  Cargo-style mtime fingerprint layer; ZOM source snapshots are already
  content-digested.
- **Build files**: none. Keep the closed-schema `Zom.toml`; convention for
  layout. Bazel's per-target files are unjustified for a single-language project
  whose compiler already knows its dependency graph.
- **Caching**: one content-addressed local cache (CAS + action-cache split),
  per-profile/per-triple isolation, toolchain revision in every key, a
  cache-key collision with differing outputs is a fail-closed error. Remote
  caching designed-for but deferred; no remote execution (nowhere near the scale
  that justifies it).
- **Build scripts**: execute only through the existing default-deny sandbox;
  declared-inputs-only rerun keys (hermetic, enforced) - structurally superior
  to Cargo's unsandboxed `build.rs`.
- **CLI**: `build`, `test`, `run`, `clean`, `check`, invoking the `zomc`
  session; `zomc` stays the low-level compiler entry.

## 3. Language server

Mature winners: rust-analyzer (separate salsa-based analyzer, because rustc was
not incremental/resilient/lazy), clangd and gopls (reuse the compiler/type
checker, add snapshotting on top).

RFC 0023 (REVIEW) already encodes the correct decision and the survey affirms
it: **reuse ZOM's incremental query layer** (clangd/gopls model), do not build a
rust-analyzer-style separate analyzer. The reasons rust-analyzer forked do not
apply to ZOM, which already has incremental queries (RFC 0017), body-local
invalidation (RFC 0019), and a verified tooling projection (RFC 0022). The
"semantic snapshots" in RFC 0023's title are leases over the existing query-DB
snapshots - exactly gopls's Snapshot / rust-analyzer's Analysis, but over ZOM's
existing database.

Key design points, all already in RFC 0023:

- Map LSP document versions to query-DB inputs; cancel superseded leases; make
  sealed input-frontier validation (not prompt cancellation) the correctness
  boundary; rely on equality-based backdating for edit latency.
- Reconcile the fail-closed verified frontend with an IDE's need for partial
  results via **two disjoint authority rails**: one recoverable CST feeds both a
  recovery-free verifier (authoritative) and a non-authoritative IDE rail, with
  a permanent valid-source differential-equality gate. Do not weaken the
  compiler rail to serve the IDE.
- Transport: JSON-RPC over stdio, LSP lifecycle, incremental sync, negotiate
  UTF-8 position encoding (store UTF-8 bytes as authoritative, convert only at
  the adapter), `ContentModified`/`RequestCancelled` for stale/superseded.
- Product location: `products/zomlang/tools/ide/**` (facade, no AST/HIR/`DefId`
  in public values) + `products/zomlang/tools/lsp/**` (the only JSON-RPC/URI/
  UTF-16 component); depend on the compiler as a library, never the CLI. Today
  only `tools/gdb` and `tools/lldb` exist. v1: diagnostics + hover + goto-def +
  completion.

## 4. Formatter

Mature winners: gofmt and Black (zero-config, one true style), Prettier (the
Wadler/Lindig Doc-IR algebra: `group`/`indent`/`line`/`softline`/`hardline`/
`ifBreak`/`fill` + width-driven `fits`), rustfmt and clang-format (configurable,
and paying for it - rustfmt needed a whole "style edition" mechanism to change a
default; clang-format carries hundreds of options).

RFC 0044 (DRAFT) is already aligned on config, input, idempotence, boundary, and
scope. The single highest-leverage addition the survey recommends: **name the
Doc-IR (Wadler/Prettier) algebra as the core engine and pin a target line
width.** RFC 0044 currently specifies style rules but not the engine that
enforces them.

- **Config**: zero-config, one fixed style (gofmt/Black). Fits ZOM's strict
  culture; RFC 0044 already commits to this. Hold the line against a config file.
- **Engine**: the Doc-IR algebra - a language printer emits intent, one generic
  printer owns all line-breaking. More maintainable than rustfmt's per-node
  heuristics; simpler than clang-format's global penalty search; sufficient.
- **Input**: format from the lossless token/trivia stream, never the semantic
  AST/HIR (RFC 0044 mandates this). Comments are first-class trivia; keep RFC
  0044's fail-closed "reject on ambiguous comment attachment" rule for v1.
  Gated on RFC 0023's lossless snapshot.
- **Idempotence**: guarantee `format(format(x)) == format(x)`, enforced by an
  independent token-preservation verifier (ZOM's stronger analogue of Black's
  AST-equivalence self-check), tested with lit golden tests + ztest idempotence/
  round-trip/mutation suites.
- **Product**: one pure core library at `products/zomlang/tools/formatter/**`,
  exposed as a `zomc format` / `zomc format --check` subcommand (not a separate
  binary - `zomc` already has subcommands), a `.zom` CI format-check gate
  mirroring `scripts/check-format.py`, and LSP format-on-save via the IDE facade.

## 5. Debugger

Overwhelming best practice, stated outright by rustc-dev-guide ("writing a
debugger from scratch... requires a lot of work... GDB and LLDB... can be
extended... This is the path that Rust has chosen") and by Swift (its compiler
is embedded inside an extended LLDB): **emit good DWARF debug info and reuse
LLDB/GDB; do not build a debugger.** Both reach IDE debugging through the Debug
Adapter Protocol via `lldb-dap`.

RFC 0045 (DRAFT) should adopt this decisively (it depends on native output and
ZOM already chose LLVM + has `tools/gdb`/`tools/lldb` dirs):

- **Debug-info generation**: emit DWARF via LLVM's DIBuilder during the LLVM
  translation the backend is building - a codegen responsibility layered onto
  the MIR -> LIR -> LLVM path. Downstream of object emission.
- **DAP**: ship a thin bridge reusing `lldb-dap`, plus ZOM-type pretty-printers
  (the `rust-lldb`/`rust-gdb` wrapper + Python printer model, which is exactly
  what ZOM's existing `tools/gdb`/`tools/lldb` scripts do for compiler
  internals - to be repointed at ZOM *program* types once native output exists).
- **Sequencing**: debugging is the most downstream tool. It is blocked until
  native output exists (object emission + linking, RFC 0043). This is
  explicitly the last capability, appropriate for a later quarter.

## The open decision that must precede code: zomcrate / zomforge / driver

This is the single most important product question and it is unresolved. Two
independent surveys (package + build) converged on it:

- Cargo, Go, and SwiftPM all ship **one** front-end command that does resolve +
  build + test + run. A separate package binary and build binary is rare and
  usually a symptom of retrofitting.
- ZOM's compiler driver **already owns the package layer** (manifest, resolver,
  lockfile, source snapshots, sandbox, compilation request), and RFC 0012
  currently routes the whole pipeline through `zomc compile --package`.
- Yet `products/` has both an empty `zomcrate/` and an empty `zomforge/`. Filling
  both as separate user-facing binaries while the driver also owns resolution is
  a three-way overlap that will rot.

Recommended resolution (driver as the shared engine):

1. **Compiler driver = the shared "cargo-core" engine.** Single source of truth
   for the manifest model, resolver, lockfile, source materialization, sandbox,
   and canonical compilation request. Both tools link it; neither reimplements
   it. Two independent TOML parsers or resolvers would be a determinism hazard
   the repository would not tolerate.
2. **`zomforge` = build/compile orchestrator + developer CLI** (`build`/`test`/
   `run`/`clean`/`check`), owning the unit graph + content-addressed cache +
   scheduling + link/run. Read-only consumer of the driver's resolved graph.
   Never touches the network.
3. **`zomcrate` = package distribution lifecycle** (publish, add/remove/update
   deps through the driver's resolver, registry/VCS acquisition, vendoring,
   checksum verification). The only network-touching tool.

The split's only real justification is isolating the frequently-run local build
path from the network-touching distribution path (a legitimate blast-radius
argument). If the team does not value that separation, **merging into one CLI is
the well-trodden, lower-risk choice and matches every peer language.** Either
way, this must be decided and recorded in an RFC (an amendment to RFC 0012, or a
new toolchain-architecture RFC) **before** either directory gets code, because
two tools with independent manifest/resolver logic would violate the
repository's no-drift and determinism rules. Right now both dirs are empty -
this is the moment to decide.

## Recommended RFC follow-ups

These notes are non-normative; turning them into contracts needs RFCs:

- A **toolchain-architecture RFC** (or an RFC 0012 amendment) that fixes the
  `zomcrate` / `zomforge` / driver boundary above. This is the prerequisite for
  any code in either directory and the highest-priority governance item.
- RFC 0044: add the Doc-IR engine and a target line width to the DRAFT, then
  advance DRAFT -> REVIEW.
- RFC 0023: continue toward ACCEPTED (it is already well-grounded).
- RFC 0045: adopt "emit DWARF + reuse LLDB via lldb-dap" explicitly; keep it
  sequenced after native output (RFC 0043).
