#include "src/zom/diagnostics/diagnostic_state.h"

#include "src/zom/source/manager.h"

namespace zom {
namespace diagnostics {

DiagnosticState::DiagnosticState() : ignored_diagnostics_(kNumDiags) {}

void DiagnosticState::IgnoreDiagnostic(uint32_t diag_id) {
  if (diag_id < kNumDiags) {
    ignored_diagnostics_[diag_id] = true;
  }
}

bool DiagnosticState::IsDiagnosticIgnored(uint32_t diag_id) const {
  return diag_id < kNumDiags && ignored_diagnostics_[diag_id];
}

source::CharSourceRange DiagnosticState::ToCharSourceRange(
    const source::SourceManager& sm, source::SourceRange range) {
  return sm.GetCharSourceRange(range);
}

char DiagnosticState::ExtractCharAfter(const source::SourceManager& sm,
                                       source::SourceLoc loc) {
  return sm.ExtractCharAfter(loc);
}

}  // namespace diagnostics
}  // namespace zom