// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/frozen-definition-inventory.h"

#include "zc/core/vector.h"
#include "zomlang/compiler/binder/definition-inventory.h"

namespace zomlang::compiler::binder {
namespace {

FrozenInventoryInvariantFact failure(FrozenInventoryInvariantKind kind) { return {kind, 1}; }

bool permitsAbsentLexicalBinding(identity::DefinitionKind kind) {
  return kind == identity::DefinitionKind::Constructor ||
         kind == identity::DefinitionKind::Destructor;
}

bool sameParentPath(zc::ArrayPtr<const StructuralIdentityParent> left,
                    zc::ArrayPtr<const StructuralIdentityParent> right) {
  if (left.size() != right.size()) { return false; }
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index].kind != right[index].kind || left[index].node != right[index].node) {
      return false;
    }
  }
  return true;
}

uint32_t siblingOrdinal(const DefinitionInventory& inventory,
                        const DefinitionInventoryEntry& entry) {
  uint32_t ordinal = 0;
  for (const auto& candidate : inventory.definitions()) {
    if (candidate.node.value < entry.node.value && candidate.moduleNode == entry.moduleNode &&
        sameParentPath(candidate.parentPath.asPtr(), entry.parentPath.asPtr())) {
      ++ordinal;
    }
  }
  for (const auto& candidate : inventory.impls()) {
    if (candidate.node.value < entry.node.value && candidate.moduleNode == entry.moduleNode &&
        sameParentPath(candidate.parentPath.asPtr(), entry.parentPath.asPtr())) {
      ++ordinal;
    }
  }
  return ordinal;
}

uint32_t siblingOrdinal(const DefinitionInventory& inventory, const ImplInventoryEntry& entry) {
  uint32_t ordinal = 0;
  for (const auto& candidate : inventory.definitions()) {
    if (candidate.node.value < entry.node.value && candidate.moduleNode == entry.moduleNode &&
        sameParentPath(candidate.parentPath.asPtr(), entry.parentPath.asPtr())) {
      ++ordinal;
    }
  }
  for (const auto& candidate : inventory.impls()) {
    if (candidate.node.value < entry.node.value && candidate.moduleNode == entry.moduleNode &&
        sameParentPath(candidate.parentPath.asPtr(), entry.parentPath.asPtr())) {
      ++ordinal;
    }
  }
  return ordinal;
}

zc::Maybe<const DefinitionInventoryEntry&> definitionEntry(const DefinitionInventory& inventory,
                                                           ast::NodeId node) {
  for (const auto& entry : inventory.definitions()) {
    if (entry.node == node) { return entry; }
  }
  return zc::none;
}

zc::Maybe<const ImplInventoryEntry&> implEntry(const DefinitionInventory& inventory,
                                               ast::NodeId node) {
  for (const auto& entry : inventory.impls()) {
    if (entry.node == node) { return entry; }
  }
  return zc::none;
}

zc::Maybe<identity::DefinitionNameKey> definitionName(const DefinitionInventoryEntry& entry,
                                                      const VerifiedParsedModule& parsedModule) {
  if (entry.nameKind == InventoryDefinitionNameKind::Declared) {
    ZC_IF_SOME(value, identity::DeclaredDefinitionName::fromSource(
                          parsedModule.tree().ident(entry.declaredName))) {
      return identity::DefinitionNameKey::declared(zc::mv(value));
    }
    return zc::none;
  }
  ZC_IF_SOME(role, entry.anonymousRole) { return identity::DefinitionNameKey::anonymous(role); }
  return zc::none;
}

zc::Maybe<identity::DefinitionPathSegment> definitionSegment(
    const DefinitionInventory& inventory, const DefinitionInventoryEntry& entry,
    const VerifiedParsedModule& parsedModule) {
  auto name = definitionName(entry, parsedModule);
  auto span = parsedModule.spanFor(entry.source);
  if (name == zc::none || span == zc::none) { return zc::none; }
  ZC_IF_SOME(nameValue, name) {
    ZC_IF_SOME(spanValue, span) {
      return identity::DefinitionPathSegment::from(entry.kind, zc::mv(nameValue), zc::mv(spanValue),
                                                   siblingOrdinal(inventory, entry));
    }
  }
  return zc::none;
}

