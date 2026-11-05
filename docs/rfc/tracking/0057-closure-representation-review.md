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
| 2 | Unboxed code/environment pair; by-value scalar captures at call-site-local closures | Landed (flagged) via RFC 0058 B1/B2 (`4af11649`, `1fa01bf2`, `8e43639f`): one/two Copy scalar captures on the reused direct-call edge, not yet the env-pair/indirect-call carrier | two-capturing-closure-consumer native exit 42, conformance pos/neg, parity 1105, full sanitizer ctest |
| 3 | Shared/mutable-reference and move captures | Not scheduled | blocked on RFC 0007 ownership completion |
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
