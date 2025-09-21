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
#include "zomlang/compiler/symbol/symbol.h"

namespace zomlang {
namespace compiler {
namespace symbol {

// Forward declarations
class TypeSymbol;
class ParameterSymbol;

/// \brief Base class for all value-bearing symbols
///
/// Value symbols represent entities that have a value at runtime,
/// such as variables, constants, functions, etc.
class ValueSymbol : public Symbol {
public:
  /// \brief Constructor with location
  ValueSymbol(SymbolId id, zc::StringPtr name, SymbolFlags flags, const source::SourceLoc& location,
              zc::Own<TypeSymbol> type);

  // Move constructor and assignment
  ValueSymbol(ValueSymbol&& other) noexcept;
  ValueSymbol& operator=(ValueSymbol&& other) noexcept;

  // Virtual destructor
  virtual ~ValueSymbol() noexcept(false);

  // Disable copy
  ZC_DISALLOW_COPY(ValueSymbol);

  /// \brief Get the type of this value
  const TypeSymbol& getType() const;

  /// \brief Get the mutable type of this value
  TypeSymbol& getType();

  /// \brief Set the type of this value
  void setType(zc::Own<TypeSymbol> newType);

  /// \brief Check if this value is mutable
  bool isMutable() const;

  /// \brief Check if this value is constant
  bool isConstant() const;

  // Symbol type checking
  bool isValueSymbol() const override;
  SymbolKind getKind() const override;
  zc::StringPtr getKindName() const override;

protected:
  struct Impl;
  zc::Own<Impl> impl;
};

/// \brief Symbol representing a variable
class VariableSymbol : public ValueSymbol {
public:
  /// \brief Constructor
  VariableSymbol(SymbolId id, zc::StringPtr name, SymbolFlags flags,
                 const source::SourceLoc& location, zc::Own<TypeSymbol> type,
                 bool isMutable = true);

  // Move constructor and assignment
  VariableSymbol(VariableSymbol&& other) noexcept;
  VariableSymbol& operator=(VariableSymbol&& other) noexcept;

  // Virtual destructor
  virtual ~VariableSymbol() noexcept(false);

  // Disable copy
  ZC_DISALLOW_COPY(VariableSymbol);

  /// \brief LLVM-style type checking
  static bool classof(zc::Maybe<const Symbol&> symbol);

  /// \brief Get symbol kind
  SymbolKind getKind() const override;

  /// \brief Check if this variable is a parameter
  bool isParameter() const;

  /// \brief Check if this variable is captured by closure
  bool isCaptured() const;

  /// \brief Check if this variable is a local variable
  bool isLocal() const;
};

/// \brief Symbol representing a constant value
class ConstantSymbol final : public ValueSymbol {
public:
  /// \brief Constructor
  ConstantSymbol(SymbolId id, zc::StringPtr name, SymbolFlags flags,
                 const source::SourceLoc& location, zc::Own<TypeSymbol> type);

  // Move constructor and assignment
  ConstantSymbol(ConstantSymbol&& other) noexcept;
  ConstantSymbol& operator=(ConstantSymbol&& other) noexcept;

  // Virtual destructor
  virtual ~ConstantSymbol() noexcept(false);

  // Disable copy
  ZC_DISALLOW_COPY(ConstantSymbol);

  /// \brief LLVM-style type checking
  static bool classof(zc::Maybe<const Symbol&> symbol);

  /// \brief Get symbol kind
  SymbolKind getKind() const override;

  /// \brief Get the constant value as a string representation
  zc::StringPtr getValueText() const;

  /// \brief Set the constant value as a string representation
  void setValueText(zc::StringPtr text);

private:
  struct Impl;
  zc::Own<Impl> impl;
};

/// \brief Symbol representing a function parameter
class ParameterSymbol final : public ValueSymbol {
public:
  /// \brief Constructor
  ParameterSymbol(SymbolId id, zc::StringPtr name, SymbolFlags flags,
                  const source::SourceLoc& location, zc::Own<TypeSymbol> type,
                  bool isOptional = false);

  // Move constructor and assignment
  ParameterSymbol(ParameterSymbol&& other) noexcept;
  ParameterSymbol& operator=(ParameterSymbol&& other) noexcept;

  // Virtual destructor
  virtual ~ParameterSymbol() noexcept(false);

  // Disable copy
  ZC_DISALLOW_COPY(ParameterSymbol);

