---
rfc: 53
title: LIR Structural Verification And MIR-to-LIR Translation Validation
type: compiler
status: DRAFT
author: ZOM Compiler Team
review-manager: rfc
required-owners: [rfc, ir-backend, verification]
approvers: []
created: 2026-09-18
updated: 2026-09-19
area: compiler
requires: [10, 21, 47]
supersedes: []
superseded-by: []
discussion: TBD
decision: TBD
implementation: TBD
tracking-issue: TBD
---

# RFC 0053: LIR Structural Verification And MIR-to-LIR Translation Validation

## Summary

Add two independent, fail-closed evidence stages between MIR-to-LIR lowering
and LLVM translation: a structural verifier that walks a LIR module and proves
it well-formed without knowing its source shape, and a translation validator
that proves the LIR module preserves the semantics of the verified Built MIR
functions it was lowered from, using an order-preserving effect correspondence
derived by walking MIR rather than by classifying whole-function shapes. Both
stages are independent code from `compiler/lir/mir-to-lir.cc`, run on every
`zomc compile --emit=binary` path before `llvm::verifyModule`, and reject with
an internal invariant failure rather than emitting a silently wrong object.

## Motivation

The backend currently trusts the MIR-to-LIR lowering completely. Lowering is a
collection of per-shape admittance arms in `compiler/lir/mir-to-lir.cc`,
selected by whole-module shape matching in `utils/zomc/zomc.cc`
(`emitBinary`), and nothing between lowering and LLVM translation examines the
produced `lir::Module`:

1. There is no LIR structural verifier. A malformed LIR module (dangling block
   target, undeclared local ordinal, wrong arity on a terminator, mismatched
   carrier) is only caught if it happens to produce LLVM IR that fails
   `llvm::verifyModule`. A mapping error that produces well-formed but wrong
   LLVM IR (swapped branches, wrong constant bits, a call targeting the wrong
   function index) is not caught at all.
2. There is no semantic-preservation check across the MIR-to-LIR hop. The
   923-source byte-parity channels detect *unexpected changes* against the
   previous lowering implementation; they cannot detect a defect shared by the
   old and new implementation, and byte parity is deliberately being retired as
   the correctness mechanism as RFC 0048 replaces the shape rails.
3. The lowering surface is growing (scalar initializer, field fold, aggregate
   return, conditional diamond, reducible loop, comparison diamond, zero/one/
   two-argument calls, three-function modules) and RFC 0010/0021 schedule
   general legalization and ABI lowering next. Verification is cheapest to
   introduce while the admitted construct set is small and every construct is
   covered by a ztest, and it becomes part of the admission contract every
   later producer must satisfy.

The MIR side already has structural-or-shape verification (RFC 0048 is
generalizing it), canonical codec re-encoding, and an independent ownership
proof oracle. The LLVM side already mandates `llvm::verifyModule`. The LIR hop
is the one hop in the compiled pipeline with no independent evidence.

## Goals

- Add a structural LIR verifier that validates every invariant of the current
  closed `lir::Module` algebra independently of the producer.
- Add a translation validator that, for every currently admitted lowering
  shape, proves a construct-level correspondence between the verified Built
  MIR functions and the emitted LIR module: no MIR effect dropped, no LIR
  instruction unaccounted for, constants, places, operators, block edges, call
  targets, and carriers preserved.
- Derive the correspondence by walking MIR constructs, not by duplicating the
  whole-function shape classifiers, so the validator generalizes as lowering
  generalizes and does not become a third shape rail alongside HIR and MIR.
- Make both stages mandatory on the object-emission path and fail closed: a
  rejected module never reaches LLVM translation or object emission.
- Keep both stages linear in the size of their inputs with explicit budget
  rules, matching the RFC 0007 operational-budget discipline.

## Non-Goals

- SMT-based or bounded symbolic translation validation (Alive/Alive2 style).
  The arithmetic/comparison subset is a future slice once this structural
  relation lands.
- A MIR reference interpreter and reference-vs-native differential execution
  harness. That is a separate future RFC.
- Verification of LLVM IR or LLVM optimizations. `llvm::verifyModule` remains
  the boundary at LLVM; semantic validation of LLVM translation itself is out
  of scope.
