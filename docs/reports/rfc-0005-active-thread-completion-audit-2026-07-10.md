# RFC 0005 Active Thread Completion Audit

Date: 2026-07-10

## Scope

This report audits the active type-system follow-up thread against the
user-provided objective that listed ten work areas: RFC status governance,
parser/spec alignment, object safety metadata, operator-to-trait semantics,
error lowering, borrow checking, cross-module sessions, type architecture
cleanup, stronger conformance tests, and documentation closure.

This is not an RFC approval record. It does not change any RFC status, decision,
or approver field.

## Completion Criteria

The thread can be called complete only when every explicit item has either:

- landed code, spec, RFC, and tests that satisfy the requested behavior; or
- a reviewed follow-up RFC that explicitly owns the unfinished work and records
  why the current thread must not implement it.

Passing tests alone is not enough. The evidence must cover the named behavior.

## Prompt-To-Artifact Checklist

| Item | Required Artifact Or Gate | Current Evidence | Status | Gap |
|---|---|---|---|---|
| 1. RFC status governance for RFC 0004 and RFC 0005 | Frontmatter, RFC index, `approvers`, `decision`, status history, `scripts/check-rfc.py` | RFCs 0004 and 0005 are in `REVIEW`, use dedicated tracking records, keep `approvers: []` and `decision: TBD` in proposal frontmatter, and match the index. RFC 0011 is `ACCEPTED` with its recorded decision. RFC 0005 exact-hash review now has every required-owner approval at `ed71e36`; dependent RFCs 0006, 0008, 0009, and 0010 also have complete exact-hash tracker approval at their current proposal bytes. RFC 0004 defines context-checked binding input, deterministic scopes and definitions, verified export surfaces, source-ordered local activation, alias provenance, and whole-fact verification. RFC 0005 separates inference from branded canonical semantic types and defines immutable checked facts. | Not complete | Complete dependency-ordered RFC 0012 and RFC 0004 review and decisions, then record the legal RFC 0005 decision and frontmatter approval set. No dependent implementation may be treated as accepted while its RFC remains in review. |
| 2. Parser/spec alignment for `where`, `dyn I + M`, and `<T as I>::Item` | Parser AST surface, spec text, lit/conformance tests, focused verification | `docs/spec/chapters/09-interfaces.md` states that interface declarations reject trailing `where` with `ZOM2076`; `iface_where_reject_neg_05` AST and diagnostics tests pass. `docs/spec/chapters/17-grammar-reference.md` defines fully qualified and unqualified associated projections plus generic dyn heads. The systematic sweep repaired stale `parser-coverage.yml` entries, added missing coverage for dyn associated bindings and impl members, aligned `ZomParser.g4` with generic dyn heads, unsafe ordinary impls, and impl-member alias rejection, and supplied the missing AST/grammar expectations. `check-parser-coverage.py`, `check-lexer-architecture.py`, and `check-ast-coverage.py` pass. | Complete for the named current parser surface | The wider semantic and diagnostic inventory remains part of items 5 through 9; this row does not claim that the checker or backend is complete. |
| 3. Object safety and interface method metadata | Metadata for superinterfaces, method type parameters, receiver attributes, associated type bindings, static methods, GATs, diagnostics | `DeclSignatureComputer::isDynObjectSafe()` checks superinterface object safety, generic methods, bare `Self` returns, move-self attributes, unbound associated types, static methods, GATs, and unsized parameter/return boundaries. Unit tests and diagnostics conformance cover `ZOM4001` through `ZOM4008` paths. | Complete for RFC 0005 checker gate | Backend vtable layout and call lowering are still separate follow-up work. |
| 4. Operator-to-trait semantics | Trait mapping for arithmetic, equality, ordering, unary, index, and compound-assignment operators; positive and negative conformance | RFC 0005 evidence records simple `Add`, `Sub`, `Mul`, `Div`, `Rem`, `Pow`, `Eq`, `Ord`, `Neg`, `Not`, and `Index` coverage through `body-checker-test.cc` plus diagnostics fixtures. | Partial | `BodyChecker::checkAssignmentExpr()` does not inspect `AssignmentExpr.op`; compound assignments therefore bypass the corresponding binary trait lookup and signature validation. Existing compound-assignment cases only prove AST parsing. Full call lowering also remains owned by RFC 0009. |
| 5. Error lowering and backend contract | RFC 0006, canonical target-aware error-union ABI, panic ABI, backend lowering tests | RFC 0006 is `ACCEPTED`; its accepted implementation has not started, and current `irgen` remains a disposable mixed prototype. The prototype owns explicit ZOM ILP32/LP64 target queries, canonical-key error tags, known/unknown layouts, typed success construction, and deterministic dumping. The checked `?!` slice emits tag branching, payload moves, canonical destination-tag reconstruction, and one shared typed return block. The `!!` slice emits forced-unwrap panic metadata with an exact operator span. `diagnostics-lowering.def` owns `ZOM6001-ZOM6008` capability diagnostics and `ZOM9901-ZOM9903` invariant diagnostics; lowering and dump failures carry closed structural facts. The registered IR diagnostic architecture gate proves exhaustive enum-to-diagnostic mappings and rejects raw assertions, string failure fields, unmapped failure kinds, and missing `.def` entries through four negative fixtures. No `ZC_IREQUIRE` remains in `compiler/irgen`. | Partial | The mixed prototype combines logical error operations with target layout, the shared return block has no drop actions, and locals remain rejected. Multi-residual source unions, general calls/expressions, runtime/target capability integration, unwind, main/task boundaries, named aggregate layouts, cross-module ABI publication, and backend/FFI conformance remain missing. HIR/MIR/LIR verifiers and their full diagnostic matrix are not implemented. |
| 6. Borrow/lifetime/ownership checker | Separate RFC, borrow model, move/loan/region dataflow, diagnostics, driver integration | RFC 0007 is `RETURNED`; its AST-driven checker work is explicitly a disposable pre-acceptance experiment that conflicts with RFC 0010's proposed Built MIR boundary. `borrow-model.h/.cc` and `borrow-model-test.cc` provide a standalone partial model: places, CFG summaries, move facts, loan facts, region checks, use-after-move reports, move-out-of-borrow reports, borrow-conflict reports, region-escape reports, linear-obligation reports, scoped-task capture reports, raw-pointer boundary reports, and related queries. `ZOM4056 UseAfterMove`, `ZOM4057 ValueMovedHere`, and `emitBorrowDiagnostics()` now bridge stored use-after-move reports to diagnostics. `Checker::check()` runs this bridge after clean body checking and trait coherence, with unit coverage for default checker-pipeline emission. `use_after_move_neg_11.check` covers the CLI diagnostics path. `ZOM4070 MoveOutOfBorrow` covers straight-line moves overlapping active loans, `move_out_of_borrow_neg_15.check` covers CLI `ZOM4070` plus the `ZOM4060` borrow-origin note, `move_after_block_borrow_pos_16.check` covers lexical block loan-end precision where a nested-block borrow no longer blocks a later outer move, `move_after_if_borrow_pos_17.check` covers the corresponding `if`-branch block case, `move_after_while_borrow_pos_18.check` covers the corresponding `while`-body block case, `move_after_match_borrow_pos_19.check` covers the corresponding `match`-arm block case, and `move_after_call_borrow_pos_20.check` covers call-argument borrow lifetime ending at the call boundary. `ZOM4058 MutableBorrowConflicts`, `ZOM4059 SharedBorrowConflicts`, `ZOM4060 BorrowOriginHere`, and `emitBorrowConflictDiagnostic()` cover conflict-report diagnostics; phase-level conflict inference now ignores ended loans using the same lexical/call scope filter, `borrow_after_block_borrow_pos_21.check` covers CLI acceptance after a nested-block shared borrow ends before a later mutable borrow, `borrow_after_if_borrow_pos_22.check` covers the corresponding `if`-branch shared-borrow case, `borrow_after_while_borrow_pos_23.check` covers the corresponding `while`-body shared-borrow case, `borrow_after_match_borrow_pos_24.check` covers the corresponding `match`-arm shared-borrow case, and `borrow_after_call_borrow_pos_25.check` covers the corresponding call-argument shared-borrow case. `borrow_conflict_mutable_neg_12.check` covers source `&value` followed by `&mut value`, while `borrow_conflict_shared_neg_13.check` covers `&mut value` followed by `&value` through the CLI diagnostics runner. `ZOM4061 BorrowDoesNotLiveLongEnough`, `ZOM4062 BorrowReferentHere`, and `emitBorrowRegionEscapeDiagnostic()` cover region-escape diagnostics; `BorrowCheckerPhase::run()` now infers source-AST region-escape slices for `return &local`, `return &local.field`, nested local-field projections such as `return &local.field.child`, indexed local projections such as `return &local[index]`, one-hop local reference bindings such as `let ref = &local; return ref;`, bounded alias chains such as `let other = ref; return other;`, simple stored local borrows such as `slot = &local; return slot;`, stored local reference aliases such as `slot = ref; return slot;`, and returned reborrows such as `return &*ref;`, stores `BorrowRegionEscapeReport` values, and maps binder-symbol-rooted places back to AST binding patterns for the `ZOM4062` note. `return_local_reference_escape_neg_14.check`, `return_local_field_reference_escape_neg_26.check`, `return_nested_local_field_reference_escape_neg_27.check`, `return_local_reference_binding_escape_neg_28.check`, `return_local_reference_alias_escape_neg_29.check`, `return_local_index_reference_escape_neg_30.check`, `return_stored_local_reference_escape_neg_31.check`, `return_stored_alias_reference_escape_neg_32.check`, `return_reborrow_local_reference_escape_neg_33.check`, `return_nested_block_local_reference_escape_neg_34.check`, `return_if_branch_local_reference_escape_neg_35.check`, `return_else_branch_local_reference_escape_neg_38.check`, `return_while_body_local_reference_escape_neg_36.check`, and `return_match_arm_local_reference_escape_neg_37.check` cover CLI `ZOM4061` plus the `ZOM4062` referent note. `ZOM4063 LinearNotConsumed`, `ZOM4064 LinearInitializedHere`, `ZOM4065 LinearConsumedTwice`, `ZOM4066 LinearFirstConsumedHere`, `emitBorrowMissingConsumeDiagnostic()`, and `emitBorrowDoubleConsumeDiagnostic()` cover unit-level linear diagnostics. `ZOM4067 ScopedTaskBorrowEscapes`, `ZOM4068 ScopedTaskReferentHere`, and `emitBorrowScopedTaskCaptureDiagnostic()` cover unit-level scoped-task capture diagnostics. `ZOM4069 RawPointerBoundaryRequiresUnsafe` and `emitBorrowRawPointerBoundaryDiagnostic()` cover raw-pointer safe-boundary diagnostics; `BorrowCheckerPhase::run()` now infers the first typed-AST boundary slice for raw-pointer `*ptr` dereference outside `unsafe {}`, including `let` initializers, suppresses it inside unsafe blocks, and emits inferred reports through `emitBorrowDiagnostics()`. `BodyChecker` preserves the pointee type for reference/raw-pointer dereference so the borrow phase owns the safe-boundary diagnostic. `raw_pointer_deref_requires_unsafe_neg_39.check` covers CLI `ZOM4069` in a `let` initializer, `raw_pointer_deref_expression_requires_unsafe_neg_41.check` covers CLI `ZOM4069` in an expression statement, `raw_pointer_deref_assignment_requires_unsafe_neg_42.check` covers CLI `ZOM4069` in an assignment RHS, `raw_pointer_deref_call_arg_requires_unsafe_neg_43.check` covers CLI `ZOM4069` in a call argument, `raw_pointer_deref_return_requires_unsafe_neg_44.check` covers CLI `ZOM4069` in a return value, `raw_pointer_deref_binary_requires_unsafe_neg_45.check` covers CLI `ZOM4069` in a binary operand, `raw_pointer_deref_conditional_requires_unsafe_neg_46.check` covers CLI `ZOM4069` in a conditional branch, `raw_pointer_deref_index_requires_unsafe_neg_47.check` covers CLI `ZOM4069` in an index operand, and `raw_pointer_deref_unsafe_pos_40.check` covers CLI acceptance under `unsafe {}`. | Not complete | Marker-driven Copy/Linear classification is incomplete, general source-AST region inference beyond direct return borrows, bounded local reference alias chains, simple local-borrow stores, and returned reborrows is missing, lifetime-end inference and reborrow restoration are missing beyond the first lexical block, `if`-branch, `while`-body, `match`-arm, and call-argument loan-end slices, real linear type/fact inference is missing, trusted task API recognition is missing, raw-pointer cast/FFI/unsafe-call/packed-field/marker-sensitive boundary inference is missing, reborrow/richer-temporary/path-sensitive branch-join loan lifetime inference is missing for move-out and conflict precision, and remaining borrow diagnostics are not fully wired into checker-pipeline fact inference. |
| 7. Cross-module `CompilerSession` | RFC 0008, session owner, module graph, module interfaces, visibility, global coherence index | RFC 0008 is `IMPLEMENTING` with a concrete tracker. `CompilerSession` directly replaces `CompilerDriver` in the driver library, CLI, and tests; it owns one process-unique `SemanticContextBrand` and the sole RFC 0011 registry family for that context. The architecture gate proves the exact driver surface, unique process-root factory, sole registry-family claim path, and one frontend scheduler; seven negative fixtures reject old-driver, alias, wrapper, second-scheduler, missing-registry-owner, second-factory, and raw-session-assertion violations. The accepted design defines `PackageId -> CrateId -> ModuleId -> DefId/ImplId`, verified immutable interfaces, context-checked signatures, rejection of all import cycles, source-session dependencies without metadata stubs, and one session scheduler. | Not complete | `ModuleGraph`, `VerifiedModuleInterface`, `SignatureStore`, dependency scheduling, checked-facts publication, and the global coherence index are not implemented. Current frontend maps remain `BufferId`-keyed, semantic consumers still use table-local `SymbolId`, and the import resolver still contains unsound ownership and identity surfaces. The gate must expand to foreign semantic-store bypasses when those stores exist, and full multi-module, same-package multi-target vertical slices remain required. |
| 8. Type architecture cleanup | Canonical `TypeId -> TypeData` ownership, nominal symbol identity, immutable checked types, reduced helper coupling, no stale compatibility paths | `docs/reports/zom-type-architecture-audit-2026-07-10.md` records ten adversarially reviewed findings. This thread repaired dangling concrete inference bindings, concrete generic impl specialization collisions, generic-argument loss, and silently ignored complex local annotations. The RFC drafts now separate `VerifiedSignatureFacts` from body-complete `VerifiedCheckedFacts`. | Not complete | `TypeId` remains unbranded and multi-issuer; nominal and impl identity remains spelling-based; `TypeEnv` still mixes mutable inference with duplicated publishable facts; coherence is single-AST; scope identity is address-derived; the polymorphic type graph conflicts with zc container rules. RFC 0011/0005/0008 must replace these atomically. |
| 9. Stronger `.zom` conformance | User-visible diagnostics and positive behavior in grammar, AST, diagnostics, IR, and execution suites | After deleting the unsupported macro surface and adding the named-parameter drift regression, the current coverage gate reports 861 corpus inputs, 865 AST expectations, 794 grammar verdicts, 68 explicit AST-only checks, and 4 explicit extras. The full validation passes 55 unit tests, 794 ANTLR grammar verdicts, 865 AST lit tests, 207 diagnostics lit tests, and 5 IR lit tests. Module declarations preserve root, inline-root, alias, and exported-alias forms and reject a single-segment alias in both parsers. Constructor, destructor, class-constant, and method-raises roles survive into exact AST producers and checker signatures. Dyn/object-safety coverage is broad, open primitive match cases have CLI evidence, and executable IR snapshots cover tag-zero construction, one-residual `?!` tag remapping, forced-unwrap panic metadata, structured unsupported-shape and output-path diagnostics, and pre-lowering unwind rejection. | Partial | Compound assignments lack semantic diagnostics; union-match and union-coercion negative matrices are incomplete; RFC 0007 lacks linear/scoped-task/reborrow CLI coverage and a translated corpus; RFC 0006 still lacks real cleanup, multi-residual, enabled-unwind, backend, and FFI matrices. `ImportCallExpression` and `NewExpression` also lack complete checker semantics. |
| 10. Documentation status closure | RFC/spec alignment audit, explicit current parser and compiler architecture status, no misleading claims | The named parser surface is aligned across Chapter 17, ANTLR, recursive parser, AST snapshots, grammar verdicts, and diagnostics. The duplicate `docs/design/syntax-ebnf.md` authority was deleted. RFC 0011 owns `SemanticContextBrand` and the `PackageId -> CrateId -> ModuleId -> DefId/ImplId` hierarchy; RFC 0008 is `IMPLEMENTING` and owns module discovery plus `CompilerSession`; RFC 0012 owns the package and resolver design. Normative chapters without implementation were removed. Chapters 13, 22, and 23 describe current observable behavior. The recursive parser preserves every accepted module form and declaration role. `docs/design/architecture.md` records the live `CompilerSession`/AST/Binder/Checker/mixed-irgen pipeline and separates the implemented identity-owner cutover from the absent module graph, HIR/MIR/LIR, LLVM, binary emission, and native artifacts. | Partial | RFC implementation closure remains incomplete. The architecture, identity, module, type, IR, and spec documents must continue to track each completed implementation slice without projecting accepted design as live behavior. |

