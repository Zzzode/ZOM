#include "zomlang/compiler/diagnostics/diagnostic-state.h"

#include "zc/ztest/test.h"
#include "zomlang/compiler/diagnostics/diagnostic-ids.h"

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
  ZC_EXPECT(!state.isDiagnosticIgnored(DiagID::TypeMismatch));
}

ZC_TEST("DiagState IgnoreOutOfBounds") {
  DiagnosticState state;
  state.ignoreDiagnostic(DiagID::InvalidPath);  // Test with a valid DiagID
  ZC_EXPECT(state.isDiagnosticIgnored(DiagID::InvalidPath));
}

ZC_TEST("DiagState MultipleIgnores") {
  DiagnosticState state;
  state.ignoreDiagnostic(DiagID::InvalidCharacter);
  state.ignoreDiagnostic(DiagID::UnterminatedString);
  state.ignoreDiagnostic(DiagID::TypeMismatch);

  ZC_EXPECT(state.isDiagnosticIgnored(DiagID::InvalidCharacter));
  ZC_EXPECT(state.isDiagnosticIgnored(DiagID::UnterminatedString));
  ZC_EXPECT(state.isDiagnosticIgnored(DiagID::TypeMismatch));
  ZC_EXPECT(!state.isDiagnosticIgnored(DiagID::InvalidPath));
}

}  // namespace diagnostics
}  // namespace compiler
}  // namespace zomlang
