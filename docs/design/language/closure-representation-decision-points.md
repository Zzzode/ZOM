# Closure Representation: Decision Points

Updated: 2026-11-03

## Status

Non-normative design input. No closure representation is implemented in HIR,
MIR, LIR, or the runtime. This note records the current compiler state, the
representation decisions an owner must make, and the smallest first slice each
decision admits. It precedes an RFC; it is not itself one.

## What Already Exists

| Layer | Closure state |
|---|---|
| Parser | Anonymous function expressions, generic closures, a `use [...]` capture clause (`x` by value, `&x` by reference, `this`), and `raises`. |
| Binder | `DefinitionKind::Closure`; inferred free-variable facts and explicit capture facts with `ByValue` / `ByReference` / `This` modes. |
| Checker | `CheckedCaptureFact` per capture with `CaptureMode { SharedReference, MutableReference, Move, Copy }` and `CaptureOrigin { Explicit, Inferred }`; capture requirements are part of the verified body fact inventory. Closure bodies therefore resolve and type-check their captures today. |
| HIR | No closure value shape. No closure expression reaches HIR. |
| MIR / LIR | No closure value. LIR already has an `IndirectCall` terminator over a function-pointer slot, pointer carriers, and the dyn-erasure precedent (data pointer plus side table). |
| Runtime | Entry shim and host link only; no allocation ABI for callable values. |

The gap is entirely downstream of the checker: there is no physical value that
pairs code with captured state.

## Decision Point 1: Physical Representation

| Option | Shape | Industry precedent | Cost |
|---|---|---|---|
| A. Unboxed pair | Closure values are a concrete aggregate `{ function pointer, environment pointer }` passed directly, like any two-slot carrier. | Rust `Fn` fat pointers (code + env ref) | No heap ABI for borrowed/non-escaping closures; monomorphized call sites can inline the pair. Escaping closures still need a heap allocation somewhere. |
| B. Boxed environment | Every closure allocates one heap environment record; the value is one pointer to it and the code is selected by a discriminant or side table. | Go escaping closures | Uniform calling convention, but forces allocation for non-escaping closures and ties representation to an allocator contract the backend does not have yet. |
| C. Pointer-only first | A non-capturing closure is a bare function pointer; capturing closures are rejected until a later slice. | C++ stateless lambdas | Smallest possible first slice: zero new carrier, reuses `IndirectCall` directly. Does not answer the real question, only sequences it. |

Recommendation sequence: C first (a compileable, executable, non-capturing
closure slice with no representation commitment), then A for the first
capturing slice, with B considered only if escape analysis and the allocator
ABI later make the unboxed form more expensive than the boxed one. A keeps the
"no silent allocation" principle: a call-site-local non-escaping closure needs
no heap.

## Decision Point 2: Callable Type And Trait Coupling

ZOM has marker bounds but no `Fn` / `FnMut` / `FnOnce` traits today. The
capture modes already encode the same distinction (`Copy`/`SharedReference`
correspond to `Fn`, `MutableReference` to `FnMut`, `Move` to `FnOnce`).

- Bind closure types to three built-in callable traits from the start, and let
  ordinary `fn` items implement the by-reference one automatically. This is
  the Rust model and maps directly onto existing marker-bound and dispatch
  machinery, but it pulls trait-dispatch work into the closure slice.
- Alternatively, treat closure values as opaque structural function types
  (`fn(i32) -> i32`) for the non-capturing slice and defer all trait plumbing
  until capturing closures need mode-sensitive dispatch.

Recommendation: structural function types for slice C; introduce the callable
traits together with capturing closures, when the mode distinctions have to
affect dispatch.

## Decision Point 3: First Capturing Slice Scope

When representation A lands, admit captures in the same narrow order the
monomorphization work used:

1. `Copy`/by-value captures of the existing mono scalar carriers (`i32`,
   `i64`, `bool`) only, at a call-site-local closure invoked directly (never
   stored, returned, or passed across a function boundary). The environment is
   a local aggregate the caller fills; no allocation.
2. Then shared-reference captures, reusing the borrow classifier and the
   reference carriers.
3. Mutable-reference and move captures need the ownership overlay work from
   RFC 0007 and stay out of scope until that lands.

## Decision Point 4: Interaction With Monomorphization

Generic closures (`fn <T>(...)`) and closures instantiated at scalar carriers
should reuse the RFC 0056 instance pipeline rather than inventing a second
specialization path: a closure is an anonymous owner local, its code is a
`MirFunction`, and an instantiated closure is an instance whose symbol derives
from the same content-addressed identity scheme. This keeps one call-edge
identity model for both generic functions and generic closures.

## Walls That Remain Regardless Of The Decisions

- Closures stored in locals/structs, returned, or passed across module
  boundaries (escaping closures) need the boxed/allocator contract.
- Mutable-reference and move captures are blocked on the RFC 0007 ownership
  overlay completion.
- Coroutine/async closures and `raises` closures each add a calling convention
  dimension.
- The `this` capture is blocked on receiver-method lowering.
