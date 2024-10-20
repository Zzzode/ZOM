#include "src/zom/diagnostics/diagnostic-state.h"

#include "src/zom/source/manager.h"

namespace zom {
namespace diagnostics {

DiagnosticState::DiagnosticState() : ignoredDiagnostics(kNumDiags) {}

void DiagnosticState::ignoreDiagnostic(uint32_t diagId) {
  if (diagId < kNumDiags) { ignoredDiagnostics[diagId] = true; }
}

bool DiagnosticState::isDiagnosticIgnored(uint32_t diagId) const {
  return diagId < kNumDiags && ignoredDiagnostics[diagId];
}

source::CharSourceRange DiagnosticState::toCharSourceRange(const source::SourceManager& sm,
                                                           source::SourceRange range) {
  return sm.getCharSourceRange(range);
}

char DiagnosticState::extractCharAfter(const source::SourceManager& sm, source::SourceLoc loc) {
  return sm.extractCharAfter(loc);
}

}  // namespace diagnostics
}  // namespace zom
