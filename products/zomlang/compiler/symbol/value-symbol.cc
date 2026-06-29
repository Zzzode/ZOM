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

#include "zomlang/compiler/symbol/value-symbol.h"

#include "zc/core/memory.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/symbol/symbol.h"
#include "zomlang/compiler/symbol/type-symbol.h"

namespace zomlang {
namespace compiler {
namespace symbol {

// ValueSymbol::Impl definition
struct ValueSymbol::Impl {
  zc::Own<TypeSymbol> type;

  explicit Impl(zc::Own<TypeSymbol> type) : type(zc::mv(type)) {}
};

// ValueSymbol implementation
ValueSymbol::ValueSymbol(SymbolId id, zc::StringPtr name, SymbolFlags flags,
                         const source::SourceLoc& location, zc::Own<TypeSymbol> type)
    : Symbol(id, name, flags, location), impl(zc::heap<Impl>(zc::mv(type))) {}

ValueSymbol::ValueSymbol(ValueSymbol&& other) noexcept = default;
ValueSymbol& ValueSymbol::operator=(ValueSymbol&& other) noexcept = default;
ValueSymbol::~ValueSymbol() noexcept(false) = default;

const TypeSymbol& ValueSymbol::getType() const { return *impl->type; }

TypeSymbol& ValueSymbol::getType() { return *impl->type; }

void ValueSymbol::setType(zc::Own<TypeSymbol> newType) { impl->type = zc::mv(newType); }

bool ValueSymbol::isMutable() const { return hasFlag(SymbolFlags::Mutable); }

bool ValueSymbol::isConstant() const { return hasFlag(SymbolFlags::Constant); }

bool ValueSymbol::isValueSymbol() const { return true; }

SymbolKind ValueSymbol::getKind() const { return SymbolKind::Value; }

zc::StringPtr ValueSymbol::getKindName() const { return "value"_zc; }

// VariableSymbol implementation
VariableSymbol::VariableSymbol(SymbolId id, zc::StringPtr name, SymbolFlags flags,
                               const source::SourceLoc& location, zc::Own<TypeSymbol> type,
                               bool isMutable)
    : ValueSymbol(id, name, flags | (isMutable ? SymbolFlags::Mutable : SymbolFlags{}), location,
                  zc::mv(type)) {}

VariableSymbol::VariableSymbol(VariableSymbol&& other) noexcept = default;
VariableSymbol& VariableSymbol::operator=(VariableSymbol&& other) noexcept = default;
VariableSymbol::~VariableSymbol() noexcept(false) = default;

bool VariableSymbol::classof(zc::Maybe<const Symbol&> symbol) {
  ZC_IF_SOME(sm, symbol) { return sm.getKind() == SymbolKind::Variable; }
  return false;
}

SymbolKind VariableSymbol::getKind() const { return SymbolKind::Variable; }

bool VariableSymbol::isParameter() const { return hasFlag(SymbolFlags::Parameter); }

bool VariableSymbol::isCaptured() const { return hasFlag(SymbolFlags::Local); }

bool VariableSymbol::isLocal() const { return !isGlobal() && !isParameter(); }

// ConstantSymbol::Impl definition
struct ConstantSymbol::Impl {
  zc::StringPtr valueText;

  explicit Impl(zc::StringPtr text = ""_zc) : valueText(text) {}
};

// ConstantSymbol implementation
ConstantSymbol::ConstantSymbol(SymbolId id, zc::StringPtr name, SymbolFlags flags,
                               const source::SourceLoc& location, zc::Own<TypeSymbol> type)
    : ValueSymbol(id, name, flags | SymbolFlags::Constant, location, zc::mv(type)),
      impl(zc::heap<Impl>()) {}

ConstantSymbol::ConstantSymbol(ConstantSymbol&& other) noexcept = default;
ConstantSymbol& ConstantSymbol::operator=(ConstantSymbol&& other) noexcept = default;
ConstantSymbol::~ConstantSymbol() noexcept(false) = default;

bool ConstantSymbol::classof(zc::Maybe<const Symbol&> symbol) {
  ZC_IF_SOME(sm, symbol) { return sm.getKind() == SymbolKind::Constant; }
  return false;
}

SymbolKind ConstantSymbol::getKind() const { return SymbolKind::Constant; }

zc::StringPtr ConstantSymbol::getValueText() const { return impl->valueText; }

void ConstantSymbol::setValueText(zc::StringPtr text) { impl->valueText = text; }

// ParameterSymbol::Impl definition
struct ParameterSymbol::Impl {
  size_t index;
  bool isOptional;

  explicit Impl(bool optional = false) : index(0), isOptional(optional) {}
};

// ParameterSymbol implementation
ParameterSymbol::ParameterSymbol(SymbolId id, zc::StringPtr name, SymbolFlags flags,
                                 const source::SourceLoc& location, zc::Own<TypeSymbol> type,
                                 bool isOptional)
    : ValueSymbol(id, name, flags | SymbolFlags::Parameter, location, zc::mv(type)),
      impl(zc::heap<Impl>(isOptional)) {}

ParameterSymbol::ParameterSymbol(ParameterSymbol&& other) noexcept = default;
ParameterSymbol& ParameterSymbol::operator=(ParameterSymbol&& other) noexcept = default;
ParameterSymbol::~ParameterSymbol() noexcept(false) = default;

bool ParameterSymbol::classof(zc::Maybe<const Symbol&> symbol) {
  ZC_IF_SOME(sm, symbol) { return sm.getKind() == SymbolKind::Parameter; }
  return false;
}

SymbolKind ParameterSymbol::getKind() const { return SymbolKind::Parameter; }

size_t ParameterSymbol::getIndex() const { return impl->index; }

void ParameterSymbol::setIndex(size_t index) { impl->index = index; }

bool ParameterSymbol::isOptional() const { return impl->isOptional; }

// FunctionSymbol::Impl definition
struct FunctionSymbol::Impl {
  zc::Vector<ParameterSymbol> parameters;

