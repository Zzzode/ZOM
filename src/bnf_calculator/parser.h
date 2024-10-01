#ifndef BNF_CALCULATOR_PARSER_H_
#define BNF_CALCULATOR_PARSER_H_

#include <memory>

#include "src/bnf_calculator/grammar.h"
#include "src/bnf_calculator/lexer.h"

namespace bnf_calculator {

class Parser {
 public:
  explicit Parser(const std::vector<Token>& tokens);
  std::unique_ptr<Grammar> parse();

 private:
  const std::vector<Token>& tokens;
  size_t currentToken;

  Token peek() const;
  Token advance();
  bool match(TokenType type);
  void expect(TokenType type);

  std::unique_ptr<Production> parseProduction();
  std::vector<std::unique_ptr<Symbol>> parseRHS();
  std::unique_ptr<Symbol> parseSymbol();
};

}  // namespace bnf_calculator

#endif  // BNF_CALCULATOR_PARSER_H