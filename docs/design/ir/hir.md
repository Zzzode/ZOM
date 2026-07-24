# Semantic HIR

Updated: 2026-07-24

## Authority And Status

| Field | Value |
|---|---|
| Authority | Non-normative compiler implementation guide |
| Coverage | Partial production HIR |
| Governing decisions | [RFC 0010](../../rfc/0010-intermediate-representation-pipeline.md), [RFC 0013](../../rfc/0013-ownership-analysis-integration-boundary.md) |
| Production implementation | [`compiler/hir`](../../../products/zomlang/compiler/hir/) |
| Session integration | [`compiler-session.cc`](../../../products/zomlang/compiler/driver/compiler-session.cc) |
| Native verification | [`hir-module-test.cc`](../../../products/zomlang/tests/unittests/compiler/hir/hir-module-test.cc), [`compiler-session-package-test.cc`](../../../products/zomlang/tests/unittests/compiler/driver/compiler-session-package-test.cc) |

Semantic HIR is a live, immutable, session-published capability. Its current
producer covers only module scalar declarations and a narrow scalar-return
function shape. This document does not claim general expression, statement, or
control-flow lowering.

## Role In The Pipeline

`HirBuilder` consumes one `VerifiedCheckedModule`. That handoff binds:

- the semantic context, package, crate, module, source, and parsed receipt;
- checked and dispatch revisions;
- verified borrow evidence and its repository lease;
- the module's own interface revision and imported interface revisions; and
- the canonical semantic type store used by the session.

The builder creates an untrusted `HirModuleCandidate` that retains the exact
verified handoff. `HirVerifier` independently checks the candidate through
those retained semantic authorities and is the sole creator of
`VerifiedHirModule`. The live downstream consumer is `BuiltMirBuilder`.

## Representation

The current HIR data model contains:

| Record | Meaning |
|---|---|
| `HirValueDeclaration` | A module-level value declaration and its initializer |
| `HirFunctionDeclaration` | A module-level function and body block |
| `HirBlockStatement` | A function block containing statement identities |
| `HirReturnStatement` | A return and its result expression |
| `HirBindingPattern` | The binding definition created by a declaration |
| `HirScalarLiteralExpression` | A canonical scalar literal with semantic type |

Expression records carry `HirValueCategory`, whose schema can represent
`Value` and `Place`. Every current producer path emits `Value`.

HIR identities are layer-local `HirNodeId` values. They are one-based,
deterministically assigned, and never expose AST `NodeId` as semantic IR
identity. Source ranges and canonical `DefId` values preserve traceability
without making syntax-tree identity part of the HIR contract.

## Production Profile

### Module value declarations

The live builder admits only declarations that satisfy all of these conditions:

- module scope;
- `Static` or `Constant` storage;
- one direct identifier binding pattern;
- no explicit type annotation;
- one scalar literal initializer; and
- exact agreement among definition, type, pattern, literal, signature, checked
  facts, and module interface.

A constant additionally requires the matching canonical constant fact with no
dependencies.

Each admitted declaration creates three deterministic HIR records: binding
pattern, scalar literal expression, and value declaration.

### Functions

The live builder admits only an ordinary module-level function with:

- no generic parameters;
- no receiver;
- no parameters;
- no `raises` contract;
- one block statement;
- one `return` statement; and
- one scalar literal return expression whose type matches the signature.

Each admitted function creates four deterministic HIR records: scalar literal
expression, return statement, block statement, and function declaration.

Dispatch facts and every unsupported checker fact family must be empty.
Aggregates, places, coercions, casts, calls, assignments, members, indexes,
captures, unsafe operations, obligations, and error operations therefore fail
closed before HIR publication.

## Verified Guarantees

`HirVerifier` does not trust builder output. It rechecks:

- repository leases and exact checked and borrow-evidence revisions;
- module, source, context, interface, and import lineage;
- complete record cardinalities and definition coverage;
- deterministic node identities and record relationships;
- definition kinds, semantic types, value categories, and source ranges;
- AST shape only through the verified checked-module handoff;
- signature, interface, binding-pattern, literal, and constant facts; and
- absence of all fact families outside the admitted profile.

Success produces a move-only, immutable, NodeId-free `VerifiedHirModule`.
Failure produces no partial HIR publication.

## Identity, Lineage, And Determinism

Declarations and functions are sorted by source byte offset and then by
canonical definition key. Record allocation follows the fixed shapes above, so
equivalent semantic contexts produce the same HIR ordering.

`VerifiedHirModule` retains:

- semantic context fingerprint;
- package, crate, module, and source identities;
- parsed receipt;
- checked, dispatch, and borrow-evidence revisions;
- own and imported interface revisions; and
- live checked-fact and borrow-evidence leases.

HIR currently has no independent canonical revision or reversible codec.

## Inspection And Native Verification

`VerifiedHirModule::dump()` returns a deterministic diagnostic rendering that
starts with `zom.hir.v0`. It includes lineage digests, imported interfaces,
declarations, patterns, functions, blocks, returns, and canonical literals. It
first confirms that retained evidence is still resolvable.

The dump intentionally omits some in-memory fields, including visibility,
linkage, mutability, source ranges, declared types, and value categories. It is
therefore neither a complete serialization nor a reconstruction format.

Native tests cover empty-module lineage, scalar declarations, constants,
deterministic ordering and dumps, the scalar-return session path, and rejection
before partial HIR/MIR publication.

## Known Gaps

- General expressions, calls, operators, assignment, aggregates, places,
  borrows, projections, coercions, casts, and temporaries are not lowered.
- General statements and control flow, including local declarations,
  multi-statement blocks, branches, loops, and void returns, are not lowered.
- Parameters, receivers, generics, effects, methods, closures, constructors,
  destructors, and extern bodies are not lowered.
- No production path emits `HirValueCategory::Place`.
- HIR has no independent revision, complete canonical codec, or reversible
  text format.
- Native tests do not directly corrupt an arbitrary `HirModuleCandidate`, and
  imported-interface lineage lacks a dedicated positive multi-module HIR test.