- Object, ELF/Mach-O, or linker validation, which RFC 0043 owns through bounded
  inspection and the host `ld`.
- General legalization, calling-convention, and ABI lowering validation; those
  contracts arrive with their RFC 0010/0021 producers and extend the relation
  defined here in the same change.
- Deletion of any MIR or LIR shape classifier; that remains RFC 0048 Phase 5.
- New user-facing `ZOMxxxx` diagnostics. A verification failure on already
  verified MIR is a compiler invariant failure.

## Prior Art

### LLVM `verifyModule` And The MachineFunction Verifier

LLVM structurally verifies IR after every pass in assertion builds and
mandates `verifyModule` before codegen in production pipelines: every operand
references a defined value, terminators are complete, types are consistent, and
the CFG is coherent. The verifier is per-construct, not per-pattern; it has no
knowledge of which pass produced the function. ZOM copies the per-construct
structure and the "verify at every trust boundary, independent of the producer"
placement. ZOM makes it mandatory in all builds, not only assertion builds,
because the LIR producer set is small and the cost is linear.

### Cranelift `cranelift-verify`

Cranelift ships a structural verifier that checks SSA dominance, type
consistency, and block/terminator coherence over `Function` IR, run after
construction and before legalization. Its verifier defines validity directly
over the IR algebra and reports the first violated invariant rather than
matching templates. ZOM's structural LIR verifier follows the same organization
over LIR's slot-based (non-SSA) algebra: dense ordinals, declared slots,
closed terminator arity, carrier consistency, and a coherent CFG.

### rustc MIR Structural Verification

`rustc_mir::transform::verify` walks every MIR body and checks structural
invariants independent of which lowering produced it. RFC 0048 imports that
model for HIR and MIR; this RFC applies the identical principle one hop later
so that LIR does not remain the only IR whose validity is implied rather than
checked.

### Translation Validation (Pnueli; LLVM AArch64 Translation Validator)

Translation validation, introduced by Pnueli, Siegel, and Singerman, does not
prove the compiler correct; it proves that *this translation run* preserves the
semantics of *this input*, by constructing a refinement or simulation relation
between source and target programs. The Utah "Translation Validation for
LLVM's AArch64 Backend" work applied relation-checking to a production backend,
demonstrating that a construct-by-construct correspondence with block and
effect bijection catches mapping defects that pattern tests and byte-parity
miss. ZOM adopts the relation approach and the independence requirement (the
validator shares no producer helper code), at the syntactic level appropriate
to the current LIR subset.

### Alive2

Alive2 validates LLVM peephole optimizations by SMT-based refinement over
bounded undef/poison semantics and is used in LLVM development. ZOM does not
adopt SMT in this RFC: the admitted LIR algebra has no optimization rewrites
yet, and a syntactic correspondence covers the existing defects (mapping,
arity, edge, carrier, constant) exhaustively. The abstract effect relation is
designed so an SMT refinement layer for arithmetic and comparison effects can
be added without changing the structural stages.

## Guide-Level Explanation

A contributor adding a new MIR-to-LIR lowering shape today must trust that
their arm maps each MIR construct to the right LIR construct. After this RFC,
two machines check that work independently:

```text
Verified Built MIR
       |
       v
+-------------------+
| mir-to-lir.cc     |  producer (admitted lowering arms)
+-------------------+
       | lir::Module
       v
+-------------------+
| LirStructural     |  is the LIR module well-formed in isolation?
| Verifier          |
+-------------------+
       |
       v
+-------------------+
| Translation       |  does this LIR module preserve the MIR it
| Validator         |  claims to implement, effect by effect?
+-------------------+
       |
       v
  LLVM translation + verifyModule -> object
```

For example, lowering a four-block conditional produces a LIR `CondBranch`
whose true and false targets correspond to the MIR `SwitchInt` targets, and
arm assignment statements whose operands match the MIR arm values. The
structural verifier proves both branch targets exist and the condition slot is
a one-bit carrier. The translation validator proves the target correspondence,
the branch polarity, the arm constants or copied slots, and the join return,
walking the MIR blocks in order. If a lowering arm accidentally swaps the true
and false targets, the LIR module is still structurally well-formed and LLVM
would accept it; the translation validator rejects it.

