// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/stable/definition/header-producer.h"

#include "zomlang/compiler/ast/generated/node-accessors.h"
#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/ast/generated/node-schema.h"
#include "zomlang/compiler/binder/metadata/definition-inventory.h"
#include "zomlang/compiler/binder/stable/stable-binding-codec.h"
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

int comparePath(zc::ArrayPtr<const uint32_t> left, zc::ArrayPtr<const uint32_t> right) {
  const size_t shared = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < shared; ++index) {
    if (left[index] < right[index]) { return -1; }
    if (left[index] > right[index]) { return 1; }
  }
  if (left.size() < right.size()) { return -1; }
  if (left.size() > right.size()) { return 1; }
  return 0;
}

bool sourceOrderLess(const RevisionLocalDefinitionSite& left,
                     const RevisionLocalDefinitionSite& right) {
  if (left.byteStart() != right.byteStart()) { return left.byteStart() < right.byteStart(); }
  if (left.byteEnd() != right.byteEnd()) { return left.byteEnd() < right.byteEnd(); }
  return comparePath(left.site().moduleSyntaxPath(), right.site().moduleSyntaxPath()) < 0;
}

bool selectAuthority(const DefinitionHeaderInput& input) {
  zc::Maybe<const RevisionLocalDefinitionSite&> selected;
  for (const auto& site : input.definitionSites.entries()) {
    if (site.definition() != input.queryKey.definition()) { continue; }
    if (selected == zc::none || sourceOrderLess(site, ZC_ASSERT_NONNULL(selected))) {
      selected = site;
    }
  }
  return selected != zc::none && ZC_ASSERT_NONNULL(selected).node() == input.authoritySite.node() &&
         ZC_ASSERT_NONNULL(selected).definition() == input.authoritySite.definition() &&
         ZC_ASSERT_NONNULL(selected).site().sameAs(input.authoritySite.site()) &&
         ZC_ASSERT_NONNULL(selected).byteStart() == input.authoritySite.byteStart() &&
         ZC_ASSERT_NONNULL(selected).byteEnd() == input.authoritySite.byteEnd();
}

zc::Maybe<const DefinitionInventoryEntry&> definitionAt(const DefinitionInventory& inventory,
                                                        ast::NodeId node) {
  for (const auto& entry : inventory.definitions()) {
    if (entry.node == node) { return entry; }
  }
  return zc::none;
}

bool completeProjections(const DefinitionHeaderInput& input, ast::NodeId moduleNode) {
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
    if (definition.node == input.authoritySite.node() &&
        definition.authority.record().encode().asPtr() != input.entry.record().encode().asPtr()) {
      return false;
    }
  }
  for (const auto& implementation : verified.implementations) {
    size_t matches = 0;
    for (const auto& site : input.implementationSites.entries()) {
      const auto& occurrence = site.occurrence();
      if (site.node() == implementation.node &&
          occurrence.implementation() == implementation.authority.key() &&
          occurrence.site().sameAs(implementation.site) &&
          site.byteStart() == implementation.source.byteStart() &&
          site.byteEnd() == implementation.source.byteEnd()) {
        ++matches;
      }
    }
    if (matches != 1) { return false; }
  }
  return true;
}

zc::Maybe<const identity::DefinitionKey&> definitionKeyAt(const DefinitionHeaderInput& input,
                                                          ast::NodeId node) {
  for (const auto& site : input.definitionSites.entries()) {
    if (site.node() == node) { return site.definition(); }
  }
  return zc::none;
}

zc::Maybe<const identity::ImplKey&> implementationKeyAt(const DefinitionHeaderInput& input,
                                                        ast::NodeId node) {
  for (const auto& site : input.implementationSites.entries()) {
    if (site.node() == node) { return site.occurrence().implementation(); }
  }
  return zc::none;
}

bool matchesOwners(const DefinitionHeaderInput& input,
                   zc::ArrayPtr<const StructuralIdentityParent> parents) {
  const auto owners = input.entry.record().owners();
  if (parents.size() != owners.size()) { return false; }
  for (size_t index = 0; index < parents.size(); ++index) {
    if (parents[index].kind == StructuralIdentityParentKind::Definition) {
      auto key = definitionKeyAt(input, parents[index].node);
      auto expected = owners[index].definitionKey();
      if (key == zc::none || expected == zc::none ||
          ZC_ASSERT_NONNULL(key) != ZC_ASSERT_NONNULL(expected)) {
        return false;
      }
    } else {
      auto key = implementationKeyAt(input, parents[index].node);
      auto expected = owners[index].implKey();
      if (key == zc::none || expected == zc::none ||
          ZC_ASSERT_NONNULL(key) != ZC_ASSERT_NONNULL(expected)) {
        return false;
      }
    }
  }
  return true;
}

