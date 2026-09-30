// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and limitations under
// the License.

#pragma once

#include "compiler/lir/lir-module.h"
#include "zc/core/memory.h"

namespace zomlang::compiler::mir {
struct MirFunction;
}  // namespace zomlang::compiler::mir

namespace zomlang::compiler::type {
class SemanticTypeStore;
}  // namespace zomlang::compiler::type

namespace zomlang::compiler::lir {

/// \brief Minimal fail-closed MIR -> LIR lowering for one scalar module initializer.
///
/// This is the first vertical slice of RFC 0021 lowering. It admits exactly the
/// already-verified Built MIR shape that `mir::validScalarFunction` accepts: a
/// `ModuleInitializer` function with one source scope, one result local, one
/// block of `StorageLive` + `Assign(integer constant)` and a `Return` of that
/// local. It resolves the integer carrier width from the function result type's
/// primitive kind through the semantic type store, and materializes the constant
/// bit pattern from the canonical integer magnitude.
///
/// Every shape outside this slice (non-initializer functions, non-integer
/// results, multi-block bodies, non-constant returns, out-of-width constants)
/// returns `none`. No partial or best-effort LIR is ever produced.
class MirToLirLowering final {
public:
  /// \brief Lowers one scalar module initializer to a single-function LIR module.
  /// \param function Verified Built MIR function to lower.
  /// \param semanticTypes Session-owned type store that owns `function.resultType`.
  /// \return The lowered LIR module, or none when the function is outside the slice.
  ZC_NODISCARD static zc::Maybe<Module> lowerScalarInitializer(
      const mir::MirFunction& function, const type::SemanticTypeStore& semanticTypes);

  /// \brief Lowers one scalar string-return function to a single-function LIR module.
  ///
  /// Admits exactly the verified Built MIR shape that
  /// `mir::validScalarReturnFunction` accepts for a standalone function: a
  /// `Function` with no locals, one block with no statements, and a `Return` of
  /// a string constant. It resolves the opaque-pointer carrier from the `Str`
  /// result type through the semantic type store, copies the canonical UTF-8
  /// bytes into a `StringConstant`, and emits a `ReturnString` terminator. The
  /// function folds to the reserved no-argument `zom.module_init` entry so the
  /// runtime `_start` runs it. Every shape outside this slice returns `none`.
  ///
  /// \param function Verified Built MIR function to lower.
  /// \param semanticTypes Session-owned type store that owns `function.resultType`.
  /// \return The lowered LIR module, or none when the function is outside the slice.
  ZC_NODISCARD static zc::Maybe<Module> lowerScalarReturn(
      const mir::MirFunction& function, const type::SemanticTypeStore& semanticTypes);

  /// \brief Lowers one struct-local field-return function to a scalar LIR module.
  ///
  /// Admits exactly the verified Built MIR shape that
  /// `mir::validLocalAggregateFieldReturnFunction` accepts: a `Function` with one
  /// `UserLocal` of a struct type and a single block
  /// (`StorageLive(local); local = NominalAggregate{constant fields};
  /// return copy local.field`) whose result is one field's integer value. Because
  /// every aggregate element is a constant, the returned field is resolved at
  /// lowering time and emitted as the existing single-block integer-constant
  /// return; no struct is materialized in LIR. Every shape outside this slice
  /// returns `none`.
  ///
  /// \param function Verified Built MIR function to lower.
  /// \param semanticTypes Session-owned type store that owns `function.resultType`.
  /// \return The lowered LIR module, or none when the function is outside the slice.
  ZC_NODISCARD static zc::Maybe<Module> lowerAggregateFieldInitializer(
      const mir::MirFunction& function, const type::SemanticTypeStore& semanticTypes);