A verification failure is not a user error: the MIR input already passed
checking and MIR verification, so a failed relation is evidence of a compiler
defect. It is reported through the internal incident failure path, never as a
`ZOMxxxx` source diagnostic, and compilation fails closed.

## Reference-Level Design

### Placement And Types

Two new components live under `compiler/lir/verify/` (a pure path-prefixed
subdirectory, listed explicitly in `compiler/lir/CMakeLists.txt`):

- `lir-verifier.{h,cc}` defines `LirStructuralVerifier`.
- `translation-validator.{h,cc}` defines `TranslationValidator`.

Neither file includes `compiler/lir/mir-to-lir.h` or any lowering-internal
helper. The producer and the validators may share only the immutable
`lir::Module` and `mir::MirFunction` types, the session `SemanticTypeStore`,
canonical type/constant encoders, and branded identity types. In particular
the carrier-derivation, local-slot mapping, block correspondence, and symbol
matching are independently implemented in the validator; a shared helper that
both producer and validator call is prohibited because it would share the
defect it exists to detect.

Both components return `zc::Maybe<LirVerificationFinding>`, where `none` means
the module passed. A finding is an immutable canonical record:

```text
LirVerificationFinding {
  stage:      Structural | Translation,
  fault:      LirVerificationFaultKind,
  mirContext: { function owner ordinal, block ordinal, statement index }
              (translation stage only; absent fields are zero),
  lirContext: { function index, block ordinal, statement index },
  detail:     one closed uint8 tag selecting which fields carry payload
}
```

Finding order is irrelevant because verification fails on the first finding in
a deterministic visitation order (functions by module order, blocks and
statements by vector order); the finding is a diagnostic carrier, not an
aggregate. `LirVerificationFaultKind` is a closed `uint8` enum declared in the
verifier header beginning at `0x01` in declaration order: structural faults
(`EmptyOrDuplicateSymbol`, `MissingEntryBlock`, `DuplicateBlockOrdinal`,
`NonDenseBlockOrdinals`, `UnreachableBlock`, `NonDenseLocalSlots`,
`UndeclaredLocalSlot`, `DanglingBlockTarget`, `TerminatorArity`,
`CarrierMismatch`, `ConditionNotBit1`, `CalleeIndexOutOfRange`,
`ReturnCarrierMismatch`) and translation faults
(`FunctionSetMismatch`, `BlockBijectionMismatch`, `EffectMismatch`,
`ConstantMismatch`, `PlaceMappingMismatch`, `OperatorMismatch`,
`EdgeTargetMismatch`, `CallCalleeMismatch`, `SlotSetMismatch`,
`SymbolMismatch`). Invariants the closed Terminator factories already make
unrepresentable (empty or over-cap call argument and aggregate bundle
vectors) deliberately carry no tag; the structural verifier additionally
checks the aggregate bundle at its own trust boundary. The exact payloads
land with the canonical encoding in the implementing change; no tag is
reserved without a producer and a mutation test.

### Structural Verification

`LirStructuralVerifier::verify(const lir::Module&) -> Maybe<Finding>` checks,
in order:

1. Function symbols are non-empty and unique within the module.
2. Every function has at least one block; block ordinals are dense from one and
   unique.
3. Every terminator target (`Goto`, `CondBranch` true/false, `Call` normal)
   names a declared block of the same function.
4. Every block is reachable from the entry block through terminator edges.
   Functions unreferenced by any call elsewhere in the module are legal
   (standalone leaves); the requirement is intra-function reachability.
5. Declared parameter and body-local slots are dense from one with no overlap
   between the parameter and local ordinal ranges, and every slot referenced by
   a statement or terminator is declared. Declaration order matches ordinal
   order.
