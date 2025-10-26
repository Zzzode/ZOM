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
#include "zomlang/compiler/source/location.h"
#include "zomlang/compiler/symbol/symbol-id.h"
#include "zomlang/compiler/symbol/symbol.h"

namespace zomlang {
namespace compiler {

// Forward declarations
namespace ast {
class TypeNode;
}

namespace symbol {

/// \brief TypeSymbol - Represents type-level symbols in the compiler.
///
/// This includes:
/// - Primitive types (int, float, string, bool)
/// - User-defined types (classes, structs, interfaces)
/// - Generic type parameters
/// - Type aliases
/// - Function types
///
/// Design follows ZOM style guide with PIMPL pattern for encapsulation.
class TypeSymbol : public Symbol {
public:
  TypeSymbol(SymbolId id, zc::StringPtr name, SymbolFlags flags,
             const source::SourceLoc& location) noexcept;

  // Move constructor and assignment
  TypeSymbol(TypeSymbol&& other) noexcept;
  TypeSymbol& operator=(TypeSymbol&& other) noexcept;

  ZC_DISALLOW_COPY(TypeSymbol);

  // Virtual destructor
  virtual ~TypeSymbol() noexcept(false);

  // Type classification
  bool isPrimitive() const;
  bool isClass() const;
  bool isInterface() const;
  bool isGeneric() const;
  bool isAlias() const;
  bool isFunction() const;
  bool isEnumType() const;
  bool isUnionType() const;
  bool isIntersectionType() const;

  // Symbol type checking
  bool isTypeSymbol() const override { return true; }
  SymbolKind getKind() const override { return SymbolKind::Type; }
  zc::StringPtr getKindName() const override { return "type"_zc; }

  // Type relationships
  bool isSubtypeOf(const TypeSymbol& other) const;
  bool isAssignableFrom(const TypeSymbol& other) const;
  zc::Array<zc::Maybe<const TypeSymbol&>> getSupertypes() const;
  zc::Array<zc::Maybe<const TypeSymbol&>> getSubtypes() const;

  // Generic support
  zc::Array<zc::Maybe<const TypeSymbol&>> getTypeParameters() const;
  zc::Maybe<const TypeSymbol&> getDeclaringType() const;

  // Type bounds (for type parameters)
  zc::Array<zc::Maybe<const TypeSymbol&>> getUpperBounds() const;
  zc::Array<zc::Maybe<const TypeSymbol&>> getLowerBounds() const;

  // AST integration
  zc::Maybe<const ast::TypeNode&> getAstType() const;
  void setAstType(zc::Maybe<const ast::TypeNode&> type);

  // Symbol classification
  bool isType() const { return true; }
  TypeSymbol& asType() { return *this; }
  const TypeSymbol& asType() const { return *this; }

  // Utility
  zc::String getQualifiedName() const;
  bool isNumeric() const;
  bool isString() const;
  bool isBoolean() const;
  bool isVoid() const;

private:
  struct Impl;
  zc::Own<Impl> impl;
};

/// \brief Special built-in type symbols
class BuiltInTypeSymbol final : public TypeSymbol {
public:
  BuiltInTypeSymbol(SymbolId id, zc::StringPtr name, SymbolFlags flags,
                    const source::SourceLoc& location) noexcept;

  ZC_DISALLOW_COPY(BuiltInTypeSymbol);

  SymbolKind getKind() const override { return SymbolKind::Type; }

  static zc::Own<BuiltInTypeSymbol> createI32(SymbolId id, const source::SourceLoc& location);
  static zc::Own<BuiltInTypeSymbol> createF32(SymbolId id, const source::SourceLoc& location);
  static zc::Own<BuiltInTypeSymbol> createStr(SymbolId id, const source::SourceLoc& location);
  static zc::Own<BuiltInTypeSymbol> createBool(SymbolId id, const source::SourceLoc& location);
  static zc::Own<BuiltInTypeSymbol> createUnit(SymbolId id, const source::SourceLoc& location);

private:
  struct Impl;
  zc::Own<Impl> impl;
};

/// \brief InterfaceSymbol - Represents interface types
class InterfaceSymbol final : public TypeSymbol {
public:
  InterfaceSymbol(SymbolId id, zc::StringPtr name, SymbolFlags flags,
                  const source::SourceLoc& location) noexcept;

  // Move constructor and assignment
  InterfaceSymbol(InterfaceSymbol&& other) noexcept;
  InterfaceSymbol& operator=(InterfaceSymbol&& other) noexcept;

