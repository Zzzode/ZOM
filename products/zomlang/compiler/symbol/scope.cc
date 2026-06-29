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

#include "zomlang/compiler/symbol/scope.h"

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/debug.h"
#include "zc/core/map.h"
#include "zc/core/memory.h"
#include "zc/core/string.h"
#include "zomlang/compiler/symbol/symbol.h"

namespace zomlang {
namespace compiler {
namespace symbol {

// Scope implementation using pimpl pattern
struct Scope::Impl {
  Impl(Kind kind, zc::StringPtr name, zc::Maybe<Scope&> parent)
      : kind(kind), name(name), parent(parent) {}

  // Basic properties
  Kind kind;
  zc::StringPtr name;
  zc::Maybe<Scope&> parent;

  // Symbol storage
  zc::HashMap<zc::StringPtr, zc::Own<Symbol>> symbols;

  // Child scope storage
  zc::HashMap<zc::StringPtr, zc::Own<Scope>> children;
};

// Scope constructor
Scope::Scope(Kind kind, zc::StringPtr name, zc::Maybe<Scope&> parent) noexcept
    : impl(zc::heap<Impl>(kind, name, parent)) {}

// Scope destructor
Scope::~Scope() noexcept(false) = default;

// Basic properties
Scope::Kind Scope::getKind() const { return impl->kind; }

zc::StringPtr Scope::getName() const { return impl->name; }

zc::String Scope::getFullName() const {
  ZC_IF_SOME(parent, impl->parent) {
    zc::String parentFullName = parent.getFullName();
    if (parentFullName.size() == 0) { return zc::str(impl->name); }
    return zc::str(parentFullName, ".", impl->name);
  }

  return zc::str(impl->name);
}

zc::Maybe<const Scope&> Scope::getParent() const { return impl->parent; }

bool Scope::isRoot() const { return impl->parent == zc::none; }

// Symbol management
void Scope::addSymbol(zc::Own<Symbol> symbol) {
  impl->symbols.insert(symbol->getName(), zc::mv(symbol));
}

void Scope::removeSymbol(zc::StringPtr name) { impl->symbols.erase(name); }

zc::Maybe<Symbol&> Scope::lookupSymbol(zc::StringPtr name) { return lookupSymbolRecursively(name); }

zc::Maybe<Symbol&> Scope::lookupSymbolLocally(zc::StringPtr name) {
  auto it = impl->symbols.find(name);
  ZC_IF_SOME(symbol, it) { return *symbol; }
  return zc::none;
}

zc::Maybe<Symbol&> Scope::lookupSymbolRecursively(zc::StringPtr name) {
  ZC_IF_SOME(symbol, impl->symbols.find(name)) { return *symbol; }
  ZC_IF_SOME(parent, impl->parent) { return parent.lookupSymbolRecursively(name); }
  return zc::none;
}

bool Scope::hasSymbol(zc::StringPtr name) { return lookupSymbolLocally(name) != zc::none; }

size_t Scope::getSymbolCount() const { return impl->symbols.size(); }

void Scope::addChild(zc::Own<Scope> child) {
  impl->children.insert(child->getName(), zc::mv(child));
}

void Scope::removeChild(Scope& child) { impl->children.erase(child.getName()); }

zc::Maybe<const Scope&> Scope::getChild(zc::StringPtr name) const {
  auto it = impl->children.find(name);
  ZC_IF_SOME(child, it) { return *child; }
  return zc::none;
}

bool Scope::hasChild(Scope& child) const { return getChild(child.getName()) != zc::none; }

// Scope relationships
bool Scope::isAncestorOf(const Scope& other) const {
  const Scope* current = &other;
  while (current != nullptr) {
    if (current == this) { return true; }
    ZC_IF_SOME(parent, current->impl->parent) { current = &parent; }
    else { current = nullptr; }
  }
  return false;
}

bool Scope::isDescendantOf(const Scope& other) const { return other.isAncestorOf(*this); }

bool Scope::isSiblingOf(const Scope& other) const {
  ZC_IF_SOME(parent, impl->parent) {
    ZC_IF_SOME(otherParent, other.impl->parent) { return &parent == &otherParent; }
  }
  return false;
}

zc::Maybe<const Scope&> Scope::getCommonAncestor(const Scope& other) const {
  // Use references to avoid pointer arithmetic
  const Scope* a = this;
  const Scope* b = &other;

  // Calculate depths
  size_t depthA = 0;
  size_t depthB = 0;

  // Calculate depth of first scope
  const Scope* curr = a;
  while (curr != nullptr) {
    depthA++;
    ZC_IF_SOME(parent, curr->impl->parent) { curr = &parent; }
    else { curr = nullptr; }
  }

  // Calculate depth of second scope
  curr = b;
  while (curr != nullptr) {
    depthB++;
    ZC_IF_SOME(parent, curr->impl->parent) { curr = &parent; }
    else { curr = nullptr; }
  }

  // Align depths by moving deeper scope up
  while (depthA > depthB && a != nullptr) {
    ZC_IF_SOME(parent, a->impl->parent) { a = &parent; }
    else { a = nullptr; }
    depthA--;
  }

  while (depthB > depthA && b != nullptr) {
    ZC_IF_SOME(parent, b->impl->parent) { b = &parent; }
    else { b = nullptr; }
    depthB--;
  }

  // Find common ancestor by moving both up simultaneously
  while (a != nullptr && b != nullptr) {
    if (a == b) { return *a; }
    ZC_IF_SOME(parentA, a->impl->parent) { a = &parentA; }
    else { a = nullptr; }
    ZC_IF_SOME(parentB, b->impl->parent) { b = &parentB; }
    else { b = nullptr; }
  }

  return zc::none;
}

// Scope validation
bool Scope::isValid() const {
  // Check basic invariants
  if (impl->name.size() == 0) { return false; }

  // Check that parent doesn't form a cycle
  ZC_IF_SOME(parent, impl->parent) {
    const Scope* current = &parent;
    while (current != nullptr) {
      if (current == this) { return false; }
      ZC_IF_SOME(parent, current->impl->parent) { current = &parent; }
      else { current = nullptr; }
    }
  }

  return true;
}

void Scope::validate() const { ZC_REQUIRE(isValid(), "Invalid scope: ", toString()); }

// Symbol overloads
zc::Maybe<Symbol&> Scope::operator[](zc::StringPtr name) { return lookupSymbol(name); }

bool Scope::operator==(const Scope& other) const { return this == &other; }

bool Scope::operator!=(const Scope& other) const { return !(*this == other); }

// Utility functions
zc::String Scope::toString() const {
  return zc::str("Scope[", impl->name, ":", static_cast<int>(impl->kind), "]");
}

// Scope creation helpers
zc::Own<Scope> Scope::createGlobalScope() { return zc::heap<Scope>(Kind::Global, "global"); }

zc::Own<Scope> Scope::createPackageScope(zc::StringPtr name, Scope& parent) {
  return zc::heap<Scope>(Kind::Package, name, parent);
}

zc::Own<Scope> Scope::createClassScope(zc::StringPtr name, Scope& parent) {
  return zc::heap<Scope>(Kind::Class, name, parent);
}

zc::Own<Scope> Scope::createFunctionScope(zc::StringPtr name, Scope& parent) {
  return zc::heap<Scope>(Kind::Function, name, parent);
}

zc::Own<Scope> Scope::createBlockScope(zc::StringPtr name, Scope& parent) {
  return zc::heap<Scope>(Kind::Block, name, parent);
}

// ScopeManager implementation
struct ScopeManager::Impl {
  Impl() : currentScope(zc::none) {}

