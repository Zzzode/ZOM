# RFC 0035 Review And Implementation Tracker

## Discussion Record

### 2026-07-28 R30-12I-C Dependency Rejection

The approved `R30-12H-C` candidate retains a complete
`identity::ImplIdentityRecord`. The next Binder codec must reconstruct that
record, but the live identity layer has no
`ImplIdentityRecord::decodeCanonical` operation and no recursive canonical
implementation-header decoder.

Implementing the grammar in `stable-binding-codec.cc` would duplicate identity
authority in Binder. Source work therefore pauses after the exact approved
`R30-12H-C` candidate and resumes only after this RFC's identity decoder
closure.

The review also found that RFC 0018's tracker describes canonical
implementation records as having strict decoders. That statement is true for
`DefinitionIdentityRecord` but not for `ImplIdentityRecord`; the acceptance
transaction corrects the evidence without changing RFC 0018's implementation
status.

## Decision Record

Accepted by `rfc`, `module-system`, `binder-checker`, and `verification`
against exact proposal SHA-256
`e79c292e8d3aefcce76d32923e566bc625e49b9b67d8bd1968fbd4b9620ee6c8`
and tracker SHA-256
`d50ec5efe5718d6eaa657463a348ac0956dd954174345d7b90c00d99d0f6ec9f`.
The synchronized design-only transaction is
`rfc0035-accept-20260728-e79c292e`.
Its recorded repository baseline is
`3cba781fdbcc3bb85a40787d2b4b8a73a0f39611`.

The transaction synchronizes RFCs 0030, 0034, and 0035, RFC 0018's evidence
tracker, the affected implementation trackers, and the RFC index. It changes
no source, schema, native test, immutable implementation base, pending
`query-types.{h,cc}` file, or implementation status. Source review resumes at
`R35-11`; RFC 0030 `R30-15` remains the only source commit and push.

## Review Tracker

| Task | Owner | Depends On | Deliverable | Verification | Status |
|---|---|---|---|---|---|
| `R35-01` | `rfc` | None | Complete RFC 0035, tracker, and RFC index row. | `python3 scripts/check-rfc.py` | Complete |
| `R35-02` | `rfc` | `R35-01` | Review governance, prior art, synchronization scope, and atomic landing preservation. | Exact-hash review | Complete |
| `R35-03` | `module-system` | `R35-01` | Review identity ownership, public API, private grammar, resource limits, and record admission. | Exact-hash review | Complete |
| `R35-04` | `binder-checker` | `R35-01` | Review Binder consumption boundary and absence of duplicated implementation-header grammar. | Exact-hash review | Complete |
| `R35-05` | `verification` | `R35-01` | Review task line caps, native mutation matrix, CMake wiring, clean landing set, and final gates. | Exact-hash review | Complete |
| `R35-06` | `rfc` | `R35-02`; `R35-03`; `R35-04`; `R35-05` | Record one unchanged proposal and tracker hash plus every owner decision. | RFC, English-only, internal-versioning, format, and diff gates | Complete; proposal `e79c292e8d3aefcce76d32923e566bc625e49b9b67d8bd1968fbd4b9620ee6c8`, tracker `d50ec5efe5718d6eaa657463a348ac0956dd954174345d7b90c00d99d0f6ec9f` |
| `R35-07` | `rfc` | `R35-06` | Accept and publish the synchronized design-only transaction. | Local, upstream, and remote SHA parity | Complete; transaction `rfc0035-accept-20260728-e79c292e` |
| `R35-08` | `rfc` | `R35-07` | Authorize source review to resume at `R35-11`. | Acceptance transaction audit | Complete |

## Implementation Tracker

| Task | Owner | Depends On | Deliverable | Verification | Status |
|---|---|---|---|---|---|
| `R35-11` | `module-system` with `verification` review | `R35-08`; RFC 0030 `R30-12H-C` | Add canonical name-root and name-reference decoders in `canonical-header-name.{h,cc}` plus native name evidence; at most 400 changed source lines. | Focused native name test and mutation matrix | Pending |
| `R35-12` | `module-system` with `verification` review | `R35-11` | Add the recursive depth core and scalar and unary canonical header type decoding in `canonical-header-type-decode.cc`; at most 400 changed source lines. | Exact tag, field, limit, and predecessor-hash review | Pending |
| `R35-13` | `module-system` with `verification` review | `R35-12` | Complete all aggregate canonical header type and subordinate record decoding, public type API, identity CMake wiring, and native all-variant evidence; at most 400 changed source lines. | Focused native type test and mutation matrix | Pending |
| `R35-14` | `module-system` with `verification` review | `R35-13` | Add generic, obligation, trait, and implementation-header decoders plus native implementation-header evidence; at most 400 changed source lines. | Focused native implementation-header test and mutation matrix | Pending |
| `R35-15` | `module-system` with `binder-checker` and `verification` review | `R35-14` | Add strict `ImplIdentityRecord::decodeCanonical`, complete record evidence, and exact re-encoding admission; at most 400 changed source lines. | Focused native definition-key test and mutation matrix | Pending |
| `R35-16` | `binder-checker` with `verification` review | `R35-15`; RFC 0030 `R30-12H-C` | Resume and approve RFC 0030 `R30-12I-C` using only the identity-owned record decoder. | RFC 0030 exact-file line cap and native evidence | Pending |
| `R35-17` | `rfc` | RFC 0030 `R30-15` | Synchronize truthful evidence and move RFC 0035 to LANDED only after the expanded atomic transaction is published. | RFC and SHA audit | Pending |

The `R35-08` design gate is satisfied. Source review may resume at `R35-11`;
no source implementation is declared complete by this tracker.
