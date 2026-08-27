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

#include "zc/core/memory.h"
#include "zomlang/compiler/lir/lir-module.h"

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
};

}  // namespace zomlang::compiler::lir
