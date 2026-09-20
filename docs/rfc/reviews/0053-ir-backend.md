# RFC 0053 Owner Review — Technical Soundness Of The LIR Verifier And Translation Validator

Reviewer: `ir-backend` (owner of `compiler/lir/**`, `compiler/ir/diagnostics/**`, `utils/zomc/**`)
Date: 2026-09-20
Review target: `docs/rfc/0053-lir-structural-verification-and-translation-validation.md`
Reviewed revision: `status: DRAFT`, `updated: 2026-09-19`, four landed slices
Reviewed tree: `develop` @ `e0cb57da` (clean)
Method: source reading plus an **out-of-tree adversarial probe** that links the
landed `libfrontend.a` and drives `LirStructuralVerifier::verify` and
`TranslationValidator::validate` directly through the public MIR/LIR algebras.
All probe results below are reproduced output, not inference.

## Verdict

**REQUEST-CHANGES**

The architecture is right and the placement is right: two independent
per-construct stages, no shared producer helper, fail-closed ordering before
`llvm::verifyModule`, and the end-to-end corpus genuinely crosses both gates.
I re-ran the evidence myself: `lir-verifier-test` 16/16 and
`lir-translation-validator-test` 14/14 pass on the sanitizer build, and
`object-emission-cli` + `native-run-cli` pass on the LLVM-configured build
(13.59 s / 3.12 s, 2/2).

The blockers are that **the validator's central soundness claim is narrower
than the RFC states**, and that **the validator is not total over its declared
input type**: it aborts the process instead of returning a finding, and in a
release build it reads the wrong `OneOf` variant (UB) rather than failing.

Blocking findings: **2**. Non-blocking (NIT) findings: **7**.

Cross-reference: the `rfc` owner review (`docs/rfc/reviews/0053-rfc.md`) already
raises the missing `IrFailurePhase::LirVerification` projection as its B1. I do
not re-count that as my blocker, but I add the technical evidence it needs under
N6 below, and B2 here has a second, independent consequence for it.

---

## Blocking Findings

### BLOCKING 1. The `SwitchInt` arm *value* is never checked, so the validator accepts a semantically inverted module

**Where.** `compiler/lir/verify/translation-validator.cc:508-535`, specifically
the arm correspondence at `:526-534`:

```cpp
// The first arm is the true arm and maps to the LIR true target; the
// default maps to the false target; every later arm must share the
// default target (a one-true-arm loop or a two-arm true/false diamond).
if (lirTerminator.condTrueTarget().ordinal() != switchInt.arms[0].target.ordinal() ||
    lirTerminator.condFalseTarget().ordinal() != switchInt.defaultTarget.ordinal()) {
  return fault(TranslationFaultKind::EdgeTargetMismatch, functionIndex, b + 1, b + 1);
}
```

`switchInt.arms[a].value` is read nowhere in this file or in
`lir-verifier.cc`. `grep -n "booleanValue" compiler/lir/verify/*.cc` returns
nothing. The code asserts *positionally* that `arms[0]` is the true arm; it never
proves it.

**RFC claims contradicted.** The effect-relation row at RFC line 354 says

> `Terminator::SwitchInt(bool place, then/else)` | `CondBranch(placeSlot, then', else')`; **polarity preserved**

and the Guide-Level Explanation (line 193) promises the validator proves "the
branch polarity", and line 196 states the validator is what catches a swapped
true/false pair. The landed code preserves polarity only because the *MIR shape
rail* independently pins the arm values: `validConditionalReturnFunction`
(`compiler/mir/built-mir.cc:1809-1815`), `validEqualityConditionalReturnFunction`
(`:1810-1818`) and `validLoopReturnFunction` (`:2111-2113`) all explicitly
require `arms[0].value.booleanValue() == true` and (for the diamonds)
`arms[1] == false`. The validator inherits that guarantee; it does not establish
it.

**Concrete counterexample, observed.** Out-of-tree probe, `PROBE-A`:

```
PROBE-A: structural=PASS translation=ACCEPT(!!)
```

