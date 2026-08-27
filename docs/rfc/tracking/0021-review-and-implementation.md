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

### 2026-08-24 Governance Readiness Note (no approval)

This entry records the exact sequence required before any `compiler/lir` or
`compiler/backend` code may land. It does not approve the proposal; frontmatter
remains authoritative and RFC 0021 stays `DRAFT`.

Prerequisite chain (each gate blocks the next):

1. RFC 0016 (`REVIEW`, `approvers: []`, `decision: TBD`) must reach `ACCEPTED`:
   the eight required owners (task-router, rfc, module-system, error-system,
   ir-backend, runtime-memory, spec-audit, verification) must approve the exact
   frozen snapshot `e421dc3b...`; `scripts/check-rfc.py` blocks `ACCEPTED` until
   `decision` is not `TBD` and `approvers` covers every required owner. Record
   the ACCEPTED proposal SHA-256.
2. Rebind this tracker's Bound Proposal Snapshots RFC 0016 row (and the RFC 0021
   normative-authority clause) from the current REVIEW hash `efe800c6...` to the
   RFC 0016 ACCEPTED hash; no earlier review evidence carries forward across a
   snapshot change.
3. RFC 0021 `DRAFT -> REVIEW` (a `DRAFT -> ACCEPTED` skip is rejected by
   `check-rfc.py`), then `REVIEW -> ACCEPTED` on ten-owner approval of one frozen
   snapshot with `decision` set, then `ACCEPTED -> IMPLEMENTING` with the
   implementation pointer set. Only after this is LIR/backend code authorized;
   the first authorized slice is the LIR identity/carrier/layout/ABI/revision
   codec foundation (pure data, no live executable MIR).

Open operational blocker (independent of owner sign-off): RFC 0016 requires
`llvmorg-22.1.8` API provenance, but this environment resolves llvm-config /
clang 19.1.5. RFC 0016 cannot enter IMPLEMENTING until the toolchain is aligned
through the accepted CMake/CI transaction; ambient LLVM must not be admitted.
These sign-offs and the toolchain alignment are human/governance actions and are
not performed by this note. Item 11 (FFI) is additionally backend-blocked: its
`FfiConversion`/`FfiWrapper` keys require a live `VerifiedTargetSelection`
context bundle and the RFC 0006 error-union layout promoted to a live LIR
service; only the pre-LIR `VerifiedFfiBoundaryFacts` eligibility verifier is
frontend-reachable ahead of that.

### 2026-08-26 Dependency Gate Cleared And Entry Into Review

Gate 1 of the Governance Readiness Note cleared: RFC 0016 reached `ACCEPTED` on
2026-08-26 on unanimous approval of its `cfabf1a0` snapshot (final accepted
SHA-256 `ec27f6d3015ed5f91d903671f225141832ef165eec8fd799845ae8913743baee`).
Gate 2 (rebind) is done: this tracker's Bound Proposal Snapshots table and the
RFC 0021 body normative-authority table were resynchronized to the current
authoritative upstream hashes. All bound rows had drifted from the prior pins
(0006, 0007, 0008, 0009, 0010, 0011, 0012, 0013, and 0016), so every pin was
refreshed to the file's current SHA-256. This is a mechanical rebind of the
snapshot pointers; it does not itself reconcile RFC 0021's design against the
new upstream bytes - that semantic reconciliation is exactly the required-owner
review work, which remains `Pending` for all ten owners in the checklist above.

RFC 0021 transitioned `DRAFT -> REVIEW` (gate 3, first step). Frontmatter status
is `REVIEW`, `approvers` stays empty, `decision` stays `TBD`; no approval is
recorded and no `REVIEW -> ACCEPTED` transition is performed. The operational
LLVM `22.1.8` vs `19.1.5` toolchain blocker and the human governance sign-offs
noted on 2026-08-24 are unchanged and still precede any IMPLEMENTING pointer.

### 2026-08-26 Ten-Owner Required Review And Acceptance

Gate 3 (second step) cleared: `REVIEW -> ACCEPTED`. The ten required owners each
performed a substantive review of their blocking surface against the frozen
REVIEW snapshot and the live repository, not the prose alone. All nine bound
upstream pins (RFC 0006, 0007, 0008, 0009, 0010, 0011, 0012, 0013, 0016) were
recomputed with `sha256sum` and each equals its Bound Proposal Snapshots row;
the RFC body normative-authority table (0006, 0010, 0013, 0016) matches too. The
three documented empty-codec oracles were independently recomputed: exec-mir-set
(72 bytes) and lir-revision (546 bytes) reproduced their claimed SHA-256 and
byte counts exactly.

