# ZOM Package System

This document defines the package-tooling behavior implemented by the current
ZOM compiler. It is both a user reference for `Zom.toml`, `Zom.lock`, and
`zomc compile`, and a contributor reference for the package subsystem's safety
and determinism boundaries.

The implementation currently covers package manifests, local workspaces,
deterministic dependency resolution over verified records, canonical lock
graphs, digest-verified source snapshots, registered target selection, package
and crate identity construction, and the Linux build-script sandbox service.
The following integrations are outside this document's implemented contract:

- acquisition of registry or VCS sources by the `zomc` CLI;
- construction of build-script plans by the `zomc` CLI;
- RFC 0010 HIR, MIR, LIR, native backend, or binary publication.

## Manifest Discovery And Admission

`zomc compile` consumes exactly one package manifest named `Zom.toml`.
`--manifest-path <path-to-Zom.toml>` selects it explicitly. Without that option,
the compiler searches the current directory and then each parent directory for
a regular file named `Zom.toml`.

A manifest must be UTF-8 without a byte-order mark. Text, identifiers, and paths
must satisfy the compiler's canonical Unicode and path rules. The parser accepts
TOML 1.0 syntax through a closed schema: an unknown top-level table or unknown
key is an error rather than an extension point.

The admitted top-level keys are:

| Key | Shape | Purpose |
|---|---|---|
| `package` | table | Package name, version, and edition. |
| `workspace` | table | Workspace member directories. |
| `lib` | table | Optional library target. |
| `bin` | array of tables | Named binary targets. |
| `test` | array of tables | Named test targets. |
| `bench` | array of tables | Named benchmark targets. |
| `example` | array of tables | Named example targets. |
| `build` | table | Build-script contract. |
| `dependencies` | table | Target-domain dependencies. |
| `dev-dependencies` | table | Development-domain dependencies. |
| `build-dependencies` | table | Build-domain dependencies. |
| `features` | table | Feature graph. |

A root must contain `package`, `workspace`, or both. A workspace-only root may
contain only workspace configuration; package targets, dependencies, features,
and build scripts require a package in the same manifest. Each workspace member
is a canonical relative directory containing its own `Zom.toml`. Missing
members, nested workspaces, duplicate canonical member paths, and duplicate
package names are rejected.

## Package And Target Schema

`package` has exactly three required string keys:

```toml
[package]
name = "app"
version = "1.0.0"
edition = "2026"
```

The only implemented edition is `2026`. `name` is a canonical package name and
`version` is a canonical resolved semantic version.

`workspace` has one optional array key when the same manifest also contains a
package, and one required non-empty array key for a workspace-only root:

```toml
[workspace]
members = ["packages/core", "packages/tools"]
```

Targets admit only `name` and `path`. A library may omit its name and defaults
to the package name. Repeated targets require a name. Paths are canonical
package-relative `.zom` paths and must identify inventoried regular files.

```toml
[lib]
path = "src/lib.zom"

[[bin]]
name = "app"
path = "src/main.zom"

[[test]]
name = "smoke"
path = "tests/smoke.zom"
```

When they exist and no colliding explicit target is present, `src/lib.zom`
creates an implicit library target and `src/main.zom` creates an implicit binary
target named after the package. Other default target paths are:

| Kind | Default path |
|---|---|
| binary | `src/bin/<name>.zom` |
| test | `tests/<name>.zom` |
| benchmark | `benches/<name>.zom` |
| example | `examples/<name>.zom` |

Target kind/name collisions, two targets using one path, missing files, and
non-regular files are errors.

## Dependencies And Features

Every dependency is an inline table keyed by its local alias. The admitted
fields are `package`, `version`, `registry`, `trust-domain-sha256`, `git`, `rev`,
`tag`, `branch`, `subdirectory`, `path`, `features`, `default-features`, and
`optional`.

Exactly one source form is required:

```toml
[dependencies]
local_codec = { package = "codec", path = "../codec", version = "^1.0.0" }
git_codec = { package = "codec", git = "https://example.invalid/codec.git", rev = "0123456789abcdef0123456789abcdef01234567" }
registry_codec = { package = "codec", version = "^1.0.0", registry = "https://packages.example.invalid/", trust-domain-sha256 = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef" }
```

