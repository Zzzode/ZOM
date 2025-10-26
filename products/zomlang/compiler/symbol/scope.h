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

#include "zc/core/common.h"
#include "zc/core/string.h"

namespace zomlang {
namespace compiler {
namespace symbol {

class Symbol;

/// \brief Scope - Represents a lexical scope in the program
///
/// The Scope class provides a hierarchical namespace for symbols,
/// supporting lexical scoping rules and symbol lookup.
class Scope {
public:
  enum class Kind {
    Global,
    Package,
    Module,
    Class,
    Interface,
    Function,
    Block,
    Enum,
    Catch,
    For,
    While,
    If,
    Switch,
    Lambda,
    Namespace
  };

  Scope(Kind kind, zc::StringPtr name, zc::Maybe<Scope&> parent = zc::none) noexcept;
  Scope(Scope&& other) noexcept = default;
  Scope& operator=(Scope&& other) noexcept = default;
  ~Scope() noexcept(false);

  /// \brief Basic properties
  Kind getKind() const;
  zc::StringPtr getName() const;
  zc::String getFullName() const;
  zc::Maybe<const Scope&> getParent() const;
  bool isRoot() const;

  /// \brief Symbol management
  void addSymbol(zc::Own<Symbol> symbol);
  void removeSymbol(zc::StringPtr name);
  zc::Maybe<Symbol&> lookupSymbol(zc::StringPtr name);
  zc::Maybe<Symbol&> lookupSymbolLocally(zc::StringPtr name);
  zc::Maybe<Symbol&> lookupSymbolRecursively(zc::StringPtr name);

  /// \brief Symbol enumeration
  bool hasSymbol(zc::StringPtr name);
  size_t getSymbolCount() const;

  /// \brief Scope hierarchy
  void addChild(zc::Own<Scope> child);
  void removeChild(Scope& child);
  zc::Maybe<const Scope&> getChild(zc::StringPtr name) const;
  bool hasChild(Scope& child) const;

  /// \brief Scope relationships
  bool isAncestorOf(const Scope& other) const;
  bool isDescendantOf(const Scope& other) const;
  bool isSiblingOf(const Scope& other) const;
  zc::Maybe<const Scope&> getCommonAncestor(const Scope& other) const;

  // Scope validation
  bool isValid() const;
  void validate() const;

  /// \brief Utility functions
  zc::String toString() const;

  /// \brief Operator overloads
  zc::Maybe<Symbol&> operator[](zc::StringPtr name);
  bool operator==(const Scope& other) const;
  bool operator!=(const Scope& other) const;

  /// \brief Scope creation helpers
  static zc::Own<Scope> createGlobalScope();
  static zc::Own<Scope> createPackageScope(zc::StringPtr name, Scope& parent);
  static zc::Own<Scope> createClassScope(zc::StringPtr name, Scope& parent);
  static zc::Own<Scope> createFunctionScope(zc::StringPtr name, Scope& parent);
  static zc::Own<Scope> createBlockScope(zc::StringPtr name, Scope& parent);

private:
  struct Impl;
  zc::Own<Impl> impl;
};

/// \brief ScopeManager - Manages scope lifecycle and relationships
///
/// The ScopeManager provides centralized management of all scopes
/// in the compilation unit, supporting scope creation, lookup, and cleanup.
class ScopeManager {
public:
  ScopeManager() noexcept;
  ~ScopeManager() noexcept(false);

  /// \brief Scope lifecycle
  Scope& createScope(Scope::Kind kind, zc::StringPtr name, zc::Maybe<Scope&> parent = zc::none);
  void destroyScope(Scope& scope);

  /// \brief Current scope management
  zc::Maybe<const Scope&> getCurrentScope() const;
  void setCurrentScope(const Scope& scope);
  void pushScope(const Scope& scope);
  void popScope();

  /// \brief Scope lookup
  zc::Maybe<const Scope&> getGlobalScope() const;
  zc::Maybe<const Scope&> getPackageScope(zc::StringPtr name) const;
  zc::Maybe<const Scope&> getClassScope(zc::StringPtr name) const;
  zc::Maybe<const Scope&> getFunctionScope(zc::StringPtr name) const;

  /// \brief Mutable scope lookup (for ScopeGuard factory methods)
  zc::Maybe<Scope&> getGlobalScopeMutable();
  zc::Maybe<Scope&> getPackageScopeMutable(zc::StringPtr name);
  zc::Maybe<Scope&> getClassScopeMutable(zc::StringPtr name);
  zc::Maybe<Scope&> getFunctionScopeMutable(zc::StringPtr name);

  /// \brief Scope enumeration
  zc::ArrayPtr<const zc::Own<Scope>> getAllScopes() const;
  zc::Array<zc::Maybe<const Scope&>> getScopesOfKind(Scope::Kind kind) const;

  /// \brief Scope validation
  bool isValidScope(const Scope& scope) const;
  void validateAllScopes() const;

  /// \brief Utility functions
  void clear();

private:
  struct Impl;
  zc::Own<Impl> impl;
};

/// \brief ScopeGuard - RAII helper for scope management
///
/// Automatically manages scope entry and exit, ensuring proper cleanup.
class ScopeGuard {
public:
  explicit ScopeGuard(ScopeManager& manager, const Scope& scope) noexcept;
  ~ScopeGuard() noexcept(false);

  zc::Maybe<const Scope&> getScope() const;
  bool isActive() const;
  void release();

  ZC_DISALLOW_COPY_AND_MOVE(ScopeGuard);

  static zc::Maybe<zc::Own<ScopeGuard>> create(ScopeManager& manager, Scope& scope);
  static zc::Maybe<zc::Own<ScopeGuard>> createGlobal(ScopeManager& manager);
  static zc::Maybe<zc::Own<ScopeGuard>> createPackage(ScopeManager& manager, zc::StringPtr name);
  static zc::Maybe<zc::Own<ScopeGuard>> createClass(ScopeManager& manager, zc::StringPtr name);
  static zc::Maybe<zc::Own<ScopeGuard>> createFunction(ScopeManager& manager, zc::StringPtr name);

private:
  struct Impl;
  zc::Own<Impl> impl;
};

}  // namespace symbol
}  // namespace compiler
}  // namespace zomlang
