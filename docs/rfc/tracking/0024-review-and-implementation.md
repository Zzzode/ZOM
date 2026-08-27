# RFC 0024 Review And Implementation Tracker

## Discussion Record

### 2026-07-25 Production Authority Audit

The audit traced package discovery through `CompilerSession::checkSources()`,
`BodyCheckingInput`, `MarkerProofInput`, Built MIR, and the ownership overlay.
It confirmed:

- module graph construction supplies an empty configured-prelude inventory;
- checker startup constructs `MarkerPolicyConfiguration::explicitOnly()`;
- marker proof validates policy lineage but cannot name the semantic roles
  `Copy` and `Linear`;
- RFC 0007 requires those roles from checked input and forbids spelling or
  prelude-name discovery; and
- the overlay lacks checked, HIR, policy, coherence, and marker authority.

Move, initialization, logical-drop, and linear-obligation work would therefore
publish incorrect facts if implemented first. RFC 0024 proposes one
distribution role configuration, real configured-prelude injection, one
verified authority, one complete body input, and fresh producer and verifier
proof contexts.

The first owner-review candidate,
`bf8c23d0f42e7d6d11934c2ca1c875d65083c33509acb2612269a61a6ac1241d`,
was rejected. Review found incomplete distribution bootstrap, configuration
codec, independent promotion, canonical-oracle, revision-domain,
specification-alignment, path-ownership, and native-gate contracts. No
approval from that candidate is retained.

The proposal now defines the source-backed distribution path, atomic resolver
injection, post-identity role construction, exact codecs and oracles,
independent candidate verification, separate resolver and policy revisions,
mandatory Chapters 3 and 14 alignment, exact native tests, and a registered
RFC 0007 architecture gate. It does not authorize implementation until an
exact revised candidate is accepted.

The second candidate,
`f048c7ae034b8def37b91f5abd3e3b9ae81f89bc466eb38e98d3e88319a59606`,
was also rejected. Review found that standard `Copy` semantics were still
implementation-defined, module-graph verification trusted candidate-carried
expectations, RFC 0005 failure precedence was not preserved, the RFC 0004
normative replacement was not part of acceptance, standard-library path
ownership was ambiguous, semantic corpus files had no runner, and stable
manifest plus build/install discovery identities were incomplete. The revised
proposal closes those findings with an exact `Copy` policy and RFC 0015 schema
replacement, verified compiler-marker input, exhaustive failure precedence,
atomic normative synchronization, split path ownership, native session tests,
and one executable-relative distribution layout. The binder-checker and
module-system approvals for the rejected hash are not retained.

The third candidate,
`d5160ff41ff30cc4721605522eacc70e695262b0c94e2df172dab13f77c1a6b1`,
was rejected. Review found that compiler-marker configuration lacked a
complete candidate promotion boundary, the policy admitted unconditional
mutable references without matching evidence, the configuration object field
count was misstated, the checker failure order diverged from RFC 0005,
`ZOM4099` lacked complete node, provenance, ordinal, and suppression
contracts, and the routing and top-level CMake impact were incomplete.
Runtime-memory and spec-audit approvals for that hash are not retained because
the normative proposal changed. The next candidate directly rejects the
unrepresentable mutable rule, defines independent compiler-configuration
promotion, restores exact failure precedence, closes the diagnostic contract,
and makes path ownership exhaustive.

The fourth candidate,
`9d31763ce8081fb8c5279cbc07cb5c8d096b5ebbfdd043679da6eeda06586702`,
was rejected. Review found imprecise and undefined configuration-input types,
an invalid use of checker failures before a checker `ModuleId` exists, no
explicit diagnostics lit pair for `ZOM4099`, and omission of RFC 0007's
normative overlay API from the pre-acceptance synchronization transaction. No
approval is retained. The next candidate uses borrowed production types, a
closed pre-checker failure algebra, an exact registered lit corpus and
expectation pair, and mandatory RFC 0007 normative synchronization.

The fifth candidate,
`56a51ba59cd8f761ee2a6260d14ae9dc4ab9310b6565645e3439126a3d575f3d`,
was approved without objection by all nine required owners. The acceptance
transaction synchronized RFC 0004's module-graph input, RFC 0005's checker and
evidence inputs, RFC 0007's ownership-overlay API, and RFC 0015's policy,
proof, diagnostic, and canonical-vector contracts before changing RFC 0024's
status.

## Owner Review Matrix

| Owner | State | Review Surface |
|---|---|---|
| `rfc` | Approved | Governance, status, scope, prior art, rollout, and exact-hash review |
| `task-router` | Approved | Standard-prelude path ownership and gate routing |
| `binder-checker` | Approved | Configuration, shape and policy validation, body input, proof capability, and failures |
| `module-system` | Approved | Distribution input, configured prelude, graph/query provenance, lifetime, and publication |
| `error-system` | Approved | `ZOM4099` registry, source precedence, anchor, suppression, and publication failure |
| `ir-backend` | Approved | Build-tree and installed executable/resource layout |
| `runtime-memory` | Approved | Copy/Linear roles, overlay boundary, independent proof contexts, and fail-closed behavior |
| `spec-audit` | Approved | RFC 0005/0007/0015 alignment and normative specification drift |
| `verification` | Approved | Native fixtures, oracles, mutation coverage, gates, and full validation |

