#include "src/zom/basic/pipeline.h"

#include "src/zc/base/common.h"

namespace zom {
namespace basic {

void CompilerPipeline::Process(const zc::String& input) {
  // 重置状态
  stage_ = CompilationStage::kNotStarted;
  // ir_ = ir::IntermediateRepresentation();
  results_.clear();

  // 添加输入到 SourceManager
  ZC_UNUSED unsigned buffer_id = source_mgr_.AddMemBufferCopy(input.asBytes());

  // 运行编译管道
  RunLexer();
  if (diags_.HasErrors()) return;

  RunParser();
  if (diags_.HasErrors()) return;

  RunTypeChecker();
  if (diags_.HasErrors()) return;

  GenerateResults();

  stage_ = CompilationStage::kCompilationComplete;
}

void CompilerPipeline::RunLexer() {
  zc::Vector<lexer::Token> tokens;
  // lexer_.Lex(tokens);
  // ir_.setTokens(zc::mv(tokens));
  stage_ = CompilationStage::kLexingComplete;
}

void CompilerPipeline::RunParser() {
  // auto ast = parser_.Parse(ir_.getTokens());
  // ir_.setAST(zc::mv(ast));
  stage_ = CompilationStage::kParsingComplete;
}

void CompilerPipeline::RunTypeChecker() {
  // type_checker_.Check(ir_.getAST());
  stage_ = CompilationStage::kTypeCheckingComplete;
}

void CompilerPipeline::GenerateResults() {
  // 这里应该实现结果生成的逻辑
  // 例如，可以将 AST 或类型信息转换为字符串表示
  results_.add(zc::str("Compilation completed successfully."));
  // 添加更多结果...
}

}  // namespace basic
}  // namespace zom
