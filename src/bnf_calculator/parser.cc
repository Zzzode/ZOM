#include "src/bnf_calculator/parser.h"

#include <stdexcept>

namespace bnf_calculator {

Parser::Parser(const std::vector<Token>& tokens)
    : tokens(tokens), currentToken(0) {}

std::unique_ptr<Grammar> Parser::parse() {
  auto grammar = std::make_unique<Grammar>();

  while (currentToken < tokens.size() &&
         peek().type != TokenType::END_OF_FILE) {
    auto production = parseProduction();
    if (grammar->rules.empty()) {
      grammar->setStartSymbol(production->lhs->name);
    }
    grammar->addProduction(zc::mv(production));
  }

  return grammar;
}

std::unique_ptr<Production> Parser::parseProduction() {
  auto production = std::make_unique<Production>();

  production->lhs = parseSymbol();
  if (!production->lhs || production->lhs->isTerminal) {
    throw std::runtime_error(
        "Expected nonterminal on left-hand side of production");
  }

  expect(TokenType::ARROW);

  do {
    production->rhs.push_back(parseRHS());
    if (peek().type == TokenType::OR) {
      advance();  // Consume '|'
    } else {
      break;
    }
  } while (true);

  return production;
}

std::vector<std::unique_ptr<Symbol>> Parser::parseRHS() {
  std::vector<std::unique_ptr<Symbol>> symbols;
  while (peek().type != TokenType::OR &&
         peek().type != TokenType::END_OF_FILE) {
    symbols.push_back(parseSymbol());
  }
  return symbols;
}

std::unique_ptr<Symbol> Parser::parseSymbol() {
  Token token = advance();
  switch (token.type) {
    case TokenType::NONTERMINAL:
      return std::make_unique<Symbol>(token.value, false);
    case TokenType::TERMINAL:
      return std::make_unique<Symbol>(token.value, true);
    case TokenType::IDENTIFIER:
      return std::make_unique<Symbol>(token.value, true);
    default:
      throw std::runtime_error("Unexpected token: " + token.value);
  }
}

Token Parser::peek() const {
  if (currentToken >= tokens.size()) {
    return Token(TokenType::END_OF_FILE, "", 0, 0);
  }
  return tokens[currentToken];
}

Token Parser::advance() { return tokens[currentToken++]; }

bool Parser::match(TokenType type) {
  if (peek().type == type) {
    advance();
    return true;
  }
  return false;
}

void Parser::expect(TokenType type) {
  if (!match(type)) {
    throw std::runtime_error("Expected token type: " +
                             std::to_string(static_cast<int>(type)));
  }
}

}  // namespace bnf_calculator