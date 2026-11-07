# RFC 0057 Review Record: Closure Representation and Non-Capturing Closures

## Discussion Record

### Round 1: empirical scope survey (2026-11-03)

Measured before drafting:

- parser: anonymous functions, generic closures, `use [x, &y, this]`, `raises`;
- binder: `DefinitionKind::Closure`, inferred free-variable and explicit
  capture facts (`ByValue` / `ByReference` / `This`);
- checker: `CheckedCaptureFact` with `CaptureMode { SharedReference,
  MutableReference, Move, Copy }`; closure bodies resolve capture requirements;
- HIR/MIR/LIR: no closure value, no function-address rvalue; LIR
  `IndirectCall` models only the dyn dispatch shape (interface/method/receiver);
- empirical probes: `let f = fn(n: i32) -> i32 { return n; }; f(42)` drains
  ZOM4099 at the lambda, and a call through a function-typed parameter drains
  ZOM4099 at the call.

Conclusion recorded in
[`docs/design/language/closure-representation-decision-points.md`](../../design/language/closure-representation-decision-points.md):
slice 1 is bare code pointer for non-capturing direct-called closures; the
unboxed code/environment pair and heap escape promotion are later slices.

## Owner Reviews

| Owner | Decision | Date |
|---|---|---|
| binder-checker | APPROVE (2026-11-03) | 2026-11-03 |
| ir-backend | APPROVE (2026-11-03) | 2026-11-03 |
| runtime-memory | APPROVE (2026-11-03) | 2026-11-03 |
| spec-audit | APPROVE (2026-11-03) | 2026-11-03 |
| verification | APPROVE (2026-11-03) | 2026-11-03 |

Round 1 review basis (agent owner review per the repo's owner-approval
contract; no human reviews fabricated):

- **binder-checker**: the checker gate uses only existing facts
  (`DefinitionKind::Closure`, empty inferred + explicit capture sets,
  structural `FunctionTypeData`); no binder schema change is needed for slice
  1. The callable-value call shape mirrors the admitted generic-call
  positions (direct return / single-local initializer), so the position
  matrix stays consistent.
- **ir-backend**: a new MIR function-address value is the minimal primitive
  and an additive codec tag; the plain code-pointer indirect call does not
  alter the existing dyn-dispatch terminator semantics when implemented as a
  parallel minimal form. LIR already resolves global symbols. Accepted with
  the implementation-time requirement that the choice between extending
  `MirIndirectCallTerminator` and a parallel terminator minimize the dyn
  validator surface.
- **runtime-memory**: slice 1 performs zero allocations and needs no runtime
  ABI change; the allocator dependence appears only at the explicitly
  deferred escape slice. Accepted.
- **spec-audit**: closure syntax is already specified and parsing today;
  slice 1 changes semantic admission only, with the rejection boundary stated
  per construct. No grammar/spec chapter change is required for slice 1; the
  admitted-behavior sentence is required with the implementation.
- **verification**: the plan independently re-derives callee ownership and
  pointer slot carriers, adds a malformed-bytes codec rejection, and keeps
  fail-closed behavior for every listed boundary. Accepted.

## Decision Record

Accepted 2026-11-03 by agent owner approval on all five axes. Slice 1 is the
non-capturing direct-called closure via a bare function pointer as specified;
capturing closures (unboxed code/environment pair), callable traits, escaping
closures/function-typed parameters, and generic closures are follow-up
slices, with mutable/move captures blocked on RFC 0007 and escaping blocked
on the allocator ABI. Implementation begins in a fresh focused session given
the cross-layer codec and calling-convention surface.

## Spec Change Record

Pending spec audit; the closure syntax is already specified and parses today,
so slice 1 changes semantic admission (checker/backend), not grammar.

## Implementation Tracker

