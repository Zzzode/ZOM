#ifndef ZOM_DIAGNOSTIC_ENGINE_H_
#define ZOM_DIAGNOSTIC_ENGINE_H_

#include "src/zom/diagnostics/diagnostic.h"
#include "src/zom/diagnostics/diagnostic_state.h"
#include "src/zom/source/manager.h"

namespace zom {
namespace diagnostics {

class DiagnosticEngine {
public:
  DiagnosticEngine(source::SourceManager& sourceMgr) : sourceMgr_(sourceMgr) {}

  void AddConsumer(zc::Own<DiagnosticConsumer> consumer) { consumers_.add(zc::mv(consumer)); }

  void Emit(const source::SourceLoc& loc, const Diagnostic& diagnostic) {
    if (diagnostic.kind() == DiagnosticKind::kError) { state_.SetHadAnyError(); }
    for (auto& consumer : consumers_) { consumer->HandleDiagnostic(loc, diagnostic); }
  }

  bool HasErrors() const { return state_.HadAnyError(); }

  source::SourceManager& getSourceManager() { return sourceMgr_; }

  // 添加对 DiagnosticState 的访问方法
  DiagnosticState& getState() { return state_; }
  const DiagnosticState& getState() const { return state_; }

private:
  source::SourceManager& sourceMgr_;
  zc::Vector<zc::Own<DiagnosticConsumer>> consumers_;
  DiagnosticState state_;
};

}  // namespace diagnostics
}  // namespace zom

#endif  // ZOM_DIAGNOSTIC_ENGINE_H