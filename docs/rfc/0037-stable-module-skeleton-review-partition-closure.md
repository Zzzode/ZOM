---
rfc: 37
title: Stable Module Skeleton Review Partition Closure
type: testing
status: ACCEPTED
author: ZOM Compiler Team
review-manager: rfc
required-owners: [rfc, binder-checker, verification]
approvers: [rfc, binder-checker, verification]
created: 2026-07-28
updated: 2026-07-28
area: testing
requires: [27, 30, 31, 34, 36]
supersedes: []
superseded-by: []
discussion: docs/rfc/tracking/0037-review-and-implementation.md#discussion-record
decision: docs/rfc/tracking/0037-review-and-implementation.md#decision-record
implementation: docs/rfc/tracking/0037-review-and-implementation.md#implementation-tracker
tracking-issue: docs/rfc/tracking/0037-review-and-implementation.md#implementation-tracker
---

# RFC 0037: Stable Module Skeleton Review Partition Closure

## Summary

This RFC replaces RFC 0030 tasks `R30-12N` through `R30-12Q` with bounded,
dependency-ordered fact and codec reviews. Module-skeleton component facts are
implemented in five groups, and each group receives its codec before the next
group. Failed-lookup facts and codecs then land before the complete
`BoundModuleSkeleton`, because the aggregate stores a canonical sequence of
`StableFailedLookupFact`. Projection facts and codecs follow the complete
aggregate.

Every review retains the 400 additions-plus-deletions cap. All source remains
in the existing cumulative uncommitted tree and lands only through RFC 0030
`R30-15`. Stable types, fields, domains, tags, wire formats, invariants,
landing files, and the immutable implementation-series base do not change.

## Motivation

The accepted `R30-12N` task combines eleven Pimpl records with 64 schema
fields, their admitted factories, clone and equality behavior, cross-field
validation, and native tests in three files under one 400-line cap. A readable
implementation cannot satisfy that boundary. The minimum public Pimpl shells,
field storage, accessors, factories, and equality surfaces consume the review
budget before the required mutation evidence is added. Dense macro expansion
would hide the contracts reviewers must inspect.

The accepted dependency order also places `BoundModuleSkeleton` before
`StableFailedLookupFact`, even though the aggregate owns
`CanonicalSequence<StableFailedLookupFact>` by value. A forward declaration
can name the sequence in a public declaration, but the aggregate Pimpl,
factory, clone, comparison, codec, and populated native test require the
complete failed-lookup type and codec.

The review graph must expose both the size boundary and the real type
dependency.

## Goals

- Preserve the 400-line additions-plus-deletions cap for every source review.
- Give each coherent module-skeleton fact group an independent Pimpl review.
- Implement each component codec before a later aggregate constructs populated
  canonical sequences.
- Implement failed-lookup facts and codecs before `BoundModuleSkeleton`.
- Require the complete aggregate test to use production sequence admission for
  every populated component family.
- Preserve one cumulative source tree and one final atomic landing.

## Non-Goals

- This RFC does not change stable record fields, tags, domains, bounds, or
  canonical ordering.
- This RFC does not change Binder provider or verifier responsibilities.
- This RFC does not add public constructors, fixture-only builders, alternate
  comparators, generated C++ records, compatibility paths, or fallback
  behavior.
- This RFC does not change CMake, the exact landing allowlist, the immutable
  base, or the final commit boundary.
- This RFC does not authorize an intermediate source commit or push.

## Prior Art

LLVM review guidance permits dependency-ordered patch series and asks that
changes remain small enough for effective review:
<https://llvm.org/docs/CodeReview.html>.

LLVM contribution guidance recommends isolated changes with focused tests:
<https://llvm.org/docs/Contributing.html>.

Git patch series record an explicit base and prerequisite order:
<https://git-scm.com/docs/git-format-patch#_base_tree_information>.

This RFC applies those review boundaries while preserving the accepted atomic
repository transaction.

## Guide-Level Explanation

The corrected review sequence is:

```mermaid
flowchart LR
    M["Approved R30-12M"] --> SA["Scope facts and codecs"]
    SA --> DA["Declaration facts and codecs"]
    DA --> PA["Parameter facts and codecs"]
    PA --> IA["Import facts and codecs"]
    IA --> EA["Export facts and codecs"]
    EA --> LF["Failed lookup facts and codecs"]
    LF --> MS["Bound module skeleton and codec"]
    MS --> PR["Projection facts and codecs"]
    PR --> OB["Owner body partitions"]
```

Each fact review owns public invariants and value behavior. Its following codec
review owns canonical bytes, independent wire oracles, complete consumption,
and hostile mutation rejection. The aggregate is delayed until every element
type it stores is complete and canonically admissible.

