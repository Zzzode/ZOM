# RFC 0055 Review And Implementation Tracker

## Discussion Record

RFC 0055 was prompted by a language-design question about whether an
initializer-less `mut` binding can be assigned later and whether `let`
supports a one-shot late initialization. Investigation established:

- Chapter 6 (`06-declarations.md`) already states the direction in the
  `VariableDecl` EBNF comment: `mut` without an initializer requires a type
  annotation; `let` without an initializer is accepted only where definite
  assignment proves exactly one write before the first read.
- Chapter 14 already states the memory rule: reading/borrowing/moving an
  uninitialized place is an error (ZOM4093/ZOM4094), and complete-place
  assignment reinitializes a moved-from place.
- The production gap is in admission and lowering, not language intent:
  `let v: T; v = 1; return v;` drains as ZOM4099, and
  `mut v: T; v = 1; return v;` passes `--check` but has no HIR/MIR lowering
  strategy.

The RFC therefore does not invent a new language direction; it promotes the
one-sentence grammar intent into a reference-level contract: a two-level
init lattice over the reducible CFG, path-join intersection, a per-path
single-assignment rule for `let` (second write drains to ZOM4024),
`Initialize`/`Overwrite` classification across HIR and Built MIR, and
end-to-end lowering on the RFC 0048 recursive MIR write family. The initial
admitted slice is straight-line `let`/`mut` and the four-block `let`
diamond with scalar leaves; loop-carried and partial-aggregate
initialization are explicit non-goals.

## Decision Record

TBD - pending all required owner approvals.

## Owner Review Matrix

| Owner | Surface | Round 1 |
|---|---|---|
| `rfc` | Process, template conformance, required-owner completeness, RFC granularity | APPROVE (Round 1, 2026-10-17; two non-blocking editorial nits) |
| `spec-audit` | Chapter 6/14 normative prose, five-way spec alignment, drift posture | APPROVED (Round 1, 2026-10-17; six non-blocking spec-edit precision notes) |
| `binder-checker` | LocalWrite admission, init lattice, Initialize/Overwrite classification, ZOM4024 drain | Pending |
| `ir-backend` | HIR late-init arm, recursive MIR write family, structural verifier, codec stability | Pending |
| `error-system` | ZOM4024/ZOM4093/ZOM4094 diagnostic contracts, projector/stage mapping, no parallel code | Pending |
| `verification` | Conformance grammar/AST/diagnostics coverage, parity gate, mutation tests | Pending |

### 2026-10-17 Six-Owner Review Round 1 - Opened

Review dispatched to all six required owners. Findings and resolutions are
recorded below as the owner agents report.

#### rfc owner, Round 1: APPROVE (2026-10-17)

- `python3 scripts/check-rfc.py` has no findings for RFC 0055. The only
  script failures are pre-existing and out of scope: RFC 0048 frontmatter
  date ordering and RFC 0054 required-owners mismatch.
- Frontmatter: `required-owners` exactly equals the unique owner set in the
  Repository Impact table; all six ids exist in `.codex/subagents/
  manifest.yaml`; `discussion` and `tracking-issue` point to anchors that
  exist in this file; `type`/`area = language` are valid for a
  user-visible semantic and diagnostic change; the same-day DRAFT -> REVIEW
  transition is legal and recorded; the RFC index row is synced.
- All nineteen required template sections are present in template order;
  goals and non-goals are concrete and scope-controlling.
- Granularity decision (separate RFC, not drift repair under RFC 0048) is
  correct. Independently verified `docs/spec/chapters/06-declarations.md`:
  lines 97-100 are solely an EBNF admission-intent comment, lines 136-143
  a `mut` example, line 1228 a one-line mention. None define the
  path-join/meet lattice, the second-`let`-write ZOM4024 channel, or the
  `Initialize`/`Overwrite` HIR/Built MIR contract; Chapter 14 lines 70-72
  cover only uninitialized reads and move reinitialization. The
  "intended contract already written elsewhere" drift-repair exemption
  therefore does not apply, and RFC 0052's language-semantics,
  diagnostic-meaning, and cross-two-owner matrix rows independently
  require the full path. The RFC 0048 dependency is correctly modeled via
  `requires: [7, 48]` rather than folded scope.
- Prior art cites four mature families (Rust, Java/Kotlin/C#, Swift, plus
  Chapter 14) with explicit copy-vs-avoid rationale; the hard-error
  posture on a second `let` write is justified against Rust.
- Referenced contracts verified in tree: `HirLocalWriteKind`
  (`compiler/hir/hir-module.h:215`, `Initialize = 0x01`,
  `Overwrite = 0x02`), `MirInitializationKind`
  (`compiler/mir/built-mir.h:402`), `isSimpleLocalWrite` /
  `isMutableOwnerLocal` (`compiler/checker/body/body-checker.cc`), and
  ZOM4024/ZOM4087/ZOM4093/ZOM4094/ZOM4099 in
  `compiler/diagnostics/defs/diagnostics-checker.def`.
- No forward-compatibility shim, dual code path, or internal revision
  suffix; the proposal reuses existing IR kinds and the existing RFC 0048
  fallback, and non-admitted shapes keep the existing fail-closed ZOM4099
  behavior. `check-english-only.py --check` passes.
- Non-blocking editorial nits: (1) the Repository Impact design-note row
  names `rfc` for `docs/design/language/definite-assignment.md`, while
  `manifest.yaml` assigns `docs/design/**` to `spec-audit`, which is
  already a required owner, so coverage is complete; relabel the row to
  `spec-audit` (or joint) at the author's discretion. (2) Two em dashes
  appear in the RFC and this tracker; ASCII hyphens match repository
  typographic discipline, though the English-only gate itself passes.

#### spec-audit owner, Round 1: APPROVE (2026-10-17)

- Verified every checkable claim against Chapters 5/6/14/17, both ANTLR
  grammars, the diagnostic catalog, and the ownership init rail, and ran
  the five motivating shapes through the sanitizer build.
- Chapter 6 lines 97-100 are confirmed to be only an intent comment with no
  path-join model, diagnostic, or IR contract; the separate-RFC call holds.
- The proposed two-level lattice matches Chapter 14 lines 70-72 and the
  ownership intersection implementation (`ownership/facts/init.h:70`,
  `init.cc:1267`); no contradiction.
- No grammar change: `ZomParser.g4:982` already makes the initializer
  optional for both keywords; five-way alignment passes as written.
- Non-goals (partial field init, loop-carried init, module scope, const)
  are correctly bounded.
- Six non-blocking precision notes for the follow-on normative spec edit
  were folded into the RFC's Documentation And Teaching Plan: carve out
  the single-initializing write from the Chapter 6 line-113 reassignment
  prohibition; require a type annotation for initializer-less `let` and
  `mut`; cross-reference the subsection from Chapter 17; scope the
  Initialize/Overwrite transfer to body-local scalar bindings; add
  ZOM4093/ZOM4094 to the Chapter 14 diagnostic-family list; write the
  rule normatively.

## Implementation Tracker

Not started. Implementation begins only after the RFC moves to ACCEPTED.
