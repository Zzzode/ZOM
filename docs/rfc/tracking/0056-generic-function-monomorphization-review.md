# RFC 0056 Review And Implementation Tracker

## Discussion Record

RFC 0056 was prompted by starting the T3 backlog wall (generic functions) on
2026-10-28. Re-measurement on HEAD established that the prior T3 diagnosis was
stale:

- Generic declarations and bodies pass `--check`, publish borrow signatures
  with a none return relation for parametric results, and lower to complete
  parametric HIR and post-elaboration executable MIR functions.
- The remaining wall is the call site: `identity(42)` and
  `identity<i32>(42)` drain ZOM4099 at two body-checker shape gates
  (`readCallable` rejects `genericParameters.size() != 0`; `directCallShape`
  rejects non-empty explicit type arguments). Module-scope initializer calls
  are gated earlier by ZOM4104.
- Infrastructure for the producer is already in the tree: `ir::InstanceId`,
  the `Monomorphization = 0x0a` failure phase with ZOM6010/ZOM6011,
  substitution/witness slots on checked call and dispatch facts, the unused
  ground-equality `InferenceContext` engine, and a local substitution walk in
  the borrow classifier.

The RFC selects post-executable-MIR specialization at the reserved 0x0a
position (parametric checking once, then clone-and-substitute per instance),
matching Rust's collection model, and stages from scalar free functions toward
bounds, receivers, generic nominal types, and generic-to-generic composition.

## Owner Reviews

| Owner | Round | Result | Notes |
|---|---|---|---|
| binder-checker | 1 | REQUEST-CHANGES | B1 substitution key domain is GenericParameterKey not DefId; B2 HIR guards are internal incidents, must convert to capability drain atomically; B3 InferenceContext is ground-equality only, ZOM4011 unreachable; M1 conflict -> ZOM4010; M2 ZOM4014 display schema -> Kind::Type; M3 explicit args need shared scalar resolver; M4 module init stays ZOM4104; M5 Ch.12 needs inference rules |
| ir-backend | 1 | REQUEST-CHANGES | B1 instance edges must be emitted at MIR build, mono never rewrites verified roots; B2 new verified instance-set artifact required; B3 ownership bypass needs enforced predicate; M1 identity expansion scoped to 0x0a+ with lease path; M2 full MIR type-carrier walk; M3 source bytes must stay unchanged; M4 canonical instance ordering and pre-ladder CLI branch; m2 ZOM6010 unit-only without g2g; m5 mono dump surface |
| runtime-memory | 1 | REQUEST-CHANGES | B1 triviality predicate enforced by instance verifier, destructor precedent is not a generic precedent; B2 ownership facts stay DefId-keyed, source encoding byte-preserved; B3 instance expansion needs framed injectivity and context validation; B4 ownership resolvers keep documented unused pattern (17 sites unreachable); B5 intern canonical empty witness record; B6 clone post-elaboration template |
| verification | 1 | REQUEST-CHANGES | B1 tag-byte vs byte-identical contradiction; B2 single stage numbering, tests land with producers; B3 specify 0x0a runs inside checkSources; M1 enumerate gate/CMake updates; M2 name parity tool, both snapshots, commands, per-stage flip sets; M3 reject fixture needs three coordinated CMake edits; M4/M5 name concrete suites and gates |
| binder-checker | 2 | REQUEST-CHANGES | B4 empty witness must ride envelope/dispatch; B5 three neg fixtures are module-scope (ZOM4098/ZOM4104), need new in-body fixtures; M6 4098 vs 4099 position matrix, ~23 guard arms auditable; M7 Ch.03 scoping + ZOM4014 schema split; m8 name set vs carrier set; m10 ordinal rendering |
| ir-backend | 2 | REQUEST-CHANGES | B1 keep MirFunction.owner as DefId (option A), separate instance record, joined presentation; B2 no new MirRevisionCodec digest (transitive via checked/dispatch revisions); M1 complete validator comparison enumeration; M2 exclude templates from presented set; m1 self-describing callee slot; m2 empty-witness delivery + edge arity; m3 resolver wording; m4 pre-0x0a failures Definition-owned |
| runtime-memory | 2 | REQUEST-CHANGES | M-1 byte-identity scoped to zero-edge modules; edge modules' chained revisions flip deterministically (incl. reject consumer package); M-2 carrier whitelist enforced on inferred path too and re-checked by verifier; precise zero-reference statement definition; minor citation refresh and lineage evidence |
| verification | 2 | REQUEST-CHANGES | B1 parity needs two executable commands with --zomc/--snapshot; B2 enumerate exact flips (Stage 3 needs new body fixtures); B3 resolve owner/callee gate contradiction (chose option A); B4 add check-compiler-session-architecture.py; M1 dump text stability; minor test path fix |
| spec-audit | 1 | REQUEST-CHANGES | F1 no ZOM6010 normative text without source producer; F2 same for ZOM6011 or add source fixture (chose defer); F3 stage spec sentences per producer (boundary at S2, positive at S3, codegen at S4); F4 bounds paragraph honesty; F5 Ch.03 scoping/terminology and impact row; F6 single ZOM4014 headline; F7 examples scope note |
| binder-checker | 5 | APPROVE | Final stale-paragraph fix verified |
| ir-backend | 4 | APPROVE | Shared helper, HIR witness field, callee framing verified |
| runtime-memory | 5 | APPROVE | Final parity-scope fix verified |
| spec-audit | 4 | APPROVE | Two-layer staged spec text, Ch.03, example follow-up verified |
| verification | 5 | APPROVE | RFC/tracker stage tables synchronized |

## Decision Record

