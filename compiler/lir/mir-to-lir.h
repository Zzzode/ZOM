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
  ZC_NODISCARD static zc::Maybe<LirModule> lowerScalarInitializer(
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
  ZC_NODISCARD static zc::Maybe<LirModule> lowerConditionalReturn(
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
  ZC_NODISCARD static zc::Maybe<LirModule> lowerLoopReturn(
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
  ZC_NODISCARD static zc::Maybe<LirModule> lowerEqualityConditionalReturn(
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
  ZC_NODISCARD static zc::Maybe<LirModule> lowerCallModule(
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
  ZC_NODISCARD static zc::Maybe<LirModule> lowerCallModuleWithArgument(
      const mir::MirFunction& caller, const mir::MirFunction& callee,
      const type::SemanticTypeStore& semanticTypes);
};

}  // namespace zomlang::compiler::lir
