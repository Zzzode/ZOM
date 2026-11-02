# Monomorphized Instance Set

Updated: 2026-11-01

## Authority And Status

| Field | Value |
|---|---|
| Authority | Non-normative compiler implementation guide |
| Coverage | RFC 0056 stages 3-4: the verified monomorphization fixpoint and its native single-argument scalar instance-call slice |
| Governing decisions | [RFC 0056](../../rfc/0056-generic-function-monomorphization.md) |
| Production implementation | [`mono/instance-cloner.h`](../../../compiler/mono/instance-cloner.h), [`mono/instance-fixpoint.h`](../../../compiler/mono/instance-fixpoint.h), [`mono/instance-set.h`](../../../compiler/mono/instance-set.h), [`mono/instance-set-builder.h`](../../../compiler/mono/instance-set-builder.h), [`ir/instance-identity.h`](../../../compiler/ir/instance-identity.h) |
| Session stage | phase `0x0a` inside `CompilerSession::checkSources()`, immediately after executable MIR verification |
| Native verification | [`tests/unittests/compiler/mono/`](../../../tests/unittests/compiler/mono/), [`tests/unittests/compiler/ir/instance-identity-test.cc`](../../../tests/unittests/compiler/ir/instance-identity-test.cc), `tests/integration/core-library/generic-identity-consumer/` |

The monomorphized instance set is the post-executable-MIR artifact that turns
parametric generic templates into concrete functions. One independently
verified `VerifiedMonoInstanceSet` is published per ordinary module in the same
`checkSources` transaction as the executable MIR wrappers. A module with zero
Instance edges publishes a valid empty set; its revision is still computed and
reproduced across builds.

## Template And Instance Distinction

Generic functions remain Source-owned parametric `MirFunction` values in the
source Built MIR module. They are never members of the instance set and never
reach the LIR/object presented set (their carriers contain unbound type
parameters). Only a call edge carrying
`MirCallableIdentity::Instance(ir::InstanceId)` demands an instance.

Each `MonoInstance` pairs:

- its canonical `ir::InstanceId { DefId, CanonicalSubstitutionId, WitnessArgumentsId }`;
- a cloned post-elaboration `MirFunction` body with every `SemanticTypeId`
  carrier rewritten through `type::semantic::substituteType`;
- the length-framed instance identity bytes (the sort key);
- the independently re-encoded canonical body bytes;
- the span of the root call edge that first demanded it.

Instances are cloned from the post-drop/coroutine-elaboration verified
executable MIR template, so no instance silently skips cleanup elaboration.
For the admitted slice elaboration on a scalar template is a no-op, which the
ownership-triviality predicate independently enforces.

## Builder, Fixpoint, And Ordering

`MonoInstanceSetBuilder` collects instances from the non-generic Source roots
(the same "every bodied definition" set the backend lowers). Worklist order is
deterministic function/block/terminator order; newly appended instances are
rescanned FIFO. A previously seen instance is shared, so two calls with the
same substitution produce one specialization and same-substitution recursion
stays ordinary recursion. Final artifact order is a byte sort over the
expanded identity frames; roots keep source order.

The evidence provider interface (`MonoEvidenceProvider`) isolates the fixpoint
core from session storage: production supplies repository-backed substitution
and witness records and post-elaboration templates; component tests install
hand-built evidence so the fixpoint, guards, and predicates are exercised
without forging a sealed session artifact.

## Independent Verifier Predicates

`MonoInstanceSetVerifier` re-runs the fixpoint and accepts the candidate only
when every RFC 0056 predicate holds:

1. no residual carrier references any of the callee's generic parameter keys;
2. every carrier type is an admitted scalar (`i32`, `i64`, `bool`),
   independently re-checking the checker's carrier gate;
3. ownership triviality: no Reference or RawPointer carrier, zero
   BorrowCreation / UnsafeScopeBoundary / Deinitialize statements, and every
   call edge carries `MirCallEffect::noActivation()`;
4. template lineage: the body is re-derived from the same context's verified
   post-elaboration template at the folded built/overlay/facts revisions;
5. edge keys and arity: each Instance edge's substitution parameter keys and
   count equal the callee's ordered generic parameters and resolve to a member
   instance (edge closure);
6. identity frame ordering and the fixed instance/node budgets.

## Representable-But-Not-Source-Emitted Guards

Two capability kinds are encoded and component-tested but have no source-level
producer in the stage-3 slice:

- `IrFailureKind::RecursiveInstantiation` (ZOM6010): a chain whose head
  definition repeats with a strictly growing nested substitution. Generic
  bodies cannot call generic callees yet (the checker keeps that ZOM4099
  drain), so only a hand-built fixpoint provider reaches the guard.
- `IrFailureKind::InstantiationBudgetExceeded` (ZOM6011): the fixed instance
  count (`kMonomorphizationInstanceBudget`) or substituted-node count
  (`kMonomorphizationSubstitutionNodeBudget`) is exceeded. With two admitted
  scalar carriers and no generic-to-generic edges the limits are unreachable
  from source; the guard bounds compilation resource use for later slices.

A non-scalar, non-trivial, or edge-malformed instance is a capability rejection
(`UnsupportedSourceConstruct` projected to ZOM4099) owned by the instance; an
identity or codec fault is an invariant rejection, never a silent partial
publication.

