# ZOM Remaining Work Task List

Status snapshot: 2026-09-21. Layered maturity estimate: frontend ~80%,
type system ~65% (known representability cracks), middle-end ~35%,
backend ~15% (minimal verified slice, real ELF + native exec on Linux x86-64).

Tasks are ordered by dependency. Each line names the work, the owning RFC (if
any), and the rough layer. "ICE" entries are compiler crashes observed on
in-repo corpus that must become source diagnostics.

## Resolved

- 2026-09-22. Parser/spec alignment for binding and associated-type member
  forms. The hand-written parser accepted two forms the grammar reference,
  `ZomParser.g4`, and the semantic chapters reject, and the mismatch only
  surfaced as downstream checker invariants:
  - `const` without an initializer was accepted at module/block scope and as a
    class/impl member, then rejected by the checker. The parser now enforces the
    `constDeclarationList` rule (Ch.06, Ch.17) with ZOM2106
    `ConstInitializerRequired`. The checker's ZOM4086 of the same name became
    unreachable and was removed; the failure moved from the checker to the
    parser.
  - An associated type member (`type Item;`) was accepted inside a class,
    struct, or error body even though `classMember`/`structMember` define no
    such element; it is now a parser error ZOM2107
    `AssociatedTypeMemberNotAllowed`. An impl block accepts only the assignment
    form (`type Item = T;`, mandatory `=`, no bound clause), matching
    `implMember`. Impl assignment is now fully implemented (see A7e).
  - A module-scope `let`/`mut` without an initializer is now refused honestly on
    the signature rail with ZOM4087 `ModuleBindingInitializerRequired` after the
    annotation is analyzed (so an ill-formed annotation still reports its own
    diagnostic), replacing an HIR-stage internal compiler error. Definite
    assignment remains specified but unimplemented.
  Also fixed a pre-existing architecture-gate failure by including the canonical
  `checker/body/marker-proof.h` from `mir/build/mir-builder.h` instead of a
  duplicate `MarkerProofEngine` forward declaration. Full serial sanitizer
  suite (335/335) and every architecture/coverage gate pass.

## Phase A — Close the checker representability cracks (unblocks the middle end)

- [x] A1. AST node/token id space: AttributeList id 0x000 collides with
  SyntaxKind::Unknown; any attributed parameter is seen as Unknown and ICEs
  in the body checker. DONE 2026-09-21 (commit on feat/remaining-work):
  gen_ast.py kNodeKindBase 0x200 offsets emitted node values + category ranges;
  schema fingerprint and name-keyed node-schema lookup unchanged; attributed
  `#[zom::param::move] x: i32` now compiles. Also fixed the interface-body
  bracket heuristic in pattern-parser that misfired ZOM2077 on an attributed
  parameter (outer `#[...]` brackets are not array types).
- [x] A2. Move-self receiver (RFC 0005 OS-3, ZOM4003). DONE 2026-09-22: the OS-3
  detector was dead because `isReceiverName` read the parameter name word as an
  ast::NodeId and compared the node kind to the ThisKeyword *token* kind, while
  that word stores an ast::IdentId; it returned false for every receiver, so
  MovesSelfCause never fired. Rewrote it to compare the interned identifier
  text against `this`, matching the canonical header reader. dyn_move_self_neg_01
  now emits ZOM4003 at the type-formation site; ordinary move parameters are
  unaffected and the other object-safety diagnostics (ZOM4001/4002/4005/4007)
  are unchanged. The positive object-safe dyn *value* path remains the A8 slice;
  the earlier "buildCallableParameters fails / MissingRequiredFact" diagnosis was
  stale — buildDyn/internExistential already succeed (a mismatched initializer
  reports ZOM4009 with the existential printed).
