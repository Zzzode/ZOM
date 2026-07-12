// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/internal/binding-skeleton.h"

#include "zc/core/debug.h"

namespace zomlang::compiler::binder {
namespace {

enum class SkeletonEligibility : uint8_t { Value, Type, Deferred };

SkeletonEligibility eligibility(identity::DefinitionKind kind) {
  using identity::DefinitionKind;
  switch (kind) {
    case DefinitionKind::Function:
    case DefinitionKind::Method:
    case DefinitionKind::Constructor:
    case DefinitionKind::Destructor:
    case DefinitionKind::Field:
    case DefinitionKind::EnumVariant:
    case DefinitionKind::Constant:
    case DefinitionKind::Static:
      return SkeletonEligibility::Value;
    case DefinitionKind::Class:
    case DefinitionKind::Struct:
    case DefinitionKind::Interface:
    case DefinitionKind::Enum:
    case DefinitionKind::Error:
    case DefinitionKind::TypeAlias:
    case DefinitionKind::AssociatedType:
      return SkeletonEligibility::Type;
    case DefinitionKind::ModuleAlias:
    case DefinitionKind::Parameter:
    case DefinitionKind::TypeParameter:
    case DefinitionKind::Local:
    case DefinitionKind::PatternBinding:
    case DefinitionKind::Closure:
    case DefinitionKind::ImportAlias:
    case DefinitionKind::ReexportAlias:
      return SkeletonEligibility::Deferred;
  }
  ZC_UNREACHABLE;
}

bool ownsScope(identity::DefinitionKind kind) {
  using identity::DefinitionKind;
  switch (kind) {
    case DefinitionKind::Function:
    case DefinitionKind::Method:
    case DefinitionKind::Constructor:
    case DefinitionKind::Destructor:
    case DefinitionKind::Class:
    case DefinitionKind::Struct:
    case DefinitionKind::Interface:
    case DefinitionKind::Enum:
    case DefinitionKind::Error:
      return true;
    case DefinitionKind::ModuleAlias:
    case DefinitionKind::TypeAlias:
    case DefinitionKind::AssociatedType:
    case DefinitionKind::Field:
    case DefinitionKind::EnumVariant:
    case DefinitionKind::Parameter:
    case DefinitionKind::TypeParameter:
    case DefinitionKind::Constant:
    case DefinitionKind::Static:
    case DefinitionKind::Local:
    case DefinitionKind::PatternBinding:
    case DefinitionKind::Closure:
    case DefinitionKind::ImportAlias:
    case DefinitionKind::ReexportAlias:
      return false;
  }
  ZC_UNREACHABLE;
}

BinderInvariantFact failure(const VerifiedBindingInput& input, BinderInvariantKind kind,
                            ast::NodeId node) {
  return BinderInvariantFact{kind, input.module(), zc::none, BinderEmitterSite::ModuleSkeleton,
                             node.value};
}

int compareBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) {
  const size_t count = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < count; ++index) {
    if (left[index] < right[index]) { return -1; }
    if (left[index] > right[index]) { return 1; }
  }
  if (left.size() < right.size()) { return -1; }
  if (left.size() > right.size()) { return 1; }
  return 0;
}

zc::Maybe<ScopeId> scopeForNode(const ScopeArenaCandidate& arena, ast::NodeId node) {
  for (const auto& fact : arena.nodeScopes) {
    if (fact.node == node) { return fact.scope; }
  }
  return zc::none;
}

bool sameSpan(const identity::SourceSpan& left, const identity::SourceSpan& right) {
  return left.byteStart() == right.byteStart() && left.byteEnd() == right.byteEnd();
}

bool contains(const identity::SourceSpan& parent, const identity::SourceSpan& child) {
  return parent.byteStart() <= child.byteStart() && child.byteEnd() <= parent.byteEnd();
}

bool bindingLess(const ScopeBindingEntry& left, const ScopeBindingEntry& right) {
  if (left.name.nameSpace() != right.name.nameSpace()) {
    return static_cast<uint8_t>(left.name.nameSpace()) <
           static_cast<uint8_t>(right.name.nameSpace());
  }
  return left.name.name() < right.name.name();
}

bool sameBindingName(const ScopeBindingEntry& left, const ScopeBindingEntry& right) {
  return left.name.nameSpace() == right.name.nameSpace() && left.name.name() == right.name.name();
}

void sortBindings(zc::Vector<ScopeBindingEntry>& bindings) {
  for (size_t index = 1; index < bindings.size(); ++index) {
    auto current = zc::mv(bindings[index]);
    size_t insertion = index;
    while (insertion > 0 && bindingLess(current, bindings[insertion - 1])) {
      bindings[insertion] = zc::mv(bindings[insertion - 1]);
      --insertion;
    }
    bindings[insertion] = zc::mv(current);
  }
}

bool seedLess(const ModuleSkeletonSurfaceSeed& left, const ModuleSkeletonSurfaceSeed& right) {
  if (left.name.nameSpace() != right.name.nameSpace()) {
    return static_cast<uint8_t>(left.name.nameSpace()) <
           static_cast<uint8_t>(right.name.nameSpace());
  }
  return left.name.name() < right.name.name();
}

void sortSurfaceSeeds(zc::Vector<ModuleSkeletonSurfaceSeed>& seeds) {
  for (size_t index = 1; index < seeds.size(); ++index) {
    auto current = zc::mv(seeds[index]);
    size_t insertion = index;
    while (insertion > 0 && seedLess(current, seeds[insertion - 1])) {
      seeds[insertion] = zc::mv(seeds[insertion - 1]);
      --insertion;
    }
    seeds[insertion] = zc::mv(current);
  }
}

}  // namespace

