#pragma once

#include <string>
#include <vector>

#include "src/zom/lexer/lexer.h"
#include "src/zom/parser/parser.h"
#include "src/zom/typecheck/typechecker.h"

class CompilerPipeline {
  ConcurrentLexer lexer;
  ConcurrentParser parser;
  ConcurrentTypeChecker typeChecker;

 public:
  void process(const std::string& input);
  std::vector<std::string> getResults();
};