## Reference-Level Design

### Replacement Review Tasks

RFC 0030 tasks `R30-12N`, `R30-12O`, `R30-12P`, and `R30-12Q` are replaced by:

| Task | Entities And Evidence |
|---|---|
| `R30-12N-A` | `StableScopeFact` and `StableNodeScopeFact`; owner, parent, kind, root, path, scope, clone, equality, and local ownership mutations |
| `R30-12O-A` | Matching scope codecs; fixed wires, field mutations, truncation, trailing bytes, and canonical sequence admission |
| `R30-12N-B` | `StableDeclarationFact` and `StableImplementationOccurrenceFact`; key-record, module, authority, declaring-scope, kind, namespace, name, activation, visibility, clone, and equality mutations |
| `R30-12O-B` | Matching declaration codecs and complete independent wire mutations |
| `R30-12N-C` | Both parameter-declaration facts; key-record, site, declaring-scope, name-position, clone, and equality mutations |
| `R30-12O-C` | Matching parameter-declaration codecs and complete independent wire mutations |
| `R30-12N-D` | `StableImportFact` and `StableModuleAliasFact`; import key, scope, target, canonical target, namespace, origin, visibility, export flag, alias, module, revision, clone, and equality mutations |
| `R30-12O-D` | Matching import and alias codecs and complete independent wire mutations |
| `R30-12N-E` | `StableReexportStep` and `StableLocalExportFact`; module, exact export path, binding, canonical target, name, visibility, reexport chain, clone, and equality mutations |
| `R30-12O-E` | Matching reexport and local-export codecs, populated production-built reexport sequence, and complete independent wire mutations |
| `R30-12P-A` | `StableFailedLookupOutcome` and `StableFailedLookupFact`; closed variants, non-empty namespaces or candidates, owner, path, namespace, name, clone, equality, and relation mutations |
| `R30-12Q-A` | Matching failed-lookup codecs, closed tags, populated production-built sequences, and complete independent wire mutations |
| `R30-12N-F` | `BoundModuleSkeleton`; populated production-built component sequences, module ownership, uniqueness, parent order, coverage, body-owner, failed-lookup, clone, and inequality mutations |
| `R30-12O-F` | Complete module-skeleton codec, independent wire oracle, sequence, count, field, truncation, trailing-byte, and ownership mutations |
| `R30-12P-B` | `StableExportedBinding`, `StableExportedBindingQueryKey`, and `StableScopeNameBucketQueryKey`; projection fact and key invariants |
| `R30-12Q-B` | Matching projection codecs, independent wire oracles, and complete mutation coverage |

### Exact Files And Line Accounting

Every fact task edits exactly:

```text
products/zomlang/compiler/binder/stable-binding-facts.h
products/zomlang/compiler/binder/stable-binding-facts.cc
products/zomlang/tests/unittests/compiler/binder/stable-binding-facts-test.cc
```

Every codec task edits exactly:

```text
products/zomlang/compiler/binder/stable-binding-codec.h
products/zomlang/compiler/binder/stable-binding-codec.cc
products/zomlang/tests/unittests/compiler/binder/stable-binding-facts-test.cc
```

Each review records the exact approved predecessor SHA-256 tuple, exact
candidate tuple, and additions plus deletions across all three files. The
total must not exceed 400. No later task may use an unapproved predecessor.

### Dependency Order

The strict cumulative order is:

```text
R30-12M
  -> R30-12N-A -> R30-12O-A
  -> R30-12N-B -> R30-12O-B
  -> R30-12N-C -> R30-12O-C
  -> R30-12N-D -> R30-12O-D
  -> R30-12N-E -> R30-12O-E
  -> R30-12P-A -> R30-12Q-A
  -> R30-12N-F -> R30-12O-F
  -> R30-12P-B -> R30-12Q-B
  -> R30-12R
```

The non-alphabetic aggregate position is intentional. Task identifiers retain
their RFC 0030 semantic families, while dependency edges define execution.

### Aggregate Admission

`R30-12N-F` may begin only after `R30-12Q-A`. Its native test must construct
at least one valid instance of every module-skeleton component through the
public admitted factory, admit every sequence through
`StableBindingSequenceBuilder<T>`, and pass those populated sequences to the
aggregate factory.

The aggregate factory enforces only stable local relations representable from
its complete inputs. Provider and verifier responsibilities recorded by RFC
0027 remain unchanged. No test-only constructor or empty-sequence substitution
may replace required populated evidence.

### Landing And Status

The design transaction changes no source or implementation status. All review
tasks accumulate in the existing uncommitted RFC 0030 source tree. RFC 0030
`R30-15` remains the only source commit and push.

## Repository Impact

