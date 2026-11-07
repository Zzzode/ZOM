# RFC 0058 Review Record: Capturing Closure Unboxed Pair and Closure Identity

## Discussion Record

### Draft under Tech Lead decision D-001 (2026-11-03)

Drafted design-first on direction that capturing closures follow a minimal
vertical slice within the existing RFC 0057 boundaries and behind a feature
flag, without changing Work limits or regenerating planning artifacts.

Key measured input carried over from RFC 0057 slice 1:

- A closure has no `identity::DefId`; it is a dense per-owner
  `AnonymousOwnerLocalId`. `MirFunction.owner` and the Source call edge are
  `DefId`-typed, so the content-addressed closure identity is the floor for
  both the non-capturing and capturing slices.
- The capture fact model already exists (`CheckedCaptureFact` with
  `SharedReference`, `MutableReference`, `Move`, `Copy` and Explicit/Inferred
  origins) but has no producer or backend consumer.
- `Move` and reference captures depend on the incomplete RFC 0007 ownership
  overlay; escaping closures depend on the allocator ABI. The minimal slice is
  therefore frame-relative, non-escaping, by-value scalar capture only.

The DRAFT proposes Part A (closure DefId identity) plus Part B (flagged
unboxed code/environment pair for `Copy` scalar capture).

### Part A placement (measured and built)

Closure DefIds are interned in `VerifyBoundModule::provide`: it fetches the
interner via
`context.semanticContextResources<ModuleGraphIdentityMaterializationResources>()`
and enumerates every materialized owner body. `MaterializedOwnerBody` itself
cannot intern because its capability witness encodes only stable evidence and
is produced before the full owner-body set is known.

The closure identities are projected on a **separate frozen rail**
(`ImmutableDefinitionInventory::closureDefinitions()`), not appended to the
skeleton `definitions()` vector. `definitions()` is treated by more than forty
checker/HIR walks as the complete skeleton-definition set, and skeleton
`identities().definitions()` cannot contain later-phase rows; merging closures
into it would blur the phase boundary without making `definition(DefId)`
resolve them. Point lookups cross the rail instead.

Built shape:

1. `closureSyntheticName(path)` builds
   `"__closure_" + underscore-joined decimal path components` and validates it
   as a `DeclaredDefinitionName`.
2. For each owner body and each stable closure, the owner chain for a
   Definition owner is cloned from the owner definition record's owners and
   appended with that owner key; Module owners use an empty chain. The
   `DefinitionIdentityRecord(Closure, Value, none)` is interned and
   reverse-validated.
3. The ordered rows (owner-body order then stable closure order) pass into
   `VerifiedBoundModule::from` and `ImmutableDefinitionInventory::from` as a
   dedicated argument. The inventory validates exact count against the sum of
   `boundOwnerBody.closures()`, node/handle/key uniqueness, that each row is a
   Closure/Value/header-less record whose key recomputes, and that its node is
   the `Closure`-role anonymous entity node.
4. `VerifyBoundModule::verify` independently re-interns and requires identity
   equality, so the rail is fail-closed. The anonymous-key mapping for capture
   facts is unchanged.

## Owner Reviews

| Owner | Decision | Date |
|---|---|---|
| binder-checker | Pending | |
| ir-backend | Pending | |
| runtime-memory | Pending | |
| verification | Pending | |
| rfc | Pending | |

## Decision Record

Pending owner review. Author scope: content-addressed closure identity and a
default-off non-escaping by-value scalar-capture slice; reference/move
captures, escaping closures, callable traits, and generic closures deferred.

## Implementation Tracker