- [x] A3. Interface inheritance signature publication (RFC 0005 OS-0, ZOM4008).
  PARTIAL 2026-09-22: the binder only resolves names inside detached declaration
  and member bodies, so an interface heritage header never got a node binding
  for its parent path and MarkerShapeInventoryBuilder failed closed with a
  MissingRequiredFact invariant — a safe locally-defined `interface Child : Base`
  ICEd even without dyn. Added a checker fallback that resolves a relative
  single-segment heritage path to a unique locally-defined named type, preferring
  the binder binding when present. Safe local single/multiple inheritance now
  publishes (signature-facts ztest) and dyn over a child of an object-unsafe
  super emits ZOM4008 (lit). An explicit parent-graph back-edge check now
  rejects self inheritance and behavior-bearing inheritance cycles, which the
  readiness fixpoint alone let through because member-bearing interfaces are
  pre-classified (ztest). Still fail-closed invariants, deferred to A3b:
  undefined/imported parent, a non-interface parent, and duplicate parents
  (these need the structural marker-shape inventory to carry source failures
  instead of a bare reject; cycles also deserve a user diagnostic); inherited
  members/associated types are still reference-only, not added to the child
  inventory (A10/A11/A12).
- A4. Pre-monomorphization parametric return: `fun f<T>() -> T` hits the
  borrow-rail ZOM4083; slice `[T]` parameter hits an InvalidFact in
  borrow classification. Decide and implement the parametric region/borrow
  contract so these signatures build. (RFC 0007 boundary)
- A5. dyn marker bounds. STORE LAYER DONE 2026-09-22: the canonical semantic
  type key now encodes a non-empty existential marker sequence in strictly
  ascending canonical order with no duplicates (wrong kind/foreign handle fails
  closed), replacing the blanket reject and the hardcoded size-0 marker
  sequence; empty-marker key bytes are unchanged. Remaining: the production
  producer (`buildDyn` still hardcodes an empty marker vector and never reads
  `kDynTypeExprMarkersIdWord`), marker-name resolution with a closed unknown
  diagnostic, source-side sorting, and multi-qualified markers, so `dyn I + M`
  is still not observable from source.
- A6. DONE 2026-09-22: a dyn principal resolving to a struct, class, enum, or
  error emits ZOM4110 DynPrincipalNotInterface (with the offending definition)
  at the principal node across every drained type position; parenthesized
  principals are identical (parser strips them). Type-alias principals stay on
  ZOM4106 until A11; unresolvable principals in parameter/return positions
  (binder does not cover those positions) remain an invariant.
- A7. DONE 2026-09-22: unknown dyn head binding emits ZOM4108
  DynUnknownAssociatedTypeBinding at the binding node, carrying the written
  name (new SignatureIdentifierDisplayArg) and the principal interface. A name
  inherited from a super-interface emits ZOM4109
  DynInheritedAssociatedTypeBindingUnsupported until inherited head bindings
  are actually built. ZOM4055 duplicate bindings now anchor at the binding and
  pass the principal as its second argument. The SourceTypeBuilder sink reaches
  every signature type-build position (parameters, returns, fields, variants,
  generics, impls, marker impls).
- A7b. DONE 2026-09-22 (with A3b): malformed interface heritage clauses emit
  ZOM4111 HeritageParentNotFound, ZOM4112 HeritageParentNotInterface, ZOM4113
  HeritageDuplicateParent, ZOM4114 HeritageParentIsTypeParameter, and ZOM4115
  HeritageCycle, through InterfaceHeritageValidator as a per-user-module
  pre-gate and a per-module SignatureFactsBuilder check. Structural/authority
  malformations and toolchain/core modules keep the invariant rail.
  Remaining: qualified/imported heritage parents resolve as ZOM4111 (cross-
  module heritage resolution), heritage parent type arguments are ignored by
  the validator (generic behavior interfaces still ICE on bad args), and the
  O(N x D) local-name scan makes the 256-parent corpus slow under ASan.
