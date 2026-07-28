# RFC 0036 Review And Implementation Tracker

## Discussion Record

### 2026-07-28 R30-12M Exact-Bound Rejection

The RFC 0030 `R30-12M` candidate attempted to admit canonical diagnostic
sequences through the live diagnostic encoder and decoder.

Independent Binder and verification reviews rejected exact candidate:

- `stable-binding-codec.h`
  `ad0cbdd5266e603d70a978eb87589566610e18d5e02c7c6eb3a1442a66f057da`;
- `stable-binding-codec.cc`
  `452dce18b181e707881b0890cc9101e48e733db36d78868916a6679617c6459b`;
- `stable-binding-facts-test.cc`
  `3d35bf045692be00c9a66d6f9de02458ac162fe81b70cd9e096c830e79aecb8e`.

The diagnostic decoder applied a private 4,096-fact limit while the accepted
stable-binding field limit is 1,048,576. The encoder allocated the complete
payload before the caller could enforce 64 MiB. Binder-local repair was
rejected because diagnostics owns the canonical wire and ordering.

RFC 0030 source review remains paused after the exact approved `R30-12L`
candidate. The rejected `R30-12M` hashes carry no approval.

### 2026-07-28 Initial RFC 0036 Review Rejection

The first exact-hash review rejected the draft because the proposal and
tracker assigned different task identifiers to verification, source
publication, and the Binder bridge. Verification also found no observable
allocation seam, no exact-capacity canonical output mechanism, incomplete
offset and arithmetic boundaries, and insufficient dirty-worktree isolation.
Error-system review found that a short payload could declare the Binder
maximum fact count and cause disproportionate result reservation.

The revised design adds the identity-owned exact-capacity mode, explicit
resource injection, subtraction-first size checks, remaining-byte
feasibility checks, non-reserving collection decode, complete field
boundaries, and an isolated-index source transaction. The exact repository
baseline for this revised review is
`30bffa4a35cce9346491464341f79e843a6fb674`.

## Decision Record

Accepted by `rfc`, `module-system`, `error-system`, `lexer-parser`,
`binder-checker`, and `verification` against exact proposal SHA-256
`3bcf4ae97a7ca60228a9ff0f6fd1b99f4436cd467955a2123150fa147ef0321a`
and tracker SHA-256
`f6fe9ac34f0c98375d671b9090a3d41e80372bde011726cbee341830f3ec2098`.
The synchronized design-only transaction is
`rfc0036-accept-20260728-3bcf4ae9`.
Its recorded repository baseline is
`30bffa4a35cce9346491464341f79e843a6fb674`.

The transaction synchronizes RFCs 0030 and 0036, the RFC 0029 and RFC 0030
implementation trackers, this tracker, and the RFC index. It changes no
source, schema, native test, approved RFC 0030 predecessor, pending
`query-types.{h,cc}` file, or implementation status. Source work begins at
`R36-11`; the rejected `R30-12M` candidate remains unapproved.

## Review Tracker

| Task | Owner | Depends On | Deliverable | Verification | Status |
|---|---|---|---|---|---|
| `R36-01` | `rfc` | None | Complete RFC 0036, tracker, and RFC index row. | `python3 scripts/check-rfc.py` | Complete |
| `R36-02` | `rfc` | `R36-01` | Review dependency correction, public contract, prior art, rollout, and atomic boundaries. | Exact-hash review | Complete |
| `R36-03` | `module-system` | `R36-01` | Review exact-capacity canonical encoder ownership, invariants, and native evidence. | Exact-hash review | Complete |
| `R36-04` | `error-system` | `R36-01` | Review single diagnostic authority, preflight validation, canonical order, feasibility checks, and codec limits. | Exact-hash review | Complete |
| `R36-05` | `lexer-parser` | `R36-01` | Review source caller limits, resource choice, failure propagation, and parser evidence. | Exact-hash review | Complete |
| `R36-06` | `binder-checker` | `R36-01` | Review Binder limits, absence of duplicated grammar, and R30-12M dependency. | Exact-hash review | Complete |
| `R36-07` | `verification` | `R36-01` | Review boundary tests, resource observations, line caps, isolated-index assembly, build, and push gates. | Exact-hash review | Complete |
| `R36-08` | `rfc` | `R36-02`; `R36-03`; `R36-04`; `R36-05`; `R36-06`; `R36-07` | Record one unchanged proposal and tracker hash plus every owner decision. | RFC, English-only, internal-versioning, format, and diff gates | Complete; proposal `3bcf4ae97a7ca60228a9ff0f6fd1b99f4436cd467955a2123150fa147ef0321a`, tracker `f6fe9ac34f0c98375d671b9090a3d41e80372bde011726cbee341830f3ec2098` |
| `R36-09` | `rfc` | `R36-08` | Accept and publish the synchronized design-only transaction. | Local, upstream, and remote SHA parity | Complete; transaction `rfc0036-accept-20260728-3bcf4ae9` |

