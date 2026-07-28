# RFC 0039 Review And Implementation Tracker

## Discussion Record

### 2026-07-28 R30-12O-D Dependency Rejection

The approved `R30-12N-D` candidate retains an `ExportSurfaceRevision` in
`StableModuleAliasFact`. The matching stable codec can decode the exact
32-byte `Sha256Digest`, but the owning type exposes no public operation that
constructs the typed revision from that digest.

`computeFramed(...)` requires the complete canonical export-surface preimage,
which is intentionally absent from the stable alias record. Binder layout
access, a fabricated preimage, or a duplicate raw digest would violate the
current value boundary. Source work therefore pauses after the exact approved
`R30-12N-D` candidate and resumes through this RFC.

The approved candidate hashes are:

- `stable-binding-facts.h`
  `3188c9da1358352bb59d5a97960f08ec34882d6b579189aca170f43c5d36f62d`;
- `stable-binding-facts.cc`
  `77e393fbc392ae1fddc746bc834d2e5dffc084b3644c4ee4ba8933fe95586461`;
- `stable-binding-facts-test.cc`
  `de85c991439dde10404e4d9c86567cdde18ada57862542e2f70379e7c4ee0b54`.

## Decision Record

Accepted by `rfc`, `binder-checker`, and `verification` against exact proposal
SHA-256
`de7ab2aa3e571b39aa4c67a48ab32ca219c2f74241fc72dc4ae3c89ffc35cd1a`
and tracker SHA-256
`253766beefaee323618cc9a589ea015258d19cba16a1cf5e285c39c23b8d7e8b`.
The synchronized design-only transaction is
`rfc0039-accept-20260728-de7ab2aa`. Its recorded repository baseline is
`95bf56028c95aca9e3b3e105c187dc07ed651a39`.

The transaction synchronizes RFCs 0030, 0037, and 0039, their trackers, and
the RFC index without changing source, schema, CMake, native test
registration, the immutable base, the approved `R30-12N-D` candidate, pending
`query-types.{h,cc}`, or RFC 0038. Publication completes `R39-07` and
authorizes source review to resume at `R39-11`.

## Review Tracker

| Task | Owner | Depends On | Deliverable | Verification | Status |
|---|---|---|---|---|---|
| `R39-01` | `rfc` | None | Complete RFC 0039, tracker, RFC 0030 and RFC 0037 synchronization, and RFC index row. | `python3 scripts/check-rfc.py` | Complete |
| `R39-02` | `rfc` | `R39-01` | Review governance, prior art, status truth, atomic landing preservation, and absence of compatibility behavior. | Exact-hash review | Complete |
| `R39-03` | `binder-checker` | `R39-01` | Review revision ownership, total typed admission, preimage boundary, and stable codec consumption. | Exact-hash review | Complete |
| `R39-04` | `verification` | `R39-01` | Review exact files, line cap, pre-registration compile evidence, expanded allowlist, and final native gates. | Exact-hash review | Complete |
| `R39-05` | `rfc` | `R39-02`; `R39-03`; `R39-04` | Record one unchanged proposal hash, tracker hash, and every owner decision. | RFC, English-only, internal-versioning, format, and diff gates | Complete; proposal `de7ab2aa3e571b39aa4c67a48ab32ca219c2f74241fc72dc4ae3c89ffc35cd1a`, tracker `253766beefaee323618cc9a589ea015258d19cba16a1cf5e285c39c23b8d7e8b` |
| `R39-06` | `rfc` | `R39-05` | Accept and publish one synchronized design-only transaction. | Local, upstream, and remote SHA parity | Complete; transaction `rfc0039-accept-20260728-de7ab2aa` |
| `R39-07` | `rfc` | `R39-06` | Authorize source review to resume at `R39-11`. | Acceptance transaction audit | Complete; transaction `rfc0039-accept-20260728-de7ab2aa` |

## Implementation Tracker

| Task | Owner | Depends On | Deliverable | Verification | Status |
|---|---|---|---|---|---|
| `R39-11` | `binder-checker` with `verification` review | `R39-07`; RFC 0030 `R30-12N-D` | Add `ExportSurfaceRevision::fromDigest` in `binding-metadata.{h,cc}` plus a digest-preservation assertion in `stable-binding-facts-test.cc`; at most 400 changed source lines. | C++23 ASan and UBSan `-Werror` syntax compilation plus exact-hash review; executable test after RFC 0030 `R30-13` | Pending |
| `R39-12` | `binder-checker` with `verification` review | `R39-11` | Resume and approve RFC 0030 `R30-12O-D` through the public typed admission operation. | RFC 0030 exact-file line cap and wire mutation evidence | Pending |
| `R39-13` | `rfc` | RFC 0030 `R30-15` | Synchronize truthful evidence and move RFC 0039 to LANDED only after the expanded atomic transaction is published. | RFC and SHA audit | Pending |

The `R39-07` design gate is satisfied. Source review may resume at `R39-11`;
no source implementation is declared complete by this tracker.
