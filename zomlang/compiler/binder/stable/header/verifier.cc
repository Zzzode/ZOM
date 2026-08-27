// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/stable/header/verifier.h"

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

zc::Maybe<const NamedDefinitionInventoryEntry&> definitionEntry(
    const StableHeaderVerificationContext& context, const identity::DefinitionKey& key) {
  zc::Maybe<const NamedDefinitionInventoryEntry&> selected;
  for (const auto& entry : context.definitionInventory.entries()) {
    if (entry.key() != key) { continue; }
    if (selected != zc::none) { return zc::none; }
    selected = entry;
  }
  return selected;
}

zc::Maybe<const NamedImplementationInventoryEntry&> implementationEntry(
    const StableHeaderVerificationContext& context, const identity::ImplKey& key) {
  zc::Maybe<const NamedImplementationInventoryEntry&> selected;
  for (const auto& entry : context.implementationInventory.entries()) {
    if (entry.key() != key) { continue; }
    if (selected != zc::none) { return zc::none; }
    selected = entry;
  }
  return selected;
}

zc::Maybe<const RevisionLocalDefinitionSite&> definitionAuthority(
    const StableHeaderVerificationContext& context, const identity::DefinitionKey& key) {
  zc::Maybe<const RevisionLocalDefinitionSite&> selected;
  for (const auto& site : context.definitionSites.entries()) {
    if (site.definition() != key) { continue; }
    if (selected == zc::none || sourceOrderLess(site, ZC_ASSERT_NONNULL(selected))) {
      selected = site;
    }
  }
  return selected;
}

zc::Maybe<const RevisionLocalImplementationSite&> implementationOccurrence(
    const StableHeaderVerificationContext& context, const ImplSourceOccurrenceKey& occurrence) {
  zc::Maybe<const RevisionLocalImplementationSite&> selected;
  for (const auto& site : context.implementationSites.entries()) {
    if (!site.occurrence().sameAs(occurrence)) { continue; }
    if (selected != zc::none) { return zc::none; }
    selected = site;
  }
  return selected;
}

zc::Maybe<const DefinitionInventoryEntry&> definitionAt(const DefinitionInventory& inventory,
                                                        ast::NodeId node) {
  for (const auto& entry : inventory.definitions()) {
    if (entry.node == node) { return entry; }
  }
  return zc::none;
}

zc::Maybe<const ImplInventoryEntry&> implementationAt(const DefinitionInventory& inventory,
                                                      ast::NodeId node) {
  for (const auto& entry : inventory.impls()) {
    if (entry.node == node) { return entry; }
  }
  return zc::none;
}

zc::Maybe<DefinitionBodyDisposition> bodyDisposition(const ast::Tree& tree, ast::NodeId node) {
  if (!tree.contains(node)) { return zc::none; }
  const auto& syntax = tree.node(node);
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
      if (!initializer) { return DefinitionBodyDisposition::NoExecutableBody; }
      if (!tree.contains(initializer)) { return zc::none; }
      const auto kind = tree.node(initializer).kind;
      if (!ast::isLiteralExprKind(kind) && !ast::isExprKind(kind) &&
          kind != ast::SyntaxKind::UnsafeBlockExpr) {
        return zc::none;
      }
      return DefinitionBodyDisposition::ExecutableBody;
    }
    default:
      return DefinitionBodyDisposition::NoExecutableBody;
  }
  const ast::NodeId body(syntax.payload.words[bodyWord]);
  if (syntax.kind == ast::SyntaxKind::MethodDecl && !body) {
    return DefinitionBodyDisposition::NoExecutableBody;
  }
  if (!body || !tree.contains(body) || tree.node(body).kind != ast::SyntaxKind::BlockStmt) {
    return zc::none;
  }
  return DefinitionBodyDisposition::ExecutableBody;
}