| Part | Change | Status | Verification |
|---|---|---|---|
| A0 | Closure Value-namespace header-less identity record admission | Landed (a3a06714) | key-definition-key-test 15; behavior-preserving (no producer yet) |
| A1 | Intern one closure DefId per StableClosureFact in `VerifyBoundModule::provide` (fetch `identityInterners` via `semanticContextResources`); synthetic name from `AnonymousOwnerLocalKey.path()` (`__closure_0_3`); owner chain cloned from the owner definition record for Definition owners, empty for Module owners | Landed (0bf04b77) | key-definition-key-test; new `VerifiedModuleInternsOneDefIdPerClosure` (3 closures, Module+Definition owners, both spellings, sibling divergence, point lookups); existing capturing-lambda module binds+verifies end to end |
| A2 | Project closure DefIds on a separate frozen `closureDefinitions()` rail of `ImmutableDefinitionInventory` (not appended to the skeleton `definitions()` vector, which over 40 checker/HIR walks treat as the complete skeleton-definition set); `closureDefinition(DefId)` / `closureDefinitionAt(node)` point lookups; count + node/handle/key uniqueness + Closure/Value/header-less record validation; `verify` independently re-interns and requires identity equality | Landed (0bf04b77) | definition-inventory-test, materialized-module-skeleton-test, active-definition-authority query/session tests; full sanitizer build green |
| A3 | Verify the new closure definition rail leaves emitted output unchanged | Landed (0bf04b77) | full sanitizer `ctest --preset default -j16` 322/322 (820s), including lit IR/process parity conformance; no snapshot re-record needed since no closure program lowers yet (empty rail without closures) |
| B0 | Zero-capture closure native execution (the prerequisite slice the RFC folds into Part B): point the checker closure-DefId resolution at the new Part A rail; publish the closure CallableSignature; then widen HIR/MIR/LIR one closure function per non-escaping direct-call module | Landed: B0-1/B0-2/B0-3 all landed; both spellings lower end to end and run natively exit 42; later copy-chain, parameter/capture arithmetic, capture-plus-literal, one-parameter literal-arithmetic, and zero-argument slices build on the same closure DefId rail (RFC 0057 row 2) | checker ztest green for both spellings; full sanitizer ctest 322/322; `zomc run` exit 42 |
| B0-1 | `closureValueShape` reads the closure DefId from `closureDefinitionAt(node)` instead of the skeleton DefinitionFact list | Landed (e8cf1aac) | behavior preserving until B0-2 |
| B0-2 | Publish the zero-parameter, annotated, non-generic, non-raising CallableSignature for both closure spellings on the closure rail; admit Closure in isCallableDefinition, cross-rail `definition(DefId)`, header-less header match, and signature census | Landed (756be8db) | session ztest `PublishesZeroCaptureClosureSignature` (no Checker/Signature incident, both spellings); full sanitizer ctest 322/322 |
| B0-3 | HIR closure-alias call node, MIR closure MirFunction owned by the DefId, LIR Source-edge enumeration so `zomc run` exits 42 for a zero-capture closure | Landed: steps 1/2/3 all landed; both spellings verified natively exit 42 on the current tree (2026-11-08) | native consumer fixture exit 42; `LowersZeroCaptureClosureAliasCallToHirAndMir` covers both spellings |
| B0-3 s1 | HIR closure-alias callee resolution reads `closureDefinitionAt(initializer)` from the closure rail instead of scanning skeleton definitions | Landed (48bdc2e6) | hir-module/session tests green; wall moved to HirVerification MissingRequiredFact (no HIR function for the closure DefId) |
| B0-3 s2 | Dedicated closure pass in hir-builder.cc after the skeleton loop, before candidate sealing: appends one parameterless ScalarLiteralReturn HIR function per admitted block-body function-expression closure on the closureDefinitions rail; verifier gains a four-node closure branch keyed by a cross-rail `closureDefinitionIndex`, accepts FunctionExpression as a function source node, and resolves the alias-call callee with the same `resolveClosureAliasCallee` fallback as the builder. Block-body `fn() -> T { return lit; }` only; expression-body lambda keeps ZOM4099 | Landed (21a86852) | new `LowersZeroCaptureClosureAliasCallToHirAndMir` (2 HIR/MIR functions, zero locals, one direct call) and `ExpressionBodyLambdaClosureKeepsCapabilityDrain` ztests green; `zomc run` exits 42 |
| B0-3 s3 | Same closure pass and verifier branch gain an expression-body LambdaExpression arm: the body is the `kLambdaExpressionExprBodyWord` scalar literal, block/return HIR anchors are sentinel with closure-source spans, value anchors the expression node. MIR/LIR unchanged. Both spellings now lower; parameter/block-body-lambda/IIFE/uncalled forms keep ZOM4099 | Landed (local) | ztest covers both spellings (2 HIR/MIR functions each) plus a one-parameter lambda negative case; `zomc run` exits 42 for `() -> i32 => 42` |

### Part B0-3 design (zero-capture closure native execution)

