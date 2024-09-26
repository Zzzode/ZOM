#pragma once

#include <string>

enum class TokenType {
  IDENTIFIER,
  KEYWORD,
  NUMBER,
  STRING,
  OPERATOR,
  PUNCTUATION,
  END_OF_FILE
};

struct Token {
  TokenType type;
  std::string lexeme;
  int line;
  int column;

  Token(TokenType t, std::string l, int ln, int col)
      : type(t), lexeme(std::move(l)), line(ln), column(col) {}

  static Token createSentinel() { return {TokenType::END_OF_FILE, "", 0, 0}; }
};