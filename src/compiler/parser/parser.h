#pragma once
#include "src/compiler/lexer/token.h"
#include "src/compiler/stage.h"
#include "src/compiler/zis/zis.h"

class ConcurrentParser : public CompilerStage<Token, std::unique_ptr<ASTNode>> {
 protected:
  void process(const Token& input,
               std::vector<std::unique_ptr<ASTNode>>& outputs) override;

 public:
  ConcurrentParser() = default;
};