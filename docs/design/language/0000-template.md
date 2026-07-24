# <Concept Or Question>

## Authority And Status

| Field | Value |
|---|---|
| Authority | Non-normative language design note |
| Coverage | Complete or partial |
| Last verified | YYYY-MM-DD |
| Normative sources | Links to exact specification sections |
| Governing decisions | Links to landed or accepted RFCs |
| Production evidence | Links to implementation paths |
| Verification evidence | Links to project-native tests or gates |

State which claims are normative, implemented, accepted targets, or open gaps.
If coverage is partial, name the excluded boundary here.

## Question

State one semantic question in terms of observable source behavior. Explain why
the answer must remain stable across compiler implementations.

## Current Model

Explain the model as one coherent whole. Link normative rules instead of
copying grammar productions or large specification tables.

Examples must use current syntax and carry one of these labels:

- **Production:** reaches and passes the documented production stage.
- **Syntax-only:** accepted by the parser but not admitted by the documented
  semantic stage.

## Semantic Invariants

List the smallest set of rules that every conforming implementation must
preserve. Each invariant must point to normative authority or be explicitly
labelled as an accepted target.

## Compiler Realization

Trace the concept only through stages that currently construct, verify, or
consume it. Distinguish a data model from its production producers and
consumers.

## Evidence Map

| Claim | Class | Specification or RFC | Implementation | Native verification |
|---|---|---|---|---|
| <claim> | Normative, implemented, accepted target, or open gap | <link> | <link or none> | <link or none> |

## Known Gaps

Record exact missing or conflicting authority, implementation, or verification.
Do not choose a new semantic rule or include a rollout plan in this section.
