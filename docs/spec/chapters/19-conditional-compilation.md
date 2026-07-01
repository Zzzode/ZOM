# Chapter 19 — Conditional Compilation: Cfg-Gated AST Stripping with Compile-Time Decidable Boolean Predicates

**Version:** 1.0.0-rc2
**Status:** Normative
**Canonical Name:** *Attribute-Gated, AST-Level Conditional Compilation with Compile-Time-Decidable Three-Valued Boolean Predicate System and Filesystem-Convention Gate Combinators*
**Short Name:** *ZOM Cfg-Gated AST Stripping System* (`cfg-stripping` for tooling)
**Cross-references:**
  - Chapter 16 (Attributes — Tier-0 `zom::cfg` schema)
  - Chapter 13 (Modules — dual filesystem convention with file suffix)
  - Chapter 21 (Manifest `[features]` section, PubGrub dependency resolver)
  - Chapter 20 (Edition & Stability — reserved `cfg`/`feature` contextual keywords across editions)
  - Chapter 17 (Grammar Reference — `CfgPredicate` EBNF, Section 17.19)

---

## 19.0 Purpose and Overview

The ZOM conditional-compilation system provides **three orthogonal,
conjunctively-combining gating mechanisms** that operate strictly *before*
semantic analysis. Any AST node whose composite cfg predicate evaluates to
*false* is **deleted from the AST as if it were commented out**; no type
inference, no name resolution, and no borrow/lifetime checking ever runs on
deleted nodes. This model is called **cfg-gated AST stripping**.

Mechanisms, in order of application (from coarsest to finest):

1. **(A) Filesystem convention gating** — Source files whose stem ends with a
   recognized underscore suffix (e.g. `tcp_linux.zom`, `net_unix.zom`,
   `gui_test.zom`, `foo_simd.zom`) are automatically gated by the implied
   predicate *before any item inside is parsed*.
2. **(B) Attribute-gated AST nodes** — The `#[zom::cfg(...)]` Tier-0 attribute
   applied to individual declarations, members, or statement-*block* statements
   gates that node and its subtree.
3. **(C) Feature flags via manifest** — Optional features declared in
   `Zom.toml` under the `[features]` table populate the `feature = "..."`
   cfg namespace and transitively enable/disable optional dependencies.

Pipeline overview:

```mermaid
flowchart LR
    A[Raw Source Tree + Manifest] --> B[Stage 1: Filename-suffix gating]
    B --> C[Stage 2: Feature-gated deps via PubGrub]
    C --> D[Stage 3: #[zom::cfg(...)] AST stripping]
    D --> E[Processed AST]
    E --> F[Semantic Analysis ∩ Name Resolution]

    style A fill:#e1f5fe,stroke:#01579b
    style E fill:#e8f5e9,stroke:#1b5e20
    style F fill:#fff3e0,stroke:#e65100
```

The three stages compose **conjunctively** (logical AND, narrower wins): a
node survives to semantic analysis only when every predicate applied to it
(file-level suffix, transitive feature enablement, per-node attribute)
evaluates to *true*.

### 19.0.1 Design Goals (Rationale)

