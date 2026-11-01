# Monomorphized Instance Set

Updated: 2026-10-31

## Authority And Status

| Field | Value |
|---|---|
| Authority | Non-normative compiler implementation guide |
| Coverage | RFC 0056 stage 3 monomorphization (free, unbounded generic functions over admitted scalar carriers) |
| Governing decisions | [RFC 0056](../../rfc/0056-generic-function-monomorphization.md) |
| Production implementation | [`mono/instance-cloner.h`](../../../compiler/mono/instance-cloner.h), [`mono/instance-fixpoint.h`](../../../compiler/mono/instance-fixpoint.h), [`mono/instance-set.h`](../../../compiler/mono/instance-set.h), [`mono/instance-set-builder.h`](../../../compiler/mono/instance-set-builder.h) |
| Session stage | phase `0x0a` inside `CompilerSession::checkSources()`, immediately after executable MIR verification |
| Native verification | [`tests/unittests/compiler/mono/`](../../../tests/unittests/compiler/mono/) |

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
2. every carrier type is an admitted scalar (`i32`, `bool`), independently
   re-checking the checker's carrier gate;
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

In stage 3 no production consumer lowers instances: the CLI still rejects
object emission through the existing path, and `compile --check` observes the
0x0a result. Stage 4 adds the joined Source+instance presentation, the
instance-aware LIR branch before the legacy function-count ladders, derived
`zom.mono.<definition-key hex>.<digest>` symbols, and the `--dump-mir`
"instances" section. Generic templates remain visible in the source MIR dump;
instance symbols never participate in `zom.module_init` entry selection.

## Stage 4 Implementation Map (measured 2026-11-01)

Facts established before starting the LIR/CLI work:

- A cloned instance `MirFunction` keeps the TEMPLATE's `identity::DefId owner`
  (`instance-cloner.cc` returns `MirFunction{templateFunction.owner, ...}`).
  Two instances of one template therefore have identical `owner`. Identity
  discrimination lives ONLY on the terminator edge:
  `MirCallTerminator.callee` is a full `MirCallableIdentity` (Source DefId or
  Instance InstanceId). Do NOT widen `MirFunction.owner`; instead resolve
  callees through an identity-to-index map.
- The zomc binary-emission ladder in `utils/zomc/zomc.cc::buildNativeObject`
  currently reads `session->getOwnershipCheckedMirModules()[0].builtMir()`
  only. It must additionally read
  `session->getVerifiedMonoInstanceSets()[0].instances()` and build a
  presented ordered vector of `const MirFunction*`:
    1. every Source function that is NOT a parameterized template, then
    2. every instance body, in byte-sorted identity-frame order.
  Parameterized templates are excluded (their concrete work is already
  represented by instances).
- Caller/callee selection inside the ladder matches by
  `call.callee.isSourceFor(functions[i].owner)` at 10 sites. For instance
  edges this is always false. Introduce one resolver:
  `resolveFunctionIndex(MirCallableIdentity) -> Maybe<size_t>` backed by a map
  built from the presented vector (Source keyed by DefId, Instance keyed by
  InstanceId), and route the ladder's identity comparisons through it rather
  than block-count/owner heuristics.
- `compiler/lir/verify/translation-validator.cc` has 31 callee/owner
  comparison sites. Route them through the same resolver/helper so an Instance
  call edge validates against the instance body, not the template.
- `MirFunctionCanonicalCodec` is shared; instance bodies already encode
  byte-exactly. LLVM symbol naming derives from
  `zom.mono.<definition-key hex>.<digest>` using the InstanceId frame.
- Existing acceptance target: `identity(42)` compiles, links, runs, exits 42.
  The reject tuple lives at `tests/CMakeLists.txt`
  (`reject-generic-function-consumer/Zom.toml::...`); remove it and replace
  with a native-run tuple plus an object-byte determinism ztest.
