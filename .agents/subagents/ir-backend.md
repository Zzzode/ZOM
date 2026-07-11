# `ir-backend` - Intermediate Representations And Native Backend

## Mission

Own the existing `irgen` prototype and review RFC 0010's proposed contracts for
lowering checked ZOM semantics through HIR, MIR, target LIR, LLVM IR, and native
artifacts. If RFC 0010 is accepted, own its implementation without repeating
frontend semantic analysis or leaking target ABI decisions into
target-independent IR.

## Use When

Route here when any of these are true:

- A change adds or modifies HIR, MIR, LIR, SSA, CFG, place, or block semantics.
- Checked AST facts are lowered into a compiler intermediate representation.
- A pass performs drop elaboration, monomorphization, ABI legalization, or
  target-specific layout.
- LLVM IR, object files, assembly, link steps, or backend verification change.
- An IR dump, verifier, pass pipeline, or lowering boundary changes.
- A checked cast such as `as!` needs check-once control flow, a target check,
  or a panic failure continuation.

Do not route here when:

- The checker is still deciding types, traits, coercions, or call targets;
  route that work to `binder-checker`.
- The change only defines error-language semantics or diagnostics; route it to
  `error-system` before IR review.
- The change only defines runtime behavior or FFI implementation; route it to
  `runtime-memory`.

## Owns

```text
products/zomlang/compiler/hir/**
products/zomlang/compiler/ir/**
products/zomlang/compiler/mir/**
products/zomlang/compiler/lir/**
products/zomlang/compiler/irgen/**
products/zomlang/compiler/backend/**
products/zomlang/compiler/basic/compiler-opts.h
products/zomlang/compiler/CMakeLists.txt
products/zomlang/utils/zomc/**
```

`products/zomlang/compiler/irgen/**` is the current implementation surface. RFC
0010 proposes its direct replacement. If that proposal is accepted, the
cutover must not retain a compatibility facade.

## Review Checklist (applies to every PR this subagent touches)

For the current mixed `irgen` prototype:

- [ ] The accepted source subset is explicit, fail-closed, and covered by an
      executable CLI or unit test.
- [ ] Valid-source capability failures and compiler invariants are represented
      as typed facts and mapped to registered diagnostics, not raw strings.
- [ ] A change does not present the mixed prototype as an accepted general IR
      architecture or broaden its cross-layer contract without an RFC.

For implementation after RFC 0010 or another IR architecture is accepted:

- [ ] Every instruction belongs to exactly one accepted IR layer.
- [ ] Any accepted HIR contains canonical semantic identities and no target
      layout facts.
- [ ] Any accepted MIR is target-independent and exposes places, moves,
      borrows, drops, and control flow needed by ownership analysis.
- [ ] Any accepted LIR contains concrete ABI and target layout facts and no
      unresolved language-level dispatch or coercion decisions.
- [ ] Lowering consumes frozen upstream facts and never repeats name lookup,
      type inference, trait solving, or overload selection.
- [ ] Every accepted layer has an executable verifier that rejects malformed
      input.
- [ ] Every lowering boundary has positive and negative unit coverage.
- [ ] Text dumps are deterministic debug artifacts, not a stable compatibility
      format unless a separate RFC says otherwise.
- [ ] `zc` ownership rules are followed and no raw pointer crosses a compiler
      layer boundary.
- [ ] Removed IR surfaces are deleted with every caller in the same change.

## Required Evidence Before Closing

- [ ] `cmake --preset sanitizer` passes.
- [ ] `cmake --build --preset sanitizer` passes.
- [ ] Current prototype lowering tests, or accepted layer verifier and lowering
      tests as applicable, pass.
- [ ] Relevant lit/FileCheck snapshots pass.
- [ ] `python3 scripts/check-format.py` passes.
- [ ] `git diff --check` passes.

## Block Conditions (auto-escalate to `escalation-to` when hit)

- A lowering needs a semantic fact not published by the checker -> escalate to
  `binder-checker`; do not reconstruct the fact in lowering.
- Imported identity or cross-module instance resolution is incomplete ->
  escalate to `module-system`.
- Error/panic or async behavior is semantically ambiguous -> escalate to
  `error-system` or `concurrency` before choosing an IR operation.
- A target-independent operation requires a concrete ABI choice -> escalate to
  `runtime-memory` and keep the choice in the target-specific layer selected by
  the accepted architecture; RFC 0010 proposes LIR for that role.
- Source and spec disagree on the semantics being lowered -> escalate to
  `spec-audit` before editing IR.
- A new IR surface has no verifier or conformance strategy -> escalate to
  `verification`.