- A7c. DONE 2026-09-23: `dyn I` in an interface-bound position is a closed
  source error everywhere the bound rail runs — generic parameter bounds
  (`fun f<T: dyn I>()`), associated type bounds (`type X : dyn I;`), and impl
  `where` predicates (`impl I for T where T: dyn I`) — via
  ZOM4121 DynTypeNotAllowedAsBound emitted at the single SourceTypeBuilder
  bound funnel. Function-level `where` clauses are still parser-unparsed
  (ZOM2076), so they never reach this rail.
- A7d. DONE 2026-09-23: the shared SourceTypeBuilder name-resolution funnel
  now closes an unresolvable named type at every signature position
  (parameters, returns, fields, variants, impl self/interface/targets,
  associated type defaults, type arguments, and the dyn principal) with
  ZOM4120 TypeNameUnresolved, replacing the long-standing dd0442 invariant for
  unresolved type-position names (binder covers let annotations as ZOM3001).
  Associated type bounds additionally classify an unresolved bound as
  ZOM4118 AssociatedTypeBoundNotFound and a non-interface bound as ZOM4119
  AssociatedTypeBoundNotInterface. A repeated bound (`I + I`) on either a
  generic parameter or an associated type is ZOM4122 DuplicateInterfaceBound;
  the associated-type and generic-parameter signature encoders now canonicalize
  their bound order, which also fixed a pre-existing ICE on any legal
  multi-bound parameter (`fun f<T: A + B>()`).
- A7e. DONE 2026-09-23: impl associated type assignment is implemented end to
  end. An interface-declared associated type now publishes a real
  AssociatedTypeSignature (generic parameters, ordered bounds, marker bounds,
  optional default) instead of being skipped, which closes the downstream
  driver MissingProjection module-interface incident. The impl head consumes
  each `type Name = T;` assignment: a name not declared by the interface is
  ZOM4116 ImplAssociatedTypeNotMember, an impl that omits a required
  non-generic associated type is ZOM4117 ImplMissingAssociatedType, and a GAT
  assignment (`type Iter<T> = U;`) is ZOM4123
  ImplGenericAssociatedTypeUnsupported until GAT substitution lands. The
  now-unreachable ZOM4107 was removed.
- A8. PARTIAL 2026-09-23 (checker coercion rail landed; HIR/MIR carrier open):
  `let d: dyn D = sprite` now produces a real, verifier-adopted RFC 0005
  `CoercionAdjustment` with one `DynErase` step at annotated initializer
  sites. The body checker selects the unique non-generic impl of a bare
  object-safe principal for a non-generic nominal initializer (matching
  associated bindings), binds the local to the declared existential while
  keeping the initializer node at the concrete type, and publishes a witness
  record. A concrete type with no such impl is the source diagnostic
  ZOM4018 `CheckerTraitNotImplemented` (producer Obligation / recovery class
  FailedObligation) instead of the former dd0442/08865 invariant. A concrete
  nominal that has an impl but falls outside the non-generic erasure slice
  (generic self type, generic impl) is ZOM4124
  `GenericConcreteDynErasureUnsupported`, never a false 4018. A dyn-to-dyn
  identity copy (an already-existential parameter or annotated local moved
  into the same `dyn` type) records no coercion and is accepted; references to
  an erased local resolve to the bound existential type, so an unsound
  dyn-to-concrete downcast is rejected with ZOM4009. The HIR boundary fails
  closed: a single-step DynErase reaches HIR construction and is rejected as a
  per-definition *capability* failure (UnsupportedSourceConstruct -> ZOM4099),
  never silently scalar-lowered and never an invariant; the failure is
  attributed to the innermost enclosing executable definition. Open: the
  actual HIR erasure node, MIR rvalue, LIR 2-word fat-pointer/vtable layout
  and vtable globals (no object code yet), plus argument-position
  (`CheckedArgumentFact.adjustment`), return, assignment, generic impls,
  markers, and additional interfaces. The positive coercion, identity-copy,
  downcast-rejection, and generic-rejection shapes are covered by body ztests
  through the full verifier; CLI lit covers the 4018/4124 negatives (positive
  CLI lit is masked by the unrelated pre-existing impl-method-body c28f1 gap).