## Systematic Spec-Alignment Evidence

The automated sweep found and repaired drift that focused feature tests had not
exposed:

- `parser-coverage.yml` lacked `DynAssocBinding`, `DynAssocBindingArgs`, and
  `ImplMember`, and retained productions that no longer exist in Chapter 17.
  The coverage map now matches all 230 syntactic and 35 lexical
  productions.
- `ZomParser.g4` rejected generic dynamic interface heads accepted by the
  recursive parser and documented by `DynType ::= 'dyn' InterfaceType ...`.
  It now accepts both `dyn Box<i32>` and
  `dyn Iterator<i32><Item = u8>`.
- The ANTLR grammar accepted aliases inside standalone impl bodies even though
  the EBNF and recursive parser reject them. The extra alternative was deleted.
- The ANTLR grammar lacked the `unsafe` prefix for ordinary interface impls.
  Marker and ordinary impl alternatives now use complementary leading gates,
  and `unsafe impl Interface for Type` is accepted.
- Fifty-seven existing corpus cases had grammar verdicts but no AST snapshots;
  six AST cases had no grammar verdict. The missing artifacts were generated or
  authored and reviewed by category.
- The focused dyn/projection diagnostic run exposed that
  `BodyChecker::resolveTypeExpr()` discarded generic arguments from local
  annotations even though the AST and `DeclSignatureComputer` retained them.
  This made `Box<Good>` and `Box<Unsafe>` both appear as `Box`, bypassing the
  generic marker-impl matcher. Body checking now preserves generic arguments
  and the resolved nominal symbol; a unit regression checks the stored
  `Box<Good>` type.

Verification for this slice:

- `python3 scripts/check-parser-coverage.py` passed: 230 syntactic and 35
  lexical productions.
- `python3 scripts/check-lexer-architecture.py` passed.
- `python3 tests/conformance/tools/check-ast-coverage.py`
  passed: 857 corpus inputs, 861 AST expectations, 790 grammar verdicts, 68
  explicit AST-only checks, and 4 explicit extras.
- The ANTLR grammar runner passed both generic dyn cases and all 40
  `09-interfaces` cases.
- The registered grammar/AST/diagnostics subset passed 125/125 CTest cases.
- The named `where`, dynamic-interface marker, generic-head, associated-binding,
  inherited-binding, and associated-projection subset passed 32/32 registered
  AST and diagnostics tests after the generic-annotation repair.
- `cmake --preset sanitizer` and `cmake --build --preset sanitizer -j 8`
  passed after registering the new snapshots.

## 2026-07-11 AST Surface And Spec-Alignment Evidence

The live compiler had retained a macro AST and grammar surface without a macro
expander, binder consumer, checker consumer, or executable semantic contract.
The empty macro nodes, parser branches, grammar productions, conformance
inventory, identity kind, and unused diagnostic placeholder file were deleted.
The AST generator now reports 134 concrete variants. Empty conditional and FFI
diagnostic placeholder files were also deleted because they were not included
registries and allocated no diagnostics.

The same audit repaired normative examples and productions that the recursive
parser or ANTLR grammar rejected: single-line comments now admit apostrophes;
Chapter 6 uses struct literals, valid struct field modifiers, ordinary interface
methods, and `Self::` associated-type projections; Chapter 9 uses valid impl
methods, a non-keyword static method name, type-parameter GAT syntax, and only
accepted positive generic bounds; Chapter 17 now matches impl members, generic
parameter and argument commas, `do-while`, classic and iterator `for` forms,
statement-list items, marker impls, and labeled statements.

The sanitizer parser test exposed four stale unit expectations plus one real
recursive-parser drift. The stale tests required interface fields, a `pub val`
member, and parameterless syntax without `()` for destructors. They now test
the accepted interface and destructor forms. A new negative unit contract first
proved that the recursive parser accepted `do-while` without its required
semicolon; `parseDoWhileStatement()` now rejects that form like the ANTLR
grammar and Chapter 17.

Current verification for this slice:

- sanitizer configure and build passed;
- all 55 sanitizer unit tests passed;
- all 790 ANTLR grammar verdicts passed;
- all 861 AST lit tests, 207 diagnostics lit tests, and 5 IR lit tests passed;
- AST code generation passed with 134 variants;
- parser coverage passed for 230 syntactic and 35 lexical productions;
- AST coverage passed for 857 corpus inputs, 861 AST expectations, 790 grammar
  verdicts, 68 explicit AST-only checks, and 4 explicit extras;
- lexer architecture, RFC structure, format, and diff checks passed.

This evidence closes the current macro-placeholder and Chapter 6/9/17 drift
slice. It does not complete RFC 0006, RFC 0007, RFC 0008, the type architecture
replacement, or the RFC 0004/0005 governance sequence.

## 2026-07-11 IR Diagnostic Boundary And RFC 0011 Entry Evidence

