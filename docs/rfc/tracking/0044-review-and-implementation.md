# RFC 0044 Review And Implementation Tracker

## Discussion Record

### 2026-08-28 Doc-IR Engine Authored And DRAFT -> REVIEW

RFC 0044 (Source Formatter Architecture) was assessed for a `DRAFT -> REVIEW`
transition against the RFC process gates in `docs/rfc/README.md` and the
review-readiness bar in `.codex/skills/rfc/SKILL.md`, using the same procedure
that moved RFC 0021 and RFC 0043 into REVIEW.

Structural state already satisfied before this pass:

- All required template sections are present in order and
  `python3 scripts/check-rfc.py` passed for the tree.
- `required-owners` (`rfc`, `lexer-parser`, `module-system`, `tooling-lsp`,
  `verification`) exactly matches the Repository Impact owner set, and every id
  exists in `.codex/subagents/manifest.yaml`.
- `review-manager` is `rfc`.

Blocking findings that kept the RFC in DRAFT, and their resolution in this pass:

1. **No layout engine.** The DRAFT specified style *rules* (indentation, spacing,
   trailing commas) but not the mechanism that enforces them, leaving line
   breaking underspecified and un-reviewable. Resolved: added a "Layout Engine"
   section adopting the Wadler/Lindig document algebra proven by Prettier and the
   Haskell `prettyprinter` library - a closed `Doc` constructor set
   (`text`/`concat`/`line`/`softline`/`hardline`/`group`/`indent`/`ifBreak`/
   `fill`) rendered by one linear, search-free, width-driven `fits` pass. A
   syntax-directed printer emits `Doc`; no node computes its own breaks. Pinned
   the target width at a fixed 100 columns (Unicode scalars), explicitly not a
   configurable option.

2. **Two Open Questions deferred "before REVIEW" were unanswered.** Both are now
   authored from prior art and folded into the Reference-Level Design, leaving
   Open Questions as `None`:
   - *Safe range-expansion ownership* - range expansion accepts an enclosing node
     only when both boundary tokens are non-recovery tokens with unambiguous
     trivia attachment in the RFC 0023 recoverable CST (rust-analyzer's
     node-level formatting model). Recorded in "Ranges, Recovery, And Errors".
   - *Cross-platform atomic write* - temp sibling in the destination directory,
     `fsync`, then atomic same-directory `rename(2)` after digest re-validation;
     atomic on ext4/xfs and APFS/HFS+, the gofmt/rustfmt/Black pattern. Recorded
     in "Integration And Writes".

Frontmatter moved to `status: REVIEW`, `updated: 2026-08-28`, with `discussion`,
`decision`, and `tracking-issue` bound to this tracker. `approvers` stays empty
and `decision` stays TBD in frontmatter; no `REVIEW -> ACCEPTED` transition is
performed and no owner approval is recorded here. The RFC 0023 dependency
(recoverable lossless snapshot) remains REVIEW; RFC 0044 acceptance and
implementation stay gated on it as stated in Acceptance Criteria.

### 2026-08-28 Five-Owner Review And REVIEW -> ACCEPTED

The RFC 0023 dependency reached ACCEPTED (2026-08-28), satisfying Acceptance
Criterion #1 ("RFC 0023 is accepted before any LSP integration lands"). All five
required owners reviewed the frozen snapshot against the live repository:

- **Input contract.** The formatter consumes RFC 0023's recoverable lossless
  CST / token-trivia stream (now ACCEPTED); it never reads semantic AST/HIR.
- **No diagnostic churn.** The RFC introduces no new `ZOMxxxx` code (grep
  confirms zero); rejects reuse the existing source diagnostic rail.
- **Engine substance.** The Wadler/Lindig Doc-IR layout engine and the pinned
  100-column width authored at REVIEW are intact and implementable.
- **Prior art.** Eight formatters cited (gofmt, Black, Prettier, rustfmt,
  clang-format, plus the Wadler/Lindig algebra) - the gate needs three.
- **Open Questions** are `None`.