  /// \brief Lowers one whole-struct constant return function to LIR.
  ///
  /// Admits exactly the verified Built MIR shape that
  /// `mir::validLocalAggregateReturnFunction` accepts: a `Function` with one
  /// `UserLocal` of a struct type and a single block
  /// (`StorageLive(local); local = NominalAggregate{constant fields};
  /// return copy local`) whose result is the whole struct (the return place has
  /// zero projections). Each aggregate element must be a constant of an integer
  /// carrier; the elements lower in MIR element order to the slots of a
  /// `ReturnAggregate` terminator (RFC 0021 carrier bundle). That MIR element
  /// order is the source struct-literal property order, NOT the nominal type's
  /// declared field order (which the signature facts discard by a digest sort),
  /// and it makes no claim about the target ABI struct layout. Every shape
  /// outside this slice, and any non-constant or non-integer element, returns
  /// `none`.
  ///
  /// The lowered function's return carrier is the first slot's integer carrier as
  /// a transitional placeholder to satisfy the translator entry check; it is not
  /// the real return type, which the translator builds as a literal struct from
  /// the slot carriers themselves.
  ///
  /// \param function Verified Built MIR function to lower.
  /// \param semanticTypes Session-owned type store that owns the element types.
  /// \return The lowered LIR module, or none when the function is outside the slice.
  ZC_NODISCARD static zc::Maybe<Module> lowerAggregateReturn(
      const mir::MirFunction& function, const type::SemanticTypeStore& semanticTypes);

  /// \brief Lowers one four-block boolean-conditional return function to LIR.
  ///
  /// Admits exactly the verified Built MIR shape that
  /// `mir::validConditionalReturnFunction` accepts: a `Function` with one boolean
  /// parameter and an integer result, a four-block diamond
  /// (`entry: StorageLive(result); SwitchInt(bool param) -> then/else`;
  /// `then/else: Assign(result = arm); Goto(join)`; `join: Return(result)`)
  /// whose arms assign either an integer constant or a place-use of a parameter
  /// local. It lowers the `SwitchInt` to a `CondBranch` on the boolean parameter,
  /// the arm assigns to LIR `Assign` statements, and the place-use return to
  /// `ReturnLocal`. Every shape outside this slice returns `none`.
  ///
  /// \param function Verified Built MIR function to lower.
  /// \param semanticTypes Session-owned type store that owns the function types.
  /// \return The lowered LIR module, or none when the function is outside the slice.
  ZC_NODISCARD static zc::Maybe<Module> lowerConditionalReturn(
      const mir::MirFunction& function, const type::SemanticTypeStore& semanticTypes);

  /// \brief Lowers one reducible four-block while-loop return function to LIR.
  ///
  /// Admits exactly the verified Built MIR shape that
  /// `mir::validLoopReturnFunction` accepts: a `Function` with one boolean
  /// parameter and an integer result, a reducible four-block loop
  /// (`entry: StorageLive(result); Goto(header)`;
  /// `header: SwitchInt(bool param) -> body (true), exit (default)`;
  /// `body: Goto(header)` back-edge; `exit: Assign(result = literal);
  /// Return(result)`). It lowers the header `SwitchInt` to a `CondBranch` (true
  /// -> body, false -> exit), the entry/body `Goto`s to LIR `Goto`, the exit
  /// assign to a LIR `Assign`, and the place-use return to `ReturnLocal`. Every
  /// shape outside this slice returns `none`.
  ///
  /// \param function Verified Built MIR function to lower.
  /// \param semanticTypes Session-owned type store that owns the function types.
  /// \return The lowered LIR module, or none when the function is outside the slice.
  ZC_NODISCARD static zc::Maybe<Module> lowerLoopReturn(
      const mir::MirFunction& function, const type::SemanticTypeStore& semanticTypes);

  /// \brief Lowers one reducible four-block while-loop body return to LIR.
  ///
  /// Admits the verified Built MIR shape that
  /// `mir::validLoopBodyReturnFunction` accepts: a `Function` with a boolean
  /// condition parameter and an integer user local, a reducible four-block loop
  /// (`entry: StorageLive(local); Assign(local = constant, Initialize);
  /// Goto(header)`; `header: SwitchInt(bool param) -> body (true), exit
  /// (default)`; `body: N Overwrite assigns of the local; Goto(exit)` when a
  /// trailing break is present or `Goto(header)` back-edge otherwise; `exit:
  /// Return(place-use local)`). Each body assign lowers a constant, parameter,
  /// or local place-use to a LIR `Assign`, and an Arithmetic or Comparison
  /// rvalue to a LIR `Arithmetic` or `Compare` statement. The header
  /// `SwitchInt` lowers to a `CondBranch`; the place-use return lowers to
  /// `ReturnLocal`. Every shape outside this slice returns `none`.
  ///
  /// \param function Verified Built MIR function to lower.
  /// \param semanticTypes Session-owned type store that owns the function types.
  /// \return The lowered LIR module, or none when the function is outside the slice.
  ZC_NODISCARD static zc::Maybe<Module> lowerLoopBodyReturn(
      const mir::MirFunction& function, const type::SemanticTypeStore& semanticTypes);

