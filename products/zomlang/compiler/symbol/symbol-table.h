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

#pragma once

#include <cstdint>

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/string.h"
#include "zomlang/compiler/symbol/symbol-denotation.h"
#include "zomlang/compiler/symbol/symbol.h"

namespace zomlang {
namespace compiler {
namespace symbol {

// Forward declarations
class Scope;
class ScopeManager;
class Symbol;
class TypeSymbol;
class VariableSymbol;
class FunctionSymbol;
class ClassSymbol;
class InterfaceSymbol;
class PackageSymbol;
class SymbolDenotation;

/// \brief SymbolTable - Enhanced symbol management with distributed architecture
///
/// This enhanced symbol table:
/// 1. Clear ownership model using zc::Own<Symbol>
/// 2. Efficient lookup with hierarchical hash tables
/// 3. Scope-aware symbol resolution with parent chain traversal
/// 4. Type-safe symbol creation with denotation support
/// 5. Phase-aware compilation support
/// 6. Symbol uniqueness guarantees
class SymbolTable {
public:
  SymbolTable() noexcept;
  ~SymbolTable() noexcept(false);

  // Non-copyable, movable
  ZC_DISALLOW_COPY(SymbolTable);

  SymbolTable(SymbolTable&&) noexcept;
  SymbolTable& operator=(SymbolTable&&) noexcept;

  /// \brief Convenient symbol creation methods
  VariableSymbol& createVariable(zc::StringPtr name, const Scope& scope);
  FunctionSymbol& createFunction(zc::StringPtr name, const Scope& scope);
  ClassSymbol& createClass(zc::StringPtr name, const Scope& scope);
  InterfaceSymbol& createInterface(zc::StringPtr name, const Scope& scope);
  PackageSymbol& createPackage(zc::StringPtr name, const Scope& scope);

  /// \brief Enhanced symbol lookup - scope-aware resolution
  zc::Maybe<Symbol&> lookup(zc::StringPtr name, const Scope& scope) const;
  zc::Maybe<Symbol&> lookupInCurrentScope(zc::StringPtr name) const;
  zc::Maybe<Symbol&> lookupRecursive(zc::StringPtr name, const Scope& scope) const;

  /// \brief Denotation-based lookup
  zc::Maybe<SymbolDenotation&> lookupDenotation(zc::StringPtr name, const Scope& scope);
  zc::Maybe<SymbolDenotation&> lookupDenotationRecursive(zc::StringPtr name, const Scope& scope);

  /// \brief Qualified name resolution
  zc::Maybe<Symbol&> resolveQualified(zc::StringPtr qualifiedName, const Scope& scope) const;

  /// \brief Symbol enumeration
  zc::Array<zc::Maybe<const Symbol&>> getAllSymbols() const;
  zc::Array<zc::Maybe<const Symbol&>> getSymbolsInScope(const Scope& scope) const;

  /// \brief Get symbols of specific type using SymbolKind
  zc::Array<zc::Maybe<const Symbol&>> getSymbolsOfType(SymbolKind kind) const;
  zc::Array<zc::Maybe<const Symbol&>> getSymbolsOfType(SymbolKind kind, const Scope& scope) const;

  /// \brief Scope management
  void setCurrentScope(const Scope& scope);
  zc::Maybe<const Scope&> getCurrentScope() const;

  /// \brief ScopeManager access
  ScopeManager& getScopeManager();
  const ScopeManager& getScopeManager() const;

  /// \brief Phase management (for multi-phase compilation)
  void setCurrentPhase(uint32_t phase);
  uint32_t getCurrentPhase() const;

  /// \brief Symbol uniqueness and caching
  template <typename T>
  T& getUniqueSymbol(zc::StringPtr name, const Scope& scope);

  /// \brief Symbol entry and removal (inspired by Scala 3's enter/drop)
  void enterSymbol(Symbol& symbol, const Scope& scope);
  void dropSymbol(Symbol& symbol, const Scope& scope);

  /// \brief Implicit symbol management
  zc::Array<zc::Maybe<Symbol&>> getImplicitSymbols(const Scope& scope) const;
  void registerImplicitSymbol(Symbol& symbol, const Scope& scope);

  /// \brief Statistics and debugging
  size_t getSymbolCount() const;
  size_t getDenotationCount() const;
  void dumpSymbols() const;
  void dumpDenotations() const;

private:
  struct Impl;
  zc::Own<Impl> impl;
};

}  // namespace symbol
}  // namespace compiler
}  // namespace zomlang
