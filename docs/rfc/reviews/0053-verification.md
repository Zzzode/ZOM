# RFC 0053 Owner Review — Evidence And Gate Quality

Reviewer: `verification` (evidence and gate quality owner)
Date: 2026-09-20
Review target: `docs/rfc/0053-lir-structural-verification-and-translation-validation.md`
Reviewed revision: `status: DRAFT`, `updated: 2026-09-19`, four landed slices
Reviewed tree: `develop` @ `e0cb57da`

## Verdict

**REQUEST-CHANGES**

Blocking findings: **4**. Non-blocking (NIT) findings: **6**.

The two stages are real, independent, and genuinely on the production path.
This review is not a challenge to the architecture. It is a challenge to the
RFC's *evidence claims*, because three of the seven acceptance criteria are
unsatisfiable as written and one asserted gate does not exist. Every claim
below was executed, not read.

### What is verified and sound (no finding)

Executed evidence, all on the sanitizer preset unless stated:

| Claim | Method | Result |
|---|---|---|
| CMake configure + build clean | `cmake --preset sanitizer` + `--build` | exit 0, 191/191 targets |
| Both stages exist under `compiler/lir/verify/` | file inspection + explicit CMake list | `lir-verifier.{h,cc}`, `translation-validator.{h,cc}`, `compiler/lir/CMakeLists.txt:7-8` |
| Both stages are independent of the producer | `grep` includes | Neither file includes `mir-to-lir.h`; validator includes only `signature-facts.h`, `lir-store.h`, `built-mir.h`, `semantic-type-data.h`, `semantic-type-store.h`. Carrier derivation, bit materialization, and place mapping are file-local duplicates. **Independence holds.** |
| Asan/UBSan/LSan clean, tests pass | direct runs + ctest | `lir-verifier-test` 16/16; `lir-translation-validator-test` 14/14; `lir-module-test`, `lir-store-test`, `lir-algebra-codec-oracle-test`, `built-mir-test`, `hir-module-test` all pass (7/7, 721s) |
| Lit suite green | `ctest --preset default -L lit` | 4/4 passed (~71s) |
| Stages run on `zomc compile --emit=binary` | gdb breakpoint on built `zomc` | **Breakpoint hit in both.** `LirStructuralVerifier::verify` at `zomc.cc:1547`, `TranslationValidator::validate` at `zomc.cc:1559`, both called from `buildNativeObject` |
| Emission end-to-end unchanged | `ctest --test-dir build-llvm -R "object-emission-cli\|native-run-cli"` | 2/2 passed (ELF written for all nine shapes; real host execution exit 42) |
| Format gate on touched files | `clang-format` directly on all six new files | All six byte-identical to `clang-format` output |
| `check-rfc` frontmatter | `python3 scripts/check-rfc.py` | pass (53 proposal RFCs) |
| RFC 0043 rollback safety of the new call site | measured | With both stages bypassed and rebuilt, `object-emission-cli` + `native-run-cli` still pass — the stages are evidence-only, exactly as the RFC's Compatibility section claims |

### The single most important structural finding

**Every script gate in this repository is blind to this RFC's call sites, and
the only test that exercises the fail-closed path constructs its fault in
another language.** I proved this by mutation:

- Removing both calls outright: `check-ir-architecture.py`, `check-rfc.py`,
  `check-english-only.py`, `check-ownership-architecture.py`, and
  `check-diff-hygiene.py` all exit **0**. Only `check-format.py` fires, and
  only because the deletion happened to leave non-`clang-format` whitespace —
  a formatting accident, not a gate.
- A *format-clean* bypass (`if (false && …)`, `false ? … : …`), rebuilt into a
  working `zomc`: **every gate exits 0** and **`object-emission-cli` and
  `native-run-cli` both still pass** (2/2).

`grep -rl "LirStructuralVerifier\|TranslationValidator" scripts/` returns
nothing. Nothing in CI can detect the removal or weakening of either stage.

---

## Blocking Findings

### B1. Acceptance criterion 5 is contradicted by the RFC's own test plan, and both parity channels are red today

RFC sections: `Acceptance Criteria` item 5 (lines 553-555), `Test Plan` →
"Parity" (lines 589-591).

Criterion 5 requires "both IR parity channels pass on the sanitizer preset".
The Test Plan likewise requires both channels "run serially ... over the
923-source corpus". Neither is runnable as documented, and neither passes:

**(a) The `--ir` channel has no default snapshot.** `check-ir-parity.py:315`
defaults `--snapshot` to `tests/coverage/corpus-process-parity.json`. Running
the documented invocation:

```
$ python3 scripts/check-ir-parity.py --check --ir --zomc ./build-sanitizer/compiler/zomc
corpus parity check failed [snapshot-error]: snapshot irChannel=None does not match --ir=True
$ echo $?
1
```

The `--ir` channel requires `--snapshot tests/coverage/corpus-ir-parity.json`,
which the RFC's Test Plan never names. Measured directly, with the correct
snapshot, the IR channel fails with the same 924 differences as the process
channel.

**(b) Both channels are red at HEAD, by 924 of 923 sources.** The baseline was
recorded 2026-09-10 (`f5c45192`, `7075efb3`); **48 commits** have landed since,
including this RFC's five. Baseline `f5c45192`..`7075efb3` predates the RFC's
first commit `529e9112` (2026-09-18):

```
$ python3 scripts/check-ir-parity.py --check --zomc ./build-sanitizer/compiler/zomc --limit 100000
corpus parity check failed: 924 shown difference(s) between builds:
  - 03-types/type_alias_unsupported_target_neg_01.zom: added in new build
  - 02-lexical/bigint_suffix_neg_01.zom: output changed (same exit 1)
  - 02-lexical/bigint_suffix_pos_01.zom: exit 0 -> 1
  ...
```

924 of 923 sources differ, including `exit 0 -> 1` flips. This is not caused by
RFC 0053 — it is cumulative drift from RFC 0048's HIR work and the four
parser/checker commits immediately below this RFC's commits (`bfab3a9d`,
`b7ac2ca1`, `1c476007`, `e0cb57da`) — but it means the RFC's central motivating
claim, that "the 923-source byte-parity channels detect *unexpected changes*"
(line 51-52), is **false on this backend today**: the channels are not a
working detector at all. An author cannot cite a red channel as the baseline
they are improving on.

Note the intersections with this RFC's own mandate. The RFC elevates byte
parity from "the correctness mechanism" to "a regression tool" (line 497-499),
yet the regression tool is 100% red, and the RFC's Compatibility section
asserts "accepted source programs compile to identical objects because both
stages are evidence-only and pass on the current corpus" (line 503-504). That
sentence has not been demonstrated against any recorded baseline.

**Closes with (artifact):** either (i) re-record both snapshots at the RFC's
slice-4 commit and state the recorded revision + counts in the RFC, or (ii)
delete criterion 5's parity clause, move the channel to `docs/rfc/tracking/`
as an explicitly known-red regression tool, and correct line 51-52 to say the
channels are currently non-detecting. Option (i) requires the RFC to name the
`--snapshot` path for the `--ir` channel; the current text is not runnable.

### B2. The fail-closed path exercised anywhere is not the production fail-closed path

RFC sections: `Acceptance Criteria` item 4 (lines 551-552), `Integration Point`
step 5 (line 401-402), `Guide-Level Explanation` (lines 198-201), `Failure
Projection` (lines 379-388).

The RFC requires that a finding "abort[s] the emission transaction through the
internal failure path". What is implemented at `utils/zomc/zomc.cc:1547-1565`
is neither:

```cpp
auto structural = lir::LirStructuralVerifier::verify(lirModule);
if (structural != zc::none) {
  return NativeObjectResult(
      zc::str("Internal compiler error: LIR structural verification failed (fault ",
              static_cast<unsigned>(ZC_ASSERT_NONNULL(structural).fault), ")."));
}
```

The finding is flattened to a decimal integer in an ad-hoc `zc::String`,
returned as the ordinary `Validity` failure. It never enters
`reportSessionIncident()` / `renderCompilerIncident()`, and no
`IrFailurePhase::LirVerification` value is ever constructed:

```
$ grep -rn "IrFailurePhase::LirVerification" compiler/ utils/
compiler/ir/diagnostics/ir-failure.h:50   (declaration)
compiler/ir/diagnostics/ir-failure.cc:121 (legality switch)
compiler/ir/diagnostics/ir-failure.cc:195 (legality switch)
compiler/ir/diagnostics/ir-diagnostic-projector.cc:31 (projector switch)
# plus the two mirror switches in tests/unittests/compiler/ir/diagnostics/ir-failure-test.cc
```