  // All scopes managed by this manager - stored as owned pointers to avoid reference invalidation
  zc::Vector<zc::Own<Scope>> ownedScopes;

  // Current scope stack
  zc::Vector<zc::Maybe<const Scope&>> scopeStack;

  // Current active scope
  zc::Maybe<const Scope&> currentScope;

  // Scope lookup caches - store references to scopes in the vector
  zc::HashMap<zc::StringPtr, zc::Maybe<Scope&>> packageScopes;
  zc::HashMap<zc::StringPtr, zc::Maybe<Scope&>> classScopes;
  zc::HashMap<zc::StringPtr, zc::Maybe<Scope&>> functionScopes;
};

// ScopeManager constructor
ScopeManager::ScopeManager() noexcept : impl(zc::heap<Impl>()) {
  // Create a global scope by default
  Scope& globalScope = createScope(Scope::Kind::Global, "global");
  setCurrentScope(globalScope);
}

// ScopeManager destructor
ScopeManager::~ScopeManager() noexcept(false) = default;

// Scope lifecycle
Scope& ScopeManager::createScope(Scope::Kind kind, zc::StringPtr name, zc::Maybe<Scope&> parent) {
  // Use heap allocation to avoid reference invalidation when vector grows
  auto scopePtr = zc::heap<Scope>(kind, name, parent);
  Scope& scope = *scopePtr;

  // Store the owned pointer in a vector to manage lifetime
  impl->ownedScopes.add(zc::mv(scopePtr));

  // Add this scope as a child to its parent if it has one
  ZC_IF_SOME(parentScope, parent) {
    // We need to create a separate owned pointer for the parent's children collection
    // This is necessary because the parent needs to own its children
    auto childPtr = zc::heap<Scope>(kind, name, parent);
    parentScope.addChild(zc::mv(childPtr));
  }

  // Update lookup caches - store references to the heap-allocated scopes
  switch (kind) {
    case Scope::Kind::Package:
      impl->packageScopes.insert(name, scope);
      break;
    case Scope::Kind::Class:
      impl->classScopes.insert(name, scope);
      break;
    case Scope::Kind::Function:
      impl->functionScopes.insert(name, scope);
      break;
    default:
      break;
  }

  return scope;
}

void ScopeManager::destroyScope(Scope& scope) {
  // Save scope information before destruction
  Scope::Kind scopeKind = scope.getKind();
  zc::String scopeName = zc::heapString(scope.getName());

  // Remove from lookup caches first (while scope is still valid)
  switch (scopeKind) {
    case Scope::Kind::Package:
      impl->packageScopes.erase(scopeName);
      break;
    case Scope::Kind::Class:
      impl->classScopes.erase(scopeName);
      break;
    case Scope::Kind::Function:
      impl->functionScopes.erase(scopeName);
      break;
    default:
      break;
  }

  // Remove from scope stack if present (while scope is still valid)
  for (size_t i = 0; i < impl->scopeStack.size(); ++i) {
    ZC_IF_SOME(sp, impl->scopeStack[i]) {
      if (sp == scope) {
        // Shift remaining elements
        for (size_t j = i + 1; j < impl->scopeStack.size(); ++j) {
          impl->scopeStack[j - 1] = impl->scopeStack[j];
        }
        impl->scopeStack.removeLast();
        break;
      }
    }
  }

  // Update current scope if it's the one being destroyed (while scope is still valid)
  ZC_IF_SOME(currScope, impl->currentScope) {
    if (currScope == scope) {
      if (!impl->scopeStack.empty()) {
        impl->currentScope = impl->scopeStack.back();
      } else {
        impl->currentScope = zc::none;
      }
    }
  }

  // Remove from ownedScopes array (this will destroy the scope)
  zc::Vector<zc::Own<Scope>> newOwnedScopes;
  newOwnedScopes.reserve(impl->ownedScopes.size());
  for (auto& ownedScope : impl->ownedScopes) {
    if (*ownedScope != scope) { newOwnedScopes.add(zc::mv(ownedScope)); }
  }
  impl->ownedScopes = zc::mv(newOwnedScopes);
}

zc::Maybe<const Scope&> ScopeManager::getCurrentScope() const { return impl->currentScope; }

void ScopeManager::setCurrentScope(const Scope& scope) { impl->currentScope = scope; }

void ScopeManager::pushScope(const Scope& scope) {
  impl->scopeStack.add(scope);
  impl->currentScope = scope;
}

void ScopeManager::popScope() {
  ZC_REQUIRE(!impl->scopeStack.empty(), "Scope stack is empty");

  impl->scopeStack.removeLast();

  if (!impl->scopeStack.empty()) {
    ZC_IF_SOME(scope, impl->scopeStack.back()) { impl->currentScope = scope; }
    else { impl->currentScope = zc::none; }
  } else {
    impl->currentScope = zc::none;
  }
}

zc::Maybe<const Scope&> ScopeManager::getGlobalScope() const {
  for (const auto& ownedScope : impl->ownedScopes) {
    if (ownedScope->isRoot()) { return *ownedScope; }
  }
  return zc::none;
}

zc::Maybe<const Scope&> ScopeManager::getPackageScope(zc::StringPtr name) const {
  ZC_IF_SOME(pkgScope, impl->packageScopes.find(name)) { return pkgScope; }
  return zc::none;
}

zc::Maybe<const Scope&> ScopeManager::getClassScope(zc::StringPtr name) const {
  ZC_IF_SOME(clsScope, impl->classScopes.find(name)) { return clsScope; }
  return zc::none;
}

zc::Maybe<const Scope&> ScopeManager::getFunctionScope(zc::StringPtr name) const {
  ZC_IF_SOME(funcScope, impl->functionScopes.find(name)) { return funcScope; }
  return zc::none;
}

// Mutable scope lookup methods
zc::Maybe<Scope&> ScopeManager::getGlobalScopeMutable() {
  for (auto& ownedScope : impl->ownedScopes) {
    if (ownedScope->isRoot()) { return *ownedScope; }
  }
  return zc::none;
}

zc::Maybe<Scope&> ScopeManager::getPackageScopeMutable(zc::StringPtr name) {
  ZC_IF_SOME(pkgScope, impl->packageScopes.find(name)) { return pkgScope; }
  return zc::none;
}

zc::Maybe<Scope&> ScopeManager::getClassScopeMutable(zc::StringPtr name) {
  ZC_IF_SOME(clsScope, impl->classScopes.find(name)) { return clsScope; }
  return zc::none;
}

zc::Maybe<Scope&> ScopeManager::getFunctionScopeMutable(zc::StringPtr name) {
  ZC_IF_SOME(funcScope, impl->functionScopes.find(name)) { return funcScope; }
  return zc::none;
}

// Scope enumeration
zc::ArrayPtr<const zc::Own<Scope>> ScopeManager::getAllScopes() const {
  return impl->ownedScopes.asPtr();
}

zc::Array<zc::Maybe<const Scope&>> ScopeManager::getScopesOfKind(Scope::Kind kind) const {
  // First count how many scopes match the kind to avoid ArrayBuilder overflow
  size_t count = 0;
  for (const auto& ownedScope : impl->ownedScopes) {
    if (ownedScope->getKind() == kind) { count++; }
  }

  // Create ArrayBuilder with the exact capacity needed
  auto result = zc::heapArrayBuilder<zc::Maybe<const Scope&>>(count);
  for (const auto& ownedScope : impl->ownedScopes) {
    if (ownedScope->getKind() == kind) { result.add(*ownedScope); }
  }
  return result.finish();
}

// Scope validation
bool ScopeManager::isValidScope(const Scope& scope) const {
  for (const auto& ownedScope : impl->ownedScopes) {
    if (ownedScope.get() == &scope) { return scope.isValid(); }
  }
  return false;
}

void ScopeManager::validateAllScopes() const {
  for (const auto& ownedScope : impl->ownedScopes) { ownedScope->validate(); }
}

void ScopeManager::clear() {
  impl->ownedScopes.clear();
  impl->scopeStack.clear();
  impl->currentScope = zc::none;
  impl->packageScopes.clear();
  impl->classScopes.clear();
  impl->functionScopes.clear();
}

// ScopeGuard implementation using pimpl pattern
struct ScopeGuard::Impl {
  Impl(ScopeManager& manager, const Scope& scope) noexcept : manager(manager), active(true) {
    // Save the current scope before switching
    ZC_IF_SOME(currScope, manager.getCurrentScope()) { originalScope = currScope; }
    // Switch to the new scope
    manager.setCurrentScope(scope);
  }