6. Carrier consistency using the closed `ValueType` algebra:
   - every `Assign` constant operand carries the destination slot's carrier;
   - every `Compare` has operands of one identical carrier, writes a `Bit1`
     slot, and the operator is a closed `ComparisonOp`;
   - every `CondBranch` condition names a `Bit1` slot;
   - `ReturnLocal` names a slot whose carrier equals the function return
     carrier;
   - `ReturnInteger` carries the function return carrier;
   - `ReturnAggregate` has a non-empty bundle within
     `kMaxAggregateReturnSlots`, every slot an integer carrier, and the
     function return carrier equals the first slot carrier (the current RFC
     0021 transitional placeholder contract; when the placeholder is removed,
     this rule is replaced in the same change, no dual rule kept);
   - a `Call` callee index is within the module function range, its destination
     carrier matches the callee return carrier, its argument count equals the
     callee parameter count (the closed Terminator factories already bound the
     vector at `kMaxCallArguments`), and every argument carries the
     corresponding callee parameter carrier in order.
7. Every statement is one of the closed `StatementKind` alternatives and every
   terminator one of the closed `TerminatorKind` alternatives, with the fields
   valid for that alternative.

### Carrier Derivation

The validator independently maps a MIR semantic type to a LIR carrier through
`SemanticTypeStore`: the admitted scalar primitives map to their closed integer
widths, the boolean primitive maps to `Bit1`, and any type outside the
admitted carrier set fails the relation. This table duplicates the producer's
derivation on purpose and is kept to the same small primitive set; a second
shared abstraction is explicitly not introduced.

### Translation Validation

`TranslationValidator::validate(ArrayPtr<const mir::MirFunction>,
const lir::Module&, const SemanticTypeStore&) -> Maybe<Finding>` constructs the
expected LIR trace by walking MIR.

**Function correspondence.** The LIR module's function set equals the set of
MIR functions presented for lowering: same count, same definition owners,
matched by owner, never by array position. The module-initializer function
matches the reserved initializer symbol. Every LIR function corresponds to
exactly one MIR function; a missing or extra function is
`FunctionSetMismatch`.

**Block bijection.** For a matched function, MIR block count equals LIR block
count and the correspondence is the dense ordinal order (MIR block one to LIR
block one, and so on). Both algebras allocate blocks densely in construction
order for every admitted shape, so the order correspondence is the contract;
a count or order divergence is `BlockBijectionMismatch`. The terminator of
each block pair must correspond under the terminator relation below, including
edge targets, which are mapped through the block bijection.

**Slot set and place mapping.** The validator derives the set of slots LIR
must declare from the MIR effects it walks: every MIR local that a
corresponded effect reads or writes contributes its slot. MIR locals map in
ordinal order; parameter locals occupy the first ordinals and user locals and
temporaries follow in MIR declaration order. The declared LIR parameter and
local slots must equal the derived set with matching carriers
(`SlotSetMismatch`/`CarrierMismatch`). A MIR place referenced by an effect maps
to the slot of its root local; the admitted subset carries zero projections
(an aggregate field projection is handled by the aggregate fold relation, not by
a projected place). A place that maps to an undeclared slot is
`PlaceMappingMismatch`.

**Effect relation, per MIR construct in block statement order:**

| MIR construct | Expected LIR effect |
|---|---|
| `StorageLive(local)` | slot present in the derived slot set; no instruction (liveness is structural) |
| `StorageDead(local)` | no instruction; slot remains declared (no LIR liveness statement in the admitted subset) |
| `Assign(place, Use(Constant), Initialize/Overwrite)` | `Assign(slot, constant)` with equal carrier and equal bit pattern |
| `Assign(place, Use(Copy/Move operandPlace), init)` | `Assign(slot, localUse(operandSlot))` with equal carrier |
| `Assign(place, Comparison(op, l, r), init)` | `Compare(slot, op', operand(l), operand(r))`; `op'` is the same relational operator; operands map as constant or slot; result carrier `Bit1` |
| `Assign(place, NominalAggregate{...})` with field-folded return | no aggregate instruction; the returned field's constant reaches the return effect (field-fold shape) |
| `Assign(place, NominalAggregate{...})` with whole-struct return | the elements feed the `ReturnAggregate` bundle in MIR element order, equal bit patterns and carriers |
| `Terminator::Return(Constant)` | `ReturnInteger` equal |
| `Terminator::Return(place)` | `ReturnLocal(slot)` equal |
| `Terminator::Goto(b)` | `Goto(b')` with `b'` the bijection image of `b` |
| `Terminator::SwitchInt(bool place, then/else)` | `CondBranch(placeSlot, then', else')`; polarity preserved |
| `Terminator::Call(calleeOwner, dest, continuation)` | `Call(calleeIndex, destSlot, continuation')` where the LIR callee index resolves to the LIR function whose MIR owner is `calleeOwner`; arguments correspond element-wise |
| `UnsafeScopeBoundary`, ownership-only statements | no LIR effect in the admitted subset |

