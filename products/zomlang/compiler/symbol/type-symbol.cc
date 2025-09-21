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

#include "zomlang/compiler/symbol/type-symbol.h"

#include "zc/core/array.h"
#include "zc/core/memory.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/ast/expression.h"
#include "zomlang/compiler/ast/type.h"
#include "zomlang/compiler/symbol/symbol.h"

namespace zomlang {
namespace compiler {
namespace symbol {

// TypeSymbol::Impl definition
struct TypeSymbol::Impl {
  zc::Maybe<const ast::Type&> astType;
  zc::Vector<zc::Maybe<const TypeSymbol&>> superTypes;
  zc::Vector<zc::Maybe<const TypeSymbol&>> typeParameters;
  zc::Vector<zc::Maybe<const TypeSymbol&>> upperBounds;
  zc::Vector<zc::Maybe<const TypeSymbol&>> lowerBounds;
  zc::Maybe<const TypeSymbol&> declaringType;

  Impl() = default;
};

// TypeSymbol implementation
TypeSymbol::TypeSymbol(SymbolId id, zc::StringPtr name, SymbolFlags flags,
                       const source::SourceLoc& location) noexcept
    : Symbol(id, name, flags, location), impl(zc::heap<Impl>()) {}

TypeSymbol::TypeSymbol(TypeSymbol&& other) noexcept = default;
TypeSymbol& TypeSymbol::operator=(TypeSymbol&& other) noexcept = default;
TypeSymbol::~TypeSymbol() noexcept(false) = default;

zc::Maybe<const ast::Type&> TypeSymbol::getAstType() const { return impl->astType; }

void TypeSymbol::setAstType(zc::Maybe<const ast::Type&> type) { impl->astType = type; }

bool TypeSymbol::isClass() const { return hasFlag(SymbolFlags::Class); }

bool TypeSymbol::isInterface() const { return hasFlag(SymbolFlags::Interface); }

bool TypeSymbol::isGeneric() const { return impl->typeParameters.size() != 0; }

bool TypeSymbol::isPrimitive() const { return hasFlag(SymbolFlags::Builtin); }

bool TypeSymbol::isAlias() const { return hasFlag(SymbolFlags::TypeAlias); }

bool TypeSymbol::isFunction() const { return hasFlag(SymbolFlags::Function); }

bool TypeSymbol::isEnumType() const { return hasFlag(SymbolFlags::Enum); }

bool TypeSymbol::isUnionType() const {
  ZC_IF_SOME(astType, impl->astType) { return astType.getKind() == ast::SyntaxKind::kUnionType; }
  return false;
}

bool TypeSymbol::isIntersectionType() const {
  ZC_IF_SOME(astType, impl->astType) {
    return astType.getKind() == ast::SyntaxKind::kIntersectionType;
  }
  return false;
}

bool TypeSymbol::isSubtypeOf(const TypeSymbol& other) const {
  // Same type is always a subtype of itself
  if (this == &other) { return true; }

  // Check if names are the same (for nominal typing)
  if (getName() == other.getName()) { return true; }

  // Check supertype relationships
  const auto supertypes = getSupertypes();
  for (const auto& supertype : supertypes) {
    ZC_IF_SOME(super, supertype) {
      if (super.isSubtypeOf(other)) { return true; }
    }
  }

  // For class symbols, check inheritance hierarchy
  if (getKind() == SymbolKind::Class && other.getKind() == SymbolKind::Class) {
    const auto& thisClass = static_cast<const ClassSymbol&>(*this);
    const auto& otherClass = static_cast<const ClassSymbol&>(other);

    // Check superclass chain
    ZC_IF_SOME(superclass, thisClass.getSuperclass()) {
      if (superclass.isSubtypeOf(otherClass)) { return true; }
    }

    // Check implemented interfaces
    const auto interfaces = thisClass.getInterfaces();
    for (const auto& interface : interfaces) {
      ZC_IF_SOME(iface, interface) {
        if (iface.isSubtypeOf(otherClass)) { return true; }
      }
    }
  }

  return false;
}

bool TypeSymbol::isAssignableFrom(const TypeSymbol& other) const {
  // A type can be assigned from its subtypes
  if (other.isSubtypeOf(*this)) { return true; }

  // Same type assignment
  if (this == &other || getName() == other.getName()) { return true; }

  // Built-in type compatibility rules
  if (getKind() == SymbolKind::Type && other.getKind() == SymbolKind::Type) {
    // Allow numeric type widening (i32 -> f32)
    if (getName() == "f32"_zc && other.getName() == "i32"_zc) { return true; }
  }

  return false;
}

zc::Array<zc::Maybe<const TypeSymbol&>> TypeSymbol::getSupertypes() const {
  auto builder = zc::heapArrayBuilder<zc::Maybe<const TypeSymbol&>>(impl->superTypes.size());
  for (const auto& supertype : impl->superTypes) { builder.add(supertype); }
  return builder.finish();
}

zc::Array<zc::Maybe<const TypeSymbol&>> TypeSymbol::getSubtypes() const {
  // TODO: Implement subtype collection
  return zc::heapArray<zc::Maybe<const TypeSymbol&>>(0);
}

zc::Array<zc::Maybe<const TypeSymbol&>> TypeSymbol::getTypeParameters() const {
  auto builder = zc::heapArrayBuilder<zc::Maybe<const TypeSymbol&>>(impl->typeParameters.size());
  for (const auto& param : impl->typeParameters) { builder.add(param); }
  return builder.finish();
}

zc::Maybe<const TypeSymbol&> TypeSymbol::getDeclaringType() const { return impl->declaringType; }

zc::Array<zc::Maybe<const TypeSymbol&>> TypeSymbol::getUpperBounds() const {
  auto builder = zc::heapArrayBuilder<zc::Maybe<const TypeSymbol&>>(impl->upperBounds.size());
  for (const auto& bound : impl->upperBounds) { builder.add(bound); }
  return builder.finish();
}

zc::Array<zc::Maybe<const TypeSymbol&>> TypeSymbol::getLowerBounds() const {
  auto builder = zc::heapArrayBuilder<zc::Maybe<const TypeSymbol&>>(impl->lowerBounds.size());
  for (const auto& bound : impl->lowerBounds) { builder.add(bound); }
  return builder.finish();
}

zc::String TypeSymbol::getQualifiedName() const {
  // TODO: Implement qualified name generation
  return zc::heapString(getName());
}

bool TypeSymbol::isNumeric() const {
  auto name = getName();
  return name == "i32"_zc || name == "f32"_zc || name == "double"_zc;
}

bool TypeSymbol::isString() const { return getName() == "str"_zc; }

bool TypeSymbol::isBoolean() const { return getName() == "bool"_zc; }

bool TypeSymbol::isVoid() const { return getName() == "unit"_zc; }

// BuiltInTypeSymbol::Impl definition
struct BuiltInTypeSymbol::Impl {
  Impl() = default;
};

// BuiltInTypeSymbol implementation
BuiltInTypeSymbol::BuiltInTypeSymbol(SymbolId id, zc::StringPtr name, SymbolFlags flags,
                                     const source::SourceLoc& location) noexcept
    : TypeSymbol(id, name, flags, location), impl(zc::heap<Impl>()) {}

zc::Own<BuiltInTypeSymbol> BuiltInTypeSymbol::createI32(SymbolId id,
                                                        const source::SourceLoc& location) {
  return zc::heap<BuiltInTypeSymbol>(id, "i32"_zc, SymbolFlags::TypeKind | SymbolFlags::Builtin,
                                     location);
}

zc::Own<BuiltInTypeSymbol> BuiltInTypeSymbol::createF32(SymbolId id,
                                                        const source::SourceLoc& location) {
  return zc::heap<BuiltInTypeSymbol>(id, "f32"_zc, SymbolFlags::TypeKind | SymbolFlags::Builtin,
                                     location);
}

zc::Own<BuiltInTypeSymbol> BuiltInTypeSymbol::createStr(SymbolId id,
                                                        const source::SourceLoc& location) {
  return zc::heap<BuiltInTypeSymbol>(id, "str"_zc, SymbolFlags::TypeKind | SymbolFlags::Builtin,
                                     location);
}

zc::Own<BuiltInTypeSymbol> BuiltInTypeSymbol::createBool(SymbolId id,
                                                         const source::SourceLoc& location) {
  return zc::heap<BuiltInTypeSymbol>(id, "bool"_zc, SymbolFlags::TypeKind | SymbolFlags::Builtin,
                                     location);
}

zc::Own<BuiltInTypeSymbol> BuiltInTypeSymbol::createUnit(SymbolId id,
                                                         const source::SourceLoc& location) {
  return zc::heap<BuiltInTypeSymbol>(id, "unit"_zc, SymbolFlags::TypeKind | SymbolFlags::Builtin,
                                     location);
}

// InterfaceSymbol::Impl definition
struct InterfaceSymbol::Impl {
  Impl() = default;
};

// InterfaceSymbol implementation
InterfaceSymbol::InterfaceSymbol(SymbolId id, zc::StringPtr name, SymbolFlags flags,
                                 const source::SourceLoc& location) noexcept
    : TypeSymbol(id, name, flags, location), impl(zc::heap<Impl>()) {}

InterfaceSymbol::InterfaceSymbol(InterfaceSymbol&& other) noexcept = default;
InterfaceSymbol& InterfaceSymbol::operator=(InterfaceSymbol&& other) noexcept = default;
InterfaceSymbol::~InterfaceSymbol() noexcept(false) = default;

// ClassSymbol::Impl definition
struct ClassSymbol::Impl {
  zc::Maybe<const ClassSymbol&> superclass;
  zc::Vector<zc::Maybe<const ClassSymbol&>> interfaces;
  zc::Vector<zc::Maybe<const Symbol&>> members;
  zc::Vector<zc::Maybe<const Symbol&>> constructors;

