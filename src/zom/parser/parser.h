#pragma once
#include "src/zom/lexer/token.h"
#include "src/zom/stage.h"
#include "src/zom/zis/zis.h"

class ConcurrentParser : public CompilerStage<Token, std::unique_ptr<ASTNode>> {
 protected:
  void process(const Token& input,
               std::vector<std::unique_ptr<ASTNode>>& outputs) override;

 public:
  ConcurrentParser() = default;
};