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
| B0 | Zero-capture closure native execution (the prerequisite slice the RFC folds into Part B): point the checker closure-DefId resolution at the new Part A rail; publish the closure CallableSignature; then widen HIR/MIR/LIR one closure function per non-escaping direct-call module | In progress: B0-1/B0-2 landed; B0-3 HIR/MIR/LIR next | checker ztest green for both spellings; full ctest 322/322; `zomc run` exit 42 is B0-3 |
| B0-1 | `closureValueShape` reads the closure DefId from `closureDefinitionAt(node)` instead of the skeleton DefinitionFact list | Landed (e8cf1aac) | behavior preserving until B0-2 |
| B0-2 | Publish the zero-parameter, annotated, non-generic, non-raising CallableSignature for both closure spellings on the closure rail; admit Closure in isCallableDefinition, cross-rail `definition(DefId)`, header-less header match, and signature census | Landed (756be8db) | session ztest `PublishesZeroCaptureClosureSignature` (no Checker/Signature incident, both spellings); full sanitizer ctest 322/322 |
| B0-3 | HIR closure-alias call node, MIR closure MirFunction owned by the DefId, LIR Source-edge enumeration so `zomc run` exits 42 for a zero-capture closure | Next | native consumer fixture exit 42 |
| B1 | Flagged one-Copy-scalar capture on top of B0: unboxed `{code,env}` carrier, HIR leading environment parameter + field projection, implicit context argument, MIR leading env local + frame-relative operand, LIR leading pointer parameter | Proposed | capturing-closure-consumer native exit, checker/HIR/MIR/LIR ztests, conformance pos/neg, parity |
| B2 | Bounded multi-Copy-scalar capture set (canonical syntax-path field order) | Proposed | multi-capture consumer |
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