  /// \brief Lowers one four-block comparison-driven conditional return to LIR.
  ///
  /// Admits the verified Built MIR shape that
  /// `mir::validEqualityConditionalReturnFunction` accepts: a `Function` with N
  /// integer parameters, an integer result local, and a boolean temporary; a
  /// four-block diamond whose entry computes `temp = (a CMP b)` with a
  /// `Comparison` rvalue over parameter/constant operands and switches on the
  /// temp, whose arms assign integer constants, and whose join returns the
  /// result. It lowers the comparison to a LIR `Compare` statement, the
  /// `SwitchInt` on the temp to a `CondBranch` on the temp local, and the
  /// place-use return to `ReturnLocal`. Non-constant arms and every shape outside
  /// this slice return `none`.
  ///
  /// \param function Verified Built MIR function to lower.
  /// \param semanticTypes Session-owned type store that owns the function types.
  /// \return The lowered LIR module, or none when the function is outside the slice.
  ZC_NODISCARD static zc::Maybe<Module> lowerEqualityConditionalReturn(
      const mir::MirFunction& function, const type::SemanticTypeStore& semanticTypes);

  /// \brief Lowers K leading scalar locals followed by a comparison conditional
  /// to a single-function LIR module.
  ///
  /// Admits the verified shape of `let a: i32 = <lit/param/local>; ...; if (x
  /// CMP y) { return lt; } else { return le; }`: a parameter-local prefix, K
  /// integer `UserLocal` body locals each brought to life by one `StorageLive`
  /// plus an initializing `Assign` of a constant or zero-projection place, then
  /// a function-result local and a bool comparison temporary. The entry block
  /// holds the K preamble pairs before the comparison temp assignment and
  /// `SwitchInt`; lowering otherwise matches `lowerEqualityConditionalReturn`.
  /// The leading locals, result, parameters, and arm constants share one
  /// non-one-bit integer carrier; the comparison temporary is Bit1.
  ///
  /// \param function Verified Built MIR function to lower.
  /// \param semanticTypes Session-owned type store that owns the function types.
  /// \return The lowered LIR module, or none when the function is outside the slice.
  ZC_NODISCARD static zc::Maybe<Module> lowerLeadingLocalConditionalReturn(
      const mir::MirFunction& function, const type::SemanticTypeStore& semanticTypes);

  /// \brief Lowers a four-block ternary-initialized local return to a
  /// single-function LIR module.
  ///
  /// Admits the verified Built MIR shape of `let a: T = ...; ...; let r: T =
  /// cond ? lt : le; return r;`: a parameter-local prefix, K leading integer
  /// `UserLocal` body locals each brought to life by one `StorageLive` plus an
  /// initializing `Assign` of a constant or zero-projection place, then one
  /// `UserLocal` holding the ternary result. The entry block holds the K
  /// preamble pairs plus one `StorageLive` for the ternary local before a
  /// `SwitchInt` on a bool parameter or earlier local; the two arm blocks each
  /// assign the ternary local a constant and jump to the join; the join returns
  /// the ternary local. Every carrier is an integer width; the condition is
  /// Bit1. The function folds to the reserved no-argument `zom.module_init`
  /// entry when it has no parameters, and keeps the parameterized
  /// `zom.ternary` symbol otherwise. Every other shape returns `none`.
  ///
  /// \param function Verified Built MIR function to lower.
  /// \param semanticTypes Session-owned type store that owns the function types.
  /// \return The lowered LIR module, or none when the function is outside the slice.
  ZC_NODISCARD static zc::Maybe<Module> lowerTernaryLocalReturn(
      const mir::MirFunction& function, const type::SemanticTypeStore& semanticTypes);

