#pragma once

#include "src/zom/stage.h"
#include "src/zom/typecheck/symbol_table.h"
#include "src/zom/zis/zis.h"

class ConcurrentTypeChecker
    : public CompilerStage<std::unique_ptr<ASTNode>, std::string> {
 protected:
  void process(const std::unique_ptr<ASTNode>& input,
               std::vector<std::string>& outputs) override;

 private:
  SymbolTable symbolTable;

 public:
  ConcurrentTypeChecker() = default;
};