The mixed `irgen` prototype now returns closed typed lowering and dump-verifier
failure facts instead of display strings. User-correctable capability failures
map to registered `ZOM6001-ZOM6008` diagnostics. Missing checked facts,
malformed IR, and impossible lowering states map to registered
`ZOM9901-ZOM9903` invariant diagnostics. `ZOM9901` preserves lowering phase,
failure kind, and AST node; `ZOM9903` preserves verifier site, failure kind,
symbol, block, value, type, and index. The driver switches exhaustively over
the internal enums, and no `ZC_IREQUIRE`, ad hoc diagnostic print, throw, or
assertion path remains under `compiler/irgen`.

The same verification cycle found and repaired two parser-boundary defects.
The recursive parser now ends an inline module at its matching closing brace,
so a following top-level declaration is preserved. Labels now target only a
loop, block, or nested label in both parsers. The ANTLR grammar expresses that
contract as a structural `labelTarget` rule rather than a throwing semantic
predicate; this prevents `where F: Sendable + Linear` lookahead from being
misclassified as a labeled statement. New `.zom` regressions cover labeled
mutable declarations, labeled expression statements, outer attributes after a
label, and a declaration following an inline module.

Every required RFC 0011 owner approved the `DRAFT -> REVIEW` entry gate after
those repairs. At this entry-gate checkpoint RFC 0011 was `REVIEW` with empty
`approvers` and a `TBD` decision. That transition did not accept the proposal or
authorize implementation; the later acceptance record below supersedes this
checkpoint status.

Current verification for this slice:

- sanitizer configure and incremental build passed;
- all 55 sanitizer unit tests passed;
- all 793 ANTLR grammar verdicts passed;
- all 864 AST lit tests, 207 diagnostics lit tests, and 5 IR lit tests passed;
- AST code generation passed with 134 variants;
- parser coverage passed for 231 syntactic and 35 lexical productions;
- AST coverage passed for 860 corpus inputs, 864 AST expectations, 793 grammar
  verdicts, 68 explicit AST-only checks, and 4 explicit extras;
- lexer architecture, RFC structure, format, and diff checks passed.

This evidence closed the diagnostic-boundary, label, inline-module, and RFC
0011 entry-governance slices. At that point it did not accept RFC 0011,
implement HIR/MIR/LIR, or complete the broader active thread; the separate
acceptance evidence follows.

## 2026-07-11 RFC 0011 Acceptance Evidence

The separate acceptance review returned concrete blockers rather than reusing
the entry-gate approvals. The repaired RFC now defines exact scalar domains,
NFC source-name construction, SemVer and feature identity, idempotent and
credential-free `https`/`ssh` URL canonicalization, complete build-script host
dependency closure, and deterministic Unicode redeclaration diagnostics.
Producerless builtin marker and synthetic-definition identities were deleted.
Marker identity now refers to a real interface definition, while primitive
types remain semantic-type-store entities. RFC 0009 was moved from `REVIEW` to
`RETURNED` because its name, `SymbolId`, `TypeId`, AST impl-node, early vtable
slot, and `ErrorTarget` contract conflicts with the accepted identity and
verified-handoff design.

The parser/spec repair separated named declaration parameters from unnamed
structural function-type components. `fun f(i32, str)` is rejected by both
parsers and has grammar plus AST evidence; `(i32, str) -> bool` remains a valid
function type. The grammar inventory and its README now agree at 39 Chapter 6
cases and 794 total verdicts.

All nine required RFC 0011 owners independently approved the repaired final
revision. The proposal, tracker, and RFC index now record `ACCEPTED`, the exact
approver set, and a real decision. `implementation` remains `TBD`; acceptance
does not claim that `compiler/identity`, the architecture gate, or any dependent
replacement exists.

Final verification for this slice:

- sanitizer and debug configuration plus builds passed;
- all 1178 registered tests passed, including 55 unit, 794 grammar, 865 AST,
  207 diagnostics, and 5 IR tests;
- AST coverage passed for 861 corpus inputs, 865 AST expectations, 794 grammar
  verdicts, 68 explicit AST-only checks, and 4 explicit extras;
- parser coverage passed for 233 syntactic and 35 lexical productions;
- AST code generation passed with 134 variants;
- RFC, format, and diff checks passed.

This closes only the RFC 0011 design-governance dependency. RFC 0012 and RFC
0004 remain the next active reviews; RFC 0005, RFC 0008, RFC 0010, and all
implementation requirements remain incomplete.

## Incremental Package, Module, and Visibility Evidence

The cross-module design review exposed a dependency cycle between package
identity, binder definitions, module interfaces, semantic types, and IR. RFC
0011 now owns the shared identity foundation, while RFC 0004, RFC 0005, RFC
0008, and RFC 0010 consume it without redefining local handle shapes.

The normative module chapters were reduced to the implemented language surface,
while unimplemented architecture moved back behind RFC review:

- Chapter 13 defines only accepted module declarations, `::` paths,
  import/export grammar, and the actual registered module diagnostics.
- Chapter 16 now defines only the executable outer-attribute, parameter,
  marker-path, and marker-impl surface. Proc-macro expansion, marker
  declarations, post-bind AST injection, LSP contracts, and unimplemented
  metadata tiers were deleted. Unsupported type-member, enum-variant, and
  module-declaration attribute targets fail with registered `ZOM2089` instead
  of silently losing the parsed metadata.
- Chapter 22 now matches the live single-compilation coherence pass: local
  interface/self declarations, exact duplicates, and blanket overlap.
- Chapter 23 defines source-level `export`, retained member-visibility facts,
  and the current absence of access enforcement. Source-level `open`, `sealed`,
  and `final` declaration modifiers are rejected.
- Chapters 20, 21, 24, 25, and 26 were removed from the normative order because
  their edition, manifest, resolver, module-discovery, and prelude systems do
  not exist.
- RFC 0008 now contains the proposed complete structural module-resolution key,
  deterministic source-candidate rules, fixed-point discovery, and identity
  freeze. RFC 0012 contains the proposed manifest, package graph, deterministic
  resolver, lock graph, secure source materialization, and build-script
  boundary. Both remain `DRAFT` and non-normative.

The source implementation enforces the parser context boundary for visibility.
The recursive parser and ANTLR grammar reject `private`, `protected`, and
`public` on a module-level declaration through registered `ZOM2088`. The
binder materializes default private facts for class members and default public
facts for interface members. Member lookup does not yet enforce private or
protected access. The unused `ClassExtensibility` schema enum and fixed
`extensibility=Sealed` payload fields were deleted from the AST, generated
factories, parser, fixtures, and regenerated snapshots.

Sanitizer verification found and repaired two test-infrastructure defects
instead of accepting stale evidence:

- `regen-lit.py` preferred an old debug compiler over the mandatory sanitizer
  compiler, which generated an accepting snapshot for a source the current
  parser rejected. Default discovery now selects `build-sanitizer` first, and
  the regenerated snapshot checks `ZOM2088` with an expected-failure RUN line.
- the declaration-collector fixture destroyed its AST before tests inspected
  scope and symbol names borrowed from the tree arena. `TestFixture` now
  retains the tree for those post-pass assertions, eliminating the sanitizer
  use-after-free.

Verification for this slice:

- `cmake --preset sanitizer` and `cmake --build --preset sanitizer -j 8`
  passed.
- focused declaration collector, parser recovery, parser, and symbol tests
  passed 6/6 under ASan and UBSan.
- RFC, parser-coverage, lexer-architecture, and AST-coverage gates passed.
- At the time of this slice, AST coverage reported 870 corpus inputs, 874 AST expectations, 802 grammar
  verdicts, 68 explicit AST-only checks, and 4 explicit extras.

## Incremental Module Declaration Preservation Evidence

The recursive parser no longer truncates accepted module declarations to a
single path. `ModuleDeclaration` now stores the exact
`RootDeclaration | InlineRoot | Alias` form, declared name, optional alias
target, inline `StatementListItem` nodes, and exported-alias state. An exported
module alias is classified as the leading source module rather than as a
top-level `ExportDeclaration` statement. The old `path` payload and every
caller were deleted.

Verification for this slice:

- the sanitizer build passed after regenerating the AST schema support files;
- at the time of this slice, all 261 parser unit tests passed, including exact module-form, declaration-role,
  method-raises, and single-segment-alias assertions;
- all 52 registered Chapter 13 AST conformance tests passed;
- all 51 Chapter 13 ANTLR grammar verdicts passed;
- parser coverage then passed for 225 syntactic and 35 lexical productions;
- AST coverage then passed for 870 corpus inputs, 874 AST expectations, 802 grammar
  verdicts, 68 explicit AST-only checks, and 4 explicit extras;
- the RFC checker, format gate, and `git diff --check` passed.

## Incremental RFC 0006 Evidence

The error-lowering foundation moved from a host-layout prototype to executable,
target-aware success-construction and one-residual propagation slices:

- `compiler/irgen/target-data-layout.*` owns the ZOM ILP32 and LP64 pointer and
  scalar queries used by layout computation; no compiler-host `sizeof` value
  participates.
- `compiler/irgen/error-union-layout.*` recursively flattens error unions,
  reserves tag zero for success, deduplicates and sorts residual alternatives
  by canonical key, handles direct success, and reports unresolved payload
  layouts as unknown.
- `compiler/irgen/ir.*`, `lowering.*`, and `ir-dump.*` define typed integer
  constants, symbolic same-source raising calls, error-union construction and
  payload moves, blocks, tag branches, jumps with block arguments, return
  terminators, and a deterministic textual form. Binder symbols identify
  functions; frozen checker dispatch identifies calls; raw local symbol ids are
  never serialized.
- Direct `return callee()?!;` lowers when the source union has exactly one
  concrete residual. Success and error paths reconstruct the enclosing ABI
  union and jump to one typed return block. The regression uses callee
  `raises str` and caller `raises bool | str`, proving source `str` tag one is
  rebuilt as destination tag two instead of forwarding an ABI-incompatible
  source union.
- The propagation return block is only an empty cleanup convergence point.
  Functions with locals are rejected until checker-owned drop facts and real
  destructor targets reach IR. Multi-residual sources, external/method calls,
  missing or unfrozen dispatch, unknown layouts, and unsupported shapes are
  fail-closed.
- Compiler options default to abort. `--panic unwind` is recognized and then
  rejected after checking but before final emission; the expected-failure IR
  test proves that the command emits no IR header.
- Direct `return callee()!!;` now branches on the source error union, moves and
  reconstructs the success value, and emits a forced-unwrap panic terminator
  carrying payload type plus file, line, column, and exact two-byte operator
  span. This is still a mixed prototype terminator; it does not yet call the
  runtime panic ABI or borrow the payload through the proposed MIR contract.
- The lowering diagnostic boundary now uses a closed `LoweringFailureKind`,
  `LoweringPhase`, and AST node instead of display strings. `diagnostics-lowering.def` registers
  `ZOM6001-ZOM6008` for current capability failures and `ZOM9901-ZOM9903` for
  missing checked input plus lowering and dump invariants. `zomc` owns the
  exhaustive fact-to-diagnostic mapping and source location selection, and
  preserves the lowering phase/failure kind or dump failure kind in the
  registered invariant message.