ast::NodeId genericParameters(const ast::Tree& tree, ast::NodeId node) {
  if (!tree.contains(node)) { return {}; }
  const auto& syntax = tree.node(node);
  switch (syntax.kind) {
    case ast::SyntaxKind::EnumDeclaration:
      return ast::NodeId(syntax.payload.words[ast::kEnumDeclarationTypeParamsIdWord]);
    case ast::SyntaxKind::FunctionDecl:
      return ast::NodeId(syntax.payload.words[ast::kFunctionDeclTypeParamsIdWord]);
    case ast::SyntaxKind::ClassDecl:
      return ast::NodeId(syntax.payload.words[ast::kClassDeclTypeParamsIdWord]);
    case ast::SyntaxKind::StructDecl:
      return ast::NodeId(syntax.payload.words[ast::kStructDeclTypeParamsIdWord]);
    case ast::SyntaxKind::InterfaceDecl:
      return ast::NodeId(syntax.payload.words[ast::kInterfaceDeclTypeParamsIdWord]);
    case ast::SyntaxKind::AliasDecl:
      return ast::NodeId(syntax.payload.words[ast::kAliasDeclTypeParamsIdWord]);
    case ast::SyntaxKind::MethodDecl:
      return ast::NodeId(syntax.payload.words[ast::kMethodDeclTypeParamsIdWord]);
    case ast::SyntaxKind::AssociatedTypeDecl:
      return ast::NodeId(syntax.payload.words[ast::kAssociatedTypeDeclTypeParamsIdWord]);
    default:
      return {};
  }
}

zc::Maybe<DefinitionBodyDisposition> bodyDisposition(const ast::Tree& tree, ast::NodeId node) {
  if (!tree.contains(node)) { return zc::none; }
  const auto& syntax = tree.node(node);
  auto result = DefinitionBodyDisposition::NoExecutableBody;
  uint32_t bodyWord = UINT32_MAX;
  switch (syntax.kind) {
    case ast::SyntaxKind::FunctionDecl:
      bodyWord = ast::kFunctionDeclBodyWord;
      break;
    case ast::SyntaxKind::ConstructorDecl:
      bodyWord = ast::kConstructorDeclBodyWord;
      break;
    case ast::SyntaxKind::DestructorDecl:
      bodyWord = ast::kDestructorDeclBodyWord;
      break;
    case ast::SyntaxKind::MethodDecl:
      bodyWord = ast::kMethodDeclBodyWord;
      break;
    case ast::SyntaxKind::FieldDecl:
    case ast::SyntaxKind::ClassConstDecl: {
      const uint32_t initializerWord = syntax.kind == ast::SyntaxKind::FieldDecl
                                           ? ast::kFieldDeclInitWord
                                           : ast::kClassConstDeclInitWord;
      const ast::NodeId initializer(syntax.payload.words[initializerWord]);
      if (!initializer) { return result; }
      if (!tree.contains(initializer)) { return zc::none; }
      const auto kind = tree.node(initializer).kind;
      if (!ast::isLiteralExprKind(kind) && !ast::isExprKind(kind) &&
          kind != ast::SyntaxKind::UnsafeBlockExpr) {
        return zc::none;
      }
      return DefinitionBodyDisposition::ExecutableBody;
    }
    default:
      return result;
  }
  const ast::NodeId body(syntax.payload.words[bodyWord]);
  if (syntax.kind == ast::SyntaxKind::MethodDecl && !body) { return result; }
  if (!body || !tree.contains(body) || tree.node(body).kind != ast::SyntaxKind::BlockStmt) {
    return zc::none;
  }
  return DefinitionBodyDisposition::ExecutableBody;
}

bool interfaceOwner(const ast::Tree& tree, const DefinitionInventoryEntry& entry) {
  if (entry.parentPath.empty()) { return false; }
  const auto& parent = entry.parentPath.back();
  return parent.kind == StructuralIdentityParentKind::Definition && tree.contains(parent.node) &&
         tree.node(parent.node).kind == ast::SyntaxKind::InterfaceDecl;
}

