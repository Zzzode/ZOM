# Diagnostics Architecture - Current Implementation

Updated: 2026-09-06

This document describes the diagnostics architecture exercised by the current
compiler. RFC 0047 owns the design decision and its acceptance evidence.

## 1. Failure Classification

The compiler keeps three failure classes separate:

| Class | Meaning | Public representation |
|---|---|---|
| Diagnostic | A source author, package author, build operator, or tool user can correct admitted input or a supported configuration | A catalog `ZOMxxxx` code and an immutable canonical fact |
| Compiler incident | A compiler invariant, verifier, identity, codec, or internal pipeline contract failed | A registered `CompilerIncidentDescriptor` aggregated in `BoundedIncidentSet` |
| Operational failure | Allocation, I/O, external-process, or unavailable-resource failure prevented the requested operation | A closed operation-specific failure value rendered without a `ZOMxxxx` code |

An internal incident is not a diagnostic. It contains registered domain, phase,
kind, producer, and occurrence-count fields, but no source text, user
identifier, host path, address, thread identifier, or raw external error. An
operational failure is not promoted to an incident merely because it prevented
compilation.

## 2. Catalog

The ordered X-macro files under `compiler/diagnostics/defs/` are the sole source
of public diagnostic metadata. Each `DIAG` entry defines the numeric code,
symbolic name, default severity, English template, and argument arity. Their
expansion defines `DiagID`, catalog lookup, typed construction constraints, and
compile-time validation. There is no parallel YAML registry or checked-in
generated catalog.

The active allocation is:

| Range | Purpose |
|---|---|
| `ZOM2000-ZOM2999` | Lexing, parsing, grammar, and source shape |
| `ZOM3000-ZOM3999` | Binding, names, modules, imports, exports, and visibility |
| `ZOM4000-ZOM4999` | Types, semantics, coherence, borrow, and ownership |
| `ZOM6000-ZOM6999` | User-actionable target, runtime-capability, lowering, and backend conditions |
| `ZOM7000-ZOM7999` | User-actionable package, manifest, dependency, build-script, and materialization conditions |
| `ZOM9900-ZOM9999` | Unassigned |

Compilation rejects duplicate codes or names, malformed placeholder sequences,
arity mismatches, invalid severity, codes outside their owner partition, and
any entry in `ZOM9900-ZOM9999`. Runtime decoding admits only a live identifier
with its exact declared argument count. Unknown identifiers fail closed.

## 3. Canonical Facts

`DiagnosticFact` is the query-safe semantic value. It owns:

- one `DiagnosticOccurrenceKey`;
- one catalog `DiagID`;
- a bounded ordered argument sequence;
- one `DiagnosticProvenanceKey` for the primary location; and
- zero or more ordered `DiagnosticSecondary` records.

A secondary record has the closed role `Highlight`, `Note`, or
`PreviousDeclaration`. Notes carry their own catalog code and arguments. Facts
contain neither `SourceLoc` nor a `SourceManager`, borrowed strings, rendered
sentences, query leases, output streams, terminal escapes, or IDE protocol
objects.

Occurrence identity and display equality are intentionally different. Distinct
occurrence keys remain distinct even when their code, resolved range, and
arguments match. Byte-equal payloads for one occurrence collapse; conflicting
payloads for one occurrence reject the request.

Source and document locations are represented by canonical provenance keys. A
source key identifies an immutable source snapshot. A document key identifies
an admitted non-source input such as a manifest. Revision-local provenance maps
resolve those keys only after the complete request fact set is known.

## 4. Producer Boundaries

Analysis libraries return typed issues or immutable fact roots and never write
to presentation consumers. The production boundaries are:

- lexer and parser publish through `SourceDiagnosticSink` and
  `SourceDiagnosticDraftBuffer`, preserving parser checkpoint and recovery
  semantics;
- Binder, module graph, Checker, borrow-interface, ownership, and IR capability
  issues use exhaustive owner-local source diagnostic projectors;
- package manifest and toolchain-root issues project to document-backed facts;
- invariant alternatives project to registered compiler incidents; and
- source-less IR output and external-resource failures remain typed operational
  failures.

Projectors select diagnostic identifiers but do not render messages. Query
providers can therefore return and reuse the same semantic result on cold and
warm execution without relying on an emission side effect.

## 5. Collection And Atomic Materialization

One `CompilationDiagnosticCollector` belongs to one compilation request. It
registers exact source and document authorities, atomically admits complete
fact roots and their provenance, rejects missing or conflicting authority, and
seals one `CompilationDiagnosticFacts` value.

Sealing validates occurrence conflicts and orders the retained facts
canonically. Producer completion order and insertion order do not participate.
The sealed value retains the complete authoritative fact set even when a later
display policy omits entries.

`materializeDiagnosticFacts` resolves every primary and related provenance key
against the sealed source and document authorities. It returns one immutable
`ResolvedDiagnosticBatch` only after all records succeed. Missing, stale,
foreign, role-mismatched, conflicting, or out-of-range provenance rejects the
whole operation and is projected to the diagnostics incident rail. No normal
consumer can observe a partially materialized batch.

## 6. Policy And Consumers

`DiagnosticPolicy` is immutable presentation input. The current default admits
at most 100 error-or-fatal primary diagnostics for display while retaining the
complete resolved batch and all related records for each admitted primary. A
display limit does not change compilation success or semantic identity.

`renderTerminalDiagnostics` accepts only a `DiagnosticPolicyResult` and a
`DiagnosticPresentationResolver`. It resolves every source or document view
before its single output write, so missing presentation authority produces zero
normal diagnostic output. Formatting, escaping, source excerpts, and color are
terminal concerns rather than semantic fact fields.

The IDE facade projects the same resolved batch to `SnapshotDiagnostic`. Each
snapshot diagnostic owns its catalog code, severity, arguments, a
resource-qualified primary location, and complete resource-qualified related
records. It exposes canonical source or document identity bytes rather than
compiler provenance objects, `SourceManager`, or query/session handles. Snapshot
version and cancellation rules decide whether that immutable set can publish.

For the same recovery-free snapshot and policy, terminal and IDE views derive
from the same code, severity, multiplicity, primary location, arguments, and
related information.

## 7. Session Publication

`CompilerSession` owns request collection, admitted document presentation
authority, and the sealed policy result. `finalizeDiagnostics()` seals,
materializes, and applies policy exactly once. `getDiagnostics()` exposes only
the immutable result after successful finalization. `hasDiagnosticErrors()`
reports semantic error state before or after sealing without making a consumer
the source of truth.

The CLI renders the sealed result, prints sanitized incident or operational
records on their respective rails, and never invents a public diagnostic code
for an internal or environmental failure.

## 8. Verification Boundary

The architecture is verified through the implementation itself:

- C++ catalog instantiation and typed factories enforce valid construction;
- target dependencies keep producers independent of terminal and IDE consumers;
- native ztests cover catalog validation, codecs, collection conflicts,
  deterministic ordering, atomic materialization, display policy, terminal
  resolution, projectors, incidents, and IDE related-information projection;
- query and session tests cover retained fact publication; and
- sanitizer builds plus real CLI and IDE workflows cover the public paths.

Repository text searches can help a review locate stale names, but they are not
architecture evidence and are not RFC 0047 completion gates.