  ScopeManager& manager;
  zc::Maybe<const Scope&> originalScope;
  bool active;
};

ScopeGuard::ScopeGuard(ScopeManager& manager, const Scope& scope) noexcept
    : impl(zc::heap<Impl>(manager, scope)) {
  impl->manager.pushScope(scope);
}

ScopeGuard::~ScopeGuard() noexcept(false) {
  if (impl->active) {
    impl->manager.popScope();
    // Restore the original scope if there was one
    ZC_IF_SOME(original, impl->originalScope) { impl->manager.setCurrentScope(original); }
  }
}

zc::Maybe<const Scope&> ScopeGuard::getScope() const { return impl->originalScope; }

bool ScopeGuard::isActive() const { return impl->active; }

void ScopeGuard::release() { impl->active = false; }

// Factory functions
zc::Maybe<zc::Own<ScopeGuard>> ScopeGuard::create(ScopeManager& manager, Scope& scope) {
  return zc::heap<ScopeGuard>(manager, scope);
}

zc::Maybe<zc::Own<ScopeGuard>> ScopeGuard::createGlobal(ScopeManager& manager) {
  ZC_IF_SOME(globalScope, manager.getGlobalScope()) {
    return zc::heap<ScopeGuard>(manager, globalScope);
  }
  return zc::none;
}

zc::Maybe<zc::Own<ScopeGuard>> ScopeGuard::createPackage(ScopeManager& manager,
                                                         zc::StringPtr name) {
  ZC_IF_SOME(packageScope, manager.getPackageScope(name)) {
    return zc::heap<ScopeGuard>(manager, packageScope);
  }
  return zc::none;
}

zc::Maybe<zc::Own<ScopeGuard>> ScopeGuard::createClass(ScopeManager& manager, zc::StringPtr name) {
  ZC_IF_SOME(classScope, manager.getClassScope(name)) {
    return zc::heap<ScopeGuard>(manager, classScope);
  }
  return zc::none;
}

zc::Maybe<zc::Own<ScopeGuard>> ScopeGuard::createFunction(ScopeManager& manager,
                                                          zc::StringPtr name) {
  ZC_IF_SOME(functionScope, manager.getFunctionScope(name)) {
    return zc::heap<ScopeGuard>(manager, functionScope);
  }
  return zc::none;
}

}  // namespace symbol
}  // namespace compiler
}  // namespace zomlang
