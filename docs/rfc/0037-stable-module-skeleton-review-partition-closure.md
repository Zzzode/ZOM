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
dependency. The first `R30-12N-F` candidate later demonstrated a second
boundary: its complete aggregate value and local admission logic consumed the
full 400-line budget, leaving no room for the required adversarial matrix.
Verification returned that candidate while Binder approved its value contract.
Because all source is still cumulative and uncommitted, the aggregate
implementation review and its exhaustive evidence can be separated without
publishing an untested intermediate state.

## Goals

- Preserve the 400-line additions-plus-deletions cap for every source review.
- Give each coherent module-skeleton fact group an independent Pimpl review.
- Implement each component codec before a later aggregate constructs populated
  canonical sequences.
- Implement failed-lookup facts and codecs before `BoundModuleSkeleton`.
- Require the complete aggregate test to use production sequence admission for
  every populated component family.
- Review the complete aggregate value before a separately bounded adversarial
  evidence sequence.
- Block the aggregate codec and every consumer until the aggregate value and
  both aggregate evidence tasks are approved.
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
| `R30-12O-D` | Matching import and alias codecs through RFC 0039 typed revision admission and complete independent wire mutations |
| `R30-12N-E` | `StableReexportStep` and `StableLocalExportFact`; module, exact export path, binding, canonical target, name, visibility, reexport chain, clone, and equality mutations |
| `R30-12O-E` | Matching reexport and local-export codecs, populated production-built reexport sequence, and complete independent wire mutations |
| `R30-12P-A` | `StableFailedLookupOutcome` and `StableFailedLookupFact`; closed variants, non-empty namespaces or candidates, owner, path, namespace, name, clone, equality, and relation mutations |
| `R30-12Q-A` | Matching failed-lookup codecs, closed tags, populated production-built sequences, and complete independent wire mutations |
| `R30-12N-F1` | Complete `BoundModuleSkeleton` Pimpl, admitted factory, clone, equality, accessors, locally provable module and reference relations, linear indexes, iterative parent-graph validation, populated production-built component sequences, clone, inequality, foreign export, and missing module-body-owner evidence |
| `R30-12N-F2A` | Aggregate-only structural and relational evidence: reachable scope-graph rejection, semantic uniqueness, missing scope and owner references, body-owner and failed-lookup boundaries, and canonical-admission proofs for implied multiplicity invariants |
| `R30-12N-F2B` | Aggregate-only ownership and scale evidence: per-family module ownership, all eleven populated sequence accessors not already covered by the F1 module accessor, implementation-owned generic success and missing-occurrence rejection, and a reference-complete accepted iterative parent chain |
| `R30-12O-F` | Complete module-skeleton codec after the aggregate value and both evidence tasks; independent wire oracle, sequence, count, field, truncation, trailing-byte, and ownership mutations |
| `R30-12P-B` | `StableExportedBinding`, `StableExportedBindingQueryKey`, and `StableScopeNameBucketQueryKey`; projection fact and key invariants |
| `R30-12Q-B` | Matching projection codecs, independent wire oracles, and complete mutation coverage |

### Exact Files And Line Accounting

Every fact task edits exactly:

```text
products/zomlang/compiler/binder/stable/stable-binding-facts.h
products/zomlang/compiler/binder/stable/stable-binding-facts.cc
products/zomlang/tests/unittests/compiler/binder/stable-binding-facts-test.cc
```

Every codec task edits exactly:

```text
products/zomlang/compiler/binder/stable/stable-binding-codec.h
products/zomlang/compiler/binder/stable/stable-binding-codec.cc
products/zomlang/tests/unittests/compiler/binder/stable-binding-facts-test.cc
```

Each review records the exact approved predecessor SHA-256 tuple, exact
candidate tuple, and additions plus deletions across all three files. The
total must not exceed 400. No later task may use an unapproved predecessor.

`R30-12N-F2A` and `R30-12N-F2B` are the only fact reviews that each edit
exactly one file:

```text
products/zomlang/tests/unittests/compiler/binder/stable-binding-facts-test.cc
```

`R30-12N-F2A` records the approved `R30-12N-F1` test hash as its predecessor.
`R30-12N-F2B` records the approved `R30-12N-F2A` test hash as its predecessor.
Each changes at most 400 lines and may add production-facing fixtures and
assertions but no test-only constructor, alternate sequence builder, product
source, schema, or codec.

### Dependency Order

The strict cumulative order is:

```text
R30-12M
  -> R30-12N-A -> R30-12O-A
  -> R30-12N-B -> R30-12O-B
  -> R30-12N-C -> R30-12O-C
  -> R30-12N-D -> R39-11 -> R30-12O-D
  -> R40-11 -> R30-12N-E -> R30-12O-E
  -> R30-12P-A -> R30-12Q-A
  -> R30-12N-F1 -> R30-12N-F2A -> R30-12N-F2B -> R30-12O-F
  -> R30-12P-B -> R30-12Q-B
  -> RFC 0041 R41-11A
```

The non-alphabetic aggregate position is intentional. Task identifiers retain
their RFC 0030 semantic families, while dependency edges define execution.

### Aggregate Admission

`R30-12N-F1` may begin only after `R30-12Q-A`. Its native smoke test constructs
at least one valid instance of every module-skeleton component through the
public admitted factory, admits every sequence through
`StableBindingSequenceBuilder<T>`, and passes those populated sequences to the
aggregate factory. It proves move-only Pimpl behavior, complete populated
aggregate construction, clone, equality, one unequal field, one cross-module
local export rejection, and the module-body-owner presence requirement. Direct
sequence-accessor retention evidence belongs to `R30-12N-F2B`.

