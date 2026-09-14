# Built MIR

Updated: 2026-09-14

## Authority And Status

| Field | Value |
|---|---|
| Authority | Non-normative compiler implementation guide |
| Coverage | Partial production Built MIR |
| Governing decisions | [RFC 0010](../../rfc/0010-intermediate-representation-pipeline.md), [RFC 0013](../../rfc/0013-ownership-analysis-integration-boundary.md) |
| Production implementation | [`built-mir.h`](../../../compiler/mir/built-mir.h), [`built-mir.cc`](../../../compiler/mir/built-mir.cc) |
| Native verification | [`built-mir-test.cc`](../../../tests/unittests/compiler/mir/built-mir-test.cc), [`compiler-session-package-test.cc`](../../../tests/unittests/compiler/driver/compiler-session-package-test.cc) |

Built MIR is a live, revision-bound, independently verified artifact. Its
producer covers the admitted constructor inventory: scalar module
initialization, scalar and parameter/local returns, nominal aggregates and
field projections, primitive binary and comparison rvalues, mutable local
writes, same-module direct and one admitted receiver call, four-block
conditional diamonds, reducible while loops, and bounded borrow/reborrow and
unsafe-block shapes. The construct-level inventory is
[lowerable-constructs.md](lowerable-constructs.md). General CFG construction
and general ownership completeness remain open; see
[ownership-and-executable-mir.md](ownership-and-executable-mir.md).

## Role In The Pipeline

`BuiltMirBuilder` consumes `VerifiedHirModule`, including its retained verified
borrow-evidence lineage. It creates a mutable `BuiltMirCandidate`.
`BuiltMirVerifier` independently checks the candidate and is the sole creator
of `VerifiedBuiltMir`.

The session retains verified Built MIR and feeds it into the production
ownership rail (event overlay, validated proofs, ownership-checked MIR, and
`VerifiedExecutableMir`). The admitted backend slice reaches Built MIR through
the ownership wrapper; that shortcut is recorded in
[llvm-backend-and-object-emission.md](llvm-backend-and-object-emission.md).

## Representation

The data model can represent:

| Category | Current schema |
|---|---|
| Body-local identity | One-based `MirLocalId`, `MirSourceScopeId`, and `MirBlockId` |
| Place | Local plus field, index, dereference, downcast, or subslice projections |
| Operand | `Copy`, `Move`, or `Constant` |
| Rvalue | `Use`, `NominalAggregate`, `Comparison`, or `Arithmetic` |
| Initialization | `Initialize` or `Overwrite` |
| Statement | `Assign`, `StorageLive`, `StorageDead`, `BorrowCreation`, `SetDiscriminant`, `Deinitialize`, or `UnsafeScopeBoundary` |
| Terminator | `Return`, `Unreachable`, `Call`, `Goto`, or `SwitchInt` |
| Local kind | `ModuleInitializerResult`, `Temporary`, `FunctionResult`, `UserLocal`, or `Parameter` |
| Function kind | `ModuleInitializer` or `Function` |

A `MirPlace` carries its local and projection sequence. It does not carry an
explicit result `SemanticTypeId` for every projection. Schema capacity must not
be read as proof that a producer, type checker, dataflow engine, or verifier
supports every listed alternative.

## Production Profile

The builder emits the admitted shapes inventoried in
[lowerable-constructs.md](lowerable-constructs.md): single-block scalar
initializers and returns, sequential scalar locals, nested arithmetic through
synthesized temporaries, nominal aggregate initialization with field
projection, mutable local writes, two-block same-module calls with
continuation blocks, four-block conditional diamonds driven by `SwitchInt` on
a comparison rvalue, reducible loops with `Goto`, and bounded borrow scopes
with `StorageLive`/`StorageDead`.

Functions are sorted by complete canonical owner key before their records and
module revision are computed. The recursive destination-driven HIR builder
(RFC 0048) changes which source shapes reach this profile but not the emitted
MIR bytes; the corresponding recursive MIR builder and structural MIR
verifier remain future RFC 0048 phases, so the current verifier still dispatches
per emitted shape.

## Representable But Not Emitted

The production builder still does not emit general projections beyond the
admitted aggregate field set, unwind edges, drop/panic terminators, suspend or
resume, general multi-function call coverage, or irreducible control flow.
Schema alternatives without a producer remain unreachable and are not
production behavior.

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
- emitted blocks, statements, rvalues, and terminators match the admitted
  shape inventory including conditional diamonds, reducible loops, and
  same-module call continuations;
- owners are in strict canonical order;
- every canonical function record equals an independent re-encoding;
- the module revision equals an independent recomputation; and
- evidence remains resolvable when the immutable capability is published.

This is not yet a general structural CFG, projection-type, dominance, or
ownership verifier. RFC 0048 replaces the per-shape validators and global
count equations with structural graph verification; until that phase lands the
per-shape verifier dispatch remains the production mechanism, and its admitted
shapes are exactly the lowering inventory.

## Ownership Boundary

`VerifiedBuiltMir` binds verified borrow-evidence lineage. Borrow evidence
describes the admitted frontend borrow surface; by itself it is not
ownership-event dataflow or a proof that MIR obeys move, loan, region,
reborrow, drop, linear, unsafe, or concurrency rules.

That proof is now produced by the successor rail: the event overlay,
independent fact builders, `OwnershipProofValidation`, ownership-checked MIR,
drop/coroutine elaboration, and `VerifiedExecutableMir`, all described in
[ownership-and-executable-mir.md](ownership-and-executable-mir.md). The
boundary remains partial: a `BorrowCreation` statement or a fact builder
existing on disk is not evidence that the general ownership model is complete.

## Inspection And Native Verification

`VerifiedBuiltMir` exposes its functions and canonical function records. The
CLI provides `--emit=mir`, rendering each canonical function record as framed
hex plus the revision digest deterministically for the RFC 0048 parity
channel; there is still no human-readable `dump()` API or pass-dump framework.

Native tests cover the canonical empty and non-empty codec oracles, the
emitted scalar/call/conditional/loop/aggregate shapes, selected corruption
rejection, evidence lineage, and atomic session publication. The IR
architecture gate checks direct HIR-to-Built-MIR wiring, the single canonical
domain, target independence, and selected forbidden alternate rails.

## Known Gaps

- General structural CFG construction and verification are pending the RFC
  0048 recursive MIR builder and structural verifier phases.
- General drop, unwind, panic, and coroutine terminators and complete
  ownership typestate are open (see the ownership note).
- Place/projection typing is not general.
- There is no pass pipeline or human-readable MIR dump; `--emit=mir` is the
  canonical hex surface.
- Target lowering through a verified LIR capability is absent; the current
  backend slice is documented separately.
- MIR-specific unit coverage remains concentrated on codec oracles and the
  admitted shapes; most corruption coverage is in session integration tests.
