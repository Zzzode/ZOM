#include "src/zom/basic/pipeline.h"

#include "src/zc/core/common.h"

namespace zom {
namespace basic {

void CompilerPipeline::process(const zc::String& input) {
  // 重置状态
  stage = CompilationStage::kNotStarted;
  // ir_ = ir::IntermediateRepresentation();
  results.clear();

  // 添加输入到 SourceManager
  ZC_UNUSED unsigned bufferId = sourceMgr.addMemBufferCopy(input.asBytes());

  // 运行编译管道
  runLexer();
  if (diags.hasErrors()) return;

  runParser();
  if (diags.hasErrors()) return;

  runTypeChecker();
  if (diags.hasErrors()) return;

  generateResults();

  stage = CompilationStage::kCompilationComplete;
}

void CompilerPipeline::runLexer() {
  zc::Vector<lexer::Token> tokens;
  // lexer.Lex(tokens);
  // ir_.setTokens(zc::mv(tokens));
  stage = CompilationStage::kLexingComplete;
}

void CompilerPipeline::runParser() {
  // auto ast = parser.Parse(ir_.getTokens());
  // ir_.setAST(zc::mv(ast));
  stage = CompilationStage::kParsingComplete;
}

void CompilerPipeline::runTypeChecker() {
  // type_checker_.Check(ir_.getAST());
  stage = CompilationStage::kTypeCheckingComplete;
}

void CompilerPipeline::generateResults() {
  // 这里应该实现结果生成的逻辑
  // 例如，可以将 AST 或类型信息转换为字符串表示
  results.add(zc::str("Compilation completed successfully."));
  // 添加更多结果...
}

}  // namespace basic
}  // namespace zom
