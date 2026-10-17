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
| `binder-checker` | LocalWrite admission, init lattice, Initialize/Overwrite classification, ZOM4024 drain | APPROVE (Round 2, 2026-10-17; Round 1 six findings revised and code-verified resolved; two non-blocking nits applied) |
| `ir-backend` | HIR late-init arm, recursive MIR write family, structural verifier, LIR admission, codec stability | REQUEST-CHANGES (Round 1, 2026-10-17; HIR diamond node missing, LIR layer absent/fail-open, verifier needs CFG lattice + nits); revised 2026-10-18, awaiting Round 2 |
| `error-system` | ZOM4024/ZOM4093/ZOM4094 diagnostic contracts, projector/stage mapping, no parallel code | APPROVED (Round 2, 2026-10-17; both Round 1 blockers resolved, nits applied) |
| `runtime-memory` | Ownership init rail seam, ZOM4093 sole-producer boundary, zc/runtime/FFI scope | APPROVE (Round 1, 2026-10-18; five evaluation points code-verified; three non-blocking nits, all applied) |
| `verification` | Conformance grammar/AST/diagnostics coverage, parity gate, mutation tests | APPROVE (Round 2, 2026-10-18; Round 1 three blockers resolved, nits applied) |

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

#### error-system owner, Round 1: REQUEST-CHANGES (2026-10-17); revised

Verified: ZOM4024 reuse is a same-concept extension (Identifier arg,
Body/Mutation/InvalidOperation, factory proven for locals and fields);
ZOM4093/ZOM4094 are ownership-rail-only with no checker-side producer;
ZOM4087/ZOM4099 stay correctly scoped. Two blocking corrections applied:

1. Pinned ZOM4093/ZOM4094 as ownership-rail-only and required
   error-preserving HIR/MIR construction for the one-branch-missing diamond
   and uninitialized-`mut` shapes (extending the existing
   UninitializedLocalReturn pattern), explicitly rejecting a duplicate
   checker-side ZOM4093 producer and its undeclared contracts. The
   Reference-Level lattice and body-checker sections and implementation
   plan step 3 were rewritten to state the body checker classifies writes
   only.
2. Removed "multiple writes on the `let` path" from the fail-closed
   acceptance boundary; that shape is semantically invalid and must reach
   ZOM4024 (write) / ZOM4093 (read) before the capability gate, not ZOM4099.
   The boundary now lists only semantically-valid-but-unlowerable shapes
   with an explicit invalid-program rejection list.

Nits applied: the Compatibility section now records the ZOM4099 to ZOM4024
sharpening for initializer-present immutable reassignment (no pinned
fixture breaks); the test plan adds the both-errors diamond and the
initialized-`let` reassignment case and requires CHECK-LITERAL/backtick
expectations. Awaiting Round 2 review.

#### error-system owner, Round 2: APPROVE (2026-10-17)

Both Round 1 blocking findings verified resolved against the tree: the
sole-producer ZOM4093 contract, error-preserving HIR/MIR construction, and
the rejected checker-side alternative are now explicit; the acceptance
boundary lists only semantically-valid shapes and routes invalid reads and
double-`let` writes to ZOM4093/ZOM4024 before the capability gate. All
nits applied (Compatibility sharpening note, both-errors diamond test,
CHECK-LITERAL/backtick style). One ungrammatical table row was fixed. No
new diagnostic code or producer is required.

#### binder-checker owner, Round 1: REQUEST-CHANGES (2026-10-17); revised

Six code-verified findings, all addressed in the revision:

1. Lattice location: rewrote Reference-Level as two layers - a NEW
   AST-structural body-checker write-registry pre-pass and the unchanged
   post-MIR ownership lattice (sole ZOM4093 producer). The body checker has
   no init state and emits no ZOM4093.
2. ZOM4093 producer seam: chose alternative (b) - keep ZOM4093 ownership-
   only, require error-preserving HIR/MIR construction for the
   one-branch-missing diamond and uninitialized-mut; rejected the checker
   typed-issue/projector/note-channel option explicitly.
3. Read-before-init soundness: bounded the first-write exception to plain
   Assign with a RHS that does not read the target (excludes `v = v`,
   `v = v+1`, `v += 1`); the read rule applies to all RHS operands and the
   condition; ++/-- stay mut+initialized; diamond arms structurally bounded.
4. Stale motivation: corrected - straight-line mut late init and overwrite
   already build end to end via GenericShape (must be parity-protected, not
   new); added the parameter-RHS/overwrite ir-phase ICEs as baselines to
   clear; corrected the arm name to LocalWriteReturn.
