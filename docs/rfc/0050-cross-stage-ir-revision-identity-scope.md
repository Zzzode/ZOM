---
rfc: 50
title: Cross-Stage IR Revision Identity Scope
type: compiler
status: REVIEW
author: ZOM Compiler Team
review-manager: rfc
required-owners: [ir-backend, module-system, rfc, runtime-memory, verification]
approvers: []
created: 2026-09-14
updated: 2026-09-15
area: compiler
requires: [10, 13, 15, 17, 21]
supersedes: []
superseded-by: []
discussion: docs/rfc/tracking/0050-cross-stage-ir-revision-identity-scope-review.md#discussion-record
decision: TBD
implementation: TBD
tracking-issue: docs/rfc/tracking/0050-cross-stage-ir-revision-identity-scope-review.md#decision-record
---

# RFC 0050: Cross-Stage IR Revision Identity Scope

## Summary

RFCs 0010, 0013, 0015, and 0021 place a SHA-256 canonical-content revision at
nearly every in-memory compiler boundary (MIR, target registry, feature
boundaries, borrow module interfaces, checker marker registries, and a specified
but unbuilt LIR revision), with exact byte-level hex preimages embedded as
oracles in the RFCs. This RFC re-reviews that decision against how the revisions
are actually consumed and against the revision mechanisms of mature compilers.
It asks reviewers to choose one of three scopes: retain and extend, re-scope to
a written "in-process lineage and leases only" rule with explicit deferral of
the unbuilt revisions, or remove revisions from boundaries that make no
cross-boundary integrity claim. This RFC changes no code and weakens no
implemented guarantee; it is a scoping decision and a removal of specified
unused surface.

## Motivation

The original motivation (RFC 0010 Motivation) is anti-correlation: a downstream
stage must not silently consume an IR that does not exactly match the upstream
facts and leases it verified against, and canonical identity must not depend on
heap addresses, hash-map iteration order, or debug names. That motivation is
sound and the implemented MIR revision is consumed. A 2026-09-14 audit,
however, found three drifts between the specification and production:

1. The revisions are used **only** inside one process. The MIR revision is
   recomputed by the Built MIR verifier
   (`compiler/mir/built-mir.cc`, `MirRevisionCodec` / `computeBuilt` / verifier
   comparison), retained on ownership overlay lineage fields and capability
   leases, and rendered by `--emit=mir`. No revision is persisted, used as a
   cache key, sent to another process, or content-addresses a store. RFC 0010
   explicitly forbids persistence use of these revisions and dumps.
2. The LIR revision specified by RFC 0021 (`LirRevisionId`,
   `zom.lir-revision`, a 546-byte empty-module preimage) is not implemented;
   the only LIR digest code, `AlgebraRevision` over `zom.lir-algebra`, has a
   codec oracle test but zero non-test consumers.
3. The query runtime separately implements its own `ReuseClass::Persisted`
   category with no descriptor rows using it, so the one use that would
   justify inter-process content addressing does not exist.

Meanwhile every cited mature compiler versions in-memory data with monotonic
integers, interned handles, or pointers within a session, and reserves hashes
for serialized metadata and build artifacts. The cost of the current scheme is
real: SHA-256 over full canonical records at every stage, exact preimage
oracles that must be regenerated on every codec ordering change, and a spec
surface (`LirRevisionId`, `AlgebraRevision`) that implies consumers which do
not exist. The review must decide whether the anti-correlation benefit at
in-process boundaries justifies retaining, extending, or trimming the scheme.

## Goals

- State a single written rule for which boundaries carry a canonical-content
  revision, what guarantees a revision provides, and which mechanism provides
  each guarantee.
- Decide the fate of the specified-but-unbuilt `LirRevisionId` and the
  consumerless `AlgebraRevision`.
- Keep every implemented anti-correlation guarantee (verifier recomputation,
  lease mismatch rejection, deterministic dumps) byte-identical or stronger.
