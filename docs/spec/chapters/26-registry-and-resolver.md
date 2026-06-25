# Chapter 26 -- Registry & PubGrub Resolver (Roadmap -- v2 Release)

> **Roadmap banner.** This chapter describes the registry and dependency
> resolver subsystem, planned for ZOM **v2**. Its contents are normative
> (locked decisions, intended to prevent implementation divergence) but are
> **not required** for v1.0-rc1 self-host. v1.0 supports only `path` and
> `git` dependency sources per Chapter 21 Sec.21.3. Registry sources, the
> PubGrub resolver, and the `Zom.lock` format defined here are mandatory for
> any ZOM toolchain that claims v2 conformance.

---

## 26.0 Scope -- v2 Roadmap

The purpose of locking this chapter early in the v1 lifecycle is twofold:

1. **Prevent ecosystem fragmentation.** Independent zomc authors and
   third-party tooling can implement against a single, agreed-upon spec
   instead of each inventing their own registry protocol, index format, or
   resolver heuristic.
2. **Avoid re-debate.** The decisions documented below (index layout,
   resolver algorithm, lockfile schema, security posture) are frozen on
   publication. Future revisions to any of them SHALL go through the same
   RFC process used by the language itself, and SHALL be versioned together
   with an edition bump where user-visible.

```mermaid
flowchart TB
    CLI[zom CLI<br/>(build / update / publish / yank)]
    BT[zomc build tool<br/>manifest & workspace orchestration]
    RESOLVER[PubGrub Resolver<br/>version + feature unification]
    HTTP[Registry HTTP client<br/>Bearer-token auth, retry w/ jitter]
    WEB[crates.zomlang.org<br/>REST API surface]
    INDEX[index.git<br/>append-only NDJSON store]
    CACHE[Local cache<br/>archives / src / git / registries]

    CLI --> BT
    BT  --> RESOLVER
    RESOLVER --> HTTP
    HTTP --> WEB
    HTTP --> INDEX
    RESOLVER --> CACHE
    HTTP --> CACHE
```

---

## 26.1 Package Registry API (HTTP REST)

The canonical ZOM package registry exposes an HTTP/1.1 or HTTP/2 REST surface
whose endpoints are locked by this section. Third-party registries MUST
implement the endpoints below with the same semantics and payload schemas to
be considered ZOM-v2-compliant.

| Method | Endpoint | Purpose |
|---|---|---|
| `GET`  | `/api/v1/crates` | Search summary. Query params: `q=` (free text), `page=`, `per_page=`. Returns paginated list of crate summaries. |
| `GET`  | `/api/v1/crates/{name}` | Full crate metadata: all versions, owners, cumulative download counts, categories, keywords. |
| `GET`  | `/api/v1/crates/{name}/{version}` | Specific version metadata: checksum, dependency list, feature matrix, yank flag, publication timestamp. |
| `GET`  | `/api/v1/crates/{name}/{version}/download` | Redirect (302) to the `.tar.gz` distribution. Response `Content-Type: application/gzip`. |
| `PUT`  | `/api/v1/crates/new` | Publish a new version. Body is `multipart/form-data` with two parts: the `.crate` gzipped tarball and a JSON metadata payload matching the index schema. |
| `DELETE` | `/api/v1/crates/{name}/{version}/yank` | Mark a version as yanked. Yanked versions remain downloadable for existing lockfiles but are never the resolver's default. |
| `PUT`    | `/api/v1/crates/{name}/{version}/unyank` | Restore a previously yanked version. |
| `GET`  | `/api/v1/crates/{name}/owners` | List crate owners (user IDs, emails, display names; redacted as per registry policy). |
| `PUT`  | `/api/v1/crates/{name}/owners` | Invite one or more new owners by account identifier. |
| `DELETE` | `/api/v1/crates/{name}/owners` | Revoke an existing owner's publish and yank permissions. |

### 26.1.1 Authentication

Publish, yank, and owner-modification endpoints require authentication via
the HTTP request header:

```
Authorization: zom-token <TOKEN>
```

Tokens are issued by the registry web UI. A token's scope is **publish-only**
by default: it may never modify account-level settings, change passwords, or
read private user data, regardless of the endpoint set it accompanies.

### 26.1.2 Rate Limiting

