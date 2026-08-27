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

## Owner Review Matrix

| Owner | Surface | Status |
|---|---|---|
| `rfc` | Process, template conformance, scope | Pending |
| `lexer-parser` | Lossless token/trivia stream, `Doc` emission from recoverable CST | Pending |
| `module-system` | Source snapshots, digest-checked atomic writes | Pending |
| `tooling-lsp` | CLI/editor facade sharing, byte-edit result contract | Pending |
| `verification` | Idempotence, token-preservation, mutation, CI gates | Pending |

## Decision Record

No decision recorded. RFC 0044 is in REVIEW; `decision` remains TBD until the
required owners approve a frozen snapshot.

## Implementation Tracker

No implementation authorized. RFC 0044 is not `ACCEPTED` and not `IMPLEMENTING`;
the implementation pointer stays TBD. The first authorized slice, once accepted,
is the pure formatter core plus the independent token/trivia verifier
(Implementation Plan step 2), gated on the accepted RFC 0023 lossless snapshot
(step 1).

## Verification Evidence

- `python3 scripts/check-rfc.py` passes for this transition.
