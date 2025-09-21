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

#include "zomlang/compiler/symbol/symbol.h"

#include "zomlang/compiler/source/manager.h"
#include "zomlang/compiler/symbol/scope.h"
#include "zomlang/compiler/symbol/type-symbol.h"

namespace zomlang {
namespace compiler {
namespace symbol {

/// \brief This implementation unifies symbol identity, properties, and flags into a single 64-bit
/// system for optimal memory usage and cache locality.
struct Symbol::Impl {
  /// \brief Construct symbol implementation
  Impl(SymbolId id, zc::StringPtr name, SymbolFlags flags, const source::SourceLoc& location)
      : id(id), name(name), flags(flags), location(location), scope(zc::none), type(zc::none) {}

  Impl(SymbolId id, zc::StringPtr name, SymbolFlags flags)
      : id(id), name(name), flags(flags), location(), scope(zc::none), type(zc::none) {}

  // Identity and properties
  SymbolId id;
  zc::StringPtr name;
  SymbolFlags flags;
  source::SourceLoc location;

  // Symbol relationships
  zc::Maybe<const Scope&> scope;
  zc::Maybe<TypeSymbol&> type;

  // 64-bit packed properties
  union Properties {
    struct {
      uint8_t visibility : 2;  // 00=public, 01=private, 10=protected, 11=internal
      uint8_t storage : 2;     // 00=mutable, 01=final, 10=lazy, 11=inline
      uint8_t lifetime : 2;    // 00=static, 01=instance, 10=global, 11=local
      uint8_t reserved : 2;    // Reserved for future use
    } bits;
    uint8_t packed;
  } properties;
};

/// \brief Symbol constructor with location
/// \param id Symbol identifier
/// \param name Symbol name
/// \param flags Symbol properties flags
/// \param location Source code location
Symbol::Symbol(SymbolId id, zc::StringPtr name, SymbolFlags flags,
               const source::SourceLoc& location) noexcept
    : impl(zc::heap<Impl>(id, name, flags, location)) {}

/// \brief Symbol constructor without location
/// \param id Symbol identifier
/// \param name Symbol name
/// \param flags Symbol properties flags
Symbol::Symbol(SymbolId id, zc::StringPtr name, SymbolFlags flags) noexcept
    : impl(zc::heap<Impl>(id, name, flags)) {}

/// \brief Move constructor
Symbol::Symbol(Symbol&& other) noexcept = default;

/// \brief Move assignment
Symbol& Symbol::operator=(Symbol&& other) noexcept = default;

/// \brief Destructor
Symbol::~Symbol() noexcept(false) = default;

/// \brief Get symbol ID
SymbolId Symbol::getId() const { return impl->id; }

/// \brief Get symbol name
zc::StringPtr Symbol::getName() const { return impl->name; }

/// \brief Set symbol name
void Symbol::setName(zc::StringPtr name) { impl->name = name; }

/// \brief Get source location
const source::SourceLoc& Symbol::getLocation() const { return impl->location; }

/// \brief Get file name from location
zc::StringPtr Symbol::getFileName(const source::SourceManager& sourceManager) const {
  if (!impl->location.isInvalid()) { return sourceManager.getDisplayNameForLoc(impl->location); }
  return "<unknown>"_zc;
}

/// \brief Get line number from location
uint32_t Symbol::getLine(const source::SourceManager& sourceManager) const {
  if (!impl->location.isInvalid()) { return sourceManager.getLineNumber(impl->location); }
  return 0u;
}

/// \brief Check if symbol has specific flag
bool Symbol::hasFlag(SymbolFlags flag) const { return (impl->flags & flag) != SymbolFlags::None; }

/// \brief Check if symbol has any of the given flags
bool Symbol::hasAnyFlag(SymbolFlags mask) const {
  return (impl->flags & mask) != SymbolFlags::None;
}

/// \brief Check if symbol has all of the given flags
bool Symbol::hasAllFlags(SymbolFlags mask) const { return (impl->flags & mask) == mask; }

/// \brief Add flag to symbol
void Symbol::addFlag(SymbolFlags flag) { impl->flags |= flag; }

/// \brief Remove flag from symbol
void Symbol::removeFlag(SymbolFlags flag) { impl->flags &= ~flag; }

/// \brief Get all flags
SymbolFlags Symbol::getFlags() const { return impl->flags; }

/// \brief Check if this is a type symbol
bool Symbol::isTypeSymbol() const { return false; }

/// \brief Check if this is a value symbol
bool Symbol::isValueSymbol() const { return false; }

/// \brief Check if this is a module symbol
bool Symbol::isModuleSymbol() const { return false; }

/// \brief Check if this is a function symbol
bool Symbol::isFunctionSymbol() const { return false; }

/// \brief Check if this is a variable symbol
bool Symbol::isVariableSymbol() const { return false; }

/// \brief Check if this is a class symbol
bool Symbol::isClassSymbol() const { return false; }

zc::Maybe<const Scope&> Symbol::getScope() const { return impl->scope; }

zc::Maybe<const TypeSymbol&> Symbol::getType() const { return impl->type; }

void Symbol::setScope(zc::Maybe<const Scope&> scope) { impl->scope = scope; }

void Symbol::setType(zc::Maybe<TypeSymbol&> type) { impl->type = type; }

/// \brief Check if symbol is public
bool Symbol::isPublic() const {
  return !hasAnyFlag(SymbolFlags::Private | SymbolFlags::Protected | SymbolFlags::Internal);
}

/// \brief Check if symbol is private
bool Symbol::isPrivate() const { return hasFlag(SymbolFlags::Private); }

/// \brief Check if symbol is protected
bool Symbol::isProtected() const { return hasFlag(SymbolFlags::Protected); }

/// \brief Check if symbol is internal
bool Symbol::isInternal() const { return hasFlag(SymbolFlags::Internal); }

/// \brief Check if symbol is mutable
bool Symbol::isMutable() const {
  return !hasFlag(SymbolFlags::Final) && hasFlag(SymbolFlags::Mutable);
}

/// \brief Check if symbol is final
bool Symbol::isFinal() const { return hasFlag(SymbolFlags::Final); }

/// \brief Check if symbol is lazy
bool Symbol::isLazy() const { return hasFlag(SymbolFlags::Lazy); }

/// \brief Check if symbol is inline
bool Symbol::isInline() const { return hasFlag(SymbolFlags::Inline); }

/// \brief Check if symbol is static
bool Symbol::isStatic() const { return hasFlag(SymbolFlags::Static); }

/// \brief Check if symbol is instance member
bool Symbol::isInstance() const { return !hasFlag(SymbolFlags::Static); }

/// \brief Check if symbol is global
bool Symbol::isGlobal() const { return hasFlag(SymbolFlags::Global); }

/// \brief Check if symbol is local
bool Symbol::isLocal() const { return hasFlag(SymbolFlags::Local); }

/// \brief Convert symbol to string representation
zc::String Symbol::toString() const {
  zc::String result = zc::str(getKindName());
  result = zc::str(result, "(", getName(), ")");

  if (getLocation().isValid()) { result = zc::str(result, " @ <location>"); }

  return result;
}

}  // namespace symbol
}  // namespace compiler
}  // namespace zomlang
