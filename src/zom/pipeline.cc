#include "src/zom/pipeline.h"

void CompilerPipeline::process(const std::string& input) {
  lexer.pushInput(input);

  Token token = Token::createSentinel();
  while (lexer.getOutput(token)) {
    parser.pushInput(zc::mv(token));
    token = Token::createSentinel();
  }
  lexer.setDone();

  std::unique_ptr<ASTNode> node;
  while (parser.getOutput(node)) {
    typeChecker.pushInput(zc::mv(node));
  }
  parser.setDone();

  typeChecker.setDone();
}

std::vector<std::string> CompilerPipeline::getResults() {
  std::vector<std::string> results;
  std::string result;
  while (typeChecker.getOutput(result)) {
    results.push_back(zc::mv(result));
  }
  return results;
}