# IR Debugging And Dumps

Updated: 2026-09-14

## Authority And Status

| Field | Value |
|---|---|
| Authority | Non-normative contributor workflow |
| Coverage | Current HIR and MIR canonical dump surfaces, record access, tests, and architecture gate |
| HIR implementation | [`compiler/hir/`](../../../compiler/hir/) |
| Built MIR implementation | [`built-mir.cc`](../../../compiler/mir/built-mir.cc) |
| CLI implementation | [`zomc.cc`](../../../utils/zomc/zomc.cc) |

The compiler has a deterministic HIR diagnostic dump and a deterministic
canonical-record MIR emission mode used by the RFC 0048 corpus parity
channel. It does not have a human-readable MIR text dump, an IR pass-dump
framework, or CLI LIR/LLVM emission modes.

## HIR Dump

`VerifiedHirModule::dump()` returns `zc::Maybe<zc::String>`. It first verifies
that the retained checked-fact and borrow-evidence leases are still resolvable.
If that evidence cannot be recovered, no dump is returned.

A successful dump starts with:

```text
zom.hir
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
contract. They are not a stable external codec.

The CLI `--emit=mir` selection renders each canonical function record as
framed hex text together with the revision digest, deterministically and
independent of build path; `--emit=hir` renders the HIR dump. These modes back
the process and IR channels of `scripts/check-ir-parity.py`. There is no
human-readable `VerifiedBuiltMir::dump()`, no `.zmir` artifact, and no
before/after pass-dump mechanism. The absence of those surfaces must remain
visible in contributor documentation and test expectations.

## Session Inspection

The compiler session exposes verified HIR, Built MIR, ownership-checked MIR,
validated ownership proofs, and executable MIR collections to native
integration tests after successful checking (see
[ownership-and-executable-mir.md](ownership-and-executable-mir.md)). These
accessors are useful for asserting exact identities, lineages, statements,
terminators, records, and revisions.

They are not a user-facing command-line inspection contract. The CLI exposes
AST, HIR, and MIR dump selections; the binary and run paths additionally
produce objects and linked executables internally, but there is no explicit
`--emit=lir`, `--emit=llvm-ir`, or `--emit=obj` selection yet (see
[llvm-backend-and-object-emission.md](llvm-backend-and-object-emission.md)).

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
| [`hir-module-test.cc`](../../../tests/unittests/compiler/hir/hir-module-test.cc) | HIR lineage, emitted records for the admitted families, deterministic identity, and dump behavior |
| [`built-mir-test.cc`](../../../tests/unittests/compiler/mir/built-mir-test.cc) | Built MIR empty and non-empty codec oracles and emitted scalar/call/control shapes |
| [`compiler-session-package-test.cc`](../../../tests/unittests/compiler/driver/compiler-session-package-test.cc) | End-to-end admitted shapes, selected corruption rejection, and no partial publication |
| [`tests/unittests/compiler/ownership/`](../../../tests/unittests/compiler/ownership/) | Fact, overlay, lineage-mutation, drop, and proof-validation behavior |
| [`tests/unittests/compiler/lir/`](../../../tests/unittests/compiler/lir/) | Lowered LIR shapes, scalar stores, and the algebra codec oracle |
| Corpus parity baselines | Process channel over 923 sources and IR-channel HIR/MIR dump hashes over the 64 clean sources |

Use the native debugger for the host platform against the sanitizer or debug
compiler executable when an invariant failure needs control-flow inspection:

```bash
# macOS and other LLDB hosts
lldb build-sanitizer/bin/zomc
(lldb) command script import tools/lldb/zomlang_lldb.py

# Linux and other GDB hosts
gdb build-sanitizer/bin/zomc
(gdb) source tools/gdb/zomlang_gdb.py
```

Installed toolchains place the same helpers under
`<prefix>/share/zom/debuggers/lldb/zomlang_lldb.py` and
`<prefix>/share/zom/debuggers/gdb/zomlang_gdb.py`.

Both helpers summarize `ast::Node` and `zc::Own<ast::Node>`. Their `zomkind`
and `zominfo` commands inspect either an explicit expression or the local
variable named `expression`. They inspect compiler memory only; they do not
add a language-level debugging statement or alter verifier behavior.

Inspect the candidate immediately before the verifier call and the closed
failure branch immediately after it. Do not patch a verified wrapper or bypass
the verifier for debugging.

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
- No before/after lowering or pass-pipeline dump mechanism exists.
- No explicit LIR or LLVM IR CLI inspection surface exists;
  `LlvmTranslationResult` retains textual IR and object bytes in-process for
  tests but exposes no user selection.
- HIR text is incomplete and non-reversible.
- Built MIR corruption testing covers selected live shapes, not a general CFG
  verifier (pending RFC 0048 structural verification phases).
