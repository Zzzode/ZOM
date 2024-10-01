#include <fstream>
#include <iostream>
#include <sstream>

#include "src/bnf_calculator/calculator.h"
#include "src/bnf_calculator/lexer.h"
#include "src/bnf_calculator/parser.h"
#include "src/zc/base/common.h"
#include "src/zc/utility/one_of.h"
#include "src/zom/diagnostics/diagnostic_engine.h"
#include "src/zom/source/location.h"

using namespace bnf_calculator;

void printSet(const std::string& setName, const Calculator::SetMap& set) {
  for (const auto& [symbol, symbolSet] : set) {
    std::cout << setName << "(" << symbol << ") = { ";
    for (const auto& item : symbolSet) {
      std::cout << item << " ";
    }
    std::cout << "}\n";
  }
  std::cout << std::endl;
}

int main(int argc, char* argv[]) {
  zom::diagnostics::DiagnosticEngine diagnosticEngine;

  zc::OneOf<Token, Lexer> test;
  test.init<Token>(Token(TokenType::END_OF_FILE, "", 0, 0));
  ZC_SWITCH_ONE_OF(test) {
    ZC_CASE_ONE_OF(token, Token) {}
    ZC_CASE_ONE_OF_DEFAULT {}
  }

  if (argc != 2) {
    diagnosticEngine.emit(zom::diagnostics::Diagnostic(
        zom::diagnostics::DiagnosticSeverity::Error,
        "Usage: " + std::string(argv[0]) + " <input_file>",
        zom::source::SourceLoc("", 0, 0)));  // Default location
    return 1;
  }

  std::string inputFilePath = argv[1];
  std::ifstream inputFile(inputFilePath);
  if (!inputFile) ZC_UNLIKELY {
      diagnosticEngine.emit(zom::diagnostics::Diagnostic(
          zom::diagnostics::DiagnosticSeverity::Error,
          "Unable to open input file.",
          zom::source::SourceLoc(inputFilePath, 0, 0)));
      return 1;
    }

  std::stringstream buffer;
  buffer << inputFile.rdbuf();
  std::string input = buffer.str();

  try {
    Lexer lexer(input, diagnosticEngine);
    auto tokens = lexer.tokenize();

    Parser parser(tokens);
    auto grammar = parser.parse();

    Calculator calculator(*grammar);

    auto firstSets = calculator.computeFirstSets();
    std::cout << "FIRST Sets:\n";
    printSet("FIRST", firstSets);

    auto followSets = calculator.computeFollowSets();
    std::cout << "FOLLOW Sets:\n";
    printSet("FOLLOW", followSets);

    auto selectSets = calculator.computeSelectSets();
    std::cout << "SELECT Sets:\n";
    for (const auto& [production, set] : selectSets) {
      std::cout << "Production: " << production << std::endl;
      printSet("SELECT", set);
    }

  } catch (const std::exception& e) {
    diagnosticEngine.emit(zom::diagnostics::Diagnostic(
        zom::diagnostics::DiagnosticSeverity::Error, e.what(),
        zom::source::SourceLoc(inputFilePath, 0, 0)));
  }

  // Check for errors and report them
  if (diagnosticEngine.hasErrors()) {
    return 1;
  }

  zc::OneOf<int, std::string> a;

  return 0;
}