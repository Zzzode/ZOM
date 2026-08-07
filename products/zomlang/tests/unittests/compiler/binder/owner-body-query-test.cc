// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations under
// the License.

#include "zomlang/compiler/binder/owner-body-query.h"

#include "zc/core/vector.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/ast/generated/node-schema.h"
#include "zomlang/compiler/binder/stable-binding-codec.h"
#include "zomlang/compiler/identity/canonical-encoder.h"
#include "zomlang/tests/unittests/compiler/test-semantic-identities.h"

namespace zomlang::compiler::binder {
namespace {

template <typename T>
T require(zc::Maybe<T>&& value) {
  return zc::mv(ZC_REQUIRE_NONNULL(value));
}

zc::Array<uint8_t> emptyStatementPayload() {
  identity::CanonicalEncoder encoder;
  encoder.encodeSequenceSize(0);
  return encoder.finish();
}

zc::Array<uint8_t> blockPayload(uint32_t statementCount) {
  identity::CanonicalEncoder encoder;
  encoder.encodeSequenceSize(1);
  encoder.encodeUint8(2);
  encoder.encodeBool(false);
  encoder.encodeSequenceSize(statementCount);
  return encoder.finish();
}

zc::Array<uint8_t> identExprPayload(zc::StringPtr name) {
  identity::CanonicalEncoder encoder;
  encoder.encodeSequenceSize(1);
  encoder.encodeUint8(static_cast<uint8_t>(ast::NodeSchemaFieldStorage::IdentId) + 1);
  encoder.encodeBool(false);
  encoder.encodeBool(true);
  encoder.encodeByteString(name.asBytes());
  return encoder.finish();
}

identity::DefinitionKey foreignDefinitionKey() {
  uint8_t bytes[32] = {};
  bytes[31] = 1;
  return require(identity::DefinitionKey::fromBytes(zc::arrayPtr(bytes)));
}

identity::DefinitionKey otherDefinitionKey() {
  uint8_t bytes[32] = {};
  bytes[30] = 1;
  return require(identity::DefinitionKey::fromBytes(zc::arrayPtr(bytes)));
}

identity::ModuleKey foreignModuleKey() {
  zc::Vector<identity::ModulePathSegment> path;
  path.add(tests::test_identity_detail::scalar<identity::ModulePathSegment>("foreign"_zc));
  return require(identity::ModuleKey::from(tests::test_identity_detail::crate(), zc::mv(path)));
}

IdentitySyntaxSiteKey headerSite() {
  zc::Vector<uint32_t> path;
  return require(IdentitySyntaxSiteKey::from(tests::test_identity_detail::module(),
                                             tests::test_identity_detail::source(), zc::mv(path)));
}

template <typename T>
void sortCanonical(zc::Vector<T>& values) {
  for (size_t index = 1; index < values.size(); ++index) {
    auto current = zc::mv(values[index]);
    const auto currentBytes = StableBindingCodec<T>::encode(current);
    size_t insertion = index;
    while (insertion != 0) {
      const auto previousBytes = StableBindingCodec<T>::encode(values[insertion - 1]);
      if (previousBytes.asPtr() < currentBytes.asPtr()) { break; }
      values[insertion] = zc::mv(values[insertion - 1]);
      --insertion;
    }
    values[insertion] = zc::mv(current);
  }
}

template <typename T>
CanonicalSequence<T> singletonSequence(T&& value) {
  zc::Vector<T> values;
  values.add(zc::mv(value));
  return require(StableBindingSequenceBuilder<T>::from(zc::mv(values)));
}

struct DefinitionBodySkeleton final {
  BoundModuleSkeleton skeleton;
  StableOwnerBodyQueryKey owner;
  StableScopeOwnerKey rootScope;
};

DefinitionBodySkeleton definitionBodySkeleton(identity::DefinitionKind kind) {
  auto module = tests::test_identity_detail::module();
  auto name = require(identity::DeclaredDefinitionName::fromCanonical("body"_zc));
  const bool callable = kind == identity::DefinitionKind::Function;
  zc::Vector<identity::EnclosingStableOwnerKey> owners;
  zc::Maybe<identity::OverloadHeaderDigest> overloadDigest;
  if (callable) {
    uint8_t digestBytes[32] = {};
    digestBytes[0] = 1;
    overloadDigest = require(identity::OverloadHeaderDigest::fromBytes(zc::arrayPtr(digestBytes)));
  }
  auto nameSpace = require(identity::definitionNamespaceFor(kind));
  auto record = require(identity::DefinitionIdentityRecord::from(
      module.clone(), zc::mv(owners), kind, nameSpace, name.clone(), zc::mv(overloadDigest)));
  auto definition = identity::DefinitionKey::compute(record);
  auto queryKey = StableDefinitionQueryKey::from(module.clone(), definition.clone());
  zc::Maybe<MemberVisibility> noVisibility;
  auto declaration = require(StableDeclarationFact::from(
      queryKey.clone(), record.clone(), StableScopeOwnerKey::module(module.clone()), kind,
      Namespace::Value, zc::mv(name), DefinitionActivation::ModuleSkeleton, zc::mv(noVisibility)));

  zc::Maybe<StableScopeOwnerKey> noParent;
  auto moduleScope = require(StableScopeFact::from(StableScopeOwnerKey::module(module.clone()),
                                                   zc::mv(noParent), ScopeKind::Module));
  auto declarationScopeOwner =
      require(StableScopeOwnerKey::definition(queryKey.clone(), ScopeRole::Declaration));
  auto declarationScope = require(StableScopeFact::from(
      declarationScopeOwner.clone(),
      zc::Maybe<StableScopeOwnerKey>(StableScopeOwnerKey::module(module.clone())),
      ScopeKind::TypeBody));
  zc::Vector<StableScopeFact> scopes;
  scopes.add(zc::mv(moduleScope));
  scopes.add(zc::mv(declarationScope));
  auto rootScope = declarationScopeOwner.clone();
  if (callable) {
    auto parameterScopeOwner =
        require(StableScopeOwnerKey::definition(queryKey.clone(), ScopeRole::Parameters));
    auto parameterScope = require(StableScopeFact::from(
        parameterScopeOwner.clone(), zc::Maybe<StableScopeOwnerKey>(declarationScopeOwner.clone()),
        ScopeKind::Function));
    scopes.add(zc::mv(parameterScope));
    rootScope = zc::mv(parameterScopeOwner);
  }
  sortCanonical(scopes);
  auto admittedScopes =
      require(StableBindingSequenceBuilder<StableScopeFact>::from(zc::mv(scopes)));

  auto moduleOwner = require(
      StableOwnerBodyQueryKey::from(module.clone(), StableBodyOwnerKey::module(module.clone())));
  auto definitionOwner = require(StableOwnerBodyQueryKey::from(
      module.clone(), StableBodyOwnerKey::definition(definition.clone())));
  zc::Vector<StableOwnerBodyQueryKey> ownerValues;
  ownerValues.add(zc::mv(moduleOwner));
  ownerValues.add(definitionOwner.clone());
  sortCanonical(ownerValues);
  auto bodyOwners = require(
      StableBindingSequenceBuilder<StableOwnerBodyQueryKey>::fromNonEmpty(zc::mv(ownerValues)));
  auto callableParameters = CanonicalSequence<StableCallableParameterDeclarationFact>::empty();
  if (callable) {
    auto receiverRecord = identity::CallableParameterIdentityRecord::from(
        definition.clone(), identity::CallableParameterPosition::receiver());
    zc::Maybe<identity::DeclaredDefinitionName> noName;
    auto receiver = require(StableCallableParameterDeclarationFact::from(
        StableCallableParameterQueryKey::from(
            module.clone(), identity::CallableParameterKey::compute(receiverRecord)),
        receiverRecord.clone(), StableHeaderSite::definition(headerSite()), rootScope.clone(),
        zc::mv(noName)));
    callableParameters = singletonSequence(zc::mv(receiver));
  }
  auto skeleton = require(BoundModuleSkeleton::from(
      zc::mv(module), zc::mv(admittedScopes), CanonicalSequence<StableNodeScopeFact>::empty(),
      singletonSequence(zc::mv(declaration)),
      CanonicalSequence<StableImplementationOccurrenceFact>::empty(),
      CanonicalSequence<StableGenericParameterDeclarationFact>::empty(), zc::mv(callableParameters),
      CanonicalSequence<StableModuleAliasFact>::empty(),
      CanonicalSequence<StableImportFact>::empty(),
      CanonicalSequence<StableLocalExportFact>::empty(), zc::mv(bodyOwners),
      CanonicalSequence<StableFailedLookupFact>::empty()));
  return DefinitionBodySkeleton{zc::mv(skeleton), zc::mv(definitionOwner), zc::mv(rootScope)};
}

}  // namespace

ZC_TEST("OwnerBodyQueryTest.TraversalRebuildsPreorderPathsAndAncestry") {
  zc::Vector<DetachedModuleBodyNode> nodes;
  nodes.add(
      require(DetachedModuleBodyNode::syntax(ast::SyntaxKind::BlockStmt, blockPayload(1), 1)));
  nodes.add(require(
      DetachedModuleBodyNode::syntax(ast::SyntaxKind::EmptyStatement, emptyStatementPayload(), 0)));
  nodes.add(DetachedModuleBodyNode::definitionBoundary(foreignDefinitionKey()));
  auto syntax = require(ModuleBodySyntax::from(2, zc::mv(nodes)));

  auto traversal = require(OwnerBodySyntaxTraversal::from(syntax));
  const auto entries = traversal.entries();
  ZC_REQUIRE(entries.size() == 3);
  uint32_t rootPath[] = {0};
  uint32_t childPath[] = {0, 0};
  uint32_t boundaryPath[] = {1};
  ZC_EXPECT(entries[0].path.components() == zc::arrayPtr(rootPath) &&
            entries[0].parentIndex == UINT32_MAX && entries[0].scopeKind == ScopeKind::Block);
  ZC_EXPECT(entries[1].path.components() == zc::arrayPtr(childPath) && entries[1].parentIndex == 0);
  ZC_EXPECT(entries[2].path.components() == zc::arrayPtr(boundaryPath) &&
            entries[2].kind == DetachedModuleBodyNodeKind::DefinitionBoundary);
}

ZC_TEST("OwnerBodyQueryTest.TraversalCarriesRootIdentityAcrossNestedRoots") {
  zc::Vector<DetachedModuleBodyNode> nodes;
  nodes.add(
      require(DetachedModuleBodyNode::syntax(ast::SyntaxKind::BlockStmt, blockPayload(1), 1)));
  nodes.add(
      require(DetachedModuleBodyNode::syntax(ast::SyntaxKind::BlockStmt, blockPayload(1), 1)));
  nodes.add(require(
      DetachedModuleBodyNode::syntax(ast::SyntaxKind::EmptyStatement, emptyStatementPayload(), 0)));
  nodes.add(
      require(DetachedModuleBodyNode::syntax(ast::SyntaxKind::BlockStmt, blockPayload(1), 1)));
  nodes.add(require(
      DetachedModuleBodyNode::syntax(ast::SyntaxKind::EmptyStatement, emptyStatementPayload(), 0)));
  auto syntax = require(ModuleBodySyntax::from(2, zc::mv(nodes)));

  auto traversal = require(OwnerBodySyntaxTraversal::from(syntax));
  const auto entries = traversal.entries();
  ZC_REQUIRE(entries.size() == 5);
  uint32_t firstGrandchildPath[] = {0, 0, 0};
  uint32_t secondRootPath[] = {1};
  uint32_t secondChildPath[] = {1, 0};
  ZC_EXPECT(entries[2].path.components() == zc::arrayPtr(firstGrandchildPath) &&
            entries[2].rootIndex == 0);
  ZC_EXPECT(entries[3].path.components() == zc::arrayPtr(secondRootPath) &&
            entries[3].parentIndex == UINT32_MAX && entries[3].rootIndex == 1);
  ZC_EXPECT(entries[4].path.components() == zc::arrayPtr(secondChildPath) &&
            entries[4].parentIndex == 3 && entries[4].rootIndex == 1);
}

ZC_TEST("OwnerBodyQueryTest.TraversalAdmitsEmptyDetachedSyntax") {
  zc::Vector<DetachedModuleBodyNode> nodes;
  auto syntax = require(ModuleBodySyntax::from(0, zc::mv(nodes)));
  auto traversal = require(OwnerBodySyntaxTraversal::from(syntax));
  ZC_EXPECT(traversal.entries().size() == 0);
}

ZC_TEST("OwnerBodyQueryTest.ScopeProjectionRetainsInheritedModuleScope") {
  zc::Vector<DetachedModuleBodyNode> nodes;
  nodes.add(require(
      DetachedModuleBodyNode::syntax(ast::SyntaxKind::EmptyStatement, emptyStatementPayload(), 0)));
  nodes.add(
      require(DetachedModuleBodyNode::syntax(ast::SyntaxKind::BlockStmt, blockPayload(1), 1)));
  nodes.add(require(
      DetachedModuleBodyNode::syntax(ast::SyntaxKind::EmptyStatement, emptyStatementPayload(), 0)));
  auto syntax = require(ModuleBodySyntax::from(2, zc::mv(nodes)));
  auto module = tests::test_identity_detail::module();
  auto owner = require(
      StableOwnerBodyQueryKey::from(module.clone(), StableBodyOwnerKey::module(module.clone())));
  auto rootScope = StableScopeOwnerKey::module(module.clone());

  auto projection = require(OwnerBodyScopeProjection::from(owner, syntax, rootScope));
  ZC_EXPECT(OwnerBodyScopeProjection::verify(owner, syntax, rootScope, projection.scopes(),
                                             projection.nodeScopes()));
  ZC_REQUIRE(projection.scopes().values().size() == 1);
  uint32_t emptyPath[] = {0};
  uint32_t blockPath[] = {1};
  uint32_t childPath[] = {1, 0};
  const auto& nodeScopes = projection.nodeScopes().values();
  ZC_REQUIRE(nodeScopes.size() == 3);
  ZC_EXPECT(nodeScopes[0].nodePath().components() == zc::arrayPtr(emptyPath) &&
            nodeScopes[0].scope() == rootScope);
  ZC_EXPECT(nodeScopes[1].nodePath().components() == zc::arrayPtr(blockPath));
  ZC_EXPECT(nodeScopes[2].nodePath().components() == zc::arrayPtr(childPath));
}

ZC_TEST("OwnerBodyQueryTest.ScopeProjectionRejectsWrongDefinitionHeaderScope") {
  zc::Vector<DetachedModuleBodyNode> nodes;
  nodes.add(require(
      DetachedModuleBodyNode::syntax(ast::SyntaxKind::EmptyStatement, emptyStatementPayload(), 0)));
  auto syntax = require(ModuleBodySyntax::from(1, zc::mv(nodes)));
  auto module = tests::test_identity_detail::module();
  auto definition = foreignDefinitionKey();
  auto owner = require(StableOwnerBodyQueryKey::from(
      module.clone(), StableBodyOwnerKey::definition(definition.clone())));
  auto headerScope = require(StableScopeOwnerKey::definition(
      StableDefinitionQueryKey::from(module.clone(), definition.clone()), ScopeRole::Declaration));
  auto projection = require(OwnerBodyScopeProjection::from(owner.clone(), syntax, headerScope));
  ZC_EXPECT(OwnerBodyScopeProjection::verify(owner, syntax, headerScope, projection.scopes(),
                                             projection.nodeScopes()));
  ZC_EXPECT(OwnerBodyScopeProjection::from(
                owner.clone(), syntax, StableScopeOwnerKey::module(module.clone())) == zc::none);
}

ZC_TEST("OwnerBodyQueryTest.ScopeProjectionRejectsForeignAndWrongOwnerRoots") {
  zc::Vector<DetachedModuleBodyNode> nodes;
  auto syntax = require(ModuleBodySyntax::from(0, zc::mv(nodes)));
  auto module = tests::test_identity_detail::module();
  auto moduleOwner = require(
      StableOwnerBodyQueryKey::from(module.clone(), StableBodyOwnerKey::module(module.clone())));
  auto foreignRoot = StableScopeOwnerKey::module(foreignModuleKey());
  ZC_EXPECT(OwnerBodyScopeProjection::from(moduleOwner.clone(), syntax, foreignRoot) == zc::none);
  ZC_EXPECT(!OwnerBodyScopeProjection::verify(moduleOwner, syntax,
                                              StableScopeOwnerKey::module(foreignModuleKey()),
                                              CanonicalSequence<StableBodyScopeFact>::empty(),
                                              CanonicalSequence<StableBodyNodeScopeFact>::empty()));

  auto definition = foreignDefinitionKey();
  auto definitionOwner = require(StableOwnerBodyQueryKey::from(
      module.clone(), StableBodyOwnerKey::definition(definition.clone())));
  auto wrongDefinitionScope = require(StableScopeOwnerKey::definition(
      StableDefinitionQueryKey::from(module.clone(), otherDefinitionKey()), ScopeRole::Parameters));
  ZC_EXPECT(OwnerBodyScopeProjection::from(definitionOwner.clone(), syntax, wrongDefinitionScope) ==
            zc::none);
  ZC_EXPECT(!OwnerBodyScopeProjection::verify(
      definitionOwner, syntax,
      require(StableScopeOwnerKey::definition(
          StableDefinitionQueryKey::from(module.clone(), otherDefinitionKey()),
          ScopeRole::Parameters)),
      CanonicalSequence<StableBodyScopeFact>::empty(),
      CanonicalSequence<StableBodyNodeScopeFact>::empty()));
}

ZC_TEST("OwnerBodyQueryTest.ScopeProjectionSelectsFunctionParameterScopeFromSkeleton") {
  zc::Vector<DetachedModuleBodyNode> nodes;
  nodes.add(
      require(DetachedModuleBodyNode::syntax(ast::SyntaxKind::BlockStmt, blockPayload(1), 1)));
  nodes.add(require(
      DetachedModuleBodyNode::syntax(ast::SyntaxKind::EmptyStatement, emptyStatementPayload(), 0)));
  auto syntax = require(ModuleBodySyntax::from(1, zc::mv(nodes)));
  auto fixture = definitionBodySkeleton(identity::DefinitionKind::Function);

  auto projection =
      require(OwnerBodyScopeProjection::fromSkeleton(fixture.owner, syntax, fixture.skeleton));
  ZC_EXPECT(OwnerBodyScopeProjection::verifyFromSkeleton(
      fixture.owner, syntax, fixture.skeleton, projection.scopes(), projection.nodeScopes()));
  ZC_REQUIRE(projection.scopes().values().size() == 1);
  ZC_EXPECT(projection.scopes().values()[0].parent() == fixture.rootScope);
  auto foreignOwner = require(StableOwnerBodyQueryKey::from(
      fixture.owner.module().clone(), StableBodyOwnerKey::definition(foreignDefinitionKey())));
  ZC_EXPECT(OwnerBodyScopeProjection::fromSkeleton(foreignOwner, syntax, fixture.skeleton) ==
            zc::none);
}

ZC_TEST("OwnerBodyQueryTest.ScopeProjectionSelectsFieldDeclarationScopeFromSkeleton") {
  zc::Vector<DetachedModuleBodyNode> nodes;
  nodes.add(
      require(DetachedModuleBodyNode::syntax(ast::SyntaxKind::BlockStmt, blockPayload(1), 1)));
  nodes.add(require(
      DetachedModuleBodyNode::syntax(ast::SyntaxKind::EmptyStatement, emptyStatementPayload(), 0)));
  auto syntax = require(ModuleBodySyntax::from(1, zc::mv(nodes)));
  auto fixture = definitionBodySkeleton(identity::DefinitionKind::Field);

  auto projection =
      require(OwnerBodyScopeProjection::fromSkeleton(fixture.owner, syntax, fixture.skeleton));
  ZC_EXPECT(OwnerBodyScopeProjection::verifyFromSkeleton(
      fixture.owner, syntax, fixture.skeleton, projection.scopes(), projection.nodeScopes()));
  ZC_REQUIRE(projection.scopes().values().size() == 1);
  ZC_EXPECT(projection.scopes().values()[0].parent() == fixture.rootScope);
}

ZC_TEST("OwnerBodyQueryTest.ScopeProjectionVerifierRejectsTamperedFactDomains") {
  zc::Vector<DetachedModuleBodyNode> nodes;
  nodes.add(
      require(DetachedModuleBodyNode::syntax(ast::SyntaxKind::BlockStmt, blockPayload(1), 1)));
  nodes.add(require(
      DetachedModuleBodyNode::syntax(ast::SyntaxKind::EmptyStatement, emptyStatementPayload(), 0)));
  auto syntax = require(ModuleBodySyntax::from(1, zc::mv(nodes)));
  auto module = tests::test_identity_detail::module();
  auto owner = require(
      StableOwnerBodyQueryKey::from(module.clone(), StableBodyOwnerKey::module(module.clone())));
  auto rootScope = StableScopeOwnerKey::module(module.clone());
  auto projection = require(OwnerBodyScopeProjection::from(owner.clone(), syntax, rootScope));

  ZC_EXPECT(!OwnerBodyScopeProjection::verify(owner.clone(), syntax, rootScope.clone(),
                                              CanonicalSequence<StableBodyScopeFact>::empty(),
                                              projection.nodeScopes()));
  ZC_EXPECT(!OwnerBodyScopeProjection::verify(owner, syntax, rootScope, projection.scopes(),
                                              CanonicalSequence<StableBodyNodeScopeFact>::empty()));
}

ZC_TEST("OwnerBodyQueryTest.LookupVerifierRejectsMissingResolutionWitness") {
  zc::Vector<DetachedModuleBodyNode> nodes;
  nodes.add(require(
      DetachedModuleBodyNode::syntax(ast::SyntaxKind::IdentExpr, identExprPayload("body"_zc), 0)));
  auto syntax = require(ModuleBodySyntax::from(1, zc::mv(nodes)));
  auto fixture = definitionBodySkeleton(identity::DefinitionKind::Field);
  auto scopes =
      require(OwnerBodyScopeProjection::fromSkeleton(fixture.owner, syntax, fixture.skeleton));
  auto lookups = require(OwnerBodyLookupProjection::from(
      fixture.owner, syntax, fixture.skeleton, scopes.scopes(), scopes.nodeScopes(),
      CanonicalSequence<StableOwnerLocalBindingFact>::empty()));

  ZC_REQUIRE(lookups.resolutions().values().size() == 1);
  ZC_REQUIRE(lookups.failedLookups().values().size() == 0);
  ZC_EXPECT(OwnerBodyLookupProjection::verify(
      fixture.owner, syntax, fixture.skeleton, scopes.scopes(), scopes.nodeScopes(),
      CanonicalSequence<StableOwnerLocalBindingFact>::empty(), lookups.resolutions(),
      lookups.failedLookups()));
  ZC_EXPECT(!OwnerBodyLookupProjection::verify(
      fixture.owner, syntax, fixture.skeleton, scopes.scopes(), scopes.nodeScopes(),
      CanonicalSequence<StableOwnerLocalBindingFact>::empty(),
      CanonicalSequence<StableResolutionFact>::empty(), lookups.failedLookups()));
}

ZC_TEST("OwnerBodyQueryTest.ReceiverVerifierRejectsMissingThisWitness") {
  zc::Vector<DetachedModuleBodyNode> nodes;
  nodes.add(require(
      DetachedModuleBodyNode::syntax(ast::SyntaxKind::ThisExpr, emptyStatementPayload(), 0)));
  auto syntax = require(ModuleBodySyntax::from(1, zc::mv(nodes)));
  auto fixture = definitionBodySkeleton(identity::DefinitionKind::Function);
  auto receivers =
      require(OwnerBodyReceiverProjection::from(fixture.owner, syntax, fixture.skeleton));

  ZC_REQUIRE(receivers.bindings().values().size() == 1);
  ZC_EXPECT(OwnerBodyReceiverProjection::verify(fixture.owner, syntax, fixture.skeleton,
                                                receivers.bindings()));
  ZC_EXPECT(!OwnerBodyReceiverProjection::verify(
      fixture.owner, syntax, fixture.skeleton, CanonicalSequence<StableThisBindingFact>::empty()));
}

}  // namespace zomlang::compiler::binder