- A triggerability audit replaced the private target-layout, layout-table, and
  resolved-call-dump assertions with closed target/width types, checked table
  access, and a typed pre-output dump failure. No `ZC_IREQUIRE` remains in
  `compiler/irgen`. Output-path exceptions become `ZOM6008`, and checked but
  unsupported return/propagation/unwrap type shapes become `ZOM6002` instead
  of `ZOM9901`. The invalid-output IR test checks that no uncaught exception,
  stack trace, or raw emission-failure summary escapes.
- Sanitizer verification exposed undefined behavior in advancing an invalid
  `SourceLoc`. `getAdvancedLoc()` now preserves invalid locations, and the
  diagnostic test uses a managed in-memory buffer instead of manufacturing an
  external null location.
- `zomc compile --emit ir` now runs this checked single-source lowering path.
  The dedicated IR lit runner rejects empty/orphan expectation sets and checks
  `error_union_construct_pos_15.zom` and
  `error_propagate_lowering_pos_16.zom` against target/layout/IR snapshots, and
  `panic_unwind_capability_pos_17.zom` against the pre-lowering rejection
  contract. Stable snapshots contain no AST node identifiers or raw local
  symbol ids.

Verification for this slice:

- `cmake --preset sanitizer` and `cmake --build --preset sanitizer`
  passed.
- `lowering-test` covers success construction, direct success, unknown-layout
  rejection, unfrozen dispatch, invalid propagation operands, multi-residual
  rejection, unresolved call targets, deterministic dumping, same-source
  symbol resolution, shared return convergence, and residual tag remapping.
- The focused registered diagnostic, lowering, and IR slice passed 7/7 under
  the sanitizer build.
- The IR conformance label now owns five executable tests, including the
  unsupported-shape diagnostic and expected-failure unwind capability gates.
- `check-ast-coverage.py`, `check-parser-coverage.py`,
  `check-lexer-architecture.py`, `check-rfc.py`, `check-format.py`, and
  `git diff --check` passed.

## Incremental IR Architecture Evidence

The mixed IR prototype exposed a missing repository-wide layer contract. RFC
0010 now defines three proposed ZOM IR layers without claiming they exist:

- a verified checked-module handoff consumes complete frozen semantic facts;
- semantic HIR preserves canonical meaning without target layout;
- Built MIR contains every semantic exit and logical drop before path-sensitive
  ownership analysis;
- proof-carrying drop and coroutine elaboration produce executable MIR;
- target LIR owns SSA, storage layout, function ABI, monomorphization, runtime
  symbols, and total LLVM translation;
- user/capability failures require registered diagnostics, while malformed IR
  and missing verified facts require structured `ZOM99xx` invariant reports.

The RFC has a real local review and implementation tracker. Initial RFC,
IR-backend, and spec-audit reviews returned blocking governance, identity,
phase-order, LIR, monomorphization, and drift findings. A revised proposal
resolved those findings, but a later Chapter 21 cross-check found that
`ModuleId { PackageId, ModuleIndex }` collides across multiple crate targets in
one package. RFC 0010 was returned and is now `DRAFT` after adding the missing
`CrateId` level. `approvers` remains empty, `decision` remains `TBD`, and no
acceptance is claimed.

The root agent routing now includes an `ir-backend` owner for the current
prototype and any accepted future IR/backend implementation. The subagent
contract distinguishes present prototype gates from post-acceptance HIR/MIR/LIR
gates.

`docs/design/architecture.md` describes only live code. It records the
`CompilerSession` root, per-buffer AST/binding/type maps, RFC 0011 context and
registry ownership, current checker, limited mixed `irgen`, typed lowering
diagnostic boundary, incomplete cross-module architecture, absent LLVM/backend,
and unsupported binary emission.

Verification for this slice:

- `python3 scripts/check-rfc.py` passed for 10 proposal RFCs.
- `git diff --check` passed.
- The latest dependency review superseded the earlier no-P0 review result with
  a package/crate/module identity blocker and returned RFC 0010.

## Incremental RFC 0007 Evidence

The raw-pointer safe-boundary slice now also covers null-coalescing,
type-test, array literal, tuple literal, cast, object literal, and struct
literal operands, plus member expression receivers:
`BorrowCheckerPhase.ReportsRawPointerDerefInNullCoalesceExpression` exercises
`NullCoalesceExpr` traversal in `borrow-model-test.cc`, and
`raw_pointer_deref_null_coalesce_requires_unsafe_neg_48.check` covers CLI
`ZOM4069` for `let value = *ptr ?? fallback;`.
`BodyChecker.IsExprChecksOperand` verifies that `IsExpression` operands publish
their nested expression types into `TypeEnv`,
`BorrowCheckerPhase.ReportsRawPointerDerefInIsExpression` exercises borrow-phase
traversal for type-test operands, and
`raw_pointer_deref_is_requires_unsafe_neg_49.check` covers CLI `ZOM4069` for
`let value = *ptr is i32;`.
`BorrowCheckerPhase.ReportsRawPointerDerefInArrayLiteral` exercises array
element traversal, and
`raw_pointer_deref_array_literal_requires_unsafe_neg_50.check` covers CLI
`ZOM4069` for `let values = [*ptr];`.
`BorrowCheckerPhase.ReportsRawPointerDerefInTupleLiteral` exercises tuple
element traversal, and
`raw_pointer_deref_tuple_literal_requires_unsafe_neg_51.check` covers CLI
`ZOM4069` for `let values = (*ptr, 1);`.
`BorrowCheckerPhase.ReportsRawPointerDerefInCastExpression` exercises cast
operand traversal, and `raw_pointer_deref_cast_requires_unsafe_neg_52.check`
covers CLI `ZOM4069` for `let value = (*ptr) as i32;`.
`BorrowCheckerPhase.ReportsRawPointerDerefInObjectLiteral` exercises object
property value traversal, and
`raw_pointer_deref_object_literal_requires_unsafe_neg_53.check` covers CLI
`ZOM4069` for `let value = { item: (*ptr) };`.
`BorrowCheckerPhase.ReportsRawPointerDerefInStructLiteral` exercises struct
field value traversal, and
`raw_pointer_deref_struct_literal_requires_unsafe_neg_54.check` covers CLI
`ZOM4069` for `let value = Point { x: (*ptr) };`.
`BorrowCheckerPhase.ReportsRawPointerDerefInMemberExpressionObject` exercises
member receiver traversal, and `raw_pointer_deref_member_requires_unsafe_neg_55.check`
covers CLI `ZOM4069` for `let value = { field: (*ptr) }.field;`.

## Incremental RFC 0007 Region Evidence

The region-escape slice now covers object-member and struct-member compound
return expressions, aggregate object/struct/array returns, conditional return
expressions, cast-wrapper return expressions, and null-coalescing return
expressions, error-default return expressions, index-expression returns, and
call-expression returns, new-expression returns, and import-call returns:
`BorrowCheckerPhase.ReportsReturningObjectMemberReferenceToLocal` traces
`return { item: (&value) }.item;` through the object literal property value,
and `return_object_member_local_reference_escape_neg_39.check` covers CLI
`ZOM4061` with the `ZOM4062` referent note for that returned object-member
reference. The same object-like property trace also covers
`return Box { item: (&value) }.item;`, with CLI evidence in
`return_struct_member_local_reference_escape_neg_40.check`, direct aggregate
object returns such as `return { item: (&value) };`, with CLI evidence in
`return_object_literal_local_reference_escape_neg_42.check`, direct aggregate
struct returns such as `return Box { item: (&value) };`, with CLI evidence in
`return_struct_literal_local_reference_escape_neg_41.check`, and direct
aggregate array returns such as `return [(&value)];`, with CLI evidence in
`return_array_literal_local_reference_escape_neg_43.check`.
`BorrowCheckerPhase.ReportsReturningConditionalReferenceToLocal` traces
`return flag ? (&value) : (&value);` through both conditional branches, and
`return_conditional_local_reference_escape_neg_44.check` covers CLI `ZOM4061`
with the `ZOM4062` referent note for that returned conditional expression.
`BorrowCheckerPhase.ReportsReturningCastReferenceToLocal` traces
`return ((&value) as &i32);` through the cast operand, and
`return_cast_local_reference_escape_neg_45.check` covers CLI `ZOM4061` with
the `ZOM4062` referent note for that returned cast expression.
`BorrowCheckerPhase.ReportsReturningNullCoalesceReferenceToLocal` traces
`return (&value) ?? (&value);` through primary and fallback values, and
`return_null_coalesce_local_reference_escape_neg_46.check` covers CLI `ZOM4061`
with the `ZOM4062` referent note for that returned null-coalescing expression.
`BorrowCheckerPhase.ReportsReturningErrorDefaultReferenceToLocal` traces
`return (&value) ?: (&value);` through primary and fallback values, and
`return_error_default_local_reference_escape_neg_49.check` covers CLI
`ZOM4061` with the `ZOM4062` referent note for that returned error-default
expression.
`BorrowCheckerPhase.ReportsReturningIndexReferenceToLocal` traces
`return [(&value)][0];` through the indexed object and index expression, and
`return_index_local_reference_escape_neg_47.check` covers CLI `ZOM4061` with
the `ZOM4062` referent note for that returned index expression.
`BorrowCheckerPhase.ReportsReturningCallReferenceToLocal` conservatively traces
`return id(&value);` through call arguments, and
`return_call_local_reference_escape_neg_48.check` covers CLI `ZOM4061` with the
`ZOM4062` referent note for that returned call expression.
`BorrowCheckerPhase.ReportsReturningNewExpressionReferenceToLocal`
conservatively traces `return new Box(&value);` through allocation arguments,
and `return_new_local_reference_escape_neg_50.check` covers CLI `ZOM4061` with
the `ZOM4062` referent note for that returned new expression.
`BorrowCheckerPhase.ReportsReturningImportCallReferenceToLocal`
conservatively traces `return import("x", &value);` through import-call
arguments, and `return_import_call_local_reference_escape_neg_51.check` covers
CLI `ZOM4061` with the `ZOM4062` referent note for that returned import-call
expression.

## Incremental Checker Conformance Evidence

The match-exhaustiveness CLI suite now also covers open primitive scrutinee
types that require a wildcard arm. `match_open_i32_non_exhaustive_neg_11.check`
covers user-visible `ZOM4022` for a `match` over `i32` with only finite literal
arms, mirroring the existing unit coverage in
`Exhaustiveness.OpenTypeI32WithoutWildcardReportsError`.

## Verification Performed

- Final `cmake --preset sanitizer` and `cmake --build --preset sanitizer`
  passed after refreshing the registered conformance inventory and timeout
  budgets.
- Final `ctest --test-dir build-sanitizer -j 8 --output-on-failure -E
  '^conformance-grammar$'` passed 1183/1183 tests in 368.13 seconds.
- Final `ctest --test-dir build-sanitizer --output-on-failure -R
  '^conformance-grammar$'` passed the complete grammar suite in 1097.73
  seconds. That unchanged grammar covered the prior 800-case inventory; the
  added exported-module-alias verdict then passed both independently and in the
  51/51 Chapter 13 run. The later macro-surface deletion produced the current
  790-case inventory.
- The regenerated dynamic-type AST snapshot set plus the two function-type
  diagnostic replacements passed 38/38 registered AST and diagnostics tests.