The expected and actual statement sequences are consumed in lockstep, skipping
exactly the MIR constructs with no LIR effect. Any unmatched expected construct
is `EffectMismatch`; an unmatched actual LIR statement is `EffectMismatch`
(no unaccounted instruction). Arithmetic rvalues, calls with non-constant
arguments, loops beyond the reducible four-block shape, and every other
construct not in the table fail the relation; they are admitted to the
relation only in the change that adds their producer.

**Constant equality.** Integer constants compare on the zero-extended bit
pattern after carrier derivation. Magnitude comparison uses the canonical
const encoding already shared through the signature-facts codec, never a
re-decoded host integer with assumed signedness.

**Module-level call integrity.** Every LIR `Call` callee index resolves
through the function correspondence to the MIR call's callee owner; emission
position is derived from the owner match and must equal the stored index. This
rejects the class of defects where reordering functions silently redirects a
call.

### Failure Projection

Both stages project through the existing IR failure algebra at phase
`IrFailurePhase::LirVerification` (`0x0d`), which is already declared but has no
producer today. A structural or translation finding maps to
`IrFailureKind::InvalidFact` with a closed detail record carrying the
`LirVerificationFaultKind` tag and contexts. The failure is an internal
compiler failure: it renders through the incident path, never through the
`ZOMxxxx` source diagnostic catalog, because valid verified MIR that fails
these relations cannot be caused by user source text.

### Integration Point

`utils/zomc/zomc.cc` `emitBinary` currently lowers selected MIR functions to a
`lir::Module` and immediately calls the LLVM translator. The new sequence is:

1. run the selected lowering arms exactly as today;
2. `LirStructuralVerifier::verify(lirModule)`;
3. `TranslationValidator::validate(selectedMirFunctions, lirModule, types)`,
   where the selected function set is exactly the set handed to lowering
   (including the standalone leaf in the three-function module);
4. on `none` from both, call the translator;
5. on a finding, abort the emission transaction through the internal failure
   path without invoking the linker or writing an output file.

When lowering later moves from the CLI into a session-owned library driver,
the same calls move with it; no CLI-specific semantics are introduced by this
RFC.

### Budget And Termination

Let `F` be the function count, `B` total blocks, `S` total statements, and `E`
total terminator edges in the LIR module, and let `M` be the total MIR
statement count of the presented functions. Structural verification is
`O(F + B + S + E)`; reachability visits each edge once. Translation validation
is `O(M + S + B)` plus owner-key comparisons linear in encoded owner length.
Neither stage allocates proportional to a constant bit pattern, a loop trip
count, or any source-derived numeric value; vector caps reuse the closed
`kMaxCallArguments` and `kMaxAggregateReturnSlots` limits. Resource exhaustion
aborts the whole driver transaction and is never relabelled a verification
finding, matching RFC 0007 operational-budget semantics.

## Repository Impact

| Area | Paths | Owner |
|---|---|---|
| LIR verification components | `compiler/lir/verify/lir-verifier.{h,cc}`, `compiler/lir/verify/translation-validator.{h,cc}`, `compiler/lir/CMakeLists.txt` | `ir-backend` |
| IR failure algebra detail extension | `compiler/ir/diagnostics/ir-failure.h` and its codec consumers | `ir-backend` |
| Object-emission gate | `utils/zomc/zomc.cc` (`emitBinary`) | `ir-backend` |
| Verifier and validator ztests | `tests/unittests/compiler/lir/lir-verifier-test.cc`, `tests/unittests/compiler/lir/lir-translation-validator-test.cc` | `verification` |
| Object-emission conformance suite | existing `tests/conformance` object-emission corpus now crosses the new gate | `verification` |
| IR design notes | `docs/design/ir/` LIR/backend notes identify verifier and validator as independent stages | `ir-backend` |
| RFC process record | `docs/rfc/0053-*.md`, `docs/rfc/README.md`, tracking document | `rfc` |

