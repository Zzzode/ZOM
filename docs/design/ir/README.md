# ZOM Compiler IR Design Notes

Updated: 2026-07-24

This directory explains the intermediate representations and lowering
boundaries that exist in the production compiler. It is a contributor guide,
not a language specification, RFC, stable interchange format, or implementation
tracker.

## Authority

Use the following order when sources disagree:

1. `docs/spec/chapters/` defines user-observable language behavior.
2. Accepted RFCs define approved compiler contracts and their trackers record
   implementation status.
3. Production builders, independent verifiers, session consumers, and
   project-native tests establish what is implemented.
4. These notes explain that current implementation and expose gaps.

An RFC, namespace, public type, enum alternative, codec tag, diagnostic, or
failure phase is not evidence that an IR artifact exists in production. A
current artifact needs a production builder, an independent capability
verifier, an explicit session publication or access path, and native
verification. A successor stage is not current until a real production consumer
constructs and uses its verified capability.

## Current Pipeline

```mermaid
flowchart LR
    B["VerifiedBoundModuleInput"] --> C["Verified checker facts"]
    C --> E["Verified BorrowEvidence"]
    C --> M["VerifiedCheckedModule"]
    E --> M
    M --> HC["HIR candidate"]
    HC --> HV["HIR verifier"]
    HV --> H["VerifiedHirModule"]
    H --> MC["Built MIR candidate"]
    MC --> MV["Built MIR verifier"]
    MV --> R["VerifiedBuiltMir"]
    R -. "not implemented" .-> O["Ownership proof"]
    O -. "not implemented" .-> X["Executable MIR"]
    X -. "not implemented" .-> L["Target LIR"]
    L -. "not implemented" .-> N["LLVM and native artifacts"]
```

The session publishes `VerifiedHirModule` and `VerifiedBuiltMir` atomically for
the currently admitted scalar subset. Production ownership analysis,
drop/coroutine elaboration, executable MIR, target LIR, LLVM translation,
object emission, linking, and native execution are absent.

## Status Matrix

| Layer or boundary | Production status | Current live profile |
|---|---|---|
| Checked-module handoff | Implemented | Exact checker, dispatch, interface, and borrow-evidence lineage |
| Semantic HIR | Implemented, partial | Module scalar declarations and zero-parameter functions whose body is one scalar-literal return |
| Built MIR | Implemented, partial | One-block scalar initializer and scalar-return function shapes |
| Ownership and executable MIR | Not implemented | Borrow evidence is upstream lineage, not an ownership proof |
| Target LIR | Not implemented | The design remains in [RFC 0021](../../rfc/0021-target-aware-lir-and-llvm-translation.md), currently `DRAFT` |
| LLVM and native backend | Not implemented | No LLVM IR, object, link, or binary publication |

## Cross-Layer Invariants

The live IR pipeline enforces these rules:

1. **No semantic re-resolution.** Lowering consumes verified semantic facts and
   canonical identities; it does not repeat binding, inference, dispatch, or
   borrow-surface selection.
2. **Target independence.** Current HIR and Built MIR do not consume target
   layout, ABI, object-format, or LLVM state.
3. **Verifier-owned capability creation.** Builders create candidates. Only the
   corresponding verifier may create a public verified capability;
   `CompilerSession` separately owns atomic session adoption.
4. **Exact lineage.** Published modules retain the semantic context and the
   checked, dispatch, interface, and borrow-evidence revisions on which they
   depend.
5. **Deterministic identity and order.** Layer-local identities are one-based;
   declarations and functions use canonical deterministic order; Built MIR
   records and revisions are recomputable.
6. **Atomic adoption.** `CompilerSession::checkSources()` commits checker
   repositories, evidence, HIR, and MIR together only after every module
   succeeds.
7. **Representation is not reachability.** A representable place projection,
   statement, terminator, phase, or failure site is not production behavior
   unless the live builder emits it and the verifier proves it.
8. **Debug formats are internal.** A dump or canonical record is not a stable
   public serialization contract.

## Document Map

| Document | Purpose |
|---|---|
| [Semantic HIR](hir.md) | Current HIR model, admitted subset, lineage, builder, verifier, and dump |
| [Built MIR](built-mir.md) | Representation capacity, live producer profile, revision, and verified guarantees |
| [Lowering And Verification](lowering-and-verification.md) | Candidate-to-capability pattern, failure algebra, and atomic session publication |
| [Debugging And Dumps](debugging-and-dumps.md) | Available inspection surfaces, native checks, and missing dump support |

There is no `lir.md`. Add one only when the repository contains a production
LIR builder, independent verifier, session publication or downstream consumer,
and project-native tests. Until then, LIR belongs in RFC 0021 and its tracker.

## Required Shape For New Notes

Every IR note must include:

1. **Authority And Status** with the evidence date and coverage boundary.
2. **Role In The Pipeline** naming producer inputs and real consumers.
3. **Representation** describing what the data model can encode.
4. **Production Profile** listing exactly what live lowering emits.
5. **Verified Guarantees** listing what the independent verifier proves.
6. **Identity, Lineage, And Determinism** where applicable.
7. **Inspection And Native Verification** naming dumps, tests, and gates.
8. **Known Gaps** without designing the replacement inline.

Build the evidence map from live code and tests before writing explanatory
prose. Keep proposed operations, alternatives, rollout order, and unresolved
contracts in RFCs. Update a note in the same change whenever its builder,
verifier, publication boundary, emitted operation inventory, revision codec, or
debug surface changes.

## Documentation Model

This structure follows the separation used by mature compiler projects:

- the [Rust Compiler Development Guide MIR chapter](https://rustc-dev-guide.rust-lang.org/mir/index.html)
  explains a live IR through its CFG, places, operands, and transformations;
- [Swift SIL](https://github.com/swiftlang/swift/blob/main/docs/SIL.rst)
  documents a compiler IR separately from the source-language reference;
- the [LLVM Language Reference](https://llvm.org/docs/LangRef.html) distinguishes
  representable syntax from well-formed IR checked by the verifier;
- the [Go compiler SSA guide](https://github.com/golang/go/blob/master/src/cmd/compile/internal/ssa/README.md)
  combines the value/block model with practical inspection workflows; and
- [MLIR Language Reference](https://mlir.llvm.org/docs/LangRef/) keeps reference
  material separate from [design rationale](https://mlir.llvm.org/docs/Rationale/Rationale/).

ZOM applies the same separation while treating live production evidence as the
boundary for every implementation claim.