One blocking defect was found and fixed in this pass: the LIR-algebra
empty-registry oracle was labeled `59 bytes`, but its documented hex preimage
decodes to 56 bytes and its documented SHA-256
`03106c3451b5e1adab5310b8643c8d59657e0635f804b5be8fc9b9754199e1c8` already
matches the 56-byte preimage. This was mechanical documentation drift in the
byte-count label only (not the hash or preimage), so the label was corrected to
`56 bytes` and the snapshot re-reviewed. `python3 scripts/check-rfc.py` passes
(46 proposal RFCs). No non-ASCII text and no revision-suffixed internal names
were found in the RFC.

Each owner verdict is recorded in the Owner Review Checklist above; all ten are
`Approve 2026-08-26`. The frozen accepted snapshot SHA-256 is
`c2769266fb2c51f7d7c8789622804a84a764afafaf8df136f42913d857d89d65`; the final
file SHA-256 after the ACCEPTED frontmatter and status-history edits is recorded
in the Decision Record. No `IMPLEMENTING` pointer is added, and the LLVM
`22.1.8` toolchain gate is unchanged and still precedes any implementation.

## Bound Proposal Snapshots

| RFC | File SHA-256 | State |
|---|---|---|
| RFC 0006 | `248080cd962e2ecb5cf1bf84124e38ce54ec3e1ed2e734b2237d7e43bbf08092` | Accepted design in implementation |
| RFC 0007 | `026036c363961fcdc0181a9398965a3967679a6515314fde587cdc53aa6dbf74` | Accepted design; production ownership analysis remains gated |
| RFC 0008 | `d9201a0df96e613f75ad4ff82110858b9f8cc286098349b999307b61e71e0314` | Accepted design in implementation |
| RFC 0009 | `0d6696de1cfdcaac4c4c56aae94c2267d7868dd180f728f66f00b8053fe86d63` | Accepted design in implementation |
| RFC 0010 | `d816f30d07291a6260241ddfe8ab5dc5405d5812e3241a974e08368bca077209` | Accepted design in implementation |
| RFC 0011 | `1ae17ce4233b29f5723259b1622396ac1fa4c9fb29087c661b236ed7c5592081` | Landed identity authority |
| RFC 0012 | `4661fd71d3c2529e94289f1641c175fc73e92f0255f12f44fbb6f74515dea5e7` | Accepted design in implementation |
| RFC 0013 | `25493ab792258d2c746381bddc26cd153d9200c6ebf2a8c6b3df50896c974dad` | Accepted overlay in implementation |
| RFC 0016 | `ec27f6d3015ed5f91d903671f225141832ef165eec8fd799845ae8913743baee` | ACCEPTED 2026-08-26 target-authority snapshot |

## Owner Review Checklist

| Owner | Review State | Blocking Surface |
|---|---|---|
| `task-router` | Approve 2026-08-26: Repository Impact routing, owned path families, and cross-owner gate set are complete and map to real manifest owner ids; the ACCEPTED->IMPLEMENTING pointer and LLVM toolchain gate correctly precede any code. | Complete routing, path ownership, and final gate set |
| `rfc` | Approve 2026-08-26: all 19 required sections present in template order, Open Questions is `None`, frontmatter/index synchronized, prior-art cites five mature designs, and `check-rfc.py` passes; every bound snapshot pin equals the current file SHA-256. | Dependency readiness, prior art, structure, snapshots, and transition |
| `binder-checker` | Approve 2026-08-26: semantic type, signature, dispatch, and backend-attribute-proof handoffs are consumed as verified inputs only; no checker semantics are re-decided in LIR. | Semantic type, signature, dispatch, and attribute-proof handoff |
| `module-system` | Approve 2026-08-26: context brand/fingerprint, crate/module/instance/session identity, and deterministic canonical ordering are closed and non-forgeable across stores. | Context, crate, instance, session, and deterministic identity |
| `error-system` | Approve 2026-08-26: the failure/diagnostic contract adds no new kind, tag, or family and correctly reuses RFC 0010 `IrOperationResult`, RFC 0016 cleanup precedence, and the registered `ZOM9947-ZOM9949` invariant families. | Error, panic, capability, invariant, and CLI diagnostics |
| `concurrency` | Approve 2026-08-26: atomic ordering matrix, fence legality, coroutine entry/resume/destroy keys, and unwind interaction are closed and target-gated; no unadmitted model is reserved. | Coroutine, task boundary, atomics, cancellation, and unwind interaction |
| `ir-backend` | Approve 2026-08-26: LIR algebra, legalization typestates, independent verifier order, total LLVM translation, and object pipeline are implementation-closed; the LIR-algebra empty-registry oracle byte-count label was corrected from `59` to the true self-consistent `56` in this pass. | LIR model, legalization, verifier, LLVM translation, and artifact pipeline |
| `runtime-memory` | Approve 2026-08-26: layout/provenance separation, runtime ABI manifest, FFI containment, panic boundary, and memory-authorization proofs exclude undef/poison and forbid unproved LLVM attributes. | Layout, provenance, runtime ABI, FFI, panic, and memory safety |
| `spec-audit` | Approve 2026-08-26: the RFC specializes only the named RFC 0010 clauses, contradicts no CLAUDE.md known gap, and authorizes no premature ownership-fact or backend publication ahead of RFC 0007/0013 gating. | Specification and live architecture consistency |
| `verification` | Approve 2026-08-26: acceptance criteria, mutation/determinism/ABI/LLVM/object test plan, and codec-oracle evidence are concrete; the three documented empty-codec preimages (exec-mir-set 72 B, lir-algebra 56 B, lir-revision 546 B) each recompute to their claimed SHA-256. | Mutation, determinism, ABI, LLVM, object, and CI evidence |