Problem: after B0-2 the checker accepts the closure at the signature stage,
but checkSources fails at IR phase HirConstruction (0x02) MissingRequiredFact
because the closure-alias call cannot name a callee and no HIR/MIR producer
emits the closure body. Goal of this minimal vertical slice:
`fn entry()->i32 { let f = fn()->i32 { return 42; }; return f(); }` runs
natively and exits 42. Zero captures, zero parameters; no environment and no
ABI change.

Identity model: unchanged from Part A. The closure is one content-addressed
DefId on the closureDefinitions rail. The call stays a direct Source edge
named by that DefId — there is no indirect call, no function-address rvalue,
and no environment slot. The `let f` binding is a pure alias with no lowered
slot (the returnsClosureAliasCall HIR shape already encodes this).

Migration, in the smallest independently-revertible order:
1. HIR callee resolution: `resolveClosureAliasCallee` reads
   `closureDefinitionAt(initializer)` instead of scanning skeleton
   definitions. The checker must also publish a direct dispatch fact for the
   closure call so `dispatchFactIndex` resolves.
2. HIR body production for the closure function itself (the closure body is a
   parameterless function returning a literal; reuse the existing
   functionReturnShape/DirectCallReturn machinery).
3. MIR: build the closure as an ordinary MirFunction owned by its DefId; the
   entry module's call targets it on the Source edge.
4. LIR/CLI: enumerate the closure DefId on the Source ladder and derive a
   symbol exactly as for a second named function; link and run.

Invariants: one lowered function per closure DefId; the call target set
matches the published dispatch/definition set (fail-closed verifier); no
change for programs without closures; reference/move/generic/raising closures
and any escaping use keep draining at their existing gates.

Rollback: each numbered step is its own local commit reverting cleanly. If a
step cannot be reached with the minimal shape, stop and report rather than
widen the admission surface.

### Part B0-3 step 2 design — HIR closure function body (APPROVED D-036, LANDED)

Measured wall after step 1 (48bdc2e6): IR phase HirVerification (0x03)
MissingRequiredFact. HIR now builds `entry` and its closure-alias call names
the closure DefId, but no HIR function node exists for that DefId, so the
verifier's call-to-function reconciliation fails.

Producer location: a dedicated closure pass in hir-builder.cc, run AFTER the
existing skeleton-definition loop (line ~514) and BEFORE the candidate is
sealed. It iterates `bound.definitions().closureDefinitions()` (the Part A
rail) and, for each admitted zero-capture closure (the SAME predicate as B0-2:
both spellings, annotated return, no raises/generics/capture, zero params),
appends one HIR function to the same `impl->functions` pool. It does NOT widen
the existing loop: the rail entry has a different type
(MaterializedClosureDefinitionEntry), so a separate small loop keeps the
change local and independently revertible.

What it reuses, unchanged:
- `functionReturnShape` already recognizes a closure body (isClosure handles
  both spellings + body words), returning the literal-return shape.
- The existing scalar-literal return HIR builder that the Function branch
  uses (fact lookups, return pool append). The closure needs no parameters,
  no locals, no receiver, so the reused path is the parameterless literal
  return.
- The call site already lowers to a Source call named by the DefId after
  step 1; nothing changes there.

Per-closure mapping the new pass supplies: the DefId (rail), the AST body
node/span (rail node), and the function type/signature (looked up via
lookupSignature, which B0-2 populated). No signature root is required for a
closure (it is not a module-scope root), mirroring how impl methods skip the
root check.

Invariants:
1. Exactly one HIR function per admitted closure DefId; zero for any closure
   the B0-2 predicate excludes (those keep the body-stage drain).
2. Every closure call target resolves to exactly one produced HIR function
   (the verifier stays fail-closed; an extra or missing function rejects).
3. No environment parameter or locals exist yet; the function is a closed
   parameterless literal-return. Behavior for programs without closures is
   byte-identical (the added pass sees an empty rail).

Migration/rollback: one commit adding the closure pass and its verifier
expectation; reverted by deleting the pass (closures then fail HIR verification
again, exactly the current state). MIR MirFunction and LIR Source
enumeration remain separate later steps, also independently revertible.

Decision under D-036: the closure HIR function is indistinguishable from a
parameterless named function. The identity is already the closure DefId; a
closure-origin marker is added only if the environment work in B1 needs it.