- `python3 scripts/check-rfc.py` passed for 11 proposal RFCs;
  `check-parser-coverage.py` passed for 230 syntactic and 35 lexical
  productions; `check-lexer-architecture.py` passed for UCD 15.1.0 with 660
  identifier-start and 769 identifier-part ranges; and
  `check-ast-coverage.py` passed for 857 corpus inputs, 861 AST expectations,
  790 grammar verdicts, 68 explicit AST-only checks, and 4 explicit extras.
- Final `python3 scripts/check-format.py` and `git diff --check` passed.
- `cmake --build --preset sanitizer -j` passed after adding the null-coalescing
  raw-pointer traversal.
- `ctest --test-dir build-sanitizer -R 'borrow-model-test' --output-on-failure`
  passed.
- `lit tests/conformance/expectations/diagnostics/04-expressions/raw_pointer_deref_null_coalesce_requires_unsafe_neg_48.check -v`
  passed: 1/1.
- `cmake --preset sanitizer` passed and registered the new conformance test.
- `ctest --test-dir build-sanitizer -R 'raw_pointer_deref_(null_coalesce_requires_unsafe_neg_48|conditional_requires_unsafe_neg_46|binary_requires_unsafe_neg_45|return_requires_unsafe_neg_44|call_arg_requires_unsafe_neg_43|assignment_requires_unsafe_neg_42|expression_requires_unsafe_neg_41|index_requires_unsafe_neg_47|requires_unsafe_neg_39|unsafe_pos_40)' --output-on-failure`
  passed: 10/10.
- `ctest --test-dir build-sanitizer -R 'body-checker-test|borrow-model-test' --output-on-failure`
  passed: 2/2 after adding `IsExpression` operand checking.
- `lit tests/conformance/expectations/diagnostics/04-expressions/raw_pointer_deref_is_requires_unsafe_neg_49.check -v`
  passed: 1/1.
- `ctest --test-dir build-sanitizer -R 'raw_pointer_deref_(is_requires_unsafe_neg_49|null_coalesce_requires_unsafe_neg_48|conditional_requires_unsafe_neg_46|binary_requires_unsafe_neg_45|return_requires_unsafe_neg_44|call_arg_requires_unsafe_neg_43|assignment_requires_unsafe_neg_42|expression_requires_unsafe_neg_41|index_requires_unsafe_neg_47|requires_unsafe_neg_39|unsafe_pos_40)' --output-on-failure`
  passed: 11/11.
- `lit tests/conformance/expectations/diagnostics/04-expressions/raw_pointer_deref_array_literal_requires_unsafe_neg_50.check -v`
  passed: 1/1.
- `ctest --test-dir build-sanitizer -R 'raw_pointer_deref_(array_literal_requires_unsafe_neg_50|is_requires_unsafe_neg_49|null_coalesce_requires_unsafe_neg_48|conditional_requires_unsafe_neg_46|binary_requires_unsafe_neg_45|return_requires_unsafe_neg_44|call_arg_requires_unsafe_neg_43|assignment_requires_unsafe_neg_42|expression_requires_unsafe_neg_41|index_requires_unsafe_neg_47|requires_unsafe_neg_39|unsafe_pos_40)' --output-on-failure`
  passed: 12/12.
- `lit tests/conformance/expectations/diagnostics/04-expressions/raw_pointer_deref_tuple_literal_requires_unsafe_neg_51.check -v`
  passed: 1/1.
- `ctest --test-dir build-sanitizer -R 'raw_pointer_deref_(tuple_literal_requires_unsafe_neg_51|array_literal_requires_unsafe_neg_50|is_requires_unsafe_neg_49|null_coalesce_requires_unsafe_neg_48|conditional_requires_unsafe_neg_46|binary_requires_unsafe_neg_45|return_requires_unsafe_neg_44|call_arg_requires_unsafe_neg_43|assignment_requires_unsafe_neg_42|expression_requires_unsafe_neg_41|index_requires_unsafe_neg_47|requires_unsafe_neg_39|unsafe_pos_40)' --output-on-failure`
  passed: 13/13.
- `lit tests/conformance/expectations/diagnostics/04-expressions/raw_pointer_deref_cast_requires_unsafe_neg_52.check -v`
  passed: 1/1.
- `ctest --test-dir build-sanitizer -R 'raw_pointer_deref_(cast_requires_unsafe_neg_52|tuple_literal_requires_unsafe_neg_51|array_literal_requires_unsafe_neg_50|is_requires_unsafe_neg_49|null_coalesce_requires_unsafe_neg_48|conditional_requires_unsafe_neg_46|binary_requires_unsafe_neg_45|return_requires_unsafe_neg_44|call_arg_requires_unsafe_neg_43|assignment_requires_unsafe_neg_42|expression_requires_unsafe_neg_41|index_requires_unsafe_neg_47|requires_unsafe_neg_39|unsafe_pos_40)' --output-on-failure`
  passed: 14/14.
- `lit tests/conformance/expectations/diagnostics/04-expressions/raw_pointer_deref_object_literal_requires_unsafe_neg_53.check -v`
  passed: 1/1.
- `ctest --test-dir build-sanitizer -R 'raw_pointer_deref_(object_literal_requires_unsafe_neg_53|cast_requires_unsafe_neg_52|tuple_literal_requires_unsafe_neg_51|array_literal_requires_unsafe_neg_50|is_requires_unsafe_neg_49|null_coalesce_requires_unsafe_neg_48|conditional_requires_unsafe_neg_46|binary_requires_unsafe_neg_45|return_requires_unsafe_neg_44|call_arg_requires_unsafe_neg_43|assignment_requires_unsafe_neg_42|expression_requires_unsafe_neg_41|index_requires_unsafe_neg_47|requires_unsafe_neg_39|unsafe_pos_40)' --output-on-failure`
  passed: 15/15.
- `lit tests/conformance/expectations/diagnostics/04-expressions/raw_pointer_deref_struct_literal_requires_unsafe_neg_54.check -v`
  passed: 1/1.
- `ctest --test-dir build-sanitizer -R 'raw_pointer_deref_(struct_literal_requires_unsafe_neg_54|object_literal_requires_unsafe_neg_53|cast_requires_unsafe_neg_52|tuple_literal_requires_unsafe_neg_51|array_literal_requires_unsafe_neg_50|is_requires_unsafe_neg_49|null_coalesce_requires_unsafe_neg_48|conditional_requires_unsafe_neg_46|binary_requires_unsafe_neg_45|return_requires_unsafe_neg_44|call_arg_requires_unsafe_neg_43|assignment_requires_unsafe_neg_42|expression_requires_unsafe_neg_41|index_requires_unsafe_neg_47|requires_unsafe_neg_39|unsafe_pos_40)' --output-on-failure`
  passed: 16/16.
- `lit tests/conformance/expectations/diagnostics/04-expressions/raw_pointer_deref_member_requires_unsafe_neg_55.check -v`
  passed: 1/1.
- `ctest --test-dir build-sanitizer -R 'raw_pointer_deref_(member_requires_unsafe_neg_55|struct_literal_requires_unsafe_neg_54|object_literal_requires_unsafe_neg_53|cast_requires_unsafe_neg_52|tuple_literal_requires_unsafe_neg_51|array_literal_requires_unsafe_neg_50|is_requires_unsafe_neg_49|null_coalesce_requires_unsafe_neg_48|conditional_requires_unsafe_neg_46|binary_requires_unsafe_neg_45|return_requires_unsafe_neg_44|call_arg_requires_unsafe_neg_43|assignment_requires_unsafe_neg_42|expression_requires_unsafe_neg_41|index_requires_unsafe_neg_47|requires_unsafe_neg_39|unsafe_pos_40)' --output-on-failure`
  passed: 17/17.
- `lit tests/conformance/expectations/diagnostics/05-statements/return_object_member_local_reference_escape_neg_39.check -v`
  passed: 1/1.
- `ctest --test-dir build-sanitizer -R 'return_(object_member_local_reference_escape_neg_39|local_reference_escape_neg_14|local_field_reference_escape_neg_26|nested_local_field_reference_escape_neg_27|local_reference_binding_escape_neg_28|local_reference_alias_escape_neg_29|local_index_reference_escape_neg_30|stored_local_reference_escape_neg_31|stored_alias_reference_escape_neg_32|reborrow_local_reference_escape_neg_33|nested_block_local_reference_escape_neg_34|if_branch_local_reference_escape_neg_35|else_branch_local_reference_escape_neg_38|while_body_local_reference_escape_neg_36|match_arm_local_reference_escape_neg_37)' --output-on-failure`
  passed: 15/15.
- `lit tests/conformance/expectations/diagnostics/05-statements/return_struct_member_local_reference_escape_neg_40.check -v`
  passed: 1/1.
- `lit tests/conformance/expectations/diagnostics/05-statements/return_struct_literal_local_reference_escape_neg_41.check -v`
  passed: 1/1.
- `lit tests/conformance/expectations/diagnostics/05-statements/return_object_literal_local_reference_escape_neg_42.check -v`
  passed: 1/1.
- `lit tests/conformance/expectations/diagnostics/05-statements/return_array_literal_local_reference_escape_neg_43.check -v`
  passed: 1/1.
- `lit tests/conformance/expectations/diagnostics/05-statements/return_conditional_local_reference_escape_neg_44.check -v`
  passed: 1/1.
- `lit tests/conformance/expectations/diagnostics/05-statements/return_cast_local_reference_escape_neg_45.check -v`
  passed: 1/1.
- `lit tests/conformance/expectations/diagnostics/05-statements/return_null_coalesce_local_reference_escape_neg_46.check -v`
  passed: 1/1.
- `lit tests/conformance/expectations/diagnostics/05-statements/return_error_default_local_reference_escape_neg_49.check -v`
  passed: 1/1.
- `lit tests/conformance/expectations/diagnostics/05-statements/return_index_local_reference_escape_neg_47.check -v`
  passed: 1/1.
- `lit tests/conformance/expectations/diagnostics/05-statements/return_call_local_reference_escape_neg_48.check -v`
  passed: 1/1.
- `lit tests/conformance/expectations/diagnostics/05-statements/return_new_local_reference_escape_neg_50.check -v`
  passed: 1/1.
- `lit tests/conformance/expectations/diagnostics/05-statements/return_import_call_local_reference_escape_neg_51.check -v`
  passed: 1/1.
- `cmake --preset sanitizer` passed and registered
  `return_conditional_local_reference_escape_neg_44` and
  `return_cast_local_reference_escape_neg_45`, and
  `return_null_coalesce_local_reference_escape_neg_46`, and
  `return_error_default_local_reference_escape_neg_49`, and
  `return_index_local_reference_escape_neg_47`, and
  `return_call_local_reference_escape_neg_48`, and
  `return_new_local_reference_escape_neg_50`, and
  `return_import_call_local_reference_escape_neg_51`.
