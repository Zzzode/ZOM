# Built MIR

Updated: 2026-07-24

## Authority And Status

| Field | Value |
|---|---|
| Authority | Non-normative compiler implementation guide |
| Coverage | Partial production Built MIR |
| Governing decisions | [RFC 0010](../../rfc/0010-intermediate-representation-pipeline.md), [RFC 0013](../../rfc/0013-ownership-analysis-integration-boundary.md) |
| Production implementation | [`built-mir.h`](../../../compiler/mir/built-mir.h), [`built-mir.cc`](../../../compiler/mir/built-mir.cc) |
| Native verification | [`built-mir-test.cc`](../../../tests/unittests/compiler/mir/built-mir-test.cc), [`compiler-session-package-test.cc`](../../../tests/unittests/compiler/driver/compiler-session-package-test.cc) |

Built MIR is a live, revision-bound, independently verified artifact. Its
current producer and verifier support only scalar module initialization and
scalar-literal returns. General CFG lowering, ownership analysis, cleanup, and
executable MIR are not current capabilities.

## Role In The Pipeline

`BuiltMirBuilder` consumes `VerifiedHirModule`, including its retained verified
borrow-evidence lineage. It creates a mutable `BuiltMirCandidate`.
`BuiltMirVerifier` independently checks the candidate and is the sole creator
of `VerifiedBuiltMir`.

The current session retains verified Built MIR for inspection and future
successor stages. There is no production ownership analyzer, executable-MIR
consumer, target lowering, or backend consumer.

## Representation

The data model can represent:

| Category | Current schema |
|---|---|
| Body-local identity | One-based `MirLocalId`, `MirSourceScopeId`, and `MirBlockId` |
| Place | Local plus field, index, dereference, downcast, or subslice projections |
| Operand | `Copy`, `Move`, or `Constant` |
| Rvalue | `Use` |
| Initialization | `Initialize` or `Overwrite` |
| Statement | `Assign`, `StorageLive`, `StorageDead`, `BorrowCreation`, `SetDiscriminant`, or `Deinitialize` |
| Terminator | `Return`, `Unreachable`, `Call`, `Goto`, or `SwitchInt` |
| Local kind | `ModuleInitializerResult` or `Temporary` |
| Function kind | `ModuleInitializer` or `Function` |

A `MirPlace` carries its local and projection sequence. It does not carry an
explicit result `SemanticTypeId` for every projection. Schema capacity must not
be read as proof that a producer, type checker, dataflow engine, or verifier
supports every listed alternative.

## Production Profile

### Scalar module initializer

Each admitted module scalar declaration becomes one MIR function containing:

- kind `ModuleInitializer`;
- one source scope;
- one `ModuleInitializerResult` local;
- one basic block;
- `StorageLive(local)`;
- `Assign(local, Use(Constant), Initialize)`; and
- `Return(Move(local))`.

### Scalar-return function

Each admitted HIR function becomes one MIR function containing:

- kind `Function`;
- one source scope;
- no MIR locals;
- one basic block;
- no statements; and
- `Return(Constant)`.

Functions are sorted by complete canonical owner key before their records and
module revision are computed.

## Representable But Not Emitted

The production builder does not emit:

- place projections;
- `Copy` operands;
- temporary locals;
- overwrite assignment;
- `StorageDead`, `BorrowCreation`, `SetDiscriminant`, or `Deinitialize`;
- `Unreachable`;
- `Goto` and `SwitchInt` branch terminators, which the algebra represents but
  no production lowering emits; or
- drops, assertions, panics, unwind edges, or general multi-block control
  flow beyond the admitted call shapes.

## Revision And Canonical Records

Built MIR canonically encodes definitions, semantic types, constants,
places, operands, rvalues, statements, terminators, and functions. Each
verified module retains the exact canonical function records used to compute
its revision.

The `zom.mir-revision` input binds:

- semantic context and module identity;
- checked, dispatch, and borrow-evidence revisions;
- canonically ordered function records.

The resulting SHA-256 revision is recomputed by the verifier. Canonical records
are revision evidence, not a public binary format or a human-readable dump.

## Verified Guarantees

The current verifier proves the exact live profile:

- borrow-evidence lease and revision match;
- function and canonical-record cardinalities match HIR;
- every MIR function uniquely maps to one HIR declaration or function;
- module initializers and scalar-return functions have the exact shapes above;
- owners are in strict canonical order;
- every canonical function record equals an independent re-encoding;
- the module revision equals an independent recomputation; and
- evidence remains resolvable when the immutable capability is published.

This is not a general CFG, projection, type, dominance, ownership, or
initialization verifier. The exact scalar shapes make the current broader
schema alternatives unreachable.

## Ownership Boundary

`VerifiedBuiltMir` binds verified borrow-evidence lineage. Borrow evidence
describes the admitted frontend borrow surface; it is not ownership-event
dataflow or a proof that MIR obeys move, loan, region, reborrow, drop, linear,
unsafe, or concurrency rules.

No `VerifiedOwnershipFacts`, ownership-checked MIR, drop-elaborated MIR,
coroutine-elaborated MIR, or executable MIR capability is produced. A
`BorrowCreation` statement alternative does not change that boundary.

## Inspection And Native Verification

`VerifiedBuiltMir` exposes its functions and canonical function records. It has
no human-readable `dump()` API and the CLI has no MIR emission mode.

Native tests cover the canonical empty and non-empty codec oracles, exact scalar
initializer and scalar-return shapes, selected corruption rejection, evidence
lineage, and atomic session publication. The IR architecture gate checks direct
HIR-to-Built-MIR wiring, the single canonical domain, target independence, and
selected forbidden alternate rails.

## Known Gaps

- General CFG construction and verification are absent.
- Calls, drops, cleanup edges, error control flow, assertions, panics, and
  coroutine control flow are absent.
- Place/projection typing and validation are not general.
- Production ownership analysis and ownership-result publication are absent.
- Drop, coroutine, and executable MIR artifacts are not produced.
- There is no MIR dump, pass pipeline, target lowering, or backend consumer.
- MIR-specific unit coverage is currently concentrated on codec oracles; most
  producer and corruption coverage is in session integration tests.
