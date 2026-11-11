# RFC 0059 Review And Implementation Tracker

## Discussion Record

RFC 0059 was prompted by the closure narrowing track (RFC 0057 and the
non-capturing parts of RFC 0058) reaching the point where every HIR-only count
and parameter-key slice was exhausted. Re-measurement on `origin/develop` at
commit `dee3f5e` established that:

- All simple non-capturing scalar closure arms (bare reference, arithmetic,
  parameter-plus-literal, unary) already cover one or two declared parameters,
  an identifier on either parameter position, and repeated operands, in both
  expression-body lambda and block-body function-expression spellings.
- The three-parameter closure `(a, b, c) -> i32 => a + b + c` fail-closes on one
  clean `ZOM4099` with zero incidents. The HIR builder comment at
  `compiler/hir/build/hir-builder.cc:13883` names the reason: the closure
  parameter cap mirrors the LIR scalar call ladder width
  `lir::kMaxCallArguments`.
- The representations below the cap are already arity-agnostic:
  `MirCodePointerCallTerminator` stores a vector of operands, the LIR algebra
  codec frames sequences by element count, the LLVM translator derives the
  function type from carriers, and the SysV runtime bootstrap has no fixed
  argument assumption. The only hard walls are the HIR admission constants and
  the LIR factory, ladder, and validator arity predicates.

The RFC therefore proposes one coordinated bound raise rather than a new
operation or carrier, with explicit preservation of every zero-, one-, and
two-argument module.

## Decision Record

The technical gate was **APPROVED** on 2026-11-11. The change is limited to
named-constant widening plus predicate and validator generalization; the LIR
terminator representation, codec, LLVM translator, and runtime are not
modified, so length-framed encoding keeps existing modules byte-identical and
over-bound shapes stay fail-closed.

Adjudications on the three open questions:

1. **Bound increment:** raise the scalar width from two to three only in the
   first increment, not directly to six. This unblocks the motivating shape with
   the smallest step; any later raise remains a pure constant change with the
   same validation pattern.
2. **Aggregate field flattening:** continues to share `kMaxCallArguments` and
   does not gain a separate bound. Its existing tests must be re-run and remain
   green at widths up to two.
3. **Float ABI:** a carrier-specific width for floating point is explicitly
   deferred to future work and is out of scope for this RFC; only integer scalar
   carriers are admitted.

Approval conditions carried into implementation:

- Preserve every existing width zero-to-two assertion; only add bounded
  wider-width checks.
- Both corpus parity channels may only add or newly admit keys; every retained
  key hash must be unchanged, which would falsify the byte-stability claim if
  violated.
- A cross-layer test must prove the HIR closure bound never exceeds the LIR call
  bound.
- Implementation is local only; no push or merge, no capture/environment change,
  no stack-parameter lowering, no codec/runtime/LLVM change.

## Implementation Tracker

Status: ACCEPTED, implementation in progress. Milestone 1 (the three-parameter
bare parameter-reference arm) is implemented and verified end to end; the
three-parameter arithmetic, parameter-plus-literal, and unary body arms remain
on their existing ZOM4099 capability drain and are tracked as the next slice.

| Stage | Change | Status | Verification |
|---|---|---|---|
| 1 | RED tests and native fixtures for the three-parameter closure and over-bound rejection | Done for the bare parameter-reference arm | Three-parameter identity pos fixture plus a four-parameter over-bound neg fixture; pre-fix probes showed the value call hit an invariant incident and the over-bound shape drains one ZOM4099 |
| 2 | Raise `kMaxCallArguments` to three, widen LIR ladder predicates, generalize translation-validator arity matchers while keeping width zero-to-two assertions | Done; narrower than predicted | LIR ladder predicates and the translation-validator were already arity-generic (bounded predicates and per-argument loops), so only the named constant changed there. The real fixed-count wall was one stage earlier in MIR construction; see Milestone 1. LIR and validator suites green |
| 3 | Add cross-layer bound-equality test, raise the two HIR closure constants in lockstep | Done | HIR/LIR bound-equality ztest, LIR factory over-cap rejection ztest, and a compiler-session three-parameter closure ztest all green |
| 4 | Four-way conformance fixtures, native packages, parity snapshots, full gates | In progress | AST and diagnostics fixtures, a native-run package exiting 3, both parity channels re-recorded, full `ctest` and repo-lint run for the milestone; arithmetic-body widening is a later slice |

## Milestone 1: Three-Parameter Bare Parameter-Reference Arm (2026-11-11)

Scope delivered: a non-capturing closure with three declared `i32` parameters
whose sole body value is one declared parameter, in both the expression-body
paren-arrow lambda and the block-body function-expression spelling, reached
through a whole-local copy chain and an indirect code-pointer value call. The
programs `(a, b, c) => a`, `=> b`, and `=> c` called as `g(1, 2, 3)` execute
natively with exit codes 1, 2, and 3.

Root-cause correction to the RFC hypothesis. The RFC expected the fixed-count
wall below HIR to live in MIR-to-LIR lowering. Measurement placed it one stage
earlier, in Built MIR construction:
`compiler/mir/build/mir-builder.cc` `buildClosureValueCallReturn` still rejected
`constantArgumentCount > 2` with `zc::none`. After the HIR and LIR constants
were raised to three, a three-argument value call passed HIR verification, then
fell out of the recursive builder into the legacy rejection cascade and
surfaced as a `MirConstruction` `MissingRequiredFact` invariant incident (an
internal compiler error), not a clean capability drain. The fix bounds that
predicate with `hir::kMaxClosureValueCallArguments` so MIR admission equals HIR
admission. The MIR structural verifier, the length-framed codec, the LIR
factories, the translation-validator, and the LLVM translator were audited and
are all arity-generic; the LIR terminator representation, codec, LLVM
translator, and runtime were not modified.

Parity evidence and one documented deviation. Both corpus parity channels were
re-recorded (1144 to 1146 sources). Every retained key keeps its exit code, and
the IR channel shows zero `hirSha256` or `mirSha256` changes for every retained
key, so the RFC bitwise artifact guarantee (no canonical hash of an existing
artifact changes) holds in full. Three retained *process-channel* hashes do
change: the pre-existing three-parameter arithmetic, parameter-plus-literal,
and unary negative fixtures still reject with the same ZOM4099 code and exit
code 1 with zero incidents, but the diagnostic span moves from the whole
function (line 5, column 1) to the precise unsupported closure expression (line
6, column 11). This is an unavoidable consequence of admitting the
parameter-reference arm: the closure now passes the parameter-count gate and is
rejected later at the body-arm gate, which carries the closure-expression span.
The new span is more accurate and no accepted program's generated bytes change;
preserving the old coarse span would require front-loading body-shape decisions
into the count gate, an unjustified abstraction. The two new keys are the
admitted three-parameter identity fixture (exit 0, with HIR/MIR hashes) and the
four-parameter over-bound fixture (exit 1, no IR channel).

Remaining work. Widening the three-parameter arithmetic, parameter-plus-literal,
and unary body arms requires relaxing the matching HIR body-arm count
predicates and their verifier counterparts together; those shapes currently
drain cleanly and are the next independently verifiable slice. Float carrier
width and stack-argument lowering stay out of scope per the gate adjudications.