| Slice | Change | Status | Verification |
|---|---|---|---|
| 1a | Non-capturing annotated lambda assigned to an owner local and directly called in the same body, routed over the reused Source direct edge (no closure value, no indirect call) | Landed via RFC 0058 B0-1/B0-2/B0-3 (`0bf04b77`, `107952f2`, `21a86852`, `1075752f`): closure DefId rail, CallableSignature publication, zero-capture HIR/MIR/LIR closure functions, both spellings native exit 42 | zero/one-capture ztests, capturing-closure-consumer native, full sanitizer ctest |
| 1b | Closure usable as a value: MIR `FunctionAddress` rvalue (DefId -> code address, new canonical codec tag), code-pointer indirect `MirCall`, LIR function-symbol materialization plus plain indirect call, LLVM internal-function-address constant; unlocks non-escaping fn-pointer values (still no function-typed parameters, which stay in slice 4) | Direct-call sub-slice landed 2026-11-05 (`87a84e6f`, `3ba18506`, `825ed356`): checker layer + single-parameter non-capturing block-body closure direct call end to end on the existing Source edge. `fn(n: i32) -> i32 { return n; }` lowers to a one-parameter function returning the parameter, and `f(42)` passes the literal through the ordinary direct call; the MIR scalar call ladder/LIR/LLVM path executes it. Value-carrier foundation landed 2026-11-05 (`d86a4570`, `98976042`, `1f10bd93`): LIR `FunctionSymbolAddr` statement (0x0c) materializes a one-based module-local function into an opaque pointer slot with range/destination-carrier verification and an LLVM internal-function constant; MIR `FunctionAddress` rvalue (0x08) and plain `CodePointerCall` terminator (0x07, no interface/receiver) with canonical codec, clone, structural validation, and full ownership/mono/cloner/CFG consumer coverage; `lowerCodePointerCallModule` lowers a P-parameter identity callee plus a two-block caller that materializes the address, optionally copies it through a bounded pointer-alias chain (`let g = f`), and invokes it through the generic LIR IndirectCall with integer constant arguments; translation validation proves symbol ownership and call correspondence, function types derive the pointer carrier, and the zomc two-function ladder dispatches it. Four backend ztests cover direct/two-arg/copy-chain/fail-closed native ELF paths. Checker producer step 1 landed 2026-11-05 (`6f97c10c`): `directCallableShape` follows a bounded cycle-checked chain of identifier-initializer owner locals, so `let g = f; g(42)` resolves to the closure definition and publishes the direct call fact with zero type errors; the ZOM4099 drain moves from the call site to the HIR function boundary (session ztest pins one ZOM4099, zero ZOM4009, zero HIR modules). HIR/MIR value-call producer landed 2026-11-05 (`e8f4f33e`): HIR gains `HirClosureAddressExpression` and `HirIndirectCallExpression` with full candidate/verified plumbing, dump output, an N-plus-two bounded copy-chain shape detector and builder gate, a dedicated fixed-stride verifier branch reconciling the binder owner-local chain, closure rail row, checker call and dispatch facts, and spans; the Built MIR builder ClosureValueCall strategy emits FunctionAddress plus whole-local chain copies and a CodePointerCall into the proven `lowerCodePointerCallModule` shape, with matching node-balance checksum terms. `let f = fn(n: i32) -> i32 { return n; }; let g = f; return g(42);` and longer chains run natively exit 42. Two-parameter widening landed 2026-11-05 (`2ccfc852`, `dc9673fd`): the closure rail and the copied closure-value call rail now admit one or two ordinary integer value parameters and one or two scalar-literal i32 arguments; the closure HIR function carries every published parameter and its verifier proves the returned reference names that declared parameter, while the value-call builder/verifier reconcile per-argument checker call, dispatch, literal, and span facts. `lowerCallModuleWithArguments` routes the caller through the shared `buildMonoCallerFunction`, admitting both the initializer and Temporary/FunctionResult return shapes with per-parameter scalar carriers. `let f = fn(a: i32, b: i32) -> i32 { return a; }; return f(40, 2);` and the copied `g(40, 2)` chain run natively exit 40; a return naming the second parameter fails closed at LIR; three parameters and arity mismatch keep their existing drains. The expression-body lambda spelling, function-typed parameters, captures, and full ownership stay on later slices. | checker: 50 signature-facts; native parameterized-closure-consumer, copied-closure-value-consumer (exit 42), two-parameter-closure-consumer and two-parameter-copied-closure-value-consumer (exit 40); directed session ztests 8/8, hir-module 153, LIR/MIR/backend carrier suites green. **Milestone closed 2026-11-05**: full sanitizer ctest pass (937.65 s, 0 failures; 265 unittest binaries plus lit), corpus process parity re-recorded and checked 1105 sources byte-parallel (snapshot unchanged). Two-parameter closure form DONE. Expression-body parameter-reference widening landed 2026-11-05 (`68c47d31`): the parameterized closure rail now also admits the paren-arrow lambda whose sole body expression names a declared parameter (`(n: i32) -> i32 => n` exits 42; two-parameter `(a, b) => a` copy chain exits 40), with block/compound lambda bodies keeping the drain; expression-body-closure-consumer and two-parameter-expression-body-copied-closure-consumer fixtures added. **Milestone closed 2026-11-05**: full sanitizer ctest pass (955.85 s, 0 failures; 265 unittest binaries plus lit), corpus process parity re-recorded and checked 1105 sources byte-parallel (snapshot unchanged). Non-capturing closure forms (zero/one/two parameters, both spellings, direct and copied value edges) DONE; remaining slice 1b/2 front is capture forms on the indirect-call carrier. |
| 2 | Unboxed code/environment pair; by-value scalar captures at call-site-local closures | Landed (flagged) via RFC 0058 B1/B2 (`4af11649`, `1fa01bf2`, `8e43639f`): one/two Copy scalar captures on the reused direct-call edge. Captured-value indirect-call carrier landed 2026-11-05 (`f5d030e4`, `c5695ff6`, `cbe3bba7`, `49dca29c`): a one/two-Copy-scalar capturing closure reached through a pointer-copy chain (`let x = 42; let f = fn() -> i32 { return x; }; let g = f; return g();`) now prepends the captured locals to the value-call layout, passes them as implicit by-value arguments of the CodePointerCall, and runs natively (B1 one-capture identity body; longer chains included). B2 two-capture arithmetic body on the copied value edge landed 2026-11-05 (`326fcef7`, `8ef7514c`): the shared scalar-body callee lowering now admits one admitted Use/Arithmetic/Comparison computation over the leading environment parameters and dense body locals on both the direct scalar-local ladder and the code-pointer edge, with per-parameter scalar carriers; both captured slots pass the CodePointerCall by value, so `let a = 40; let b = 2; let f = fn() -> i32 { return a + b; }; let g = f; return g();` runs natively exit 42 (50-8 subtraction exits 42 as well). Mixed one-declared-parameter plus one-capture arithmetic body on the copied value edge landed 2026-11-06 (`bcbc8f02`, `8f4bf87c`, `d3d4cc2c`, `040eadd0`, `6b5fda2b`): the checker structurally recognizes the declared parameter through the closure's own FunctionParameterDecl (closure parameters bind on the owner-local rail) and admits exactly one by-value i32 parameter leaf plus one captured leaf in an arithmetic body; HIR/MIR/LIR carry declared-first ABI (the published declared parameter keeps ordinary position zero and the synthetic environment parameter shifts to position one, so no content-addressed key collides, with the caller's one source literal argument preceding the captured-local by-value argument). `let a = 40; let f = fn(n: i32) -> i32 { return n + a; }; let g = f; return g(2);` and 50-8 subtraction run natively exit 42. The same mixed form invoked directly on the origin binding (`return f(2)`, no copy chain) was already admitted end to end and gained a native fixture plus session ztest lock (`d94aa4cf`). Capture-plus-literal arithmetic body on the copied value edge landed 2026-11-06 (`73105ce4`, `cce7bd8c`, `fcd48505`, `daa325bd`): a zero-parameter, one-Copy-capture closure whose body returns one arithmetic operation over the captured slot and an integer literal (`let a = 41; let f = fn() -> i32 { return a + 1; }; let g = f; return g();`) now admits one identifier capture operand plus one `IntLiteral` operand; the literal lowers to a constant HIR arm inside the closure body while the call still passes only the single captured local as its by-value environment argument. Literal-left add, subtraction, and the direct edge run natively exit 42. Two captures with a parameter, two parameters with a capture, two literal operands, a nested two-leaf binary, a parameter-plus-literal body, non-literal arguments, non-i32 parameters, and the flag-off case fail closed ZOM4099 with zero incidents. One-capture expression-body lambda landed 2026-11-06 (`da5ca636`, `1e7d1ad7`, `f75d9344`, `b89e2fec`, `9d599e28`): a zero-parameter paren-arrow lambda with no block or return that yields its single Copy capture directly (`let a = 42; let f = () -> i32 => a; let g = f; return g();`), reached both directly and through a whole-local copy chain. The closure function carries one synthetic by-value environment parameter returned through the fixed parameter-reference stride with sentinel body/return anchors; the checker reads the expression-body yield, the capturing HIR branch adds a lambda arm, and the captured-value caller detectors and verifier accept a lambda origin with the lambda parameter-list word; MIR/LIR are unchanged. Two-capture arithmetic, capture-plus-literal arithmetic, and mixed-parameter expression bodies stay block-body-only and fail closed ZOM4099 with zero incidents. Two-parameter arithmetic closure landed 2026-11-07 (`ba13e673`, `356231fd`, `22571256`, `f361cfb1`): a non-capturing closure with two declared by-value i32 parameters whose sole body is one integer arithmetic binary over two distinct declared parameters (`(a, b) -> i32 => a + b`), in both the block-body function-expression and expression-body paren-arrow spellings, reached directly and through a copy chain, with no capture gate. The HIR parameter-closure rail resolves both leaves to distinct published parameter keys and emits the parameter-binary ComparisonReturn; the verifier probes the binary shape, uses the six-node stride, and reconciles the operation and operand nodes to the checker primitive call fact; MIR/LIR are unchanged. One-parameter-plus-literal, repeated-operand (`a + a`), and three-or-more-parameter arithmetic bodies fail closed ZOM4099 with zero incidents. Two-capture arithmetic expression-body lambda landed 2026-11-07 (`06c331e6`, `2f495bf2`, `813ecf8a`, `e34aaca4`): a zero-parameter paren-arrow lambda with no block or return whose sole expression combines two captured Copy scalars in one integer arithmetic body (`() -> i32 => a + b`), on both the direct and copy-chain edges, reusing the parameter-binary ComparisonReturn keyed to environment parameters zero and one; subtraction exits 42 and capture-plus-literal, mixed-parameter expression bodies, and flag-off fail closed ZOM4099 with zero incidents. Mixed parameter-plus-capture arithmetic expression-body lambda landed 2026-11-07 (`70a4cc37`, `8722c932`, `7814a4b9`, `747d3584`): a one-parameter paren-arrow lambda with no block or return combining its declared by-value i32 parameter with one Copy capture in one arithmetic body (`(n) -> i32 => n + a`), in either operand order, on both edges, under the existing declared-first ABI; two-parameter-plus-capture and capture-plus-literal expression bodies and flag-off fail closed ZOM4099 with zero incidents. Capture-plus-literal arithmetic expression-body lambda landed 2026-11-07 (`a5487e4e`, `91494b0b`, `78d965fa`, `dde9947e`): a zero-parameter paren-arrow lambda with no block or return combining one captured Copy scalar with an integer literal in one arithmetic body (`() -> i32 => a + 1`), in either operand order, on both edges, reusing the capture-plus-literal constant arm (the literal folds inside the callee and the call passes only the captured environment argument); two-literal, parameter-plus-literal, and flag-off bodies fail closed ZOM4099 with zero incidents. The bounded two-operand i32 Copy-only closure matrix (zero/one/two declared parameters crossed with zero/one/two captures and identity/arithmetic/literal-operand bodies, both spellings and edges) is now complete; the non-RFC-0007 remainder is documented as separate shapes (zero-parameter literal expression body, parameter-plus-literal arithmetic, then unary/nested/relational-bool and wider ladders); shared/reference/move captures stay in slice 3, blocked on RFC 0007. | captured-copied-closure-value-consumer and two-capturing-copied-closure-consumer native exit 42 (--experimental-captures), mixed-captured-parameter-closure-consumer and mixed-captured-parameter-direct-closure-consumer native exit 42, captured-literal-arithmetic-closure-consumer native exit 42 (all --experimental-captures), session ztests positive and flag-off drain, boundary cases (three captures, two parameters, two literal operands, nested binary, parameter-plus-literal, non-literal, non-i32, no flag) fail closed with ZOM4099, CLI native/object suites. **Milestone closed 2026-11-06**: full sanitizer ctest pass (984.23 s, 0 failures; 265 unittest binaries plus lit; 153 hir-module, 21 closure session ztests), corpus process parity checked 1105 sources byte-parallel (snapshot unchanged). Captured Copy scalar values now ride both the direct and indirect-call edges through identity, arithmetic, and one-parameter-mixed bodies; the next capture front is slice 3, blocked on RFC 0007. **Capture-plus-literal milestone closed 2026-11-06**: full sanitizer ctest pass (973.74 s, 0 failures; 265 unittest binaries plus lit; 153 hir-module; 22 closure session ztests including the new capture-plus-literal case), corpus process parity checked 1105 sources byte-parallel (snapshot unchanged), native-run-cli green including the three new 2026-11-06 fixtures. **Conformance pos/neg closeout 2026-11-06** (`6d2f85f1`, `e446b90e`, `149bd3d9`): the mixed parameter-plus-capture and capture-plus-literal (both operand orders) forms now carry diagnostics lit cases, golden AST dumps, and grammar ACCEPT verdicts alongside three semantic negatives (parameter-plus-literal, two-literal, nested binary) and a flag-off negative; the three-way `conformance-ast-coverage` gate and the ANTLR oracle pass, and corpus process parity extended 1105 to 1111 sources with exactly six added keys and zero changed. Full sanitizer ctest 923.76 s with zero failures. **Expression-body lambda milestone closed 2026-11-06**: captured-expression-body-closure-consumer native exit 42 both edges, a positive plus three expression-body negatives through the three-way conformance suites and ANTLR oracle, 22 closure-family session ztests, full sanitizer ctest 1099.79 s zero failures, direct hir-module 153, corpus process parity extended 1111 to 1114 sources with three added keys and zero changed. **Two-parameter arithmetic closure milestone closed 2026-11-07**: two-parameter-arithmetic-closure-consumer native exit 42 both spellings and both edges (no flag), one positive plus three semantic negatives through the three-way conformance suites and ANTLR oracle, 24 closure-family session ztests, direct hir-module 153, corpus process parity extended 1114 to 1118 with four added keys and zero changed; full sanitizer ctest 897.98 s with the single load-induced `package-workspace-build` timeout confirmed flaky (isolated rerun passes 44.35 s). **Two-capture arithmetic expression-body milestone closed 2026-11-07**: two-captured-expression-body-closure-consumer native exit 42 both edges, one positive plus a flag-off and a capture-plus-literal negative through the three-way conformance suites and ANTLR oracle, 20 closure-family session ztests, direct hir-module 153, corpus process parity extended 1118 to 1120 with two added keys and zero changed; full sanitizer ctest 813.90 s with two heavy-ASan timeouts (body-primitive-binary-operation-test, inference-checked-facts-test) confirmed load-induced flakes on isolated rerun (450.26 s and 114.79 s pass). **Mixed parameter-capture expression-body milestone closed 2026-11-07**: mixed-captured-parameter-expression-body-closure-consumer native exit 42 both edges and operand orders, one positive plus a flag-off and a two-parameter-plus-capture negative through the three-way conformance suites and ANTLR oracle, 21 closure-family session ztests, direct hir-module 153, corpus process parity extended 1120 to 1122 with two added keys and zero changed; full sanitizer ctest 1057.61 s with four heavy-ASan/CLI timeouts (package-workspace-build, body-marker-proof-test, body-primitive-binary-operation-test, inference-checked-facts-test) all confirmed load-induced flakes on isolated rerun (46.26, 422.13, 408.39, 92.06 s pass). **Capture-plus-literal expression-body milestone closed 2026-11-07**: captured-literal-arithmetic-expression-body-closure-consumer native exit 42 both edges and operand orders; the old capture-plus-literal/two-capture expression-body negatives became admitted and were replaced by dedicated positives plus a two-literal negative; diagnostics and AST closure suites (51) and ANTLR oracle pass, direct hir-module 153, corpus process parity settles at 1121 sources (two added, three obsolete negatives removed, zero changed); full sanitizer ctest 940.33 s with three timeouts (async-async-unix-xthread-test, body-marker-proof-test, body-primitive-binary-operation-test) all confirmed load-induced flakes on isolated rerun (0.34, 450.68, 592.09 s pass). |
| 3 | Shared/mutable-reference and move captures | Not scheduled. **Blocking point:** the closure environment today is a by-value Copy scalar argument list validated by no ownership overlay; reference captures require proven loan regions spanning the closure origin to every indirect call, and move captures require capture-boundary move-out production plus drop suppression on the moved path. **Dependency:** RFC 0007 general region liveness, loans/references/regions completeness, the Built MIR ownership overlay, and publication of `VerifiedOwnershipFacts`; checker escape/capture-boundary production logic (currently fail-closed, no producer); an LIR reference/pointer carrier ABI for environment arguments beyond the one admitted raw-pointer slice. **Minimal prerequisite before scheduling:** one module containing a non-escaping closure with a single `&i32` reference capture reaches `VerifiedOwnershipFacts` on the production path with zero incidents, then the same shape through MIR ownership overlay and LIR with an explicit pointer-carrier contract; move captures additionally require move-path capture facts and drop elision. | Blocked on RFC 0007 ownership completion; no slice-3 implementation work is reachable before that prerequisite lands. |
| 4 | Escaping closures and function-typed parameters | Not scheduled | blocked on allocator ABI / backend general lowering |
| 5 | Generic closures through RFC 0056 instances; callable traits | Not scheduled | |