## Security And Safety Impact

The stages defend against miscompilation: a lowering defect that maps a
verified safe program onto wrong native code (swapped branch polarity, wrong
constant, redirected call target) is rejected before object emission. Because
the object-emission path already produces linked executables that run on the
host, silently wrong code generation is a direct memory-safety boundary
violation for programs relying on ownership checking. The design fails closed
under every malformed-input case, and the validators never execute user-
supplied logic or allocate based on source numeric values. The validators do
not weaken any unsafe boundary: they add evidence; they admit no new source
construct.

## Drawbacks And Risks

- **Triple maintenance of the lowering contract.** The construct mapping then
  exists in the producer, the structural verifier, and the translation
  validator. This is the same deliberate independence RFC 0007 uses for
  ownership proofs; the mitigation is that the relation table is small,
  construct-level, and extended only when a producer is added. A shared helper
  would defeat the purpose and is prohibited.
- **Validator brittleness as lowering generalizes.** A construct-driven
  relation (rather than a shape classifier) limits churn, but legalization and
  ABI lowering will require new relation rows and richer place/effect types.
  The RFC accepts that each new producer extends the relation in the same
  change; an unverified producer is prohibited rather than allowed through.
- **False rejection risk.** An overly strict structural rule could block a
  future legal producer. Every rule in this RFC is justified against a current
  or scheduled construct, and the reachability rule is the only rule without a
  current defect class; it is kept because all admitted shapes satisfy it and
  it catches stale-block defects, with relaxation explicitly allowed when a
  producer demonstrates the need.
- **Compile-time cost.** Two linear scans over small modules are negligible
  against LLVM translation; budgets prevent pathological growth.

## Alternatives Considered

### Validate Only Through LLVM `verifyModule`

Rejected. `verifyModule` validates LLVM IR structure, not the MIR-to-LIR
mapping, and it sees neither MIR nor semantic types. Well-formed LLVM IR can
encode the wrong branch targets, constants, and call indices. LLVM verification
stays as the downstream boundary; it cannot substitute for either new stage.

### SMT Translation Validation From The Start

Rejected for this slice. Alive2-style refinement pays for itself over
optimization rewrites and undef/poison-style semantics; LIR currently performs
no rewrites and has a closed scalar carrier algebra. The syntactic relation
exhaustively covers the existing defect classes at a fraction of the cost and
introduces no solver dependency. The effect abstraction is the future
insertion point for SMT refinement of arithmetic and comparison.

### Extend The Existing Per-Shape MIR Verifiers To Cover LIR

Rejected. Duplicating the 29-function shape-whitelist pattern one phase later
would recreate the architecture RFC 0048 is dismantling. The structural
verifier and the construct-walked correspondence are independent of whole-
function shapes and need no modification when the MIR shape rail is deleted.

### Byte-Parity Snapshots As The Correctness Mechanism

Rejected as insufficient. Byte parity against the previous implementation
detects drift but not shared defects and depends on the legacy producer it is
meant to replace. It remains a regression tool; semantic correspondence
becomes the correctness gate.

## Compatibility And Rollout

No user-visible surface changes; accepted source programs compile to identical
objects because both stages are evidence-only and pass on the current corpus.
Rollout is four ordered slices, each independently shippable and gated:

1. `LirStructuralVerifier` plus its positive and mutation ztests, not yet
   wired into emission.
2. Single-function translation relations (scalar initializer, field fold,
   whole-struct return, conditional, loop, comparison diamond) with ztests.
3. Module-level function correspondence and call-integrity relations for the
   two- and three-function call modules.
4. Fail-closed wiring in `emitBinary` plus the `docs/design/ir/` note; the
   full object-emission conformance corpus crosses the gate.

Rollback cost is one include and two calls in `emitBinary` plus the new files;
no IR codec, source syntax, or artifact format changes. The failure-algebra
detail extension is additive within the closed `LirVerification` phase.

## Documentation And Teaching Plan

- Update the LIR/backend IR design notes under `docs/design/ir/` to name the
  builder, the independent structural verifier, and the independent
  translation validator as three separate components with their inputs and
  failure phases, per the IR design-notes contract.
