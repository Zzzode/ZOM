// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/stable/implementation/header-producer.h"

#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/binder/definition-inventory.h"
#include "zomlang/compiler/binder/stable-binding-codec.h"
#include "zomlang/compiler/binder/stable/candidate/verifier.h"

namespace zomlang::compiler::binder {
namespace {

template <typename Left, typename Right>
bool sameEncoding(const Left& left, const Right& right) {
  return left.encode().asPtr() == right.encode().asPtr();
}

bool sameModule(const identity::ModuleKey& left, const identity::ModuleKey& right) {
  return sameEncoding(left, right);
}

zc::Maybe<const ImplInventoryEntry&> implementationAt(const DefinitionInventory& inventory,
                                                      ast::NodeId node) {
  for (const auto& entry : inventory.impls()) {
    if (entry.node == node) { return entry; }
  }
  return zc::none;
}

bool containsOccurrence(const ImplementationHeaderInput& input) {
  size_t matches = 0;
  for (const auto& site : input.implementationSites.entries()) {
    if (site.node() == input.occurrenceSite.node() &&
        site.occurrence().sameAs(input.occurrenceSite.occurrence()) &&
        site.byteStart() == input.occurrenceSite.byteStart() &&
        site.byteEnd() == input.occurrenceSite.byteEnd()) {
      ++matches;
    }
  }
  return matches == 1;
}

bool completeProjections(const ImplementationHeaderInput& input, ast::NodeId moduleNode) {
  auto reconstructed =
      CandidateVerifier::reconstruct(input.parsed, input.queryKey.module(), moduleNode);
  if (!reconstructed.is<VerifiedStableIdentityCandidateInventory>()) { return false; }
  const auto& verified = reconstructed.get<VerifiedStableIdentityCandidateInventory>();
  if (verified.definitions.size() != input.definitionSites.entries().size() ||
      verified.implementations.size() != input.implementationSites.entries().size()) {
    return false;
  }
  for (const auto& definition : verified.definitions) {
    size_t matches = 0;
    for (const auto& site : input.definitionSites.entries()) {
      if (site.node() == definition.node && site.definition() == definition.authority.key() &&
          site.site().sameAs(definition.site) &&
          site.byteStart() == definition.source.byteStart() &&
          site.byteEnd() == definition.source.byteEnd()) {
        ++matches;
      }
    }
    if (matches != 1) { return false; }
  }
  for (const auto& implementation : verified.implementations) {
    size_t matches = 0;
    for (const auto& site : input.implementationSites.entries()) {
      if (site.node() == implementation.node &&
          site.occurrence().implementation() == implementation.authority.key() &&
          site.occurrence().site().sameAs(implementation.site) &&
          site.byteStart() == implementation.source.byteStart() &&
          site.byteEnd() == implementation.source.byteEnd()) {
        ++matches;
      }
    }
    if (matches != 1) { return false; }
    if (implementation.node == input.occurrenceSite.node() &&
        implementation.authority.record().encode().asPtr() !=
            input.entry.record().encode().asPtr()) {
      return false;
    }
  }
  return true;
}

zc::Maybe<const identity::DefinitionKey&> definitionKeyAt(const ImplementationHeaderInput& input,
                                                          ast::NodeId node) {
  for (const auto& site : input.definitionSites.entries()) {
    if (site.node() == node) { return site.definition(); }
  }
  return zc::none;
}

zc::Maybe<const identity::ImplKey&> implementationKeyAt(const ImplementationHeaderInput& input,
                                                        ast::NodeId node) {
  for (const auto& site : input.implementationSites.entries()) {
    if (site.node() == node) { return site.occurrence().implementation(); }
  }
  return zc::none;
}

bool matchesOwners(const ImplementationHeaderInput& input,
                   zc::ArrayPtr<const StructuralIdentityParent> parents) {
  const auto owners = input.entry.record().owners();
  if (parents.size() != owners.size()) { return false; }
  for (size_t index = 0; index < parents.size(); ++index) {
    if (parents[index].kind == StructuralIdentityParentKind::Definition) {
      auto actual = definitionKeyAt(input, parents[index].node);
      auto expected = owners[index].definitionKey();
      if (actual == zc::none || expected == zc::none ||
          ZC_ASSERT_NONNULL(actual) != ZC_ASSERT_NONNULL(expected)) {
        return false;
      }
    } else {
      auto actual = implementationKeyAt(input, parents[index].node);
      auto expected = owners[index].implKey();
      if (actual == zc::none || expected == zc::none ||
          ZC_ASSERT_NONNULL(actual) != ZC_ASSERT_NONNULL(expected)) {
        return false;
      }
    }
  }
  return true;
}

template <typename T>
void sortCanonical(zc::Vector<T>& values) {
  for (size_t index = 1; index < values.size(); ++index) {
    auto current = zc::mv(values[index]);
    const auto currentBytes = StableBindingCodec<T>::encode(current);
    size_t insertion = index;
    while (insertion != 0) {
      const auto previousBytes = StableBindingCodec<T>::encode(values[insertion - 1]);
      if (stable_binding_codec_detail::compareBytes(previousBytes.asPtr(), currentBytes.asPtr()) <
          0) {
        break;
      }
      values[insertion] = zc::mv(values[insertion - 1]);
      --insertion;
    }
    values[insertion] = zc::mv(current);
  }
}

zc::Maybe<CanonicalSequence<StableHeaderGenericParameter>> buildGenericParameters(
    const ImplementationHeaderInput& input, const DefinitionInventory& inventory) {
  zc::Vector<StableHeaderGenericParameter> values;
  uint32_t ordinal = 0;
  for (const auto& parameter : inventory.genericParameters()) {
    if (parameter.parentPath.empty() ||
        parameter.parentPath.back().kind != StructuralIdentityParentKind::Impl ||
        parameter.parentPath.back().node != input.occurrenceSite.node()) {
      continue;
    }
    if (parameter.nameKind != InventoryDefinitionNameKind::Declared) { return zc::none; }
    auto name = identity::DeclaredDefinitionName::fromSource(
        input.parsed.tree().ident(parameter.declaredName));
    if (name == zc::none) { return zc::none; }
    auto record = identity::GenericParameterIdentityRecord::type(
        identity::StableGenericParameterOwnerKey::implementation(input.entry.key().clone()),
        ordinal);
    auto key = identity::GenericParameterKey::compute(record);
    auto value = StableHeaderGenericParameter::from(
        zc::mv(key), zc::mv(record),
        StableHeaderSite::implementation(input.occurrenceSite.occurrence().clone()),
        zc::mv(ZC_ASSERT_NONNULL(name)), ordinal++);
    if (value == zc::none) { return zc::none; }
    values.add(zc::mv(ZC_ASSERT_NONNULL(value)));
  }
  sortCanonical(values);
  return StableBindingSequenceBuilder<StableHeaderGenericParameter>::from(zc::mv(values));
}

zc::Maybe<CanonicalSequence<ScopeRole>> buildScopeRoles(const ast::Tree& tree, ast::NodeId node) {
  const auto& syntax = tree.node(node);
  zc::Vector<ScopeRole> values;
  if (syntax.kind == ast::SyntaxKind::StandaloneImplDecl &&
      syntax.payload.words[ast::kStandaloneImplDeclTypeParamsIdWord] != 0) {
    values.add(ScopeRole::Generic);
  }
  values.add(ScopeRole::Implementation);
  return StableBindingSequenceBuilder<ScopeRole>::from(zc::mv(values));
}

zc::Maybe<ImplementationSourceForm> sourceForm(const ast::Tree& tree, ast::NodeId node) {
  const auto& syntax = tree.node(node);
  if (syntax.kind == ast::SyntaxKind::MarkerImpl) {
    return ImplementationSourceForm::BodylessMarker;
  }
  if (syntax.kind != ast::SyntaxKind::StandaloneImplDecl) { return zc::none; }
  const ast::NodeId members(syntax.payload.words[ast::kStandaloneImplDeclMembersIdWord]);
  if (!members || !tree.contains(members) ||
      tree.node(members).kind != ast::SyntaxKind::ClassMemberList) {
    return zc::none;
  }
  return ImplementationSourceForm::Ordinary;
}

}  // namespace

zc::Maybe<StableImplementationOccurrenceHeader> ImplementationHeaderProducer::produce(
    const ImplementationHeaderInput& input) {
  const auto& occurrence = input.queryKey.occurrence();
  const auto& record = input.entry.record();
  if (!sameModule(input.queryKey.module(), record.module()) ||
      occurrence.implementation() != input.entry.key() ||
      input.entry.key() != identity::ImplKey::compute(record) ||
      !occurrence.sameAs(input.occurrenceSite.occurrence()) || !containsOccurrence(input)) {
    return zc::none;
  }

  const auto inventory = DefinitionInventory::collect(input.parsed.tree());
  auto syntax = implementationAt(inventory, input.occurrenceSite.node());
  if (syntax == zc::none || !completeProjections(input, ZC_ASSERT_NONNULL(syntax).moduleNode) ||
      !matchesOwners(input, ZC_ASSERT_NONNULL(syntax).parentPath.asPtr())) {
    return zc::none;
  }
  auto genericParameters = buildGenericParameters(input, inventory);
  auto scopeRoles = buildScopeRoles(input.parsed.tree(), input.occurrenceSite.node());
  auto form = sourceForm(input.parsed.tree(), input.occurrenceSite.node());
  if (genericParameters == zc::none || scopeRoles == zc::none || form == zc::none) {
    return zc::none;
  }

  return StableImplementationOccurrenceHeader::from(
      input.queryKey.clone(),
      StableImplementationQueryKey::from(input.queryKey.module().clone(),
                                         input.entry.key().clone()),
      record.clone(), zc::mv(ZC_ASSERT_NONNULL(genericParameters)),
      zc::mv(ZC_ASSERT_NONNULL(scopeRoles)), ZC_ASSERT_NONNULL(form));
}

}  // namespace zomlang::compiler::binder
