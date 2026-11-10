# Engineering Efficiency (Non-Negotiable)

> Applies globally. Engineering efficiency is a first-class, gated project
> metric, not a matter of personal pace. The wall-clock cost of landing one
> logical change must stay roughly constant as the repository and its test
> corpus grow. A workflow whose cost grows super-linearly with project size is
> a defect and must be fixed, not accepted as "the repo got bigger".

---

## 1. Risk-tiered gates, not one uniform heavy gate

The strongest available gate is not always the correct gate. Match the gate to
the blast radius.

| Change class | Required per slice | Full gate timing |
|---|---|---|
| HIR-only, shape-driven, verified zero MIR/LIR/op/codec/ABI change | Directed `ztest` red->green, scoped lit (AST/diagnostics/grammar), affected native package exit code | Full `ctest` 322 + full corpus parity only at a **batch boundary** or pre-push |
| Cross-layer or shared area (LIR, MIR, call ladder arity, ABI, codec, identity) | All of the above **plus** full `ctest` + full parity + `repo-lint` on every change, under the shared-change gate | Every change |
| Docs / non-code | `check-format.py`, `check-english-only.py`, `repo-lint` | N/A |

A **batch** is 2-3 strongly related slices that share one rail and one risk
class. Each slice keeps its own atomic Conventional Commit; the expensive
full-corpus gates run once at the batch boundary, not once per slice. The
"series closeout" language in AGENTS.md already permits this — do not turn every
slice into its own independent full closeout.

---

## 2. Test and build efficiency rules

1. **Enumerate the blast radius before any expensive gate.** Before running a
   full-corpus parity or the 322-test suite, grep every fixture and code site on
   the affected rail/spelling and run a local `run-zomc-package.py --check`
   sweep over them. A one-minute local sweep must find every source whose
   accept/drain status flips; never let a 25-minute full-parity job be the thing
   that discovers an unanticipated flip. The sweep must cover all four fixture
   channels, not just the conformance tree:
   1. conformance corpus plus the AST/diagnostics/grammar expectation dirs;
   2. driver/session `ztest` sources that pin the old drain behavior;
   3. native-run `--run-case` integration packages and their exit codes;
   4. object-emission `--case` / `--reject-case` integration packages.
   Grep the old shape's source pattern across the whole `tests/` tree; a rail
   widening flips every channel that encodes the old accept/drain verdict.
2. **The corpus has a known pathological source.**
   `interface_heritage_over_255_pos_33` is O(n^2) under ASan and takes roughly
   5.5 minutes per pass (about 11 minutes for the process+IR double pass) even
   when it is unrelated to the change. Prefer affected-directory/affected-rail
   parity during development; run the whole corpus only at the batch boundary.
   Do not pay the heritage tax on every slice.
3. **Never re-record the full corpus twice in one slice.** Record process and
   IR serially in one job using the explicit IR snapshot flag
   (`--snapshot tests/coverage/corpus-ir-parity.json`); if the fixture set is
   found incomplete after a record, restore the backup snapshots, finish the
   fixtures, and re-record once — not per correction.
4. **Serialize build and test, and keep one heavy background job at a time.**
   Rebuild first, then test; never rebuild while a test binary is executing (a
   replaced binary invalidates the run). Do not start a second ctest/parity
   while one is running.
5. **Compact at clean milestones.** When a milestone is fully closed and the
   working tree is clean, a new session/context with continuity carried by the
   memory notes is faster than an unbounded single session whose history makes
   every code lookup a re-grep.

---

## 3. Code-change efficiency and safety rules

These prevent both production defects and the rework that destroys throughput.

1. **Admission predicates across layers must be identical.** The HIR builder's
   capability gate and the HIR verifier's accepted-shape set must widen and
   narrow together, in one atomic change. A builder that emits a shape the
   verifier rejects does not produce a user diagnostic — it produces an
   invariant incident (internal compiler error). Never land a builder change
   without its matching verifier change.
2. **A flag's blast radius spans every shape behind it.** A single flag often
   classifies more than one surface (for example one flag covering both
   arithmetic and relational closures, or one rail covering both the
   expression-body lambda and the block-body function-expression spelling).
   Before widening a flag, enumerate every shape, operator family, and spelling
   that reads it, and confirm each intended flip; otherwise an apparently local
   slice silently changes unrelated fixtures.
3. **Find every copy of a duplicated rule before editing it.** A policy such as
   "the two operands must be distinct parameters" can live in several places
   (one builder check plus multiple verifier paths). Grep the rule, not just the
   nearest occurrence; a partial widening leaves a dead drain or an
   unreachable-then-ICE path.
