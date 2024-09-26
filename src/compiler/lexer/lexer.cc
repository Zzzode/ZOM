#include <cctype>

#include "lexer.h"

void ConcurrentLexer::process(const std::string& input,
                              std::vector<Token>& outputs) {
  // Simplified lexer implementation
  size_t pos = 0;
  int line = 1, column = 1;

  while (pos < input.length()) {
    if (std::isalpha(input[pos])) {
      size_t start = pos;
      while (pos < input.length() && std::isalnum(input[pos])) ++pos;
      outputs.emplace_back(TokenType::IDENTIFIER,
                           input.substr(start, pos - start), line, column);
      column += pos - start;
    } else if (std::isdigit(input[pos])) {
      size_t start = pos;
      while (pos < input.length() && std::isdigit(input[pos])) ++pos;
      outputs.emplace_back(TokenType::NUMBER, input.substr(start, pos - start),
                           line, column);
      column += pos - start;
    } else if (input[pos] == '\n') {
      ++line;
      column = 1;
      ++pos;
    } else {
      // Handle other token types...
      ++pos;
      ++column;
    }
  }

  outputs.emplace_back(TokenType::END_OF_FILE, "", line, column);
}