- A8b. Open (next A8 step): argument-position concrete-to-dyn erasure
  (`render(concrate)` to a `dyn I` parameter) via
  `CheckedArgumentFact.adjustment` with `CoercionSite::Argument`; the call
  envelope and `validArgument` already cross-check the adjustment, so this
  reuses the same selector and witness path.
- A9. dyn upcast `dyn I as dyn J` (ZOM4044) and invalid-upcast rejection;
  vtable/super offset representation. Depends on A3.
- A10. dyn method calls / trait dispatch through an existential receiver.
- A11. Type-alias expansion (ZOM4106 currently rejects all alias targets):
  transparent alias resolution for types; associated-type projection
  (`<T as I>::Item`, `T::Item`, GATs, defaults).
- A12. GATs with bounds and default associated types at value/body level.

## Phase B — Finish the middle end (RFC 0048 recursive builder)

- B1. Migrate remaining statement families off the legacy shape rail:
  full if/else (not only 4-block diamonds), arbitrary while/loop nesting,
  for/iteration, match/SwitchValue, unreachable/break/continue,
  expression statements.
- B2. Unsafe blocks and unsafe expressions (RFC 0007).
- B3. Borrow expressions: references (`&T`/`&mut T`), reborrows, deref;
  function arguments that are references.
- B4. Composite rvalues: tuples, tuple structs, full enums (not just field
  folds), closures/lambdas, fn pointers.
- B5. Enum construction and pattern matching lowering (scrutinee + arms);
  bind/move/copy arm semantics.
- B6. Empty-family and module-statement bodies (ZOM4095 body stage; the
  BodyFactRequirementInventory 3rd OneOf alternative + 2 session call sites).
- B7. Closures: capture analysis, closure environment lowering.
- B8. Phase 3-5 of RFC 0048: delete the legacy shape rails, generalize
  control/unsafe/borrow/composites, then remove the temporary MIR rail the
  new LIR translation validator currently inherits polarity from.

## Phase C — Ownership / borrow / lifetime (RFC 0007, the big one)

- C1. General region liveness beyond the 4-block/5-block fixed shapes
  (already partial: move-source facts, reducible while).
- C2. Move-out / move-in / return-of-loan enforcement end to end.
- C3. Loan conflicts (shared vs mutable) wired to ZOM4056-4062 in production.
- C4. Linear resource consumption and the Linear marker boundary;
  ZOM4063 on unconsumed Linear values.
- C5. Marker trait proofs forwarded from interface nominal types to
  existentials in dyn/borrow positions (marker-proof forwarding gap).
- C6. Destructor/drop elaboration for arbitrary types (runtime `__zom_drop`
  landed but consumer is fixed-shape); integrate with general CFG.
- C7. Unsafe marker/safety boundary (ZOM4091/4092) and extern raw pointer
  boundary.
- C8. NLL-style region inference at general CFG (not just reducible shapes).

## Phase D — Error handling ABI (RFC 0006 + RFC 0046)

- D1. Error union semantic type (`error` / `T!`) and raises-carrying
  function signatures (currently raises is rejected before a type exists).
- D2. `?` propagation, `!` unwrap, forced error operators with real facts
  (not capability stubs).
- D3. try/catch surface and panic boundaries.
- D4. Error lowering: landing pads / return-code ABI choice across MIR/LIR;
  forced-error panic/abort ABI (RFC 0046, DRAFT).
- D5. Error-region output relationships in borrow.

## Phase E — Generics / monomorphization (gate to general codegen)

- E1. Generic function and generic type instantiation pipeline
  (substitution + witness args -> InstanceId).
- E2. Trait bound solving / coherence checks in the body (beyond signature
  admission): positive + negative coherence, orphan rule at impl bodies.