  Impl() = default;
};

// ClassSymbol implementation
ClassSymbol::ClassSymbol(SymbolId id, zc::StringPtr name, SymbolFlags flags,
                         const source::SourceLoc& location) noexcept
    : TypeSymbol(id, name, flags, location), impl(zc::heap<Impl>()) {}

ClassSymbol::ClassSymbol(ClassSymbol&& other) noexcept = default;
ClassSymbol& ClassSymbol::operator=(ClassSymbol&& other) noexcept = default;
ClassSymbol::~ClassSymbol() noexcept(false) = default;

zc::Maybe<const ClassSymbol&> ClassSymbol::getSuperclass() const { return impl->superclass; }

void ClassSymbol::setSuperclass(zc::Maybe<const ClassSymbol&> newSuperclass) {
  impl->superclass = newSuperclass;
}

zc::Array<zc::Maybe<const ClassSymbol&>> ClassSymbol::getInterfaces() const {
  auto builder = zc::heapArrayBuilder<zc::Maybe<const ClassSymbol&>>(impl->interfaces.size());
  for (const auto& interface : impl->interfaces) { builder.add(interface); }
  return builder.finish();
}

void ClassSymbol::addInterface(zc::Maybe<const ClassSymbol&> interface) {
  impl->interfaces.add(interface);
}

zc::Array<zc::Maybe<const Symbol&>> ClassSymbol::getMembers() const {
  auto builder = zc::heapArrayBuilder<zc::Maybe<const Symbol&>>(impl->members.size());
  for (const auto& member : impl->members) { builder.add(member); }
  return builder.finish();
}

void ClassSymbol::addMember(zc::Maybe<const Symbol&> member) { impl->members.add(member); }

zc::Array<zc::Maybe<const Symbol&>> ClassSymbol::getConstructors() const {
  auto builder = zc::heapArrayBuilder<zc::Maybe<const Symbol&>>(impl->constructors.size());
  for (const auto& constructor : impl->constructors) { builder.add(constructor); }
  return builder.finish();
}

void ClassSymbol::addConstructor(zc::Maybe<const Symbol&> constructor) {
  impl->constructors.add(constructor);
}

bool ClassSymbol::isAbstract() const { return hasFlag(SymbolFlags::Abstract); }

bool ClassSymbol::isFinal() const { return hasFlag(SymbolFlags::Final); }

// FunctionTypeSymbol::Impl definition
struct FunctionTypeSymbol::Impl {
  zc::Maybe<const TypeSymbol&> returnType;
  zc::Vector<zc::Maybe<const TypeSymbol&>> parameterTypes;
  bool variadic = false;