## Implementation Tracker

| Task | Owner | Depends On | Deliverable | Verification | Status |
|---|---|---|---|---|---|
| `R36-11` | `module-system` with `verification` review | `R36-09` | Add identity-owned exact-capacity canonical output and native invariant evidence; at most 400 changed source lines. | Focused canonical-encoder test | Complete; commit `6b92cfc65bf6c19dfc1591c8abe345d21aa28cda` |
| `R36-12` | `error-system` with `verification` review | `R36-11` | Replace the diagnostic fact codec API with explicit limits, resource injection, checked preflight, and encoder evidence; at most 400 changed source lines. | Focused diagnostic codec test | Complete; commit `6b92cfc65bf6c19dfc1591c8abe345d21aa28cda` |
| `R36-13` | `error-system` with `verification` review | `R36-12` | Complete bounded non-reserving decode, top-level and nested feasibility checks, exact re-encoding, and hostile-input evidence; at most 400 changed source lines from `R36-12`. | Focused diagnostic boundary matrix | Complete; commit `6b92cfc65bf6c19dfc1591c8abe345d21aa28cda` |
| `R36-14` | `lexer-parser` with `error-system` and `verification` review | `R36-13` | Replace every parser and canonical parsed-source caller with explicit source limits and resource choice; at most 400 changed source lines. | Focused parser tests and codec failure propagation | Complete; commit `6b92cfc65bf6c19dfc1591c8abe345d21aa28cda` |
| `R36-15` | `verification` | `R36-14` | Record the full accepted-governance baseline, build an isolated candidate index and tree from only the exact allowlist, materialize it in a detached clean temporary worktree with a fresh build directory, prove the complete changed and untracked path set, record `git write-tree`, and run sanitizer, focused identity, diagnostic, and parser tests, diagnostic coverage, format, and naming gates there. | Candidate tree SHA and project-native clean-assembly evidence | Complete; published transaction evidence for `6b92cfc65bf6c19dfc1591c8abe345d21aa28cda` |
| `R36-16` | `module-system` and `error-system` with `lexer-parser` and `verification` review | `R36-15` | Reconstruct the isolated index from the same baseline and exact approved path hashes, run its staged diff check, prove `git write-tree` equals the R36-15 tested candidate tree SHA, commit, push, and prove local/upstream/remote SHA parity. | Tested-tree identity, ASCII-English Conventional Commit, and immutable source transaction audit | Complete; commit `6b92cfc65bf6c19dfc1591c8abe345d21aa28cda` |
| `R36-17` | `binder-checker` with `error-system` and `verification` review | `R36-16`; RFC 0030 `R30-12L` | Repair and approve RFC 0030 `R30-12M` through the published bounded diagnostic API; at most 400 changed source lines from the approved R30-12L predecessor. | Stable result codec and 4,097-fact boundary matrix | Complete; commit `8885782747e4c863cefcb0d069bc4569cefce9aa` |
| `R36-18` | `rfc` | RFC 0042 `R42-16` | Synchronize truthful evidence and move RFC 0036 to LANDED after the bounded source codec and its canonical direct replacement are published. | RFC and SHA audit | Pending |

`R36-11` through `R36-17` are complete. The originally rejected `R30-12M`
candidate remains unapproved; the repaired bounded implementation is part of
the independently reviewed and published `8885782747e4c863cefcb0d069bc4569cefce9aa`
transaction. `R36-18` is the only remaining row.

## Source Transaction Allowlist

`R36-16` may stage only these paths from the exact approved predecessors:

- `products/zomlang/compiler/identity/canonical-encoder.h`
- `products/zomlang/compiler/identity/canonical-encoder.cc`
- `products/zomlang/tests/unittests/compiler/identity/canonical-encoder-test.cc`
- `products/zomlang/compiler/diagnostics/diagnostic-fact.h`
- `products/zomlang/compiler/diagnostics/diagnostic-fact.cc`
- `products/zomlang/tests/unittests/compiler/diagnostics/diagnostic-fact-codec-test.cc`
- `products/zomlang/compiler/parser/parse-source-query.cc`
- `products/zomlang/compiler/parser/canonical-parsed-source.cc`
- `products/zomlang/tests/unittests/compiler/parser/diagnostic-fact-caller-test.cc`

The source baseline is the accepted RFC 0036 governance commit that directly
precedes candidate assembly. `R36-15` records that full SHA, initializes an
isolated index with its tree, stages only the exact approved path hashes,
records `git write-tree`, and materializes that tree in a detached clean
temporary worktree with a fresh build directory. The union of tracked
differences and untracked paths must equal this allowlist exactly before any
native gate runs.

`R36-16` reconstructs the staged tree, proves its path list equals this
allowlist, runs the staged diff check, and proves `git write-tree` equals the
candidate tree SHA tested by `R36-15` before commit and push. No pending RFC
0030 source or `query-types.{h,cc}` path may enter the index, candidate
worktree, build input, or commit.