  /// \brief Lowers a one-block sequential integer arithmetic body to a
  /// single-function LIR module.
  ///
  /// Admits the verified Built MIR shape of `let z: T = a OP b; ... return z;`
  /// and the direct `return a OP b;` form: one `Function` scope, a prefix of
  /// integer parameter locals followed by one or more body locals
  /// (`UserLocal`, `Temporary`, or `FunctionResult`), one block whose
  /// statements are one `StorageLive` plus one initializing `Assign` per body
  /// local in order, and a place-copy return of the last local. Each assigned
  /// rvalue is a `Use` (an integer constant or a zero-projection parameter or
  /// earlier-body-local place) or an `Arithmetic` rvalue whose operands have the
  /// same shape; exponentiation (`Pow`) stays outside the slice. Every assigned
  /// and returned carrier is one equal non-one-bit integer width. The function
  /// folds to the reserved no-argument `zom.module_init` entry when it has no
  /// parameters, and keeps the parameterized `zom.arithmetic` symbol otherwise.
  /// Every other shape returns `none`.
  ///
  /// \param function Verified Built MIR function to lower.
  /// \param semanticTypes Session-owned type store that owns the function types.
  /// \return The lowered LIR module, or none when the function is outside the slice.
  ZC_NODISCARD static zc::Maybe<Module> lowerArithmeticReturn(
      const mir::MirFunction& function, const type::SemanticTypeStore& semanticTypes);

  /// \brief Lowers one scalar-local binary-overwrite-return function to LIR.
  ///
  /// Admits the verified Built MIR shape that
  /// `mir::validLocalBinaryOverwriteReturnFunction` accepts: one block with one
  /// user local brought to life by StorageLive plus an initializing Assign of a
  /// constant, then overwritten by an Arithmetic or Comparison rvalue whose
  /// operands are constants or place-uses of the parameter locals or the user
  /// local itself (`x = x + 1`), and a place-copy return of that local. Every
  /// carrier is one equal non-one-bit integer width. Every shape outside this
  /// slice returns `none`.
  ///
  /// \param function Verified Built MIR function to lower.
  /// \param semanticTypes Session-owned type store that owns the function types.
  /// \return The lowered LIR module, or none when the function is outside the slice.
  ZC_NODISCARD static zc::Maybe<Module> lowerScalarLocalOverwriteReturn(
      const mir::MirFunction& function, const type::SemanticTypeStore& semanticTypes);

  /// \brief Lowers one same-module zero-argument direct call to a two-function
  /// LIR module (caller plus its defined callee).
  ///
  /// Admits the verified Built MIR shape that
  /// `mir::validLocalCallReturnFunction` accepts for the caller (a `Function`
  /// with one integer result local and a two-block `Call` + `Return` body whose
  /// call takes zero arguments) together with a scalar constant-return callee
  /// (`mir::validScalarReturnFunction` shape: no locals, one block returning an
  /// integer constant). Both functions are *defined* in the emitted module, so
  /// the caller's call targets a real module-local function (a module-local
  /// index-derived symbol, the same documented boundary as the reserved
  /// module-initializer symbol); no external/synthetic callee symbol is invented.
  /// Every shape outside this slice returns `none`.
  ///
  /// \param caller Verified caller MIR function (the two-block Call+Return shape).
  /// \param callee Verified callee MIR function (the scalar constant-return shape).
  /// \param semanticTypes Session-owned type store that owns the function types.
  /// \return The lowered two-function LIR module, or none when outside the slice.
  ZC_NODISCARD static zc::Maybe<Module> lowerCallModule(
      const mir::MirFunction& caller, const mir::MirFunction& callee,
      const type::SemanticTypeStore& semanticTypes);

  /// \brief Lowers one same-module direct call passing a single integer-constant
  /// argument to a one-parameter callee that returns that parameter.
  ///
  /// Admits a caller that is the two-block Call+Return shape whose call carries
  /// exactly one integer-constant argument (destination is the caller's result
  /// local), together with a callee of the `mir::validParameterReturnFunction`
  /// single-parameter shape (one Parameter local, one block returning a place-use
  /// of that parameter). Both functions are defined in the emitted module; the
  /// caller's `Call` targets the module-local callee and threads the argument.
  /// Every shape outside this slice returns `none`. This is the first
  /// argument-carrying call lowering (RFC 0021 KR5.2); wider argument vectors and
  /// non-constant arguments are later steps.
  ///
  /// \param caller Verified caller MIR function (two-block Call+Return, one arg).
  /// \param callee Verified callee MIR function (single-parameter return shape).
  /// \param semanticTypes Session-owned type store that owns the function types.
  /// \return The lowered two-function LIR module, or none when outside the slice.
  ZC_NODISCARD static zc::Maybe<Module> lowerCallModuleWithArgument(
      const mir::MirFunction& caller, const mir::MirFunction& callee,
      const type::SemanticTypeStore& semanticTypes);