The probe builds a MIR four-block diamond whose single arm is
`{value = false -> bb2}`, `default = bb3` (so the program takes `bb2` when the
condition is **false**), with the faithful LIR
`CondBranch(cond=1, TRUE -> bb2, FALSE -> bb3)`, and the validator **accepts**.
The two programs compute different results for every input; no stage objects.
`PROBE-B` confirms the same module with `arms[0].value = true` also accepts, so
the validator is not distinguishing the two cases at all.

**Severity and why this is blocking rather than speculative.** Two independent
reasons. First, the RFC's stated design goal is that the correspondence is
derived "by walking MIR constructs, not by duplicating the whole-function shape
classifiers, so the validator generalizes as lowering generalizes and does not
become a third shape rail" (Goal 3, line 78-79), and RFC 0048 Phase 5 deletes
the MIR shape rail that currently supplies this property. The validator will
survive that deletion and silently lose the guarantee it advertises. Second,
`TranslationValidator::validate(const mir::MirFunction&, ...)` is a **public
library API with a `const MirFunction&` parameter**; it declares no
precondition, checks none, and is documented in `translation-validator.h:77-92`
as proving "the LIR module preserves the semantics of the verified Built MIR
functions". It does not, for this construct, on any input whose arm value is
not `true`. A verification component whose contract is false as stated is a
defect in the component, not only in the prose.

**Required fix (one of).**

- (a) Add the check: require `switchInt.arms[0].value.booleanValue()` to be
  present and `true`, and every further arm present and equal to the default
  target's meaning — i.e. read the arm value and reject a `false` value on a
  target mapped to `condTrueTarget`. Add the corresponding mutation ztest
  (`EdgeTargetMismatch` or a new closed tag) whose mutation is exactly
  `arms[0].value = false`; and
- (b) update `docs/design/ir/lir.md` ("switch polarity") to state that the arm
  value *is* checked once (a) lands.

If the author instead wants to keep the positional convention, then the RFC line
354 row must say "target correspondence; the arm value is assumed `true` by the
MIR producer and is not re-checked", and Goal 3 plus the `Drawbacks And Risks`
"unverified producer is prohibited" sentence (line 459) must be amended to admit
that one MIR invariant is trust-inherited.

---

### BLOCKING 2. `MirOperand::constantValue()` is called without a `Constant` guard: the validator aborts the process instead of returning a finding, and in release builds it is undefined behaviour

**Where.** Six unguarded call sites in
`compiler/lir/verify/translation-validator.cc`:

| Line | Expression | Guard present? |
|---|---|---|
| 259 | `element.operand.constantValue().type` (field-fold carrier) | no |
| 262 | `aggregate.elements[0].operand.constantValue().type` | no |
| 264 | `aggregate.elements[0].operand.constantValue().type` | no |
| 457 | `aggregate.elements[e].operand.constantValue().type` (bundle return) | no |
| 472 | `element.operand.constantValue().type` (projected field) | no |
| 574 | `call.arguments[a].constantValue().type` (call argument) | no — the `kind() != Constant` test is the **second** disjunct |

Line 574 is the clearest:

```cpp
const auto carrier = integerCarrier(call.arguments[a].constantValue().type, types);
if (call.arguments[a].kind() != mir::MirOperandKind::Constant || carrier == zc::none || ...
```

The `constantValue()` call precedes the `kind()` check that would have justified
it. By contrast line 347-349 and line 381 do guard correctly, so this is an
inconsistent application of one rule, not a design choice.

`MirOperand::constantValue()` is `value.get<MirConstantOperand>()`
(`compiler/mir/built-mir.cc:234-236`), and `OneOf::get<T>()` is
(`libraries/zc/core/one-of.h:1701-1704`):

```cpp
const T& get() const& {
  ZC_IREQUIRE(is<T>(), "Must check OneOf::is<T>() before calling get<T>().");
  return *reinterpret_cast<const T*>(space);
}
```

**Concrete counterexamples, observed.** Two probes were used; both MIR inputs
are constructible through the public `mir::MirFunction` algebra and differ from
an admitted shape in exactly one field.

- `PROBE-D`: a MIR two-block caller whose single `Call` argument is a
  `MirOperand::copy` place rather than a constant, validated against a
  plausible (constant-argument) LIR module. Output:

  ```
  PROBE-D: about to call validate with a non-constant call arg
  *** Fatal uncaught zc::Exception: core/one-of.h:1702: failed: expected is<T>(); Must check OneOf::is<T>() before calling get<T>().
  ```

  Process exit status **1**; no `TranslationFinding` is ever returned.

