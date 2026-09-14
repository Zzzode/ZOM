# RFC 0050 Review And Implementation Tracker

## Discussion Record

### 2026-09-14 DRAFT Authored And DRAFT -> REVIEW

RFC 0050 (Cross-Stage IR Revision Identity Scope) moves to `REVIEW`. It
re-reviews the SHA-256 canonical-content revisions placed at in-memory IR
boundaries by RFCs 0010, 0013, 0015, and 0021 against their actual consumers
and against industry revision practice.

Why an RFC is required: the decision changes IR identity/lineage contracts
and potentially normative acceptance criteria and codecs across
`compiler/mir`, `compiler/lir`, and the ownership overlay; it also overlays
LANDED and IMPLEMENTING RFCs.

Audit evidence presented to reviewers:

- `MirRevisionCodec` is implemented and consumed in-process by the Built MIR
  verifier, ownership overlay lineage, and leases, and rendered by
  `--emit=mir`.
- Target/feature registry and borrow/checker revisions (RFCs 0010/0013/0015)
  are implemented with external-data or lineage consumers.
- `LirRevisionId` (RFC 0021) is not implemented; `AlgebraRevision` has a
  codec oracle but zero non-test consumers; `ReuseClass::Persisted` has zero
  descriptor rows.
- No revision is persisted; RFC 0010 explicitly forbids persistence use.

Three options are offered (retain and extend, re-scope to an in-process rule
with explicit deferrals, remove at in-process boundaries); the audit
recommends re-scope but implemented consumers make retain and remove
arguable, so the decision belongs to owners.

Frontmatter is `status: REVIEW`, `updated: 2026-09-14`, with `discussion` and
`tracking-issue` bound here. `approvers` is empty; no approval or
`REVIEW -> ACCEPTED` transition is recorded.

Frozen proposal snapshot (SHA-256 of the RFC document at REVIEW entry):

| Proposal SHA-256 | `5e4d0462b91d6c2f9578f889110201e69e38fa1d5b17a959b44021e5457f9d9d` |
|---|---|

## Owner Review Matrix

| Owner | Surface | Round 1 |
|---|---|---|
| `ir-backend` | MIR/LIR revision codecs, oracle inventories, verifier and lease usage, RFC 0010/0021 overlay effects | Pending |
| `module-system` | Borrow/checker lineage revisions and query runtime use of revisions and leases | Pending |
| `verification` | Oracle tests, corpus IR parity, mutation tests, regeneration policy | Pending |
| `rfc` | Process, supersession/overlay convention, owner completeness | Pending |

## Decision Record

TBD.

## Implementation Tracker

Not started; the RFC is not ACCEPTED.