- `ctest --test-dir build-sanitizer -R 'return_(import_call_local_reference_escape_neg_51|new_local_reference_escape_neg_50|error_default_local_reference_escape_neg_49|call_local_reference_escape_neg_48|index_local_reference_escape_neg_47|null_coalesce_local_reference_escape_neg_46|cast_local_reference_escape_neg_45|conditional_local_reference_escape_neg_44|array_literal_local_reference_escape_neg_43|object_literal_local_reference_escape_neg_42|struct_literal_local_reference_escape_neg_41|struct_member_local_reference_escape_neg_40|object_member_local_reference_escape_neg_39|local_reference_escape_neg_14|local_field_reference_escape_neg_26|nested_local_field_reference_escape_neg_27|local_reference_binding_escape_neg_28|local_reference_alias_escape_neg_29|local_index_reference_escape_neg_30|stored_local_reference_escape_neg_31|stored_alias_reference_escape_neg_32|reborrow_local_reference_escape_neg_33|nested_block_local_reference_escape_neg_34|if_branch_local_reference_escape_neg_35|else_branch_local_reference_escape_neg_38|while_body_local_reference_escape_neg_36|match_arm_local_reference_escape_neg_37)' --output-on-failure`
  passed: 27/27.
- `ctest --test-dir build-sanitizer -R 'lit-09-interfaces-iface_where_reject_neg_05|diagnostics-09-interfaces-iface_where_reject_neg_05' --output-on-failure`
  passed: 2/2.
- `lit tests/conformance/expectations/diagnostics/05-statements/match_open_i32_non_exhaustive_neg_11.check -v`
  passed: 1/1.
- `ctest --test-dir build-sanitizer -R 'match_(open_i32_non_exhaustive_neg_11|non_exhaustive_bool_neg_01|non_exhaustive_enum_neg_05|guard_non_bool_neg_08|guarded_wildcard_non_exhaustive_neg_09|wildcard_unreachable_warn_10|duplicate_arm_warn_04|exhaustive_bool_pos_02|exhaustive_enum_pos_06)' --output-on-failure`
  passed: 17/17.
- `python3 scripts/check-rfc.py` passed: 11 proposal RFCs.
- `python3 scripts/check-format.py` passed for changed files.
- `git diff --check` passed.

Earlier focused verification in this thread also passed `parser-test` and the
dyn lit subset covering generic dyn heads, associated type bindings, marker
suffixes, and their combinations.

## Incremental Type Architecture Evidence

The multi-expert type-architecture audit is recorded in
`docs/reports/zom-type-architecture-audit-2026-07-10.md`. Refutation separated
direct behavior and safety defects from cross-module architecture blockers:

- `TypeEnv` previously stored caller-owned concrete type addresses during
  binding and unification. Generic call instantiation could destroy the owner
  while leaving the environment binding live. Borrowed inputs are now cloned
  into environment-owned storage, with sanitizer regressions that resolve only
  after the source value has been destroyed.
- the trait resolver's lossy `typeName::interfaceName` cache allowed a concrete
  impl for `Box<Good>` to match `Box<Plain>`. The cache was deleted, marker keys
  include the complete current specialization, and a unit regression proves
  specialization isolation.
- body type-expression resolution discarded generic arguments and omitted
  accepted tuple, array, function, intersection, object, and bottom annotation
  forms. It now preserves resolved nominal symbols and arguments, resolves the
  accepted complex forms, and rejects a boolean initializer for a tuple
  annotation. Two conformance cases that had treated function types as an
  unsupported resolver gap were replaced: casting `i32` to a function type now
  reports `ZOM4013`, and explicitly instantiating a generic with a function
  type before passing `i32` now reports `ZOM4009`.
- RFC 0005 now publishes `VerifiedSignatureFacts` through a dedicated verifier
  before body checking. RFC 0008 consumes that proof for signature-first module
  interface publication; body-complete `VerifiedCheckedFacts` records the exact
  signature revision. This closes the prior phase-order contradiction without
  claiming implementation exists.

The audit remains open because local unbranded `TypeId`, spelling-based nominal
identity, mixed mutable `TypeEnv` facts, per-AST coherence, address-derived
scope identity, and the polymorphic owned type graph require the atomic RFC
0011/0005/0008 replacement.

## Incremental Conditional-Surface Reconciliation Evidence

The former Chapter 19 described an evaluator, AST stripping, feature
environment, file selection, options, and diagnostics that did not exist in
the compiler. The chapter, specialized AST variants, ANTLR predicate grammar,
and shell-only conformance cases were deleted. Exact `zom::cfg` use now fails
with registered `ZOM2090`; the recursive parser, ANTLR oracle, Chapter 16, and
the AST/grammar negative case agree. The duplicate
`docs/design/syntax-ebnf.md` authority was also deleted, leaving normative
Chapter 17 as the grammar reference.

The first complete grammar run exposed one contradictory positive case that
still allowed an attribute on a module declaration. That case was deleted
because `attr_on_module_decl_reject_neg_17` already owns the rejection
contract. The remaining module subset now passes 50/50; combined with the other
751 passing cases, the inventory then passed 802/802. The unsupported macro
surface was subsequently deleted. Four parser-boundary regressions added in the
later alignment and identity acceptance slices bring the current inventory to
794/794 passing.

## 2026-07-11 Result Identity And IR Failure Ownership Evidence

The error-model review found a three-document identity conflict. Chapter 6
defined `Result<T, E>` both as a nominal enum and as a transparent union alias,
while the concurrency design and RFC 0006 inferred error roles from the alias.
The aligned contract is now explicit:

- the identifier `Result` has no intrinsic compiler meaning;
- the `Result<T, E>` used by the specification examples is a nominal enum and
  is represented by RFC 0005 `TypeData::Nominal`;
- ordinary unions and nominal enums do not publish `ErrorUnionShapeFact`;
- a raising signature stores success and raises separately, and a raising call
  publishes the canonical union value plus checked success/residual roles;
- RFC 0006 consumes those roles only through RFC 0009 dispatch and RFC 0010
  verified MIR; its target layout retains value, success, and residual keys plus
  the checked-facts and dispatch revisions.

The IR diagnostic audit also distinguishes internal failure facts from public
diagnostics. `LoweringFailureKind` and `IrDumpFailureKind` correctly remain
closed IR-layer enums rather than duplicating `.def` diagnostic IDs. The `.def`
registry remains the sole owner of numeric code, severity, headline, and arity.
The current prototype still mixes capability and invariant variants in one
`LoweringResult` and maps them in the `zomc` CLI. RFC 0010 therefore requires a
compiler-owned typed adapter with distinct `CapabilityRejected`,
`IdentityInvariantRejected`, and `IrInvariantRejected` results, mandatory
failure owner/site data, and generated exhaustive mapping and producer tests.
The current enum-to-`ZOM6001-ZOM6004`/`ZOM9901`/`ZOM9903` switch is valid
prototype containment, not evidence that the final failure boundary has landed.

The exact current proposal hashes are RFC 0005
`ed71e363082d35c7738fc3f529a619f70645262cd2987e17e1bb82f9f71b14a4`,
RFC 0006
`aea15335a11da7d59a579d713abfb30267d72a8043f90988dfb598a8cfb06bda`,
RFC 0008
`3f77e6d785611f8f828d0fc6865606898af8e6a0e22b628b279bc4823aca691d`,
RFC 0009
`b51775967f3df96b692b4620ad083125a4dab21e14ce2a2b652c8bd1f72a5d9d`,
and RFC 0010
`857ce8361c684b9a7e10e9ccca66d6a4d01e415a0619b0c049ad5648bc745d87`.
`check-rfc.py`, `check-format.py`, and `git diff --check` pass on the current
forced-cast response. Exact-hash owner approvals for the preceding coordinated
set are superseded. No current required-owner approval, acceptance decision, or
implementation authorization is recorded.

## 2026-07-11 Five-Way Type-Surface And Feature-Gate Evidence

Formal semantic review exposed two parser/specification drifts that the earlier
focused matrix did not cover. Function-type parameter clauses are positional
type lists in Chapter 3, Chapter 17, ANTLR, and the AST schema, but the recursive
parser silently skipped a leading `name:`. The recursive parser now rejects
that form, the positive fixture uses `<T>(T) -> T`, and a dedicated negative
fixture checks both the recursive parser and ANTLR. Empty `()` is the unit tuple
form in the specification and recursive parser, so ANTLR now has an explicit
empty-tuple alternative and a dedicated positive grammar fixture.

The same review found that RFC 0006 described a source-rejecting,
target-dependent FFI verifier as a target-selection gate even though RFC 0010
had no source branch there and no proof token required by LIR. RFC 0010 now
owns one generic `FeatureBoundaryVerificationResult` seam,
`VerifiedTargetSelection`, and an exact `VerifiedFeatureBoundarySet` required
by target lowering. RFC 0006 specializes that seam; its proof revision binds
the checked-facts revision, executable MIR revision, and `TargetSpecId`. The
new 181-byte oracle hashes to
`9f5ac18311f9aba4af2e66107a55d80f9444ac66714ac8d9fb127d1735637b35`.
Source or invariant rejection publishes neither facts nor a gate proof.

Formal invariant review then found that the proof oracle omitted the
`Executable = 0x04` portion of `MirRevisionId`, while `TargetSpecId` and the
gate registry still lacked canonical codecs. The corrected FFI proof oracle is
181 bytes. RFC 0010 now fixes the complete target profile encoding with a
111-byte oracle, uses non-zero big-endian `uint32` gate IDs, fingerprints the
generated registry with a 49-byte oracle, binds common proof identity fields,
and fixes multi-gate source rejection order. The three independently
recomputed hashes are respectively
`9f5ac18311f9aba4af2e66107a55d80f9444ac66714ac8d9fb127d1735637b35`,
`6d72a26055117cb6e84c3cc3a72fd4c1e42caf861138d8f84f5bf34f2f244d37`,
and `7a71e0d8b4b07a804e7aa46ac05ace15ac96d88b6b6d536b61b5814e496a9b13`.

The final target-identity review removed RFC 0006's duplicate target strings
and panic/object tag assignments. Error-union descriptors and target-artifact
manifests now bind only RFC 0010's canonical `TargetSpecId`; the matching
`VerifiedTargetSelection` provides the profile used for layout. The v2
descriptor oracle is 423 bytes and independently hashes to
`0960aace205395c9ec049c04e7e1509d6945c72f128fce221771a23adbf98fdb`.
`VerifiedTargetSelection` now owns the immutable canonical target profile and
its recomputed ID, closing the data path from selection through FFI layout and
LIR without a reverse digest lookup or unverified parallel record.

Runtime-memory review then closed the unwind lifetime boundary. A borrowed
`PanicInfoView` is valid only during a panic entry call; unwind eagerly copies
source, message, bounded residual summary, and backtrace frames into an opaque
runtime-owned record before any frame is destroyed. Catch transfers one owning
handle, inspection borrows only from that handle, and explicit exact-once drop
or uncaught/second-panic runtime destruction releases the record. Routing
review also assigned `compiler/ir/**` to `ir-backend`, corrected error-system
paths, and made `runtime-memory` the sole Chapter 14 owner while concurrency
retains Chapter 15.

The cast surface now retains all three intended modes: guaranteed `as T`,
optional checked `as? T`, and forced checked `as! T`. The earlier forced-cast
rejection contradicted RFC 0002 and the centralized parser mode mapping. The
ANTLR grammar, recursive parser, generated AST schema, checker result typing,
Chapters 3, 4, and 17, RFC 0005, and positive conformance now agree. RFC 0006's
Repository Impact also no longer assigns checker files to `error-system`;
checker/type ownership remains exclusively with `binder-checker`, while
`error-system` owns diagnostics and Chapter 11.

