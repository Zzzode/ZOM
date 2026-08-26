# Lowerable Constructs Inventory (Frontend to Built MIR)

Updated: 2026-08-27. HEAD at authoring: `a0f894ab`.

This note is the O1/KR1.4 deliverable of `docs/plan/2026-q4.md`: an
evidence-backed enumeration of every source-language construct that currently
lowers **end-to-end to verified Built MIR**, with a measurable count against the
2026-08-25 entry-state baseline. It follows the required shape in
`docs/design/ir/README.md`.

## What "lowers end-to-end" means here

"End-to-end" is the **frontend-to-MIR boundary only**: a construct that is
admitted by surface admission, checked, assembled into a `VerifiedHirModule`,
and built into a `VerifiedBuiltMir` published by `CompilerSession`. It does
**not** mean "compiles to a native binary." There is no backend: no target LIR,
no ABI lowering, no LLVM IR, no object emission, no linking (see
`docs/design/ir/README.md` Status Matrix and `docs/plan/2026-q4.md` Entry state
lines 33-34). The last artifact any construct below reaches is verified Built
MIR.

## Role in the pipeline

- **Admitting filter:** `products/zomlang/compiler/ownership/surface-admission.cc`
  (`OwnershipSurfaceAdmissionBuilder::admit`). Each `isAdmitted*` predicate is a
  construct boundary; a shape that no predicate admits fails closed with an
  `OwnershipSurfaceFailure` before any HIR is built.
- **Live HIR builder / verifier / capability:** `hir::HirBuilder::build` ->
  `hir::HirVerifier::verify` -> `hir::VerifiedHirModule`
  (`products/zomlang/compiler/hir/hir-module.{h,cc}`).
- **Live MIR builder / verifier / capability:** `mir::BuiltMirBuilder::build` ->
  `mir::BuiltMirVerifier::verify` -> `mir::VerifiedBuiltMir`
  (`products/zomlang/compiler/mir/built-mir.{h,cc}`).
- **Session publisher / consumer access path:**
  `driver::CompilerSession::checkSources()` publishes atomically;
  `getVerifiedHirModules()` and `getOwnershipCheckedMirModules()[i].builtMir()`
  are the read paths (`compiler-session.h:216,222`).
- **Project-native tests:** the end-to-end tests live in
  `products/zomlang/tests/unittests/compiler/hir/hir-module-test.cc` (drives
  source through the real `CompilerSession` and asserts on
  `builtMir[0].builtMir().functions()`), plus
  `products/zomlang/tests/unittests/compiler/driver/session/compiler-session-test.cc`
  and `.../ownership/ownership-event-overlay-test.cc` for field-write and
  receiver-call MIR shapes.

Representation is not reachability: the MIR data model can encode projections,
statements, and terminators (`built-mir.h`) that no live construct emits. Only
the rows below are produced by the live builder and proven by the verifier.

## Inventory: constructs that lower end-to-end to Built MIR

Predicate line numbers are in `surface-admission.cc` at HEAD `a0f894ab`. Test
names are `ZC_TEST` labels; the file is abbreviated `hir-module-test.cc` unless
noted.

