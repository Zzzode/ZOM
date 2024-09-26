#pragma once

#include "src/compiler/lexer/lexer.h"
#include "src/compiler/parser/parser.h"
#include "src/compiler/typecheck/typechecker.h"
#include <string>
#include <vector>

class CompilerPipeline {
  ConcurrentLexer lexer;
  ConcurrentParser parser;
  ConcurrentTypeChecker typeChecker;

public:
  void process(const std::string& input);
  std::vector<std::string> getResults();
};