- `PROBE-E`: a MIR single-block whole-struct return whose
  `MirNominalAggregate` carries one `MirOperand::copy` element, validated
  against a plausible aggregate-return LIR module. Same fatal message, same
  exit status.

**Two distinct defects, both blocking.**

1. **The declared fail-closed rail does not exist for these inputs.**
   `TranslationValidator::validate` is declared `noexcept`
   (`translation-validator.h:119` for the multi-function overload, `:102` for
   the single-function one). An exception escaping a `noexcept` function calls
   `std::terminate`; zomc installs a handler that prints the message above and
   exits. This violates the RFC's contract directly:
   `Failure Projection` (line 379-388) and `Acceptance Criteria` 4 (line 551)
   require a **finding**, and `Non-Goals` (line 100-102) plus the whole design
   rest on "a verification failure on already verified MIR is a compiler
   invariant failure" reported through the internal failure path. The observed
   behaviour is an uncontrolled crash with no fault tag and no context — worse
   than the `zc::String` rail `zomc.cc:1547-1564` currently uses, and it is not
   the incident rail either.

2. **In a release build the same path is undefined behaviour, not a crash.**
   `build-release/compile_commands.json` shows the release preset compiles
   `compiler/lir/**` with `-O3 -DNDEBUG`. `libraries/zc/core/common.h:168-180`
   then defines `ZC_NDEBUG` and not `ZC_DEBUG`, and `:351` makes `ZC_IREQUIRE`
   expand to **nothing**. `get<T>()` degrades to a bare
   `*reinterpret_cast<const T*>(space)` over the wrong variant. The
   `MirCopyOperand`/`MirMoveOperand`/`MirConstantOperand` alternatives have
   different layouts, so the validator reads a `MirPlace` as a
   `MirConstantOperand` and continues with a garbage `SemanticTypeId`. There is
   no fail-closed guarantee at all on the release configuration: the module is
   either rejected by accident or, if the garbage type happens to resolve, can
   be **accepted**. This is the exact property RFC 0053 exists to provide.

**Reachability.** Not reachable through `zomc` today, because the MIR shape rail
guarantees constant call arguments and constant aggregate elements before the
validator runs. It is reachable through the public API, it is reachable the
moment RFC 0048 Phase 5 generalizes the MIR producers (the RFC's own
`Drawbacks And Risks` names this risk at line 455-459), and it is reachable in
any future session-owned driver that calls the library directly (the RFC's
`Open Questions`). A verification component must be total over the types it
declares, not over the subset one current caller happens to feed it.

**Required fix.** Guard every `constantValue()` call with
`operand.kind() == mir::MirOperandKind::Constant` and return the appropriate
existing fault (`EffectMismatch` for a construct the relation does not admit;
`ConstantMismatch` for a non-constant where a constant is required), so both
stages are total and never throw. Add the two probes above as negative ztests
(`ConstantMismatch`/`EffectMismatch` on a non-constant call argument and on a
non-constant aggregate element). Independently, remove `noexcept` from the two
`validate` declarations or make the totality an explicit precondition —
`noexcept` plus `ZC_IREQUIRE` is a `std::terminate` on any missed invariant,
which is not a fail-closed design.

**This finding also strengthens `rfc`'s B1.** With the projection unimplemented,
a missed invariant is not merely mis-railed; it terminates the process. That
makes implementing the projection (or the rewritten internal-error rail) a
prerequisite for this stage being trustworthy, not a documentation cleanup.

---

## Non-Blocking Findings (NIT)

### N1. The structural verifier performs no carrier-*domain* check; `Float` and `Pointer` carriers pass

`lir-verifier.cc` checks carrier **equality** at every use
(`:213-224`, `:252-255`, `:264`, `:273`, `:291`, `:318`, `:334-339`) and checks
`Integer` explicitly only for `Compare` destinations (`:229-233`),
`CondBranch` conditions (`:301-304`) and `ReturnAggregate` slots (`:286-289`).
Nothing requires an `Assign` destination, an operand, a `ReturnLocal` slot or a
function `returnCarrier` to be an `Integer`.