  /// \brief LLVM-style type checking
  static bool classof(zc::Maybe<const Symbol&> symbol);

  /// \brief Get symbol kind
  SymbolKind getKind() const override;

  /// \brief Get the parameter index in the parameter list
  size_t getIndex() const;

  /// \brief Set the parameter index in the parameter list
  void setIndex(size_t index);

  /// \brief Check if this parameter is optional
  bool isOptional() const;

private:
  struct Impl;
  zc::Own<Impl> impl;
};

/// \brief Symbol representing a function
class FunctionSymbol final : public ValueSymbol {
public:
  /// \brief Constructor
  FunctionSymbol(SymbolId id, zc::StringPtr name, SymbolFlags flags,
                 const source::SourceLoc& location, zc::Own<TypeSymbol> type);

  // Move constructor and assignment
  FunctionSymbol(FunctionSymbol&& other) noexcept;
  FunctionSymbol& operator=(FunctionSymbol&& other) noexcept;

  // Virtual destructor
  virtual ~FunctionSymbol() noexcept(false);

  // Disable copy
  ZC_DISALLOW_COPY(FunctionSymbol);

  /// \brief LLVM-style type checking
  static bool classof(zc::Maybe<const Symbol&> symbol);

  /// \brief Get symbol kind
  SymbolKind getKind() const override;

  /// \brief Get the function parameters
  zc::ArrayPtr<const ParameterSymbol> getParameters() const;

  /// \brief Add a parameter to this function
  void addParameter(zc::Own<ParameterSymbol> param);

  /// \brief Get parameter count
  size_t getParameterCount() const;

  /// \brief Check if this function is a method
  bool isMethod() const;

  /// \brief Check if this function is a constructor
  bool isConstructor() const;

  /// \brief Check if this function is a destructor
  bool isDestructor() const;

  /// \brief Check if this function is an operator
  bool isOperator() const;

  /// \brief Check if this function is a builtin function
  bool isBuiltin() const;

  /// \brief Check if this function is variadic
  bool isVariadic() const;

private:
  struct Impl;
  zc::Own<Impl> impl;
};

/// \brief Symbol representing a field/member variable
class FieldSymbol : public VariableSymbol {
public:
  /// \brief Constructor
  FieldSymbol(SymbolId id, zc::StringPtr name, SymbolFlags flags, const source::SourceLoc& location,
              zc::Own<TypeSymbol> type, bool isMutable = true);

  // Move constructor and assignment
  FieldSymbol(FieldSymbol&& other) noexcept;
  FieldSymbol& operator=(FieldSymbol&& other) noexcept;

  // Virtual destructor
  virtual ~FieldSymbol() noexcept(false);

  // Disable copy
  ZC_DISALLOW_COPY(FieldSymbol);

  /// \brief LLVM-style type checking
  static bool classof(zc::Maybe<const Symbol&> symbol);

  /// \brief Get symbol kind
  SymbolKind getKind() const override;

  /// \brief Check if this field is public
  bool isPublic() const;

  /// \brief Check if this field is private
  bool isPrivate() const;

  /// \brief Check if this field is protected
  bool isProtected() const;

  /// \brief Check if this field is static
  bool isStatic() const;

  /// \brief Check if this field is mutable
  bool isMutable() const;
};

/// \brief Symbol representing an enum case
class EnumCaseSymbol : public ValueSymbol {
public:
  /// \brief Constructor
  EnumCaseSymbol(SymbolId id, zc::StringPtr name, SymbolFlags flags,
                 const source::SourceLoc& location, zc::Own<TypeSymbol> type);

  // Move constructor and assignment
  EnumCaseSymbol(EnumCaseSymbol&& other) noexcept;
  EnumCaseSymbol& operator=(EnumCaseSymbol&& other) noexcept;

  // Virtual destructor
  virtual ~EnumCaseSymbol() noexcept(false);

  // Disable copy
  ZC_DISALLOW_COPY(EnumCaseSymbol);

  /// \brief LLVM-style type checking
  static bool classof(zc::Maybe<const Symbol&> symbol);

  /// \brief Get symbol kind
  SymbolKind getKind() const override;

  /// \brief Get the associated value for this enum case
  zc::Maybe<int64_t> getAssociatedValue() const;

  /// \brief Set the associated value for this enum case
  void setAssociatedValue(int64_t value);

private:
  struct Impl;
  zc::Own<Impl> impl;
};

}  // namespace symbol
}  // namespace compiler
}  // namespace zomlang