| # | Goal | Why |
|---|---|---|
| G1 | **Compile-time decidable** — Every `CfgPredicate` reduces to a *total Boolean function* over the cfg environment in *O(n)* time, where *n* is the number of combinator nodes. | Guarantees `zomc` can never diverge or hang during cfg evaluation; a property C/C++ preprocessor, Zig `comptime`, and Jai `static if` intentionally give up. |
| G2 | **AST-level, not text-level** — Gating operates on fully-parsed AST subtrees; no half-expressions, no unmatched braces. | Eliminates the entire class of C-style `#if 0 { ...` text-level bugs (orphan braces, partial expressions). Matches Rust's `#[cfg]` model, improves on C/C++ preprocessor. |
| G3 | **Narrow-grained targets only** — `#[zom::cfg(...)]` is valid on declarations, members, and standalone *blocks*; explicitly **forbidden on expression subtrees**. | Forces cfg-noise to module/function/block boundaries, preserving local readability of expression code. A deliberate improvement over Rust's `cfg!(expr)` macro (which is widely regarded as an attractive nuisance). |
| G4 | **Predictable unknown-key semantics** — Referencing a key not in the environment produces *false*, never an error; but produces warning `ZOM1902`. | Cross-platform code does not explode when adding new tier-3 targets; CI can run with `-Werror=ZOM1902` to catch typos. |
| G5 | **Version-comparison predicates are first-class** — Key/value atoms support `<`, `<=`, `>=`, `>` comparisons with semver-like ordered strings; this is *not* left to build.rs-style hacks. | Fixes the oldest open Rust-lang/RFC tracker items (RFC issue #3537, "cfg version comparison") which the Rust project has deferred since 2017. |
| G6 | **Explicit check-cfg lint mode** — The compiler ships a `zomc --check-cfg` sub-mode which exhaustively checks cfg coverage against the declared target matrix. | Leads Rust (RFC 3013 "checked cfg" stabilized only in Rust 1.80, Q1 2025) and Zig (no comparable tool shipped as of 0.14). |

### 19.0.2 Non-Goals

1. **Not a general compile-time computation system.** No arithmetic, no
   function calls, no type queries in predicates. Those belong in a future
   *comptime* (edition-gated) subsystem under a separate spec chapter,
   not in the cfg system.
2. **Not a preprocessor.** No textual substitution, no token-pasting,
   no header files. Use `alias`, `const`, and `comptime` (when it ships)
   instead.
3. **Not edition-gating.** Edition selection (`edition = "2026"` in
   `Zom.toml`) modulates *which grammar rules* the parser accepts;
   cfg operates *after* parsing and cannot change the grammar.

---

## 19.1 Reserved Contextual Keywords

`cfg` and `feature` are **contextual keywords**. They are *not* hard keywords
of the lexical grammar — a program may freely bind variables, fields, or type
parameters named `cfg` or `feature`. However, when they appear inside the
attribute position of a `#[zom::cfg(...)]` or `#[zom::cfg(feature = "...")]`
construct, they are parsed with cfg-specific grammar rules (see 19.3.1).

Future language editions **must not** promote `cfg` or `feature` to hard
keywords, and **must not** re-use them as non-cfg attributes in the `zom::`
namespace, in order to preserve the parsing model described in this chapter.

---

## 19.2 CompilerOptions Input

Conditional compilation evaluates against three structured inputs carried on
the `CompilerOptions` record:

### 19.2.1 `target_triple: string`

Target triple string of the form `"arch-vendor-os-env"` (the fourth segment
may be absent on platforms that do not require it). The compiler parses the
triple at startup and materializes the primitive predefined cfg keys listed in
19.3.3. Example triples: `"aarch64-apple-darwin"`, `"x86_64-unknown-linux-gnu"`,
`"riscv64gc-unknown-none-elf"`, `"wasm32-unknown-wasi"`.

### 19.2.2 `enabled_features: Set<string>`

Populated from three sources, unioned:

1. The default feature set declared under `[features].default` in `Zom.toml`,
   minus any features excluded via `default-features = false` on a dependency
   edge (see Ch.21 §21.3).
2. Features explicitly enabled via the `--features=a,b,c` compiler/CLI flag.
3. Transitive features enabled as a side effect of feature resolution in the
   PubGrub dependency resolver (e.g. `foo = ["dep:bar", "baz/traits"]` enabling
   the `traits` feature of crate `baz`).

### 19.2.3 `cfg_overrides: Map<string, string>`

Populated from `-Z cfg-override=key=value` command-line flags. Intended for
testing and experimental use. Overrides take effect after predefined keys are
materialized and before any predicate evaluation. User code **shall not**
depend on specific overrides being present in stable toolchains.

---

## 19.3 Tier-0 Attribute `#[zom::cfg(predicate)]`

`#[zom::cfg]` is a Tier-0 attribute (Ch.16) accepted on any declarative or
block-level construct (see 19.6 for the exhaustive list).

### 19.3.1 Grammar (Normative EBNF — mirrored in ZomParser.g4 rule `cfgPredicate`)

The `CfgPredicate` grammar is intentionally restricted to a closed set of four
combinator forms plus one atom form with six comparison operators.
This restriction enables G1 (decidable), G4 (predictable unknowns), and
G6 (static check-cfg lints).

```
(* §19.3.1 — CfG PREDICATE GRAMMAR [normative]
   Mirror:   ZomParser.g4 rule `cfgPredicate`
   Lexical:  every identifier below is a plain Ch.2 Identifier;
             `=` `!=` `<` `<=` `>` `>=` are reused from the relational tier;
             string literals are Ch.2 DOUBLE_STRING_LITERAL;
             `all` `any` `not` inside a cfg(...) context are CONTEXTUAL —
             outside they remain plain identifiers (see §19.1 for cfg/feature).
*)

CfgPredicate        ::=  CfgAll | CfgAny | CfgNot | CfgAtom
CfgAll              ::=  'all' '(' ( CfgPredicateListTrailing )? ')'
CfgAny              ::=  'any' '(' ( CfgPredicateListTrailing )? ')'
CfgNot              ::=  'not' '(' CfgPredicate ')'
CfgPredicateListTrailing
                    ::=  CfgPredicate ( ',' CfgPredicate )* ','?

(* Single atomic predicate: check one key against the environment.
   CmpOp absent     → "bare key" existence check (§19.3.2 row 5-6)
   CmpOp =  '='     → exact value match, string comparison
   CmpOp = '!='     → negated exact value match
   CmpOp ∈ {<,<=,>=,>} → ordered comparison (see 19.3.1.1 ordering rules)
*)
CfgAtom             ::=  Identifier ( CfgOp CfgValue )?
CfgOp               ::=  '=' | '!=' | '<' | '<=' | '>' | '>='
CfgValue            ::=  DOUBLE_STRING_LITERAL
```

> **Note on integration with ZomParser.g4.** The grammar above is parsed
> *inside* the delimited argument list of `#[zom::cfg( ... )]`. The parser
> dispatches to the `cfgPredicate` sub-grammar when an outer `attribute`
> rule matches:
> ```
> attribute
>   : HASH LBRACK path=attributePath LPAREN cfgPredicate RPAREN RBRACK
>     { $path.getText().equals("zom::cfg") }?   // semantic predicate
>   ;
> ```
> Any `#[zom::cfg( malformed )]` input that does not match `cfgPredicate`
> produces `ZOM1900 CfgPredicateParseError` rather than a generic "no viable
> alternative" from the outer attribute rule.

#### 19.3.1.1 Ordered-Comparison Semantics (`<`, `<=`, `>=`, `>`)

Operators `<` `<=` `>=` `>` compare the stored environment value with the
RHS string according to the **ZOM SemVer-aware dotted-order comparison**
defined here. The algorithm is total for every UTF-8 string pair:

1. Split both strings into *segments* on `.` and `-` codepoints.
   Example: `"1.4.2-beta.3"` → `["1","4","2","beta","3"]`.
2. Walk segments pairwise, left to right:
   - If both segments parse as non-negative decimal integers: compare by
     integer value (`"10" > "2"`).
   - Otherwise: compare by UTF-8 byte-wise lexicographic order.
   - If equal, continue to next segment.
3. Shorter segment-list is *less than* longer if all common-prefix segments
   are equal (`"1.4" < "1.4.0"` is **false**; `"1.4"` **equals** `"1.4.0"` —
   trailing zero integer segments are implicitly padded; see note below).
4. If the environment key does *not* exist in the cfg environment → the
   predicate yields *false* (same rule as `=`), warning `ZOM1902`.

> **Zero-padding rule.** To prevent `"1.4"` vs `"1.4.0"` asymmetry, both
> sides are right-padded with implicit `"0"` integer segments up to the
> maximum length of the two before integer comparison begins. Non-integer
> segments are not padded.

Ordered comparisons are well-defined and total, satisfying properties:
- **Trichotomy.** For any `a, b` exactly one of `a<b`, `a=b`, `a>b` holds.
- **Transitivity.** `a<b ∧ b<c ⇒ a<c`.
- **Compatibility with equality.** `(a == b)` under this ordering iff
  the `=` operator yields *true*.

Use cases:
```zom
#[zom::cfg(zomc_version >= "1.4.0")]     // require at least zomc 1.4
#[zom::cfg(target_os_version < "14.0")]  // macOS < 14 fallback path
#[zom::cfg(android_api_level >= "33")]   // Android Tiramisu+
```

### 19.3.2 Complete Semantics Table (Three-Valued)

Predicate evaluation is a **total three-valued Boolean function**
{*true*, *false*, *unknown-key*} × severity level →
effective {*true*, *false*} + optional diagnostic emission:

| Form | Environment state | Effective value | Diagnostic |
|---|---|---|---|
| `all()` (zero args) | — | **TRUE** (tautology; default-on feature flag idiom) | — |
| `all(P1, P2, … Pn)`, n>0 | — | TRUE iff every Pi yields TRUE | any ZOM1902 from inner Pi bubbles |
| `any()` (zero args) | — | **FALSE** (tautological false) | — |
| `any(P1, P2, … Pn)`, n>0 | — | TRUE iff at least one Pi yields TRUE | any ZOM1902 from inner Pi bubbles |
| `not(P)` | — | Boolean negation of P's effective value | ZOM1902 from P bubbles |
| `key = "value"` (equality atom) | key present ∧ `env(key) == "value"` | TRUE | — |
| `key = "value"` | key present ∧ `env(key) != "value"` | FALSE | — |
| `key = "value"` | key absent | FALSE | **`ZOM1902 CfgUnknownKey`** (warning) |
| `key != "value"` | key present | TRUE when `=` gives FALSE, FALSE when `=` gives TRUE | — |
| `key != "value"` | key absent | FALSE | ZOM1902 |
| `key < / <= / >= / > "value"` | key present | Result of §19.3.1.1 ordering | — |
| `key < / <= / >= / > "value"` | key absent | FALSE | ZOM1902 |
| bare `key` (no CfgOp, no value) | key present (regardless of stored value) | TRUE | — |
| bare `key` | key absent | FALSE | ZOM1902 |

**Severity rules for unknown keys:**
- ZOM1902 is **warning-severity by default**.
- `zomc -Werror` or `zomc -Werror=ZOM1902` promotes it to hard error; this
  is the recommended CI configuration.
- ZOM1902 is **never** emitted for keys beginning with `_` (single leading
  underscore); these are *ad-hoc user overrides*, e.g. `my_feature_debug`.
- `feature = "foo"` uses a **separate diagnostic ZOM1903** (§19.4) with
  higher (error) severity, because the feature key *must* be declared in
  `[features]`.

### 19.3.3 Predefined Cfg Keys (Normative)

The following keys are **always materialized** by the compiler from the
target triple, compilation mode, and crate metadata. User code **must not**
rely on `cfg_overrides` to change these in stable code.

| Key | Value kind | Example(s) | Meaning |
|---|---|---|---|
| `target_arch` | valued | `"x86_64"`, `"aarch64"`, `"arm"`, `"riscv64"`, `"wasm32"` | CPU architecture segment of the triple |
| `target_vendor` | valued | `"apple"`, `"unknown"`, `"pc"` | Vendor segment |
| `target_os` | valued | `"macos"`, `"linux"`, `"windows"`, `"freebsd"`, `"wasi"`, `"none"` | Operating system; `"none"` for bare-metal |
| `target_os_version` | valued (ordered) | `"14.4"`, `"22.04"`, `"10"` | OS version string; useful for `<`, `>=` |
| `target_env` | valued | `""`, `"gnu"`, `"msvc"`, `"musl"` | ABI environment; empty string when unspecified |
| `target_family` | valued | `"unix"`, `"windows"`, `"wasm"` | Aggregate family key |
| `target_pointer_width` | valued (ordered) | `"16"`, `"32"`, `"64"` | Pointer bit-width |
| `target_endian` | valued | `"little"`, `"big"` | Byte order |
| `target_has_atomic` | **bare** | — | Set (existence-checked) iff the target supports atomic load/store for all of `{8, 16, 32, 64, ptr}` |
| `target_triple` | valued | `"aarch64-apple-darwin"` | Raw triple (for rare exact-match gates) |
| `zomc_version` | valued (ordered) | `"1.4.0"`, `"2026.11.0-nightly"` | Compiler version string, SemVer-comparable per §19.3.1.1 |
| `zomc_edition` | valued | `"2026"`, `"2029"` | Active edition (from `Zom.toml`); edition-gating *pre*-parsing is separate, this is the post-parsing accessible copy |
| `test` | **bare** | — | Set when compiled under `zom test` |
| `debug_assertions` | **bare** | — | Set in dev profile, unset in release profile |
| `proc_macro` | **bare** | — | Set when compiling a proc-macro crate-type |
| `panic` | valued | `"unwind"`, `"abort"` | Current crate's panic strategy |
| `miri` | **bare** | — | Set under `zom miri` (when shipped) |
| `sanitize` | valued | `"address"`, `"thread"`, `"memory"`, `"leak"` | Active sanitizer (if any) |
| `feature` | valued (multi-valued) | see §19.4 | Checked with `feature = "foo"` atoms; keys gated by ZOM1903 |
| `android_api_level` | valued (ordered) | `"33"`, `"21"` | Only defined on Android targets; useful for `>= "33"` checks |

---

## 19.4 Feature Flags via `[features]` in Zom.toml

Cross-reference: Ch.21 §21.3.

For every feature `foo` declared in the crate manifest under `[features]`, and
for every such feature present in `CompilerOptions.enabled_features` at
compile time, the compiler **shall** add the valued atom `feature = "foo"` to
the cfg environment. Predicates written `#[zom::cfg(feature = "foo")]` test
against this namespace.

Feature resolution occurs in the PubGrub resolver (Ch.21) **before**
compilation, so the final `enabled_features` set is stable by the time cfg
evaluation begins.

### 19.4.1 Feature List Syntax

Within the manifest `[features]` table, the right-hand side of a feature
declaration is a list of strings with three recognized forms:

1. `dep:name` — Enables optional dependency `name` as if it were a regular
   dependency, but only when the feature is enabled.
2. `name/subfeature` — Enables the feature `subfeature` in dependency `name`.
   `name` must be declared as a (possibly optional) dependency.
3. Plain identifier (not starting with `dep:` and not containing `/`) —
   Enables another feature of the *same* crate, supporting nested feature
   enablement such as `full = ["serde", "async", "gui"]`.

### 19.4.2 Cycles

A feature list that transitively references itself (cycle of length ≥ 1)
**must** produce `ZOM1904 FeatureCycle` and terminate compilation.

### 19.4.3 Strict Undeclared-Feature Check (ZOM1903)

A predicate `feature = "foo"` where `foo` is **not** declared in the
`[features]` table of the current crate **must** produce
`ZOM1903 FeatureUndeclared` at **error severity** (not warning, not silent
false). This catches typos like `#[zom::cfg(feature = "serde_json")]`
when the real feature is `"serde"`.

---

## 19.5 File-Name Suffix Convention

As a complementary mechanism to per-node `#[zom::cfg]` attributes, ZOM
recognizes a file-name suffix convention (**(A) Filesystem-convention
gating**) so that platform-specific source trees can be organized without
per-item attribute noise.

This mechanism is inspired by Go's `_linux.go`, `_windows.go` system, and
extends it with feature-name and `test`/`debug` suffixes which Go lacks.

### 19.5.1 Suffix Matching Rule (Ordered, First-Match)

Given a source file whose stem (without the `.zom` extension) is of the form
`base_SUFFIX` where `_` is a literal underscore:

The suffix `SUFFIX` is matched **in order** against the following categories;
the first match wins:

1. **Exact target_os match.** If `SUFFIX` equals a `target_os` predefined
   value (§19.3.3), the file is auto-gated with
   `target_os = "<SUFFIX>"`.
2. **Exact target_family match.** If `SUFFIX ∈ {"unix", "windows", "wasm"}`,
   the file is auto-gated with `target_family = "<SUFFIX>"`.
3. **Exact target_vendor match.** If `SUFFIX ∈ {"apple", "unknown", "pc"}`,
   auto-gated with `target_vendor = "<SUFFIX>"`.
4. **Reserved suffixes `test` / `debug`.** Auto-gated with the bare atoms
   `test` / `debug_assertions` respectively.
5. **Feature-name match.** If `SUFFIX` exactly matches a feature name
   declared in `[features]` of the current crate, auto-gated with
   `feature = "<SUFFIX>"`.
6. **Otherwise.** No gating. The compiler **must not** warn. Unknown
   underscore suffixes (e.g. `foo_v2.zom`) are treated as ordinary stems.

Examples (cross-checked against §19.3.3's value set):

| File name | Implied gate | Match rule # |
|---|---|---|
| `net/tcp_linux.zom` | `target_os = "linux"` | 1 |
| `net/tcp_macos.zom` | `target_os = "macos"` | 1 |
| `net/unix_socket_unix.zom` | `target_family = "unix"` | 2 |
| `gui/darwin_utils_apple.zom` | `target_vendor = "apple"` | 3 |
| `util/foo_test.zom` | bare atom `test` | 4 |
| `util/foo_debug.zom` | bare atom `debug_assertions` | 4 |
| `render/simd_pass_simd.zom` | `feature = "simd"` (if declared in `[features]`) | 5 |
| `data/random_xyzzy.zom` | no gating (unknown suffix) | 6 |

### 19.5.2 Composition with Explicit Attributes (Conjunctive AND)

If a suffix-gated file also contains explicit `#[zom::cfg(...)]` attributes on
its items, the effective predicate for each item is the logical AND of the
file-level suffix gate and the item-level attribute gate (narrower wins).

Nested `mod foo;` declarations inside a suffix-gated file are also ANDed with
the parent's suffix gate, transitively.

### 19.5.3 Ambiguity Detection (ZOM0888 FileSuffixAmbiguous)

If two distinct suffix categories could match, the rule #1–#6 *first-match*
priority resolves it deterministically. However, if the stem's last two
underscore-separated segments **each** independently match a **different**
recognized gate category (e.g. `net_unix_apple.zom` where segment `unix`
matches rule #2 *and* segment `apple` matches rule #3), the compiler
**must** emit `ZOM0888 FileSuffixAmbiguous` with a hint to rename the file or
use explicit `#[zom::cfg(...)]` attributes instead.

---

## 19.6 Gated Semantics (Target Surface)

### 19.6.1 Valid Gate Targets (Allowlist)

`#[zom::cfg(...)]` is legal on the following constructs:

1. **Top-level declarations.** `class`, `struct`, `enum`, `interface`,
   `error`, `function` (including `fun`, standalone `method`), `const`,
   `static`, `mod`, `import`, `export`, `impl`, `alias`, `marker`.
2. **Class / interface / struct / enum members.** Methods, fields,
   associated types, enum variants.
3. **Statement-block statements.** A statement whose form is exactly
   `#[zom::cfg(...)] { stmt* }` where the braced construct is a standalone
   statement in its enclosing block. If the predicate is false the compiler
   replaces the block with a `NoOp` AST node (no runtime trace).

### 19.6.2 Forbidden Gate Targets (Denylist — ZOM1901 CfgOnExpression)

`#[zom::cfg(...)]` is **not supported** on:
- expression subtrees of any kind (primary, unary, binary, postfix,
  call arguments, array/struct literal elements, return expressions);
- statement forms that are *not* a standalone block — i.e. you cannot
  cfg-gate a single `let x = 5;` statement without wrapping it in `{ ... }`.

Applying the attribute to any construct in the denylist **must** produce
`ZOM1901 CfgOnExpression` at error severity.

> **Rationale (G3, narrow-grained targets).** Forbidding expression-level cfg
> forces conditional logic to be expressed with ordinary `if`/`match` at the
> language level, which is readable, type-checked, and debuggable. Cfg is
> meant for *compile-time target/platform selection*, not for runtime-value
> selection. This is the single biggest readability win ZOM cfg has over
> Rust `cfg!` / C `#if` / Zig `if (@import("builtin")....)`.

### 19.6.3 Cross-Gating Behavior

If a declaration `D` is removed by cfg evaluation, it is truly absent from the
processed AST. Any reference to a name introduced by `D` from outside `D`'s
gate therefore produces the standard "undeclared identifier" / "undeclared
type" diagnostic path. No cfg-specific "you may have meant to reference a
gated item" diagnostic is required (though `zomc --check-cfg` may surface
related lints; see §19.7).

### 19.6.4 Exports

Gating an `export` declaration — or gating the item that an `export`
re-exports — removes the exported symbol from downstream crates' visible
namespaces exactly as if the item had never existed.

### 19.6.5 Multiple `#[zom::cfg(...)]` Attributes on One Node

If a node carries **multiple** `#[zom::cfg(...)]` attributes (possibly
interleaved with other attributes), the effective predicate is the logical
AND of all cfg predicates on that node. Recommendation: users should prefer
single `#[zom::cfg(all(P1, P2, …))]` form for readability; the AND-composition
rule exists so that procedural macro expansions can stack cfg gates without
needing to merge predicate ASTs.

---

## 19.7 Cfg Lint Mode (`--check-cfg`) — G6 Explicit Checkability

This section is **normative for all tier-1 targets** and **recommended for
all tiers**. It defines `zomc --check-cfg`, a compilation sub-mode that
statically verifies the cfg predicate space against the crate's supported
target matrix without running code generation or semantic analysis.

### 19.7.1 Invocation

```bash
zomc --check-cfg [--targets TARGETS_FILE] path/to/Zom.toml
```

Where `--targets` points to a TOML file listing the tier-1 and tier-2 target
triples the project explicitly supports. If `--targets` is omitted, the
compiler uses the platform-default single target (the host).

### 19.7.2 Lint Set (Normative)

All lints below are **allow-by-default** unless the crate manifest sets
`[profile.check] cfg-warnings = "deny"`.

| Code | Mnemonic | Severity | Meaning |
|---|---|---|---|
| `ZOM1905` | `CfgAlwaysFalse` | warn | A `#[zom::cfg(...)]` gate's predicate evaluates to *false* on **every** supported target (user probably wrote inverted logic or typo) |
| `ZOM1906` | `CfgAlwaysTrue` | allow-by-default | A gate evaluates to *true* on every supported target (gate is a no-op; useful for dead-code detection when combined with `-W CfgAlwaysTrue`) |
| `ZOM1907` | `CfgMatchArmsIncomplete` | warn | A `match` on a `#[repr(C)]` enum has arms gated by disjoint predicates that fail to cover every target (one or more supported triples have no matching arm reachable) |
| `ZOM1908` | `CfgExportsUnstable` | warn | An `export` declaration is cfg-gated by a predicate that differs across tier-1 targets; downstream crates will observe an inconsistent public API |
| `ZOM1909` | `CfgPublicTypeMissing` | warn | A public type `T` in a `pub(crate)`-super visibility context is cfg-gated, but another public (ungated) declaration `U` references `T` by name — on targets where `T` is stripped, `U` fails to compile |
| `ZOM1910` | `CfgFileSuffixUnmatched` | allow | A source file stem ends with `_FOO` where `FOO` failed every rule in §19.5.1; intended to catch typos like `tcp_linuxx.zom` (silent-no-op today becomes visible under check-cfg) |

### 19.7.3 Implementation Notes (Informative)

Because `CfgPredicate` is intentionally decidable and finite (G1), every
predicate in the crate can be evaluated in the Cartesian product of
`N_targets × features_boolean_lattice` with time complexity
`O(N_predicates × N_operands × N_targets × 2^N_nominal_features)`. For
realistic crate sizes (< 5000 cfg gates, < 32 named features, < 24 supported
targets), this is a few milliseconds of CPU.

---

## 19.8 Design Rationale & Industry Comparison (Informative)

This section is **informative**. It records why specific decisions were made
relative to the state of the art in other languages as of 2026.

### 19.8.1 vs C/C++ Preprocessor (`#if`, `#ifdef`)

C's preprocessor does **text-level** conditional substitution. Half-open
`#if 0 { /* unbalanced */` produces undecipherable cascaded diagnostics.
ZOM AST-stripping eliminates the entire bug class by requiring well-formed
input *before* stripping begins.

C also lacks any form of `--check-cfg`; the only way to validate a codebase
across targets is to compile it N times, which is O(N) expensive. ZOM does
it in O(1) pass (§19.7).

### 19.8.2 vs Rust `#[cfg]` + `cfg!(...)` + Cargo `[features]`

Rust's model is the closest to ZOM's and was the starting point; the
improvements are fourfold:

| Dimension | Rust (2024 edition) | ZOM §19 |
|---|---|---|
| Filesystem-level gating | ❌ requires per-mod `#[cfg(...)]` boilerplate | ✅ §19.5 suffix convention (Go-inspired) |
| Ordered version comparison (`>= "1.4"`) | ❌ deferred since 2017 (RFC issue #3537) | ✅ §19.3.1.1 SemVer-aware ordering |
| `--check-cfg` lint mode | ❌ partially shipped (RFC 3013) in Rust 1.80 (Q1 2025), no `CfgMatchArmsIncomplete` / `CfgExportsUnstable` | ✅ §19.7 six-lint normative set |
| Expression-level cfg (`cfg!(...)` macro) | ✅ widely used, widely lamented as unreadable | ❌ **deliberately forbidden** — §19.6.2 ZOM1901 |
| Cycle detection in `[features]` | ✅ (Cargo) | ✅ §19.4.2 ZOM1904 |
| Typo of feature name | silent false (until runtime missing feature) | **error** §19.4.3 ZOM1903 |
| Multiple cfg attrs stack | AND ✓ | AND ✓ (§19.6.5) |

### 19.8.3 vs Zig `@import("builtin")` + `if (comptime_bool)`

Zig's compile-time computation is strictly more expressive than ZOM cfg
predicates — arbitrary Zig code (including function calls) can run at
compile time inside `if` expressions. This breaks G1 (decidability) and
enables bugs where `comptime` accidentally loops or performs IO.

ZOM's approach is a deliberate two-layer design:
- **Layer 1 (this chapter)** — decidable cfg-stripping for *platform &
  feature gating* (the 99% use case);
- **Layer 2 (future §27 comptime, edition-gated)** — full compile-time
  computation for the 1% cases that genuinely need it.

This separation enables the §19.7 lint engine, which is impossible for a
Turing-complete `comptime` system.

### 19.8.4 vs Go Build Tags (`//go:build ...`)

Go's tag model is line-based (`//go:build linux && amd64`) and supports
file-level only; item-level gating requires source-level refactoring.
ZOM's attribute-on-declaration form combines both: file-level (suffix) and
item-level (attribute), with a richer predicate language (ordered
comparisons). Go tags also lack any lint/checker equivalent to
`--check-cfg`.

### 19.8.5 vs Jai `#run` / `static if`

Jai's compile-time execution is intentionally unconstrained — you can
invoke the compiler itself from `#run` blocks. This makes cfg-like gating
trivial but removes every static guarantee; the decidability + linting
properties (G1, G6) are sacrificed. ZOM treats this as a separate
feature class (§27 comptime) from cfg.

---

## 19.9 Diagnostic Code Registry (Normative)

This chapter allocates the following diagnostic codes in the `19xx` family
(and one cross-range code reused from the `08xx` module-surface range for
the related filesystem convention). Full registry rows with severity,
message templates, structured fields, and fix-it edits are provided by the
central diagnostic-registry spec.

| Code | Mnemonic | Severity | Trigger |
|---|---|---|---|
| `ZOM1900` | `CfgPredicateParseError` | error | Malformed cfg predicate inside `#[zom::cfg(...)]`; grammar does not match §19.3.1 |
| `ZOM1901` | `CfgOnExpression` | error | `#[zom::cfg(...)]` applied to an expression, non-block statement, or other denylist target (§19.6.2) |
| `ZOM1902` | `CfgUnknownKey` | warn (promotable to error) | Cfg predicate references a key that is neither predefined (§19.3.3) nor present in `enabled_features` or `cfg_overrides` |
| `ZOM1903` | `FeatureUndeclared` | error | `feature = "foo"` atom where `foo` is not declared in `[features]` (§19.4.3) |
| `ZOM1904` | `FeatureCycle` | error | Transitive cycle detected in `[features]` enablement graph (§19.4.2) |
| `ZOM1905` | `CfgAlwaysFalse` | warn | `--check-cfg` lint: predicate always false on supported targets (§19.7.2) |
| `ZOM1906` | `CfgAlwaysTrue` | allow | `--check-cfg` lint: predicate always true on supported targets (§19.7.2) |
| `ZOM1907` | `CfgMatchArmsIncomplete` | warn | `--check-cfg` lint: cfg-gated match arms do not cover all supported targets (§19.7.2) |
| `ZOM1908` | `CfgExportsUnstable` | warn | `--check-cfg` lint: public exports differ across tier-1 targets (§19.7.2) |
| `ZOM1909` | `CfgPublicTypeMissing` | warn | `--check-cfg` lint: gated public type referenced by ungated public declaration (§19.7.2) |
| `ZOM1910` | `CfgFileSuffixUnmatched` | allow | `--check-cfg` lint: underscore-stem suffix failed every §19.5.1 rule (§19.7.2) |
| `ZOM0888` | `FileSuffixAmbiguous` | error | Source stem's trailing segments match two distinct gate categories (§19.5.3) |

---

## 19.10 Example — Platform-Specific Network Implementation

```zom
// net/mod.zom — exports unified API; mod tcp; resolved per §19.5
mod tcp;

export interface Socket {
    fun connect(addr: str) -> unit raises NetError;
    fun send(buf: [u8]) -> u64 raises NetError;
}

export class NetError : Error {}
```

```zom
// net/tcp_linux.zom — compiled only on target_os = "linux" (suffix rule #1)
use libc;

struct LinuxTcpSocket {
    fd: i32,
}

impl Socket for LinuxTcpSocket {
    fun connect(addr: str) -> unit raises NetError { /* ... */ }
    fun send(buf: [u8]) -> u64 raises NetError { /* ... */ }
}
```

```zom
// net/tcp_macos.zom — compiled only on target_os = "macos" (suffix rule #1)
use darwin_libc;

struct MacosTcpSocket {
    sock: DarwinSocket,
}

impl Socket for MacosTcpSocket {
    fun connect(addr: str) -> unit raises NetError { /* ... */ }
    fun send(buf: [u8]) -> u64 raises NetError { /* ... */ }
}
```

```zom
// gui/button.zom — item-level gating; zomc_version >= 1.4 requirement
#[zom::cfg(all(feature = "gui", zomc_version >= "1.4.0"))]
export class Button {
    x: i32;
    y: i32;
    mut label_text: str;

    fun label() -> str { return self.label_text; }

    #[zom::cfg(target_os = "windows")]
    fun win32Refresh() { /* Win32-specific repaint */ }

    #[zom::cfg(any(target_os = "macos", target_os = "ios"))]
    fun cocoaRefresh() { /* Apple-specific repaint */ }
}
```

---

## 19.11 Example — `all` / `any` / `not` / Ordered Comparison Combinations

```zom
// Compiled only on 64-bit unix targets that provide atomic instructions
#[zom::cfg(all(
    target_pointer_width = "64",
    target_family = "unix",
    target_has_atomic,
))]
struct AtomicPtr<T> {
    inner: u64,
}

// Compiled anywhere EXCEPT wasi bare-wasm environments
#[zom::cfg(not(target_os = "wasi"))]
use std.fs;

// Compiled when feature "std" is enabled OR on target_family = "unix"
#[zom::cfg(any(feature = "std", target_family = "unix"))]
fun platformSleepMs(ms: u32) { /* ... */ }

// Tautologically enabled (zero-argument all = true); useful for
// feature declarations that default to enabled in the manifest.
#[zom::cfg(all())]
const ALWAYS_PRESENT: i32 = 1;

// Android API-level gating — ordered comparison, direct fix to Rust RFC#3537
#[zom::cfg(all(target_os = "android", android_api_level >= "33"))]
fun useNewPhotoPicker() { /* Android 13+ only */ }

// Compiler minimum-version gate: use a newer feature but keep old fallback
#[zom::cfg(zomc_version >= "1.5.0")]
fun useNewTypeLayout() { /* leverages a zomc 1.5+ layout optimization */ }

#[zom::cfg(zomc_version < "1.5.0")]
fun useNewTypeLayout() { /* compatibility shim; identical ABI */ }
```

---

## 19.12 EBNF Consolidation for Grammar Reference (Informative)

This section reproduces the grammar fragments defined in §19.3.1 in a form
suitable for direct cross-reference with Ch.17 (Grammar Reference) and with
the `ZomParser.g4` / `ZomLexer.g4` source files. It is informative; the
normative grammar is the union of §19.3.1 with Ch.17's `attribute` rule.

```
(* Integration point with Ch.17 attribute rule *)
attribute           ::=  '#' '[' attributePath '(' cfgPredicate ')' ']'        (* when path == "zom::cfg" *)
                     |   (* ... all other attribute forms from Ch.16/17 ... *)

(* §19.3.1 normative grammar, repeated for convenience *)
cfgPredicate        ::=  cfgAll | cfgAny | cfgNot | cfgAtom
cfgAll              ::=  'all' '(' ( cfgPredicate ( ',' cfgPredicate )* ','? )? ')'
cfgAny              ::=  'any' '(' ( cfgPredicate ( ',' cfgPredicate )* ','? )? ')'
cfgNot              ::=  'not' '(' cfgPredicate ')'
cfgAtom             ::=  IDENTIFIER ( cfgOp CFG_VALUE )?
cfgOp               ::=  '=' | '!=' | '<' | '<=' | '>' | '>='
CFG_VALUE           ::=  DOUBLE_STRING_LITERAL    (* lexical reuse from Ch.2 *)

(* Contextual keyword resolution (§19.1): inside the 'zom::cfg(...)'
   delimited scope only, identifiers 'all' 'any' 'not' are recognized as
   combinators; 'cfg' and 'feature' as predefined keys. Outside this scope
   they remain ordinary identifiers. This is NOT implemented by adding
   lexer tokens; it is purely a parser-level disambiguation within the
   cfgPredicate sub-grammar. *)
```

### 19.12.1 Semantic Predicates Required in ZomParser.g4

Two semantic predicates are **required** to implement this chapter
correctly:

1. **P1: `#[zom::cfg(...)]` dispatch.** In the outer `attribute` rule, when
   the adjacent `HASH LBRACK path LPAREN arg RPAREN RBRACK` form is matched, a
   semantic predicate asserts that
   `$path.getText().equals("zom::cfg")` before delegating the arg-list
   parse to `cfgPredicate` rather than the generic `attrArgList` sub-grammar.
   Failure → fall through to generic attribute form, never silently accept a
   malformed cfg.

2. **P2: ZOM1901 `CfgOnExpression`.** A predicate on every attribute target
   site (declarations, class/struct/interface/enum members, block-statement
   form) asserts that when an attribute list carries any `zom::cfg`, the
   containing construct is one of the allowlisted forms (§19.6.1). Failure
   → `ZOM1901`.

These two predicates (along with the 11 diagnostics in §19.9) are the
*minimum* parser/semantic-pass surface required to fully implement this
chapter.

---

## 19.13 Implementation Notes — ANTLR 4 Tail-Action Safety Pattern (Informative)

> This section is an implementation-level note for the reference grammar in
> `docs/spec/ZomParser.g4`. It documents a novel, general-purpose pattern
> discovered while integrating ZOM1900 / ZOM1903 diagnostics into the
> `attrItem : attrZomCfg` alternative. The pattern is reusable across every
> ANTLR 4 grammar that must emit hard (rc=2) parser diagnostics from inside
> a semantic-predicate-disambiguated rule.

### 19.13.1 The Simulator Poisoning Problem

ANTLR 4 uses an **ALL(\*) Simulator** during DFA construction. For every
alternative in a decision, the simulator walks every reachable ATN path
*ahead* of actual input consumption to merge equivalent states into a
deterministic DFA. A crucial, almost entirely undocumented detail: if a
parser action `{ ... }` anywhere on a reachable path throws an uncaught
`RuntimeException` (such as ANTLR 4's own `ParseCancellationException`,
hereafter **PCE**), the simulator marks the entire alternative as
*exception-terminated* and **poisons** the corresponding DFA state — even
when a gated semantic predicate `{p}?` semantically *selected* that
alternative on the actual input path. The result is a spurious
`NoViableAltException` (NVA) for perfectly valid source code.

Concretely, the naive form below **never works**:

```
// BROKEN: { throw PCE } is on the prediction path for alt1.
// The simulator poisons alt1; gated predicate {isZomCfg}?
// never rescues it at runtime → NVA on any valid #[zom::cfg(...)].
attrItem
    : {isZomCfg()}?  IDENTIFIER LPAREN cfgPredicate { throw PCE; } RPAREN
    | ...
    ;
```

### 19.13.2 Tail-Action Placement — ATN-Level Diagram

The **Tail-Action Safety Pattern** relocates the throwing action to the
position *after* the very last terminal token of the alternative. At that
point the simulator has already committed to the alternative (there are no
more decision points), so the action sits on a post-decision ε-edge that
the predictor treats as benign. The diagnostics still fire with 100%
fidelity and carry exact source positions (because every preceding
terminal has already been matched, giving the action access to a fully
populated `$ctx` with correct `start` / `stop` / line / column).

```mermaid
graph LR
    Start -->|"DFA prediction (simulator visible)"| P1[Decision]
    P1 -->|"T1 T2 ... Tn (terminals)"| Last[Last terminal]
    Last -->|"epsilon (simulator hidden)"| Tail["{ throw PCE } (Tail Action)"]
    style Tail fill:#9f9,stroke:#080
```

### 19.13.3 Placement Comparison Matrix

| Action placement | Simulator visible? | DFA poisoned? | Diagnostic fires? |
|---|---|---|---|
| Before a gated predicate, or anywhere before a decision point | YES | ❌ entire alt killed | **Never** (falls through to NVA) |
| Between terminals, mid-alternative | YES | ❌ alt killed | **Never** (NVA raised first) |
| **After the last terminal (Tail)** | **NO** | ✅ safe | **100%** with exact position |

### 19.13.4 Concrete Adoption in `attrItem : attrZomCfg`

The reference grammar places `enforceCfgAtomQuotedRhs(...)` immediately
after the final `RPAREN` terminal of the `attrZomCfg` alternative. The
Java helper walks the fully-parsed `cfgPredicate` subtree and enforces
two syntactic invariants that the pure grammar cannot express without
exploding ambiguity:

- **ZOM1900 `CfgAtomRhsMustBeDoubleQuoted`** — every `cfgAtom` whose
  child-rule kind is `valuedCfgAtomRhs` must carry a
  `DOUBLE_STRING_LITERAL` on its RHS; bare identifiers or unquoted
  tokens are rejected.
- **ZOM1903 `CfgFeatureValueNotEmpty`** — a `cfgAtom` whose identifier
  is literally `feature` and whose RHS parses to the empty string
  literal `""` is rejected (features must have non-empty names).

Illustrative helper (Java; full implementation lives in
`ZomParserBaseListener` / the helper class referenced from the `.g4`
actions block):

```java
void enforceCfgAtomQuotedRhs(ParserRuleContext ctx, Parser recognizer) {
    for (CfgAtomContext atom : findAll(ctx, CfgAtomContext.class)) {
        if (atom.valuedCfgAtomRhs() != null) {
            TerminalNode str = atom.valuedCfgAtomRhs().DOUBLE_STRING_LITERAL();
            if (str == null)
                throw new ParseCancellationException(
                    new ZOM1900(atom.start, atom.IDENTIFIER().getText()));
            String raw = str.getText(); // includes surrounding "..."
            if (raw.length() == 2)       // ""
                throw new ParseCancellationException(
                    new ZOM1903(atom.start, atom.IDENTIFIER().getText()));
        } else if (atom.bareCfgAtomRhs() != null) {
            // bare key = "present?" test — always legal, nothing to enforce
        } else if (atom.badRhsCfgAtomRhs() != null) {
            throw new ParseCancellationException(
                new ZOM1900(atom.start, atom.IDENTIFIER().getText()));
        }
    }
}
```

### 19.13.5 Cross-Reference

This pattern is normative for all `.g4` edits under the ZOM project and
is formally registered as:

> **`ZOM-G4-PATTERN-001: Tail-Parser-Action Safety Pattern`**
>
> Cross references:
> - `AGENTS.md § ANTLR 4 .g4 Authoring Rules (ZOM-G4-PATTERN-001 ~ 003)`
> - `products/zomlang/tests/conformance/runners/grammar/README.md § Semantic Predicate Matrix`