## Layer 0 prerequisite: closure DefId identity (measured 2026-11-03)

Implementing slice 1 after narrowing it to the reused Source edge exposed a
false premise in the measured prerequisite map above. A closure does NOT
today carry a `DefId`:

- `DefinitionKind::Closure` (0x15) is a non-stable kind:
  `definitionNamespaceFor` returns none in identity/key/definition-key.cc, so
  `DefinitionIdentityRecord::from` rejects it and no definition key/handle is
  ever interned.
- binder `addByDomain` (binder/metadata/definition-inventory.cc) routes
  Closure into `anonymousEntities`, whose handle is a dense per-owner
  `AnonymousOwnerLocalId`, never a `DefId`. A probe module with
  `fn entry(){ let f = fn..; return f(42); }` materializes exactly one
  definition (entry); the closure appears only in owner-body
  `closures()` / `anonymousEntities()`.
- `MirFunction.owner` and `MirCallableIdentity::Source` are hard-typed to
  `identity::DefId`; neither has an anonymous-local-key variant. So the reused
  Source direct edge cannot name a closure until closures hold DefIds.
- `HirBuilder::build` receives only CheckedModule + SemanticTypeStore (no
  interner), so the DefId must already exist in
  `boundModule.definitions().definitions()` at binder/driver materialization;
  it cannot be session-interned at HIR/MIR time.