Measured scope after landing: the MIR generic builder already emits one
MirFunction per HIR function (owned by the closure DefId), and the existing
multi-function call ladder enumerates the Source-edge symbol, so no MIR or LIR
producer change was required for the block-body slice; `zomc run` exits 42.
Expression-body lambdas, IIFE, defined-but-uncalled closures, parameters,
captures, and generics keep the ZOM4099 capability drain.

### Part B0-3 step 3 design — expression-body lambda closure (APPROVED, LANDED)

Scope: admit the zero-capture paren-arrow lambda whose body is a single
scalar-literal expression, `let f = () -> i32 => 42; return f();`, so it runs
natively and exits with the literal value. The step-2 block-body
function-expression path is unchanged; block-body lambdas
(`() -> i32 => { return 42; }`), IIFE, defined-but-uncalled closures,
parameters, captures, and generics stay on the ZOM4099 drain.

Problem: an expression-body lambda anchors no source BlockStmt or ReturnStmt;
its body is the single expression at `kLambdaExpressionBodyWord` (grammar:
`LambdaExpression ::= OrdinaryParameterClause ReturnType? '=>'
(AssignmentExpression | BlockStatement)`). `functionReturnShape` requires a
block body, so the step-2 pass rejects the spelling. The checker already
publishes its zero-capture CallableSignature (B0-2) and a literal fact for the
body expression; only the HIR shape is missing.

Producer shape: the same dedicated closure pass gains a LambdaExpression arm
alongside the FunctionExpression arm. It reads the body word directly, admits
it only when the body node is a scalar literal, and appends a
ScalarLiteralReturn pending declaration whose block/return anchors are
sentinel `ast::NodeId()` (the materializer and per-node correspondence
validator already support source-less HIR nodes; destructors use the same
convention) and whose value anchor is the body expression. HIR layout stays
four nodes (function F, block F+1, return F+2, literal F+3), so MIR and LIR
need no change, as in step 2. Span convention: the synthetic block and return
spans use the closure source span; the value span is the body-expression span
and is checked against the literal fact. The verifier closure branch gains the
mirror arm: LambdaExpression accepted as a function source node, body word
required to be a scalar literal, and the two source-less anchors allowed with
the closure-source span convention. The alias-call side is unchanged (the
closure-alias shape and `resolveClosureAliasCallee` already accept both
spellings).

Invariants:
1. Exactly one HIR function per admitted lambda DefId; any spelling the arm
   excludes produces zero functions and keeps the capability drain.
2. Admission predicate is strict: zero parameters, annotated return, no
   raises/generics/capture (the B0-2 signature gate), and a body that is
   itself one scalar literal (`=> 40 + 2`, calls, references, blocks drain).
3. Call-target to function-set reconciliation stays fail-closed; programs
   without expression-body lambdas are unchanged.

Tests: replace the step-2 lambda drain ztest with a positive
HIR/MIR/native test for both spellings (two functions, one direct call, zero
locals); keep one negative ztest for a non-admitted lambda (one-parameter
`(x: i32) -> i32 => x`) asserting ZOM4099 and zero incidents; native `zomc
run` exit 42 for the lambda; full sanitizer ctest.

Rollback: one local commit deleting the lambda arm restores the step-2 state
(lambda ZOM4099) with no effect on the function-expression slice.

Implementation note measured while landing: a LambdaExpression payload has two
mutually exclusive body words, `kLambdaExpressionBodyWord` for a block body and
`kLambdaExpressionExprBodyWord` for an expression body. The admitted
expression-body form has no block word set and its literal is the
ExprBodyWord node, which is the anchor the builder and verifier use.

### Part B2 design — bounded two-Copy-scalar capture (APPROVED, LANDED 1fa01bf2, 8e43639f, abb9808f)

Scope: behind the same default-off `--experimental-captures` flag, widen the
B1 slice from one to a bounded set of two by-value Copy scalar captures, and
allow the closure body to combine them:

```
let a: i32 = 40;
let b: i32 = 2;
let f = fn() -> i32 { return a + b; };
return f();   // exits 42
```

Bound is two because `lir::kMaxCallArguments` is 2 and the existing two-
argument arithmetic-callee builder is the ceiling the backend supports; three
or more captures keep ZOM4099. Admitted closure bodies are (a) one primitive
arithmetic whose two leaves are the two captured locals (`a + b`), and (b) a
bare return of either captured local (`return a;`). Both captives follow the
B1 rules individually: block-body fn-expr, zero user parameters, no
typeparams/captures-slot/raises, each is an initialized read-only explicitly
annotated i32/bool owner local; mixed arithmetic on bool drains as today.
Every other closure (one-capture B1 cases, parameters, mutation, lambda,
`use []`, escape, three captures, nested/local operands) keeps ZOM4099 even
with the flag on; flag off is unchanged.