Observed (`PROBE-C`): a two-block function whose parameter, local and return
carriers are all `ValueType::floating(Binary32)`, assigning `localUse(1)` to slot
2 and returning slot 2, is **accepted**:

```
PROBE-C float-only module: structural=ACCEPT(!!)
```

The RFC claims the verifier validates "every invariant of the current closed
`lir::Module` algebra" (Goal 1, line 70-71) and rule 6 is titled "Carrier
consistency using the closed `ValueType` algebra" (line 274). `Float` is part of
that closed algebra (`compiler/lir/lir-store.h:31-50`). Downstream, the LLVM
translator builds carrier types with
`integerType(carrier.integerWidth())` (`llvm-translator.cc:190`), which for a
`Float` carrier silently reads the default `IntegerBitWidth::Bit1` — a
well-formed but wrong LLVM function.

Not blocking because no producer can emit a non-integer carrier today
(`integerCarrier`/`boolCarrier` in both `mir-to-lir.cc` and
`translation-validator.cc` return only `Integer`). Either restrict the checked
domain to `ValueTypeKind::Integer` in `lir-verifier.cc` (one predicate, applied
where `ReturnAggregate` already applies it) or state in the RFC that the
verifier checks carrier *consistency* only and that the integer domain is a
producer obligation.

### N2. Reachability is a naive fixpoint, not the linear scan the RFC's budget claims

`lir-verifier.cc:121-162` implements reachability as
`while (changed) { for each block ... }`, which is `O(B · E)` in the worst case.
`Budget And Termination` (line 412-413) states "Structural verification is
`O(F + B + S + E)`; reachability visits each edge once". The second clause is
not true of this implementation. The symbol-uniqueness check at `:54-63` is also
`O(F²)` string comparisons, which the budget does not mention.

Harmless at the current three-function ceiling, but the RFC's budget is stated
as a checkable contract and two of its clauses do not hold. Either fix the
implementation (a worklist) or restate the bound.

### N3. Two admitted lowering shapes have no direct unit test of either stage

`grep -rn "lowerCallModuleWithArguments\|lowerCallModuleWithLeaf" tests/`
returns **zero** hits. RFC `Acceptance Criteria` 2 (line 546-547) requires "Every
currently admitted lowering shape has a positive ztest that passes both stages".
The two-argument call and the three-function-with-leaf module are exercised only
through `object-emission-cli` (`tests/CMakeLists.txt:136-167`, `call2arg-consumer`
and `call-leaf-consumer`), which does run both gates but asserts only ELF magic
and exit status. `buildCallWithLeafModule` (`llvm-translation-test.cc:1452-1521`)
builds the LIR module directly and calls only the translator — it never calls
`TranslationValidator::validate`, so the multi-function overload's owner matching
is not directly asserted for that shape. The two-function overload *is* directly
asserted (`lir-translation-validator-test.cc:480-557`).

### N4. Translation-fault test coverage is per-tag, not per-branch

All nine `TranslationFaultKind` tags have at least one rejecting ztest
(verified by reading `lir-translation-validator-test.cc`), which satisfies
`Acceptance Criteria` 3 literally. But the aggregate branches of the validator
— the fold-carrier derivation (`:249-272`) and the whole-struct and
projected-field return arms (`:445-483`) — have no dedicated mutation test. The
tags they would produce (`ConstantMismatch`, `EffectMismatch`,
`PlaceMappingMismatch`) are already "covered" by unrelated scalar and diamond
tests, so a regression confined to the aggregate relation would pass the suite.
Given that `FoldKind::Aggregate` is the most intricate branch in the file (it
re-implements the producer's element-order, field-projection and placeholder-
carrier contracts), it warrants one mutation per arm.

### N5. The "wrong owner" negative test asserts a disjunction, weakening the rejection

`lir-translation-validator-test.cc:509-513`:

```cpp
const auto fault = ZC_ASSERT_NONNULL(finding).fault;
ZC_EXPECT(fault == TranslationFaultKind::CallCalleeMismatch ||
          fault == TranslationFaultKind::FunctionSetMismatch);
```

