#ifndef ZOM_DIAGNOSTICS_DIAGNOSTIC_STATE_H_
#define ZOM_DIAGNOSTICS_DIAGNOSTIC_STATE_H_

#include "zc/core/common.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/source/location.h"
#include "zomlang/compiler/source/manager.h"

namespace zomlang {
namespace compiler {

class DiagnosticState {
public:
  DiagnosticState();

  ZC_DISALLOW_COPY(DiagnosticState);

  DiagnosticState(DiagnosticState&&) = default;
  DiagnosticState& operator=(DiagnosticState&&) = default;

  // 控制诊断行为的标志
  bool getShowDiagnosticsAfterFatalError() const { return showDiagnosticsAfterFatalError; }
  void setShowDiagnosticsAfterFatalError(bool value) { showDiagnosticsAfterFatalError = value; }

  bool getSuppressWarnings() const { return suppressWarnings; }
  void setSuppressWarnings(bool value) { suppressWarnings = value; }

  // 忽略特定诊断
  void ignoreDiagnostic(uint32_t diag_id);
  bool isDiagnosticIgnored(uint32_t diag_id) const;

  // 错误追踪
  bool getHadAnyError() const { return hadAnyError; }
  void setHadAnyError() { hadAnyError = true; }

  // 辅助函数
  static CharSourceRange toCharSourceRange(const SourceManager& sm, SourceRange range);
  static char extractCharAfter(const SourceManager& sm, SourceLoc loc);

private:
  bool showDiagnosticsAfterFatalError = false;
  bool suppressWarnings = false;
  bool hadAnyError = false;
  zc::Vector<bool> ignoredDiagnostics;

  static constexpr uint32_t kNumDiags = 1000;  // 假设有1000个诊断ID
};

}  // namespace compiler
}  // namespace zomlang

#endif  // ZOM_DIAGNOSTICS_DIAGNOSTIC_STATE_H_