Environment order (canonical): capture fields are ordered by enclosing
owner-local declaration preorder (the same order the binder free-variable
scan sees), deduplicated per binding. The checker publishes one
`CheckedCaptureFact{Copy, Inferred}` per capture in that order; the HIR
synthetic parameters are `CallableParameterIdentityRecord::from(closure.key,
ordinary(i))` for i in 0..N; the implicit call arguments pass the captured
locals in the same order. Checker, HIR signature, and MIR parameter order
must agree exactly (fail-closed), which also establishes the field order B2
later env structs will reuse.

Layer changes (three independently revertible commits):

1. Checker: generalize the B1 single-capture predicate to an ordered vector,
   admit 1..2 distinct Copy scalars whose declared types match the closure
   success type and whose body leaves classify as admitted (bare ident of one
   capture, or primitive binary with both leaves captured locals); publish N
   facts through the existing exact-set verifier (2 facts required, not one).
2. HIR: the closure lowers to the existing parameter-reference-return (one
   parameter) or primitive-binary-parameter-return (two parameters) shape;
   the caller gains a two-local closure-alias shape passing two implicit
   scalar arguments. New verifier arms re-derive the capture set from the
   facts and reconcile parameter/argument count, order, type, and span.
3. LIR: generalize the B1 scalar-local call module from exactly one user
   local to one or two (caller locals 1..2 + result temporary), reusing the
   existing identity and two-parameter arithmetic callee builders; MIR is
   unchanged. Native exit 42 is the gate.

Invariants:

1. Flag off unchanged; 3+ captures, references, mutation, parameters, and
   escape drain ZOM4099 with zero incidents.
2. Deterministic canonical field order across facts, parameters, and
   arguments; any disagreement is a per-layer InvalidFact rejection.
3. Exactly N facts / N synthetic parameters / N implicit arguments for N in
   1..2; the two-capture arithmetic closure has zero user-visible slots of
   its own.

Tests (directed during the iteration per D-051; one full sanitizer ctest at
close): flag-off drain; flag-on `a + b` native exit 42 with 2 facts / 2
closure parameters / 2 implicit arguments / 3 MIR functions; flag-on bare
`return a;` with two captures in scope; negative ztests for three captures,
a bare literal/uncaptured operand in the binary, mismatched declared types,
and one-capture regression (B1 still native 42); full sanitizer ctest once.

Rollback: three local commits; deletion restores the B1 behavior (only one
capture admitted) with no flag or B0 change.

### Part B1 design — flagged one-Copy-scalar capture (APPROVED, LANDED 4af11649)

Scope: behind a new default-off CLI flag, admit exactly one non-escaping
by-value Copy scalar capture:

```
fn entry() -> i32 {
  let x: i32 = 42;
  let f = fn() -> i32 { return x; };
  return f();   // exits 42
}
```

The captured local is an initialized scalar on the existing carrier set
(i32/bool), read-only after capture; the closure is block-body (fn-expr first;
the lambda arm follows the same path), has zero user parameters, and its body
is the single `return <captured ident>;`; it is used only as a directly called
alias. Flag off is byte-identical with today (every capture drains ZOM4099 at
the existing body-checker gate). Multi-capture, Move/reference captures,
mutated or escaping closures, captures of parameters, IIFE, and uncalled
closures keep ZOM4099 even with the flag on.

Flag: `--experimental-captures`, a bool option added through the existing
compilation-policy option channel (precedent: `allow-dollar-identifiers`),
threaded on the package compilation request into the session and read by the
checker/HIR admission gates. It is not persisted in any artifact and no test
invocation sets it by default.

Representation decision (deviation from the RFC text, recommended): for one
Copy scalar the implicit environment is the scalar value itself, passed by
value as one implicit leading argument — not a pointer to an env struct. The
closure then lowers to the existing one-parameter parameter-reference-return
shape and the caller to the existing one-scalar-argument direct-call ladder
(both already native), so no env struct, frame pointer, or ABI change is
needed. The `{code,env}` carrier and pointer environment enter with
multi-capture (B2) / references (RFC 0007), where they are actually required.