Registries MUST signal rate exhaustion via `429 Too Many Requests` responses,
and SHOULD include a `Retry-After` header. Registry clients (the zomc HTTP
client and any compliant third-party tool) **MUST** implement exponential
backoff with full jitter for any 429 or 5xx response, capped at 30 seconds
per attempt, for a minimum of five retries before surfacing an error to the
user.

---

## 26.2 Index Format (git-based, Append-only)

The canonical ZOM index is a bare Git repository hosted at
`https://index.zomlang.org/zom-index.git`. Non-fast-forward pushes to this
repository are **prohibited**. Every update to the index SHALL append one or
more lines to existing crate index files; no historical line SHALL ever be
mutated or deleted, even for yanked versions (yank is a per-version flag
mutated in-place on the JSON record, but the record itself is never removed
and its SHA-256 checksum field is immutable once written).

### 26.2.1 File Layout

Crate index files are organised by crate name length to avoid overcrowding
single directories:

- **1-character names.** Directory `1/{first}/`; filename `{name}`.
  Example: crate `a` lives at `1/a/a`.
- **2-character names.** Directory `2/{first-two}/`; filename `{name}`.
  Example: crate `ab` lives at `2/ab/ab`.
- **3-character names.** Directory `3/{first}/{second}/`; filename `{name}`.
  Example: crate `abc` lives at `3/a/b/abc`.
- **4+ character names.** Directory `{first-two}/{next-two}/`; filename
  `{name}`. Example: crate `serde` lives at `se/rd/serde`.

### 26.2.2 NDJSON Record Schema

Each file is **newline-delimited JSON** (NDJSON). Exactly one canonical
minified JSON object per line; trailing whitespace is prohibited. A
conforming client MUST tolerate Unix (`\n`) or CRLF (`\r\n`) line endings
on read but MUST produce Unix line endings on publish.

The following JSON record is normative. Every field listed is mandatory; no
field may be omitted or replaced with `null` when a value is semantically
absent -- instead use the concrete sentinel values shown below.

```json
{
  "name": "serde",
  "vers": "1.0.150",
  "deps": [
    {
      "name": "derive",
      "req": "^1.0",
      "features": [],
      "optional": true,
      "default_features": true,
      "target": null,
      "kind": "normal",
      "registry": null,
      "package": null,
      "public": false
    }
  ],
  "cksum": "sha256:abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890",
  "features": {
    "derive":  ["dep:derive"],
    "default": []
  },
  "yanked": false,
  "links": null,
  "rust_version": null
}
```

### 26.2.3 Field Semantics

| Field | Semantics |
|---|---|
| `name` | The package's canonical name (lowercase, `^[a-z0-9][a-z0-9_-]*$`). |
| `vers` | Version string, strict SemVer 2.0. Pre-release and build metadata permitted per spec. |
| `deps[*].name` | Dependency name as used in the dependent's manifest. |
| `deps[*].req` | Version requirement using the standard range grammar (`^`, `~`, `=`, `>=`, `<`, wildcard). |
| `deps[*].features` | List of feature flags enabled on this dependency. |
| `deps[*].optional` | If `true`, this dependency is only compiled when the parent's corresponding feature is enabled. |
| `deps[*].default_features` | If `false`, the resolver MUST NOT auto-enable the dependency's `default` feature. |
| `deps[*].target` | Platform-specific dependency predicate (string CFG expression) or `null`. |
| `deps[*].kind` | One of `"normal"`, `"build"`, `"dev"`. |
| `deps[*].registry` | Source registry URL override, or `null` for the publishing registry. |
| `deps[*].package` | Rename alias: the actual upstream crate name if different from `name`; otherwise `null`. |
| `deps[*].public` | If `true`, items from this dependency are re-exported as part of the parent's public API. |
| `cksum` | Algorithm-prefixed hex digest of the `.crate` distribution. `sha256:` is required; `sha512:` and `blake3:` are optional alternatives. |
| `features` | Mapping of feature name to list of activated sub-features / `dep:` references. |
| `yanked` | If `true`, resolver MUST NOT select this version unless forced by an existing lockfile. |
| `links` | Reserved for native-library linkage metadata; `null` in v2. |
| `rust_version` | Alias for the ZOM minimum edition / compiler version required. Format: `"2026"` or a SemVer `"1.2.0"`; `null` = no constraint. |

