#ifndef ZOM_DIAGNOSTICS_IN_FLIGHT_DIAGNOSTIC_H_
#define ZOM_DIAGNOSTICS_IN_FLIGHT_DIAGNOSTIC_H_

#include "zc/core/common.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/diagnostics/diagnostic.h"
#include "zomlang/compiler/source/location.h"

namespace zomlang {
namespace compiler {

class InFlightDiagnostic {
public:
  InFlightDiagnostic(DiagnosticEngine& engine, SourceLoc loc, Diagnostic&& diag)
      : engine_(&engine), loc_(loc), diag_(zc::mv(diag)), emitted_(false) {}

  // 添加移动构造函数和移动赋值运算符
  InFlightDiagnostic(InFlightDiagnostic&& other) noexcept = default;
  InFlightDiagnostic& operator=(InFlightDiagnostic&& other) noexcept = default;

  ZC_DISALLOW_COPY(InFlightDiagnostic);

  ~InFlightDiagnostic() {
    if (!emitted_) { emit(); }
  }

  void emit() {
    if (!emitted_) {
      engine_->emit(loc_, zc::mv(diag_));
      emitted_ = true;
    }
  }

  // Add methods to modify the diagnostic, e.g., add fix-its
  InFlightDiagnostic& addFixIt(const FixIt& fixit) {
    diag_.addFixIt(fixit);
    return *this;
  }

private:
  DiagnosticEngine* engine_;
  SourceLoc loc_;
  Diagnostic diag_;
  bool emitted_;
};

}  // namespace compiler
}  // namespace zomlang

#endif  // ZOM_DIAGNOSTICS_IN_FLIGHT_DIAGNOSTIC_H_