## Revision And Determinism

`MonoInstanceSetRevisionCodec` is separate from `MirRevisionCodec`. Its frame
folds a distinct domain separator, the context fingerprint, expanded module
key, the post-elaboration built/overlay/facts lineage triple, and each
instance as one length-framed identity followed by its canonical body in
byte-sorted order. A package ztest builds the admitted identity program twice
in independent `checkSources` runs and asserts equality of the Built
`MirRevisionId`, overlay and facts revisions, and the instance-set revision;
zero-edge modules keep their empty-set revision.

## Consumers And Dump Surface

The stage 4 production consumer is the zomc native object path
(`utils/zomc/zomc.cc::buildNativeObject`). Before the function-count lowering
ladders, an explicit instance-aware branch scans the source Built MIR for a
caller whose entry terminator carries a `MirCallableIdentity::Instance` edge,
resolves the matching verified body from the published
`VerifiedMonoInstanceSet`, and lowers the caller/instance-body pair through
`lir::MirToLirLowering::lowerMonomorphicCallModuleWithArgument`. The branch
runs before the legacy ladders because an Instance edge never matches a
Source-owner lookup. When the branch declines, the module falls through to the
Source-only ladders unchanged.

Production-emitted slice (verified end to end):

- one source caller with the two-block call/return shape and zero parameters,
  folded to the reserved no-argument `zom.module_init` entry;
- one single-argument scalar instance call (`identity(42)`), whose instance
  body declares one parameter and returns it;
- the generic call is admitted in either of two positions: the function's
  direct return value (`return identity(42)`, a trailing Temporary plus
  FunctionResult), or the initializer of the single local that is then
  returned (`let y = identity(42); return y;`, one trailing UserLocal). The
  HIR gate (`hir-builder.cc`, `genericInitializerReturnAdmitted`) admits the
  initializer position only when the call is exactly that local's
  initializer with every other direct-call classification clear; statement,
  condition, receiver, projected, and error-operator positions keep draining;
- MIR -> LIR -> verified LLVM IR -> ELF object -> host link -> `zomc run`,
  exiting with the returned value.

Other instance shapes are represented in the verified instance set and its
dump but are not selected by the production lowering branch; they stay on the
existing fail-closed diagnostics.

### Instance symbols

The instance callee is emitted under a derived, content-addressed symbol from
`ir::deriveMonoInstanceSymbol`: `zom.mono.<definition-key hex>.<digest>`. The
digest is SHA-256 over the length-framed substitution record followed, from
the first non-empty witness record onward, by the length-framed witness
record. The domain cannot intersect `zom.module_init`, `zom.caller`,
`zom.callee`, `zom.leaf`, or `zom.standalone_<N>`. Two objects emitted in
independent processes are byte-identical (the
`generic-identity-consumer` deterministic object case).

### Translation validation

`lir::TranslationValidator::validate` accepts a presented-identities array
parallel to the presented MIR functions. Identity resolution is centralized
in one file-local `PresentedSet` whose owner-derived bijection maps both
presented-to-LIR and LIR-to-presented indices. Every callee-edge and owner
comparison in the validator resolves through one of its predicates:
`calleeMatches` (MIR edge vs LIR function index), `lirPresentsMir`,
`lirIndexOfMir`, or `edgeNamesMir` (MIR edge vs presented MIR role inside
shape matchers). A Source edge names the presented definition owner; an
Instance edge equals the presented instance identity. There is no per-site
allowlist, so two instances of one template cannot be misresolved at an
unlisted comparison. The Source-only `validate` overloads present no identity
array and keep Source-owner matching.

### MIR dump

`zomc compile --dump-mir` renders, after the per-module source function
records, one deterministic instances section per module: the instance-set
revision and, for each instance in byte-sorted identity order, the derived
symbol, the length-framed identity, and the canonical substituted body.
Generic parametric templates stay in the source function section; instance
symbols never participate in `zom.module_init` entry selection.

### Native acceptance

`tests/integration/core-library/generic-identity-consumer/` is the direct-
return native fixture: `fn identity<T>(value: T) -> T { return value; }`
called as `identity(42)` from a parameter-free entry. It is registered as a
`native-run-cli` run case (exit 42) and as both an object-emission case and a
cross-process deterministic-object case in `object-emission-cli`.

`tests/integration/core-library/generic-initializer-consumer/` is the
initializer-position fixture (`let y = identity(42); return y;`), registered as
a run case (exit 42) and an object-emission case. The two positions share the
single-argument instance LIR slice's trailing-count-two (direct return) and
trailing-count-one (initializer) arms.

`tests/integration/core-library/generic-i64-consumer/` is the i64 carrier
fixture (`identity<i64>(42)` in direct-return position), registered as a run
case (exit 42) and an object-emission case. i64 joins the admitted scalar set
{i32, i64, bool}; the LIR and LLVM rails already derive 64-bit width from the
semantic carrier. The runtime entry shim still passes only the low 32 bits as
the process exit code, so the fixture returns 42. The inferred literal form
(`identity(42)` under an i64 entry) and an owner-local i64 argument remain
outside this slice (the literal defaults to i32 and cannot bootstrap from the
return context, and the monomorphic caller argument matcher admits only
constants and leading parameters).
