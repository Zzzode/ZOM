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
#include "zomlang/compiler/ast/statement.h"
#include "zomlang/compiler/source/location.h"
#include "zomlang/compiler/symbol/symbol-flags.h"
#include "zomlang/compiler/symbol/symbol-id.h"

namespace zomlang {
namespace compiler {

namespace source {
class SourceManager;
}

namespace ast {
class Node;
}

namespace symbol {

/// \brief Forward declarations
class TypeSymbol;
class ValueSymbol;
class ModuleSymbol;
class FunctionSymbol;
class VariableSymbol;
class ClassSymbol;
class PackageSymbol;
class NamespaceSymbol;
class Scope;
class SymbolTable;

/// \brief Symbol kind enumeration
enum class SymbolKind {
  None,
  Variable,
  Function,
  Class,
  Interface,
  Package,
  TypeAlias,
  Namespace,
  Constant,
  Parameter,
  Field,
  Enum,
  EnumCase,
  Method,
  Constructor,
  Destructor,
  Type,
  Value,
  Module
};

enum class Visibility { Public, Private, Protected, Internal };

/// \brief Symbol base class following pimpl pattern
///
/// Symbol serves as the abstract base for all semantic symbols in ZomLang,
/// implemented with unified 64-bit identity and properties system.
class Symbol {
public:
  /// \brief Constructor using pimpl pattern
  /// \param id Symbol identifier
  /// \param name Symbol name
  /// \param flags Symbol properties flags
  /// \param location Source code location
  Symbol(SymbolId id, zc::StringPtr name, SymbolFlags flags,
         const source::SourceLoc& location) noexcept;
  Symbol(SymbolId id, zc::StringPtr name, SymbolFlags flags) noexcept;

  // Move constructor and assignment
  Symbol(Symbol&& other) noexcept;
  Symbol& operator=(Symbol&& other) noexcept;

  // Destructor
  virtual ~Symbol() noexcept(false);

  // Disable copy
  ZC_DISALLOW_COPY(Symbol);

  // Identity and properties (public interface)
  SymbolId getId() const;
  zc::StringPtr getName() const;
  void setName(zc::StringPtr name);

  // Location tracking
  const source::SourceLoc& getLocation() const;
  zc::StringPtr getFileName(const source::SourceManager& sourceManager) const;
  uint32_t getLine(const source::SourceManager& sourceManager) const;

  // Symbol flags
  bool hasFlag(SymbolFlags flag) const;
  bool hasAnyFlag(SymbolFlags mask) const;
  bool hasAllFlags(SymbolFlags mask) const;
  void addFlag(SymbolFlags flag);
  void removeFlag(SymbolFlags flag);
  SymbolFlags getFlags() const;

  // Symbol classification
  virtual bool isTypeSymbol() const;
  virtual bool isValueSymbol() const;
  virtual bool isModuleSymbol() const;
  virtual bool isFunctionSymbol() const;
  virtual bool isVariableSymbol() const;
  virtual bool isClassSymbol() const;

  // Symbol kind extraction
  virtual SymbolKind getKind() const { return SymbolKind::None; }
  virtual zc::StringPtr getKindName() const { return "symbol"_zc; }

  // Scope integration
  zc::Maybe<const Scope&> getScope() const;
  void setScope(zc::Maybe<const Scope&> scope);

  // Type system integration
  zc::Maybe<const TypeSymbol&> getType() const;
  void setType(zc::Maybe<TypeSymbol&> type);

  zc::ArrayPtr<const zc::Maybe<const ast::Node&>> getDeclarationNodes() const;
  void addDeclarationNode(zc::Maybe<const ast::Node&> node);
  void removeDeclarationNode(const ast::Node& node);

  // Visibility and access
  bool isPublic() const;
  bool isPrivate() const;
  bool isProtected() const;
  bool isInternal() const;

  // Storage properties
  bool isMutable() const;
  bool isFinal() const;
  bool isLazy() const;
  bool isInline() const;

