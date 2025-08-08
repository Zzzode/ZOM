#include "zomlang/compiler/basic/frontend.h"

#include "zc/ztest/test.h"
#include "zomlang/compiler/ast/ast.h"
#include "zomlang/compiler/basic/zomlang-opts.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/source/manager.h"

namespace zomlang {
namespace compiler {
namespace basic {

ZC_TEST("FrontendTest: PerformParseEmptySource") {
  source::SourceManager sourceMgr;
  diagnostics::DiagnosticEngine diagnosticEngine(sourceMgr);
  LangOptions langOpts;

  zc::String code = zc::str("");
  auto bufferId = sourceMgr.addMemBufferCopy(code.asBytes(), "test.zom");

  auto result = performParse(sourceMgr, diagnosticEngine, langOpts, bufferId);
  ZC_EXPECT(result != zc::none, "Parse result should have value");
}

ZC_TEST("FrontendTest: PerformParseSimpleExpression") {
  source::SourceManager sourceMgr;
  diagnostics::DiagnosticEngine diagnosticEngine(sourceMgr);
  LangOptions langOpts;

  zc::String code = zc::str("42");
  auto bufferId = sourceMgr.addMemBufferCopy(code.asBytes(), "test.zom");

  auto result = performParse(sourceMgr, diagnosticEngine, langOpts, bufferId);
  ZC_EXPECT(result != zc::none, "Parse result should not be null");
}

ZC_TEST("FrontendTest: PerformParseVariableDeclaration") {
  source::SourceManager sourceMgr;
  diagnostics::DiagnosticEngine diagnosticEngine(sourceMgr);
  LangOptions langOpts;

  zc::String code = zc::str("let x = 42;");
  auto bufferId = sourceMgr.addMemBufferCopy(code.asBytes(), "test.zom");

  auto result = performParse(sourceMgr, diagnosticEngine, langOpts, bufferId);
  ZC_EXPECT(result != zc::none, "Parse result should not be null");
}

ZC_TEST("FrontendTest: PerformParseFunctionDeclaration") {
  source::SourceManager sourceMgr;
  diagnostics::DiagnosticEngine diagnosticEngine(sourceMgr);
  LangOptions langOpts;

  zc::String code = zc::str("fun add(a: i32, b: i32) -> i32 { return a + b; }");
  auto bufferId = sourceMgr.addMemBufferCopy(code.asBytes(), "test.zom");

  auto result = performParse(sourceMgr, diagnosticEngine, langOpts, bufferId);
  ZC_EXPECT(result != zc::none, "Parse result should not be null");
}

}  // namespace basic
}  // namespace compiler
}  // namespace zomlang