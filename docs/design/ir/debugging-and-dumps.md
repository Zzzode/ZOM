# IR Debugging And Dumps

Updated: 2026-07-24

## Authority And Status

| Field | Value |
|---|---|
| Authority | Non-normative contributor workflow |
| Coverage | Current HIR inspection, Built MIR record access, tests, and architecture gate |
| HIR implementation | [`hir-module.cc`](../../../products/zomlang/compiler/hir/hir-module.cc) |
| Built MIR implementation | [`built-mir.cc`](../../../products/zomlang/compiler/mir/built-mir.cc) |
| CLI implementation | [`zomc.cc`](../../../products/zomlang/utils/zomc/zomc.cc) |

The compiler currently has a deterministic HIR diagnostic dump. It does not
have a MIR text dump, IR pass-dump framework, or CLI HIR/MIR/LIR emission mode.

## HIR Dump

`VerifiedHirModule::dump()` returns `zc::Maybe<zc::String>`. It first verifies
that the retained checked-fact and borrow-evidence leases are still resolvable.
If that evidence cannot be recovered, no dump is returned.

A successful dump starts with:

```text
zom.hir.v0
```

The rendering includes:

- module and semantic-context identity;
- source, parsed, checked, dispatch, borrow, and interface lineage digests;
- imported interface revisions;
- value declarations and binding patterns;
- functions, blocks, and return statements; and
- canonical scalar literal payloads.

The rendering is deterministic for equivalent verified modules. It omits
several in-memory fields and is not reversible. Do not use it as persistence,
cache input, ABI, snapshot compatibility promise, or a substitute for the HIR
verifier.

## Built MIR Inspection

`VerifiedBuiltMir` exposes:

- immutable function records;
- immutable canonical function byte records; and
- the computed `MirRevisionId`.

The byte records exist so the verifier can independently reproduce the revision
contract. They are not a human-readable dump or stable external codec.

There is no `VerifiedBuiltMir::dump()`, `--emit=mir`, `.zmir` artifact, or
before/after pass-dump mechanism. The absence of those surfaces must remain
visible in contributor documentation and test expectations.

## Session Inspection

The compiler session exposes verified HIR and Built MIR collections to native
integration tests after successful checking. These accessors are useful for
asserting exact identities, lineages, statements, terminators, records, and
revisions.

They are not a user-facing command-line inspection contract. The current CLI
can emit AST. Its dispatch and binary selections terminate at explicit
unavailable-capability boundaries, and it has no HIR or MIR selection.

## Native Debugging Workflow

Run the narrow architecture checks first:

```bash
python3 scripts/check-ir-architecture.py --check
python3 scripts/check-ir-architecture.py --self-test
```

Then use the repository-native build and unit suite:

```bash
cmake --preset sanitizer
cmake --build --preset sanitizer
ctest --preset default -L unittest --output-on-failure
```

The most relevant native tests are:

| Test source | What it establishes |
|---|---|
| [`hir-module-test.cc`](../../../products/zomlang/tests/unittests/compiler/hir/hir-module-test.cc) | HIR lineage, scalar records, constants, deterministic identity, and dump behavior |
| [`built-mir-test.cc`](../../../products/zomlang/tests/unittests/compiler/mir/built-mir-test.cc) | Built MIR empty and non-empty codec oracles |
| [`compiler-session-package-test.cc`](../../../products/zomlang/tests/unittests/compiler/driver/compiler-session-package-test.cc) | End-to-end scalar shapes, selected corruption rejection, and no partial publication |

Use LLDB against the sanitizer or debug compiler executable when an invariant
failure needs control-flow inspection. Inspect the candidate immediately before
the verifier call and the closed failure branch immediately after it. Do not
patch a verified wrapper or bypass the verifier for debugging.

## Interpreting Evidence

| Observation | Valid conclusion |
|---|---|
| HIR dump contains a node | That node was published by the live HIR verifier |
| MIR canonical record matches its oracle | The tested canonical framing and digest are stable |
| Architecture gate passes | Named source-structure and wiring constraints are present |
| An enum alternative exists | The representation can name it |
| An RFC specifies a dump or CLI mode | The design contains that contract |

The last two observations do not prove production reachability. Confirm a live
builder, verifier, session path, and native test before describing an operation
or inspection mode as implemented.

## Known Gaps

- No human-readable Built MIR dump exists.
- No CLI HIR or MIR emission mode exists.
- No before/after lowering or pass-pipeline dump mechanism exists.
- No LIR or LLVM inspection surface exists because those stages are absent.
- HIR text is incomplete and non-reversible.
- Built MIR corruption testing covers selected live shapes, not a general CFG
  verifier.
