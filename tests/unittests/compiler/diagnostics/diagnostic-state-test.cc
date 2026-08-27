#include "compiler/diagnostics/core/diagnostic-state.h"

#include "zc/ztest/test.h"
#include "compiler/diagnostics/core/diagnostic-ids.h"

namespace zomlang {
namespace compiler {
namespace diagnostics {

ZC_TEST("DiagState DefaultConstruction") {
  DiagnosticState state;
  ZC_EXPECT(!state.isDiagnosticIgnored(DiagID::InvalidCharacter));
  ZC_EXPECT(!state.isDiagnosticIgnored(DiagID::UnterminatedString));
}

ZC_TEST("DiagState IgnoreDiagnostic") {
  DiagnosticState state;
  state.ignoreDiagnostic(DiagID::InvalidCharacter);
  ZC_EXPECT(state.isDiagnosticIgnored(DiagID::InvalidCharacter));
  ZC_EXPECT(!state.isDiagnosticIgnored(DiagID::UnterminatedString));
  ZC_EXPECT(!state.isDiagnosticIgnored(DiagID::TypeCheckerTypeMismatch));
}

ZC_TEST("DiagState IgnoreOutOfBounds") {
  DiagnosticState state;
  state.ignoreDiagnostic(DiagID::InvalidCharacter);
  ZC_EXPECT(state.isDiagnosticIgnored(DiagID::InvalidCharacter));
}

ZC_TEST("DiagState MultipleIgnores") {
  DiagnosticState state;
  state.ignoreDiagnostic(DiagID::InvalidCharacter);
  state.ignoreDiagnostic(DiagID::UnterminatedString);
  state.ignoreDiagnostic(DiagID::TypeCheckerTypeMismatch);

  ZC_EXPECT(state.isDiagnosticIgnored(DiagID::InvalidCharacter));
  ZC_EXPECT(state.isDiagnosticIgnored(DiagID::UnterminatedString));
  ZC_EXPECT(state.isDiagnosticIgnored(DiagID::TypeCheckerTypeMismatch));
  ZC_EXPECT(!state.isDiagnosticIgnored(DiagID::MissingSemicolon));
}

}  // namespace diagnostics
}  // namespace compiler
}  // namespace zomlang