**Zero producers.** The phase remains, exactly as the RFC concedes at line 382,
one "which is already declared but has no producer today" — after the slice
that was supposed to give it one.

The `docs/design/ir/lowering-and-verification.md:135-138` note added in
`d604d449` is therefore wrong, and it is an IR design note, which the
`cpp-zc.md` Diagnostics Layering rule binds to "only current production
artifacts":

> "the binary-emission path does run two independent stages that project
> findings at the `LirVerification` phase (`0x0d`) as compiler incidents"

That sentence describes infrastructure that does not exist. Its own commit
message (`d604d449`) repeats the claim. (Correction in passing: the enclosing
document's `Updated:` header still reads `2026-09-14` even though the commit
modified it on 2026-09-19; `lir.md` got `Updated: 2026-09-19`. Both live in a
directory whose README makes the freshness field part of the contract.)

Why this is *verification* blocking, not just documentation:

1. Criterion 4 cannot be assessed. "fails closed on a finding, with an internal
   incident failure and no output file" — the "no output file" half is testable
   today (see B3), the "internal incident failure" half is not implemented, so
   the criterion is neither met nor falsifiable.
2. The only place a *real* `LirVerificationFinding` is produced and asserted is
   the ztest layer, in C++, via direct calls to `verify`/`validate`
   (`lir-verifier-test.cc`, `lir-translation-validator-test.cc`,
   `llvm-translation-test.cc:128,129,272,273,314,315,385,386,436,437,534,535,640,641,758,759,866,867,967,973,1070,1076`).
   No test reaches `zomc.cc:1548` or `zomc.cc:1560`. There is no CLI-level,
   fault-injected end-to-end test of the fail-closed transaction.
   `grep -rn "Internal compiler error" tests/ scripts/` returns **nothing**.
3. `legalOwnerSite(IrFailurePhase::LirVerification, …)`
   (`ir-failure.cc:195-197`) requires `Instance` ownership, and
   `isLegalIrFailureShape` (`ir-failure.cc:121`) permits only
   `InvalidSsa`/`MissingTargetLayout`/`InvalidAbi` at that phase. The RFC says
   the finding "maps to `IrFailureKind::InvalidFact`" (line 384-385).
   `InvalidFact` is admitted through `common` (`ir-failure.cc:60-64`), so the
   two are *compatible* — but only via the common-set fallthrough, and the RFC
   does not state which `InstanceId` would be constructed from
   `functionIndex`. This is unbuilt, so it is undecided, not contradictory.

**Closes with (artifact):** one of two concrete outcomes.

- *Preferred*: implement the projection (slice 4 as the RFC describes it) and
  add a CLI-level negative test — a fixture or fault-injection hook that forces
  a finding through `zomc compile --emit=binary` and asserts non-zero exit, the
  incident record, **and no file at `-o`**. Correct
  `docs/design/ir/lowering-and-verification.md:135-138` in the same change.
- *Alternative*: rewrite `Failure Projection`, criterion 4, `Integration Point`
  step 5, `Implementation Plan` step 1, and the 2026-09-19 status row to state
  that the slices intentionally ferry the fault as an unrelated `zc::String`
  without the failure algebra, and correct the design note. This is legitimate
  under the RFC's own `Non-Goals` ("New user-facing `ZOMxxxx` diagnostics"), but
  it must be *stated*, not asserted as done.

Either way this is **BLOCKING**: criterion 4's evidence does not exist, and a
normative design note describes non-existent production behavior.

### B3. Two of the thirteen structural fault tags and four translation branches have no mutation test; one tag is claimed by two unrelated rejections

