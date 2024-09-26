#include "src/compiler/pipeline.h"

void CompilerPipeline::process(const std::string& input) {
  lexer.pushInput(input);

  Token token = Token::createSentinel();
  while (lexer.getOutput(token)) {
    parser.pushInput(std::move(token));
    token = Token::createSentinel();
  }
  lexer.setDone();

  std::unique_ptr<ASTNode> node;
  while (parser.getOutput(node)) {
    typeChecker.pushInput(std::move(node));
  }
  parser.setDone();

  typeChecker.setDone();
}

std::vector<std::string> CompilerPipeline::getResults() {
  std::vector<std::string> results;
  std::string result;
  while (typeChecker.getOutput(result)) {
    results.push_back(std::move(result));
  }
  return results;
}