The fact that every dependency record always contains all ten fields is
intentional. Downstream parsers are permitted to rely on schema stability;
registries SHALL NOT drop keys from the dependency objects.

---

## 26.3 PubGrub Resolver Algorithm (NORMATIVE Outline)

The resolver algorithm selected for ZOM v2 is **PubGrub**. The selection is
locked; competing resolvers (SAT-based, backtracking-only, Maven-style
nearest-wins) SHALL NOT be used as the default resolver in any
ZOM-v2-compliant toolchain.

### 26.3.1 Why PubGrub

1. **Polynomial time in common cases.** PubGrub solves practical dependency
   graphs in O(n^2) to O(n^3) in the number of package versions, avoiding the
   combinatorial blow-up of pure backtracking.
2. **Human-readable conflict chains.** On failure, PubGrub produces a
   derivation of the form *"package A v1 requires B >= 2, but package C pins
   B = 1.5, therefore conflict"*. This is surfaced directly to the user as
   a structured diagnostic and is the most actionable error format of any
   production resolver.
3. **Mature and proven.** PubGrub is the default resolver for the Dart
   language, the `pub` tool, Cargo (since RFC 2957), and Zig's package
   manager. Multiple independent, high-quality reference implementations
   exist.

### 26.3.2 Implementation Contract

A compliant resolver SHALL expose the equivalent of the following functional
signature (types are illustrative, not prescriptive):

```
solve(package: Name,
      version_constraint: Range,
      deps: Map<(Name, Version), List<Dep>>)
   -> Result<Map<Name, Version>, PubGrubConflict>
```

### 26.3.3 Version Ordering

Version ordering is **strict SemVer 2.0**. Pre-release versions are
considered **only** when the user-supplied version constraint explicitly
contains a pre-release segment:

- `"^2.0.0-beta.1"` matches `2.0.0-beta.1`, `2.0.0-beta.2`, `2.0.0`, `2.1.0`
  but NOT `1.9.0`.
- `"^2.0.0"` matches `2.0.0`, `2.1.0`, `2.99.99` but explicitly does **NOT**
  match `2.0.0-alpha.1`, `2.0.0-rc.5`, or any other pre-release.

Build metadata (`+foo`) SHALL be stripped for ordering purposes but retained
verbatim in the lockfile and resolver output.

### 26.3.4 Feature Unification

Feature sets are part of the **state space of the solver**, not a post-pass.
The resolver MUST compute feature activation and optional-dependency
inclusion simultaneously with version selection. In particular:

- A dependency declared with `optional = true` is treated as a feature (not
  a dependency) until explicitly enabled by a feature declaration, a
  downstream feature activation, or the `default` feature set.
- Feature unification across the graph is union-semantics per package
  version. If two dependents enable feature `F` and feature `G`
  respectively on the same chosen version of a package, the package is
  compiled with `F union G`.

### 26.3.5 Version Selection Policy

The default version-selection policy for ZOM v2 is **min-version-first**:
for every package, the resolver SHALL pick the *minimum* version that
satisfies all active constraints. This is the opposite of Cargo's default
"newest first" behaviour. The rationale is twofold:

1. **Prevent ecosystem rot.** Min-version selection guarantees that a
   library's declared `>= 1.2.3` lower bound is continuously exercised,
   catching spurious bumps to lower bounds early.
2. **Stable lockfiles.** New patch releases of transitive dependencies do
   not automatically shift the lockfile of every downstream project; users
   opt into updates via the `zom update` CLI command.

The policy is configurable per workspace via
`[workspace.metadata.zom] resolver-policy = "newest-first"` or per-invocation
via `zom build --resolver-policy newest-first`.

---

## 26.4 Lockfile Format (`Zom.lock`) -- TOML 1.0 Normative

The lockfile is always named `Zom.lock` and lives at the workspace root. It
is a TOML 1.0 document conforming exactly to the schema below.