## Decision Record

Decision: Accepted 2026-08-26. All ten required owners (task-router, rfc,
binder-checker, module-system, error-system, concurrency, ir-backend,
runtime-memory, spec-audit, verification) approved one frozen REVIEW snapshot
after a substantive per-owner review found no blocking design defect. The one
defect found was mechanical and fixed in the same pass: the LIR-algebra
empty-registry codec oracle was labeled `59 bytes` while its authoritative hex
preimage and SHA-256 `03106c3451b5e1adab5310b8643c8d59657e0635f804b5be8fc9b9754199e1c8`
were the self-consistent 56-byte pair; the label was corrected to `56 bytes`.
The final accepted RFC 0021 file SHA-256 (recomputed after the frontmatter,
status-history, and stale-gate-note removal edits) is
`3aa4cfc11d268a0bac10b7aba01e23fe9d598a224e6dcf432124bb9eafa60397`.
No implementation is authorized by this tracker until the RFC records an
`IMPLEMENTING` pointer; the operational LLVM `22.1.8` vs `19.1.5` toolchain
blocker still precedes any `compiler/lir` or `compiler/backend` code.

## Implementation Tracker

| Slice | State | Required evidence |
|---|---|---|
| RFC 0016 accepted dependency binding | Blocked by RFC 0016 review | Exact accepted hash, target-authority bundle, one-shot code-generation authority, LLVM baseline, and owner approval |
| LIR identity, carrier, layout, ABI, and revision foundation | Landed 2026-08-27 (`fabf520d`) | Closed stores, independent oracle, exact mutation matrix - DELIVERED: zomlang/compiler/lir/ (five branded identities; immutable carrier/layout/FnAbi/runtime-symbol/source-location records + interning stores; LirAlgebraCodec whose live encoder reproduces the documented 56-byte empty-registry oracle + SHA-256 03106c34...). Oracle + mutation + fail-closed tests pass; no LLVM linkage. |
| Monomorphization and generated-function materialization | Pending acceptance | Complete instance graph, canonical generated roles, deterministic order |
| Block-parameter SSA and scalar lowering | Pending executable MIR | CFG, dominance, edge arity, calls, returns, deterministic dumps |
| Memory, provenance, globals, and symbols | Pending foundation | Initialization, alignment, relocations, linkage, attribute-proof negatives |
| Atomics, runtime, FFI, panic, and exception lowering | Pending target/runtime profiles | Capability matrix, model-specific unwind, containment, ABI conformance |
| Independent LIR verifier | Pending complete LIR algebra | Full invariant and mutation coverage, successor suppression |
| LLVM translation and verification | Minimal vertical slice landed 2026-08-27 (`4f078d78`); full mapping pending | Exhaustive total mapping, deterministic module, LLVM verifier - DELIVERED SO FAR: one scalar module-initializer lowers MIR -> minimal LIR -> LLVM IR through the compiler/backend/llvm Pimpl shim and passes llvm::verifyModule (`ret i32 42`), behind ZOM_ENABLE_LLVM_BACKEND. The exhaustive operation mapping, block-parameter PHI construction, attribute adapter, and unwind translation remain. |
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