Required layer-0 change (before the checker/HIR/MIR/LIR slice-1 wiring is
reachable):

1. identity: give Closure a stable content-addressed record. Map
   `definitionNamespaceFor(Closure)` to Value; construct the record WITHOUT an
   overload header (extend the `callable` test in DefinitionIdentityRecord::from
   so Closure is a Value-namespace non-callable). Synthesize a deterministic,
   canonical-scalar-valid name from the AnonymousOwnerLocalKey path
   components (e.g. `__closure_<underscore-joined path>`) under the enclosing
   stable body owner (module or definition), so identical source interns an
   identical DefId.
2. binder/driver: intern one DefId per StableClosureFact and merge it into the
   module's MaterializedDefinitionInventory as a Closure definition at the
   closure expression node (DeclarationDefinitionSite). Injection point is the
   owner-body materialization / ImmutableDefinitionInventory::from path, which
   already sees both interners and anonymousEntities; relax the
   `definitions.size() == identities.definitions().size()` assertion there to
   include the interned closure DefIds and keep AnonymousOwnerLocalId mapping
   alongside (capture facts still key on the anonymous key).
3. Re-validate canonical codec round-trips, stable-witness schema counts, and
   re-record corpus IR/process parity; run full sanitizer ctest.

Groundwork already landed (commit b8bfff63, behavior-preserving, tests green):
both closure spellings (`fn` FunctionExpression and paren-arrow LambdaExpression)
recognized by signature publication, ClosureValue shape, and the production
switch; HIR `returnsClosureAliasCall` shape and `resolveClosureAliasCallee`
helper added but unreachable until this layer lands. body-body-checker 24,
facts-signature-facts 48, hir-module 153.