  Impl() = default;
};

// FunctionTypeSymbol implementation
FunctionTypeSymbol::FunctionTypeSymbol(SymbolId id, zc::StringPtr name, SymbolFlags flags,
                                       const source::SourceLoc& location) noexcept
    : TypeSymbol(id, name, flags, location), impl(zc::heap<Impl>()) {}

FunctionTypeSymbol::FunctionTypeSymbol(FunctionTypeSymbol&& other) noexcept = default;
FunctionTypeSymbol& FunctionTypeSymbol::operator=(FunctionTypeSymbol&& other) noexcept = default;
FunctionTypeSymbol::~FunctionTypeSymbol() noexcept(false) = default;

zc::Maybe<const TypeSymbol&> FunctionTypeSymbol::getReturnType() const { return impl->returnType; }

void FunctionTypeSymbol::setReturnType(zc::Maybe<const TypeSymbol&> type) {
  impl->returnType = type;
}

zc::Array<zc::Maybe<const TypeSymbol&>> FunctionTypeSymbol::getParameterTypes() const {
  auto builder = zc::heapArrayBuilder<zc::Maybe<const TypeSymbol&>>(impl->parameterTypes.size());
  for (const auto& paramType : impl->parameterTypes) { builder.add(paramType); }
  return builder.finish();
}

void FunctionTypeSymbol::addParameterType(zc::Maybe<const TypeSymbol&> type) {
  impl->parameterTypes.add(type);
}

bool FunctionTypeSymbol::isVariadic() const { return impl->variadic; }

void FunctionTypeSymbol::setVariadic(bool isVariadicParam) { impl->variadic = isVariadicParam; }

bool FunctionTypeSymbol::isMoreSpecificThan(const FunctionTypeSymbol& other) const {
  // TODO: Implement specificity comparison logic
  return false;
}

// TypeParameterSymbol::Impl definition
struct TypeParameterSymbol::Impl {
  TypeParameterSymbol::Variance variance = TypeParameterSymbol::Variance::Invariant;
  zc::Vector<zc::Maybe<const TypeSymbol&>> bounds;

