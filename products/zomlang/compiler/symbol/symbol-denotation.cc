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

#include "zomlang/compiler/symbol/symbol-denotation.h"

#include "zomlang/compiler/symbol/scope.h"
#include "zomlang/compiler/symbol/type-symbol.h"

namespace zomlang {
namespace compiler {
namespace symbol {

// SymbolDenotation::Impl definition
struct SymbolDenotation::Impl {
  SymbolDenotation::Kind kind;
  zc::String name;
  const Scope* scope;
  bool isAbsent = false;
  uint32_t validFromPhase = 0;
  zc::Maybe<const TypeSymbol&> cachedType;

  Impl(SymbolDenotation::Kind k, zc::StringPtr n, const Scope& s)
      : kind(k), name(zc::heapString(n)), scope(&s) {}
};

// SymbolDenotation implementation
SymbolDenotation::SymbolDenotation(Kind kind, zc::StringPtr name, const Scope& scope) noexcept
    : impl(zc::heap<Impl>(kind, name, scope)) {}

SymbolDenotation::~SymbolDenotation() noexcept(false) = default;

SymbolDenotation::SymbolDenotation(SymbolDenotation&&) noexcept = default;
SymbolDenotation& SymbolDenotation::operator=(SymbolDenotation&&) noexcept = default;

SymbolDenotation::Kind SymbolDenotation::getKind() const { return impl->kind; }

zc::StringPtr SymbolDenotation::getName() const { return impl->name; }

const Scope& SymbolDenotation::getScope() const { return *impl->scope; }

bool SymbolDenotation::isTerm() const { return impl->kind == Kind::TERM; }

bool SymbolDenotation::isType() const { return impl->kind == Kind::TYPE; }

bool SymbolDenotation::isPackage() const { return impl->kind == Kind::PACKAGE; }

bool SymbolDenotation::isAbsent() const { return impl->isAbsent; }

void SymbolDenotation::markAbsent() { impl->isAbsent = true; }

uint32_t SymbolDenotation::getValidFromPhase() const { return impl->validFromPhase; }

void SymbolDenotation::setValidFromPhase(uint32_t phase) { impl->validFromPhase = phase; }

zc::Maybe<const TypeSymbol&> SymbolDenotation::getCachedType() const { return impl->cachedType; }

void SymbolDenotation::setCachedType(const TypeSymbol& type) { impl->cachedType = &type; }

}  // namespace symbol
}  // namespace compiler
}  // namespace zomlang