- Bound the oracle burden: exact hex preimages appear in normative prose only
  where an external or persistence integrity claim is made.

## Non-Goals

- Designing a persisted incremental compilation cache or any on-disk IR
  serialization (deferred in RFC 0010; belongs to a future query persistence
  RFC).
- Changing the MIR vocabulary, codec framing, local/block ordinals, or the
  RFC 0048 re-ordinalization work.
- Changing semantic identity (`DefId`, `SemanticTypeId`, context brands,
  package/crate/module identities from RFC 0011), which answers a different
  question (identity of entities, not integrity of an IR snapshot).
- Removing target-registry or feature-boundary revisions, which bind verified
  external target data and are consumed by the discovery gates.

## Prior Art

- **rustc.** In-memory query results and MIR are versioned implicitly by query
  `DepNode`s and the monotonic red-green revision counter; no in-memory
  artifact is hashed between passes. A hash exists where the artifact crosses a
  trust boundary: `StableCrateHash`/SVH over serialized crate metadata, and
  incr-comp fingerprints over serialized on-disk cached query results. ZOM
  should copy this split: cheap in-session identity, hashes at serialization.
- **Swift.** SIL passes operate on in-memory SILFunction values verified by the
  SIL verifier without inter-pass hashing; module tokens and dependency hashes
  exist for serialized swiftmodules and the explicit module/build tracking
  systems. Again, hashing attaches to serialized artifacts.
- **MLIR.** Passes verify in-process structural properties; caching keys are
  provided per-dialect for chosen objects, not whole-module hashes between
  passes. `mlir-opt` reproducer/stability tooling relies on deterministic
  printing, like ZOM's dump parity channel, without content hashes.
- **Cranelift.** `ir::Function` is built through `FunctionBuilder` and checked
  by the `Verifier`; there is no inter-stage hashing. Determinism is tested
  directly.
- **Salsa.** `Revision` is a monotonic integer; durability and changed-at
  tracking replace content comparison. ZOM's query runtime follows this model
  already.
- **Bazel/REAPI and Nix.** Content digests are central precisely because
  artifacts cross process and machine boundaries and are addressed in remote
  caches. This is the case ZOM explicitly excludes for in-memory IR; it is the
  right prior art to revisit if query persistence ever lands.

## Guide-Level Explanation

A contributor should be able to answer one question at each boundary: "What
goes wrong, and what detects it, if the bytes arriving here are not the bytes
that were verified upstream?" Today the answer is a mixture of independent
verifier recomputation, typed capability construction, lease revision checks,
and content hashes. After this RFC the answer is explicit per boundary:

- an independently verified, move-only capability already proves structural
  and semantic well-formedness;
- a typed lease carrying the upstream snapshot's revision proves the
  downstream view is the exact one that was verified, within one session;
- a canonical dump and the corpus IR-parity channel prove determinism across
  builds;
- a content hash over a serialized artifact is required only when bytes leave
  the process or back a cache/store.

The intent is that `LirRevisionId` is not built just because a row in a table
predicts it; it is built when an independent LIR verifier, a session-published
`VerifiedLirModule`, or a persisted LIR artifact creates the integrity claim.
`AlgebraRevision` is either given its first real consumer or folded into the
store's verified publication.

## Reference-Level Design

### Current evidence inventory

Verified against the 2026-09-15 tree. "Serialized artifact" rows are included
because they define the boundary precedent for the in-memory rule.

