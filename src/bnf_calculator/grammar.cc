#include "src/bnf_calculator/grammar.h"

namespace bnf_calculator {

void Grammar::addProduction(std::unique_ptr<Production> production) {
  if (production->lhs) {
    nonTerminals.insert(production->lhs->name);
    for (const auto& alternative : production->rhs) {
      for (const auto& symbol : alternative) {
        if (symbol->isTerminal) {
          terminals.insert(symbol->name);
        } else {
          nonTerminals.insert(symbol->name);
        }
      }
    }
  }
  rules.push_back(zc::mv(production));
}

void Grammar::setStartSymbol(const std::string& symbol) {
  startSymbol = symbol;
  nonTerminals.insert(symbol);
}

}  // namespace bnf_calculator