#ifndef BNF_CALCULATOR_LEXER_H
#define BNF_CALCULATOR_LEXER_H

#include <string>
#include <unordered_map>
#include <vector>

#include "src/compiler/diagnostic/engine.h"

namespace bnf_calculator {

enum class TokenType {
  IDENTIFIER,
  TERMINAL,
  NONTERMINAL,
  ARROW,
  OR,
  EPSILON,
  LEFT_PAREN,
  RIGHT_PAREN,
  ASSIGN,  // New token type for `::=`
  END_OF_FILE
};

struct Token {
  TokenType type;
  std::string value;
  size_t line;
  size_t column;

  Token(TokenType t, std::string v, size_t l, size_t c)
      : type(t), value(std::move(v)), line(l), column(c) {}
};

class Lexer {
 public:
  Lexer(std::string input,
        compiler::diagnostic::DiagnosticEngine& diagnosticEngine);
  std::vector<Token> tokenize();

 private:
  std::string input;
  size_t position;
  size_t line;
  size_t column;
  compiler::diagnostic::DiagnosticEngine& diagnosticEngine;

  char peek() const;
  char peek(int offset) const;

  char advance();
  void skipWhitespace();
  Token identifier();
  Token terminal();
  Token nonterminal();
  Token singleCharToken(TokenType type);
  Token assign();  // New method to handle `::=`

  static const std::unordered_map<char, TokenType> SINGLE_CHAR_TOKENS;
};

}  // namespace bnf_calculator

#endif  // BNF_CALCULATOR_LEXER_H