- No normative spec chapter changes: this is compiler-internal evidence
  infrastructure and adds no source construct.
- Contributor guidance in the same notes: a new lowering construct must add
  its structural rule and relation row in the same change, with a mutation
  test, or emission fails closed.

## Operational Readiness

- CI cost: two linear scans on the existing object-emission corpus; no new
  external tool or service.
- Failure triage: a validation finding names the fault tag and both contexts,
  sufficient to locate the producer arm without rerunning a debugger.
- No runtime, target, or release-process change; the gate runs on every host
  already supported by object emission (Linux x86-64).

## Acceptance Criteria

1. `LirStructuralVerifier` and `TranslationValidator` exist under
   `compiler/lir/verify/`, share no producer code, and are listed in the
   explicit CMake source list.
2. Every currently admitted lowering shape has a positive ztest that passes
   both stages.
3. Every structural fault and every translation fault has at least one
   mutation ztest that constructs the minimal malformed LIR module and proves
   rejection; mutating the LIR back makes it pass.
4. `emitBinary` runs both stages before LLVM translation and fails closed on a
   finding, with an internal incident failure and no output file.
5. The full object-emission conformance corpus, lit suite, hir-module-test,
   built-mir-test, lir tests, and both IR parity channels pass on the
   sanitizer preset.
6. The IR design notes document both stages, their inputs, and their failure
   phase.
7. `check-rfc`, `check-format`, `check-english-only`,
   `check-ir-architecture`, `check-diff-hygiene`, and the ownership
   architecture gate all pass.

## Implementation Plan

1. Declare the closed finding and fault types with canonical encoding and the
   `LirVerification` failure projection; land behind no call site.
2. Implement and ztest `LirStructuralVerifier` over the full current LIR
   algebra (slices 1).
3. Implement carrier derivation, slot/block mappings, and the per-construct
   effect relation for single-function shapes; positive and mutation ztests.
4. Add function-set correspondence and call-callee owner integrity for
   multi-function modules.
5. Wire both stages into `emitBinary` fail-closed; run the object-emission
   corpus and both parity channels serially on sanitizer builds.
6. Update IR design notes and the RFC tracking record; move status to
   IMPLEMENTING with the implementation tracker.

## Test Plan

- Build: `cmake --preset sanitizer` and `cmake --build --preset sanitizer`
  clean with ASan/UBSan/LSan enabled.
- Unit tests: new `lir-verifier-test` and `lir-translation-validator-test`
  ztests, including the mutation matrix (one fault per closed fault tag), run
  directly and under `ctest --preset default -L unittest`.
- Lit/conformance: the object-emission corpus crosses both stages through
  `zomc compile --emit=binary`; `ctest --preset default -L lit`.
- Regression: `hir-module-test`, `built-mir-test`, existing
  `lir-module-test`, `lir-algebra-codec-oracle-test`, and
  `llvm-translation-test` unchanged and green.
- Parity: both `scripts/check-ir-parity.py --check` channels (process and
  `--ir`) run serially with `ASAN_OPTIONS=detect_leaks=0` over the 923-source
  corpus and clean dumps.
- Gates: `check-rfc.py`, `check-format.py`, `check-english-only.py --check`,
  `check-ir-architecture.py --check`, `check-ownership-architecture.py
  --check`, `check-diff-hygiene.py --check`, and `git diff --check`.

## Open Questions

- Whether the validators should also be invoked from a future session-owned
  backend driver in addition to the CLI. Non-blocking: the design is driver-
  agnostic, and the invocation moves with the producer when RFC 0010 relocates
  lowering out of `zomc.cc`; no API change is anticipated.
- Whether finding payloads should carry rendered English for CLI output.
  Non-blocking: the library emits closed tags only (per the typed-failure
  rule), and CLI rendering is decided when the internal failure renderer for
  LirVerification is implemented in slice 4.

## Status History

| Date | Status | Notes |
|---|---|---|
| 2026-09-18 | DRAFT | Initial draft. |
| 2026-09-19 | DRAFT | Slice 1 implemented: `LirStructuralVerifier` with the closed structural fault set, positive coverage of every admitted producer shape through `llvm-translation-test`, and one mutation ztest per fault tag; not yet wired into `emitBinary`. |
