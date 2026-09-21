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
    `AssociatedTypeMemberNotAllowed`. An impl block still accepts only the
    assignment form (`type Item = T;`, mandatory `=`, no bound clause), matching
    `implMember`. ZOM4107 `AssociatedTypeMemberUnsupported` stays reachable for
    the still-unimplemented impl assignment and is covered by
    `impl_assoc_type_unsupported_neg_34`.
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
- A3. Interface inheritance signature publication (RFC 0005 OS-0, ZOM4008):
  safe `interface Child : Base` ICEs even without dyn; admit parent interface
  signatures and verify inherited associated types / methods.
- A4. Pre-monomorphization parametric return: `fun f<T>() -> T` hits the
  borrow-rail ZOM4083; slice `[T]` parameter hits an InvalidFact in
  borrow classification. Decide and implement the parametric region/borrow
  contract so these signatures build. (RFC 0007 boundary)
- A5. dyn marker bounds: type store `validateMarkerFacts` rejects non-empty
  marker lists and `encodeExistential` hardcodes 0 markers. Enable
  `dyn I + M + N` interning (sorted markers), then multi-qualified markers.
- A6. dyn existential head rest: principal that is not a bare NamedTypeExpr
  (parenthesized, qualified) currently falls through to invariant.
- A7. dyn unknown associated-type binding (`dyn I<Item=u8>` when I has no
  Item): emit a closed diagnostic (no reserved code; add one or reuse
  ZOM4020 with the correct args) instead of invariant.
- A8. Concrete-to-dyn erasure at value positions (RFC 0005 coercion table):
  `let d: dyn D = sprite`. New checked coercion fact + HIR/MIR coercion
  carrier. (checker/hir/mir)
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
