---
title: Checked-Fact Requirement Table Diverges From Checker Producers
date: 2026-09-08
area: checker
scope: compiler/checker/body/body-checker.cc requirement inventory vs production switch
status: findings-open
rfc: 0005
---

# Requirement Table Diverges From Checker Producers (2026-09-08)

A large family of "error: internal compiler error / please report this compiler
bug" reports on ordinary source share one cause: the checked-fact requirement
inventory promises facts for AST kinds the body checker has no producer for.

This is an implementation divergence from accepted RFC 0005, not a new design
question. The RFC states the contract directly
(`docs/rfc/0005-type-system-architecture.md:2127`):

> The generated AST schema owns a checked-fact requirements table. Each
> expression, declaration, pattern, operator, unsafe operation, and error
> operator row names its required maps and allowed optional maps. **The table
> generator compares every concrete AST kind with checker producers and fails on
> an unclassified kind.**

## Mechanism

`BodyFactRequirementInventoryBuilder::build` walks `tree.root()` — the whole
tree, not a callable body — and registers a `NodeType` requirement for every
kind `isExpression` accepts (`body-checker.cc:2293`).

The body checker then produces facts only for kinds its production switch
handles (`body-checker.cc:2344-2484`). `CheckedFactsVerifier` compares the two
and rejects the difference with `MissingRequiredFact`
(`checked-facts.cc:3288`), which the incident rail renders as a compiler-bug
message.

The body checker's own `rejectInvariant` sites are **not** involved. Verified by
instrumenting all 58 of them plus the 52 in `signature-facts.cc`: none fires for
this class. Instrumenting `CheckerDiagnosticProjector::project` gave the exact
attribution — stage `Body` (0x03), kind `MissingRequiredFact` (0x02).

## The divergence, counted

`isExpression` accepts 37 kinds. The production switch handles 19. These 18
register a requirement nothing can satisfy:

| | | |
|---|---|---|
| `ArrayLiteral` | `TupleLiteral` | `ObjectLiteralExpr` |
| `TemplateLiteralExpr` | `ConditionalExpr` | `CommaExpr` |
| `CastExpression` | `TypeOfExpression` | `IsExpression` |
| `NullCoalesceExpr` | `ErrorDefaultExpr` | `NewExpression` |
| `FunctionExpression` | `LambdaExpression` | `ImportCallExpression` |
| `SpawnExpression` | `ThisExpr` | `SuperExpr` |

A separate inconsistency in the same walk: `NullCoalesceExpr` registers a `Call`
requirement (`body-checker.cc:2285`) while having no production at all, so
registration and production disagree in two places rather than one.

## Scope of the effect

A full-corpus scan found 53 of 875 corpus files reach a body-stage invariant, 41
of them (77%) under `04-expressions`. That distribution is this divergence: not
53 defects, but 53 occurrences of these 18 kinds. Several are `pos_` fixtures
(sources the corpus expects to compile) and several are `neg_` fixtures (sources
the corpus expects to produce a *user* diagnostic), so the corpus already
encodes the expectation this violates.

## Why it is not a small fix

Gating registration on a producible-kind list is straightforward. Reporting the
refusal is not, because this layer has no source-diagnostic channel:

```cpp
using BodyFactRequirementInventoryBuildResult =
    zc::OneOf<VerifiedBodyFactRequirementInventory,
              checked::CheckedFactsInvariantRejected>;
```

The builder can only return an invariant. Emitting a "semantics unavailable"
diagnostic instead requires a third alternative on that result type plus changes
at both call sites (`compiler-session.cc:2308` and `:3290`).

There is also an unresolved design question the RFC text bears on: it specifies
that the **table generator** compares kinds against producers and fails on an
unclassified kind. That is a build-time failure, not a runtime diagnostic. A
faithful implementation may therefore belong in the schema generator rather than
in the checker, which would make the runtime path unreachable by construction.

## Recommended disposition

The generator-side half of the RFC's contract is now enforced:
`scripts/codegen/gen_ast.py` compares the checker's `isExpression` set against
its production switch and fails when a kind appears in neither that switch nor an
explicit `CHECKER_UNSUPPORTED_EXPRESSIONS` list. That gate is already registered
as the `ast-generated-schema` CTest, so the divergence can no longer grow
silently, and landing support for a kind requires deleting its entry in the same
change as the production. Verified to fail in both directions: removing a listed
kind and listing a produced one each abort the gate.

What remains is the reporting half. The 18 listed kinds still reach
`MissingRequiredFact` at runtime, because suppressing their registration without
a source-diagnostic channel would silently accept them instead — worse than an
incident. That needs the third result alternative described above, plus an owner
decision on whether the runtime path should exist at all once the generator
guarantees classification.

## Method note

Three successive hypotheses about the location were wrong — surface admission,
then `body-checker.cc`'s rejection sites, then `signature-facts.cc` — and each
looked plausible from reading alone. Only instrumenting the incident projector
gave the true attribution. For this class of failure, instrument the choke point
where the incident is constructed before forming a hypothesis about which
producer is responsible.