| Revision | Specified | Implemented | Production consumers |
|---|---|---|---|
| MIR canonical revision (`zom.mir-revision`) | RFC 0010 | Yes, `MirRevisionCodec` | Built MIR verifier recomputation; ownership overlay lineage and borrow-evidence leases; `--emit=mir` display |
| Target spec / registry revisions (`zom.target-spec`, `zom.target-registry`) | RFC 0010 | Yes, `compiler/ir/target/` | Discovery gate, target registry verifier, verified-package-input cross-checks |
| Feature-boundary registry revision (`zom.feature-boundary-registry`) | RFC 0010 | **No**; only an unwrapped `FeatureBoundaryVerificationResult` template with zero non-test consumers | None |
| Borrow module-interface revision (`zom.module-interface-revision`) | RFC 0013 | Yes | Cross-module borrow evidence lineage |
| Checker signature/dispatch/checked-facts revisions | RFC 0015 (LANDED) | Yes (`zom.signature-facts-revision`, `zom.dispatch-facts-revision`, `zom.checked-facts-revision`) | Checker codec closure admission |
| Ownership event-overlay and facts revisions (`zom.ownership-event-overlay`, ownership facts) | RFC 0007/0013 | Yes | Ownership overlay lineage and proof validation |
| Executable-MIR set revision (`zom.executable-mir-set`) | RFC 0021 | **No**; no type, domain, or digest on `VerifiedExecutableMir`; lineage is carried by the MIR + overlay + facts + borrow-evidence revisions | None |
| Link-plan and executable-manifest/publication digests (`zom.link-plan`, `zom.executable-manifest`) | RFC 0043 | Yes; serialized cross-process artifacts | Linker invocation, executable inspector, recoverable publication |
| Error-union layout revision (`zom.error-union-layout`) | RFC 0006 groundwork | Codec + oracle only | None outside its own TUs and oracle test |
| LIR revision (`zom.lir-revision`, `LirRevisionId`) | RFC 0021, with 546-byte oracle | No | None |
| LIR algebra revision (`zom.lir-algebra`) | RFC 0021 store step | Codec + oracle only | None outside its own TUs and oracle test |
| Persisted query reuse | RFC 0017 enum `ReuseClass::Persisted` | Enum only | Zero descriptor rows |

The feature-boundary and executable-MIR-set rows were described as
implemented/partial in the Round-1 draft; that was incorrect and is corrected
here. There is also no separately named marker-registry revision distinct
from the checker revisions listed above.

### Decision options

The review selects one. The options are presented neutrally; Round-1 owners
endorsed option B as safe, but consumers exist for the MIR, ownership,
checker, and external/serialized revisions, so option A is defensible and
option C must justify each removal against the listed consumers.

1. **Retain and extend.** Keep every implemented revision; implement
   `LirRevisionId` only when the independent LIR verifier lands (no such
   phase exists in RFC 0048, whose Phase 4 is structural MIR verification and
   generalized LIR admission) and give `AlgebraRevision` a consumer in the
   same slice; build the feature-boundary registry only if/when feature
   gates exist. This maximizes uniformity.
2. **Re-scope (recommended).** Adopt the written rule:
   - content revisions are retained where they back an in-process lease or
     lineage mismatch check (MIR, ownership overlay, borrow interfaces,
     checker codec closures) or verified external/serialized data (target
     registry, link plan, executable manifest);
   - no new in-memory content revision is specified without naming its
     consumer in the same RFC section;
   - the feature-boundary registry and executable-MIR set revision are marked
     specified-but-unbuilt and are not constructed until a consumer exists;
   - `LirRevisionId` is explicitly deferred until one of: an independent LIR
     verifier, session publication of `VerifiedLirModule`, or a persisted LIR
     artifact exists;
   - `AlgebraRevision` gets one real consumer (LIR store verified publication)
     or is absorbed into that publication's integrity record, keeping its
     oracle meaningful;
   - exact hex preimages remain normative for external/serialized data and
     codec-framing oracles already tested; proposed future preimages (LIR,
     feature registry, executable-MIR set) are removed from normative tables
     until the mechanism is built, and reappear when built;
   - persistence content addressing is reserved to a future RFC that owns an
     on-disk format, at which point Bazel/REAPI-style digest design applies.
