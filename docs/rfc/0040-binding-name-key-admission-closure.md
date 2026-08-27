---
rfc: 40
title: Binding Name Key Admission Closure
type: compiler
status: ACCEPTED
author: ZOM Compiler Team
review-manager: rfc
required-owners: [rfc, binder-checker, verification]
approvers: [rfc, binder-checker, verification]
created: 2026-07-28
updated: 2026-07-28
area: compiler
requires: [27, 30, 37, 39]
supersedes: []
superseded-by: []
discussion: docs/rfc/tracking/0040-review-and-implementation.md#discussion-record
decision: docs/rfc/tracking/0040-review-and-implementation.md#decision-record
implementation: docs/rfc/tracking/0040-review-and-implementation.md#implementation-tracker
tracking-issue: docs/rfc/tracking/0040-review-and-implementation.md#implementation-tracker
---

# RFC 0040: Binding Name Key Admission Closure

## Summary

This RFC adds the missing Binder-owned validated factory for
`BindingNameKey`. The factory accepts one of the five closed `Namespace`
values and an already validated `DeclaredDefinitionName`.

RFC 0030 `R30-12N-E` can then construct `StableLocalExportFact` through public
value contracts, and `R30-12O-E` can reconstruct its name field from canonical
bytes. The prerequisite remains part of the existing cumulative atomic
`R29-12AB` source transaction.

## Motivation

`StableLocalExportFact` stores `BindingNameKey`, but the live type exposes only
`clone()`, `nameSpace()`, and `name()`. Its constructor is private to selected
production builders and verifiers. Stable fact tests cannot create a legal
fixture, and the accepted S3 decoder cannot reconstruct the field after
decoding its namespace and declared name.

Adding a decoder as a friend, exposing an unchecked constructor, or replacing
the accepted schema field would bypass the type-owned validation boundary.
Source review therefore pauses after approved `R30-12O-D`.

## Goals

- Add one validated public factory owned by `BindingNameKey`.
- Admit exactly the five current `Namespace` enumerators.
- Keep canonical name validation with `DeclaredDefinitionName`.
- Resume `R30-12N-E` and `R30-12O-E` without friendship expansion or duplicate
  storage.
- Preserve the current atomic landing set and `R30-15` as the only source
  commit and push.

## Non-Goals

- This RFC does not change `BindingNameKey` fields or namespace tags.
- This RFC does not add a standalone `BindingNameKey` codec.
- This RFC does not add a public unchecked constructor.
- This RFC does not change RFC 0027's stable fact or schema model.
- This RFC does not add compatibility behavior or an internal revision.

## Prior Art

Rust validated scalar wrappers use total input types with factories that
return `Option` when the input lies outside the value domain:
<https://doc.rust-lang.org/std/num/struct.NonZero.html>.

Git parses textual object names into typed object identifiers before object
lookup, separating identifier admission from loading and validating content:
<https://git-scm.com/docs/git-rev-parse>.

LLVM CAS similarly distinguishes parsed `CASID` values from object references
and loaded object proxies:
<https://llvm.org/docs/ContentAddressableStorage.html>.

ZOM already applies the same pattern to `LocalSyntaxPath`,
`DeclaredDefinitionName`, stable routing keys, and stable fact factories.
`BindingNameKey` should own its closed namespace admission for the same reason:
callers provide components, while the value type decides whether they form a
valid value. These three mature designs consistently keep identifier parsing
or admission separate from later object or content validation.

## Guide-Level Explanation

Callers construct a binding name through:

```cpp
auto key = BindingNameKey::from(nameSpace, zc::mv(name));
```

Unknown namespace values return `zc::none`. The declared name has already
passed its identity-owned canonical validation.

```mermaid
flowchart LR
    OD["Approved R30-12O-D"] --> A["R40-11 BindingNameKey admission"]
    A --> NE["R30-12N-E export facts"]
    NE --> OE["R30-12O-E export codecs"]
    OE --> PA["R30-12P-A failed lookup facts"]
```

## Reference-Level Design

`BindingNameKey` gains exactly:

```cpp
ZC_NODISCARD static zc::Maybe<BindingNameKey> from(
    Namespace nameSpace,
    identity::DeclaredDefinitionName&& name) noexcept;
```

The factory accepts `Namespace::Value`, `Namespace::Type`,
`Namespace::Module`, `Namespace::Label`, and `Namespace::Attribute`. Every
other underlying value returns `zc::none`. Successful admission invokes the
existing private constructor.

The constructor remains private. Existing privileged producers remain
unchanged. No alias, overload, friend, fallback, or second value
representation is added.

