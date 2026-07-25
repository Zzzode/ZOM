# RFC 0025 Review And Implementation Tracker

## Discussion Record

### 2026-07-25 Core Library Architecture Audit

The live audit found a source-backed seed under `products/zomcore`, containing
ordinary ZOM declarations for `Copy` and `Linear`. The seed is packaged as a
normal release with `version = "0.0.0"`, while production
`CompilerSession` still supplies an empty configured-prelude inventory and an
explicit-only marker policy.

The architecture review separates four authorities:

- language primitives owned by the language specification and compiler;
- user-visible core APIs owned by ZOM source;
- closed intrinsic lowering owned by verified compiler IR; and
- platform services owned by a narrow verified runtime ABI.

Rust, Swift, Zig, and Go primary sources were reviewed. The candidate adopts an
allocation-free core boundary, a target-language standard library with a
separate runtime, a toolchain-supplied source module, and a small language
universe. It rejects package-version selection for the mandatory toolchain core
and rejects name-based intrinsic discovery.

The first review candidate,
`09599f7da6b47cdfbc19b98f97fdf32627d105501c787523cbd9887dc37286bd`,
was rejected. Review found that the content digest was embedded in stable unit
identity while the RFC also promised narrow incremental projection reuse, the
proposed `CrateKey` omitted the complete RFC 0011 compilation configuration,
runtime scope was too narrow, role locators were outside authenticated
lineage, the failure algebra was open, and the RFC 0024 replacement transaction
was not mechanical. No approval is retained.

The revised draft separates stable toolchain identity from verified
distribution content, preserves `CompilationConfigKey`, authenticates role
locators, closes failures and diagnostics, fixes prelude cardinality, removes
the unused intrinsic source token, and expands the atomic accepted-RFC
transaction.

The second review candidate,
`1385d71c6095897ac79067f39e16e8b428b67830855e83a5f01c948deb1c94a6`,
was rejected. Review found that its exact root source used multiple module
declarations forbidden by the current grammar, and one context-mismatch case
had incompatible pre-publication and post-publication diagnostic precedence.
The revised draft uses the valid single-declaration root, regenerates every
affected hash and golden vector, and splits input-context from verified-state
failure. No approval from the second candidate is retained.

The third review candidate,
`86492ce69bd60f68e0a697f706f63808fb43911c9b0fd1f97dc945204dbee176`,
was rejected. Governance and specification audits found no high-severity
issue, but production-path review found that the proposed lowering union did
not follow the existing move-only checked-module ownership path, preparatory
host-library dependencies were omitted from the core-edge matrix, the closed
module-search-root union had no toolchain-core alternative, and the role
publication schema named an undefined authority instead of retaining RFC
0024's verified authority. The revised draft uses the live checked-module,
HIR, MIR, ownership-overlay, module-interface-lineage, stable-body-owner, and
standard-marker-authority contracts; it covers the complete RFC 0008 host
closure and defines verified inventory-backed core module discovery. No
approval from the third candidate is retained.

The fourth review candidate,
`0b3212cffda8f8876d4605c1022988a05e4acd43e83a2d2a2fe54f81f0f02d93`,
was rejected. It closed the third candidate's production ownership, host
closure, search-root representation, and authority-type findings, but its
module catalog required frozen source and module identities before parsing and
declaration validation. The revised draft separates a pre-parse structural
`AdmittedCoreSourceCatalog` from the post-validation, post-freeze
`VerifiedCoreModuleCatalog`, so rejected source never requires identity
registry rollback. No approval from the fourth candidate is retained.

The fifth review candidate,
`317a555494ceba8944a8d72330b6aa63020fc5ebba69aa4b2ad840e1bd35dcb7`,
was rejected. Product-path and governance review cleared the two-stage catalog
and prior findings, while specification review found an incomplete
user-package dependency-edge codec, unmapped catalog and lowering failures, an
insufficiently mechanical RFC 0008 replacement transaction, and a duplicate
Mermaid node identifier. The revised draft closes both dependency-origin
encodings, maps every new producer condition into the typed failure algebra,
enumerates the exact RFC 0008 surfaces replaced at acceptance, and assigns
unique bootstrap node identifiers. No approval from the fifth candidate is
retained.

The sixth review candidate,
`c6ef28b5e313be36ba81f5027ec08ed0db345bdf045efce514225774ab45e237`,
was rejected. It closed dependency-edge encoding, RFC 0008 synchronization,
catalog/lowering producer enumeration, and the bootstrap graph, but conflated
source-semantic body rejection with the invariant-only RFC 0010 checked-module,
HIR, MIR, and RFC 0007 ownership result branches. The revised draft gives
those exact identity/IR invariant results their own issue and cause domains,
maps stable owners deterministically, preserves their native fatal
diagnostics, and reserves core-specific verifier disagreement for failures
outside an existing typed result algebra. No approval from the sixth candidate
is retained.