- A local source requires `path` and excludes registry and VCS selectors.
- A VCS source requires `git` and exactly one of `rev`, `tag`, or `branch`.
  `rev` is lowercase SHA-1 or SHA-256 hexadecimal. `subdirectory` is optional.
- A registry source requires `version`, `registry`, and a lowercase 32-byte
  `trust-domain-sha256` value.
- `package` defaults to the dependency alias.
- `features` is a unique string array; `default-features` defaults to `true`.
- Optional activation is admitted only for target dependencies. Development and
  build dependencies cannot be optional.

Feature entries are arrays containing one of three edge forms:

```toml
[features]
default = ["fast"]
fast = []
codec = ["dep:local_codec", "local_codec/simd"]
```

`name` selects a local feature, `dep:alias` activates an optional dependency,
and `alias/name` activates a dependency feature. Unknown edges, duplicate
canonical edges, and local feature cycles are rejected.

The resolver uses one version per package coordinate, separates target and
build feature activation, validates dependency library providers, and publishes
canonically ordered package selections and package dependency edges. Resolver
services accept verified registry, VCS, and local records. The current `zomc`
orchestration creates records and snapshots only for local workspace packages;
it does not fetch registry archives or VCS repositories.

## Compiling A Package

`zomc compile` is package-only. Positional source paths are rejected. A request
must select exactly one workspace package and at least one target:

```sh
zomc compile --package app --lib --emit=ast
zomc compile --package app --bin app --features fast,logging --emit=ir
zomc compile --manifest-path ./Zom.toml --package app --test smoke --syntax-only
```

Target selectors are `--lib`, `--bin <name>`, `--test <name>`,
`--bench <name>`, and `--example <name>`. Duplicate selections are rejected.
`--features <name[,name...]>` may be repeated, and
`--no-default-features` suppresses the package's default feature set.
`--target <profile>` selects one registered target profile. Package request
normalization also includes language options and the `abort` or `unwind` panic
selection; unsupported target capabilities fail at the registered target
boundary.

Package selection installs the normalized request, verified host and target
selections, resolved package graph, and digest-verified local snapshots into one
`CompilerSession`. Package and crate identities are derived from these verified
records rather than command spelling, host paths, pointer values, or table-local
handles.

The current compiler can run its frontend and mixed IR emission for selected
package roots. This document does not define target LIR or native binary output.

## Lock Modes

The lock graph is stored as canonical TOML in `Zom.lock`. Package and edge order
is canonical, redundant identity fields are revalidated, and parsing succeeds
only when re-encoding reproduces the original bytes.

| CLI mode | Behavior |
|---|---|
| no lock flag | Use a valid existing `Zom.lock`; otherwise resolve in memory without writing a lock file. |
| `--locked` | Require an existing valid lock graph and replay it without invoking the solver. |
| `--update-lock` | Resolve the graph and atomically replace `Zom.lock`. |

`--locked` and `--update-lock` are mutually exclusive. Atomic replacement uses
a temporary file, file synchronization, rename, and directory synchronization.
A failure exposes either the complete prior graph or the complete replacement,
never a partial lock graph.

The current CLI lock replay verifies the graph against the materialized local
workspace records. A lock graph requiring a source that the CLI has not
materialized is rejected.

## Source Identity And Materialization

A package identity includes its canonical source, package name, resolved
version, and enabled feature set. Source identities distinguish registry, VCS,
and local origins. Crate identities add the selected target kind, target name,
edition, semantic compiler options, and verified build-script output when one is
required.

Source materialization publishes an owning `DigestVerifiedSourceSnapshot` only
after validating the complete regular-file inventory and source-tree digest.
Archive admission accepts one bounded Zstandard frame containing one POSIX ustar
stream. Absolute paths, parent traversal, links, special files, duplicate paths,
Unicode normalization collisions, case-fold collisions, size-limit violations,
trailing frames, and trailing archive bytes are rejected.

Local and VCS directory materialization uses two passes and rechecks file type,
link count, size, and digest. A mutation between passes fails the operation.
Snapshot cleanup is explicit and retryable; partial materialization is never a
verified source.

## Build Scripts