zc::Maybe<identity::DefinitionKey> expectedKey(const DefinitionInventory& inventory,
                                               const DefinitionInventoryEntry& entry,
                                               const VerifiedParsedModule& parsedModule,
                                               const identity::ModuleKey& module) {
  zc::Vector<identity::DefinitionPathComponent> path;
  for (const auto& parent : entry.parentPath) {
    if (parent.kind == StructuralIdentityParentKind::Definition) {
      ZC_IF_SOME(parentEntry, definitionEntry(inventory, parent.node)) {
        ZC_IF_SOME(segment, definitionSegment(inventory, parentEntry, parsedModule)) {
          path.add(identity::DefinitionPathComponent::definition(zc::mv(segment)));
          continue;
        }
      }
      return zc::none;
    }
    ZC_IF_SOME(parentImpl, implEntry(inventory, parent.node)) {
      ZC_IF_SOME(span, parsedModule.spanFor(parentImpl.source)) {
        path.add(identity::DefinitionPathComponent::impl(
            identity::ImplPathSegment::from(zc::mv(span), siblingOrdinal(inventory, parentImpl))));
        continue;
      }
    }
    return zc::none;
  }
  ZC_IF_SOME(segment, definitionSegment(inventory, entry, parsedModule)) {
    path.add(identity::DefinitionPathComponent::definition(zc::mv(segment)));
    return identity::DefinitionKey::from(module.clone(), zc::mv(path));
  }
  return zc::none;
}

zc::Maybe<identity::ImplKey> expectedImplKey(const DefinitionInventory& inventory,
                                             const ImplInventoryEntry& entry,
                                             const VerifiedParsedModule& parsedModule,
                                             const identity::ModuleKey& module) {
  zc::Vector<identity::DefinitionPathSegment> parentPath;
  for (const auto& parent : entry.parentPath) {
    if (parent.kind != StructuralIdentityParentKind::Definition) { return zc::none; }
    ZC_IF_SOME(parentEntry, definitionEntry(inventory, parent.node)) {
      ZC_IF_SOME(segment, definitionSegment(inventory, parentEntry, parsedModule)) {
        parentPath.add(zc::mv(segment));
        continue;
      }
    }
    return zc::none;
  }
  ZC_IF_SOME(span, parsedModule.spanFor(entry.source)) {
    return identity::ImplKey::from(module.clone(), zc::mv(parentPath), zc::mv(span),
                                   siblingOrdinal(inventory, entry));
  }
  return zc::none;
}

bool allRegistriesFrozen(const identity::SemanticIdentityRegistrySet& registries) {
  return registries.packages().isFrozen() && registries.crates().isFrozen() &&
         registries.sourceFiles().isFrozen() && registries.modules().isFrozen() &&
         registries.definitions().isFrozen() && registries.impls().isFrozen();
}

}  // namespace

FrozenDefinitionEntry::FrozenDefinitionEntry(ast::NodeId node, DefinitionSite&& site,
                                             identity::DefId definition,
                                             identity::DefinitionKey&& key,
                                             identity::DefinitionKind kind,
                                             identity::DefinitionNameKey&& name,
                                             zc::Maybe<identity::SemanticIdentifier>&& bindingName,
                                             identity::SourceSpan&& source) noexcept
    : node(node),
      site(zc::mv(site)),
      definition(definition),
      key(zc::mv(key)),
      kind(kind),
      name(zc::mv(name)),
      bindingName(zc::mv(bindingName)),
      source(zc::mv(source)) {}

FrozenImplEntry::FrozenImplEntry(ast::NodeId node, identity::ImplId implementation,
                                 identity::ImplKey&& key, identity::SourceSpan&& source) noexcept
    : node(node), implementation(implementation), key(zc::mv(key)), source(zc::mv(source)) {}

struct FrozenDefinitionInventoryView::Impl final {
  Impl(identity::SemanticContextBrand context,
       identity::DefinitionRegistry::FrozenKeyIndex&& definitionKeys,
       identity::ImplRegistry::FrozenKeyIndex&& implKeys, identity::ModuleId module,
       ast::NodeId moduleNode, zc::Vector<FrozenDefinitionEntry>&& definitions,
       zc::Vector<FrozenImplEntry>&& impls)
      : context(context),
        definitionKeys(zc::mv(definitionKeys)),
        implKeys(zc::mv(implKeys)),
        module(module),
        moduleNode(moduleNode),
        definitions(zc::mv(definitions)),
        impls(zc::mv(impls)) {}

