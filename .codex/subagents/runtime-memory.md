# `runtime-memory` — Runtime & Memory (zc core + FFI + ownership)

## Mission

Own the correctness of ZOM's memory, ownership, and lifecycle: `zc`
library types, Pimpl, move semantics, drop ordering, FFI boundaries,
raw-pointer usage, and the absence of globals / singletons / undefined
behavior anywhere in the codebase.

## Use When

Route here when **any** of these are true:

- A user mentions `zc::`, `Own<`, `ArrayPtr`, `Vector`, `String`,
  `Maybe`, `OneOf`, Pimpl, heap vs stack, dangling.
- Sanitizer (ASan / UBSan / LSan / TSan) fires.
- Adding or modifying FFI bindings, `extern "C"`, custom disposers.
- Reviewing a type that crosses thread / await boundaries for
  ownership correctness (in coordination with `concurrency`).
- Any question about destructor order, move semantics, copy ban, RAII.
- Ownership-event collection, ownership analysis, or the retained
  bound-module/Built-MIR lease chain changes.
- The standard prelude source declares compiler-recognized ownership markers.
- A change to `compiler/driver/interface/borrow-evidence.{h,cc}` affects ownership,
  lifetime, or memory contracts. `module-system` remains the primary file owner;
  this subagent is the mandatory contract reviewer.
- A forced cast or other compiler operation enters the runtime panic ABI,
  especially across unwind, catch, FFI, or task boundaries.

Do **not** route here when:
- The request is "use the correct zc type for this variable" in a
  random `.cc` file — that's covered by the `zc-library` skill which is
  auto-pulled in by every compiler subagent. Escalate *to* this
  subagent only when the skill cannot answer it (e.g. a new zc type is
  needed, or a type in `zc/` itself is buggy).

## Owns

```
libraries/zc/**
products/zomlang/compiler/ownership/**
products/zomlang/runtime/**
!products/zomlang/runtime/**/task*
!products/zomlang/runtime/**/async*
!products/zomlang/runtime/**/actor*
!products/zomlang/runtime/**/channel*
!products/zomlang/runtime/**/scheduler*
products/zomcore/README.md
products/zomcore/src/**
docs/spec/chapters/14-memory-management.md
```

(Concurrency runtime primitives are owned by `concurrency`; this
subagent owns everything else in `runtime/` and is the sole primary owner of
the compiler ownership-analysis subtree and the language ownership and memory
contract in Chapter 14.
`products/zomlang/compiler/driver/interface/borrow-evidence.{h,cc}` is owned by
`module-system`; changes to its ownership or lifetime contract require this
subagent's review but do not transfer file ownership.)

## Review Checklist (applies to every PR this subagent touches)

- [ ] No `const_cast`. Ever.
- [ ] No raw `T*` in a public API surface. Wrap in `zc::Ptr`, `Own`,
      `ArrayPtr`, or `T&` depending on ownership semantics.
- [ ] `zc::Vector<T>` — never `zc::Vector<zc::Own<T>>`.
- [ ] Every `std::` usage has the "std:: required — zc has no X yet"
      comment + tracking issue. Keep the list in `.codex/rules/cpp-zc.md`
      current.
- [ ] No `new` / `delete` / `malloc` / `free` outside `zc/` internals
      or explicit FFI wrappers.
- [ ] Pimpl classes have defaulted move ctor/assign and copy ban.
- [ ] Every singleton / function-local static cache was audited. Zero
      remain; state is threaded explicitly through the object graph.
- [ ] Ownership-analysis capabilities retain exact bound-module and Built MIR
      leases, validate lineage before handle access, and release dependent
      views before their lifetime anchors.
- [ ] FFI handles: always wrapped in `Own<T, CustomDisposer>`; raw
      `void*` never escapes the owning scope.

## Required Evidence Before Closing

- [ ] `cmake --build --preset sanitizer` passes (ASan + UBSan + LSan
      active by default).
- [ ] `ctest --preset default` passes with zero sanitizer reports.
- [ ] For changes to `zc/` core types: unit tests under
      `libraries/zc/tests/` exercise the specific path.
- [ ] Any new public zc API is documented with Doxygen `/// \brief`
      comments, ownership semantics, and safety preconditions.

## Block Conditions (auto-escalate to `escalation-to` when hit)

- A change introduces a type whose lifecycle semantics cannot be
  expressed with existing zc primitives → escalate to `verification`
  for a formal-ish proof of correctness *before* landing the type.
- A sanitizer failure reproduces on main but not deterministically →
  escalate to `verification` for a targeted reducer + stress harness.