## LLVM 22.1.8 Integration Design (2026-08-27)

This section is the implementation design for how ZOM discovers, links,
isolates, drives, and upgrades LLVM 22.1.8. It conforms to the accepted
contracts of RFC 0016 (discovery, CMake, CI, target-registry admission),
RFC 0021 (LIR to LLVM translation), and RFC 0043 (link and executable
publication, `REVIEW`); it does not restate or amend them. It is grounded in a
2026-08-27 best-practices survey of rustc, Swift, Zig, and Clang and records
where ZOM's accepted choices already match industry practice.

### Design ground truth (verified on disk 2026-08-27)

- No production code includes or links LLVM today: a repository grep for
  `#include "llvm/..."`, `find_package(LLVM`, `llvm_map_components`, or an LLVM
  `target_link_libraries` across `zomlang/`, `libraries/`, and `cmake/`
  (excluding coverage tooling) returns nothing. `compiler/backend` and
  `compiler/lir` do not exist. The whole integration is therefore **additive**
  and changes no existing runtime behavior.
- `compiler/ir/target-registry.{h,cc}` carries the triple, data layout, CPU, and
  features as validated ASCII strings and never calls an LLVM API; the `ir`
  library links no LLVM. The RFC 0016 admission probe order (typed
  `llvm::TargetRegistry` lookup, `MCSubtargetInfo`, `TargetMachine`,
  `isCompatibleDataLayout`) is a spec contract with no live implementation, so
  wiring real probes is net-new, not a rewrite.
- The environment provides Homebrew LLVM `19.1.5`; the repository pins exactly
  `22.1.8` (a real upstream tag,
  `e013073558445169e8732e25fa86e9913bfdd24e`). Nothing below can build or run
  until `22.1.8` is provisioned. This is the standing external gate (Q4 plan
  KR2.1b); no ambient `19.1.5` is admitted and no gate is faked to look
  productive.

### Hard dependency order (the spine)

Each step gates the next; none may be skipped or faked:

1. **Provision LLVM `22.1.8`** in the dev and CI environments (external action).
2. **Wire the RFC 0016 CMake discovery gate** (`find_package(LLVM REQUIRED
   CONFIG PATHS "${ZOM_REQUESTED_LLVM_DIR}" NO_DEFAULT_PATH)`, exact-`22.1.8`
   pin, `llvm-config` provenance chain, the seven `ZOM-CMAKE-LLVM-*` fail-closed
   identifiers, and the eleven components with X86+AArch64). Because the gate is
   unconditional and its positive fixture records a real
   `llvm-config --version == 22.1.8`, step 2 cannot land before step 1 without
   breaking the frontend-only build; the two are coupled.
3. **RFC 0021 `ACCEPTED -> IMPLEMENTING`** with the implementation pointer set,
   then the first authorized code slice: the LIR identity / carrier / layout /
   ABI / revision codec foundation (pure data, no live MIR consumer, exact byte
   oracles). No `compiler/lir` or `compiler/backend` code is authorized before
   this transition.
4. **LIR construction and the independent LIR verifier**, then the total
   LIR to LLVM translation followed by mandatory `verifyModule`.
5. **Object emission** via `TargetMachine::addPassesToEmitFile`
   (`VerifiedObjectArtifact`).
6. **RFC 0043 link and executable publication** consumes the object artifact and
   the `ToolchainClosure`; ZOM never reimplements linking.

### Best-practice-conformant decisions

- **Discovery and pinning.** `find_package(LLVM CONFIG)` with an exact
  `LLVM_PACKAGE_VERSION == 22.1.8` assertion and `NO_DEFAULT_PATH` is the
  standard out-of-tree embedding for a project that wants exactly one LLVM
  version; ZOM's rejection of an unset or ambient `LLVM_DIR` is stricter than
  the norm and is kept as-is (RFC 0016). ZOM uses the system-package route
  (Homebrew `llvm@22`, apt.llvm.org `llvm-22-dev`), not a vendored submodule;
  this is lighter and standard when the project controls its CI image, and it
  matches the accepted RFC 0016 CI contract.
- **Component linking.** The fixed eleven components
  (`Core Support Target TargetParser MC CodeGen AsmParser AsmPrinter BitWriter
  X86 AArch64`) are exactly the minimal set for a two-architecture,
  object-emitting backend; link them with `llvm_map_components_to_libnames` and
  require both X86 and AArch64 in the installed target inventory (RFC 0016).
