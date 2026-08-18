# `ir-backend` - Intermediate Representations And Native Backend

## Mission

Own the accepted RFC 0010 implementation boundary for lowering checked ZOM
semantics through verified HIR and Built MIR, and the future target LIR,
LLVM IR, and native artifacts. Do not repeat frontend semantic analysis or leak
target ABI decisions into target-independent IR.

## Use When

Route here when any of these are true:

- A change adds or modifies HIR, MIR, LIR, SSA, CFG, place, or block semantics.
- Checked AST facts are lowered into a compiler intermediate representation.
- A pass performs drop elaboration, monomorphization, ABI legalization, or
  target-specific layout.
- LLVM IR, object files, assembly, link steps, or backend verification change.
- The standard prelude build-tree or install-tree layout changes.
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
products/zomlang/compiler/backend/**
products/zomlang/compiler/basic/compiler-opts.h
CMakeLists.txt
CMakePresets.json
products/zomlang/compiler/CMakeLists.txt
products/zomlang/compiler/basic/CMakeLists.txt
products/zomlang/compiler/trace/CMakeLists.txt
products/zomlang/utils/CMakeLists.txt
products/zomlang/utils/zomc/**
products/zomcore/CMakeLists.txt
```

`compiler/hir`, `compiler/mir`, and `compiler/ir` are the only production IR
rail. Parallel IR models and compatibility facades are blockers.

## Review Checklist (applies to every PR this subagent touches)

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
- [ ] IR replacement work removes every superseded caller in the same change.

## Required Evidence Before Closing

- [ ] `cmake --preset sanitizer` passes.
- [ ] `cmake --build --preset sanitizer` passes.
- [ ] HIR, Built MIR, shared IR failure, target registry, verifier, codec
      oracle, and lowering tests pass as applicable.
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