  identity::SemanticContextBrand context;
  identity::DefinitionRegistry::FrozenKeyIndex definitionKeys;
  identity::ImplRegistry::FrozenKeyIndex implKeys;
  identity::ModuleId module;
  ast::NodeId moduleNode;
  zc::Vector<FrozenDefinitionEntry> definitions;
  zc::Vector<FrozenImplEntry> impls;
};

FrozenDefinitionInventoryView::FrozenDefinitionInventoryView(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}
FrozenDefinitionInventoryView::~FrozenDefinitionInventoryView() noexcept(false) = default;
FrozenDefinitionInventoryView::FrozenDefinitionInventoryView(
    FrozenDefinitionInventoryView&&) noexcept = default;
FrozenDefinitionInventoryView& FrozenDefinitionInventoryView::operator=(
    FrozenDefinitionInventoryView&&) noexcept = default;
identity::SemanticContextBrand FrozenDefinitionInventoryView::semanticContext() const noexcept {
  return impl->context;
}
identity::ModuleId FrozenDefinitionInventoryView::module() const noexcept { return impl->module; }
ast::NodeId FrozenDefinitionInventoryView::moduleNode() const noexcept { return impl->moduleNode; }
zc::ArrayPtr<const FrozenDefinitionEntry> FrozenDefinitionInventoryView::definitions() const {
  return impl->definitions.asPtr();
}
zc::ArrayPtr<const FrozenImplEntry> FrozenDefinitionInventoryView::impls() const {
  return impl->impls.asPtr();
}
zc::Maybe<const identity::DefinitionKey&> FrozenDefinitionInventoryView::definitionKey(
    identity::DefId definition) const {
  return impl->definitionKeys.lookup(definition);
}
zc::Maybe<const identity::ImplKey&> FrozenDefinitionInventoryView::implKey(
    identity::ImplId implementation) const {
  return impl->implKeys.lookup(implementation);
}
zc::Maybe<identity::DefId> FrozenDefinitionInventoryView::definitionAt(ast::NodeId node) const {
  for (const auto& entry : impl->definitions) {
    if (entry.node == node) { return entry.definition; }
  }
  return zc::none;
}
zc::Maybe<identity::ImplId> FrozenDefinitionInventoryView::implAt(ast::NodeId node) const {
  for (const auto& entry : impl->impls) {
    if (entry.node == node) { return entry.implementation; }
  }
  return zc::none;
}