RFC sections: `Acceptance Criteria` item 3 (lines 548-550), `Implementation
Plan` step 2 (line 566-567), `Test Plan` (line 582-583: "the mutation matrix
(one fault per closed fault tag)").

**Structural: 13 tags, 14 mutation ztests.** I enumerated both sides.

| Tag (`lir-verifier.h:29-60`) | Mutation ztest |
|---|---|
| `EmptyOrDuplicateSymbol` | 2 tests (empty symbol; duplicate symbol) |
| `MissingEntryBlock` | yes |
| `DuplicateBlockOrdinal` | yes |
| `NonDenseBlockOrdinals` | yes |
| `UnreachableBlock` | yes |
| `NonDenseLocalSlots` | yes |
| `UndeclaredLocalSlot` | yes |
| `DanglingBlockTarget` | yes |
| `TerminatorArity` | yes |
| `CarrierMismatch` | **2 tests, both via `Compare`/`Assign`** |
| `ConditionNotBit1` | yes |
| `CalleeIndexOutOfRange` | yes |
| `ReturnCarrierMismatch` | yes |

Builtin count is satisfied in aggregate (14 ≥ 13), but the RFC's wording is
"one fault per closed fault tag" and there is no tag whose *sole* coverage is a
`Call`-destination carrier mismatch. `lir-verifier.cc:319` emits
`CarrierMismatch` for a call destination with the wrong carrier; no test
constructs that. The aggregate count hides the gap.

**Translation: 9 tags, 11 mutation ztests** — every tag has at least one, so
criterion 3 holds for the translation stage on the tag axis. But the *branch*
axis is thinner than the RFC implies, and I measured it rather than inferring
it.

Uncovered by any translation ztest (verified by `grep` for the construct names
in `lir-translation-validator-test.cc` — zero hits for `UnsafeScopeBoundary`,
`BorrowCreation`, `SetDiscriminant`, `Deinitialize`):

- `translation-validator.cc:410-414` — the `BorrowCreation` / `SetDiscriminant`
  / `Deinitialize` reject arm. The RFC's effect table (line 356) lists
  "`UnsafeScopeBoundary`, ownership-only statements → no LIR effect", so the
  RFC actively asserts a contract here that no test exercises in either
  direction.
- `translation-validator.cc:400-405` — the materialized `NominalAggregate` /
  `Arithmetic` reject arm.
- `translation-validator.cc:582-583` — `MirTerminatorKind::Unreachable` reject
  arm.
- `translation-validator.cc:217-218` — the non-`Field` projection
  `PlaceMappingMismatch`, and `:251-253` — the empty-aggregate `EffectMismatch`.
- `translation-validator.cc:563-579` — the `callHasArgument()` single-argument
  vs `callArguments()` multi-argument split. Only the **zero-argument** path is
  mutation-tested. The one-argument path is asserted only positively
  (`llvm-translation-test.cc:1076`); the two-argument path
  (`callArguments()`, `mir-to-lir.cc:1104`) has **no validator assertion at all**
  in any test, and no mutation test.

**Arithmetic coverage is the substantive gap.** The RFC's effect table explicitly
routes `Assign(place, Arithmetic(…))` to a reject (`EffectMismatch`), and
`eab36e10 feat(mir): lower primitive-binary initializers through recursive
builder` landed *immediately after* the RFC's slice 4. The RFC's own Drawbacks
section (line 456-459) requires "each new producer extends the relation in the
same change; an unverified producer is prohibited rather than allowed through".
`eab36e10` added a producer with no relation row, no structural rule, and no
mutation test. Through `emitBinary` the arithmetic producer is not reachable
today (the object selector still only tries the closed shape list), so this is
not a live miscompile — but it is a producer in the tree that the RFC's stated
policy says must not exist, so the RFC's forward-looking contract is already
unhonored one commit later.

**Coverage measurement (project rule, `.codex/rules/testing.md` § Coverage):
"New compiler source files without >70% line coverage are rejected on review".**
`create_coverage_target` lives in `cmake/utils/coverage.cmake` and is bound to
the `coverage` preset, which has `ZOM_ENABLE_LLVM_BACKEND=OFF`
(`build-coverage/CMakeCache.txt:358`). The backend target
`llvm-translation-test` therefore cannot be built under the coverage preset
(`ninja: error: unknown target 'llvm-translation-test'`), so the coverage
figure excludes the file that carries most of the shape coverage. Measured with
the two dedicated ztests only (`llvm-cov report`, `llvm-profdata merge` of both
`.profraw`):

```
compiler/lir/verify/lir-verifier.cc            73.32% lines, 81.79% region, 74.23% branch   -> PASSES
compiler/lir/verify/translation-validator.cc   56.47% lines, 59.92% region, 52.09% branch   -> FAILS the rule
```

`translation-validator.cc` is **below the 70% rule as measured on the tooling
that exists for it**. Adding `llvm-translation-test`'s assertions would raise
this (that file calls both stages at 22 sites), which makes the *tooling*
deficiency the more important half of this finding — the RFC cannot claim
criterion 5 while the only coverage mechanism structurally cannot see its most
exercise-rich test.

**Closes with (artifact):** (i) one mutation ztest each for the `Call`-destination
`CarrierMismatch` (structural) and for the arithmetic materialized-reject,
ownership-only-statement reject, `Unreachable` reject, and multi-argument call
branches (translation); (ii) a coverage lane that can build a
backend-enabled, instrumented target, with the measured percentage for both new
files recorded in the RFC's `Test Plan`; or an explicit RFC statement that the
70% rule is waived for `translation-validator.cc` with the reason. Silence is
not acceptable — `.codex/rules/testing.md` makes >70% a review gate.

### B4. Acceptance criteria 5 and 7 name gates that either do not exist or are unwired

RFC sections: `Acceptance Criteria` items 5 and 7 (lines 553-560), `Test Plan`
(curiously narrower than criterion 5), `Repository Impact` (line 430: "the
existing `tests/conformance` object-emission corpus now crosses the new gate").

**(a) The object-emission corpus is in no CI lane.** `.github/workflows/CI.yml`
has exactly one backend job, `check-backend-llvm` (line 258). It builds **one
target** and runs **one test**:

```
- name: Build backend translation test
  run: cmake --build build-llvm --target llvm-translation-test
- name: Run backend translation test
  run: ctest --test-dir build-llvm --output-on-failure -R llvm-translation-test
```

`ctest --test-dir build-llvm -N` shows `object-emission-cli` (#66) and
`native-run-cli` (#67) registered but **never selected** by `-R
llvm-translation-test`. The RFC's slice-4 status row explicitly cites these two
ctests as its end-to-end evidence. They are local-only. I ran them (2/2 pass),
but no CI job would notice their regression, and no CI job would notice a
finding reaching `zomc.cc:1548` even if the incident projection of B2 were
implemented.

**(b) `check-ownership-architecture.py --check` is named but is not wired into
CI.** `grep -n "check-ownership-architecture\|check-ir-architecture"
.github/workflows/CI.yml` returns nothing. Criterion 7 lists both. I ran
`check-ownership-architecture.py --check` locally (exit 0 — implementing the
projection as the RFC describes would *not* trip it), so the criterion is
locally satisfiable but is not enforced. Compare RFC 0047, which named
`check-diagnostic-coverage.py` and had it wired as a CI job; the evidence
standard the RFC implicitly claims to be comparable to is a *job*, not a local
command.

**(c) Criterion 4's "no output file" half is untested.** `tests/tools/check-object-emission.py:71-106`
has a `reject_case` helper that asserts non-zero exit **and** no artifact on
disk — exactly the shape needed. It is used only for the three *selector*
fail-closed shapes (`reject-fourfn-consumer`, `reject-twocaller-consumer`,
`reject-nonleaf-consumer`), none of which reaches
`LirStructuralVerifier::verify`. A verification finding produced today would
still exit non-zero with no file, so the behavior happens to hold — but it holds
by accident of the string-return path, and nothing pins it.

**Closes with (artifact):** add `object-emission-cli` and `native-run-cli` to
`check-backend-llvm`'s `-R` selection (or register the job to run
`ctest --test-dir build-llvm -L llvm`), add `check-ir-architecture` and
`check-ownership-architecture` as CI steps, and add one `--reject-case` style
fixture that drives a finding through emission and asserts no artifact. Absent
(a) and (b), criteria 5 and 7 are aspirational text.

---

## Non-Blocking Findings (NIT)

### N1. RFC 0047's evidence precedent is stronger than this RFC's, and the RFC does not say so

`docs/rfc/0047-diagnostics-architecture.md` § Test Plan (lines 551-565) names a
*product* step — "successful and failing `zomc compile --check` runs, an
invariant incident case, and an IDE snapshot diagnostic case" — and its LANDED
entry enumerates concrete counts (328/328, catalogue totals). RFC 0053's Test
Plan has no product/invariant-incident step and no counts. B2 and B4(c) are the
direct consequence. Not blocking on its own; it is the *reason* B2 and B4 exist.

### N2. Slice 4's status row claims evidence it does not describe

Line 615 asserts the `object-emission-cli` (seven shapes) and `native-run-cli`
ctests "cross the gate end to end". They do — I ran them — but they assert ELF
magic and host exit codes, which cannot distinguish "gate ran and passed" from
"gate never ran" (proved in the mutation above). The status row should say what
the ctest proves (`no behavior change`) and what it does not (`the stage
executed`), or the tests should be extended to prove execution.

### N3. `ConstantMismatch` and `PlaceMappingMismatch` return sites outnumber their tests by 5:1 and 6:1

`ConstantMismatch` has 6 return sites in `translation-validator.cc` and 2
mutation tests; `PlaceMappingMismatch` has 6 return sites and 2 mutation tests.
Per-tag coverage (criterion 3) holds; per-*check* coverage does not. A NIT
because the RFC only mandates the tag axis, but the mutation matrix's value is
proportional to return-site coverage.

### N4. Two translation fault tags are asserted with an `||`, so neither is pinned

`lir-translation-validator-test.cc:512-513`:

```cpp
ZC_EXPECT(fault == TranslationFaultKind::CallCalleeMismatch ||
          fault == TranslationFaultKind::FunctionSetMismatch);
```

The test comment justifies it (the caller is validated first), and
`lir-translation-validator-test.cc:516-557` does pin `CallCalleeMismatch`
unambiguously on a sibling-index module. So the tag *is* covered — but the test
named "rejects a call whose index resolves to the wrong owner" does not pin the
fault it names. Tighten to the expected tag.

### N5. `MirTerminatorKind::Unreachable` is admitted in the MIR algebra and silently rejected by the validator

`translation-validator.cc:582-583` returns `EffectMismatch`. The RFC's effect
table (lines 342-356) does not list `Unreachable` at all, so a reader cannot
learn from the RFC that this is the contract. Either add the row or state that
it is outside the admitted relation. NIT because no producer emits it today.

### N6. The `docs/design/ir/` note set uses inconsistent `Updated:` values

`lowering-and-verification.md` reads `Updated: 2026-09-14` although `d604d449`
modified it on 2026-09-19; `lir.md` reads `2026-09-19`. Folded into B2's
correction edit, so listed here only for completeness.

---

## Test Inventory (Requested)

### Unit tests covering the two new stages

Registered via the parent `tests/unittests/compiler/lir/CMakeLists.txt`
(`add_ztest_unit_tests_from_directory`), which inherits the label from
`cmake/utils/unittests.cmake:46-53` and the default `TIMEOUT 300` (`:64-67`).
Both are backend-independent and are in the CI `debug` lane.

| Test | Label | Direct | ctest | Assertions |
|---|---|---|---|---|
| `lir-verifier-test` | `unittest;lir` | 16/16 pass | #247 pass (0.07s) | 3 accept (scalar, call, diamond) + 13 reject |
| `lir-translation-validator-test` | `unittest;lir` | 14/14 pass | #246 pass (0.39s) | 3 accept (folded scalar, diamond, call pair) + 11 reject |

Backend-gated and **not** in any CI lane:

| Test | Test # in `build-llvm` | Stage assertions |
|---|---|---|
| `llvm-translation-test` | #77 | 22 direct calls to `verify`/`validate` across the folded scalar, aggregate field/whole-struct, diamond, loop, comparison diamond, zero- and one-argument calls |
| `object-emission-cli` | #66 | none (ELF magic only) — exercises the stages transitively |
| `native-run-cli` | #67 | none (host exit 42) — exercises the stages transitively |

The nine shapes named in the RFC's slice history map to
`llvm-translation-test` as follows: scalar initializer → :117, :415;
aggregate field → :368; whole-struct → :256, :294; conditional diamond → :523,
:855; while loop → :629; comparison diamond → :746; zero-arg call → :955;
one-arg call → :1057; three-function leaf → :1540. **The two-argument call
(`lowerCallModuleWithArguments`, `mir-to-lir.cc:1104`) has no stage assertion in
any test**, though it is in the `object-emission-cli` case list
(`call2arg-consumer`) and therefore crosses the gate at runtime.

### Negative-path evidence, per reject class

**Structural (13 tags):** complete on the tag axis — see the B3 table. Only the
`Call`-destination `CarrierMismatch` return site (`lir-verifier.cc:319`) is
untested.

**Translation (9 tags):** complete on the tag axis. Branch-level gaps
enumerated in B3.

**Production fail-closed:** **absent.** No test injects a fault into
`zomc compile --emit=binary`. This is the single largest evidence gap in the
RFC.

---

## Parity State (Requested)

| Channel | Invocation | Snapshot | Result |
|---|---|---|---|
| Process | `--check --zomc …` | `tests/coverage/corpus-process-parity.json` (default) | **RED** — 924/923 differences |
| IR | `--check --ir --zomc …` | none (default is the *process* snapshot) | **RED** — `snapshot-error: irChannel=None does not match --ir=True` |
| IR | `--check --ir --snapshot tests/coverage/corpus-ir-parity.json --zomc …` | explicit | **RED** — same 924 differences |

Snapshot provenance: recorded 2026-09-10 (`f5c45192` for process, `7075efb3` for
IR; 923 entries each, `irChannel` absent vs `true`). **48 commits** have landed
since, of which 5 are this RFC's.

Is the RFC consistent with this state? **Partly.** The RFC's *diagnosis* is
correct and is the RFC's best argument: byte parity is a drift detector, not a
correctness mechanism, and it cannot catch a defect shared by the old and new
implementation (lines 44-54, 496-499). That argument survives. What does not
survive is citing the 923-source channels as a live evidence line in
`Acceptance Criteria` item 5 and `Test Plan` while they are 100% red and one
channel is not runnable as written. The RFC must either re-record the baselines
at its own slice-4 commit (making criterion 5 meaningful) or drop the parity
clause from its evidence set entirely.

---

## Gate Enforcement Summary (Requested)

| Gate | Location | Would it catch removal of a verifier call? |
|---|---|---|
| `check-ir-architecture.py` | CI job? **No** | No. `grep` finds no `LirVerifier` marker; its 27 negative fixtures (`:456-673`) include `remove_once(files, CLI_SOURCE, "ir::linkAndPublish(")` for the *link* chain but nothing for LIR verification. **This is the natural home for the missing marker.** |
| `check-rfc.py` | CI job `check-rfc` | No — frontmatter/process only |
| `check-format.py` | CI job `check-format` | No (only accident; format-clean bypass passes) |
| `check-english-only.py` | CI job `check-english-only` | No |
| `check-ownership-architecture.py` | **not in CI** | No |
| `check-diff-hygiene.py` | CI job `check-diff-hygiene` | No |
| `object-emission-cli` / `native-run-cli` | **not in CI** | No (bit-identical pass with both stages bypassed) |
| `llvm-translation-test` | CI job `check-backend-llvm` | No — it calls `verify`/`validate` directly, so it survives their removal from `zomc.cc` |

**Recommended closure:** add a `zomc.cc` required marker to
`check-ir-architecture.py` (alongside the existing `ir::linkAndPublish(` marker
in `check_native_run_cutover`/`check_wiring`, `:277-289`) plus a negative
fixture that removes it, and run that gate as a CI step. This is the same
mechanism RFC 0043's D5 chain uses for `ExecutableImageInspector::inspect(` and
`ExecutableManifestVerifier::verify(`, so the precedent and the machinery both
already exist. Without it, and without B4(a), **no automated system in this
repository can detect the removal or weakening of either stage.**

---

## Required Edits Before This Can Be Signed Off

1. **B1** — name the `--ir` snapshot path; either re-record both baselines at
   the slice-4 commit with the revision and counts stated, or delete the parity
   clause from criterion 5 and correct line 51-52.
2. **B2** — implement the `IrFailurePhase::LirVerification` +
   `IrFailureKind::InvalidFact` projection and correct
   `docs/design/ir/lowering-and-verification.md:135-138`, **or** rewrite
   `Failure Projection`, criterion 4, `Integration Point` step 5,
   `Implementation Plan` step 1, and the 2026-09-19 status row to state that no
   projection is implemented.
3. **B3** — add the missing mutation tests; publish a measured coverage figure
   for both new files, or waive the >70% rule explicitly with a reason.
4. **B4** — wire `object-emission-cli`, `native-run-cli`, and
   `check-ownership-architecture` into CI; add a fault-injected emission
   fixture asserting non-zero exit and no artifact; add the `zomc.cc` verification
   marker and negative fixture to `check-ir-architecture.py`.
5. **N1-N6** — address or explicitly accept.

Verdict stands: **REQUEST-CHANGES**. The architecture is sound and the stages
are real. The evidence set around them is not yet what the RFC says it is.
