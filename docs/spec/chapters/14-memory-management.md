# Chapter 14 - Ownership, Borrowing, and Cleanup

## 14.1 Scope

ZOM uses affine ownership with deterministic cleanup. Values are copied only
when their type has verified `Copy` evidence. Every other value is moved when
an operation consumes it. References borrow an existing value and never own
the referent. Raw pointers are non-owning unsafe addresses.

This chapter defines source-level ownership behavior. Stack placement, heap
placement, error-union layout, calling convention, and object-file emission are
implementation choices and do not change these rules.

ZOM does not define an implicit reference-counting ownership mode, a weak-
reference modifier, built-in `allocate` or `deallocate` functions, or a
distinguished `cleanup()` operation.

## 14.2 Value Transfer

The following contexts consume a value:

- initialization of an owning binding or field;
- assignment to an owning place;
- passing an argument to a value or move parameter;
- returning a value;
- constructing an owning aggregate; and
- an operation whose checked receiver mode is `Move`.

When the consumed type has verified `Copy` evidence, the operation copies the
value and the source remains initialized. Otherwise, the operation moves the
value and the source becomes uninitialized until it is assigned a new value.
Reading, borrowing, moving, or dropping an uninitialized place is a checker
error. Assignment to the complete place reinitializes it.

Move state applies to places, including field and index projections. Moving a
complete place invalidates its projections. Moving one projection invalidates
that projection and any overlapping place while leaving proven-disjoint
projections available.

`Linear` is independent of `Copy`. A type with verified `Linear` evidence
creates an exactly-once normal-path consumption obligation when initialized.
Moving the value transfers the obligation. Returning it or passing it to a
consuming operation discharges or transfers the obligation according to the
checked signature. Every normal exit must discharge each live linear
obligation exactly once.

Marker evidence and its structural derivation rules are defined in
[Chapter 3](03-types.md). The ownership checker consumes verified marker facts;
it does not infer copyability or linearity from spelling, storage location, or
nominal kind.

## 14.3 References and Borrows

`&T` is a shared reference and `&mut T` is a mutable reference. Both are
non-owning borrows whose referent must remain initialized for the complete
reference lifetime.

During one region:

- any number of shared loans may overlap;
- one mutable loan excludes every overlapping shared or mutable loan;
- a value cannot be moved from an overlapping place while a loan is active;
- a reference cannot outlive its referent; and
- returning or storing a reference is valid only when the referent outlives the
  destination region.

A reborrow creates a child loan. A mutable parent loan is suspended while an
overlapping mutable child loan is active and becomes usable again when the
child loan ends.

References are not nullable. Optional borrowed access uses the ordinary
nullable and union rules from [Chapter 3](03-types.md); nullability does not
extend a referent lifetime.

## 14.4 Raw Pointers and Unsafe Boundaries

`*const T` and `*mut T` are non-owning raw pointer types. Copying a raw pointer
copies only the address. It does not copy, move, retain, or extend the lifetime
of the pointee.

Dereferencing a raw pointer and every other operation classified by the
checker as an unsafe boundary requires an enclosing `unsafe` acknowledgement.
The acknowledgement does not prove that the address is valid, aligned, live,
or correctly typed. Code crossing that boundary is responsible for those
conditions.

Converting a reference to a raw pointer does not end the reference's loan and
does not authorize the pointer to outlive the referent. Converting a mutable
raw pointer to a const raw pointer preserves the same non-owning provenance.

## 14.5 Deinitialization

Classes and structures may declare one `deinit` body using the declaration
syntax in [Chapter 6](06-declarations.md). A deinitializer is not an ordinary
method: source code cannot call it directly, take it as a value, override it,
or select it through member lookup.

An initialized non-`Copy` value has one logical drop obligation. Ownership
checking determines where that obligation remains live. MIR cleanup
elaboration then inserts one explicit drop on every normal exit that owns the
obligation. A move transfers the obligation to the destination; it does not
run the deinitializer at the source. Reinitializing an owning place first drops
the previous initialized value when the checked operation is
`DropAndReplace`.

Aggregate cleanup runs in reverse initialization order. Only fields whose
initialization completed acquire drop obligations. A partially initialized
aggregate therefore drops exactly the initialized prefix in reverse order.

Error propagation, forced-unwrapping panic, ordinary return, loop exit, and
other control-flow edges carry explicit cleanup behavior in MIR. Panic unwind
cleanup exists only when the selected runtime and target provide the verified
unwind capability; aborting panic performs no unwind cleanup.

## 14.6 Ownership Diagnostics

Ownership diagnostics are checker-owned and use registered entries in
`zomlang/compiler/diagnostics/defs/diagnostics-checker.def`. The diagnostic
families include:

- use after move and the originating move;
- conflicting shared or mutable loans and the originating borrow;
- moving from a borrowed place;
- a borrow escaping its referent region;
- a linear value missing a normal-path consumption;
- a linear value consumed more than once;
- a scoped-task capture escaping its task region; and
- an unsafe raw-pointer boundary without acknowledgement.

The checker reports the primary source operation and preserves the originating
move, borrow, initialization, or referent as a structured secondary fact when
one exists. IR construction consumes only verified ownership facts and does not
reconstruct ownership decisions from AST syntax.

## 14.7 Conformance

Conformance must cover:

- copy versus move transfer and reinitialization;
- whole-place and projected-place invalidation;
- shared/mutable loan conflicts and non-overlapping places;
- reborrow suspension and restoration;
- returned, stored, aggregate, and closure-captured reference escapes;
- normal-path exactly-once linear consumption;
- raw-pointer operations inside and outside `unsafe`;
- deinitialization after return, propagation, replacement, and partial
  initialization; and
- panic abort versus unwind cleanup capability.

Parser or AST acceptance alone is not ownership conformance. A positive case
must pass the checker and every required lowering verifier; a negative case
must assert its registered diagnostic code.