| # | Construct | Admitting predicate (file:line) | HIR shape | MIR shape | Covering test (end-to-end) |
|---|---|---|---|---|---|
| 1 | Module-scope scalar `let` / `const` declaration | not body-gated; admitted as non-function decl (walk in `admit`, `surface-admission.cc:766-820`) | `HirValueDeclaration` (`hir-module.h:336`) | `MirFunctionKind::ModuleInitializer` fn with `ModuleInitializerResult` local (`built-mir.cc:4710,4736`) | "HIR pipeline lowers one module-scope scalar let without AST identity" (l.379); "...retains the exact canonical scalar const value" (l.401) |
| 2 | Single scalar-literal `return` | `isAdmittedReturnValue` scalar-literal arm (`:266-268`) | `HirScalarLiteralExpression` + `HirReturnStatement` (`hir-module.h:75,308`) | one-block `Return` terminator with `MirConstantOperand` | "HIR pipeline lowers an admitted while loop" exit block (l.1341) exercises literal return; empty-module baseline "publishes an exact empty module" (l.308) |
| 3 | `return <ident>` (parameter or local reference) | `isAdmittedReturnValue` IdentExpr arm (`:269`) | `HirParameterReferenceExpression` / `HirLocalReferenceExpression` (`hir-module.h:153,112`) | `Return` with `MirOperand::copy/move` place-use | "HIR pipeline preserves a returned function local..." (l.576); "...lowers a two-arm parameter conditional" (l.1459) |
| 4 | Same-module direct call, scalar-literal or identifier args | `isAdmittedDirectCall` + `hasAdmittedArguments` (`:139-147, 119-137`) | `HirDirectCallExpression` w/ `HirDirectCallArgument` (`hir-module.h:206,214`) | `Call` terminator (0x03) with arg operands, normal edge | "HIR pipeline retains verified scalar direct call arguments" (l.452); "...lowers a direct call parameter argument to a place operand" (l.492) |
| 5 | Nominal aggregate initializer (`T { field: <lit> }`) | `isAdmittedAggregateInitializer` (`:276-319`) | `HirNominalAggregateExpression` (`hir-module.h:92`) | `MirRvalueKind::NominalAggregate` (0x02) assign | "HIR pipeline lowers a local nominal aggregate field projection" (l.1175) |
| 6 | Aggregate field read (`return cell.value`) | local-body field return (`:562-567`) | `HirLocalFieldProjectionExpression` (`hir-module.h:121`) | place with `MirProjectionKind::Field` (0x01) in `Return` | compiler-session-test "PublishesVerifiedOwnershipInputsForAggregateFieldOverwrite" (l.1200) |
| 7 | Aggregate field write (`cell.value = <lit>`) | `isAdmittedExpressionStatement` MemberExpr target (`:88-91`); body write loop (`:624-628`) | `HirLocalWriteStatement` with `field` set (`hir-module.h:134`) | `Assign` w/ Field projection, `Overwrite` init | compiler-session-test same test (l.1200), asserts `Overwrite` + Field projection |
| 8 | N sequential `let` locals, scalar/aggregate/ident initializers | `isAdmittedFunctionBody` all-leading-lets branch (`:498-531`) | multiple `HirLocalBinding` + refs (`hir-module.h:102`) | one block, chained `StorageLive`/`Assign`/`Return` | "HIR pipeline lowers a three-binding sequential local body" (l.615) |
| 9 | Primitive binary local initializer (`let x = a + b`) | `isAdmittedPrimitiveBinary` via `:525` | `HirPrimitiveBinaryExpression` (`hir-module.h:266`) | `Arithmetic` (0x04) or `Comparison` (0x03) rvalue | "HIR pipeline lowers a binary-initializer sequential local body" (l.673) |
| 10 | Nested one-level binary operand (`let z = a + b * c`) | `isAdmittedPrimitiveBinary` `isNested` (`:246-263`) | inner `HirPrimitiveBinaryExpression` -> synthesized Temporary | two chained `Arithmetic` assigns via Temporary local | "HIR pipeline lowers a nested-operand binary sequential local body" (l.792) |
| 11 | Mutable-local scalar-literal write (`mut x=0; x=1;`) | body write loop, scalar value (`:610-621`) | `HirLocalWriteStatement` `Overwrite` (`hir-module.h:134`) | `Assign` `Overwrite` with `MirConstantOperand` | "HIR pipeline lowers a scalar-literal mutable local write unchanged" (l.974) |
| 12 | Mutable-local parameter-reference write (`x = a`) [G6a-1] | body write loop, ident value (`:615,619-622`) | `HirLocalWriteStatement` value = param ref | `Assign` `Overwrite` with parameter place-use | "HIR pipeline lowers a parameter-reference mutable local write" (l.893) |
| 13 | Mutable-local binary write (`x = a + b`) [G6a-2/KR1.1] | body write loop, `binaryValue` (`:616-618`) | `HirLocalWriteStatement` value = `HirPrimitiveBinaryExpression` | `Assign` `Overwrite` with `Arithmetic`/`Comparison` rvalue | "HIR pipeline lowers a binary mutable local write" (l.1026); "...with a literal operand" (l.1126) |
| 14 | Return-position relational comparison (`return a < b`) | `isAdmittedPrimitiveBinary` via `:272` | `HirPrimitiveBinaryExpression`, bool result | `Comparison` (0x03) rvalue to bool temp, `Return` | "HIR pipeline lowers a return-position relational comparison" (l.1802); all six ops (l.1861) |
| 15 | Return-position arithmetic/bitwise (`return a + b`) [G1] | `isAdmittedPrimitiveBinary` via `:272` | `HirPrimitiveBinaryExpression`, operand-typed result | `Arithmetic` (0x04) rvalue, `Return` | "HIR pipeline lowers a return-position arithmetic operation" (l.1901); all twelve ops (l.1954) |
| 16 | Two-arm diamond conditional (bool param / comparison cond) | `isAdmittedConditionalBody` (`:394-439`) | `HirConditionalExpression` (`hir-module.h:284`) | `SwitchInt` (0x05) entry + per-arm `Return`, four-block diamond | "HIR pipeline lowers a two-arm scalar-literal conditional" (l.1409); equality (l.1510); less-than (l.1572); param-and-literal (l.1745) |
| 17 | Minimal reducible `while` loop (bool param cond, empty body) | `isAdmittedLoopStatement` (`:377-392`) | `HirLoopStatement` (`hir-module.h:299`) | four-block reducible CFG: `Goto` (0x04) + `SwitchInt` (0x05) back-edge | "HIR pipeline lowers an admitted while loop" (l.1341), asserts 4-block MIR |
| 18 | Direct call as local initializer (`let v = helper();`) | `isAdmittedLocalInitializer` call arm (`:321-327`) | `HirLocalBinding` init = `HirDirectCallExpression` | `Call` terminator writing the local | "HIR pipeline retains a direct-call local initializer" (l.551) |
| 19 | Parameter reborrow (`return &*p`) | `isAdmittedReferenceReborrow` (`:166-189`) | `HirParameterReborrowExpression` (`hir-module.h:175`) | `BorrowCreation` (0x04) statement + `Return` | compiler-session-test "reborrow(value: &i32)" (l.712), asserts `BorrowCreation` Shared |
| 20 | Local borrow (`let y=p; return &*y`) | `isAdmittedLocalBorrow` / reborrow of local (`:191-205`) | `HirLocalBorrowExpression` (`hir-module.h:189`) | `BorrowCreation` from local place | compiler-session-test "reborrow ... let local = value" (l.1084) |
| 21 | Unsafe block wrapping a scalar/reborrow tail (`unsafe { &*p }`) | `isAdmittedReturnValue` UnsafeBlockExpr arm (`:273`) | `HirUnsafeBlockExpression` (`hir-module.h:247`) | `UnsafeScopeBoundary` Enter/Exit (0x07) around the value | "HIR pipeline lowers an unsafe block wrapping a parameter reborrow" (l.1298) and two siblings (l.1305, l.1323) |
| 22 | Mutable-receiver method call (`return cell.read(1)`) | `isAdmittedReceiverCall` (`:149-164`) | `HirReceiverCallExpression` (`hir-module.h:228`) | `Call` w/ `MirCallEffectKind::ActivateMutableReceiver` | ownership-event-overlay-test "records mutable receiver activation on normal call edge" (l.4178) |