3. **Remove at in-process boundaries.** Replace content hashes between
   in-process stages with monotonic snapshot counters plus the independent
   verifiers (which already recompute structure), retaining hashes for
   external/serialized data and future serialization. This is closest to
   rustc/Salsa. It must preserve the lease mismatch checks in
   `compiler/ownership/overlay/drop-elaborated-mir.cc` (revalidating the
   built/overlay/facts/borrow-evidence revisions) with foreign-database and
   stale-revision injection tests, and must prove a counter cannot be aliased
   across databases via the brand and context-fingerprint machinery.

### Relationships

This RFC overlays RFCs 0010, 0013, 0015, and 0021. It does not edit their
text before acceptance. If option B or C is accepted, the normative
replacement follows the repository's supersession/overlay convention, each
affected tracker records the bound proposal hash, and the affected acceptance
criteria and oracle inventories change in the implementing changeset.

## Repository Impact

| Area | Paths | Owner |
|---|---|---|
| IR architecture and MIR codec | `docs/rfc/0010-*`, `compiler/mir/built-mir.{h,cc}` | ir-backend |
| LIR store and unbuilt revisions | `docs/rfc/0021-*`, `compiler/lir/lir-algebra-codec.{h,cc}`, future LIR verifier | ir-backend |
| Borrow and checker lineage revisions | `docs/rfc/0013-*`, `docs/rfc/0015-*` | module-system |
| Overlay and fact revisions for ownership | `compiler/ownership/facts/**`, `compiler/ownership/overlay/**` | runtime-memory |
| Query persistence boundary | `docs/rfc/0017-*`, `compiler/query/query-types.h` | module-system |
| RFC governance and gates | `docs/rfc/README.md`, `scripts/check-rfc.py` | rfc |
| Oracle tests and parity baselines | `tests/unittests/compiler/{mir,lir,ir,ownership}/**`, `tests/coverage/` | verification |

## Security And Safety Impact

Revisions support integrity, not memory safety; the verifiers and capability
constructors enforce safety. Re-scoping must not let an unverified or stale
upstream artifact satisfy a lease: option B keeps the revision on every lease
that currently performs mismatch rejection, and option C must transfer that
guarantee to an unforgeable snapshot counter tied to the context brand and
database identity. External target/registry hashes are out of scope and keep
their supply-chain role.

## Drawbacks And Risks

- Choosing option C weakens a uniform defense-in-depth mechanism and risks
  subtle snapshot aliasing if counter scoping is wrong; the MIR mutation-test
  suite would need redesign, not deletion.
- Choosing option A perpetuates specified-but-unbuilt surface and oracle
  maintenance cost for mechanisms with no consumer, the exact failure the
  project's "remove useless things" principle targets.
- Any change to canonical ordering regenerates oracles; the corpus IR-parity
  channel and the hand-assembled codec framing tests must remain green.
- Deferring `LirRevisionId` must not be read as permission to publish
  unverified LIR; the publication prerequisite is the verifier, not the hash.

## Alternatives Considered

- **Adopt a real persistence layer now and justify all hashes as cache keys.**
  Rejected for this RFC: there is no on-disk design or consumer, and RFC 0010
  deliberately defers one; building it to justify the hashes is backwards.
- **Version the IR with semantic version numbers.** Rejected outright by the
  repository's no-internal-versioning rule; content digests and snapshot
  counters are both unversioned contracts.
- **Hash only on debug/parity builds.** Rejected: lease mismatch rejection is
  a production anti-correlation check and must behave identically in all
  builds.

## Compatibility And Rollout

Internal compiler contract; no user-visible surface, no codec format change
for implemented revisions. Under option B: update the RFC 0021 sections and
tracker to mark `LirRevisionId` deferred, remove the proposed preimage table,
and either consume or absorb `AlgebraRevision` in one code change with its
oracle test updated. Under option C: a staged migration of leases onto
snapshot counters with the mutation tests proving equivalence before any hash
is removed. Every step is gated on the existing oracle and parity tests.
Rollback is a single revert per stage; artifacts are not persisted.