  /// \brief Lowers a same-module direct call passing two integer-constant
  /// arguments to a two-function LIR module.
  ///
  /// Admits the caller shape lowered by `lowerCallModuleWithArgument` (two blocks:
  /// entry Call, continuation Return; one result local) but with a two-argument
  /// call, and a callee with exactly two parameter locals and a single block that
  /// returns its first parameter. Both call arguments must be integer constants of
  /// the matching callee parameter types, and the call must target the identified
  /// callee. This is the first multi-argument object-emission slice (RFC 0021
  /// KR5.2 / RFC 0009 widening), capped at two arguments; every other shape and
  /// argument count returns none.
  ///
  /// \param caller Verified caller MIR function (two-block Call+Return, two args).
  /// \param callee Verified callee MIR function (two-parameter return shape).
  /// \param semanticTypes Session-owned type store that owns the function types.
  /// \return The lowered two-function LIR module, or none when outside the slice.
  ZC_NODISCARD static zc::Maybe<Module> lowerCallModuleWithArguments(
      const mir::MirFunction& caller, const mir::MirFunction& callee,
      const type::SemanticTypeStore& semanticTypes);

  /// \brief Lowers a by-value aggregate call to a two-function LIR module.
  ///
  /// Admits the verified pair: a caller
  /// `fun entry() -> T { let p: P = P { ..integer constants.. }; return f(p); }`
  /// (one aggregate-initialized `UserLocal`, one result `Temporary`, entry
  /// `StorageLive; p = NominalAggregate; StorageLive; Call(f, copy p) -> result`,
  /// continuation `return result`) and a callee
  /// `fun f(p: P) -> T { return p.<field>; }` (one struct `Parameter` local,
  /// single block returning a one-field projection of it). The nominal by-value
  /// argument is flattened in the admitted scalar pipeline: the caller aggregate
  /// elements (in source struct-literal order, the only field ordering this slice
  /// observes) lower to one integer call argument per field, and the callee's
  /// projected field selects the matching parameter slot. No struct is
  /// materialized; every non-integer element and every other shape returns
  /// `none`.
  ///
  /// \param caller Verified aggregate-caller MIR function.
  /// \param callee Verified by-value parameter-field callee MIR function.
  /// \param semanticTypes Session-owned type store that owns the function types.
  /// \return The lowered two-function LIR module, or none when outside the slice.
  ZC_NODISCARD static zc::Maybe<Module> lowerByValueAggregateCallModule(
      const mir::MirFunction& caller, const mir::MirFunction& callee,
      const type::SemanticTypeStore& semanticTypes);

  /// \brief Lowers a scalar-local direct call to a two-function LIR module.
  ///
  /// Admits the verified pair: a caller
  /// `fun entry() -> i32 { let a: i32 = <integer constant>; return f(a); }`
  /// (one scalar `UserLocal` initialized from a constant, one result
  /// `Temporary`, entry `StorageLive; a = Use(constant); StorageLive;
  /// Call(f, copy a) -> result`, continuation `return result`) and a callee
  /// `fun f(x: i32) -> i32` whose single block either returns its parameter
  /// directly or computes one admitted arithmetic/use body over it. The
  /// by-value scalar argument lowers to one integer call argument; the caller
  /// folds to the reserved no-argument `zom.module_init` entry. Every other
  /// shape returns `none`.
  ///
  /// \param caller Verified scalar-local-caller MIR function.
  /// \param callee Verified one-parameter callee MIR function.
  /// \param semanticTypes Session-owned type store that owns the function types.
  /// \return The lowered two-function LIR module, or none when outside the slice.
  ZC_NODISCARD static zc::Maybe<Module> lowerScalarLocalCallModule(
      const mir::MirFunction& caller, const mir::MirFunction& callee,
      const type::SemanticTypeStore& semanticTypes);

  /// \brief Lowers a three-function module (a zero-argument direct call plus one
  /// standalone leaf) to a three-function LIR module.
  ///
  /// Admits the caller shape lowered by `lowerCallModule` (two blocks: entry
  /// zero-argument Call, continuation Return; one result local) targeting the
  /// identified callee, the scalar constant-return callee, and a third scalar
  /// constant-return `leaf` that is referenced by no call. Both `callee` and
  /// `leaf` share the scalar constant-return shape; the caller's `Call` targets
  /// the callee by owner match, never the leaf. The three functions emit in the
  /// fixed order `[caller, callee, leaf]`, so the caller's call index is the
  /// callee's emission-order position (1), independent of the MIR array order.
  /// This is the first three-function object-emission slice, strictly capped at a
  /// single caller, its one callee, and one standalone leaf; every other shape
  /// returns none.
  ///
  /// \param caller Verified caller MIR function (two-block zero-argument call).
  /// \param callee Verified callee MIR function (scalar constant-return shape).
  /// \param leaf Verified standalone MIR function (scalar constant-return shape).
  /// \param semanticTypes Session-owned type store that owns the function types.
  /// \return The lowered three-function LIR module, or none when outside the slice.
  ZC_NODISCARD static zc::Maybe<Module> lowerCallModuleWithLeaf(
      const mir::MirFunction& caller, const mir::MirFunction& callee, const mir::MirFunction& leaf,
      const type::SemanticTypeStore& semanticTypes);

