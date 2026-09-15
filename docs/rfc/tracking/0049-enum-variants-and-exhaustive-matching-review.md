# RFC 0049 Review And Implementation Tracker

## Discussion Record

### 2026-09-14 DRAFT Authored And DRAFT -> REVIEW (broad ADT draft)

A broad algebraic-data-type proposal (products, sums, newtype, matching,
boxing, variance, derives, layout, niches) was frozen for review as
`0049-algebraic-data-type-design.md`.

Frozen Round-1 proposal snapshot (superseded file):

| Round-1 proposal SHA-256 (superseded) | `bae1d2c060767d248c3560453f98a7b9b7397d64b6c2ff208e3e4bee22166ed1` |
|---|---|

### 2026-09-15 Round 1 - All owners REQUEST-CHANGES; proposal RETURNED and split

Nine owners reviewed the frozen snapshot. The direction for sums and
matching was accepted; the blockers showed the broad proposal mixed
implementable enum work with features whose prerequisites do not exist and
contained factual errors against the live catalog, grammar, and specs.

Key findings driving the split:

- Diagnostics: ZOM4022/ZOM4023 (match), ZOM4054 (orphan), and
  ZOM4063/ZOM4065 (linear) already exist; the broad table proposed
  duplicates. The 3000-range collision spans binder and module codes.
- Recursive boxing assumed a language box type (`Own<T>` is host C++), a
  runtime allocator that does not exist, and non-trivial drop glue while
  `__zom_drop` is currently a no-op.
- Niche/repr(C)/newtype layout promises require an aggregate LIR layout
  store, target-driven emission, and a size/align channel that do not exist.
- Variance requires a subtype/coercion engine; the current inference core is
  equality-only.
- The proposal wrongly said structs have no methods; methods/init/deinit are
  accepted grammar, chapter 08 text, and positive corpus fixtures, and
  `structMember` is shared with `error` declarations. Struct surface is left
  unchanged.
- Derive(Copy) conflicts with RFC 0024 automatic structural Copy and several
  derive trait names do not exist; derives are deferred.
- if-let, let-else, and or-patterns are genuinely new (the current `|`
  acceptance is a bitwise-or misparse); record variants are hard-rejected
  today and the dotted record pattern diverges between parser and oracle.
- Spawn-boundary enforcement is out of scope under ZOM4095/RFC 0007, so
  `concurrency` is not a required owner.

Decision (2026-09-15): split the RFC. Number 0049 is retitled
"Enum Variants And Exhaustive Matching" and scoped to record variants,
exhaustive matching, if-let/let-else, or-patterns, and cross-module
`non_exhaustive`. Products, newtype, boxing, layout, niches, variance, and
derives move to future RFCs. Struct methods are preserved as accepted
surface. The revised proposal re-enters DRAFT then REVIEW for Round 2 under
`0049-enum-variants-and-exhaustive-matching.md`.

### 2026-09-15 Revised DRAFT -> pending REVIEW re-freeze

The revised scope is authored and returns to DRAFT. A new frozen snapshot is
recorded when it re-enters REVIEW.

### 2026-09-15 Revised DRAFT -> REVIEW Round 2

| Proposal SHA-256 | `8fb950f01ead03a9fd4dafd8f63818353e2ffb60ebcbdb90969b1e2ae2625706` |
|---|---|


## Owner Review Matrix

| Owner | Surface | Round 1 (broad draft) | Round 2 (enum/match) |
|---|---|---|---|
| `lexer-parser` | Grammar/oracle/AST/recovery deltas | REQUEST-CHANGES (struct-method deletion; shared structMember; new if-let/let-else; or-pattern misparse; full record-variant delta; variance attachment; derive closed-set) | Pending |
| `binder-checker` | Variants, exhaustiveness, coherence, moves | REQUEST-CHANGES (duplicate codes; variance coercion dependency; terminology) | Pending |
| `error-system` | Diagnostics reuse and reservations | REQUEST-CHANGES (reuse 4022/4023/4054/4063/4065; allocate real slots) | Pending |
| `ir-backend` | Match HIR/MIR, discriminant vocabulary | REQUEST-CHANGES (discriminant-read node; boxing/layout prerequisites) | Pending |
| `runtime-memory` | Boxing and drop prerequisites | REQUEST-CHANGES (no language box/allocator/glue) - out of revised scope | Not required for revised scope |
| `module-system` | Export surface, visibility, non_exhaustive | REQUEST-CHANGES (variant export; foreign-module rule; ch23 default - removed with products scope) | Pending |
| `spec-audit` | Chapter and grammar alignment | REQUEST-CHANGES (chapter list; product/layout conflicts - narrowed in revision) | Pending |
| `verification` | Tests, parity, gates | REQUEST-CHANGES (layout harness; parity flags; diagnostic lit matrix) | Pending |
| `rfc` | Process and conformance | REQUEST-CHANGES (duplicate codes; deleted surface; Copy conflict; scope) | Pending |
| `concurrency` | Spawn boundary (advisory) | REQUEST-CHANGES on spawn content; resolved by making spawn a non-goal | Not required for revised scope |
| `task-router` | Routing (not required for 0049) | Not in required owners | N/A |

## Decision Record

TBD. No decision is recorded; no owner approval is recorded for the revised
scope.

## Implementation Tracker

Not started; the revised RFC is not ACCEPTED.