`R30-12N-F2A` begins only from the approved `R30-12N-F1` hashes. Its
production ztest fixture exercises the structural and relational aggregate
branches, including:

- missing parent, cycle, duplicate owner, body scope, and a missing module
  scope;
- duplicate declaration, occurrence, alias/import, generic-header,
  callable-parameter, node-root/path, local-export-path, and
  failed-lookup-owner/path keys;
- missing scope owners, definition-owned generic or callable owners, alias
  declarations, a definition body owner whose declaration is absent, and
  definition-header or implementation-header failed-lookup owners;
- forbidden body-query failed lookups;
- sequence-builder rejection for a second identical same-module module scope
  and module body owner, proving the public invariants that make aggregate
  multiplicity checks unnecessary.

`R30-12N-F2B` begins only from the approved `R30-12N-F2A` test hash. Its
production ztest fixture exercises:

- foreign module ownership independently in every component family not already
  covered by the F1 local-export rejection;
- all eleven populated sequence accessors not already covered by the F1 module
  accessor;
- the implementation-owned generic-parameter occurrence path for both an
  admitted occurrence and a missing occurrence;
- a deterministic, reference-complete, successfully admitted parent chain
  large enough to expose recursive or pairwise graph validation.

Both tasks derive valid and hostile values through public factories and the
production sequence builder. The large-count fixture records its deterministic
depth and must assert successful aggregate admission rather than a later
unrelated rejection.

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
| RFC authority | RFCs 0030, 0036, 0037, 0039, 0040, and 0041, their trackers, and the RFC index | `rfc` |
| Revision admission | `binding-metadata.{h,cc}` | `binder-checker` |
| Stable facts | `stable-binding-facts.{h,cc}` | `binder-checker` |
| Stable codecs | `stable-binding-codec.{h,cc}` | `binder-checker` |
| Native evidence | `stable-binding-facts-test.cc` | `verification` |

## Security And Safety Impact

The proposal adds no external input or runtime authority. Smaller reviews make
module ownership, canonical count limits, and hostile wire rejection easier to
audit without weakening any bound.

## Drawbacks And Risks

- Eighteen exact-hash reviews replace four broad reviews.
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
- `R30-12N-F1`, `R30-12N-F2A`, and `R30-12N-F2B` each remain within 400
  changed lines.
- `R30-12O-F` depends on the aggregate value and both evidence tasks.
- The aggregate evidence tasks cover every locally enforceable factory branch,
  every component ownership family, and successful deep iterative graph
  behavior.
- Projection work follows the complete module-skeleton codec.
- No alternate canonical-sequence construction path exists.
- RFC 0041 `R41-11A` depends on `R30-12Q-B`.
- The immutable base, exact landing set, and atomic commit remain unchanged.
- All required owners approve one unchanged proposal and tracker hash.
- `python3 scripts/check-rfc.py` passes.

## Implementation Plan

1. Accept and publish one design-only synchronization transaction.
2. Approve `R30-12N-F1` against its complete value and smoke-test boundary.
3. Add and approve the `R30-12N-F2A` structural and relational matrix.
4. Add and approve the `R30-12N-F2B` ownership and scale matrix.
5. Implement and approve each remaining replacement task in strict dependency
   order.
6. Resume the RFC 0041 owner-body partitions at `R41-11A`.
7. Complete RFC 0030 native wiring and clean-worktree verification.
8. Land all cumulative source only through RFC 0030 `R30-15`.
9. Synchronize truthful RFC 0037 status after the atomic source publication.

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
| 2026-07-28 | ACCEPTED | Transaction `rfc0039-accept-20260728-de7ab2aa` inserts `R39-11` between the approved `R30-12N-D` fact and its `R30-12O-D` codec without changing the stable fact contract or atomic publication boundary. |
| 2026-07-28 | ACCEPTED | All required owners approved amendment proposal SHA-256 `25caf4b94dd06953c27b1b09d8f07c4ca94f6b3c166618bc803620ebeb9f435a`, RFC 0037 tracker SHA-256 `21faf1b30428ead842e03023a1714907e355bbf7662eecf1e9257f8823f79aee`, RFC 0030 tracker SHA-256 `2351e98d1f6d73699487a4f1641f6808dd79bc8aae738f0a5e4fbe3cbec36530`, and RFC 0040 tracker SHA-256 `e60b7cd2fb1a5c81c58df22845ba90f83ffb77e879b8247c51ccec8027d1da98`. Amendment transaction `rfc0037-amend-20260728-25caf4b9` splits aggregate value and adversarial evidence review without changing source, schema, implementation status, or the atomic publication boundary. |
| 2026-07-28 | ACCEPTED | All required owners approved amendment proposal SHA-256 `d4e18a120f655e37259684de516b5455cff7ae594e9448f372b8d61ddfc35a76`, RFC 0037 tracker SHA-256 `a7474f044d81158fa7f5921206954a5118a187aa0211ffb23d0e1f66a238c58d`, and RFC 0030 tracker SHA-256 `b02684fb967253f90109a4f206c7a4bc32e32209aea56f9e205ee663ccc09fba`. Amendment transaction `rfc0037-amend-evidence-20260728-d4e18a12` partitions structural evidence from ownership and accepted-scale evidence without changing source, schema, implementation status, or the atomic publication boundary. |
