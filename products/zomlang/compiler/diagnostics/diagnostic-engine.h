#ifndef ZOM_DIAGNOSTIC_ENGINE_H_
#define ZOM_DIAGNOSTIC_ENGINE_H_

#include "zomlang/compiler/diagnostics/diagnostic-state.h"
#include "zomlang/compiler/diagnostics/diagnostic.h"

namespace zomlang {
namespace compiler {

namespace source {
class SourceManager;
}

class DiagnosticEngine {
public:
  explicit DiagnosticEngine(source::SourceManager& sourceMgr) : sourceMgr(sourceMgr) {}

  void addConsumer(zc::Own<DiagnosticConsumer> consumer) { consumers.add(zc::mv(consumer)); }

  void emit(const SourceLoc& loc, const Diagnostic& diagnostic) {
    if (diagnostic.getKind() == DiagnosticKind::kError) { state.setHadAnyError(); }
    for (auto& consumer : consumers) { consumer->handleDiagnostic(loc, diagnostic); }
  }

  ZC_NODISCARD bool hasErrors() const { return state.getHadAnyError(); }

  ZC_NODISCARD source::SourceManager& getSourceManager() const { return sourceMgr; }

  // 添加对 DiagnosticState 的访问方法
  DiagnosticState& getState() { return state; }
  ZC_NODISCARD const DiagnosticState& getState() const { return state; }

private:
  source::SourceManager& sourceMgr;
  zc::Vector<zc::Own<DiagnosticConsumer>> consumers;
  DiagnosticState state;
};

}  // namespace compiler
}  // namespace zomlang

#endif  // ZOM_DIAGNOSTIC_ENGINE_H_