The manifest build-script table admits exactly `path`, `inputs`, `outputs`,
`environment`, and `exported-environment`. `path`, `inputs`, and `outputs` are
required. Paths are canonical and unique, every input must be an inventoried
regular file, and every declared output is a `.zom` path.

```toml
[build]
path = "build.zom"
inputs = ["schema/model.json"]
outputs = ["generated/model.zom"]
environment = ["ZOM_TARGET"]
exported-environment = ["MODEL_VERSION"]
```

The build-script runtime service uses a closed request/response protocol,
verified executable and runtime identities, bounded resource and frame limits,
declared input/environment/output/export sets, and exact generated-source
verification. A cache hit is fully revalidated. A cache miss executes in two
independently created sandboxes and is published only when both verified output
records and generated bytes are identical.

Native execution is admitted only by `LinuxNativeSandboxV1` on Linux x86-64 or
AArch64. Preflight requires the supported namespace, cgroup v2, seccomp, pidfd,
timerfd, and `openat2` facilities. The executable boundary is a verified static
ELF image with a closed trusted-runtime object, symbol, relocation, operation,
and ABI manifest. An unsupported host or missing facility fails closed; there is
no unsandboxed fallback.

The current `zomc compile` path does not construct and execute a build-script
plan. If a selected root requires build output, final crate identity cannot be
constructed and compilation stops through the package build-result integrity
failure path. Contributors may exercise the build-plan, cache, and sandbox
services directly through their verified compiler APIs and tests.

## Diagnostic Safety

Package diagnostics use registered IDs in the `ZOM7001-ZOM7017`,
`ZOM7091-ZOM7093`, and `ZOM9905-ZOM9906` families. The typed package diagnostic
adapter accepts only digest-matched diagnostic documents. It renders canonical,
host-path-free document identities and escapes invalid or non-printable source
bytes before mapping validated spans onto the displayed buffer.

Invocation failures are source-less `ZOM7016` diagnostics. Rejected command-line
arguments and host paths are not diagnostic arguments. Package diagnostic
records carry closed enums and safe canonical scalars; raw credentials, registry
secrets, environment values, sandbox output, and library error strings are not
renderer inputs.

Some orchestration failures that do not yet have a complete typed producer stop
with a generic CLI failure message. Such a failure does not authorize rendering
untrusted package data.

## Determinism Contract

The package subsystem canonicalizes records at every publication boundary:

- manifest keys, workspace members, targets, dependencies, features, package
  selections, and graph edges use canonical ordering;
- resolver decisions and incompatibility evidence are independent of admitted
  input permutations;
- source-tree, signed-release, resolution, build-script, and lock identities use
  domain-separated canonical encodings and SHA-256;
- locked replay invokes the solver zero times;
- build plans execute in stable predecessor-first order;
- build-script cache publication requires byte-identical double execution; and
- diagnostics use structural facts rather than map iteration, host paths,
  completion order, or raw input spelling.

Adding an unordered publication path, a second lock encoding, a permissive
manifest extension point, a host-library fallback, or an unsandboxed build path
violates this contract.

## Contributor Verification

Use CMake presets for every build. The package-focused local checks are:

```sh
cmake --preset sanitizer
cmake --build --preset sanitizer
ctest --preset default -L package --output-on-failure
python3 scripts/check-identity-architecture.py
python3 scripts/check-compiler-session-architecture.py --check
python3 products/zomlang/tests/tools/check-vendored-dependencies.py
```

The complete repository matrix remains required before landing a package change:

```sh
ctest --preset default --output-on-failure
python3 scripts/check-format.py
python3 scripts/check-rfc.py
git diff --check
```

The resolver performance test is opt-in:

```sh
cmake --preset release -DZOM_ENABLE_PERFORMANCE_TESTS=ON
cmake --build --preset release --target package-resolver-performance-test
ctest --test-dir build-release -R '^performance-package-resolver$' --output-on-failure
```

The real sandbox integration test is also opt-in and must run on a supported,
privileged Linux host with delegated namespace and cgroup capabilities. Configure
with `ZOM_ENABLE_PRIVILEGED_LINUX_SANDBOX_TESTS=ON`, build
`linux-native-sandbox-integration-test`, and run the
`linux-native-sandbox-integration` CTest target.