  /// \brief Lowers one shared-receiver inherent method call module to LIR.
  ///
  /// Admits a two-function pair: a caller with one aggregate-initialized owner
  /// local, a shared borrow temporary, and a call result temporary (entry
  /// initializes the one-field owner slot, takes its address, calls the method
  /// with that pointer, and returns the result), and a Method-sourced callee
  /// with one shared-reference receiver parameter whose single block returns a
  /// scalar constant. The caller folds to the reserved `zom.module_init` entry
  /// symbol so the runtime `_start` runs it; the method emits as `zom.callee`.
  /// Mutable receivers, projected fields, non-scalar or multi-field owners, and
  /// every other shape return none.
  ///
  /// \param caller Verified caller MIR function (two-block shared receiver call).
  /// \param callee Verified method MIR function (constant return, receiver param).
  /// \param semanticTypes Session-owned type store that owns the function types.
  /// \return The lowered LIR module, or none when outside the slice.
  ZC_NODISCARD static zc::Maybe<Module> lowerReceiverCallModule(
      const mir::MirFunction& caller, const mir::MirFunction& callee,
      const type::SemanticTypeStore& semanticTypes);

  /// Lowers the verified caller-plus-method pair when the method is the
  /// four-block conditional shape: one shared receiver pointer parameter, one
  /// bool ordinary parameter, one FunctionResult local, and literal arms. The
  /// caller folds to the reserved module initializer and the callee lowers as a
  /// four-block `zom.callee`. Every other shape return none.
  ZC_NODISCARD static zc::Maybe<Module> lowerReceiverConditionalCallModule(
      const mir::MirFunction& caller, const mir::MirFunction& callee,
      const type::SemanticTypeStore& semanticTypes);

  /// \brief Lowers one shared-receiver self-call module to LIR.
  ///
  /// Admits a three-function chain: a module caller with one aggregate-
  /// initialized owner local, a shared borrow temporary, and a call result
  /// temporary (folding to `zom.module_init`); a forwarding Method whose leading
  /// receiver parameter is passed straight through to a zero-argument method
  /// (`return this.method();`, lowering to `zom.forwarder` with one call); and
  /// the leaf Method returning a scalar constant (`zom.leaf`). The caller calls
  /// index 1 and the forwarder calls index 2. Every other shape returns none.
  ZC_NODISCARD static zc::Maybe<Module> lowerReceiverSelfCallModule(
      const mir::MirFunction& caller, const mir::MirFunction& forwarder,
      const mir::MirFunction& leaf, const type::SemanticTypeStore& semanticTypes);

  /// \brief Lowers the discarded-mutating-call-then-shared-read receiver module.
  ///
  /// Admits a three-function module: a Function-sourced caller with one
  /// aggregate-initialized owner local, a mutable borrow temporary, an unread
  /// unit call-destination temporary, a shared borrow temporary, and a call
  /// result temporary across three blocks (a mutating unit call, then a shared
  /// value call, then the value return); a mutating Method (`voidCallee`) whose
  /// sole statement writes one ordinary parameter into the receiver field and
  /// whose terminator is a value-less unit Return; and a shared Method
  /// (`valueCallee`) returning the receiver field. The caller folds to
  /// `zom.module_init`, the unit method to `zom.setter`, and the read method to
  /// `zom.getter`. The MIR unit destination temporary is dropped by dense
  /// renumbering. Every other shape returns none.
  ZC_NODISCARD static zc::Maybe<Module> lowerReceiverVoidThenValueCallModule(
      const mir::MirFunction& caller, const mir::MirFunction& voidCallee,
      const mir::MirFunction& valueCallee, const type::SemanticTypeStore& semanticTypes);
};

}  // namespace zomlang::compiler::lir
