# RFC 0021 Review And Implementation Tracker

## Discussion Record

### 2026-07-23 Initial Draft

RFC 0021 was created to close the target LIR and LLVM translation decisions
left intentionally broad by RFC 0010. The draft uses LLVM IR, MLIR, Swift SIL,
Rust ABI/codegen, and Cranelift IR as primary prior art.

The initial design selects:

- one target-aware LIR with private construction typestates;
- SSA block parameters and mechanical LLVM PHI construction;
- separate carrier type, storage layout, and function ABI stores;
- zero-to-many physical ABI carriers;
- opaque pointers with conservative verified provenance;
- no LIR `undef`, poison, first-class void, or unproved LLVM attributes;
- closed memory, atomic, unwind, runtime, symbol, and object contracts;
- an exact canonical LIR revision; and
- total LLVM translation followed by mandatory LLVM verification.

Formal review is blocked until RFC 0016 is accepted and RFC 0021 binds the
accepted target-authority and LLVM-baseline snapshot.

### 2026-07-23 Adversarial Closure Pass

Independent repository, prior-art, and IR-design reviews found that the initial
draft was not implementation-closed. The draft was rewritten to add the
crate-wide executable-MIR set, final package-session entry authority, complete
target/runtime/translator revisions, non-circular backend-attribute proofs,
structural block-origin ordering, target ABI coercion shapes, mechanical
multi-carrier LLVM packing, an Itanium-only initial EH algebra, allocation-site
provenance, exact atomic payloads and ordering matrix, closed failure rows,
verified object publication and the canonical 546-byte revision oracle.

Product linking and executable publication were removed from RFC 0021 because
they require their own link-plan, runtime-closure, and artifact-manifest
contract. No implementation work is authorized before the RFC reaches
`ACCEPTED` and then records an `IMPLEMENTING` pointer.

### 2026-07-23 Authority-Cycle Repair

The RFC 0016 completion review found three dependency cycles. The repaired
design now imports only RFC 0016's target-authority bundle, selected
code-generation capability set, target-independent runtime ABI contract, and
private one-shot final code-generation authority. RFC 0021 exclusively owns
the concrete `lowerToLir` method, LIR algebra, translator contract, physical
runtime ABI manifest, and monomorphization sequencing.

`lowerToLir` accepts a verified semantic-root request, consumes the complete
final wrapper, privately obtains the final code-generation operation state,
constructs the complete semantic monomorphization plan, builds the ABI store,
and only then derives compiler-generated adapters and the physical runtime ABI
manifest. Imported runtime symbols are not semantic roots. Later translation
and object-emission operations consume authority-carrying verified typestates
instead of reusing the destroyed final wrapper.

The closure pass also adds a crate-wide feature-boundary collection and a
complete, revisioned ABI-classifier registry owned by RFC 0021. RFC 0016 owns
the selected classifier ID and target capability boundary; RFC 0021 owns the
decision program because it defines `FnAbi`.

## Bound Proposal Snapshots

| RFC | File SHA-256 | State |
|---|---|---|
| RFC 0006 | `c37e40c9f903901a4f8f738e8d5d8fed842d55473ec9420eda901a46aede1613` | Accepted design in implementation |
| RFC 0007 | `2766b4ce7ddbb0cc08ea550d0c618228daf7a91e8b951aef03d4c9f1aced6dbb` | Accepted design; production ownership analysis remains gated |
| RFC 0008 | `581bf790d7048368e13d52b80031c46457a6bdb57f4b0be2318df706bf1e575b` | Accepted design in implementation |
| RFC 0009 | `4acb7a7308ab271cd573c43761521f885f1bbafcdff0acefba10c027b4230253` | Accepted design in implementation |
| RFC 0010 | `6deb4954d6a6d2dc8904b366a338c1bdad452d3ab55d1392444d106653a78921` | Accepted design in implementation |
| RFC 0011 | `4c82c7bed3533d05c9b4682ed9b1ad967e7de741a6df6efd541f75a67ec05679` | Landed identity authority |
| RFC 0012 | `7f77fe66cb6a84b0279255081073755bb7d0e61ff2de908c251eb6c1182f8cce` | Accepted design in implementation |
| RFC 0013 | `9dc846eecb0212402cc3f015e52c5aa81782b8f8bd2012c29b896dbabfb6b315` | Accepted overlay in implementation |
| RFC 0016 | `fe1f2937b9426c0b0fe4729af50dc39930355d7fe7836de8b43c7501a3f4f59c` | Current REVIEW authority snapshot; must be replaced by accepted hash |

