#ifndef ZOM_DIAGNOSTICS_DIAGNOSTIC_STATE_H_
#define ZOM_DIAGNOSTICS_DIAGNOSTIC_STATE_H_

#include "src/zc/base/common.h"
#include "src/zc/containers/vector.h"
#include "src/zom/source/location.h"
#include "src/zom/source/manager.h"

namespace zom {
namespace diagnostics {

class DiagnosticState {
public:
  DiagnosticState();

  ZC_DISALLOW_COPY(DiagnosticState);

  DiagnosticState(DiagnosticState&&) = default;
  DiagnosticState& operator=(DiagnosticState&&) = default;

  // 控制诊断行为的标志
  bool show_diagnostics_after_fatal_error() const { return show_diagnostics_after_fatal_error_; }
  void set_show_diagnostics_after_fatal_error(bool value) {
    show_diagnostics_after_fatal_error_ = value;
  }

  bool suppress_warnings() const { return suppress_warnings_; }
  void set_suppress_warnings(bool value) { suppress_warnings_ = value; }

  // 忽略特定诊断
  void IgnoreDiagnostic(uint32_t diag_id);
  bool IsDiagnosticIgnored(uint32_t diag_id) const;

  // 错误追踪
  bool HadAnyError() const { return had_any_error_; }
  void SetHadAnyError() { had_any_error_ = true; }

  // 辅助函数
  static source::CharSourceRange ToCharSourceRange(const source::SourceManager& sm,
                                                   source::SourceRange range);
  static char ExtractCharAfter(const source::SourceManager& sm, source::SourceLoc loc);

private:
  bool show_diagnostics_after_fatal_error_ = false;
  bool suppress_warnings_ = false;
  bool had_any_error_ = false;
  zc::Vector<bool> ignored_diagnostics_;

  static constexpr uint32_t kNumDiags = 1000;  // 假设有1000个诊断ID
};

}  // namespace diagnostics
}  // namespace zom

#endif  // ZOM_DIAGNOSTICS_DIAGNOSTIC_STATE_H_