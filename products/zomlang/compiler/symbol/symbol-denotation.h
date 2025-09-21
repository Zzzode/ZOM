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

#include "zc/core/common.h"
#include "zc/core/string.h"

namespace zomlang {
namespace compiler {
namespace symbol {

// Forward declarations
class Scope;
class TypeSymbol;

/// \brief SymbolDenotation - Represents what a symbol denotes
///
/// This provides a clean separation between symbol identity and what the symbol represents.
/// Enhanced with phase-aware information and type caching.
///
/// SymbolDenotation is the core representation of symbol semantic information in the compiler.
/// It defines the "meaning" or "denotation" of a symbol at specific compilation phases.
/// This design is inspired by the Scala 3 compiler's denotation system, providing a clear
/// separation between symbol identity and what the symbol represents.
///
/// Key features:
/// 1. Symbol classification: TERM (variables, functions), TYPE (classes, interfaces), PACKAGE
/// (packages)
/// 2. Phase awareness: supports multi-phase compilation where symbols can have different meanings
/// at different phases
/// 3. Type caching: caches symbol type information for improved query efficiency
/// 4. Absence marking: supports marking symbols as "absent" for error recovery
class SymbolDenotation {
public:
  enum class Kind {
    TERM,    // Variables, functions, objects
    TYPE,    // Classes, interfaces, type aliases
    PACKAGE  // Packages and modules
  };

  SymbolDenotation(Kind kind, zc::StringPtr name, const Scope& scope) noexcept;
  ~SymbolDenotation() noexcept(false);

  // Non-copyable, movable
  ZC_DISALLOW_COPY(SymbolDenotation);

  SymbolDenotation(SymbolDenotation&&) noexcept;
  SymbolDenotation& operator=(SymbolDenotation&&) noexcept;

  /// \brief Get the kind of the symbol
  Kind getKind() const;

  /// \brief Get the name of the symbol
  zc::StringPtr getName() const;

  /// \brief Get the scope containing this symbol
  const Scope& getScope() const;

  // Type-specific accessors

  /// \brief Check if this is a TERM symbol (variables, functions, etc.)
  bool isTerm() const;

  /// \brief Check if this is a TYPE symbol (classes, interfaces, etc.)
  bool isType() const;

  /// \brief Check if this is a PACKAGE symbol (packages, modules, etc.)
  bool isPackage() const;

  // Enhanced denotation features

  /// \brief Check if the symbol is in "absent" state
  /// Used for error recovery when symbols cannot be properly resolved
  bool isAbsent() const;

  /// \brief Mark the symbol as "absent"
  void markAbsent();

  // Phase-aware information (for multi-phase compilation)

  /// \brief Get the compilation phase from which this symbol is valid
  uint32_t getValidFromPhase() const;

  /// \brief Set the compilation phase from which this symbol is valid
  void setValidFromPhase(uint32_t phase);

  // Type information caching

  /// \brief Get cached type information
  zc::Maybe<const TypeSymbol&> getCachedType() const;

  /// \brief Set cached type information
  void setCachedType(const TypeSymbol& type);

private:
  struct Impl;
  zc::Own<Impl> impl;
};

}  // namespace symbol
}  // namespace compiler
}  // namespace zomlang