### Coverage note

Every row above has a project-native test that drives source through the live
`CompilerSession` and asserts on the published Built MIR (`builtMir()`), except
that row 2's dedicated single-literal-return assertion is covered indirectly
through the loop-exit block and the empty-module baseline rather than a
standalone "one function returns one literal" MIR assertion; the HIR-level
literal-return path is asserted directly. No row is "lowered but untested":
receiver calls (row 22) and borrows (rows 19-21), which the plan prose grouped
under call-dispatch widening, in fact already reach Built MIR for the single
admitted shape and are asserted at the MIR level in the ownership/session suites.

## Baseline vs current (measurable delta)

**Baseline (2026-08-25, plan Entry state lines 26-30).** The plan prose lists
these lowered shapes:

1. scalar local writes/transfers
2. nominal aggregate init and field writes
3. same-module direct calls (scalar-literal or parameter args)
4. four-block diamond conditionals (relational `Eq/Ne/Lt/Le/Gt/Ge`, two scalar
   operands)
5. returned comparison results
6. a minimal reducible `while` loop

That is **6 baseline shape families**. Expanded against the current
predicate/HIR/MIR granularity (and folding in the borrow/reborrow/unsafe/
receiver shapes that already existed at baseline but were omitted from the prose
bullet), the baseline already covered roughly rows 1-8, 14, 16-22.

**Current (2026-08-27, HEAD `a0f894ab`).** 22 distinct admitted constructs
enumerated above.

