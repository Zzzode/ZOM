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
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "zomlang/compiler/mir/built-mir.h"

namespace zomlang::compiler::ownership::facts {

/// \brief Returns true when the function's block topology is in the admitted flow subset.
///
/// The admitted subset is a reducible CFG whose blocks are connected by Call, Goto, and
/// SwitchInt terminators, ending in Return or Unreachable. Every Call has no unwind target
/// and every successor target resolves to an existing block. The successor graph covers
/// every block (no unreachable blocks). Joins are admitted: a block reached through another
/// path is expanded once by the flow derivation. Loops are admitted: a back edge is
/// accepted when its target dominates its source (a single-entry loop header). Irreducible
/// control flow (a retreating edge whose target does not dominate its source, i.e. a loop
/// with multiple entry points) is rejected. Dataflow analyses over the admitted subset
/// converge by fixpoint iteration.
bool isAdmittedFlowSubset(const mir::MirFunction& function);

}  // namespace zomlang::compiler::ownership::facts
