# Semantic HIR

Updated: 2026-09-14

## Authority And Status

| Field | Value |
|---|---|
| Authority | Non-normative compiler implementation guide |
| Coverage | Partial production HIR; the admitted constructor families lower through a recursive destination-driven builder, other shapes through the legacy materializer |
| Governing decisions | [RFC 0010](../../rfc/0010-intermediate-representation-pipeline.md), [RFC 0048](../../rfc/0048-recursive-ir-construction-and-structural-verification.md) (ACCEPTED, Phase 2 in progress), [RFC 0013](../../rfc/0013-ownership-analysis-integration-boundary.md) |
| Production implementation | [`compiler/hir`](../../../compiler/hir/) |
| Session integration | [`compiler-session.cc`](../../../compiler/driver/session/compiler-session.cc) |
| Native verification | [`hir-module-test.cc`](../../../tests/unittests/compiler/hir/hir-module-test.cc), [`compiler-session-package-test.cc`](../../../tests/unittests/compiler/driver/compiler-session-package-test.cc) |

Semantic HIR is a live, immutable, session-published capability. Its
construction is mid-migration under RFC 0048: the admitted constructor
families (literal/reference, binary, aggregate field projection, and mutable
local writes) are produced by the recursive `HirFnCtx` destination-driven
builder, and the remaining shapes still use the legacy pending-record
materializer. Both paths create the same records and emit byte-identical HIR
and downstream MIR. This note describes the live profile; the construct-level
list is [lowerable-constructs.md](lowerable-constructs.md).

## Role In The Pipeline

`HirBuilder` consumes one `VerifiedCheckedModule`. That handoff binds:

- the semantic context, package, crate, module, source, and parsed receipt;
- checked and dispatch revisions;
- verified borrow evidence and its repository lease;
- the module's own interface revision and imported interface revisions; and
- the canonical semantic type store used by the session.

The builder dispatches each pending body to a recursive family arm in
`compiler/hir/build/` or to the legacy materialization path, producing an
untrusted `HirModuleCandidate` that retains the exact verified handoff.
`HirVerifier` independently checks the candidate through those retained
semantic authorities and is the sole creator of `VerifiedHirModule`. The live
downstream consumer is `BuiltMirBuilder`.

## Representation

HIR is an expression-bearing, source-shaped IR of flat record pools keyed by
one-based deterministic `HirNodeId`:

| Group | Records |
|---|---|
| Declarations and bindings | `HirValueDeclaration`, `HirFunctionDeclaration`, `HirBindingPattern`, `HirLocalBinding`, `HirParameter` |
| Leaves and references | `HirScalarLiteralExpression`, `HirParameterReferenceExpression`, `HirLocalReferenceExpression`, `HirParameterIndexExpression` |
| Aggregates and places | `HirNominalAggregateElement`, `HirNominalAggregateExpression`, `HirLocalFieldProjectionExpression`, `HirLocalBorrowExpression`, `HirParameterReborrowExpression` |
| Operations | `HirPrimitiveBinaryExpression` (comparison, arithmetic, and nested one-level operands), `HirDirectCallExpression` plus `HirDirectCallArgument`, `HirReceiverCallExpression` |
| Statements | `HirLocalWriteStatement`, `HirReturnStatement`, `HirBlockStatement`, `HirLoopStatement` |
| Control | `HirConditionalExpression`, `HirUnsafeBlockExpression` |

Expression records carry `HirValueCategory` (`Value` or `Place`); both are
emitted on live paths (place-returning references and projections emit
`Place`). Source ranges and canonical `DefId` values preserve traceability
without making syntax-tree identity part of the HIR contract.

## Production Profile

The recursive builder arms landed under RFC 0048 produce, byte-identically to
the legacy path:

1. **Family 1, literal/reference:** bare `return <literal>` /
   `return <parameter>` and a single initialized local returned by name;
2. **Family 2, binary:** return-position primitive comparison/arithmetic with
   literal or parameter operands, sequential N-local initializers, and
   one-level nested binary initializers (`a + b * c`) through synthesized
   temporary locals;
3. **Family 3, aggregate:** `let cell = T { field: <literal>, .. }; return
   cell.field` field projection returns;
4. **Family 4, local write:** `mut x` initializers followed by repeated writes
   whose RHS is a scalar literal, parameter reference, or primitive binary.

The admitted set also includes direct calls, one receiver-call shape,
conditionals, reducible loops, borrows/reborrows, and unsafe blocks emitted
through the remaining legacy materialization path; those families migrate in
the scheduled RFC 0048 order (direct/receiver call, control, unsafe/borrow,
composites, and the empty family). Generic function bodies, closures,
constructors, destructors, casts, compound assignment, and error operations
remain fail-closed.

## Verified Guarantees

`HirVerifier` does not trust builder output. It rechecks:

- repository leases and exact checked and borrow-evidence revisions;
- module, source, context, interface, and import lineage;
- complete record cardinalities and definition coverage;
- deterministic node identities and record relationships;
- definition kinds, semantic types, value categories, and source ranges;
- AST shape only through the verified checked-module handoff;
- signature, binding-pattern, literal, constant, place, call, and control
  facts for the admitted families; and
- absence of all fact families outside the admitted profile.

Success produces a move-only, immutable, NodeId-free `VerifiedHirModule`.
Failure produces no partial HIR publication. The current verifier still uses
fixed record-stride expectations for live shapes; RFC 0048 replaces those
with structural graph verification and a fact-completeness post-pass in later
phases.

## Identity, Lineage, And Determinism

Declarations and functions are sorted by source byte offset and then by
canonical definition key. The recursive builder allocates node ids in source
preorder per arm with the exact id stride of the materializer it replaces, so
equivalent semantic contexts produce the same HIR ordering and the corpus
IR-parity channel stays byte-identical across the migration.

`VerifiedHirModule` retains:

- semantic context fingerprint;
- package, crate, module, and source identities;
- parsed receipt;
- checked, dispatch, and borrow-evidence revisions;
- own and imported interface revisions; and
- live checked-fact and borrow-evidence leases.

HIR has no independent canonical revision or reversible codec; its
deterministic non-reversible textual dump is the parity surface.

## Inspection And Native Verification

`VerifiedHirModule::dump()` returns a deterministic diagnostic rendering that
starts with `zom.hir`. It includes lineage digests, imported interfaces,
declarations, bindings, parameters, functions, blocks, returns, literals,
references, binaries, aggregates, and writes. It first confirms that retained
evidence is still resolvable.

The dump intentionally omits some in-memory fields and is neither a complete
serialization nor a reconstruction format. The CLI exposes it through
`--emit=hir`.

Native tests cover lineage, all migrated families (record content, node
strides, initialization kinds, and downstream MIR assertions), deterministic
ordering and dumps, session paths, and rejection before partial HIR/MIR
publication. Both corpus parity channels (923 process cases and 64 clean HIR/MIR
dumps) gate every family migration. Fail-closed instrumentation probes
confirmed each new arm intercepts its shapes before the legacy block for that
shape was deleted.

## Known Gaps

- RFC 0048 Phase 2 is incomplete: direct/receiver calls, control flow,
  unsafe/borrow, field-write/loop-body composites, and the empty family
  (casts, compound assignment, captures) still use the legacy path.
- Generic function bodies, effects, closures with captures, constructors,
  destructors, and extern bodies are not lowered.
- The verifier still relies on fixed strides and cardinality equations rather
  than structural graph verification; general candidate mutation testing is
  part of later RFC 0048 phases.
- HIR has no complete canonical codec or reversible text format.
- Imported-interface lineage lacks a dedicated positive multi-module HIR test.