The seventh review candidate,
`835342ed429b93420cfb2444f2c881e9bc09f9680d3f4fbf9553bfbb20b3846b`,
was rejected. It separated source rejection from pipeline invariants and added
the checked-module boundary, but did not partition heterogeneous invariant
owners, conflicted with RFC 0010 invalid-descriptor normalization, and relied
on an unspecified whole-sequence encoding. The revised draft groups IR facts
by expanded stable coordinate, frames normative expanded sort-key bytes,
defines a complete local identity-invariant record, preserves one-step RFC
0010 normalization, and retains RFC 0011 plus codec-first RFC 0010 diagnostic
precedence. No approval from the seventh candidate is retained.

The eighth review candidate,
`edcadc6f3395b71eea077cc490f5e4d8c077b62f79147bd91f6afd7a745d8225`,
was rejected. Product and governance review cleared its grouped pipeline
invariant framing, while specification review found that the live expanded
identity-invariant tag domains were not mechanically synchronized with RFC
0011. The revised draft declares every phase, kind, and API-site enumerator
with its exact tag and adds the RFC 0011 replacement table, including
`DigestCollision` sharing `ZOM9921` with `NonCanonicalEncoding`. No approval
from the eighth candidate is retained.

The ninth technical candidate,
`f81c115f06bdc6f40575e18e828be0c3f30b56370fc6e8d9b00d6d2bb4ef841c`,
received no `BLOCKER` or `MAJOR` from the production-path,
specification-alignment, or governance/unversioned adversarial reviews.
Those scoped clearances authorize promotion to `REVIEW`; they are not the
required-owner approvals for `ACCEPTED`. The status-only promotion creates the
exact review hash
`a9d15b034cf646bc46f2bfed01c4ec1d133afc5fb3edc7c405bb93422457a35c`
that every required owner must cite. No implementation is authorized by this
tracker.

## Owner Review Matrix

| Owner | State | Review Surface |
|---|---|---|
| `rfc` | Pending | Governance, scope, prior art, status, and exact-hash review |
| `task-router` | Pending | Core path ownership and mandatory gate routing |
| `lexer-parser` | Pending | Removal of the unused intrinsic token and grammar-facing inventory alignment |
| `binder-checker` | Pending | Bootstrap signatures, role authority, body checking, and language/core boundary |
| `module-system` | Pending | Unversioned identity, source admission, module graph, queries, and session publication |
| `error-system` | Pending | Closed failures, diagnostics, anchors, ordering, suppression, and CLI behavior |
| `concurrency` | Pending | Removal of unsupported marker and standard-library claims from concurrency design |
| `ir-backend` | Pending | Intrinsic boundary, HIR/MIR consumption, build/install layout, and target capabilities |
| `runtime-memory` | Pending | ZOM source ownership, allocation-free boundary, marker roles, and runtime ABI |
| `tooling-lsp` | Pending | Core definition navigation and semantic source locations |
| `spec-audit` | Pending | RFC synchronization and normative/specification drift |
| `verification` | Pending | Native tests, mutation oracles, installation evidence, architecture gate, and full validation |

Every approval must identify the exact RFC SHA-256. Normative edits invalidate
earlier approvals.

## Decision Record

Decision: Technical design cleared for required-owner review.

RFC 0025 is `REVIEW`. Production implementation must not begin before the RFC
reaches `ACCEPTED`.

## Implementation Tracker

| Slice | State | Required Evidence |
|---|---|---|
| RFC contract and owner review | In progress | Exact-hash approval from every required owner |
| Accepted-RFC synchronization | Pending exact candidate approval | Package-backed core clauses replaced in the atomic acceptance change set |
| Toolchain-core identity | Blocked by acceptance | Exhaustive sum, codecs, oracles, and caller cutover |
| Source inventory, catalog, and admission | Blocked by identity | Build/install parity, search-root codec, independent verifier, and path adversaries |
| ZOM core bootstrap | Blocked by admission | Root/marker/prelude parse, bind, signature, authority, and body evidence |
| Prelude production path | Blocked by bootstrap | Exact target/host projection, one non-core-module edge, and no core self-edge |
| Compiler and runtime consumers | Blocked by prelude | Checked facts, HIR/MIR, target capability, and no duplicate APIs |
| Native gates and documentation | Blocked by prior slices | Sanitizer, CTest, lit, architecture, format, RFC, spec, and design alignment |

## Verification Evidence

- Live inspection confirmed the package-shaped core seed and its two ZOM marker
  declarations.
- Live inspection confirmed empty configured-prelude and explicit-only marker
  setup in the production compiler session.
- RFC 0024 was reviewed for package-release bootstrap, configured-prelude,
  semantic-role authority, policy, proof, and fail-closed requirements.
- Rust, Swift, Zig, and Go primary documentation and source repositories were
  reviewed for the core/source/runtime/compiler boundary.

## Blocking Dependencies

- Exact-hash review by every required owner.
- Atomic synchronization of every accepted RFC named by the proposal.
- No product implementation may start while the acceptance decision remains
  pending.
