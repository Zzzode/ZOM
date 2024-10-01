#ifndef BNF_CALCULATOR_CALCULATOR_H_
#define BNF_CALCULATOR_CALCULATOR_H_

#include <string>
#include <unordered_map>
#include <unordered_set>

#include "src/bnf_calculator/grammar.h"

namespace bnf_calculator {

class Calculator {
 public:
  explicit Calculator(const Grammar& grammar);

  using SymbolSet = std::unordered_set<std::string>;
  using SetMap = std::unordered_map<std::string, SymbolSet>;

  SetMap computeFirstSets();
  SetMap computeFollowSets();
  std::unordered_map<std::string, SetMap> computeSelectSets();

 private:
  const Grammar& grammar;
  SetMap firstSets;
  SetMap followSets;

  void initializeFirstSets();
  void initializeFollowSets();
  SymbolSet computeFirstSetOfSequence(
      const std::vector<std::unique_ptr<Symbol>>& sequence, size_t start = 0);

  bool isNullable(const std::string& symbol) const;
  bool isNullable(const std::vector<std::unique_ptr<Symbol>>& sequence,
                  size_t start = 0) const;

  std::string symbolSequenceToString(
      const std::vector<std::unique_ptr<Symbol>>& sequence) const;
};

}  // namespace bnf_calculator

#endif  // BNF_CALCULATOR_CALCULATOR_H