ModuleSkeletonSurfaceSeed::ModuleSkeletonSurfaceSeed(BindingNameKey&& name,
                                                     identity::DefId identity,
                                                     identity::SourceSpan&& source) noexcept
    : name(zc::mv(name)), identity(identity), source(zc::mv(source)) {}

DefinitionSkeletonBuildResult BindingSkeletonBuilder::build(const VerifiedBindingInput& input,
                                                            ScopeArenaCandidate& arena) {
  if (arena.scopes.empty() || arena.scopes[0].kind != ScopeKind::Module) {
    return failure(input, BinderInvariantKind::MalformedScopeGraph, input.tree().root());
  }
  const auto inventory = input.definitions().definitions();
  zc::Vector<size_t> order;
  for (size_t index = 0; index < inventory.size(); ++index) { order.add(index); }
  for (size_t index = 1; index < order.size(); ++index) {
    const size_t current = order[index];
    const auto currentKey = inventory[current].key.encode();
    size_t insertion = index;
    while (insertion > 0) {
      const auto previousKey = inventory[order[insertion - 1]].key.encode();
      if (compareBytes(currentKey.asPtr(), previousKey.asPtr()) >= 0) { break; }
      order[insertion] = order[insertion - 1];
      --insertion;
    }
    order[insertion] = current;
  }

  DefinitionSkeletonCandidate result;
  for (const size_t index : order) {
    const auto& definition = inventory[index];
    const auto classification = eligibility(definition.kind);
    if (classification == SkeletonEligibility::Deferred) {
      return failure(input, BinderInvariantKind::MissingRequiredResolution, definition.node);
    }
    if (definition.bindingName == zc::none || !input.tree().contains(definition.node)) {
      return failure(input, BinderInvariantKind::InvalidBindingFact, definition.node);
    }
    auto syntaxSpan = input.parsedModule().spanFor(input.tree().node(definition.node).range);
    auto nodeScope = scopeForNode(arena, definition.node);
    if (syntaxSpan == zc::none || nodeScope == zc::none) {
      return failure(input, BinderInvariantKind::InvalidBindingFact, definition.node);
    }

    zc::Maybe<ScopeId> declaringScope;
    ZC_IF_SOME(scope, nodeScope) {
      if (scope.index() >= arena.scopes.size() || arena.scopes[scope.index()].id != scope) {
        return failure(input, BinderInvariantKind::MalformedScopeGraph, definition.node);
      }
      if (ownsScope(definition.kind)) {
        const auto& owned = arena.scopes[scope.index()];
        const auto& owner = owned.owner.value();
        if (!owner.is<DefinitionScopeOwner>() ||
            owner.get<DefinitionScopeOwner>().definition != definition.definition ||
            owned.parent == zc::none) {
          return failure(input, BinderInvariantKind::MalformedScopeGraph, definition.node);
        }
        ZC_IF_SOME(parent, owned.parent) { declaringScope = parent; }
      } else {
        declaringScope = scope;
      }
    }
    if (declaringScope == zc::none) {
      return failure(input, BinderInvariantKind::MalformedScopeGraph, definition.node);
    }
    ZC_IF_SOME(scope, declaringScope) {
      if (scope.index() >= arena.scopes.size() || arena.scopes[scope.index()].id != scope) {
        return failure(input, BinderInvariantKind::MalformedScopeGraph, definition.node);
      }
      auto& record = arena.scopes[scope.index()];
      if (record.kind != ScopeKind::Module && record.kind != ScopeKind::TypeBody &&
          record.kind != ScopeKind::ImplBody) {
        return failure(input, BinderInvariantKind::MissingRequiredResolution, definition.node);
      }
      ZC_IF_SOME(span, syntaxSpan) {
        if (!sameSpan(span, definition.source) || !contains(record.source, definition.source)) {
          return failure(input, BinderInvariantKind::InvalidBindingFact, definition.node);
        }
      }
      const Namespace nameSpace =
          classification == SkeletonEligibility::Value ? Namespace::Value : Namespace::Type;
      const auto& name = ZC_ASSERT_NONNULL(definition.bindingName);
      zc::Maybe<identity::SourceSpan> noAlias;
      record.bindings.add(
          ScopeBindingEntry(BindingNameKey(nameSpace, name.clone()),
                            NameBinding(BindingTarget::definition(definition.definition),
                                        BindingTarget::definition(definition.definition), nameSpace,
                                        BindingOrigin::LocalDeclaration, definition.source.clone(),
                                        zc::mv(noAlias))));
      result.definitions.add(DefinitionFact(
          definition.definition, definition.site.clone(), definition.kind, definition.name.clone(),
          nameSpace, scope, definition.source.clone(), DefinitionActivation::ModuleSkeleton));
      if (record.kind == ScopeKind::Module) {
        result.moduleSurfaceSeeds.add(
            ModuleSkeletonSurfaceSeed(BindingNameKey(nameSpace, name.clone()),
                                      definition.definition, definition.source.clone()));
      }
    }
  }

  for (auto& scope : arena.scopes) {
    sortBindings(scope.bindings);
    for (size_t index = 1; index < scope.bindings.size(); ++index) {
      if (sameBindingName(scope.bindings[index - 1], scope.bindings[index])) {
        return failure(input, BinderInvariantKind::InvalidBindingFact, input.tree().root());
      }
    }
  }
  sortSurfaceSeeds(result.moduleSurfaceSeeds);
  return result;
}

}  // namespace zomlang::compiler::binder