Current evidence is:

- sanitizer configure and build pass;
- parser coverage, lexer architecture, and generated AST schema pass;
- focused ANTLR function-type matrix passes 6/6 and the empty-tuple matrix
  passes 1/1;
- focused AST/diagnostic/specification CTest passes 54/54 across `where`, dyn
  marker and associated-binding forms, qualified and unqualified associated
  projections, function-type label rejection, and empty tuple acceptance;
- the forced-cast restoration passes the 175/175 ANTLR expression matrix,
  parser and body-checker unit tests, exact forced-cast and cast-versus-power
  AST snapshots, and a real `--emit dispatch` checker path;
- the pre-restoration complete serial ANTLR matrix passes in 483.47 seconds;
  the current matrix's unchanged inventory passes in 736.87 seconds, and the
  two subsequently added/replaced cast and function-type fixtures pass focused
  ANTLR runs;
- the current partitioned CTest inventory excluding grammar and the socket HTTP
  test ran 1,181 tests: 1,180 passed and one stale positive function-type
  binding-pattern fixture failed. That unsupported fixture was directly
  replaced with a negative grammar/AST contract; its focused test and the AST
  coverage gate now pass;
- the current `http-http-socketpair-test` passes five consecutive isolated
  repetitions;
- the pre-restoration unpartitioned default-preset rerun passes 1,179/1,179 in
  599.20 seconds, including the socket HTTP test and complete grammar runner;
- current coverage reports 865 corpus inputs, 869 AST expectations, 799 grammar
  verdicts, 67 explicit AST-only checks, and 4 explicit extras;
- RFC, parser coverage, lexer architecture, AST coverage, schema generation,
  format, and `git diff --check` gates pass on the current bytes.

One earlier unmodified default-preset attempt stopped after 48 scheduled tests because
`http-http-socketpair-test` missed its internal two-second connection-count
window while the serial ANTLR matrix and other tests ran concurrently. The
same binary passed 6/6 independent repetitions, and all other 1,177 tests
passed under eight-way CTest concurrency. No timeout or zc HTTP implementation
was changed without a stable reproducer. The subsequent unpartitioned rerun
passed 1,179/1,179. The current unpartitioned attempt reproduced the same
socketpair timing failure and, because the preset stops scheduling after a
failure, completed only 39 already-started tests while the full grammar runner
passed. All current inventory is covered by the partitioned and focused runs,
but one clean unpartitioned current-byte run remains required before landing.

This closes the corrected focused five-way drift, not the complete active
thread. Exact-hash required-owner review now approves RFCs 0005, 0006, 0008,
0009, and 0010, but dependency decisions remain open and the clean
unpartitioned current-byte sanitizer CTest remains a final landing gate.

## Result

The active thread is not complete.

The currently closed slice is the named parser/spec surface: v1 interface
`where` rejection, dynamic interface markers and generic heads, qualified and
unqualified associated projections, unsafe ordinary impl syntax, impl-member
alias rejection, parser-production inventory, and AST/grammar artifact parity.

The remaining blockers are real and cannot be replaced by proxy signals:

- RFC 0011 is `ACCEPTED` with every required owner and a recorded decision, but
  its implementation has not started. RFCs 0005, 0006, 0008, 0009, and 0010
  are in `REVIEW` with empty proposal-frontmatter approvers and `decision: TBD`.
  Their trackers now record complete exact-hash owner approval, but legal
  dependency-ordered decisions and frontmatter writeback remain open. The
  identity foundation must be implemented before dependent replacements can
  land.
- RFC 0006 now has canonical target-aware layout, typed success construction,
  and a one-residual `?!` path with destination tag remapping and a shared
  return convergence block. It still needs real cleanup/drop actions,
  multi-residual switching, general calls, the runtime call and payload-borrow
  contract for the existing `!!` panic terminator, runtime/target capability
  integration, unwind, main/task boundaries, and FFI enforcement.
- RFC 0007 needs call region summaries instead of conservative argument
  tracing, path-sensitive reaching definitions and lifetimes, AST-derived
  reborrow restoration, temporary/closure/store inference, real Copy/Linear
  marker facts, scoped-task recognition, and full CLI/corpus coverage.
- RFC 0008 needs the actual `CompilerSession`, isolated module scopes,
  immutable interfaces, signature store, cross-module type lookup, and global
  coherence implementation.
- RFC 0005 needs a canonical `TypeId -> TypeData` rewrite, nominal symbol
  identity, immutable checked types, helper ownership convergence, zc-rule
  compliance, and compound-assignment checker semantics.

## Post-publication updates

### 2026-07-11 RFC 0011 implementation start and preset correction

RFC 0011 legally transitioned from `ACCEPTED` to `IMPLEMENTING`. The first
implemented slice adds `compiler/identity` build wiring, process-root semantic
context brand issuance, one registry-brand issuer claim per context, shared
thread-safe registry token allocation, and private-construction context/store
handle primitives. It does not implement canonical keys, identity registries,
consumer migration, or the architecture gate.

The first focused handle test exposed a real issuer collision: two independently
constructed issuers for one context both allocated registry token one. The
public issuer constructor and isolated counters were deleted. Issuers are now
claimed once through `SemanticContextFactory`, and registry tokens come from
shared factory state. The focused sanitizer tests pass 2/2 after the repair.

The repository's `default` CTest preset pointed at `build-debug` even though
the documented development workflow configures and builds `sanitizer`. That
selected a stale debug test registration for a deleted fixture. The preset now
selects the sanitizer configure preset. A clean rerun used
`build-sanitizer` and passed 1,183/1,183 tests; the newly added handle test was
then configured, built, and verified separately. The active thread remains
open because RFC 0011 and every dependent implementation slice remain
incomplete.

### 2026-07-11 RFC 0011 canonical source URL admission

`CanonicalUrl` now implements the accepted credential-free absolute
hierarchical `https` and `ssh` profile. It canonicalizes DNS, IPv4, RFC 5952
IPv6, default and non-default ports, percent triplets, NFC Unicode path text,
decoded dot segments, empty path roots, repeated separators, and meaningful
trailing separators. It keeps percent-encoded reserved ASCII distinct from
literal path delimiters. It rejects user information, query and fragment data,
opaque and relative forms, invalid hosts and IP literals, invalid ports,
malformed percent triplets, literal non-ASCII URI bytes, and invalid encoded
UTF-8.

Every normative RFC 0011 URL vector passes together with focused DNS, IP,
port, Unicode, delimiter, idempotence, and canonical-admission tests. The
sanitizer build, RFC, format, and diff gates pass, and all 62 unit targets pass.
The exact-current-byte default suite has not yet been rerun after the scalar
and URL slices. Canonical paths and composite keys, fixed key vectors,
registries, invariant mapping, consumer migration, and the architecture gate
remain open, so the active thread remains incomplete.

The next identity slice adds a self-contained SHA-256 implementation and the
fixed-width big-endian, boolean, digest, byte-string, sequence-count, and
optional-tag primitives of `CanonicalEncoder`. The standard empty and `abc`
SHA-256 vectors, the standard 56-byte padding-boundary vector, and the RFC-fixed
`A` byte representation, empty sequence, and empty fingerprint-domain oracles
pass. The SHA implementation uses `zc::ArrayPtr` rather than raw pointer
parameters. The three focused identity test targets pass 3/3 and all 58 unit
targets pass under sanitizers. The first complete run containing all three new
identity targets passed 1,186/1,186 in 710.17 seconds. Canonical Unicode text,
URL and scalar domains, closed-value validation, composite keys, and all frozen
registries remain open.

After replacing the SHA implementation's internal raw pointer parameters with
`zc::ArrayPtr`, the sanitizer build, all 58 unit targets, and a second
exact-current-byte default run pass. The final default run passes 1,186/1,186;
the complete grammar oracle takes 1,273.01 seconds and the suite takes 1,273.13
seconds under a host load average near 88. No socket timing failure occurs.

The test documentation also incorrectly used CTest name regular expressions
for whole test classes. `-R unittest` selected no tests because `unittest` is a
label, not a registered test-name component. Current agent, build, lit, and
testing guidance now uses `-L unittest` and `-L lit`; the corrected unit command
passes 58/58. Focused tests continue to use `-R` with an actual test-name
pattern.

### 2026-07-11 RFC 0011 Unicode NFC normalization foundation

RFC 0011 now has a platform-independent Unicode 15.1.0 NFC normalization
primitive. Generated compiler data contains 922 non-zero canonical combining
class entries, 2,061 fully expanded canonical decompositions, and 941
non-excluded composition pairs. Runtime normalization performs recursive
canonical decomposition, algorithmic Hangul decomposition and composition,
stable canonical ordering, blocked canonical composition, malformed UTF-8
rejection, and idempotent UTF-8 output.

The official Unicode 15.1.0 `NormalizationTest.txt` NFC relations deduplicate
to 36,482 source strings. The generated test frames every source and expected
NFC output and fixes the aggregate expected SHA-256 as
`e2ab0b55ce326a724957b79efe63290de3c971a0aa5166cedec05eb77e448d5b`.
The sanitizer test reproduces that digest over every runtime result. Both data
generators pass byte-for-byte `--check`, all four identity test targets pass,
and the complete sanitizer unit label passes 59/59. Unicode License v3, source
URLs, and exact input SHA-256 values are recorded under `third_party/unicode`.

This closes only the normalization algorithm and its pinned data provenance.
Canonical scalar constructors, `CanonicalEncoder` non-NFC rejection, URL and
SemVer domains, maps and closed values, composite keys, registries, consumer
migration, and the identity architecture gate remain open. The active thread
therefore remains incomplete.

The exact-current-byte default sanitizer suite subsequently passes
1,187/1,187 with zero failures in 942.01 seconds. Its complete ANTLR grammar
oracle passes in 941.27 seconds, and the NFC conformance target passes inside
the same concurrent inventory. This is full regression evidence for the
normalization slice, not evidence for the still-open RFC 0011 scalar,
registry, migration, or architecture-gate requirements.

### 2026-07-11 RFC 0011 canonical scalar admission

RFC 0011 now has distinct move-only strong values for all eleven canonical
text domains in its scalar table. Source admission normalizes to NFC before
domain validation, canonical admission rejects non-NFC input, and encoding is
the fixed length-prefixed normalized UTF-8 representation. Semantic identifier
domains reuse the current lexer identifier and reserved-keyword classifiers;
declared definition names additionally admit exact `this`. The ASCII domains
enforce the RFC's exact length, case, character, and reserved-word constraints.

`ResolvedVersion` validates complete Semantic Versioning 2.0.0 text, preserves
case-sensitive prerelease and build metadata, rejects forbidden numeric leading
zeroes, and does not impose an implementation integer-width limit.
`SortedFeatureSet` sorts unique values by canonical encoded bytes, including
the byte-string length prefix, rather than ordinary lexicographic text order.
The sanitizer build, RFC, format, and diff gates pass, and the complete unit
label passes 61/61.

This closes canonical scalar construction except for `CanonicalUrl`. The full
default suite has not yet been rerun after this slice. URL normalization,
composite keys and fixed vectors, registries, invariant mapping, consumer
migration, and the architecture gate remain open, so the active thread remains
incomplete.