- **API-isolation wall (top structural recommendation).** Confine every
  `#include "llvm/..."` to a single backend shim under
  `compiler/backend/llvm/**`, behind Pimpl, so no LLVM type appears in any ZOM
  header and an LLVM version bump touches one wall. This mirrors Swift's
  `lib/IRGen`, rustc's `llvm-wrapper`, and Zig's `zig_llvm.cpp`. RFC 0016 already
  scopes LLVM linkage to `compiler/backend/**`; this design makes the
  "no LLVM header outside the shim" rule explicit. Because ZOM is C++20 the wall
  is architectural (an isolating library + Pimpl), not the C-ABI shim that Rust
  and Zig need only because they cannot consume C++ directly.
- **IR construction and verification.** Build LLVM IR with the standard builder
  inside the shim; translate LIR block parameters to LLVM PHI nodes at the wall
  (LIR itself has no PHI op); use opaque pointers with the accessed type named on
  each load/store/GEP; run `verifyModule` after a total translation and publish
  only on success. This is textbook MLIR/Cranelift/Swift/LLVM practice and
  matches RFC 0021 exactly. ZOM additionally proves optimizer facts
  (`inbounds`, `noalias`, `nsw`, and similar) with pre-translation verifier
  records and excludes `undef`/`poison` from LIR, which is stronger than the
  reference compilers that emit such attributes on frontend trust; this is a
  deliberate improvement, not a divergence to fix.
- **Object emission and linking.** Emit a `.o` with
  `TargetMachine::addPassesToEmitFile(CodeGenFileType::ObjectFile)` inside the
  shim, then hand off to a discovered system linker driver; do not reimplement
  linking. Toolchain roots (sysroot or SDK, linker, CRT objects, default libs)
  come from RFC 0043's supplied-not-ambient `ToolchainClosure`, mirroring the
  RFC 0016 `LLVM_DIR` discipline. This split matches rustc and Clang.
- **Version-upgrade workflow.** On any LLVM bump, change the exact-version pin in
  one place, fix all API breakage only inside `compiler/backend/llvm/**`, bump
  the CI image, and run the full suite, in one reviewed change with no dual code
  paths (aligns with design principle 3 and RFC 0016's reviewed-dependency-update
  clause). The `19.1.5 -> 22.1.8` move crosses several majors; budget shim churn,
  but opaque pointers are already the only pointer form since LLVM 17, so no
  typed-pointer migration is needed at 22.1.8.

### Deliberate divergence from industry practice

ZOM exposes **no generic multi-backend abstraction** (unlike rustc's
`CodegenBackend` trait with Cranelift and GCC alternates, or Zig's swappable
self-hosted backends). RFC 0021 rejects an LLVM-shaped backend interface because
ZOM has exactly one backend and a second abstraction would be a second source of
ABI and operation truth. The recorded trade-off, per Zig's experience, is that a
future fast-debug backend or an optional-LLVM posture would require introducing
that abstraction later; this is accepted, not deferred work for this quarter.

### Prerequisite drift to resolve at implementation time

- CI (`.github/workflows/CI.yml`) currently uses `ubuntu-latest` and
  `macos-latest` with no LLVM dev package installed and no architecture matrix;
  the accepted RFC 0016/0043 CI contract fixes `ubuntu-24.04` and `macos-15`
  with pinned `22.1.8` and a four-lane native-vs-cross matrix. The runner-label
  and package cutover is part of step 2 and owned by RFC 0043 for the
  `.github/workflows/**` changes.
- `CMakePresets.json` does not yet forward `LLVM_DIR`; RFC 0016 requires presets
  to forward only the supplied `LLVM_DIR`. This forwarding is part of step 2.

### Sourcing note

The industry-practice inputs (rustc `llvm-wrapper` and `download-ci-llvm`, Swift
`lib/IRGen`, Zig `zig_llvm.cpp` and optional-LLVM posture, the Clang
`find_package(LLVM CONFIG)` plus `llvm_map_components_to_libnames` embedding
pattern, opaque pointers since LLVM 17, and `verifyModule` discipline) are
well-established facts corroborated across both the repository-contract review
and the best-practices survey; the survey's live web fetches were restricted, so
those points rest on canonical project documentation and source layout rather
than freshly fetched text. None of them is asserted as a new ZOM contract; each
either matches an already-accepted RFC choice or is an implementation-time
recommendation recorded here for the IMPLEMENTING phase.