Layer changes, in three independently revertible commits:

1. Checker (flag-gated): produce one `CheckedCaptureFact{closure, target=x,
   Copy, Inferred, i32}` for the admitted free variable instead of draining at
   body-checker.cc:9330; add the fail-closed capture-fact verifier and census
   the pool currently lacks (missing/additional/non-canonical rejects); gate
   mode (Copy only), type (scalar carrier), count (one), and non-escape.
2. HIR: the closure function gains one synthetic environment parameter
   (CallableParameterKey derived from the closure DefId plus position, the
   construction B0-2 deferred) and lowers as ParameterReferenceReturn; the
   alias call gains one implicit scalar argument carrying the captured local.
   A new verifier reconciliation accepts the source call's zero source
   arguments only when exactly one matching Copy capture fact exists; the
   argument type/span/key must equal the captured place.
3. MIR/LIR: route through the existing one-scalar-argument call ladder; the
   closure is enumerated by DefId as in B0-3. Native exit 42 is the gate.

Invariants:

1. Flag off: capture fact pool empty, output unchanged for every program.
2. Flag on admitted: exactly one Copy capture fact; one synthetic parameter;
   one implicit argument; fact, closure signature, HIR, and MIR agree
   (fail-closed per layer).
3. Everything outside the slice drains ZOM4099 with zero incidents.

Tests: flag-off ztest locking the ZOM4099 drain and the empty capture pool;
flag-on positive ztest (fact fields, HIR/MIR function and parameter counts)
plus native exit 42; flag-on negative ztests (two captures, mutation,
reference, escape) asserting ZOM4099 and no incident; full sanitizer ctest and
the default parity corpus untouched.

Rollback: three local commits; deleting them restores the B0-3 drain. The flag
is never flipped on by default. Main open risks: the capture-fact verifier and
census are new code, and the zero-source-argument to one-implicit-argument
reconciliation is a new HIR check rather than a reuse.

### Part B1 landed (2026-11-04, flagged)

The flagged one-Copy-scalar slice is implemented behind the default-off
`--experimental-captures` session gate. Scope realized exactly as proposed: a
block-body `fn` expression (the paren-arrow spelling keeps draining), zero user
parameters, whose sole statement returns one initialized read-only annotated
owner-local scalar (i32/bool); multiple captures, captured parameters, i64,
mutation, and explicit `use []` all keep ZOM4099 with zero incidents.

Implementation shape (a refinement of the sketch above): the checker is the
only phase that reads the flag. On admission it publishes one
`CheckedCaptureFact{mode=Copy, origin=Inferred}` per requirement; the existing
capture codec plus the missing/additional exact-set verifier enforce the fact
set, so no new checker codec was needed. HIR derives everything from the
verified capture fact (the flag never reaches HIR): the closure gains one
synthetic by-value parameter keyed `CallableParameterIdentityRecord::from(
closure DefKey, ordinary(0))` and lowers as `ParameterReferenceReturn`; the
caller is the three-statement
`[let x; let f=fn; return f()]` capture-alias shape, slotless for the alias,
with one literal-initialized local passed as the single implicit argument.
MIR and LIR are unchanged: the pair falls through the pre-existing
`lowerScalarLocalCallModule` identity-callee ladder and exits 42 natively.

The gate rides the non-persisted channel only (`basic::LangOptions`,
forwarded through a plain `BodyCheckingInput` bool). It is deliberately not
copied onto `SelectedLanguageOptions` / `SemanticCompilerOptionsKey`, so it
never enters the canonical request, crate identity, query cache, or checked
facts revision hash.

Tests: `CompilerSessionTest.FlagOffCapturingClosureKeepsCapabilityDrain`,
`FlagOnSingleScalarCaptureLowersToHirAndMir`, and
`FlagOnRejectedCaptureShapesKeepCapabilityDrain` (two captures, captured
parameter, explicit `use []`); the `capturing-closure-consumer` package is a
`native-run-cli --run-case ...::42::--experimental-captures` (the run harness
grew an optional fifth spec segment forwarding one experimental gate). No
default test invocation sets the flag and the default corpus is unchanged.

### Part B2 landed (2026-11-04, flagged)