- E3. Monomorphization pass producing per-instance LIR; makes InstanceId
  exist so per-instance LIR verification faults become representable.
- E4. Generic trait methods / associated types in codegen.

## Phase F — Backend generalization (RFC 0010/0021)

- F1. General MIR->LIR legalization beyond admitted shapes (arbitrary SSA,
  phi, general call ABI).
- F2. Target-aware ABI: parameter/return passing per SysV ABI, real register
  use (replace placeholder carrier-only lowering).
- F3. General aggregate codegen: full structs/enums by value, fat pointers
  for slices/trait objects.
- F4. Real reference/raw-pointer lowering.
- F5. AArch64 backend and Mach-O object emission (today Linux x86-64 ELF
  only).
- F6. Cross-target rejection through the target registry / capability matrix.
- F7. Full linked executable beyond the bundled start entry: libc interop,
  real `main`, args/env.

## Phase G — Runtime

- G1. Real allocator integration / heap operations (new/delete).
- G2. GC or ownership runtime hooks for the Copy/Linear model.
- G3. Panic/unwind runtime ABI (currently a fixed abort shim).
- G4. Drop glue codegen for all shapes; runtime __zom_* ABI completion.
- G5. Static initializers / module init execution semantics.

## Phase H — Concurrency (chapter 15, design only today)

- H1. async/await and Future state machine lowering (coroutine elaboration
  RFC 0007).
- H2. Task / nursery scheduling runtime.
- H3. Channel primitives and Sendable/Sync marker enforcement across capture.
- H4. Mutex/Shared primitives and their thread-safety proofs.
- H5. Cancel primitives.

## Phase I — FFI / interop (chapter 18)

- I1. extern fn declarations with real ABI (binder accepts shape today).
- I2. C type interop (C strings, raw pointers, struct layout).
- I3. `#[zom::ffi]` / `@foreign` blocks and safe wrappers.
- I4. Linker invocation for external libraries beyond the bundled runtime.

## Phase J — Standard / core library

- J1. Source-backed core library (RFC 0025): today core::prelude marker
  names are recognized but there are zero self-hosted .zom library sources.
- J2. Primitive methods (int arithmetic full set, string, array, slice,
  option/result combinators).
- J3. Collections: Vector, String, HashMap implemented in ZOM.
- J4. Core distribution packaging/install and std stability ladder.

## Phase K — IDE / tooling (RFC 0023)

- K1. Finish CST->AST replacement (RFC 0023 D1-D5); text-sync atomically
  coupled to crate resolution.
- K2. LSP server `zomc lsp`: T1 transport landed; T2 text sync, T3
  lifecycle + crate resolution needed.
- K3. Semantic tokens, hover, completion, go-to-def, diagnostics over
  snapshots.
- K4. IDE service 3b: crate resolution in the single-file/IDE path.

## Phase L — Process / governance / verification

- L1. Finish RFC 0053 review fixes: verification B3 mutation coverage
  (translation-validator to >=70% line coverage + per-branch matrix),
  ir-backend N1-N7, RFC doc corrections (rfc B2-B6), then re-review owners
  and move DRAFT -> REVIEW -> ACCEPTED.
- L2. Incremental query closure (RFC 0017/0018/0019/0020/0027/0028): the
  IDE unsealed path works but production query final-seal lifecycle is a
  known gap; complete one-shot/batch terminal sealing.
- L3. Re-record the IR/process parity baseline once remaining checker ICEs
  reach zero (currently 9/61 sample ICE).
- L4. Coverage CI lane with backend-enabled instrumented build.

## Suggested critical path

A1 (id collision) -> A3/A4/A5 (representability) -> B8 (remove shape rails)
-> C1-C8 (ownership) interleaved with E1-E3 (monomorphization) -> F1-F3
(general codegen) -> J1-J2 (core lib) is the route from "runs scalar
programs" to "runs general ZOM programs". D and H can proceed in parallel
once B and C land.