```toml
version = 1

[[package]]
name = "serde"
version = "1.0.150"
source = "registry+https://index.zomlang.org/zom-index.git"
checksum = "sha256:abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890"
dependencies = [
    "derive_macro 1.0.0",
    "itoa 1.0.0",
    "ryu 1.0.5",
]

[[package]]
name = "serde_derive"
version = "1.0.150"
source = "git+https://github.com/zomlang/serde-derive.git?rev=abc1234abc1234abc1234abc1234abc1234abc12"
checksum = "git-sha1:abc1234abc1234abc1234abc1234abc1234abc12"
dependencies = []

[metadata]
"checksum serde 1.0.150"        = "sha256:abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890"
"checksum serde_derive 1.0.150" = "git-sha1:abc1234abc1234abc1234abc1234abc1234abc12"
```

### 26.4.1 Schema Rules

- `version` (integer, top-level). Always `1` for ZOM v1 / v2 era lockfiles.
  Future revisions of the lockfile format SHALL increment this value and
  SHALL NOT attempt to be backwards-compatible at the TOML level.
- `[[package]]` array. One entry per crate in the resolved closure, including
  workspace members. Ordering is normative: alphabetical by `name`, then
  ascending by `version`.
- `source` field. One of:
  - `registry+{index_url}` for registry sources.
  - `git+{url}?branch={name}` / `git+{url}?tag={tag}` / `git+{url}?rev={sha}`
    for git sources. The `rev` query, when present, SHALL be the full
    40-character hexadecimal commit SHA-1 (or equivalent for future git
    hash algorithms).
  - **Omitted entirely** for workspace members and `path` dependencies.
- `checksum`. Algorithm prefix (`sha256:`, `sha512:`, `git-sha1:`,
  `blake3:`) followed by lowercase hexadecimal. Required for every
  non-workspace package.
- `dependencies`. Array of strings in the format `"<name> <version>"`
  (single space, no quotes inside the value). Entries SHALL be sorted
  alphabetically. References to workspace members omit the version segment
  and use the bare crate name.
- `[metadata]` table. Contains a set of `"checksum <name> <version>"` keys
  whose values duplicate the per-package checksums. Redundancy is deliberate:
  it enables fast offline verification of a lockfile against a local cache
  without iterating `[[package]]` entries. Implementations MUST keep both
  representations in sync; a mismatched duplicate SHALL be surfaced as
  diagnostic **ZOM0898 ProvenanceMismatch**.

---

## 26.5 Download & Checksum Cache

### 26.5.1 Cache Location

The ZOM toolchain stores downloaded artifacts in `$ZOM_HOME/cache/`. Default
values are:

- Unix-family platforms (Linux, macOS, BSD): `$HOME/.cache/zom/`.
- Windows: `%APPDATA%\zom\cache\`.

The `$ZOM_HOME` environment variable, when set, overrides the defaults above.

### 26.5.2 Cache Layout

```
cache/
  archives/                  # downloaded .crate files, named <hex-checksum>.crate
  src/
    <registry-hash>/
      <name>-<version>/      # extracted sources per-version
  git/
    db/                      # bare cloned git repositories
    checkouts/               # temporary worktrees pinned at specific revisions
  registries/
    <index-url-hash>/
      cache/                 # HTTP response bodies, keyed by endpoint + ETag
      index/                 # sparse checkout of index.git
```

### 26.5.3 Checksum Verification

Before extraction of any downloaded `.crate` archive, the client MUST compute
the SHA-256 of the downloaded bytes and compare it, in constant time, with
the `cksum` field recorded in the resolved index entry. Any mismatch SHALL
produce a **ZOM0895 ChecksumMismatch** hard error. This error has no
command-line flag to override; it is a non-negotiable security boundary.

### 26.5.4 Cache Invalidation

Cache entries are **never evicted automatically**. Disk space is the user's
responsibility. Two CLI subcommands manage the cache:

- `zom cache clean` -- purges the entire cache directory.
- `zom cache clean --uninstalled` -- removes only those archives, sources,
  and git checkouts that are not referenced by any lockfile reachable from
  the current workspace.

---

## 26.6 Security: Provenance & Supply Chain

The following decisions are locked and normative for v2:

1. **Reproducible builds.** Invoking the compiler with
   `ZOM_FLAGS='--locked --reproducible' zom build` SHALL produce byte-identical
   output binaries for every input (same source tree, same lockfile, same
   CPU target) regardless of host operating system, host toolchain version,
   build-time environment variables outside the `ZOM_FLAGS` whitelist, or
   timestamp of invocation. Any variance SHALL terminate compilation with a
   hard error. v2 toolchains are REQUIRED to produce reproducible output in
   this mode; it is not optional.
2. **Provenance attestation.** `.crate` files MAY include a `provenance.json`
   file conforming to SLSA v1.0 attestation format. The attestation is
   carried in the index metadata under a reserved `provenance` key (not
   present in v2-baseline records). Verification is optional in v2; registry
   uploaders MAY reject submissions that lack attestations but individual
   zomc invocations SHALL NOT fail if the attestation is absent.
3. **Anti-squatting heuristic.** The registry REST surface SHALL return, in
   the per-crate summary (`GET /api/v1/crates/{name}`), a boolean field
   `suspicious_activity` that is set `true` when:
   - the crate's latest version is older than 180 days, AND
   - the seven-day rolling download count exceeds the 90-day trailing
     average by a factor of 10 or more.
   zomc SHALL surface a warning when it resolves a crate flagged this way;
   the warning is non-fatal and suppressed by `#![zom::allow(zom0899)]` for
   users who have audited the package.