## Owner Review Checklist

| Owner | Review State | Blocking Surface |
|---|---|---|
| `task-router` | Pending | Complete routing, path ownership, and final gate set |
| `rfc` | Pending | Dependency readiness, prior art, structure, snapshots, and transition |
| `binder-checker` | Pending | Semantic type, signature, dispatch, and attribute-proof handoff |
| `module-system` | Pending | Context, crate, instance, session, and deterministic identity |
| `error-system` | Pending | Error, panic, capability, invariant, and CLI diagnostics |
| `concurrency` | Pending | Coroutine, task boundary, atomics, cancellation, and unwind interaction |
| `ir-backend` | Pending | LIR model, legalization, verifier, LLVM translation, and artifact pipeline |
| `runtime-memory` | Pending | Layout, provenance, runtime ABI, FFI, panic, and memory safety |
| `spec-audit` | Pending | Specification and live architecture consistency |
| `verification` | Pending | Mutation, determinism, ABI, LLVM, object, and CI evidence |

## Decision Record

Decision: Pending.

The RFC remains `DRAFT`. No implementation is authorized by this tracker.

## Implementation Tracker

| Slice | State | Required evidence |
|---|---|---|
| RFC 0016 accepted dependency binding | Blocked by RFC 0016 review | Exact accepted hash, target-authority bundle, one-shot code-generation authority, LLVM baseline, and owner approval |
| LIR identity, carrier, layout, ABI, and revision foundation | Pending acceptance | Closed stores, independent oracle, exact mutation matrix |
| Monomorphization and generated-function materialization | Pending acceptance | Complete instance graph, canonical generated roles, deterministic order |
| Block-parameter SSA and scalar lowering | Pending executable MIR | CFG, dominance, edge arity, calls, returns, deterministic dumps |
| Memory, provenance, globals, and symbols | Pending foundation | Initialization, alignment, relocations, linkage, attribute-proof negatives |
| Atomics, runtime, FFI, panic, and exception lowering | Pending target/runtime profiles | Capability matrix, model-specific unwind, containment, ABI conformance |
| Independent LIR verifier | Pending complete LIR algebra | Full invariant and mutation coverage, successor suppression |
| LLVM translation and verification | Pending verified LIR | Exhaustive total mapping, deterministic module, LLVM verifier |
| Verified object emission | Pending verified LLVM modules | Sections, symbols, relocations, ABI, personality, and deterministic object inspection |
| Production CLI and documentation cutover | Pending all prior slices | Layer outputs, architecture gates, sanitizer, default CTest, format, RFC checks |

## Verification Evidence

- `python3 scripts/check-rfc.py`: passed for 21 proposal RFCs on 2026-07-23.
- LLVM, MLIR, Swift SIL, Rust compiler, and Cranelift primary references were
  checked against their official project documentation on 2026-07-23.
- `python3 scripts/check-format.py`: passed using the Xcode toolchain
  `clang-format`; the executable was not available on the default `PATH`.
- `git diff --check`: passed on 2026-07-23.
- The independent 546-byte empty LIR preimage recomputed
  `f1789aa2f43d75da9446cea8a9321157deab13b0540a516f91f08eba2c8da0ad`
  with a second SHA-256 invocation.
- The executable-MIR-set and LIR-algebra codec oracles independently
  recomputed their documented 75-byte and 62-byte preimages and SHA-256
  values. The revised monomorphization request and plan codecs require new
  native test vectors before implementation can land.
- Reviews of earlier draft hashes were invalidated by normative edits and do
  not approve the current candidate. Fresh exact-hash review is required after
  RFC 0016 freezes.
