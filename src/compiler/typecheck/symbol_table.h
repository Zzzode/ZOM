#pragma once
#include <unordered_map>
#include <string>
#include <memory>

struct Symbol {
  std::string name;
  std::string type;
  // Add more properties as needed
};

class SymbolTable {
  std::unordered_map<std::string, std::unique_ptr<Symbol>> symbols;
public:
  void insert(std::string name, std::unique_ptr<Symbol> symbol) {
    symbols[std::move(name)] = std::move(symbol);
  }

  Symbol* lookup(const std::string& name) {
    auto it = symbols.find(name);
    return it != symbols.end() ? it->second.get() : nullptr;
  }
};