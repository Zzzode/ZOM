// Copyright (c) 2024-2025 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations under
// the License.

#include "zomlang/compiler/diagnostics/diagnostic-state.h"

#include "zomlang/compiler/diagnostics/diagnostic-ids.h"

namespace zomlang {
namespace compiler {
namespace diagnostics {

DiagnosticState::DiagnosticState() {}

void DiagnosticState::ignoreDiagnostic(DiagID diagId) { ignoredDiagnostics.upsert(diagId, true); }

bool DiagnosticState::isDiagnosticIgnored(DiagID diagId) const {
  auto result = ignoredDiagnostics.find(diagId);
  return result.orDefault(false);
}

// CharSourceRange DiagnosticState::toCharSourceRange(const SourceManager& sm, SourceRange range) {
//   return sm.getCharSourceRange(range);
// }

// char DiagnosticState::extractCharAfter(const SourceManager& sm, SourceLoc loc) {
//   return sm.extractCharAfter(loc);
// }

}  // namespace diagnostics
}  // namespace compiler
}  // namespace zomlang