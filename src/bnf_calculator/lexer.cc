#include "src/bnf_calculator/lexer.h"

#include <cctype>

#include "src/zc/base/common.h"
#include "src/zc/utility/one_of.h"
#include "src/zom/diagnostics/diagnostic_engine.h"
#include "src/zom/source/location.h"

using namespace zom;

namespace bnf_calculator {

const std::unordered_map<char, TokenType> Lexer::SINGLE_CHAR_TOKENS = {
    {':', TokenType::ARROW},
    {'|', TokenType::OR},
    {'(', TokenType::LEFT_PAREN},
    {')', TokenType::RIGHT_PAREN},
    {'=', TokenType::ASSIGN}};

Lexer::Lexer(std::string input, diagnostics::DiagnosticEngine& diagnosticEngine)
    : input(zc::mv(input)),
      position(0),
      line(1),
      column(1),
      diagnosticEngine(diagnosticEngine) {}

std::vector<Token> Lexer::tokenize() {
  std::vector<Token> tokens;

  while (position < input.length()) {
    skipWhitespace();
    char current = peek();

    if (current == '\0') {
      tokens.push_back(Token(TokenType::END_OF_FILE, "", line, column));
      break;
    } else if (current == '<') {
      tokens.push_back(nonterminal());
    } else if (current == '"') {
      tokens.push_back(terminal());
    } else if (current == ':') {
      // Check for the ::= token
      advance();
      if (peek() == ':' && peek(1) == '=') {
        tokens.push_back(assign());
      } else {
        diagnosticEngine.emit(diagnostics::Diagnostic(
            diagnostics::DiagnosticSeverity::Error,
            "Unexpected character: " + std::string(1, current),
            source::SourceLoc("", line, column)));
      }
    } else if (SINGLE_CHAR_TOKENS.find(current) != SINGLE_CHAR_TOKENS.end()) {
      tokens.push_back(singleCharToken(SINGLE_CHAR_TOKENS.at(current)));
    } else if (std::isalnum(current)) {
      tokens.push_back(identifier());
    } else {
      diagnosticEngine.emit(diagnostics::Diagnostic(
          diagnostics::DiagnosticSeverity::Error,
          "Unexpected character: " + std::string(1, current),
          source::SourceLoc("", line, column)));
    }
  }
  return tokens;
}

ZC_HOT char Lexer::peek(int offset) const {
  size_t peekPosition = position + offset;
  if (peekPosition >= input.length()) return '\0';
  return input[peekPosition];
}

Token Lexer::assign() {
  size_t startLine = line;
  size_t startColumn = column;
  advance();  // Consume the first '='

  if (peek() == '>') {
    advance();  // Consume the '>'
    return Token(TokenType::ARROW, "::=", startLine, startColumn);
  }

  diagnosticEngine.emit(diagnostics::Diagnostic(
      diagnostics::DiagnosticSeverity::Error, "Expected '>' after '='",
      source::SourceLoc("", startLine, startColumn)));
  // Return a default token to avoid compilation error
  return Token(TokenType::ARROW, "", startLine, startColumn);
}

char Lexer::peek() const {
  if (position >= input.length()) return '\0';
  return input[position];
}

char Lexer::advance() {
  char c = peek();
  position++;
  column++;
  if (c == '\n') {
    line++;
    column = 1;
  }
  return c;
}

void Lexer::skipWhitespace() {
  while (position < input.length() && std::isspace(peek())) {
    advance();
  }
}

Token Lexer::identifier() {
  size_t start = position;
  size_t startColumn = column;
  while (position < input.length() && (std::isalnum(peek()) || peek() == '_')) {
    advance();
  }
  std::string value = input.substr(start, position - start);
  return Token(TokenType::IDENTIFIER, value, line, startColumn);
}

Token Lexer::nonterminal() {
  size_t start = position;
  size_t startColumn = column;
  advance();  // Skip '<'
  while (position < input.length() && peek() != '>') {
    advance();
  }
  if (peek() != '>') {
    diagnosticEngine.emit(diagnostics::Diagnostic(
        diagnostics::DiagnosticSeverity::Error, "Unterminated nonterminal",
        source::SourceLoc("", line, startColumn)));
  }
  advance();  // Skip '>'
  std::string value = input.substr(start, position - start);
  return Token(TokenType::NONTERMINAL, value, line, startColumn);
}

Token Lexer::terminal() {
  size_t start = position;
  size_t startColumn = column;
  advance();  // Skip opening quote
  while (position < input.length() && peek() != '"') {
    advance();
  }
  if (peek() != '"') {
    diagnosticEngine.emit(diagnostics::Diagnostic(
        diagnostics::DiagnosticSeverity::Error, "Unterminated string",
        source::SourceLoc("", line, startColumn)));
  }
  advance();  // Skip closing quote
  std::string value = input.substr(start, position - start);
  return Token(TokenType::TERMINAL, value, line, startColumn);
}

Token Lexer::singleCharToken(TokenType type) {
  std::string value(1, advance());
  return Token(type, value, line, column - 1);
}

}  // namespace bnf_calculator