zc::Maybe<MemberVisibility> visibility(const ast::Tree& tree,
                                       const DefinitionInventoryEntry& entry) {
  const auto& syntax = tree.node(entry.node);
  uint32_t encoded = UINT32_MAX;
  switch (syntax.kind) {
    case ast::SyntaxKind::MethodDecl:
      encoded = syntax.payload.words[ast::kMethodDeclVisibilityWord];
      break;
    case ast::SyntaxKind::FieldDecl:
      encoded = syntax.payload.words[ast::kFieldDeclVisibilityWord];
      break;
    case ast::SyntaxKind::ConstructorDecl:
      encoded = syntax.payload.words[ast::kConstructorDeclVisibilityWord];
      break;
    case ast::SyntaxKind::DestructorDecl:
      encoded = syntax.payload.words[ast::kDestructorDeclVisibilityWord];
      break;
    case ast::SyntaxKind::ClassConstDecl:
      encoded = syntax.payload.words[ast::kClassConstDeclVisibilityWord];
      break;
    default:
      return zc::Maybe<MemberVisibility>();
  }
  switch (encoded) {
    case 0:
      return interfaceOwner(tree, entry) ? MemberVisibility::Public : MemberVisibility::Private;
    case 1:
      return MemberVisibility::Public;
    case 2:
      return MemberVisibility::Private;
    case 3:
      return MemberVisibility::Protected;
    default:
      return zc::none;
  }
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
    const DefinitionHeaderInput& input, const DefinitionInventory& inventory, ast::NodeId owner) {
  zc::Vector<StableHeaderGenericParameter> values;
  uint32_t ordinal = 0;
  for (const auto& parameter : inventory.genericParameters()) {
    if (parameter.parentPath.empty() || parameter.parentPath.back().node != owner) { continue; }
    if (parameter.nameKind != InventoryDefinitionNameKind::Declared) { return zc::none; }
    auto name = identity::DeclaredDefinitionName::fromSource(
        input.parsed.tree().ident(parameter.declaredName));
    if (name == zc::none) { return zc::none; }
    auto record = identity::GenericParameterIdentityRecord::type(
        identity::StableGenericParameterOwnerKey::definition(input.queryKey.definition().clone()),
        ordinal);
    auto key = identity::GenericParameterKey::compute(record);
    auto value = StableHeaderGenericParameter::from(
        zc::mv(key), zc::mv(record),
        StableHeaderSite::definition(input.authoritySite.site().clone()),
        zc::mv(ZC_ASSERT_NONNULL(name)), ordinal++);
    if (value == zc::none) { return zc::none; }
    values.add(zc::mv(ZC_ASSERT_NONNULL(value)));
  }
  sortCanonical(values);
  return StableBindingSequenceBuilder<StableHeaderGenericParameter>::from(zc::mv(values));
}

zc::Maybe<CanonicalSequence<StableHeaderCallableParameter>> buildCallableParameters(
    const DefinitionHeaderInput& input, const DefinitionInventory& inventory,
    const DefinitionInventoryEntry& owner) {
  zc::Vector<StableHeaderCallableParameter> values;
  uint32_t ordinaryOrdinal = 0;
  bool receiverSeen = false;
  size_t localOrdinal = 0;
  for (const auto& parameter : inventory.callableParameters()) {
    if (parameter.parentPath.empty() || parameter.parentPath.back().node != owner.node) {
      continue;
    }
    const bool receiver = input.parsed.functionParameterNameSpan(
                              parameter.node, ast::SyntaxKind::ThisKeyword) != zc::none;
    if (receiver && (receiverSeen || localOrdinal != 0 ||
                     input.parsed.tree().node(owner.node).kind != ast::SyntaxKind::MethodDecl)) {
      return zc::none;
    }
    zc::Maybe<identity::DeclaredDefinitionName> name;
    if (receiver) {
      receiverSeen = true;
    } else {
      if (parameter.nameKind != InventoryDefinitionNameKind::Declared) { return zc::none; }
      name = identity::DeclaredDefinitionName::fromSource(
          input.parsed.tree().ident(parameter.declaredName));
      if (name == zc::none) { return zc::none; }
    }
    const auto position = receiver
                              ? identity::CallableParameterPosition::receiver()
                              : identity::CallableParameterPosition::ordinary(ordinaryOrdinal++);
    auto record = identity::CallableParameterIdentityRecord::from(
        input.queryKey.definition().clone(), position);
    auto key = identity::CallableParameterKey::compute(record);
    auto value = StableHeaderCallableParameter::from(
        zc::mv(key), zc::mv(record),
        StableHeaderSite::definition(input.authoritySite.site().clone()), zc::mv(name), position);
    if (value == zc::none) { return zc::none; }
    values.add(zc::mv(ZC_ASSERT_NONNULL(value)));
    ++localOrdinal;
  }
  sortCanonical(values);
  return StableBindingSequenceBuilder<StableHeaderCallableParameter>::from(zc::mv(values));
}

