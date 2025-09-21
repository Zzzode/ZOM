// Copyright (c) 2024-2025 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations under
// the License.

#include "zomlang/compiler/symbol/symbol-table.h"

#include <cstdint>

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/map.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/symbol/package-symbol.h"
#include "zomlang/compiler/symbol/scope.h"
#include "zomlang/compiler/symbol/symbol-denotation.h"
#include "zomlang/compiler/symbol/symbol-flags.h"
#include "zomlang/compiler/symbol/symbol.h"
#include "zomlang/compiler/symbol/type-symbol.h"
#include "zomlang/compiler/symbol/value-symbol.h"

namespace zomlang {
namespace compiler {
namespace symbol {

// SymbolTable::Impl definition
struct SymbolTable::Impl {
  // Internal symbol storage
  zc::Vector<zc::Own<Symbol>> symbols;

  // Denotation storage
  zc::Vector<zc::Own<SymbolDenotation>> denotations;

  // Fast lookup by (name, scope) pair
  zc::HashMap<zc::String, zc::Vector<zc::Maybe<Symbol&>>> symbolsByName;

  // Denotation lookup by (name, scope) pair
  zc::HashMap<zc::String, zc::Vector<zc::Maybe<SymbolDenotation&>>> denotationsByName;

  // Implicit symbols by scope
  zc::HashMap<zc::String, zc::Vector<zc::Maybe<Symbol&>>> implicitsByScope;

  // Current scope for relative lookups
  zc::Maybe<const Scope&> currentScope = zc::none;

  // ScopeManager for managing scopes
  zc::Own<ScopeManager> scopeManager;

  // Current compilation phase
  uint32_t currentPhase = 0;

  // Symbol ID generation
  uint32_t nextSymbolId = 1;

  // Internal helpers
  zc::String makeKey(zc::StringPtr name, const Scope& scope) const {
    return zc::str(name, "@", scope.getName());
  }

  zc::String makeKey(const Symbol& symbol) const {
    ZC_IF_SOME(scope, symbol.getScope()) { return zc::str(symbol.getName(), "@", scope.getName()); }
    else { return zc::str(symbol.getName(), "@global"); }
  }

  void registerSymbol(Symbol& symbol) {
    zc::String key = makeKey(symbol);

    ZC_IF_SOME(existingList, symbolsByName.find(key)) { existingList.add(symbol); }
    else {
      zc::Vector<zc::Maybe<Symbol&>> newList;
      newList.add(symbol);
      symbolsByName.insert(zc::mv(key), zc::mv(newList));
    }
  }