  // Lifetime properties
  bool isStatic() const;
  bool isInstance() const;
  bool isGlobal() const;
  bool isLocal() const;

  // String representation
  zc::String toString() const;

protected:
  struct Impl;
  zc::Own<Impl> impl;
};

/// \brief Internal symbol name prefix - invalid UTF-8 sequence that will never occur as identifier
/// name
constexpr zc::StringPtr INTERNAL_SYMBOL_NAME_PREFIX = "\xFE"_zc;

/// \brief Internal symbol names for special compiler-generated symbols
/// These names use an invalid UTF-8 prefix to ensure they never conflict with user identifiers

/// Call signatures
constexpr zc::StringPtr INTERNAL_SYMBOL_NAME_CALL =
    "\xFE"
    "call"_zc;
/// Constructor implementations
constexpr zc::StringPtr INTERNAL_SYMBOL_NAME_CONSTRUCTOR =
    "\xFE"
    "constructor"_zc;
/// Constructor signatures
constexpr zc::StringPtr INTERNAL_SYMBOL_NAME_NEW =
    "\xFE"
    "new"_zc;
/// Index signatures
constexpr zc::StringPtr INTERNAL_SYMBOL_NAME_INDEX =
    "\xFE"
    "index"_zc;
/// Module export * declarations
constexpr zc::StringPtr INTERNAL_SYMBOL_NAME_EXPORT_STAR =
    "\xFE"
    "export"_zc;
/// Global self-reference
constexpr zc::StringPtr INTERNAL_SYMBOL_NAME_GLOBAL =
    "\xFE"
    "global"_zc;
/// Indicates missing symbol
constexpr zc::StringPtr INTERNAL_SYMBOL_NAME_MISSING =
    "\xFE"
    "missing"_zc;
/// Anonymous type literal symbol
constexpr zc::StringPtr INTERNAL_SYMBOL_NAME_TYPE =
    "\xFE"
    "type"_zc;
/// Anonymous object literal declaration
constexpr zc::StringPtr INTERNAL_SYMBOL_NAME_OBJECT =
    "\xFE"
    "object"_zc;
/// Anonymous JSX attributes object literal declaration
constexpr zc::StringPtr INTERNAL_SYMBOL_NAME_JSX_ATTRIBUTES =
    "\xFE"
    "jsxAttributes"_zc;
/// Unnamed class expression
constexpr zc::StringPtr INTERNAL_SYMBOL_NAME_CLASS =
    "\xFE"
    "class"_zc;
/// Unnamed function expression
constexpr zc::StringPtr INTERNAL_SYMBOL_NAME_FUNCTION =
    "\xFE"
    "function"_zc;
/// Computed property name declaration with dynamic name
constexpr zc::StringPtr INTERNAL_SYMBOL_NAME_COMPUTED =
    "\xFE"
    "computed"_zc;
/// Indicator symbol used to mark partially resolved type aliases
constexpr zc::StringPtr INTERNAL_SYMBOL_NAME_RESOLVING =
    "\xFE"
    "resolving"_zc;
/// Instantiation expressions
constexpr zc::StringPtr INTERNAL_SYMBOL_NAME_INSTANTIATION_EXPRESSION =
    "\xFE"
    "instantiationExpression"_zc;
/// Import attributes
constexpr zc::StringPtr IMPORT_ATTRIBUTES =
    "\xFE"
    "importAttributes"_zc;
/// Export assignment symbol
constexpr zc::StringPtr INTERNAL_SYMBOL_NAME_EXPORT_EQUALS = "export="_zc;
/// Default export symbol (technically not wholly internal, but included here for usability)
constexpr zc::StringPtr INTERNAL_SYMBOL_NAME_DEFAULT = "default"_zc;
/// This reference
constexpr zc::StringPtr INTERNAL_SYMBOL_NAME_THIS = "this"_zc;
/// Module exports
constexpr zc::StringPtr INTERNAL_SYMBOL_NAME_MODULE_EXPORTS = "module.exports"_zc;

}  // namespace symbol
}  // namespace compiler
}  // namespace zomlang