### 2026-07-11 RFC 0011 canonical composite keys

RFC 0011 now has canonical package sources and keys, package dependency-edge
keys, compilation configuration and crate keys, crate dependency-edge keys,
source origins and source-file keys, source spans, module keys, structural
definition paths and definition keys, and impl keys. Strong construction
rejects malformed VCS digest widths, unknown closed-enum values, invalid pointer
widths, empty or incorrectly terminated definition paths, and mismatched
package, crate, source, and module ancestry.

The fixed codec vectors cover package, package edge, crate, crate edge, source
file, module, definition, and impl keys at 43, 98, 154, 406, 240, 412, 692, and
680 bytes respectively. Their SHA-256 values are
`b0c7b4f55c7faf6d4522b3a6f81e979347436c782d29ad2eeaa09985479d40a6`,
`b4a6fdda29af9e3c0b0d6a21b062aa94be3315bc47bde3f432d46e85766b2751`,
`136b0e54d7750bc21ab3e1b5f7cd1f6046fa8f5bafab919c391444a869a6c537`,
`64fcca3d969d5d52c170d40a8a8db32005853856b61087719d003799c2c387a5`,
`f4198087783111e14911a0f550962f5c010ea2609edfdca47152907d74969102`,
`8ef9b8baabd646bf1a4640a8bd70af16e93bbe979229c21342cbebd0c429b91b`,
`3f9ea55ca0ce091341b59f3cd44b64962e9cf26f4c4e9c19815011a702432ca4`,
and `e71d00f88b11b9ee6bd0a5f2196f9c7506fbe28f341733df1e788cc192d23882`.
The complete sanitizer build, RFC structure, format, diff hygiene, Unicode data,
Unicode normalization data, and Unicode normalization oracle checks pass, and
the complete unit label passes 66/66.

This evidence does not yet prove source span bounds against an immutable source
buffer. That check belongs to the pending source registry, which must also
verify the content digest. Frozen registries, deterministic context
fingerprinting, structured invariant diagnostics, consumer migration, deletion
of old identity surfaces, the architecture gate, and a new exact-current-byte
default suite remain open. The active thread therefore remains incomplete.

### 2026-07-11 RFC 0011 frozen registries and invariant boundary

RFC 0011 now has one factory-claimed `SemanticIdentityRegistrySet` per semantic
context. Its package, crate, source, module, definition, and impl registries have
private construction, sort by full canonical encoded bytes, reject duplicate
keys before issuing handles, validate context-bound lookup, invalidate on
post-freeze mutation, and enforce the complete freeze schedule. Module slots
form one global order across crates. Package, crate, source, module, definition,
and impl registry tests cover canonical order, duplicates, foreign contexts,
freeze order, source ownership, and cross-crate inventories.

Source collection now retains immutable bytes and their computed SHA-256.
`SourceSpan` and `UnbrandedSourceRange` are created only through that snapshot
and reject reversed or out-of-bounds half-open offsets. The deterministic
context fingerprint accepts only frozen package, crate, source, and module
registries, canonicalizes package and crate edge inventories, rejects duplicate
canonical records and multiple content records for one source key, and passes
the accepted empty-domain and sorted package-graph digests
`aa36edfdf536f061cd028efd3cfe5003474aee9aa3ab39f294d3b42a95eaae5e`
and `20d2a8ab26a6a17066de900f472dab2e6222c949c6b01da507753822bc116eac`.

Every registry failure crossing the identity boundary is retained as a
structured fact with allocation phase, invariant kind, optional canonical
structural bytes, optional validated unbranded range, API site, and traversal
ordinal. Facts sort deterministically and group only by registered diagnostic
and equal validated range. `diagnostics-identity.def` owns exact fatal entries
`ZOM9910` through `ZOM9921`; the adapter emits group counts without fabricating
a location. The `zom.identity.v0` dump emits all six sections in canonical key
order with lowercase hex and one final LF. The complete sanitizer build, RFC,
format, and diff checks pass, and all 70 unit targets pass.

The exact-current-byte default sanitizer inventory subsequently passes
1,198/1,198 with zero failures in 687.73 seconds. The complete ANTLR grammar
oracle passes in 687.59 seconds, and all new identity, source snapshot,
fingerprint, invariant, diagnostic registration, and dump tests pass inside the
same concurrent run.

A subsequent hardening pass gives process-wide uniqueness to context and
registry brand tokens across independent explicit factory objects, adds
exhaustion injection through issue budgets, covers same-slot foreign-context
rejection for all six RFC 0011 context-handle tags, rejects generated-source
origin/snapshot digest mismatches before source handle issue, and adds a
`SourceManager` bridge that binds only byte-identical immutable snapshots and
resolves only fully validated unbranded ranges. The sanitizer build, RFC,
format, diff, and all 70 unit targets pass after this pass. Because it followed
the 1,198-test run, a new exact-current-byte default suite remains required.

This does not yet claim RFC 0011 completion. The live-producer architecture
gate, final driver/session ownership, semantic-type and store-local handle
integration, consumer migration, and deletion of the old identity surfaces
remain open. A new exact-current-byte default suite is also pending after the
latest hardening changes.

### 2026-07-11 CompilerSession cutover and executable failure-boundary gates

RFC 0008 legally transitioned from `ACCEPTED` to `IMPLEMENTING` when the direct
replacement series started. `CompilerSession` now replaces `CompilerDriver` in
the driver library, CLI, and unit tests. The CLI owns the process-root
`SemanticContextFactory`; each session claims one process-unique
`SemanticContextBrand` and the sole RFC 0011 registry family for that context.
Two sessions created from one factory receive distinct brands and registry
families.

`scripts/check-compiler-session-architecture.py --check` proves the exact
driver surface, unique process-root factory, sole registry claim path, and one
frontend scheduler. Its seven negative fixtures reject an old driver, alias,
wrapper, second scheduler, missing registry owner, second factory, and raw
session assertion. The
positive and negative checks are registered with CTest.

`scripts/check-ir-diagnostic-boundary.py --check` makes the current mixed IR
prototype's structured failure boundary executable. It checks the complete
`LoweringFailureKind`, `LoweringPhase`, `IrDumpFailureKind`, and
`IrDumpVerifierSite` mappings plus the required `.def` registry. Four negative
fixtures reject a raw assertion, unmapped failure kind, string failure field,
and missing diagnostic definition. This gate does not claim that accepted RFC
0010 HIR/MIR/LIR verifiers exist.

The complete sanitizer build and all 70 unit targets pass. The default matrix
configured after the CompilerSession gate passes 1,200/1,200 in 793.44 seconds;
its grammar aggregate passes in 793.14 seconds. After registering the two IR
diagnostic gate targets, all four CompilerSession/IR architecture targets pass
in 11.89 seconds. Format, RFC, parser coverage, AST coverage, lexer
architecture, and diff checks pass.

A subsequent construction-failure repair removes the two raw
`ZC_IREQUIRE` paths from `CompilerSession`. Context-brand exhaustion emits
registered `ZOM9919`; a duplicate singleton registry emits registered
`ZOM9920`; either failure leaves the session unable to run phases. The focused
session suite passes 13/13, all 70 unit targets pass, and the four architecture
targets pass after this repair. The configured inventory is 1,202; the complete
1,200 result above predates this final repair, so a new full 1,202 run remains
required before completion.

RFC 0008 remains incomplete: `ModuleGraph`, immutable module interfaces,
`SignatureStore`, checked-facts publication, dependency scheduling, and global
coherence do not exist. RFC 0011 remains incomplete because semantic consumers
still use table-local `SymbolId`, buffer-local maps, and unbranded type IDs.

### 2026-07-11 RFC 0011 live definition-producer inventory

RFC 0011 now has a real prebinding `binder::DefinitionInventory` rather than a
documentation-only producer matrix. It walks the immutable AST before symbol
creation and records module, definition, and impl producers, declared and
anonymous names, source anchors, current explicit module syntax, and structural
definition-or-impl parents. Context-sensitive handling distinguishes
module-scope constants and statics, block locals, recursive destructuring
leaves, match and loop bindings, import and re-export aliases, closures, and
impl members. `VariableDeclarator` and the other named containers remain
identity-free.

The machine-readable `definition-producers.json` inventory and
`scripts/check-identity-architecture.py` form an executable architecture gate.
The positive check aligns declaration-bearing schema nodes with exact parser
`make*` sites, prebinding switch handlers, closed definition kinds, anonymous
roles, and an empty post-parse expansion producer set. It also freezes the
current phase-local `SymbolId` and pointer-derived identity surfaces so no new
consumer can appear silently. Six negative fixtures prove the gate rejects a
missing schema rule, missing parser producer, missing inventory handler,
unknown identity kind, post-parse producer, and expanded old-identity surface.
The inventory unit target passes 3/3, both architecture-gate modes pass, the
complete sanitizer build passes, and all 71 unit targets pass.

`CompilerSession::parseSources()` now collects and owns one inventory for every
successfully parsed buffer before publishing its AST. Callers receive an owning
inventory clone rather than a mutable map or a borrowed reference that outlives
the session mutex. The session architecture gate checks the owner and collection
site. The session suite passes 14/14, and the combined session/inventory
architecture slice passes 6/6. The complete sanitizer build and all 71 unit
targets pass after the session integration.

This slice does not issue canonical handles. CompilerSession-driven package,
crate, source, module, definition, and impl key collection and registry freeze
remain next, followed by direct consumer migration and deletion of every
phase-local allowlist entry. The complete exact-current-byte suite has not yet
been rerun after this slice, so the active thread remains incomplete.

### 2026-07-11 RFC 0012 implementation start

RFC 0012 legally transitioned from `ACCEPTED` to `IMPLEMENTING` when the direct
package-input implementation series started. The first slice imports the exact
accepted releases of toml++, Neargye/semver, libsodium, libarchive, and
Zstandard under the driver package boundary. The canonical vendor manifest
records every admitted file and its size and digest together with the pinned
release URL, tag, commit, license, archive digest, extracted-content digest,
compile policy, and empty local-patch digest.

The vendored-dependency checker rejects missing, added, changed, non-regular,
or non-canonical content. It is registered with CTest and is a mandatory driver
build dependency. This proves source admission only. Final minimal static C
source lists, wrapper ownership, manifest normalization, resolution, lock and
materialization, build-script execution, CLI replacement, and package-aware
CompilerSession handoff remain open. The active thread remains incomplete.

The Zstandard boundary is the first admitted dependency to become executable.
Its exact twelve-source common/decompression inventory builds as a C11 static
library with assembly, legacy frames, compression, dictionary building, and
multithreading disabled. The move-only Pimpl `ZstdDecoder` owns the only C
context pointer through a static zc disposer, sets the RFC window before frame
admission, verifies working-memory and compressed-byte limits, streams bounded
chunks, rejects skippable, concatenated, trailing, and truncated frames, and
returns only closed `MaterializationIssue` values. Seven sanitizer tests cover
fragmented success, exact bytes, trailing and truncated inputs, both resource
limits, and typed source/sink failure forwarding. The complete sanitizer build
and all 72 unit targets pass. Libsodium, libarchive, manifest parsing, and all
later RFC 0012 stages remain open.