5. Path-scoped classification: specified a dedicated checker write-registry
   pre-pass consulted at admission and in the post-write loop, lockstep
   HIR builder+verifier updates (both diamond arms Initialize), kind stays
   HIR-derived (no new fact/codec), LocalWrite emits no CheckedPlaceFact.
6. Stage ordering: rollout is now per-shape vertical transactions
   (checker admission + HIR arm + proven MIR in one slice), not per stage;
   the structural verifier claim is scoped to structural cfg-utils checks
   (the current linear StorageLive scan cannot prove cross-edge dominance;
   ownership keeps the path-sensitive guarantee).

Added runtime-memory as a required owner for the unchanged ownership rail.
Round 2 returned APPROVE (entry below); runtime-memory Round 1 also
APPROVE.

#### verification owner, Round 1: REQUEST-CHANGES (2026-10-17); revised

Three blocking observability gaps addressed:

- B1 gate labels: replaced `-L lit` with full `ctest --preset default` plus
  the `conformance-grammar`/`coverage` labels and
  `check-diagnostic-coverage.py --check/--self-test`.
- B2 end-to-end witnesses: added a straight-line mut positive and a mut
  initialize-then-overwrite positive to the acceptance boundary; specified
  the package-driven native witness (no-arg foldable condition) with the
  exact CMake run-case identifier shape; stated parameterized guide example
  is check/object only.
- B3 parity: named the `--record --ir` then `--check --ir` command against
  tests/coverage/corpus-process-parity.json plus `--self-test`; enumerated
  the reviewed existing-source deltas (initialized-let double-write 4099 to
  4024; raw_pointer_deref_assignment_requires_unsafe_neg_42 pinned first).

Nits applied: mutation-test seam reuse (compiler-session-package-test),
straight-line + one-arm-diamond domination mutations, reuse of
uninitialized_local_return_neg_01, grammar YAML hand-authored with human
diff review of regenerated diagnostics, CHECK-LITERAL/backtick style.

#### ir-backend owner, Round 1: REQUEST-CHANGES (2026-10-17); revised 2026-10-18

Three blocking findings, all code-verified by the owner:

- B1 missing HIR diamond node: `HirConditionalExpression`
  (`compiler/hir/hir-module.h:428`) is an expression whose arms yield
  values; the statement `if (c) { v = a; } else { v = b; }` has no
  constructible HIR representation. Resolved by adding a dedicated
  block-listed `HirConditionalLocalWriteStatement { condition, thenWrite,
  elseWrite }` node with its own pool, `HirNodeKind::ConditionalLocalWrite`,
  fixed-id digest coverage, and a per-node verifier arm. The same arm
  serves `let` and `mut` diamonds (a per-path first write is
  `Initialize`); scalar guards require `write.field == none` and the
  admitted condition operand forms. The value-yield record reuse is the
  rejected alternative.
- B2 LIR layer absent: the only four-block diamond matcher
  (`compiler/lir/mir-to-lir.cc:1085` onward) requires exactly one
  FunctionResult local, a single entry `StorageLive(result)`, and a bare
  boolean-parameter discriminant; a UserLocal late-init diamond matches
  none of these. Resolved with a new "LIR contract" RFC section, a
  `compiler/lir/mir-to-lir.{h,cc}` Repository Impact row (ir-backend),
  LIR work folded into vertical slices 1-3 (scalar user-local path;
  generalized/sibling UserLocal matcher with the foldable comparison
  discriminant first), and the hard requirement that every pre-LIR miss
  during rollout be a clean ZOM4099, never an incident; the known
  parameter-RHS/overwrite ICE baselines must clear in slice 1.
- B3 verifier must be a CFG lattice: the current
  `validateStructuralWellFormedness` linear StorageLive scan
  (`built-mir.cc:1686`) never inspects `MirInitializationKind` and cannot
  see cross-arm dominance. Resolved with a forward per-block entry/exit
  initialized lattice: parameters initialized at entry; `Initialize`
  requires in=false and sets true; `Overwrite` requires in=true; block
  entry is the AND meet over predecessors via the existing terminator
  traversal and `resolveDiamondCfg`.

Layer reconciliation while resolving B3 with the error-system and
runtime-memory seams: the lattice does NOT reject a read/move/borrow/
Return operand at a false entry. Reading a storage-live-but-uninitialized
place is the error-preserving carrier the ownership rail consumes to emit
ZOM4093; making it structurally invalid would convert the existing
`UninitializedLocalReturn` path and the one-branch-missing diamond into
`IrInvariantRejected` incidents. Structural verification owns IR kind
discipline only; ownership owns source-level uninitialized reads. The
one-arm `Initialize` diamond is structurally valid and reaches ZOM4093;
the verifier mutation flips that arm to `Overwrite` (in=false), which is
the invariant rejection.