Every approval must identify the exact RFC SHA-256. Normative edits invalidate
earlier approvals.

## Decision Record

Decision: Accepted.

RFC 0024 is `IMPLEMENTING`. Implementation follows the dependency order below
and preserves the exact accepted contract as the sole internal path.

### 2026-07-25 RFC 0025 Acceptance Synchronization

The RFC 0025 `R25-02` acceptance transaction is authorized by all twelve
required-owner approvals on exact proposal SHA-256
`4f4085c176a9f391115e12170da93af899e350fa92440d5a51577692faf8bad0`.
It retains RFC 0024's marker semantics while replacing its package-backed
distribution, source layout, resolver, bootstrap, publication, and
installation contracts. RFC 0024 remains `IMPLEMENTING`.

| Binding | RFC 0025 Task Authority |
|---|---|
| Acceptance-time RFC synchronization | `R25-02` |
| Unversioned source admission and distribution inventory | `R25-04` |
| ZOM-authored core declarations and contributor boundary | `R25-04A` |
| Inventory generation and installed-consumer fixtures | `R25-05G`, `R25-05I` |
| Core query projections, role seed, and authority materialization | `R25-07` |
| Query and authority mutation evidence | `R25-07T` |
| Signature bootstrap, role authority, and marker-policy consumers | `R25-08` |
| Native bootstrap, authority, and final-interface evidence | `R25-08T` |
| Flat final core interface publication | `R25-09A` |
| Core diagnostics and exact diagnostic evidence | `R25-09C`, `R25-09D` |
| Configured consumer prelude and no-self-edge publication | `R25-11` |
| Architecture and final integrated evidence | `R25-14`, `R25-15` |

The acceptance evidence is the exact 12/12 RFC 0025 approval set. The prior
package manifest, package CLI, two-file distribution, and three-install-file
evidence below remains a historical record only; it does not satisfy any
replacement task in this table.

## Implementation Tracker

| Slice | State | Required Evidence |
|---|---|---|
| RFC contract and owner review | Complete | Exact-hash approval from every required owner |
| Standard prelude source | In progress | Exact source and manifest bytes are packaged and parse/bind as an ordinary package; configured-prelude export and shape-classification evidence remains pending |
| Distribution configuration | In progress | Build/install layout is verified; mandatory prelude target, policy, role keys, codec, and independent oracle remain pending |
| Configured-prelude path | Pending distribution configuration | Incremental transaction, query resolution, graph verification, no self-edge |
| Verified marker authority | Pending configured prelude | Context, shape, policy, owner, role, revision, and mutation evidence |
| Complete body and proof input | Pending marker authority | One aggregate authority and stale-lineage rejection |
| Ownership overlay integration | Pending complete body input | Checked/HIR/MIR/body input, separate engines, atomic failure |
| Marker uses and logical drops | Pending overlay integration | Exact queries, canonical records, independent verifier and codec oracles |
| Production gates and docs | Pending prior slices | Sanitizer, CTest, lit, architecture, format, RFC, policy, trackers, and design docs |

## Verification Evidence

- Live inspection on 2026-07-25 confirmed empty configured-prelude and marker-
  policy production paths.
- RFC 0005, RFC 0007, and RFC 0015 were checked for exact prelude, policy,
  proof-input, and overlay lineage requirements.
- Rust language items and Swift known protocols were reviewed as primary prior
  art for compiler-known roles backed by real library declarations.
- `core/Zom.toml` is exactly 108 bytes with SHA-256
  `3ec3417bca606a7cfbb588b7e177202ade5dcdec48cdff13ba6aea474000ab74`;
  `core/src/prelude.zom` is exactly 52 bytes with SHA-256
  `a05fc153f772f0075ed4c8dd9d8affeecb3f01ea674786047e31778f439833a3`.
- `cmake --preset sanitizer` and
  `cmake --build --preset sanitizer --target zomc` verify configuration and
  target materialization.
- `ctest --preset default -R '^standard-prelude-install-layout$' --output-on-failure`
  verifies the exact installed file set and bytes.
- `build-sanitizer/bin/zomc compile --manifest-path core/Zom.toml --package zomcore --lib --syntax-only`
  verifies parsing and binding of the source-backed package through the package
  CLI.
- The sanitizer build and complete `ctest --preset default --output-on-failure`
  matrix pass all 208 tests.

## Blocking Dependencies

- Production distribution admission through package and module verification.
- The accepted compiler marker configuration and authority as the sole
  semantic path.