The test is named "rejects a call whose index resolves to the wrong owner" and
the RFC names `CallCalleeMismatch` as that fault (line 355 and the "Module-level
call integrity" paragraph, line 371-377). Accepting either fault means the test
would still pass if the call-integrity check regressed and the module were
rejected only by the weaker function-set comparison. Assert the single intended
tag; the sibling test at `:516-557` already constructs a module where the
function set matches and only the callee index is wrong, so the strict form is
clearly achievable.

### N6. The production failure is a `zc::String`, not the incident rail, and the CLI reports it as a usage error

Cross-referenced to `rfc` B1 rather than re-counted, with the technical evidence.

`utils/zomc/zomc.cc:1547-1564` builds
`"Internal compiler error: LIR structural verification failed (fault N)."` and
returns it as a `NativeObjectResult` string. `emitNativeObject` (`:1579-1592`)
turns that into a `Validity` with an error message, which `MainBuilder` routes
to `usageError` (`libraries/zc/core/main.cc:413-431` → `:581`), producing
`"zomc: <message>\nTry 'zomc --help' for more information."`. Two consequences
worth recording for the fix:

- the fault tag is printed as a **raw integer** (`static_cast<unsigned>`), not as
  the closed tag the RFC specifies, and neither `mirContext` nor `lirContext` is
  printed at all, so `Operational Readiness`'s triage claim (line 536) is not
  met;
- the process exits through the **usage-error** path, so an internal compiler
  defect is presented to the user as a command-line mistake.

The stage does still fail closed in the ordering sense — no object bytes are
written and `llvm::verifyModule` (`llvm-translator.cc:407`) is never reached,
because `zomc run` goes through the same `buildNativeObject` (`zomc.cc:1636`),
so both binary paths cross the gate. That part of the claim holds.

There is also no test that the gate rejects end to end: `check-object-emission.py`
`--reject-case` entries exercise `lir == zc::none` (four-function, two-caller,
non-leaf), not a verification finding. `Acceptance Criteria` 4 ("fails closed on
a finding, with an internal incident failure and no output file") has no
executable evidence at the CLI level.

### N7. `NonDenseBlockOrdinals` reports the expected ordinal, not the observed one

`lir-verifier.cc:76-80` passes `expected` (= `i + 1`) as the finding's
`blockOrdinal` in both the `DuplicateBlockOrdinal` and `NonDenseBlockOrdinals`
cases. For the non-dense case the finding therefore names a block ordinal that
does not appear in the module, which is the opposite of what the field is
documented to mean (`lir-verifier.h:65-67`: "the one-based LIR block ordinal").
Report `blocks[i].id().ordinal()` for the non-dense case; the duplicate case
happens to coincide.

---

## Verified Claims (No Finding)

Checked against the landed tree with real execution:

- **Placement and independence.** `compiler/lir/verify/lir-verifier.{h,cc}` and
  `compiler/lir/verify/translation-validator.{h,cc}` exist; both `.cc` files are
  listed explicitly in `compiler/lir/CMakeLists.txt:6-7` (no glob). Neither the
  headers nor the `.cc` files include `compiler/lir/mir-to-lir.h`, and carrier
  derivation, bit materialization and place mapping are genuinely re-implemented
  (`mir-to-lir.cc` `integerCarrierFor`/`boolCarrierFor`/`zeroExtendedBits` versus
  `translation-validator.cc` `integerCarrier`/`boolCarrier`/`zeroExtendedBits`,
  with the duplication stated as deliberate at `:37-39`).
- **Fail-closed ordering.** `LirStructuralVerifier::verify` at `zomc.cc:1547`,
  `TranslationValidator::validate` at `:1559`, `translator.translate` at `:1566`,
  `llvm::verifyModule` at `llvm-translator.cc:407`. Reachable from both
  `zomc compile --emit=binary` and `zomc run`.
- **Not a user diagnostic.** No `ZOMxxxx` code is selected on either path; the
  classification is correct even though the rail is not (N6).
- **The order-preserving lockstep is sound for the admitted shapes except
  BLOCKING 1.** I re-derived the correspondence for the scalar fold, the
  equality diamond, the conditional diamond, the reducible loop, the comparison
  diamond and the 0/1/2-argument call modules against the producer, and the
  validator rejects every mutation I could construct within the admitted input
  set:
  - swapped LIR branch targets → `EdgeTargetMismatch` (probe);
  - swapped arm constants → `ConstantMismatch` (probe);
  - wrong folded scalar constant → `ConstantMismatch` (in-tree test);
  - wrong comparison operator → `OperatorMismatch` (in-tree test);
  - dropped arm assignment → `EffectMismatch` (in-tree test);
  - extra declared slot / missing join / wrong return local / wrong call index
    → `SlotSetMismatch` / `BlockBijectionMismatch` / `PlaceMappingMismatch` /
    `CallCalleeMismatch` (in-tree tests).
  The block bijection, the dense-ordinal terminator mapping, the independent
  zero-extension of canonical magnitudes and the owner-keyed call resolution are
  each a genuine second implementation, not a re-export of the producer.
- **Structural verifier completeness against the malformedness classes I
  enumerated.** Dangling block target (including a zero-ordinal invalid
  `LirBlockId`, via `!target.isValid()` at `:87`): checked, `:86-118`.
  Duplicate block ordinal: checked, `:75-77` (and unreachable by construction
  given the density rule). Non-dense block ordinals: checked, `:78-80`. Missing
  entry block: checked, `:70-72`. Unreachable block: checked, `:121-162`.
  Undeclared slot: checked on every statement destination, operand, condition,
  return and call destination (`requireSlot`). Duplicate or
  parameter/local-range-overlapping slot ordinals: **not** separately checked,
  but unreachable, because `:168-177` requires each declared ordinal to equal its
  dense position, which implies uniqueness and disjoint ranges.
  Terminator arity: checked for `Call` against the callee parameter count
  (`:330-332`). Carrier consistency: checked (see N1 for the domain gap).
  Fallthrough: not applicable — the closed terminator algebra has no fallthrough
  case, every block ends in one of six terminators.
- **Tests pass, as run by me.** `lir-verifier-test` 16/16 and
  `lir-translation-validator-test` 14/14 on
  `build-sanitizer` with `ASAN_OPTIONS=detect_leaks=0`;
  `object-emission-cli` 13.59 s and `native-run-cli` 3.12 s on `build-llvm`
  (real host execution, exit 42), 2/2 passed.
- **Gates.** `check-rfc.py` (53 proposals), `check-ir-architecture.py --check`
  and `check-english-only.py --check` pass. `check-format.py` reports one
  unformatted file, `compiler/checker/facts/signature-facts.cc`, which is **not**
  part of RFC 0053 (it belongs to the `b7ac2ca1`/`bfab3a9d` checker commits) and
  is a working-tree change made by another agent during this review session, not
  a landed defect; all eight RFC 0053 source and test files are clean under
  `clang-format --style=file`. `check-no-internal-versioning.py --check` reports
  findings, all of them the scanner matching the sibling marker tokens inside
  review prose that names those tokens: two in the peer review
  `docs/rfc/reviews/0053-rfc.md:304` and four in an earlier draft of this file.
  Every RFC 0053 source and test file is clean, and no finding describes an
  actual internal type or artifact. Both reviews should spell the forbidden
  spellings out in a form the scanner does not match (e.g. "internal-contract
  generation suffix") when they need to refer to them, so the gate stays
  meaningful. This is a gate-hygiene note, not a defect in RFC 0053.

---

## Summary For The Author

Two changes are required before this RFC can record an owner approval from
`ir-backend`:

1. Check the `SwitchInt` arm value in `translation-validator.cc:526-534`, or
   downgrade the "polarity preserved" claim at RFC line 354 and the design note;
2. Guard the six `constantValue()` call sites listed under BLOCKING 2 so both
   stages are total, drop or justify the `noexcept`, and add the two negative
   ztests.

The NITs can land with the same change or be recorded as follow-ups; N1, N2 and
N7 are correctness-adjacent and cheap, N3-N6 are test- and rail-quality items
that overlap the `rfc` and `verification` reviews.

## Sources

- Regehr et al., "Translation Validation for LLVM's AArch64 Backend",
  <https://www-old.cs.utah.edu/~regehr/papers/arm-tv.pdf>
- LLVM `verifyModule` documentation,
  <https://llvm.org/docs/LangRef.html#module-structure>
- Cranelift verifier (`cranelift-verify`), <https://github.com/bytecodealliance/wasmtime>