`R30-12O-E` decodes one namespace byte and one
`DeclaredDefinitionName`, calls `BindingNameKey::from`, constructs the
enclosing `StableLocalExportFact`, requires complete input consumption, and
requires exact full-record re-encoding.

### Review And Evidence

`R40-11` edits exactly:

```text
zomlang/compiler/binder/metadata/binding-metadata.h
zomlang/compiler/binder/metadata/binding-metadata.cc
zomlang/tests/unittests/compiler/binder/stable-binding-facts-test.cc
```

It counts additions plus deletions from exact predecessor hashes and permits
at most 400 changed lines. Pre-registration approval uses C++23 ASan and UBSan
`-Werror -fsyntax-only` compilation plus exact-hash review. The assertion
becomes executable after RFC 0030 `R30-13` registers the test, and `R30-14`
runs it before atomic publication.

All three files already belong to the RFC 0030 exact atomic landing set, so
this RFC does not expand the allowlist.

The strict order is:

```text
R30-12O-D -> R40-11 -> R30-12N-E -> R30-12O-E -> R30-12P-A
```

## Repository Impact

| Area | Paths | Owner |
|---|---|---|
| RFC authority | RFCs 0030, 0037, and 0040, their trackers, and the RFC index | `rfc` |
| Value admission | `binding-metadata.{h,cc}` | `binder-checker` |
| Native assertion | `stable-binding-facts-test.cc` | `verification` |
| Stable consumer | `stable-binding-facts.{h,cc}`, `stable-binding-codec.{h,cc}` | `binder-checker` |

## Security And Safety Impact

The factory rejects unknown namespace values before they enter stable facts or
canonical decoding. It moves no raw memory, performs no layout access, and
retains the existing canonical declared-name type.

## Drawbacks And Risks

The public surface grows by one factory. The narrow closed-enum contract and
absence of a standalone codec limit that surface to the required admission
boundary.

## Alternatives Considered

### Add Stable Codecs As Friends

Rejected because codec ownership would leak into the value's representation
boundary.

### Make The Constructor Public

Rejected because arbitrary underlying namespace values would become directly
constructible.

### Use `ImportBindingNameProjection`

Rejected because it is a production projection, not the accepted stable schema
field.

### Add A Test-Only Factory

Rejected because tests must exercise the production public contract.

## Compatibility And Rollout

There is no compatibility surface. The factory, stable fact consumer, stable
codec consumer, assertions, and final gates land in the existing single
atomic source transaction.

## Documentation And Teaching Plan

RFC 0030 and RFC 0037 record the inserted dependency. No language
specification or user-facing documentation changes.

## Operational Readiness

The change adds no runtime service, CLI option, or persistence migration.
Exact-file review, pre-registration syntax compilation, and RFC 0030 final
native and landing-scope gates remain mandatory.

## Acceptance Criteria

- All required owners approve one unchanged proposal and tracker hash.
- The factory accepts exactly five namespace values and rejects unknown values.
- No public unchecked constructor, friend, codec, alias, or duplicate storage
  is added.
- `R30-12N-E` depends on `R40-11`.
- The atomic landing set and sole `R30-15` source publication are unchanged.
- RFC, English-only, internal-versioning, format, and diff gates pass.

## Implementation Plan

1. Accept and publish one synchronized design-only transaction.
2. Implement and approve `R40-11` against exact predecessor hashes.
3. Resume `R30-12N-E` and `R30-12O-E`.
4. Continue RFC 0037's dependency graph.
5. Land cumulative source only through `R30-15`.

## Test Plan

- Pre-registration: C++23 ASan and UBSan `-Werror -fsyntax-only` compilation
  for the exact source and test translation units.
- Post-registration: focused `stable-binding-facts-test` plus RFC 0030 full
  native gates.
- RFC: `python3 scripts/check-rfc.py`.
- Format: `python3 scripts/check-format.py`.
- Internal naming: `python3 scripts/check-no-internal-versioning.py --check`.
- Diff hygiene: `git diff --check`.

## Open Questions

None

## Status History

| Date | Status | Notes |
|---|---|---|
| 2026-07-28 | DRAFT | Initial proposal written from the N-E/O-E admission preflight. |
| 2026-07-28 | REVIEW | Ready for exact-hash owner review. |
| 2026-07-28 | ACCEPTED | All required owners approved proposal SHA-256 `e007151b20d9803a71a8441a7b6c8ca3934c2d976c3a7cfb67ed94d13a51fd9e` and tracker SHA-256 `71d89625e1f4fc821df55b39b8d95958031480249a8d07c940a2b829f39c15c0`. Transaction `rfc0040-accept-20260728-e007151b` authorizes `R40-11` without changing source or implementation status. |
