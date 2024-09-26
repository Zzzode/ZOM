#pragma once

#include <string>

#include "src/compiler/lexer/token.h"
#include "src/compiler/stage.h"

class ConcurrentLexer : public CompilerStage<std::string, Token> {
 protected:
  void process(const std::string& input, std::vector<Token>& outputs) override;

 public:
  ConcurrentLexer() = default;
};