### 26.6.1 Diagnostics Register (Registry & Security)

| Code | Mnemonic | Trigger |
|---|---|---|
| ZOM0895 | ChecksumMismatch | Downloaded archive's SHA-256 does not match the index record. Hard error, no override. |
| ZOM0896 | RegistryTLSFailure | HTTPS connection to the registry or index cannot validate the server certificate. Hard error. |
| ZOM0897 | PackageYanked | A crate version selected by a fresh resolve is yanked; surfaced only when `--locked` is absent. |
| ZOM0898 | ProvenanceMismatch | Duplicated lockfile metadata diverges from the per-package checksum, or SLSA attestation fails to verify. |

---

## 26.7 Alternate Registries (Per Crate)

Workspaces and individual crates MAY declare multiple additional registries
in their `Zom.toml`:

```toml
[registries.my-company]
index     = "https://git.company.com/zom-crates/index.git"
token-env = "ZOM_TOKEN_MYCOMPANY"
```

A dependency is then bound to a specific registry with the `registry` key in
its dependency declaration:

```toml
[dependencies]
foo = { version = "1.0", registry = "my-company" }
```

### 26.7.1 Cross-Registry Security Policy

Cross-registry imports are **disallowed by default**. A crate whose
`package.registry` field (implicitly or explicitly) names registry **A**
SHALL NOT depend, either directly or transitively, on a crate whose source
registry is **B** unless:

- `B` is listed in `A`'s `allowed-imports` whitelist, AND
- the reverse dependency is also explicitly permitted (bidirectional whitelist
  is required for transitive closure across the boundary).

The whitelist is declared in the manifest:

```toml
[registries.my-company]
index           = "https://git.company.com/zom-crates/index.git"
allowed-imports = ["crates-io"]

[registries.crates-io]
index = "https://index.zomlang.org/zom-index.git"
```

A resolver that violates the cross-registry boundary is non-conforming.

---

## 26.8 End-to-End Registry / Resolver / Lockfile Flow

The following flowchart locks the end-to-end sequence that a conforming `zom
build` (or `zom test`, `zom check`, etc.) invocation SHALL implement for any
crate with at least one external dependency.

```mermaid
flowchart LR
    SUB[zomc build subcommand] --> RESOL[PubGrub Resolver]
    RESOL --> |no Zom.lock present| IDX[Read index.git NDJSON records]
    RESOL --> |Zom.lock present|   LOCK[Verify consistency w/ lockfile]
    IDX   --> RESOL
    LOCK  --> RESOL
    RESOL --> |success| WRIT[Write or update Zom.lock]
    RESOL --> |conflict| PRINTER[Print PubGrub chain to stderr; exit 1]
    WRIT  --> DOWNLOAD[Per-version download; use cache if present]
    DOWNLOAD --> CACHE[cache/archives/<checksum>.crate]
    CACHE  --> UNPACK[tar xzf into cache/src/<name>-<version>]
    UNPACK --> COMPILE[Per-crate compilation in topological order]
```

Every step labelled above is normative in the sense that a compliant
implementation MUST produce observable behaviour equivalent to the sequence
depicted. Internal caching, speculative parallelism, and prefetch are
permitted behind the scenes, but the user-visible side effects -- lockfile
contents, cache directory layout, diagnostic ordering on failure, and
topological compilation order -- SHALL match the flowchart.