bool completeAuthority(const StableHeaderVerificationContext& context,
                       const VerifiedStableIdentityCandidateInventory& verified) {
  if (verified.definitions.size() != context.definitionSites.entries().size() ||
      verified.implementations.size() != context.implementationSites.entries().size()) {
    return false;
  }
  for (const auto& definition : verified.definitions) {
    size_t siteMatches = 0;
    size_t entryMatches = 0;
    for (const auto& site : context.definitionSites.entries()) {
      if (site.node() == definition.node && site.definition() == definition.authority.key() &&
          site.site().sameAs(definition.site) &&
          site.byteStart() == definition.source.byteStart() &&
          site.byteEnd() == definition.source.byteEnd()) {
        ++siteMatches;
      }
    }
    auto disposition = bodyDisposition(context.parsed.tree(), definition.node);
    if (disposition == zc::none) { return false; }
    for (const auto& entry : context.definitionInventory.entries()) {
      if (entry.key() == definition.authority.key() &&
          entry.record().encode().asPtr() == definition.authority.record().encode().asPtr() &&
          entry.bodyDisposition() == ZC_ASSERT_NONNULL(disposition)) {
        ++entryMatches;
      }
    }
    if (siteMatches != 1 || entryMatches != 1) { return false; }
  }
  for (const auto& entry : context.definitionInventory.entries()) {
    size_t matches = 0;
    for (const auto& definition : verified.definitions) {
      if (entry.key() == definition.authority.key() &&
          entry.record().encode().asPtr() == definition.authority.record().encode().asPtr()) {
        ++matches;
      }
    }
    if (matches == 0) { return false; }
  }
  for (const auto& implementation : verified.implementations) {
    size_t siteMatches = 0;
    size_t entryMatches = 0;
    for (const auto& site : context.implementationSites.entries()) {
      if (site.node() == implementation.node &&
          site.occurrence().implementation() == implementation.authority.key() &&
          site.occurrence().site().sameAs(implementation.site) &&
          site.byteStart() == implementation.source.byteStart() &&
          site.byteEnd() == implementation.source.byteEnd()) {
        ++siteMatches;
      }
    }
    for (const auto& entry : context.implementationInventory.entries()) {
      if (entry.key() == implementation.authority.key() &&
          entry.record().encode().asPtr() == implementation.authority.record().encode().asPtr()) {
        ++entryMatches;
      }
    }
    if (siteMatches != 1 || entryMatches != 1) { return false; }
  }
  for (const auto& entry : context.implementationInventory.entries()) {
    size_t matches = 0;
    for (const auto& implementation : verified.implementations) {
      if (entry.key() == implementation.authority.key() &&
          entry.record().encode().asPtr() == implementation.authority.record().encode().asPtr()) {
        ++matches;
      }
    }
    if (matches == 0) { return false; }
  }
  return true;
}

zc::Maybe<const identity::DefinitionKey&> definitionKeyAt(
    const StableHeaderVerificationContext& context, ast::NodeId node) {
  for (const auto& site : context.definitionSites.entries()) {
    if (site.node() == node) { return site.definition(); }
  }
  return zc::none;
}

zc::Maybe<const identity::ImplKey&> implementationKeyAt(
    const StableHeaderVerificationContext& context, ast::NodeId node) {
  for (const auto& site : context.implementationSites.entries()) {
    if (site.node() == node) { return site.occurrence().implementation(); }
  }
  return zc::none;
}