No defect was found. Every owner approved. The RFC advances
`REVIEW -> ACCEPTED`. Implementation - the pure Doc-IR core + independent
token/trivia verifier + `zomc fmt` - stays `TBD` with no `IMPLEMENTING` pointer;
it is backend-independent and can begin as O6/KR6.2's second half.

## Owner Review Matrix

Each owner reviewed its surface against the accepted snapshot on 2026-08-28.

| Owner | Surface | Status |
|---|---|---|
| `rfc` | Process, template conformance, scope | Approved |
| `lexer-parser` | Lossless token/trivia stream, `Doc` emission from recoverable CST | Approved |
| `module-system` | Source snapshots, digest-checked atomic writes | Approved |
| `tooling-lsp` | CLI/editor facade sharing, byte-edit result contract | Approved |
| `verification` | Idempotence, token-preservation, mutation, CI gates | Approved |

## Decision Record

Decision: Accepted 2026-08-28. All five required owners approved the frozen
snapshot after the RFC 0023 dependency reached ACCEPTED and the input-contract,
diagnostic, engine, and prior-art checks passed. `decision` is set;
`implementation` stays `TBD` and no `ACCEPTED -> IMPLEMENTING` pointer is set.

## Implementation Tracker

### 2026-08-28 First Authorized Slice And ACCEPTED -> IMPLEMENTING

The first authorized slice (Implementation Plan step 2, the pure formatter core)
landed as evidence, and RFC 0044 moves `ACCEPTED -> IMPLEMENTING`. This mirrors
how RFC 0016/0021/0043 entered IMPLEMENTING on a landed first slice rather than
the full contract. The slice is the pure Wadler/Lindig Doc algebra plus its
generic width-driven layout renderer; it depends on no lexer, parser, CST, or
filesystem and builds under the default frontend `sanitizer` preset.

Landed:

- `compiler/format/doc.{h,cc}` - the closed `Doc` constructor set
  (`text`/`concat`/`line`/`softline`/`hardline`/`group`/`indent`/`ifBreak`/`fill`)
  as a move-only immutable value tree built only through named factories.
- `compiler/format/doc-renderer.{h,cc}` - the total, search-free width-driven
  `fits`/layout pass at the pinned 100-column width (`kTargetWidth`) with the
  four-column indent step (`kIndentStep`): a `group` renders flat when its flat
  width plus the current column stays within the width and it carries no
  `hardline`, otherwise its direct `line`/`softline` break and its `indent`
  applies.
- `tests/unittests/compiler/format/doc-renderer-oracle-test.cc` - hand-built
  documents rendered at width 100, asserting text verbatim, flat-vs-broken group
  decisions, hardline forcing, nested indentation accumulation, `ifBreak`
  selection, `fill` packing, and render determinism (8/8 pass).

The slice adds no `ZOMxxxx` diagnostic code. Deferred to later slices (correctly
Pending): the syntax-directed printer that walks the RFC 0023 recoverable
lossless CST to emit a `Doc` (blocked on the RFC 0023 lossless-snapshot
implementation, not yet in the tree), the independent token/trivia verifier,
canonical edit normalization and range expansion, digest-checked atomic CLI
writes, and the `zomc fmt` / `zomc fmt --check` command.

| Slice | State | Required evidence |
|---|---|---|
| Pure formatter core: `Doc` algebra + width-driven layout renderer | Landed 2026-08-28 | `compiler/format/doc.{h,cc}` + `doc-renderer.{h,cc}` + `doc-renderer-oracle-test` (8/8, frontend sanitizer build). |
| Syntax-directed printer over the RFC 0023 lossless CST | Blocked | RFC 0023 lossless-snapshot implementation (not yet in the tree). |
| Independent token/trivia preservation verifier | Pending | Token-sequence equivalence over the printer output. |
| Canonical edit normalization, range expansion, mutation tests | Pending | Sorted disjoint `SourceReplacement` set; range-expansion boundary rules. |
| Digest-checked atomic CLI writes and `zomc fmt` | Pending | Temp-sibling + fsync + rename; `--check`; native Linux/macOS tests. |
| IDE facade + LSP adapter integration | Pending | Byte-edit result bound to the document version. |

## Verification Evidence

- `python3 scripts/check-rfc.py` passes for this transition.