The flagged bounded two-Copy-scalar slice is implemented behind the same
default-off `--experimental-captures` gate in three local commits
(`1fa01bf2` checker, `8e43639f` HIR/MIR/LIR plus the native fixture,
`abb9808f` ztests). Scope realized exactly as approved: a block-body
zero-user-parameter `fn` expression combining one or two initialized
read-only annotated i32 owner locals; one capture returns the bare
identifier (the B1 shape), two captures require one primitive integer
arithmetic binary (`isIntegerArithmeticBinarySyntax`) whose two leaves are
the captured identifiers. Captures stay ordered by owner-local binding
declaration preorder (the canonical environment field order), deduplicated
per binding (`a + a` is one capture, so the two-capture binary drains),
and every capture type must equal the closure success type.

Layer shape realized:

1. Checker: the B1 single-capture predicate generalized to an ordered
   vector of one or two `AdmittedScalarCapture`s; the checker publishes one
   `CheckedCaptureFact{Copy, Inferred}` per entry in declaration preorder,
   so the existing capture exact-set verifier is reused unchanged.
2. HIR: the caller capture-alias shape generalizes from three to three or
   four statements (one or two leading scalar-initialized lets, the
   slotless alias, the zero-source-argument call) through a new
   `CapturingClosureAliasCallReturn` pending variant carrying N locals and N
   implicit by-value arguments. A shared `sortedClosureCaptureFacts` helper
   derives canonical order for both builder arms. The closure gains one
   synthetic environment parameter per capture keyed
   `CallableParameterIdentityRecord::from(closure DefKey, ordinary(i))`;
   the two-capture body reuses the parameter-binary `ComparisonReturn`
   materializer (function F, body F+1, left F+2, right F+3, binary F+4,
   return F+5), not a new local variant. Both verifier arms reconcile the
   exact fact/local/parameter/argument sets, types, positions, spans, and
   the primitive call fact.
3. MIR (a deviation from the design note that predicted "MIR unchanged",
   confirmed by an independent adversarial review): the scalar-local caller
   strategy generalizes from one to two UserLocals/arguments; the built-MIR
   node checksum takes two paired corrections (by-value-local-argument
   credits sum over all local arguments; a trailing direct call with two
   leading locals stops classifying as a sequential local-return block).
4. LIR: `lowerScalarLocalCallModule` generalizes from one to two constant
   caller UserLocals and one or two callee parameters, accepting both the
   identity callee (one capture) and the arithmetic-callee body (two
   captures) it already modeled for the one-parameter slice.

Recorded boundary (deliberate fail-closed, not a defect): an owner scope
with two locals whose closure body reads only one (`let a; let b;
fn(){return a}`) still drains ZOM4099. The checker publishes only
body-derived captures (one here), and threading a dead, uncaptured leading
local through the caller shape, checksum, and LIR is outside this slice.
`a + 1` (literal leaf), three captures, nested/non-identifier operands,
parameter captures, `use []`, and bool arithmetic all keep ZOM4099 with
zero incidents.

Tests: `FlagOnTwoScalarCaptureLowersToHirAndMir` (2 locals, 2 implicit
local-1/local-2 arguments, 2 synthetic closure parameters, 2 MIR
functions), the extended `FlagOnRejectedCaptureShapesKeepCapabilityDrain`
(literal leaf, third local, partial capture, flag-off), three
diagnostics-conformance cases
(`closure_two_scalar_capture_pos_01` with the gate,
`closure_three_scalar_capture_neg_01` and
`closure_two_scalar_capture_flagoff_neg_01` without), and the
`two-capturing-closure-consumer` native-run case exiting 42. Full
sanitizer ctest 349/349.

| B1 | Flagged one-Copy-scalar capture on top of B0: the flagged closure becomes a one-parameter function returning its by-value scalar environment parameter; the caller passes the captured local as one implicit scalar argument. Rides the existing `ParameterReferenceReturn` + `lowerScalarLocalCallModule` native ladder (no MIR/LIR change); the flag is session-scoped only and never enters a canonical codec or crate identity | Landed (local) | capturing-closure-consumer native exit 42, flag-off/on session ztests + negatives, full sanitizer ctest |
| B2 | Bounded multi-Copy-scalar capture set (canonical syntax-path field order) | Landed (local) | two-capturing-closure-consumer native exit 42, flag-on/off conformance pos/neg, session ztests, full sanitizer ctest 349 |
| Later | Reference/move captures | Blocked | RFC 0007 ownership completion |
| Later | Escaping/boxed closures | Blocked | allocator ABI |
| Later | Callable traits, generic closures | Not scheduled | trait dispatch; RFC 0056 instances |