## Documentation And Teaching Plan

Update `docs/design/ir/built-mir.md`, the new LIR and ownership notes, and
RFC 0010/0021 trackers to state the chosen rule; add one contributor paragraph
explaining "verifier proves well-formedness, lease proves exact snapshot, hash
proves external/serialized integrity, dump parity proves determinism".

## Operational Readiness

None beyond CI. No CLI, runtime, release, or performance contract changes
intended; removing SHA-256 work under option C would marginally reduce build
time, but that is not a goal and no performance gate is added.

## Acceptance Criteria

- The selected option is recorded with owner approval and bound into the
  affected RFC trackers.
- Under B: no normative text specifies an in-memory revision without a named
  production consumer; `LirRevisionId` is deferred in RFC 0021 with its
  prerequisite stated; `AlgebraRevision` has a consumer or is absorbed; every
  retained revision's existing test still passes.
- Under C: each removed in-process hash has a migrated lease-integrity test
  that injects a stale or foreign snapshot and proves rejection.
- The MIR, target registry, borrow-interface, and checker codec oracle tests
  and the corpus IR-parity baseline stay byte-green for every implemented
  revision that remains.

## Implementation Plan

1. Resolve the option decision in review and bind it into this RFC.
2. Update the overlaid RFC sections/trackers under the supersession convention.
3. Apply the chosen scope in code and oracle tests in small per-revision
   changes, each behind the parity and mutation-test gates.
4. Refresh the IR design notes in the same change per affected boundary.

## Test Plan

- Build: `cmake --preset sanitizer && cmake --build --preset sanitizer`.
- Unit tests: `ctest --preset default -L unittest` for mir, lir, ir/target,
  ownership overlay lineage mutation, and checker codec suites.
- Lit tests: unchanged; no diagnostic or surface change.
- Conformance: `python3 scripts/check-ir-parity.py --check --ir --zomc
  <built-zomc> --snapshot tests/coverage/corpus-ir-parity.json` for the
  923-source process channel and 64 clean HIR/MIR dumps; the tool requires an
  explicit binary and snapshot and is run through the CI parity wrapper
  rather than as a standalone CTest label.
- Generated files: oracle regenerations listed explicitly in the implementing
  change; hand-assembled codec framing oracles stay byte-identical. Key files:
  `tests/unittests/compiler/lir/lir-algebra-codec-oracle-test.cc`, MIR codec
  oracles in `tests/unittests/compiler/mir/`, and the lineage mutation cases
  in `tests/unittests/compiler/ownership/overlay/`.
- Format: `python3 scripts/check-format.py`; `python3 scripts/check-rfc.py`;
  `python3 scripts/check-no-internal-versioning.py`.

## Open Questions

- If a feature-boundary registry and an executable-MIR set digest are later
  built, which concrete consumer requires them, and do they use the existing
  target/manifest digest domains or a new one?
- Should future persistence (if accepted) use one global artifact digest
  domain or per-store domains, and who owns the canonical codec stability
  policy then?

## Status History

| Date | Status | Notes |
|---|---|---|
| 2026-09-14 | DRAFT | Initial draft from the 2026-09-14 architecture audit. |
| 2026-09-14 | REVIEW | Frozen for required-owner review; tracker and SHA-256 snapshot bound |
| 2026-09-15 | RETURNED | Round 1: feature-boundary and executable-MIR-set rows overstated; inventory omitted serialized-artifact domains; runtime-memory owner and machine-enforceable parity command required. |
| 2026-09-15 | DRAFT | Revised evidence inventory, complete domain list, runtime-memory owner, concrete verification commands. |
| 2026-09-15 | REVIEW | Round 2 frozen after Round 1 revision; tracker and new SHA-256 snapshot bound |