**Delta since the 2026-08-25 baseline prose** - the net-new lowerable constructs
added by the Q4 O1 slices, with per-commit attribution:

| Construct (row) | Slice | Commit |
|---|---|---|
| Return-position arithmetic/bitwise binary (row 15) | G1 | `2ed20c94` |
| N sequential `let` locals with sequential dataflow (row 8) | G2 slice 1 | `3f7f5189` |
| Primitive binary as a local initializer (row 9) | G2 slice 2 | `76eaa630` |
| Nested one-level binary operand (row 10) | G5 | `0e9e030d` |
| Mutable-local parameter-reference write (row 12) | G6a-1 | `4e63e322` |
| Mutable-local binary-operation write (row 13) | G6a-2 / KR1.1 | `4356d062` |

That is **+6 net-new admitted constructs** landed this quarter on the
frontend-to-MIR path (measurable growth, not a vibe). The baseline prose's
bullet 5 ("returned comparison results", row 14) predates G1; G1 extended it
from comparison-only to the full twelve arithmetic/bitwise operators (row 15),
which is why row 14 and row 15 are counted separately.

## NOT yet lowered (fail-closed) - nearby rejected forms

Each of these is structurally near an admitted construct but fails closed today,
with the reason:

- **Non-empty `while` body (mutating locals).** As of G6b/KR1.2 (`d49fc586`) a
  `while` whose body is a sequence of admitted mutable-local writes lowers to a
  reducible four-block CFG (`HirLoopStatement` gained a `body` node-id list). No
  longer a gap. Still fail-closed: a loop body containing a call, a nested loop,
  a `return`, or any non-write statement, and a non-parameter (e.g. literal)
  loop condition.
- **General method calls / generic-call dispatch.** `hasAdmittedArguments`
  admits a list of scalar/ident args structurally. As of KR1.3 (`7564b86a`) a
  same-module direct call with N arguments lowers end-to-end (a multi-parameter
  callee lowers too), so the earlier single-argument ceiling is closed. Still not
  driven to MIR: general receiver-method dispatch beyond the one admitted
  `cell.read(1)` shape, and generic-call dispatch. Tracked under RFC 0009.
- **Two-level or dual-nested binary operands.** `isAdmittedPrimitiveBinary`
  (`:257-263`) rejects both-operands-nested and two-level nesting; only one
  operand, one level nests (row 10).
- **Literal-vs-literal binary / comparison.** `isAdmittedPrimitiveBinary` and
  `isAdmittedConditionalBody` require at least one identifier operand
  (`:263, :414`): a constant-only op has no parameter to lower and would
  constant-fold, so it fails closed.
- **Logical `&&` / `||`.** Admitted structurally as a `BinaryExpr` but the
  checker leaves the production unsupported (no `PrimitiveOperation`) and fails
  closed with `CheckerMissingRequiredFact`. Regression-guarded by
  compiler-session-test "RejectsLogicalInitializerInSequentialLocalBody"
  (l.1315).
- **Error operators `?!` / `!!`.** `isAdmittedErrorPostfix` (`:207-222`) admits
  the postfix at the surface, but they are **spec-blocked**: no error-union
  value type exists, so a raising call forms no callable shape and
  `ErrorUnionShapeFact` has zero producers (plan KR3.3 DROP verdict,
  `body-checker.cc:1233`). Blocked on RFC 0006, not a frontend slice.
- **`spawn`, `suspend`, `match`, `for`/`for-in`/`do-while`, `break`/`continue`,
  labeled statements, void `return`.** All hard-rejected in `admit`
  (`surface-admission.cc:772-808`) as un-admitted surface syntax; no HIR or MIR
  shape exists for them.

## Known gaps

- The single-argument ceiling on direct calls is closed as of KR1.3
  (`7564b86a`): a same-module direct call with N arguments lowers end-to-end, and
  a multi-parameter callee lowers too, so admission and proven lowering agree for
  direct calls. Remaining call gap: general receiver-method dispatch beyond the
  one admitted `cell.read(1)` shape and generic-call dispatch, tracked in the
  RFC 0009 workstream, not here.
- No ownership proof, executable MIR, LIR, LLVM, or native artifact consumes any
  construct above (`docs/design/ir/README.md` Status Matrix). "End-to-end" stops
  at `VerifiedBuiltMir`.
