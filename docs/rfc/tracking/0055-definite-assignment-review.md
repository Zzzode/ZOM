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

TBD — pending all required owner approvals.

## Owner Review Matrix

| Owner | Surface | Round 1 |
|---|---|---|
| `rfc` | Process, template conformance, required-owner completeness, RFC granularity | Pending |
| `spec-audit` | Chapter 6/14 normative prose, five-way spec alignment, drift posture | Pending |
| `binder-checker` | LocalWrite admission, init lattice, Initialize/Overwrite classification, ZOM4024 drain | Pending |
| `ir-backend` | HIR late-init arm, recursive MIR write family, structural verifier, codec stability | Pending |
| `error-system` | ZOM4024/ZOM4093/ZOM4094 diagnostic contracts, projector/stage mapping, no parallel code | Pending |
| `verification` | Conformance grammar/AST/diagnostics coverage, parity gate, mutation tests | Pending |

### 2026-10-17 Six-Owner Review Round 1 - Opened

Review dispatched to all six required owners. Findings and resolutions are
recorded below as the owner agents report.

## Implementation Tracker

Not started. Implementation begins only after the RFC moves to ACCEPTED.