| Area | Paths | Owner |
|---|---|---|
| RFC authority | RFCs 0030, 0036, and 0037, their trackers, and the RFC index | `rfc` |
| Stable facts | `stable-binding-facts.{h,cc}` | `binder-checker` |
| Stable codecs | `stable-binding-codec.{h,cc}` | `binder-checker` |
| Native evidence | `stable-binding-facts-test.cc` | `verification` |

## Security And Safety Impact

The proposal adds no external input or runtime authority. Smaller reviews make
module ownership, canonical count limits, and hostile wire rejection easier to
audit without weakening any bound.

## Drawbacks And Risks

- Sixteen exact-hash reviews replace four broad reviews.
- Facts and codecs alternate, so predecessor tuples must be recorded
  carefully.
- Aggregate implementation occurs after a task whose identifier sorts later.

These costs are preferable to unreadable macro compression, incomplete-type
storage, or unreviewed invariants.

## Alternatives Considered

### Compress R30-12N Under 400 Lines

Macros or dense one-line declarations would hide Pimpl lifecycle, ownership,
and cross-field validation. They reduce physical line count without producing
a coherent review unit.

### Forward Declare StableFailedLookupFact

A forward declaration can appear in a public signature, but it cannot make the
aggregate Pimpl, canonical sequence operations, codec, and populated tests
complete before the failed-lookup implementation.

### Move Failed Lookups Out Of BoundModuleSkeleton

That changes the accepted stable record and query contract. The aggregate must
retain failed lookup evidence.

### Add A Temporary Aggregate Without Failed Lookups

That would create an intermediate contract and a forward-compatibility path.
The repository is unpublished, so the correct aggregate replaces no partial
form.

## Compatibility And Rollout

There is no compatibility surface. The RFC changes only the review graph for
unpublished source. Rollback before `R30-15` discards the cumulative candidate;
rollback after publication reverts the single atomic commit.

## Documentation And Teaching Plan

RFC 0030 and its tracker expose the corrected dependency graph. RFC 0036
records that its approved Binder bridge resumes through RFC 0037. No language
specification or user documentation changes.

## Operational Readiness

Every review records exact predecessor and candidate hashes, exact-file line
accounting, owner decisions, and source-level evidence. Existing schema,
format, English-only, internal-versioning, build, sanitizer, native-test,
architecture, allowlist, and landing-scope gates remain mandatory.

## Acceptance Criteria

- RFC 0030 names all replacement tasks and their exact dependencies.
- Every replacement task retains the 400-line exact-file cap.
- Every component codec follows its fact and precedes aggregate admission.
- Failed-lookup facts and codecs precede `BoundModuleSkeleton`.
- Projection work follows the complete module-skeleton codec.
- No alternate canonical-sequence construction path exists.
- `R30-12R` depends on `R30-12Q-B`.
- The immutable base, exact landing set, and atomic commit remain unchanged.
- All required owners approve one unchanged proposal and tracker hash.
- `python3 scripts/check-rfc.py` passes.

## Implementation Plan

1. Accept and publish one design-only synchronization transaction.
2. Implement and approve each replacement task in strict dependency order.
3. Resume the existing RFC 0030 owner-body partitions at `R30-12R`.
4. Complete RFC 0030 native wiring and clean-worktree verification.
5. Land all cumulative source only through RFC 0030 `R30-15`.
6. Synchronize truthful RFC 0037 status after the atomic source publication.

## Test Plan

- RFC structure: `python3 scripts/check-rfc.py`.
- Schema: `python3 scripts/check-stable-binding-schema.py --check`;
  `python3 scripts/check-stable-binding-schema.py --self-test`.
- Format: `python3 scripts/check-format.py`.
- Repository language: `python3 scripts/check-english-only.py --check
  --base-file products/zomlang/tests/coverage/implementation-series-base.txt`.
- Internal naming: `python3 scripts/check-no-internal-versioning.py --check`.
- Diff hygiene: `git diff --check`.
- Native evidence after source wiring: RFC 0030 Test Plan.

## Open Questions

None

## Status History

| Date | Status | Notes |
|---|---|---|
| 2026-07-28 | DRAFT | Initial proposal written from the rejected R30-12N review boundary. |
| 2026-07-28 | REVIEW | Ready for exact-hash owner review. |
| 2026-07-28 | ACCEPTED | All required owners approved proposal SHA-256 `ed0b9170c813e42cf02e8a719886ce47aadec5cfbe2ddb788e24572c7243319e` and tracker SHA-256 `063ea9961caa03d957b976a24d7f4bc9f7489dbdd0442e48b825975d1467470e`. Acceptance transaction `rfc0037-accept-20260728-ed0b9170` publishes the bounded review graph and authorizes source review to resume at `R30-12N-A` without changing source or implementation status. |