bool matchesOwners(const StableHeaderVerificationContext& context,
                   zc::ArrayPtr<const StructuralIdentityParent> parents,
                   zc::ArrayPtr<const identity::EnclosingStableOwnerKey> owners) {
  if (parents.size() != owners.size()) { return false; }
  for (size_t index = 0; index < parents.size(); ++index) {
    if (parents[index].kind == StructuralIdentityParentKind::Definition) {
      auto actual = definitionKeyAt(context, parents[index].node);
      auto expected = owners[index].definitionKey();
      if (actual == zc::none || expected == zc::none ||
          ZC_ASSERT_NONNULL(actual) != ZC_ASSERT_NONNULL(expected)) {
        return false;
      }
    } else {
      auto actual = implementationKeyAt(context, parents[index].node);
      auto expected = owners[index].implKey();
      if (actual == zc::none || expected == zc::none ||
          ZC_ASSERT_NONNULL(actual) != ZC_ASSERT_NONNULL(expected)) {
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

bool interfaceOwner(const ast::Tree& tree, const DefinitionInventoryEntry& entry) {
  if (entry.parentPath.empty()) { return false; }
  const auto& parent = entry.parentPath.back();
  return parent.kind == StructuralIdentityParentKind::Definition && tree.contains(parent.node) &&
         tree.node(parent.node).kind == ast::SyntaxKind::InterfaceDecl;
}

zc::Maybe<MemberVisibility> expectedVisibility(const ast::Tree& tree,
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

bool sameVisibility(const zc::Maybe<MemberVisibility>& left,
                    const zc::Maybe<MemberVisibility>& right) {
  if ((left == zc::none) != (right == zc::none)) { return false; }
  return left == zc::none || ZC_ASSERT_NONNULL(left) == ZC_ASSERT_NONNULL(right);
}

bool hasRole(const CanonicalSequence<ScopeRole>& roles, ScopeRole expected) {
  for (const auto role : roles.values()) {
    if (role == expected) { return true; }
  }
  return false;
}

bool definitionRoles(const ast::Tree& tree, ast::NodeId node,
                     const CanonicalSequence<ScopeRole>& roles) {
  bool parameters = false;
  bool members = false;
  switch (tree.node(node).kind) {
    case ast::SyntaxKind::ExternDecl:
    case ast::SyntaxKind::FunctionDecl:
    case ast::SyntaxKind::MethodDecl:
    case ast::SyntaxKind::ConstructorDecl:
    case ast::SyntaxKind::DestructorDecl:
      parameters = true;
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
      members = true;
      break;
    default:
      break;
  }
  const bool generic = static_cast<bool>(genericParameters(tree, node));
  const size_t expectedSize = 1 + (generic ? 1 : 0) + (parameters ? 1 : 0) + (members ? 1 : 0);
  return roles.values().size() == expectedSize && hasRole(roles, ScopeRole::Declaration) &&
         hasRole(roles, ScopeRole::Generic) == generic &&
         hasRole(roles, ScopeRole::Parameters) == parameters &&
         hasRole(roles, ScopeRole::Members) == members &&
         !hasRole(roles, ScopeRole::Implementation);
}

bool definitionGenericParameters(const StableHeaderVerificationContext& context,
                                 const DefinitionInventory& inventory, ast::NodeId owner,
                                 const identity::DefinitionKey& definition,
                                 const IdentitySyntaxSiteKey& authority,
                                 const CanonicalSequence<StableHeaderGenericParameter>& candidate) {
  size_t expectedCount = 0;
  uint32_t ordinal = 0;
  for (const auto& parameter : inventory.genericParameters()) {
    if (parameter.parentPath.empty() || parameter.parentPath.back().node != owner) { continue; }
    if (parameter.nameKind != InventoryDefinitionNameKind::Declared) { return false; }
    auto name = identity::DeclaredDefinitionName::fromSource(
        context.parsed.tree().ident(parameter.declaredName));
    if (name == zc::none) { return false; }
    auto record = identity::GenericParameterIdentityRecord::type(
        identity::StableGenericParameterOwnerKey::definition(definition.clone()), ordinal);
    const auto key = identity::GenericParameterKey::compute(record);
    size_t matches = 0;
    for (const auto& value : candidate.values()) {
      const auto expectedSite = StableHeaderSite::definition(authority.clone());
      if (value.ordinal() == ordinal && value.key() == key &&
          value.record().encode().asPtr() == record.encode().asPtr() &&
          value.site() == expectedSite && value.name().text() == ZC_ASSERT_NONNULL(name).text()) {
        ++matches;
      }
    }
    if (matches != 1) { return false; }
    ++ordinal;
    ++expectedCount;
  }
  return candidate.values().size() == expectedCount;
}

bool callableParameters(const StableHeaderVerificationContext& context,
                        const DefinitionInventory& inventory, const DefinitionInventoryEntry& owner,
                        const identity::DefinitionKey& definition,
                        const IdentitySyntaxSiteKey& authority,
                        const CanonicalSequence<StableHeaderCallableParameter>& candidate) {
  size_t expectedCount = 0;
  uint32_t ordinaryOrdinal = 0;
  bool receiverSeen = false;
  size_t localOrdinal = 0;
  for (const auto& parameter : inventory.callableParameters()) {
    if (parameter.parentPath.empty() || parameter.parentPath.back().node != owner.node) {
      continue;
    }
    const bool receiver = context.parsed.functionParameterNameSpan(
                              parameter.node, ast::SyntaxKind::ThisKeyword) != zc::none;
    if (receiver && (receiverSeen || localOrdinal != 0 ||
                     context.parsed.tree().node(owner.node).kind != ast::SyntaxKind::MethodDecl)) {
      return false;
    }
    zc::Maybe<identity::DeclaredDefinitionName> name;
    if (receiver) {
      receiverSeen = true;
    } else {
      if (parameter.nameKind != InventoryDefinitionNameKind::Declared) { return false; }
      name = identity::DeclaredDefinitionName::fromSource(
          context.parsed.tree().ident(parameter.declaredName));
      if (name == zc::none) { return false; }
    }
    const auto position = receiver
                              ? identity::CallableParameterPosition::receiver()
                              : identity::CallableParameterPosition::ordinary(ordinaryOrdinal++);
    auto record = identity::CallableParameterIdentityRecord::from(definition.clone(), position);
    const auto key = identity::CallableParameterKey::compute(record);
    size_t matches = 0;
    for (const auto& value : candidate.values()) {
      const auto expectedSite = StableHeaderSite::definition(authority.clone());
      bool sameName = (value.name() == zc::none) == (name == zc::none);
      if (sameName && name != zc::none) {
        sameName = ZC_ASSERT_NONNULL(value.name()).text() == ZC_ASSERT_NONNULL(name).text();
      }
      const auto valueOrdinal = value.position().ordinal();
      const auto expectedOrdinal = position.ordinal();
      const bool samePosition =
          value.position().kind() == position.kind() &&
          (valueOrdinal == zc::none) == (expectedOrdinal == zc::none) &&
          (valueOrdinal == zc::none ||
           ZC_ASSERT_NONNULL(valueOrdinal) == ZC_ASSERT_NONNULL(expectedOrdinal));
      if (samePosition && value.key() == key &&
          value.record().encode().asPtr() == record.encode().asPtr() &&
          value.site() == expectedSite && sameName) {
        ++matches;
      }
    }
    if (matches != 1) { return false; }
    ++localOrdinal;
    ++expectedCount;
  }
  return candidate.values().size() == expectedCount;
}

bool implementationGenericParameters(
    const StableHeaderVerificationContext& context, const DefinitionInventory& inventory,
    ast::NodeId owner, const identity::ImplKey& implementation,
    const ImplSourceOccurrenceKey& occurrence,
    const CanonicalSequence<StableHeaderGenericParameter>& candidate) {
  size_t expectedCount = 0;
  uint32_t ordinal = 0;
  for (const auto& parameter : inventory.genericParameters()) {
    if (parameter.parentPath.empty() ||
        parameter.parentPath.back().kind != StructuralIdentityParentKind::Impl ||
        parameter.parentPath.back().node != owner) {
      continue;
    }
    if (parameter.nameKind != InventoryDefinitionNameKind::Declared) { return false; }
    auto name = identity::DeclaredDefinitionName::fromSource(
        context.parsed.tree().ident(parameter.declaredName));
    if (name == zc::none) { return false; }
    auto record = identity::GenericParameterIdentityRecord::type(
        identity::StableGenericParameterOwnerKey::implementation(implementation.clone()), ordinal);
    const auto key = identity::GenericParameterKey::compute(record);
    size_t matches = 0;
    for (const auto& value : candidate.values()) {
      const auto expectedSite = StableHeaderSite::implementation(occurrence.clone());
      if (value.ordinal() == ordinal && value.key() == key &&
          value.record().encode().asPtr() == record.encode().asPtr() &&
          value.site() == expectedSite && value.name().text() == ZC_ASSERT_NONNULL(name).text()) {
        ++matches;
      }
    }
    if (matches != 1) { return false; }
    ++ordinal;
    ++expectedCount;
  }
  return candidate.values().size() == expectedCount;
}

zc::Maybe<ImplementationSourceForm> implementationSourceForm(const ast::Tree& tree,
                                                             ast::NodeId node) {
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

bool implementationRoles(const ast::Tree& tree, ast::NodeId node,
                         const CanonicalSequence<ScopeRole>& roles) {
  const auto& syntax = tree.node(node);
  const bool generic = syntax.kind == ast::SyntaxKind::StandaloneImplDecl &&
                       syntax.payload.words[ast::kStandaloneImplDeclTypeParamsIdWord] != 0;
  return roles.values().size() == 1 + (generic ? 1 : 0) &&
         hasRole(roles, ScopeRole::Generic) == generic &&
         hasRole(roles, ScopeRole::Implementation) && !hasRole(roles, ScopeRole::Declaration) &&
         !hasRole(roles, ScopeRole::Parameters) && !hasRole(roles, ScopeRole::Members);
}

template <typename T>
bool canonicalRoundTrip(const T& candidate) {
  const auto bytes = StableBindingCodec<T>::encode(candidate);
  auto decoded = StableBindingCodec<T>::decode(bytes.asPtr());
  return decoded != zc::none && ZC_ASSERT_NONNULL(decoded) == candidate &&
         StableBindingCodec<T>::encode(ZC_ASSERT_NONNULL(decoded)).asPtr() == bytes.asPtr();
}

}  // namespace

bool StableHeaderVerifier::verifyDefinition(const StableHeaderVerificationContext& context,
                                            const StableDefinitionQueryKey& queryKey,
                                            const StableDefinitionHeader& candidate) {
  auto entry = definitionEntry(context, queryKey.definition());
  auto authority = definitionAuthority(context, queryKey.definition());
  if (entry == zc::none || authority == zc::none ||
      !sameModule(queryKey.module(), ZC_ASSERT_NONNULL(entry).record().module()) ||
      candidate.queryKey() != queryKey ||
      candidate.record().encode().asPtr() != ZC_ASSERT_NONNULL(entry).record().encode().asPtr() ||
      !candidate.authoritySite().sameAs(ZC_ASSERT_NONNULL(authority).site())) {
    return false;
  }

  const auto inventory = DefinitionInventory::collect(context.parsed.tree());
  auto syntax = definitionAt(inventory, ZC_ASSERT_NONNULL(authority).node());
  if (syntax == zc::none || ZC_ASSERT_NONNULL(syntax).kind != candidate.kind() ||
      ZC_ASSERT_NONNULL(syntax).kind != ZC_ASSERT_NONNULL(entry).record().kind() ||
      ZC_ASSERT_NONNULL(syntax).nameKind != InventoryDefinitionNameKind::Declared) {
    return false;
  }
  auto reconstructed = CandidateVerifier::reconstruct(context.parsed, queryKey.module(),
                                                      ZC_ASSERT_NONNULL(syntax).moduleNode);
  if (!reconstructed.is<VerifiedStableIdentityCandidateInventory>() ||
      !completeAuthority(context, reconstructed.get<VerifiedStableIdentityCandidateInventory>()) ||
      !matchesOwners(context, ZC_ASSERT_NONNULL(syntax).parentPath.asPtr(),
                     ZC_ASSERT_NONNULL(entry).record().owners())) {
    return false;
  }

  auto name = identity::DeclaredDefinitionName::fromSource(
      context.parsed.tree().ident(ZC_ASSERT_NONNULL(syntax).declaredName));
  auto disposition = bodyDisposition(context.parsed.tree(), ZC_ASSERT_NONNULL(authority).node());
  auto visibility = expectedVisibility(context.parsed.tree(), ZC_ASSERT_NONNULL(syntax));
  if (name == zc::none || disposition == zc::none ||
      ZC_ASSERT_NONNULL(name).text() != ZC_ASSERT_NONNULL(entry).record().name() ||
      candidate.name().text() != ZC_ASSERT_NONNULL(name).text() ||
      candidate.nameSpace() !=
          static_cast<Namespace>(ZC_ASSERT_NONNULL(entry).record().nameSpace()) ||
      candidate.bodyDisposition() != ZC_ASSERT_NONNULL(disposition) ||
      candidate.bodyDisposition() != ZC_ASSERT_NONNULL(entry).bodyDisposition() ||
      !sameVisibility(candidate.visibility(), visibility)) {
    return false;
  }
  const auto activation = candidate.kind() == identity::DefinitionKind::ModuleAlias
                              ? DefinitionActivation::ImportSurface
                              : DefinitionActivation::ModuleSkeleton;
  return candidate.activation() == activation &&
         definitionGenericParameters(context, inventory, ZC_ASSERT_NONNULL(authority).node(),
                                     queryKey.definition(), ZC_ASSERT_NONNULL(authority).site(),
                                     candidate.genericParameters()) &&
         callableParameters(context, inventory, ZC_ASSERT_NONNULL(syntax), queryKey.definition(),
                            ZC_ASSERT_NONNULL(authority).site(), candidate.callableParameters()) &&
         definitionRoles(context.parsed.tree(), ZC_ASSERT_NONNULL(authority).node(),
                         candidate.declaredScopeRoles()) &&
         canonicalRoundTrip(candidate);
}

bool StableHeaderVerifier::verifyImplementationOccurrence(
    const StableHeaderVerificationContext& context,
    const StableImplementationOccurrenceQueryKey& queryKey,
    const StableImplementationOccurrenceHeader& candidate) {
  auto entry = implementationEntry(context, queryKey.occurrence().implementation());
  auto occurrence = implementationOccurrence(context, queryKey.occurrence());
  if (entry == zc::none || occurrence == zc::none ||
      !sameModule(queryKey.module(), ZC_ASSERT_NONNULL(entry).record().module()) ||
      candidate.queryKey() != queryKey ||
      !sameModule(candidate.authority().module(), queryKey.module()) ||
      candidate.authority().implementation() != ZC_ASSERT_NONNULL(entry).key() ||
      candidate.record().encode().asPtr() != ZC_ASSERT_NONNULL(entry).record().encode().asPtr()) {
    return false;
  }

  const auto inventory = DefinitionInventory::collect(context.parsed.tree());
  auto syntax = implementationAt(inventory, ZC_ASSERT_NONNULL(occurrence).node());
  if (syntax == zc::none) { return false; }
  auto reconstructed = CandidateVerifier::reconstruct(context.parsed, queryKey.module(),
                                                      ZC_ASSERT_NONNULL(syntax).moduleNode);
  if (!reconstructed.is<VerifiedStableIdentityCandidateInventory>() ||
      !completeAuthority(context, reconstructed.get<VerifiedStableIdentityCandidateInventory>()) ||
      !matchesOwners(context, ZC_ASSERT_NONNULL(syntax).parentPath.asPtr(),
                     ZC_ASSERT_NONNULL(entry).record().owners())) {
    return false;
  }
  auto sourceForm =
      implementationSourceForm(context.parsed.tree(), ZC_ASSERT_NONNULL(occurrence).node());
  return sourceForm != zc::none && candidate.sourceForm() == ZC_ASSERT_NONNULL(sourceForm) &&
         implementationGenericParameters(context, inventory, ZC_ASSERT_NONNULL(occurrence).node(),
                                         ZC_ASSERT_NONNULL(entry).key(),
                                         ZC_ASSERT_NONNULL(occurrence).occurrence(),
                                         candidate.genericParameters()) &&
         implementationRoles(context.parsed.tree(), ZC_ASSERT_NONNULL(occurrence).node(),
                             candidate.declaredScopeRoles()) &&
         canonicalRoundTrip(candidate);
}

}  // namespace zomlang::compiler::binder
