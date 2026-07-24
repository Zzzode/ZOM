# ZOM Language Design Notes

This directory explains the current ZOM language model in focused,
cross-cutting documents. These notes help contributors understand how
source-level concepts connect to the compiler without turning an RFC or an
implementation detail into language law.

## Authority

Language design notes are non-normative. When sources disagree, use this order:

1. `docs/spec/chapters/` defines user-observable language behavior.
2. Accepted and landed RFC decisions define approved target designs and
   compiler contracts; their status records whether implementation is complete.
3. The production compiler and project-native tests establish what is
   implemented.
4. Design notes synthesize those sources and expose mismatches between them.
5. RFC trackers and reports record execution evidence and review history; they
   do not define language behavior.

An `ACCEPTED` or `IMPLEMENTING` RFC is not evidence that its contract is
available in the production compiler. A public type, enum alternative, test
fixture, parser acceptance case, or diagnostic registration is also not
production implementation by itself.

If a note discovers an absent or conflicting language rule, it must record the
gap without choosing a new rule. Resolve the decision through the RFC process,
update the normative specification, implement and verify it, and only then
describe it as current behavior.

## What Belongs Here

A language design note should answer one durable semantic question, such as:

- what constitutes a value, place, move, borrow, or temporary;
- how evaluation, initialization, cleanup, and control flow interact;
- how overload resolution, conversions, and generic constraints compose;
- how errors, effects, concurrency, or unsafe operations cross boundaries; or
- which source guarantee each compiler stage must preserve.

Keep syntax inventories, grammar productions, and conformance requirements in
the specification. Keep proposed alternatives, trade-offs, and rollout plans in
RFCs. Keep compiler-only capability and artifact details in the relevant
compiler design document.

## Evidence Classes

Use these terms consistently:

| Class | Meaning | Required evidence |
|---|---|---|
| **Normative** | Required user-observable behavior | A precise section in `docs/spec/chapters/` |
| **Implemented** | Enforced or produced by the production path | Live implementation plus a project-native test or gate |
| **Accepted target** | Approved direction not yet fully implemented | An accepted RFC and its current tracker boundary |
| **Open gap** | Missing, conflicting, or insufficiently verified behavior | Exact conflicting or absent authority and affected production stage |

Do not infer a stronger class from a weaker one. In particular, parser support
does not imply semantic support, and a representable IR operation does not
imply that production lowering emits it.

## Required Document Shape

Start from [0000-template.md](0000-template.md). Every note must include:

1. **Authority And Status** — scope, evidence date, and whether coverage is
   complete or partial.
2. **Question** — the semantic problem the note explains.
3. **Current Model** — one coherent explanation of the current contract.
4. **Semantic Invariants** — concise rules that must remain true across
   implementations.
5. **Compiler Realization** — the production stages that enforce or preserve
   the model.
6. **Evidence Map** — exact spec, RFC, implementation, and native-test sources.
7. **Known Gaps** — current missing or conflicting facts, without designing the
   replacement inline.

Use examples only when the documented syntax is accepted by the current parser.
Mark whether an example is syntax-only or reaches the stated semantic stage.
Do not copy EBNF, large normative tables, or RFC history into a design note;
link the authoritative source instead.

## Authoring Workflow

1. Define one semantic question and its observable boundary.
2. Read the relevant spec chapters, governing RFCs, production implementation,
   and native tests.
3. Build the evidence map before writing the explanatory model.
4. Classify each material claim as normative, implemented, accepted target, or
   open gap.
5. Remove duplicated normative prose and link to its source.
6. Run the native checks for every touched authority. A design-only change
   requires at least RFC validation, Markdown/diff hygiene, and any existing
   documentation architecture gate that covers the referenced contract.

Language design changes that alter syntax, semantics, diagnostics, memory
behavior, or compiler contracts still require an RFC. A design note cannot be
used to bypass that process.

## Naming And Scope

- Use lowercase kebab-case filenames.
- Prefer one question per document over chapter-sized surveys.
- Split a note when two sections have independent normative sources or
  implementation owners.
- Use terminology from the specification and production types. Define a new
  term only through the RFC process.
- Keep historical and migration material outside living design notes.

## Current Notes

| Note | Coverage | Purpose |
|---|---|---|
| [Values, Places, And Evaluation](values-places-and-evaluation.md) | Partial | Separates the source semantic model from the currently admitted scalar HIR and Built MIR subset |

## Process Influences

This structure follows the separation used by mature language projects:

- [Rust RFCs](https://rust-lang.github.io/rfcs/) retain design decisions while
  the [Rust Compiler Development Guide](https://rustc-dev-guide.rust-lang.org/)
  explains the current compiler.
- [Swift Evolution](https://github.com/swiftlang/swift-evolution/blob/main/process.md)
  separates reviewed proposals from implementation and language documentation.
- [Go proposals](https://go.googlesource.com/proposal/) preserve design review
  records without replacing the language specification.
- [PEP 1](https://peps.python.org/pep-0001/) distinguishes proposal history
  from current reference documentation.
