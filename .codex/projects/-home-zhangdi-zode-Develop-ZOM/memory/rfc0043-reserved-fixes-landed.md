---
name: rfc0043-reserved-fixes-landed
description: RFC 0043 KR5.3 owner-review RESERVED findings — four fixes landed+pushed, discovery correctly-deferred
metadata:
  type: project
---

2026-09-01: the KR5.3 RFC 0043 owner-gate review produced RESERVED findings; Codex
ruled them a fix backlog (not a sign-off). Worked in Codex's priority order,
per-slice discipline, all landed on origin/develop:

- G2 host-compatibility gate — `4a0acddd` (tautological host gate -> real artifact-vs-host compare via `artifactExecutionProfileFromInspection`/`currentHostExecutionProfile`)
- R1 failure materialization — `4eef1432` (zomc-local `materializeIrRejection` routes rejected link/publish `IrOperationResult` through the diagnostic engine; no new ZOMxxxx) + follow-up `ab15c079` (RecoveryRequired arm was dropping the primary rejection — snapshot arm's primary is non-optional; now routes snapshot mandatory + publication optional primary, keeps neutral string; also raised the adapter mapping-test loop bound to 0x13 to cover link phases and asserts BackendInvariant). Found by the 6-gate re-run (error-system + ir-backend both flagged it).
- environment resolve-or-remove — `1450e7e9` (delete dead empty-env `PreparedLinkInputs` plumbing; real empty env = `envPolicy(Empty)`) + `8873a957` (RFC 0043 §6/Linker Driver Invocation rewritten to strict-empty-environment contract)
- closure-path containment — `9ce36b07` (`ToolchainClosureRecord::make` now runs `normalizeInputSequence` over crtObjects/defaultLibraries: sysroot containment + canonical sort + dedup; single guard at the sole construction entry; golden LinkPlanId `54e60703...` unchanged)

Fifth RESERVED item — **production toolchain discovery unwired** — ruled
**correctly-deferred/tracked, NOT fixed**. `discoverToolchain`/`VerifiedSysroot`
(compiler/ir/toolchain-discovery.*) are contract-complete and 18-test-covered but
called only from tests; zomc.cc:~1680 hand-assembles the closure from the
build-pinned `ZOM_HOST_LINKER` (no sysroot model). Wiring discovery into production
would require a `ToolchainSearchSpec`/sysroot config source, which RFC 0043 §"...
configuration responsibility outside the verified compiler" (line ~264) explicitly
places outside the compiler and forbids resolving via ambient probe. So: do NOT
wire in (would need banned probe or a new cross-module config-source feature), do
NOT delete (contract-complete, not a stub). Report as deferred in Q4. See
[[q4-plan-stale-and-kr53-55-status]].

2026-09-01 continuation — KR5.3 owner sign-off governance round (all docs, pushed
through `0286b894` on develop): two six-owner gate rounds (at 9ce36b07 then at the
reconciled tree) found governance blockers (Owner Review Matrix self-contradiction,
invalidated snapshot binding, tracker/RFC drift, a §Host-Execution env overclaim,
a bare-ld-vs-compiler-driver mismatch). Fixed docs-first: `f4aae600` (RFC text:
Host Execution Inherit + field-5 + Status History), `850d02e3` (tracker
reconciliation), `cd1c97fb` (tracker pin provenance -> 2026-08-27 REVIEW snapshot
`55d2b60b` + Deferred Backlog section), `47734d8e` (RFC editorial: link-side empty
env vs run-child Inherit; ld -e _start + empty CRT), `0286b894` (tracker repin to
post-editorial RFC hash). Current RFC 0043 hash = `c2e366c8...`. The second gate
round returned unanimous RECOMMEND. **KR5.3 owner sign-off is NOT complete**: the
tracker records six re-approvals pending against `c2e366c8`; the gate RECOMMENDs
are evidence only, never a substitute for six real owner approvals — do NOT mark
Approved without Codex/owner authorization. Never self-sign owner approvals.

2026-09-01 CLOSED: Codex ruled (per 张地 standing directive) that a delegated
Codex acceptance is the accepting authority for Q4 governance closure — no
separate independent human owner sign-off required, but it must be labeled as
delegated-not-human. Recorded via `fb5ce8bb` (Re-Approval Evidence section: six
subagent gates RECOMMEND at HEAD 0286b894 / hash c2e366c8, evidence-only) and
`74da2cb2` (Owner Matrix + snapshot + q4 plan → "accepted 2026-09-01 on c2e366c8
under delegated Codex authority per the standing directive (not an independent
human owner approval)"; KR5.3 marked DONE, KR5.4 stays Candidate). RFC frontmatter
`approvers` and RFC body left untouched so the hash stays `c2e366c8` (no re-pin).
All pushed; origin/develop = `74da2cb2`. KR5.3 governance is closed. The six RFC
owners are subagent role specs in `.codex/subagents/`, not humans — so "owner
sign-off" here means delegated Codex acceptance, and that framing must be kept
explicit in any future RFC governance record.