  ZC_DISALLOW_COPY(InterfaceSymbol);

  // Virtual destructor
  virtual ~InterfaceSymbol() noexcept(false);

  bool isInterface() const { return true; }
  SymbolKind getKind() const override { return SymbolKind::Interface; }

private:
  struct Impl;
  zc::Own<Impl> impl;
};

/// \brief ClassSymbol - Represents class types
class ClassSymbol final : public TypeSymbol {
public:
  ClassSymbol(SymbolId id, zc::StringPtr name, SymbolFlags flags,
              const source::SourceLoc& location) noexcept;

  // Move constructor and assignment
  ClassSymbol(ClassSymbol&& other) noexcept;
  ClassSymbol& operator=(ClassSymbol&& other) noexcept;

  ZC_DISALLOW_COPY(ClassSymbol);

  // Virtual destructor
  virtual ~ClassSymbol() noexcept(false);

  // Class hierarchy
  zc::Maybe<const ClassSymbol&> getSuperclass() const;
  void setSuperclass(zc::Maybe<const ClassSymbol&> newSuperclass);

  zc::Array<zc::Maybe<const ClassSymbol&>> getInterfaces() const;
  void addInterface(zc::Maybe<const ClassSymbol&> interface);

  /// \brief Member symbols
  zc::Array<zc::Maybe<const Symbol&>> getMembers() const;
  void addMember(zc::Maybe<const Symbol&> member);

  /// \brief Constructor symbols
  zc::Array<zc::Maybe<const Symbol&>> getConstructors() const;
  void addConstructor(zc::Maybe<const Symbol&> constructor);

  /// \brief Type checking
  bool isInterface() const { return false; }
  SymbolKind getKind() const override { return SymbolKind::Class; }
  bool isAbstract() const;
  bool isFinal() const;

  /// \brief Downcasting support
  ClassSymbol& asClass() { return *this; }
  const ClassSymbol& asClass() const { return *this; }

private:
  struct Impl;
  zc::Own<Impl> impl;
};

/// \brief FunctionTypeSymbol - Represents function types
class FunctionTypeSymbol final : public TypeSymbol {
public:
  FunctionTypeSymbol(SymbolId id, zc::StringPtr name, SymbolFlags flags,
                     const source::SourceLoc& location) noexcept;

  // Move constructor and assignment
  FunctionTypeSymbol(FunctionTypeSymbol&& other) noexcept;
  FunctionTypeSymbol& operator=(FunctionTypeSymbol&& other) noexcept;
  ~FunctionTypeSymbol() noexcept(false);

  ZC_DISALLOW_COPY(FunctionTypeSymbol);

  /// \brief Function signature
  zc::Maybe<const TypeSymbol&> getReturnType() const;
  void setReturnType(zc::Maybe<const TypeSymbol&> type);

  zc::Array<zc::Maybe<const TypeSymbol&>> getParameterTypes() const;
  void addParameterType(zc::Maybe<const TypeSymbol&> type);

  /// \brief Variadic support
  bool isVariadic() const;
  void setVariadic(bool isVariadicParam);

  /// \brief Overload resolution
  bool isMoreSpecificThan(const FunctionTypeSymbol& other) const;

private:
  struct Impl;
  zc::Own<Impl> impl;
};

/// \brief TypeParameterSymbol - Represents generic type parameters
class TypeParameterSymbol final : public TypeSymbol {
public:
  TypeParameterSymbol(SymbolId id, zc::StringPtr name, SymbolFlags flags,
                      const source::SourceLoc& location) noexcept;

  // Move constructor and assignment
  TypeParameterSymbol(TypeParameterSymbol&& other) noexcept;
  TypeParameterSymbol& operator=(TypeParameterSymbol&& other) noexcept;
  ~TypeParameterSymbol() noexcept(false);

  ZC_DISALLOW_COPY(TypeParameterSymbol);

  /// \brief Variance annotations
  enum class Variance { Invariant, Covariant, Contravariant };

  Variance getVariance() const;
  void setVariance(Variance variance);

  /// \brief Bounds
  zc::Array<zc::Maybe<const TypeSymbol&>> getBounds() const;
  void addBound(zc::Maybe<const TypeSymbol&> bound);

private:
  struct Impl;
  zc::Own<Impl> impl;
};

}  // namespace symbol
}  // namespace compiler
}  // namespace zomlang
