#include "zomlang/compiler/diagnostics/diagnostic-state.h"

#include "zomlang/compiler/source/manager.h"

namespace zomlang {
namespace compiler {

DiagnosticState::DiagnosticState() : ignoredDiagnostics(kNumDiags) {}

void DiagnosticState::ignoreDiagnostic(uint32_t diagId) {
  if (diagId < kNumDiags) { ignoredDiagnostics[diagId] = true; }
}

bool DiagnosticState::isDiagnosticIgnored(uint32_t diagId) const {
  return diagId < kNumDiags && ignoredDiagnostics[diagId];
}

// CharSourceRange DiagnosticState::toCharSourceRange(const SourceManager& sm, SourceRange range) {
//   return sm.getCharSourceRange(range);
// }

// char DiagnosticState::extractCharAfter(const SourceManager& sm, SourceLoc loc) {
//   return sm.extractCharAfter(loc);
// }

}  // namespace compiler
}  // namespace zomlang