Accepted on 2026-10-28 after all five required owners approved through
adversarial multi-round review (binder-checker round 5, ir-backend round 4,
runtime-memory round 5, spec-audit round 4, verification round 5). The
accepted contract is the four-stage plan in RFC 0056: post-executable-MIR
clone-and-substitute monomorphization keyed by `ir::InstanceId`, call-edge
identity only (DefId owners kept), a verified instance-set artifact with
enforced carrier/triviality predicates, scalar free-function first slice, and
spec text staged sentence by sentence with its producers.

### Round 1-3 design record (historical)

Round 3 revision addressed the round-2 findings:

- Only `MirCallTerminator.callee` becomes `MirCallableIdentity`;
  `MirFunction.owner` and every ownership key stay DefId; instances live in a
  separate verified artifact; presented sets are joined at LIR; the pinned
  validator marker and session gate move with tracked `--self-test` updates.
- No new digest enters `MirRevisionCodec`; edge record bytes are bound
  transitively through checked/dispatch revisions; Source-only modules are
  byte-identical and edge modules' chained revision flips are enumerated and
  determinism-gated.
- The empty witness id is interned and carried on envelope/dispatch; the
  admitted carrier set `{i32, bool}` is gated at checker resolution and
  re-checked by the instance verifier; instance verification additionally
  checks template lineage evidence and edge parameter keys/arity.
- Diagnostics split by position (ZOM4098 statement, ZOM4099 value); new
  in-body corpus fixtures carry ZOM4010/4014/4016/4035 while the existing
  module-scope fixtures stay pinned at ZOM4098/ZOM4104; two new body
  fixtures are the exact Stage 3 parity flips.
- Spec text is staged per producer; Ch.03 is scoped and given an impact row;
  ZOM6010/ZOM6011 get no normative text or source fixtures in this RFC.

## Spec Change Record

### Stage 2 (landed with the checker call-resolution change)

- `docs/spec/chapters/12-generics.md`: new "Call-Site Type Arguments"
  subsection after Generic Functions (resolution layer, four-condition
  ZOM4010/ZOM4014/ZOM4016/ZOM4035 table, ZOM4036 arity sentence, acceptance
  boundary 4099/4098/4104, opening scope sentence); "Bound Satisfaction at
  Call Site" rewritten to declaration-site validation (ZOM4129/ZOM4130) and
  parametric body checking, with call-site satisfaction marked reserved.
- `docs/spec/chapters/03-types.md` touched lines: the overview inference
  bullet (first-order unification -> whole-variable-to-ground binding), the
  Type Inference section opening paragraph plus the new Ch.12 pointer
  sentence, the Local Variable Inference ZOM4014 split (generic-parameter
  meaning now; unresolved local inference variable reserved), and the Call
  row of the Expression Type Rules table (bare-`T` cross-reference).
  Textually unchanged as planned: the Generic overview bullet and the type
  variables paragraph; their code-generation monomorphization claim becomes
  true slice-wise only in Stage 4.
- Position matrix: statement, initializer, return, and condition cells are
  covered in inferred and explicit forms (8 fixtures). The RFC's block-tail
  cell has no source position in the current grammar: a block trailing
  expression without `;` is rejected at parse time with ZOM2025, so no
  checker/HIR matrix cell exists to pin; it joins with block-tail value
  positions whenever tail expressions enter the grammar.

## Implementation Tracker

| Stage | Change | Status | Verification |
|---|---|---|---|
| 1 | General `substituteType` over all 16 branches; shared `TypeSubstitution` type adopted by the borrow classifier (read-only lazy walk kept) | Landed 8de13613 — utility + 15 ztests + borrow alias + checker gate allowlist | 15 ztests green; full build green; type/checked-facts/dispatch suites green; zero behavior change |
| 2 | Substitution codec contract (GenericParameterKey); checker generic-call ground inference, explicit scalar-name resolution, admitted-carrier gate {i32,bool}; ZOM4010/4014/4016/4035 producers; generic callees routed through the existing arity check so wrong count produces ZOM4036 (ZOM4099 on HEAD); literal/coercion precedence identity<u8>(42 narrows then 4099 / 300 -> 4077 / i32 var -> 4009); empty-witness on envelope; checked/dispatch evidence; HIR two fields, stable dump text, position-matrix drain (4099 value/4098 statement) | Landed — codec c598be90; checker call resolution, HIR capability drain, Ch.12/Ch.03 staged spec text, and 16 in-body lit fixtures landed together; carrier gate deferred until after ordinary argument conformance so 4077/4009 win | checker ztest, in-body lit diagnostics (4010/4014/4016/4035/4036 + 4077/4009/u8-4099 precedence) + HIR position matrix + module-scope 4098/4104 pins, HIR golden regen, two parity snapshots (new fixtures only) |
| 3 | MirCallableIdentity on call edge only (DefId owners); Source-preserving callee wire; record-byte provider, no new revision digest; compiler/mono verified instance-set artifact after 0x09 in checkSources; carrier/triviality/lineage/arity/noActivation predicates; framed identity expansion at 0x0a+ | Not started | mono ztest suite incl three seeded statement-kind rejections and cross-build MirRevisionId/overlay/facts revision determinism ztest, ir-failure/built-mir/session tests, ZOM6011 + ZOM6010 component tests, parity flips exactly body_pos_17/18 |
| 4 | Instance-aware CLI/LIR pre-ladder branch; joined Source+instance presented type excluding templates; one shared identity-matching helper at all 31 validator sites; instance symbols; MIR instances dump; generic-identity-consumer exit 42 | Not started | lir/llvm ztest, object-emission case, native run tuple 42, twice-built object-byte package determinism ztest, full sanitizer ctest |
