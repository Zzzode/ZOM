#ifndef ZOM_DIAGNOSTIC_ENGINE_H_
#define ZOM_DIAGNOSTIC_ENGINE_H_

#include "src/zom/diagnostics/diagnostic.h"
#include "src/zom/diagnostics/diagnostic-state.h"
#include "src/zom/source/manager.h"

namespace zom {
namespace diagnostics {

class DiagnosticEngine {
public:
  DiagnosticEngine(source::SourceManager& sourceMgr) : sourceMgr(sourceMgr) {}

  void addConsumer(zc::Own<DiagnosticConsumer> consumer) { consumers.add(zc::mv(consumer)); }

  void emit(const source::SourceLoc& loc, const Diagnostic& diagnostic) {
    if (diagnostic.getKind() == DiagnosticKind::kError) { state.setHadAnyError(); }
    for (auto& consumer : consumers) { consumer->handleDiagnostic(loc, diagnostic); }
  }

  bool hasErrors() const { return state.getHadAnyError(); }

  source::SourceManager& getSourceManager() { return sourceMgr; }

  // 添加对 DiagnosticState 的访问方法
  DiagnosticState& getState() { return state; }
  const DiagnosticState& getState() const { return state; }

private:
  source::SourceManager& sourceMgr;
  zc::Vector<zc::Own<DiagnosticConsumer>> consumers;
  DiagnosticState state;
};

}  // namespace diagnostics
}  // namespace zom

#endif  // ZOM_DIAGNOSTIC_ENGINE_H