  Impl() = default;
};

// FunctionSymbol implementation
FunctionSymbol::FunctionSymbol(SymbolId id, zc::StringPtr name, SymbolFlags flags,
                               const source::SourceLoc& location, zc::Own<TypeSymbol> type)
    : ValueSymbol(id, name, flags | SymbolFlags::Function, location, zc::mv(type)),
      impl(zc::heap<Impl>()) {}

FunctionSymbol::FunctionSymbol(FunctionSymbol&& other) noexcept = default;
FunctionSymbol& FunctionSymbol::operator=(FunctionSymbol&& other) noexcept = default;
FunctionSymbol::~FunctionSymbol() noexcept(false) = default;

bool FunctionSymbol::classof(zc::Maybe<const Symbol&> symbol) {
  ZC_IF_SOME(sm, symbol) { return sm.getKind() == SymbolKind::Function; }
  return false;
}

SymbolKind FunctionSymbol::getKind() const { return SymbolKind::Function; }

zc::ArrayPtr<const ParameterSymbol> FunctionSymbol::getParameters() const {
  return impl->parameters.asPtr();
}

void FunctionSymbol::addParameter(zc::Own<ParameterSymbol> param) {
  param->setIndex(impl->parameters.size());
  impl->parameters.add(zc::mv(*param));
}

size_t FunctionSymbol::getParameterCount() const { return impl->parameters.size(); }

bool FunctionSymbol::isMethod() const { return hasFlag(SymbolFlags::Method); }

bool FunctionSymbol::isConstructor() const { return hasFlag(SymbolFlags::Constructor); }

bool FunctionSymbol::isDestructor() const { return hasFlag(SymbolFlags::Destructor); }

bool FunctionSymbol::isOperator() const { return hasFlag(SymbolFlags::Operator); }

bool FunctionSymbol::isBuiltin() const { return hasFlag(SymbolFlags::Builtin); }

bool FunctionSymbol::isVariadic() const { return hasFlag(SymbolFlags::Implicit); }

// FieldSymbol implementation
FieldSymbol::FieldSymbol(SymbolId id, zc::StringPtr name, SymbolFlags flags,
                         const source::SourceLoc& location, zc::Own<TypeSymbol> type,
                         bool isMutable)
    : VariableSymbol(id, name, flags | SymbolFlags::Field, location, zc::mv(type), isMutable) {}

FieldSymbol::FieldSymbol(FieldSymbol&& other) noexcept = default;
FieldSymbol& FieldSymbol::operator=(FieldSymbol&& other) noexcept = default;
FieldSymbol::~FieldSymbol() noexcept(false) = default;

bool FieldSymbol::classof(zc::Maybe<const Symbol&> symbol) {
  ZC_IF_SOME(sm, symbol) { return sm.getKind() == SymbolKind::Field; }
  return false;
}

SymbolKind FieldSymbol::getKind() const { return SymbolKind::Field; }

bool FieldSymbol::isPublic() const { return hasFlag(SymbolFlags::Public); }

bool FieldSymbol::isPrivate() const { return hasFlag(SymbolFlags::Private); }

bool FieldSymbol::isProtected() const { return hasFlag(SymbolFlags::Protected); }

bool FieldSymbol::isStatic() const { return hasFlag(SymbolFlags::Static); }

bool FieldSymbol::isMutable() const {
  // Use Symbol::isMutable() instead of ValueSymbol::isMutable()
  // to properly check both Mutable and Final flags
  return Symbol::isMutable();
}

// EnumCaseSymbol::Impl definition
struct EnumCaseSymbol::Impl {
  zc::Maybe<int64_t> associatedValue;

  Impl() = default;
};

// EnumCaseSymbol implementation
EnumCaseSymbol::EnumCaseSymbol(SymbolId id, zc::StringPtr name, SymbolFlags flags,
                               const source::SourceLoc& location, zc::Own<TypeSymbol> type)
    : ValueSymbol(id, name, flags | SymbolFlags::Constant, location, zc::mv(type)),
      impl(zc::heap<Impl>()) {}

EnumCaseSymbol::EnumCaseSymbol(EnumCaseSymbol&& other) noexcept = default;
EnumCaseSymbol& EnumCaseSymbol::operator=(EnumCaseSymbol&& other) noexcept = default;
EnumCaseSymbol::~EnumCaseSymbol() noexcept(false) = default;

bool EnumCaseSymbol::classof(zc::Maybe<const Symbol&> symbol) {
  ZC_IF_SOME(sm, symbol) { return sm.getKind() == SymbolKind::EnumCase; }
  return false;
}

SymbolKind EnumCaseSymbol::getKind() const { return SymbolKind::EnumCase; }

zc::Maybe<int64_t> EnumCaseSymbol::getAssociatedValue() const { return impl->associatedValue; }

void EnumCaseSymbol::setAssociatedValue(int64_t value) { impl->associatedValue = value; }

}  // namespace symbol
}  // namespace compiler
}  // namespace zomlang