  uint32_t generateSymbolId() { return nextSymbolId++; }
};

SymbolTable::SymbolTable() noexcept : impl(zc::heap<Impl>()) {
  impl->scopeManager = zc::heap<ScopeManager>();
}

SymbolTable::~SymbolTable() noexcept(false) = default;

SymbolTable::SymbolTable(SymbolTable&&) noexcept = default;
SymbolTable& SymbolTable::operator=(SymbolTable&&) noexcept = default;

VariableSymbol& SymbolTable::createVariable(zc::StringPtr name, const Scope& scope) {
  // Create a default type for testing purposes
  auto defaultType = BuiltInTypeSymbol::createUnit(SymbolId::create(impl->generateSymbolId()),
                                                   source::SourceLoc{});

  auto symbol = zc::heap<VariableSymbol>(SymbolId::create(impl->generateSymbolId()), name,
                                         SymbolFlags::Variable | SymbolFlags::TermKind,
                                         source::SourceLoc{}, zc::mv(defaultType));

  VariableSymbol& result = *symbol;
  result.setScope(scope);

  // Store in symbols collection
  impl->symbols.add(zc::mv(symbol));

  // Register for lookup
  impl->registerSymbol(result);

  return result;
}

FunctionSymbol& SymbolTable::createFunction(zc::StringPtr name, const Scope& scope) {
  // Create a default type for testing purposes
  auto defaultType = BuiltInTypeSymbol::createUnit(SymbolId::create(impl->generateSymbolId()),
                                                   source::SourceLoc{});

  auto symbol = zc::heap<FunctionSymbol>(SymbolId::create(impl->generateSymbolId()), name,
                                         SymbolFlags::Function | SymbolFlags::TermKind,
                                         source::SourceLoc{}, zc::mv(defaultType));

  FunctionSymbol& result = *symbol;
  result.setScope(scope);

  // Store in symbols collection
  impl->symbols.add(zc::mv(symbol));

  // Register for lookup
  impl->registerSymbol(result);

  return result;
}

ClassSymbol& SymbolTable::createClass(zc::StringPtr name, const Scope& scope) {
  auto symbol =
      zc::heap<ClassSymbol>(SymbolId::create(impl->generateSymbolId()), name,
                            SymbolFlags::Class | SymbolFlags::TypeKind, source::SourceLoc{});

  ClassSymbol& result = *symbol;
  result.setScope(scope);

  // Store in symbols collection
  impl->symbols.add(zc::mv(symbol));

  // Register for lookup
  impl->registerSymbol(result);

  return result;
}

InterfaceSymbol& SymbolTable::createInterface(zc::StringPtr name, const Scope& scope) {
  auto symbol = zc::heap<InterfaceSymbol>(SymbolId::create(impl->generateSymbolId()), name,
                                          SymbolFlags::Interface | SymbolFlags::TypeKind,
                                          source::SourceLoc{});

  InterfaceSymbol& result = *symbol;
  result.setScope(scope);

  // Store in symbols collection
  impl->symbols.add(zc::mv(symbol));

  // Register for lookup
  impl->registerSymbol(result);

  return result;
}

PackageSymbol& SymbolTable::createPackage(zc::StringPtr name, const Scope& scope) {
  auto symbol = zc::heap<PackageSymbol>(SymbolId::create(impl->generateSymbolId()), name,
                                        SymbolFlags::Package, source::SourceLoc{});

  PackageSymbol& result = *symbol;
  result.setScope(scope);

  // Store in symbols collection
  impl->symbols.add(zc::mv(symbol));

  // Register for lookup
  impl->registerSymbol(result);

  return result;
}

zc::Maybe<Symbol&> SymbolTable::lookup(zc::StringPtr name, const Scope& scope) const {
  zc::String key = impl->makeKey(name, scope);
  ZC_IF_SOME(symbolList, impl->symbolsByName.find(key)) {
    if (symbolList.size() > 0) {
      ZC_IF_SOME(symbol, symbolList[0]) { return symbol; }
    }
  }
  return zc::none;
}

zc::Maybe<Symbol&> SymbolTable::lookupInCurrentScope(zc::StringPtr name) const {
  ZC_IF_SOME(scope, impl->currentScope) { return lookup(name, scope); }
  return zc::none;
}

zc::Maybe<Symbol&> SymbolTable::lookupRecursive(zc::StringPtr name, const Scope& scope) const {
  // Try current scope first
  ZC_IF_SOME(symbol, lookup(name, scope)) { return symbol; }

  // Try parent scopes
  ZC_IF_SOME(parent, scope.getParent()) { return lookupRecursive(name, parent); }

  return zc::none;
}

zc::Maybe<SymbolDenotation&> SymbolTable::lookupDenotation(zc::StringPtr name, const Scope& scope) {
  // Create a denotation for the symbol if it exists
  ZC_IF_SOME(symbol, lookup(name, scope)) {
    // Determine the denotation kind based on symbol kind
    SymbolDenotation::Kind denotationKind;
    switch (symbol.getKind()) {
      case SymbolKind::Variable:
      case SymbolKind::Function:
        denotationKind = SymbolDenotation::Kind::TERM;
        break;
      case SymbolKind::Class:
      case SymbolKind::Interface:
        denotationKind = SymbolDenotation::Kind::TYPE;
        break;
      case SymbolKind::Package:
        denotationKind = SymbolDenotation::Kind::PACKAGE;
        break;
      default:
        denotationKind = SymbolDenotation::Kind::TERM;
        break;
    }

    // Create a new denotation and store it
    auto denotation = zc::heap<SymbolDenotation>(denotationKind, name, scope);
    SymbolDenotation& result = *denotation;

    // Store in denotations collection
    impl->denotations.add(zc::mv(denotation));

    // Register in denotation lookup table
    zc::String key = impl->makeKey(name, scope);
    auto& mutableMap = impl->denotationsByName;
    ZC_IF_SOME(existingList, mutableMap.find(key)) { existingList.add(result); }
    else {
      zc::Vector<zc::Maybe<SymbolDenotation&>> newList;
      newList.add(result);
      mutableMap.insert(zc::mv(key), zc::mv(newList));
    }

    return result;
  }
  return zc::none;
}

zc::Maybe<SymbolDenotation&> SymbolTable::lookupDenotationRecursive(zc::StringPtr name,
                                                                    const Scope& scope) {
  // Try current scope first
  ZC_IF_SOME(denotation, lookupDenotation(name, scope)) { return denotation; }

  // Try parent scopes
  ZC_IF_SOME(parent, scope.getParent()) { return lookupDenotationRecursive(name, parent); }

  return zc::none;
}

zc::Maybe<Symbol&> SymbolTable::resolveQualified(zc::StringPtr qualifiedName,
                                                 const Scope& scope) const {
  // Split the qualified name by '.'
  zc::Vector<zc::String> parts;
  zc::StringPtr remaining = qualifiedName;

  while (true) {
    ZC_IF_SOME(pos, remaining.findFirst('.')) {
      parts.add(zc::heapString(remaining.slice(0, pos)));
      remaining = remaining.slice(pos + 1);
    }
    else {
      parts.add(zc::heapString(remaining));
      break;
    }
  }

  if (parts.size() == 0) { return zc::none; }

  // If it's a single part, just do regular lookup
  if (parts.size() == 1) { return lookupRecursive(parts[0], scope); }

  // For multi-part names, we need to navigate through scopes
  // Start with the first part in the given scope
  const Scope* currentScope = &scope;

  // For the first part, we look for a scope with that name
  // This handles cases like "mypackage.MyClass" where we need to find the package scope
  auto& scopeManager = getScopeManager();
  auto scopes = scopeManager.getAllScopes();

  for (size_t i = 0; i < parts.size() - 1; ++i) {
    zc::StringPtr part = parts[i];

    // Look for a scope with this name
    const Scope* foundScope = nullptr;
    for (auto& scopePtr : scopes) {
      if (scopePtr->getName() == part) {
        foundScope = scopePtr.get();
        break;
      }
    }

    if (foundScope) {
      currentScope = foundScope;
    } else {
      return zc::none;
    }
  }

  // Now look for the final symbol in the current scope
  zc::StringPtr finalPart = parts[parts.size() - 1];
  return lookup(finalPart, *currentScope);
}

zc::Array<zc::Maybe<const Symbol&>> SymbolTable::getAllSymbols() const {
  auto builder = zc::heapArrayBuilder<zc::Maybe<const Symbol&>>(impl->symbols.size());
  for (const auto& symbol : impl->symbols) { builder.add(*symbol); }
  return builder.finish();
}

zc::Array<zc::Maybe<const Symbol&>> SymbolTable::getSymbolsInScope(const Scope& scope) const {
  // Count matching symbols first
  size_t count = 0;
  for (const auto& symbol : impl->symbols) {
    ZC_IF_SOME(symbolScope, symbol->getScope()) {
      if (symbolScope == scope) { count++; }
    }
  }

  auto builder = zc::heapArrayBuilder<zc::Maybe<const Symbol&>>(count);
  for (const auto& symbol : impl->symbols) {
    ZC_IF_SOME(symbolScope, symbol->getScope()) {
      if (symbolScope == scope) { builder.add(*symbol); }
    }
  }
  return builder.finish();
}

zc::Array<zc::Maybe<const Symbol&>> SymbolTable::getSymbolsOfType(SymbolKind kind) const {
  // Count matching symbols first
  size_t count = 0;
  for (const auto& symbol : impl->symbols) {
    if (symbol->getKind() == kind) { count++; }
  }

  auto builder = zc::heapArrayBuilder<zc::Maybe<const Symbol&>>(count);
  for (const auto& symbol : impl->symbols) {
    if (symbol->getKind() == kind) { builder.add(*symbol); }
  }
  return builder.finish();
}

zc::Array<zc::Maybe<const Symbol&>> SymbolTable::getSymbolsOfType(SymbolKind kind,
                                                                  const Scope& scope) const {
  // Count matching symbols first
  size_t count = 0;
  for (const auto& symbol : impl->symbols) {
    ZC_IF_SOME(symbolScope, symbol->getScope()) {
      if (symbolScope == scope && symbol->getKind() == kind) { count++; }
    }
  }

  auto builder = zc::heapArrayBuilder<zc::Maybe<const Symbol&>>(count);
  for (const auto& symbol : impl->symbols) {
    ZC_IF_SOME(symbolScope, symbol->getScope()) {
      if (symbolScope == scope && symbol->getKind() == kind) { builder.add(*symbol); }
    }
  }
  return builder.finish();
}

void SymbolTable::setCurrentScope(const Scope& scope) { impl->currentScope = scope; }

zc::Maybe<const Scope&> SymbolTable::getCurrentScope() const { return impl->currentScope; }

ScopeManager& SymbolTable::getScopeManager() { return *impl->scopeManager; }

const ScopeManager& SymbolTable::getScopeManager() const { return *impl->scopeManager; }

uint32_t SymbolTable::getCurrentPhase() const { return impl->currentPhase; }

void SymbolTable::enterSymbol(Symbol& symbol, const Scope& scope) {
  // Set the symbol's scope
  symbol.setScope(scope);

  // Register the symbol for lookup - check if it already exists first
  zc::String key = impl->makeKey(symbol);
  ZC_IF_SOME(existingList, impl->symbolsByName.find(key)) {
    // Check if this symbol is already in the list
    bool found = false;
    for (size_t i = 0; i < existingList.size(); ++i) {
      ZC_IF_SOME(existingSym, existingList[i]) {
        if (&existingSym == &symbol) {
          found = true;
          break;
        }
      }
      else {
        // Found an empty slot, reuse it
        existingList[i] = symbol;
        found = true;
        break;
      }
    }
    if (!found) { existingList.add(symbol); }
  }
  else {
    // Create new list for this key
    zc::Vector<zc::Maybe<Symbol&>> newList;
    newList.add(symbol);
    impl->symbolsByName.insert(zc::mv(key), zc::mv(newList));
  }
}

void SymbolTable::dropSymbol(Symbol& symbol, const Scope& scope) {
  // Only remove from lookup tables, don't destroy the symbol object
  // This allows the symbol to remain valid for external references
  zc::String key = impl->makeKey(symbol);
  ZC_IF_SOME(symbolList, impl->symbolsByName.find(key)) {
    for (size_t i = 0; i < symbolList.size(); ++i) {
      ZC_IF_SOME(sym, symbolList[i]) {
        if (&sym == &symbol) {
          // Mark as none instead of removing to avoid shifting elements
          symbolList[i] = zc::none;
          break;
        }
      }
    }
  }
}

zc::Array<zc::Maybe<Symbol&>> SymbolTable::getImplicitSymbols(const Scope& scope) const {
  // Look up implicit symbols for this scope
  zc::String scopeKey = zc::str("implicit@", scope.getName());
  ZC_IF_SOME(implicitList, impl->implicitsByScope.find(scopeKey)) {
    // Return a copy of the implicit symbols
    auto builder = zc::heapArrayBuilder<zc::Maybe<Symbol&>>(implicitList.size());
    for (const auto& maybeSymbol : implicitList) {
      // Need to cast away const to match the return type
      ZC_IF_SOME(symbol, maybeSymbol) { builder.add(symbol); }
      else { builder.add(zc::none); }
    }
    return builder.finish();
  }

  // No implicit symbols found, return empty array
  auto builder = zc::heapArrayBuilder<zc::Maybe<Symbol&>>(0);
  return builder.finish();
}

void SymbolTable::registerImplicitSymbol(Symbol& symbol, const Scope& scope) {
  // Register the symbol as implicit for this scope
  zc::String scopeKey = zc::str("implicit@", scope.getName());

  ZC_IF_SOME(existingList, impl->implicitsByScope.find(scopeKey)) { existingList.add(symbol); }
  else {
    zc::Vector<zc::Maybe<Symbol&>> newList;
    newList.add(symbol);
    impl->implicitsByScope.insert(zc::mv(scopeKey), zc::mv(newList));
  }
}

size_t SymbolTable::getSymbolCount() const {
  // Count symbols that are actually registered in the lookup table
  size_t count = 0;
  for (const auto& entry : impl->symbolsByName) {
    for (const auto& maybeSymbol : entry.value) {
      ZC_IF_SOME(symbol, maybeSymbol) {
        (void)symbol;  // Mark as used
        count++;
      }
    }
  }
  return count;
}

size_t SymbolTable::getDenotationCount() const { return impl->denotations.size(); }

void SymbolTable::dumpSymbols() const {
  // Implementation for dumping symbols
}

void SymbolTable::dumpDenotations() const {
  // Implementation for dumping denotations
}

}  // namespace symbol
}  // namespace compiler
}  // namespace zomlang