zc::Maybe<CanonicalSequence<ScopeRole>> buildScopeRoles(const ast::Tree& tree, ast::NodeId node) {
  zc::Vector<ScopeRole> values;
  values.add(ScopeRole::Declaration);
  if (genericParameters(tree, node)) { values.add(ScopeRole::Generic); }
  switch (tree.node(node).kind) {
    case ast::SyntaxKind::ExternDecl:
    case ast::SyntaxKind::FunctionDecl:
    case ast::SyntaxKind::MethodDecl:
    case ast::SyntaxKind::ConstructorDecl:
    case ast::SyntaxKind::DestructorDecl:
      values.add(ScopeRole::Parameters);
      break;
    default:
      break;
  }
  switch (tree.node(node).kind) {
    case ast::SyntaxKind::EnumDeclaration:
    case ast::SyntaxKind::ClassDecl:
    case ast::SyntaxKind::StructDecl:
    case ast::SyntaxKind::InterfaceDecl:
    case ast::SyntaxKind::ErrorDecl:
      values.add(ScopeRole::Members);
      break;
    default:
      break;
  }
  return StableBindingSequenceBuilder<ScopeRole>::from(zc::mv(values));
}

}  // namespace

zc::Maybe<StableDefinitionHeader> DefinitionHeaderProducer::produce(
    const DefinitionHeaderInput& input) {
  const auto& record = input.entry.record();
  if (!sameModule(input.queryKey.module(), record.module()) ||
      input.queryKey.definition() != input.entry.key() ||
      input.entry.key() != identity::DefinitionKey::compute(record) ||
      input.authoritySite.definition() != input.queryKey.definition() || !selectAuthority(input)) {
    return zc::none;
  }

  const auto inventory = DefinitionInventory::collect(input.parsed.tree());
  auto syntax = definitionAt(inventory, input.authoritySite.node());
  if (syntax == zc::none || ZC_ASSERT_NONNULL(syntax).kind != record.kind() ||
      !completeProjections(input, ZC_ASSERT_NONNULL(syntax).moduleNode) ||
      !matchesOwners(input, ZC_ASSERT_NONNULL(syntax).parentPath.asPtr()) ||
      ZC_ASSERT_NONNULL(syntax).nameKind != InventoryDefinitionNameKind::Declared) {
    return zc::none;
  }
  auto name = identity::DeclaredDefinitionName::fromSource(
      input.parsed.tree().ident(ZC_ASSERT_NONNULL(syntax).declaredName));
  auto disposition = bodyDisposition(input.parsed.tree(), input.authoritySite.node());
  auto genericValues = buildGenericParameters(input, inventory, input.authoritySite.node());
  auto callableValues = buildCallableParameters(input, inventory, ZC_ASSERT_NONNULL(syntax));
  auto scopeRoles = buildScopeRoles(input.parsed.tree(), input.authoritySite.node());
  auto memberVisibility = visibility(input.parsed.tree(), ZC_ASSERT_NONNULL(syntax));
  if (name == zc::none || disposition == zc::none ||
      ZC_ASSERT_NONNULL(name).text() != record.name() ||
      ZC_ASSERT_NONNULL(disposition) != input.entry.bodyDisposition() ||
      genericValues == zc::none || callableValues == zc::none || scopeRoles == zc::none) {
    return zc::none;
  }

  const auto activation = record.kind() == identity::DefinitionKind::ModuleAlias
                              ? DefinitionActivation::ImportSurface
                              : DefinitionActivation::ModuleSkeleton;
  return StableDefinitionHeader::from(
      input.queryKey.clone(), record.clone(), input.authoritySite.site().clone(), record.kind(),
      static_cast<Namespace>(record.nameSpace()), zc::mv(ZC_ASSERT_NONNULL(name)), activation,
      zc::mv(memberVisibility), ZC_ASSERT_NONNULL(disposition),
      zc::mv(ZC_ASSERT_NONNULL(genericValues)), zc::mv(ZC_ASSERT_NONNULL(callableValues)),
      zc::mv(ZC_ASSERT_NONNULL(scopeRoles)));
}

}  // namespace zomlang::compiler::binder