### Part B baseline measured 2026-11-03 (sanitizer build at 0bf04b77)

`zomc compile --check` already drains ZOM4099 for every closure form, so the
wall is checker-to-HIR, not backend; a named function with the same body
checks fine. Probe matrix:

| Form | Result at `--check` |
|---|---|
| `let f = fn() -> i32 { return 42; }; return f();` (zero capture) | ZOM4099 |
| `let f = () -> i32 => 42; return f();` (zero capture) | ZOM4099 |
| `return (fn() -> i32 { return 42; })();` (direct IIFE) | ZOM4099 |
| `let f = fn() -> i32 { return 42; }; return 0;` (defined, never called) | ZOM4099 |
| `fn fortytwo() -> i32 { return 42; } return fortytwo();` | check OK |

### Part B0-2 design (closure signature publication)

Measured blockers in `SignatureFactsBuilder::build`:
- The signature loop (signature-facts.cc:7792) walks only skeleton
  `definitions().definitions()`; closures are on the Part A rail and have no
  `DefinitionFact`, so no `CallableSignature` is ever published for them
  (probe showed `sigCount=1` for a module whose only named function is
  `entry`).
- `signatureScope` keys off a `DefinitionFact` and its declaring scope.
- `buildCallableParameters` keys off skeleton-materialized
  `CallableParameterKey` identities owned by the skeleton definition. Closure
  parameters are owner-local and have no such materialized identities, so the
  existing parameter builder returns none for a closure owner.

Design (internal checker contract only; no CLI / persisted format / public
type change):
1. Add a second, closure-only signature loop over
   `input.boundModule.definitions().closureDefinitions()`, in the same
   deterministic owner/closure order as the rail (which matches the canonical
   census ordering). Only zero-capture, non-generic, non-raising, both-spelling
   closures are admitted; anything else contributes no signature and keeps the
   existing body-stage drain.
2. Scope: Module-owned closure -> `ModuleDefinitionSignatureScope`;
   Definition-owned -> `EnclosedSignatureScope{owner DefId}`, matching the
   Part A owner chain. No `DefinitionFact` is consulted.
3. Parameters: a dedicated closure path builds `ParameterSignature` values by
   position directly from the closure parameter-list syntax via
   `SourceTypeBuilder` (no skeleton `CallableParameterKey`); the
   `CallableParameterKey` field of the parameter signature is derived from the
   closure DefId + position if the codec requires one, else left empty. The
   first slice targets the zero-parameter form (`fn() -> i32`), so parameter
   identity construction is exercised minimally and extended with capture work.
4. Return type: reuse `SourceTypeBuilder.build(returnType)` exactly as the
   named-function path; raises must be absent; receiver/abi/generics empty.
5. The emitted `SemanticSignature{closureDefId, Closure, scope, [], [],
   CallableSignature{...}}` joins the same `built` vector, so canonical encoding
   and the strict byte-order sort are reused unchanged.

Verifier impact: `SignatureFactsVerifier::verify` only requires each signature
DefId to exist in the global identity authority and re-encodes the candidate;
it does not re-derive the signature set. Part A already interns the closure
DefId globally, so verify accepts the new signatures with no verifier change;
the closure DefId rail itself is independently re-derived by
`VerifyBoundModule::verify` (fail-closed). Shared-contract surface touched:
none beyond appending closure rows to the internal signature list.

Root wall (fixed in B0-1, e8cf1aac): `closureValueShape`
(compiler/checker/body/body-checker.cc:5326)
scans `input.boundModule.bindings().definitions()` (the `DefinitionFact` list,
skeleton declarations only) for `DefinitionKind::Closure`. Closures are never
in that list; after Part A they live on
`input.boundModule.definitions().closureDefinitions()`. So the resolved
closure DefId is always none and the shape returns none, and every closure
value drains ZOM4099 whether called or not. B0 step 1 is to resolve the DefId
from the Part A rail (`closureDefinitionAt(node)`). `directCallableShape`
derives the callee through `closureValueShape`, so it reopens with the same
one-point fix. The HIR `returnsClosureAliasCall` groundwork already carries
the alias mapping; MIR/LIR then need the closure `MirFunction` (owner is now a
real DefId) enumerated on the Source edge for one non-escaping direct call.

