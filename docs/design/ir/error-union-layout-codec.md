# Error-Union Layout Codec

Updated: 2026-09-14

## Authority And Status

| Field | Value |
|---|---|
| Authority | Non-normative compiler implementation guide |
| Coverage | RFC 0006 groundwork: canonical layout records, codec, and revision with oracle tests; **no production producer or consumer** |
| Governing decisions | [RFC 0006](../../rfc/0006-error-lowering-runtime-abi.md) (IMPLEMENTING) |
| Production implementation | [`compiler/ir/layout/error-union-layout.*`](../../../compiler/ir/layout/) |
| Native verification | [`error-union-layout-oracle-test.cc`](../../../tests/unittests/compiler/ir/layout/error-union-layout-oracle-test.cc) |

## Role In The Pipeline

The error-union layout records describe how a future error union tags its
success and residual alternatives at the target-layout level: discriminant
width alternatives (`U8`..`U64`), the success/residual alternative roles,
canonical residual-key ordering, and per-alternative payload size and
alignment. They are the substrate the eventual error-operator and ABI
lowering will consume.

## Representation

`compiler/ir/layout/` contains the layout record types, a canonical codec
framed under the `zom.error-union-layout` domain, and a content revision. The
codec, like the other IR codecs, is canonical byte framing with domain tag
and big-endian integers, designed to make a swapped alternative or changed
payload size detectable by re-encoding.

## Production Profile

There is no production profile. No checker fact produces an error-union
semantic type (the error-union shape fact map is emitted empty in the body
checker, and error operators are spec-blocked pending RFC 0006), so no
checker, HIR, MIR, or LIR stage constructs the layout, and nothing outside
the layout translation units reads it. The files are implemented groundwork
with tests, not an emitted construct.

## Verified Guarantees

The oracle test pins the exact canonical framing for the fixed fixtures; the
revision recomputation rejects re-encoded mismatch in the test. There is no
independent layout verification in a production pipeline because there is no
production pipeline for this artifact.

## Identity, Lineage, And Determinism

The revision is content-addressed over canonical records in the same style
as the other IR revisions; it currently serves only the codec oracle. The
open revision-scope re-review (DRAFT
[RFC 0050](../../rfc/0050-cross-stage-ir-revision-identity-scope.md)) covers
the rule that no in-memory revision should be specified without naming its
consumer; this codec is one of the pieces of evidence in that review.

## Inspection And Native Verification

`error-union-layout-oracle-test.cc` is the only native surface.

## Known Gaps

- Error-union semantic types and error-operator facts (RFC 0006) are the
  prerequisite producer; without them the layout stays groundwork.
- No lowering emits tag+payload memory, no panic/raise ABI consumes it, and
  there is no LIR operation for it.
- No session publication or access path exists.