4. **Prove downstream agnosticism before claiming an HIR-only slice.** Read the
   MIR/LIR lowering to confirm it does not key on the property being changed
   (operand order, operand distinctness, local reuse). When the downstream layer
   resolves each node independently or consumes already-lowered locals, the
   change is genuinely HIR-only; otherwise treat it as a cross-layer change and
   apply the heavier gate.
5. **Re-aim fail-closed sentinels at the next wall; do not delete them.** When a
   rail widening turns an object-emission `--reject-case` (or any drain-pinned
   fixture) green, replace its source with the nearest shape that still drains
   cleanly — typically the next parameter-count or nesting wall — and keep the
   registration in place. Deleting the sentinel leaves the integration layer
   with no proof that the drain still blocks artifact publication; re-aiming
   preserves the fail-closed contract at zero registration churn.

---

## 4. Mandatory per-milestone efficiency retro

After every milestone closeout (and before starting the next slice), answer in
the milestone report:

1. What was the total wall-clock of test/build gates, and what fraction was
   spent on sources unrelated to the change?
2. Did any gate discover something a local one-minute sweep should have caught?
   If yes, add the missing sweep to section 2 and record it in the case log.
3. Was the gate strength matched to the risk class per section 1, or was a heavy
   gate used for an HIR-only change?
4. Did the change touch duplicated rules or a shared flag whose full blast
   radius was not enumerated up front?
5. Is there a concrete process or code change that would make the next similar
   slice cheaper? If yes, file it here or as a tracked task immediately — do not
   merely note it.

Efficiency findings are project knowledge: append durable ones to the case log
below and keep the relevant rule in this file. Repeatedly observed costs
justify infrastructure work (scoped parity, caching, sharding) and should be
raised, not absorbed.

---

## 5. Case log (append-only, evidence first)

### 2026-11 — closure Copy-only slice series (branch rfc0058-b2)

- **E1 — O(corpus) parity tax.** Each nested/sequential/repeated-operand slice
  re-recorded all ~1134 corpus sources twice (process + IR). The unrelated
  `heritage_over_255` O(n^2) source alone cost ~11 minutes per record; a full
  parity cycle was 20-30 minutes and did not shrink as slices shrank to +8/-2
  line changes.
- **E2 — uniform full ctest for tiny changes.** A two-line HIR count-gate
  relaxation still triggered the full 322-test sanitizer suite (~13-15 min),
  identical in cost to the first high-risk slice. Gate strength was decoupled
  from risk.
- **E3 — full-parity rework from unenumerated blast radius (twice).** Widening
  the one-parameter repeated-operand rail flipped not only the intended
  expression-body fixture but also a separate block-body same-operand negative;
  an earlier inner-on-right slice had the same surprise. In both cases a full
  parity run was killed, snapshots restored, fixtures completed, and the entire
  corpus re-recorded — ~25 minutes wasted each time. Root cause: the rail's full
  fixture/spelling set was not enumerated before the expensive gate.
- **C1 — builder/verifier directional asymmetry produced an ICE.** A
  symmetric builder feeding a left-only verifier turned the inner-on-right
  closure from a clean ZOM4099 drain into an invariant incident. Fixed by
  widening builder and verifier atomically and keeping the verifier as the
  backstop (section 3.1).
- **C2 — one flag, two operator families.** `parameterArithmeticClosure`
  classifies both arithmetic and relational closures, so removing its
  distinctness gate admitted `a + a` and `a < a` together; an arithmetic-only
  test plan would have left the relational diagnostics lit failing (section 3.2).
- **C3 — duplicated distinctness rule.** The "two distinct parameters" policy
  lived in one builder and two verifier locations; widening required finding all
  three, and the nested branch correctly retained its own separate guard
  (section 3.3).
- **E4 — two extra flip channels found only by the full 8-minute ctest.** The
  one-parameter repeated-operand widening was swept against the conformance
  corpus and parity snapshots only. The full gate then failed twice more: a
  session `ztest` still asserted the old `(x)=>x+x` ZOM4099 drain, and the
  object-emission `--reject-case` package `reject-closure_consumer` (the same
  shape, `g(41)`) unexpectedly emitted an object. Both were stale drain pins,
  not production defects, but each cost a full-suite cycle to discover. Root
  cause: the sweep covered one of the four fixture channels (section 2.1).
  Fix: the pre-gate sweep now enumerates all four channels; the reject package
  was re-aimed in place at the three-parameter arity wall (one ZOM4099, zero
  incidents, no artifact) per section 3.5 instead of being deleted.