Nits applied: legacy-arm retirement gated on observed byte-parity with
parallel rails named as a blocker; within-transaction ordering (new arm
proven, then the ZOM4099 drain removed, then legacy deleted); mut diamond
uses the same arm as let; condition operand forms and `write.field ==
none` guards stated; existing field-level arms
(`buildLocalFieldProjectionReturn`) named as untouched; LIR helper
`lowerScalarLocalOverwriteReturn` naming clarified.

#### binder-checker owner, Round 2: APPROVE (2026-10-17)

All six Round 1 findings verified resolved against the tree rather than
the prose: two-layer location (body checker emits no ZOM4093;
`LocalWrite` sets only `producedType` and no `places.add` site is a plain
local write); sole-producer seam at `init.cc:1306` plus projector
`ownership-source-diagnostic-projector.cc:206-212`; read-before-init
bounds (plain Assign, RHS not referencing the target; `++/--` gates
verified); corrected motivation (working mut shapes build via
`GenericShape`, arm correctly named `LocalWriteReturn`, parameter-RHS
ICEs listed as baselines); path-scoped lockstep classification at all six
HIR verifier re-derivation sites; per-shape vertical transactions. Both
non-blocking nits applied: the missing-branch ZOM4093 assertion moved out
of the body-checker ztest list (the checker has no 4093 emitter), and the
`ScalarLocalOverwrite` sentence now notes the LIR helper
`lowerScalarLocalOverwriteReturn`.

#### verification owner, Round 2: APPROVE (2026-10-18)

B1 gates resolved (full `ctest --preset default`, `conformance-grammar`
and `coverage` labels confirmed in
`tests/conformance/runners/grammar/CMakeLists.txt:27`,
`check-diagnostic-coverage.py --check/--self-test`). B2 witnesses
resolved (four positives enumerated; package-driven no-arg foldable
native witness `definite-assignment-conditional-consumer`; run-case
shape matches `tests/CMakeLists.txt:422`). B3 parity resolved
(record/check/self-test commands; reviewed deltas enumerated including
`raw_pointer_deref_assignment_requires_unsafe_neg_42.zom`, confirmed to
have no diagnostics layer today). Nits all verified: mutation seam
`compiler-session-package-test.cc:2359-2396`, `MirStatement::assign`
signature, no duplicate of `uninitialized_local_return_neg_01`,
hand-authored grammar YAML, CHECK-LITERAL/backtick style. Harmless
imprecision corrected: `--self-test` uses embedded fixtures and takes no
snapshot argument.

#### runtime-memory owner, Round 1: APPROVE (2026-10-18)

All five evaluation points code-verified. The init rail already gives the
required lattice (`init.h:66-71`: intersection on `mustBeInitialized`),
admits the reducible SwitchInt/Goto topology with monotone fixpoint plus
the all-predecessor emission pass (`init.cc:890-969`), transfers
`Initialize`/`Overwrite` correctly (`init.cc:449-462`), and emits
ZOM4093/ZOM4094 with the note anchored at the `StorageLive` declaration
span (`init.cc:1138-1148`). The owner hand-traced the one-arm-missing
diamond: derivation succeeds and it surfaces as source ZOM4093. No
zc/runtime/FFI/StorageDead/drop surface is touched in the Copy-scalar
slice. Three non-blocking nits, all applied:

1. The one-branch-missing diamond must surface as `SourceRejected`
   ZOM4093, never `IrInvariantRejected`; a dedicated full-session negative
   assertion now pins this (and drove the B3 lattice layering described
   in the ir-backend entry above).
2. The admitted RHS value set is stated explicitly as Copy scalars
   (integer/boolean primitives; no aggregates, references, pointers, or
   other non-Copy values), so a non-Copy parameter RHS cannot enter the
   `Initialize` arm.
3. Follow-up prerequisite recorded: when a later RFC lifts
   compound/arithmetic writes or partial aggregate initialization, the
   `init.cc` source-failure traversal must be extended to inspect
   `Comparison`/`Arithmetic`/`NominalAggregate` rvalue operands, not only
   `Use` rvalues (`init.cc:1367-1368` today). This is a Chapter 14
   follow-up gate, out of scope for RFC 0055.

## Implementation Tracker

Not started. Implementation begins only after the RFC moves to ACCEPTED.