FrozenDefinitionInventoryResult FrozenDefinitionInventoryVerifier::verifySingleModule(
    identity::SemanticContextBrand context, identity::ModuleId module,
    const VerifiedParsedModule& parsedModule,
    const identity::SemanticIdentityRegistrySet& registries,
    const DefinitionIdentityMap& definitions) {
  if (!context.isValid() || !module.belongsTo(context) ||
      !parsedModule.sourceFile().belongsTo(context) || !allRegistriesFrozen(registries)) {
    return failure(FrozenInventoryInvariantKind::InputMismatch);
  }
  auto moduleKey = registries.modules().lookup(module);
  auto sourceKey = registries.sourceFiles().lookup(parsedModule.sourceFile());
  if (moduleKey == zc::none || sourceKey == zc::none) {
    return failure(FrozenInventoryInvariantKind::InputMismatch);
  }
  const auto inventory = DefinitionInventory::collect(parsedModule.tree());
  if (inventory.modules().size() != 1 || definitions.size() != inventory.definitions().size() ||
      registries.definitions().size() != inventory.definitions().size() ||
      registries.impls().size() != inventory.impls().size()) {
    return failure(FrozenInventoryInvariantKind::IncompleteInventory);
  }
  ZC_IF_SOME(moduleValue, moduleKey) {
    ZC_IF_SOME(sourceValue, sourceKey) {
      if (!moduleValue.source().sameAs(sourceValue)) {
        return failure(FrozenInventoryInvariantKind::InputMismatch);
      }
      zc::Vector<FrozenDefinitionEntry> frozen;
      for (const auto& entry : inventory.definitions()) {
        auto definition = definitions.find(entry.node);
        auto span = parsedModule.spanFor(entry.source);
        auto key = expectedKey(inventory, entry, parsedModule, moduleValue);
        if (definition == zc::none || span == zc::none || key == zc::none) {
          return failure(FrozenInventoryInvariantKind::InvalidDefinitionSite);
        }
        ZC_IF_SOME(definitionValue, definition) {
          if (!definitionValue.belongsTo(context)) {
            return failure(FrozenInventoryInvariantKind::InvalidDefinitionIdentity);
          }
          ZC_IF_SOME(keyValue, key) {
            auto expected = registries.definitions().find(keyValue);
            if (expected == zc::none) {
              return failure(FrozenInventoryInvariantKind::InvalidDefinitionIdentity);
            }
            ZC_IF_SOME(expectedValue, expected) {
              if (expectedValue != definitionValue) {
                return failure(FrozenInventoryInvariantKind::InvalidDefinitionIdentity);
              }
            }
          }
          auto name = definitionName(entry, parsedModule);
          if (name == zc::none) {
            return failure(FrozenInventoryInvariantKind::InvalidDefinitionIdentity);
          }
          zc::Maybe<identity::SemanticIdentifier> bindingName;
          if (entry.nameKind == InventoryDefinitionNameKind::Declared) {
            bindingName = identity::SemanticIdentifier::fromSource(
                parsedModule.tree().ident(entry.declaredName));
            if (bindingName == zc::none && !permitsAbsentLexicalBinding(entry.kind)) {
              return failure(FrozenInventoryInvariantKind::InvalidDefinitionIdentity);
            }
          }
          ZC_IF_SOME(keyValue, key) {
            ZC_IF_SOME(nameValue, name) {
              ZC_IF_SOME(spanValue, span) {
                frozen.add(FrozenDefinitionEntry(entry.node, entry.site.clone(), definitionValue,
                                                 keyValue.clone(), entry.kind, zc::mv(nameValue),
                                                 zc::mv(bindingName), zc::mv(spanValue)));
              }
            }
          }
        }
      }
      zc::Vector<FrozenImplEntry> frozenImpls;
      for (const auto& entry : inventory.impls()) {
        auto span = parsedModule.spanFor(entry.source);
        auto key = expectedImplKey(inventory, entry, parsedModule, moduleValue);
        if (span == zc::none || key == zc::none) {
          return failure(FrozenInventoryInvariantKind::InvalidDefinitionSite);
        }
        ZC_IF_SOME(keyValue, key) {
          auto implementation = registries.impls().find(keyValue);
          if (implementation == zc::none) {
            return failure(FrozenInventoryInvariantKind::InvalidDefinitionIdentity);
          }
          ZC_IF_SOME(implementationValue, implementation) {
            if (!implementationValue.belongsTo(context)) {
              return failure(FrozenInventoryInvariantKind::InvalidDefinitionIdentity);
            }
            ZC_IF_SOME(spanValue, span) {
              frozenImpls.add(FrozenImplEntry(entry.node, implementationValue, keyValue.clone(),
                                              zc::mv(spanValue)));
            }
          }
        }
      }
      auto definitionKeys = registries.definitions().snapshotKeys();
      auto implKeys = registries.impls().snapshotKeys();
      if (definitionKeys == zc::none || implKeys == zc::none) {
        return failure(FrozenInventoryInvariantKind::InputMismatch);
      }
      ZC_IF_SOME(definitionKeyIndex, definitionKeys) {
        ZC_IF_SOME(implKeyIndex, implKeys) {
          return FrozenDefinitionInventoryView(zc::heap<FrozenDefinitionInventoryView::Impl>(
              context, zc::mv(definitionKeyIndex), zc::mv(implKeyIndex), module,
              inventory.modules()[0].node, zc::mv(frozen), zc::mv(frozenImpls)));
        }
      }
      ZC_UNREACHABLE;
    }
  }
  return failure(FrozenInventoryInvariantKind::InputMismatch);
}

}  // namespace zomlang::compiler::binder