  Impl() = default;
};

// TypeParameterSymbol implementation
TypeParameterSymbol::TypeParameterSymbol(SymbolId id, zc::StringPtr name, SymbolFlags flags,
                                         const source::SourceLoc& location) noexcept
    : TypeSymbol(id, name, flags, location), impl(zc::heap<Impl>()) {}

TypeParameterSymbol::TypeParameterSymbol(TypeParameterSymbol&& other) noexcept = default;
TypeParameterSymbol& TypeParameterSymbol::operator=(TypeParameterSymbol&& other) noexcept = default;
TypeParameterSymbol::~TypeParameterSymbol() noexcept(false) = default;

TypeParameterSymbol::Variance TypeParameterSymbol::getVariance() const { return impl->variance; }

void TypeParameterSymbol::setVariance(Variance variance) { impl->variance = variance; }

zc::Array<zc::Maybe<const TypeSymbol&>> TypeParameterSymbol::getBounds() const {
  auto builder = zc::heapArrayBuilder<zc::Maybe<const TypeSymbol&>>(impl->bounds.size());
  for (const auto& bound : impl->bounds) { builder.add(bound); }
  return builder.finish();
}

void TypeParameterSymbol::addBound(zc::Maybe<const TypeSymbol&> bound) { impl->bounds.add(bound); }

}  // namespace symbol
}  // namespace compiler
}  // namespace zomlang
