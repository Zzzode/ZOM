// Copyright (c) 2025 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and limitations under
// the License.

#include "zomlang/compiler/checker/trait-resolver.h"

#include "zc/ztest/test.h"
#include "zomlang/compiler/binder/binder.h"
#include "zomlang/compiler/diagnostics/diagnostic-consumer.h"
#include "zomlang/compiler/diagnostics/diagnostic.h"
#include "zomlang/compiler/type/named-type.h"
#include "zomlang/compiler/type/object-type.h"
#include "zomlang/compiler/type/primitive-type.h"
#include "zomlang/compiler/type/raw-pointer-type.h"
#include "zomlang/compiler/type/type-env.h"
#include "zomlang/tests/unittests/compiler/test-ast-builder.h"

namespace zomlang {
namespace compiler {
namespace checker {

using tests::TestFixture;

namespace {

class CapturingDiagnosticConsumer final : public diagnostics::DiagnosticConsumer {
public:
  zc::Vector<diagnostics::DiagID> ids;

  void handleDiagnostic(const source::SourceManager&,
                        const diagnostics::Diagnostic& diagnostic) override {
    ids.add(diagnostic.getId());
  }
};

bool containsDiagnosticId(const CapturingDiagnosticConsumer& consumer, diagnostics::DiagID id) {
  for (auto emitted : consumer.ids) {
    if (emitted == id) return true;
  }
  return false;
}

ast::NodeId makeAssociatedTypeDecl(TestFixture& fix, zc::StringPtr name, ast::NodeId defaultTy) {
  ast::NodePayload payload;
  auto nameId = fix.builder().internIdent(name);
  payload.words[ast::kAssociatedTypeDeclNameWord] = nameId.value;
  payload.words[ast::kAssociatedTypeDeclTypeParamsIdWord] = 0;
  payload.words[ast::kAssociatedTypeDeclBoundWord] = 0;
  payload.words[ast::kAssociatedTypeDeclDefaultTyWord] = defaultTy.value;
  return fix.builder().makeNode(ast::SyntaxKind::AssociatedTypeDecl, source::SourceRange(),
                                payload);
}

ast::NodeId makeClassMemberList(TestFixture& fix, ast::NodeList members) {
  ast::NodePayload payload;
  payload.words[ast::kClassMemberListNmembersWord] = members.size;
  payload.words[ast::kClassMemberListMembersFirstWord] = members.first;
  payload.words[ast::kClassMemberListMembersSizeWord] = members.size;
  return fix.builder().makeNode(ast::SyntaxKind::ClassMemberList, source::SourceRange(), payload);
}

ast::NodeId makeClassDecl(TestFixture& fix, zc::StringPtr name, ast::NodeId members,
                          ast::NodeId typeParams = ast::NodeId()) {
  ast::NodePayload payload;
  auto nameId = fix.builder().internIdent(name);
  payload.words[ast::kClassDeclNameWord] = nameId.value;
  payload.words[ast::kClassDeclTypeParamsIdWord] = typeParams.value;
  payload.words[ast::kClassDeclBaseTyWord] = 0;
  payload.words[ast::kClassDeclMembersIdWord] = members.value;
  return fix.builder().makeNode(ast::SyntaxKind::ClassDecl, source::SourceRange(), payload);
}

ast::NodeId makeStandaloneImplDecl(TestFixture& fix, ast::NodeId forTy, ast::NodeId members) {
  ast::NodePayload payload;
  payload.words[ast::kStandaloneImplDeclIsUnsafeWord] = 0;
  payload.words[ast::kStandaloneImplDeclIfacesIdWord] = 0;
  payload.words[ast::kStandaloneImplDeclForTyWord] = forTy.value;
  payload.words[ast::kStandaloneImplDeclWhereWord] = 0;
  payload.words[ast::kStandaloneImplDeclTypeParamsIdWord] = 0;
  payload.words[ast::kStandaloneImplDeclMembersIdWord] = members.value;
  return fix.builder().makeNode(ast::SyntaxKind::StandaloneImplDecl, source::SourceRange(),
                                payload);
}

ast::NodeId makeImplIfaceList(TestFixture& fix, ast::NodeList ifaces) {
  ast::NodePayload payload;
  payload.words[ast::kImplIfaceListNIfacesWord] = ifaces.size;
  payload.words[ast::kImplIfaceListIfacesFirstWord] = ifaces.first;
  payload.words[ast::kImplIfaceListIfacesSizeWord] = ifaces.size;
  return fix.builder().makeNode(ast::SyntaxKind::ImplIfaceList, source::SourceRange(), payload);
}

ast::NodeId makeStandaloneImplDecl(TestFixture& fix, ast::NodeId forTy, ast::NodeId ifaces,
                                   ast::NodeId members) {
  ast::NodePayload payload;
  payload.words[ast::kStandaloneImplDeclIsUnsafeWord] = 0;
  payload.words[ast::kStandaloneImplDeclIfacesIdWord] = ifaces.value;
  payload.words[ast::kStandaloneImplDeclForTyWord] = forTy.value;
  payload.words[ast::kStandaloneImplDeclWhereWord] = 0;
  payload.words[ast::kStandaloneImplDeclTypeParamsIdWord] = 0;
  payload.words[ast::kStandaloneImplDeclMembersIdWord] = members.value;
  return fix.builder().makeNode(ast::SyntaxKind::StandaloneImplDecl, source::SourceRange(),
                                payload);
}

ast::NodeId makeStandaloneImplDecl(TestFixture& fix, ast::NodeId forTy, ast::NodeId ifaces,
                                   ast::NodeId members, ast::NodeId typeParams) {
  ast::NodePayload payload;
  payload.words[ast::kStandaloneImplDeclIsUnsafeWord] = 0;
  payload.words[ast::kStandaloneImplDeclIfacesIdWord] = ifaces.value;
  payload.words[ast::kStandaloneImplDeclForTyWord] = forTy.value;
  payload.words[ast::kStandaloneImplDeclWhereWord] = 0;
  payload.words[ast::kStandaloneImplDeclTypeParamsIdWord] = typeParams.value;
  payload.words[ast::kStandaloneImplDeclMembersIdWord] = members.value;
  return fix.builder().makeNode(ast::SyntaxKind::StandaloneImplDecl, source::SourceRange(),
                                payload);
}

ast::NodeId makeStandaloneImplDecl(TestFixture& fix, ast::NodeId forTy, ast::NodeId ifaces,
                                   ast::NodeId members, ast::NodeId typeParams,
                                   ast::NodeId whereClause) {
  ast::NodePayload payload;
  payload.words[ast::kStandaloneImplDeclIsUnsafeWord] = 0;
  payload.words[ast::kStandaloneImplDeclIfacesIdWord] = ifaces.value;
  payload.words[ast::kStandaloneImplDeclForTyWord] = forTy.value;
  payload.words[ast::kStandaloneImplDeclWhereWord] = whereClause.value;
  payload.words[ast::kStandaloneImplDeclTypeParamsIdWord] = typeParams.value;
  payload.words[ast::kStandaloneImplDeclMembersIdWord] = members.value;
  return fix.builder().makeNode(ast::SyntaxKind::StandaloneImplDecl, source::SourceRange(),
                                payload);
}

ast::NodeId makeMarkerImpl(TestFixture& fix, zc::StringPtr markerName, ast::NodeId forTy,
                           bool isUnsafe = false, bool isNegated = false) {
  ast::NodePayload payload;
  payload.words[ast::kMarkerImplIsUnsafeWord] = isUnsafe ? 1 : 0;
  payload.words[ast::kMarkerImplIsNegatedWord] = isNegated ? 1 : 0;
  payload.words[ast::kMarkerImplMarkerPathWord] = fix.makeIdentExpr(markerName).value;
  payload.words[ast::kMarkerImplForTyWord] = forTy.value;
  payload.words[ast::kMarkerImplWhereWord] = 0;
  payload.words[ast::kMarkerImplTypeParamsIdWord] = 0;
  return fix.builder().makeNode(ast::SyntaxKind::MarkerImpl, source::SourceRange(), payload);
}

ast::NodeId makeMarkerImpl(TestFixture& fix, zc::StringPtr markerName, ast::NodeId forTy,
                           ast::NodeId typeParams, ast::NodeId whereClause, bool isUnsafe = false,
                           bool isNegated = false) {
  ast::NodePayload payload;
  payload.words[ast::kMarkerImplIsUnsafeWord] = isUnsafe ? 1 : 0;
  payload.words[ast::kMarkerImplIsNegatedWord] = isNegated ? 1 : 0;
  payload.words[ast::kMarkerImplMarkerPathWord] = fix.makeIdentExpr(markerName).value;
  payload.words[ast::kMarkerImplForTyWord] = forTy.value;
  payload.words[ast::kMarkerImplWhereWord] = whereClause.value;
  payload.words[ast::kMarkerImplTypeParamsIdWord] = typeParams.value;
  return fix.builder().makeNode(ast::SyntaxKind::MarkerImpl, source::SourceRange(), payload);
}

ast::NodeId makeAttributePath(TestFixture& fix, zc::StringPtr name) {
  zc::Vector<ast::IdentId> segments;
  segments.add(fix.builder().internIdent(name));
  auto segmentList = fix.builder().makeIdentList(segments.asPtr());

  ast::NodePayload payload;
  payload.words[ast::kAttributePathSegmentsFirstWord] = segmentList.first;
  payload.words[ast::kAttributePathSegmentsSizeWord] = segmentList.size;
  payload.words[ast::kAttributePathLeadingWord] = 0;
  return fix.builder().makeNode(ast::SyntaxKind::AttributePath, source::SourceRange(), payload);
}

ast::NodeId makeAttributePathMarkerImpl(TestFixture& fix, zc::StringPtr markerName,
                                        ast::NodeId forTy, bool isUnsafe = false,
                                        bool isNegated = false) {
  ast::NodePayload payload;
  payload.words[ast::kMarkerImplIsUnsafeWord] = isUnsafe ? 1 : 0;
  payload.words[ast::kMarkerImplIsNegatedWord] = isNegated ? 1 : 0;
  payload.words[ast::kMarkerImplMarkerPathWord] = makeAttributePath(fix, markerName).value;
  payload.words[ast::kMarkerImplForTyWord] = forTy.value;
  payload.words[ast::kMarkerImplWhereWord] = 0;
  payload.words[ast::kMarkerImplTypeParamsIdWord] = 0;
  return fix.builder().makeNode(ast::SyntaxKind::MarkerImpl, source::SourceRange(), payload);
}

struct ResolverFixture {
  TestFixture fix;
  type::TypeEnv typeEnv;
  ast::Tree tree;

  ResolverFixture() : tree(fix.buildSourceFile("test"_zc, {})) {}

  TraitResolver makeResolver() {
    return TraitResolver(typeEnv, fix.symbols(), tree, fix.metadata(), fix.diagnostics());
  }
};

}  // namespace

ZC_TEST("TraitResolver.PrimitivesImplementSendableAndShared") {
  ResolverFixture fixture;
  auto resolver = fixture.makeResolver();
  auto i32 = type::PrimitiveType::createI32();

  ZC_EXPECT(resolver.implements(*i32, "Sendable"_zc));
  ZC_EXPECT(resolver.implements(*i32, "Shared"_zc));
}

ZC_TEST("TraitResolver.LegacySendAndSyncAreNotBuiltinMarkers") {
  ResolverFixture fixture;
  auto resolver = fixture.makeResolver();
  auto i32 = type::PrimitiveType::createI32();

  ZC_EXPECT(!resolver.implements(*i32, "Send"_zc));
  ZC_EXPECT(!resolver.implements(*i32, "Sync"_zc));
}

ZC_TEST("TraitResolver.ObjectMarkerDerivationRequiresEveryMember") {
  ResolverFixture fixture;
  auto resolver = fixture.makeResolver();
  auto objectTy = zc::heap<type::ObjectType>();
  objectTy->addMember("value"_zc, type::PrimitiveType::createI32());

  ZC_EXPECT(resolver.implements(*objectTy, "Sendable"_zc));
  ZC_EXPECT(resolver.implements(*objectTy, "Shared"_zc));

  objectTy->addMember("raw"_zc, zc::heap<type::RawPointerType>(type::PrimitiveType::createI32(),
                                                               type::Mutability::Const));

  ZC_EXPECT(!resolver.implements(*objectTy, "Sendable"_zc));
  ZC_EXPECT(!resolver.implements(*objectTy, "Shared"_zc));
}

ZC_TEST("TraitResolver.NamedStructMarkerDerivationChecksFields") {
  TestFixture fix;

  auto safeField = fix.makeFieldDecl("value"_zc, fix.makeNamedTypeExpr("i32"_zc));
  zc::Vector<ast::NodeId> safeMembers;
  safeMembers.add(safeField);
  auto safeStruct = fix.makeStructDecl(
      "SafeBox"_zc, makeClassMemberList(fix, fix.makeNodeList(safeMembers.asPtr())));

  auto unsafeField =
      fix.makeFieldDecl("ptr"_zc, fix.makeRawPointerTypeExpr(fix.makeNamedTypeExpr("i32"_zc)));
  zc::Vector<ast::NodeId> unsafeMembers;
  unsafeMembers.add(unsafeField);
  auto unsafeStruct = fix.makeStructDecl(
      "UnsafeBox"_zc, makeClassMemberList(fix, fix.makeNodeList(unsafeMembers.asPtr())));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(safeStruct);
  topDecls.add(unsafeStruct);
  auto tree = fix.buildSourceFile("test"_zc, topDecls.asPtr());

  type::TypeEnv typeEnv;
  TraitResolver resolver(typeEnv, fix.symbols(), tree, fix.metadata(), fix.diagnostics());

  type::NamedType safe("SafeBox"_zc);
  type::NamedType unsafe("UnsafeBox"_zc);

  ZC_EXPECT(resolver.implements(safe, "Sendable"_zc));
  ZC_EXPECT(resolver.implements(safe, "Shared"_zc));
  ZC_EXPECT(!resolver.implements(unsafe, "Sendable"_zc));
  ZC_EXPECT(!resolver.implements(unsafe, "Shared"_zc));
}

ZC_TEST("TraitResolver.DuplicateImplReportsCoherenceError") {
  TestFixture fix;
  auto consumer = zc::heap<CapturingDiagnosticConsumer>();
  auto consumerPtr = consumer.get();
  fix.diagnostics().addConsumer(zc::mv(consumer));

  auto boxDecl = fix.makeClassDecl("Box"_zc);
  auto hashableDecl = fix.makeInterfaceDecl("Hashable"_zc);
  auto forTy = fix.makeNamedTypeExpr("Box"_zc);
  auto ifaceTy = fix.makeNamedTypeExpr("Hashable"_zc);
  zc::Vector<ast::NodeId> ifaceNodes;
  ifaceNodes.add(ifaceTy);
  auto ifaces = makeImplIfaceList(fix, fix.makeNodeList(ifaceNodes.asPtr()));
  auto members = makeClassMemberList(fix, ast::NodeList());
  auto implA = makeStandaloneImplDecl(fix, forTy, ifaces, members);
  auto implB = makeStandaloneImplDecl(fix, forTy, ifaces, members);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(boxDecl);
  topDecls.add(hashableDecl);
  topDecls.add(implA);
  topDecls.add(implB);
  auto tree = fix.buildSourceFile("test"_zc, topDecls.asPtr());

  binder::Binder binder(fix.symbols(), fix.diagnostics(), tree, fix.metadata());
  ZC_EXPECT(binder.bind());

  type::TypeEnv typeEnv;
  TraitResolver resolver(typeEnv, fix.symbols(), tree, fix.metadata(), fix.diagnostics());
  resolver.checkCoherence();

  ZC_EXPECT(fix.diagnostics().hasErrors());
  ZC_EXPECT(containsDiagnosticId(*consumerPtr, diagnostics::DiagID::ConflictingImpl));
}

ZC_TEST("TraitResolver.DirectAndBlanketImplOverlapReportsCoherenceError") {
  TestFixture fix;
  auto consumer = zc::heap<CapturingDiagnosticConsumer>();
  auto consumerPtr = consumer.get();
  fix.diagnostics().addConsumer(zc::mv(consumer));

  auto boxDecl = fix.makeClassDecl("Box"_zc);
  auto hashableDecl = fix.makeInterfaceDecl("Hashable"_zc);
  zc::Vector<ast::NodeId> ifaceNodes;
  ifaceNodes.add(fix.makeNamedTypeExpr("Hashable"_zc));
  auto ifaces = makeImplIfaceList(fix, fix.makeNodeList(ifaceNodes.asPtr()));
  auto members = makeClassMemberList(fix, ast::NodeList());

  auto concreteImpl = makeStandaloneImplDecl(fix, fix.makeNamedTypeExpr("Box"_zc), ifaces, members);
  auto genericParam = fix.makeGenericTypeParam("T"_zc);
  zc::Vector<ast::NodeId> genericNodes;
  genericNodes.add(genericParam);
  auto typeParams = fix.makeGenericParams(fix.makeNodeList(genericNodes.asPtr()));
  auto blanketImpl =
      makeStandaloneImplDecl(fix, fix.makeNamedTypeExpr("T"_zc), ifaces, members, typeParams);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(boxDecl);
  topDecls.add(hashableDecl);
  topDecls.add(concreteImpl);
  topDecls.add(blanketImpl);
  auto tree = fix.buildSourceFile("test"_zc, topDecls.asPtr());

  binder::Binder binder(fix.symbols(), fix.diagnostics(), tree, fix.metadata());
  ZC_EXPECT(binder.bind());

  type::TypeEnv typeEnv;
  TraitResolver resolver(typeEnv, fix.symbols(), tree, fix.metadata(), fix.diagnostics());
  resolver.checkCoherence();

  ZC_EXPECT(fix.diagnostics().hasErrors());
  ZC_EXPECT(containsDiagnosticId(*consumerPtr, diagnostics::DiagID::ConflictingImpl));
}

ZC_TEST("TraitResolver.GenericImplWhereBoundControlsImplementation") {
  TestFixture fix;

  auto eqDecl = fix.makeInterfaceDecl("Eq"_zc);
  auto goodDecl = fix.makeClassDecl("Good"_zc);
  auto plainDecl = fix.makeClassDecl("Plain"_zc);

  zc::Vector<ast::NodeId> boxMembers;
  boxMembers.add(fix.makeFieldDecl("value"_zc, fix.makeNamedTypeExpr("T"_zc)));
  auto boxGenericParam = fix.makeGenericTypeParam("T"_zc);
  zc::Vector<ast::NodeId> boxGenericNodes;
  boxGenericNodes.add(boxGenericParam);
  auto boxTypeParams = fix.makeGenericParams(fix.makeNodeList(boxGenericNodes.asPtr()));
  auto boxDecl = fix.makeClassDecl(
      "Box"_zc, makeClassMemberList(fix, fix.makeNodeList(boxMembers.asPtr())), boxTypeParams);

  zc::Vector<ast::NodeId> ifaceNodes;
  ifaceNodes.add(fix.makeNamedTypeExpr("Eq"_zc));
  auto ifaces = makeImplIfaceList(fix, fix.makeNodeList(ifaceNodes.asPtr()));
  auto emptyMembers = makeClassMemberList(fix, ast::NodeList());
  auto goodImpl =
      makeStandaloneImplDecl(fix, fix.makeNamedTypeExpr("Good"_zc), ifaces, emptyMembers);

  zc::Vector<ast::NodeId> boxTypeArgs;
  boxTypeArgs.add(fix.makeNamedTypeExpr("T"_zc));
  auto boxForTy = fix.makeNamedTypeExpr("Box"_zc, fix.makeNodeList(boxTypeArgs.asPtr()));
  auto wherePred = fix.makeWherePred(fix.makeNamedTypeExpr("T"_zc), fix.makeNamedTypeExpr("Eq"_zc));
  zc::Vector<ast::NodeId> wherePreds;
  wherePreds.add(wherePred);
  auto whereClause = fix.makeWhereClause(fix.makeNodeList(wherePreds.asPtr()));
  auto implGenericParam = fix.makeGenericTypeParam("T"_zc);
  zc::Vector<ast::NodeId> implGenericNodes;
  implGenericNodes.add(implGenericParam);
  auto implTypeParams =
      fix.makeGenericParams(fix.makeNodeList(implGenericNodes.asPtr()), whereClause);
  auto boxImpl =
      makeStandaloneImplDecl(fix, boxForTy, ifaces, emptyMembers, implTypeParams, whereClause);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(eqDecl);
  topDecls.add(goodDecl);
  topDecls.add(plainDecl);
  topDecls.add(boxDecl);
  topDecls.add(goodImpl);
  topDecls.add(boxImpl);
  auto tree = fix.buildSourceFile("test"_zc, topDecls.asPtr());

  binder::Binder binder(fix.symbols(), fix.diagnostics(), tree, fix.metadata());
  ZC_EXPECT(binder.bind());

  type::TypeEnv typeEnv;
  TraitResolver resolver(typeEnv, fix.symbols(), tree, fix.metadata(), fix.diagnostics());
  resolver.checkCoherence();
  resolver.discoverImpls();

  type::NamedType boxGood("Box"_zc);
  boxGood.addTypeArg(zc::heap<type::NamedType>("Good"_zc));
  type::NamedType boxPlain("Box"_zc);
  boxPlain.addTypeArg(zc::heap<type::NamedType>("Plain"_zc));

  ZC_EXPECT(resolver.implements(boxGood, "Eq"_zc));
  ZC_EXPECT(!resolver.implements(boxPlain, "Eq"_zc));
}

ZC_TEST("TraitResolver.ConcreteGenericImplDoesNotMatchAnotherSpecialization") {
  TestFixture fix;
  auto eqDecl = fix.makeInterfaceDecl("Eq"_zc);
  auto goodDecl = fix.makeClassDecl("Good"_zc);
  auto plainDecl = fix.makeClassDecl("Plain"_zc);
  auto boxDecl = fix.makeClassDecl("Box"_zc);

  zc::Vector<ast::NodeId> ifaceNodes;
  ifaceNodes.add(fix.makeNamedTypeExpr("Eq"_zc));
  auto ifaces = makeImplIfaceList(fix, fix.makeNodeList(ifaceNodes.asPtr()));
  zc::Vector<ast::NodeId> goodTypeArgs;
  goodTypeArgs.add(fix.makeNamedTypeExpr("Good"_zc));
  auto boxGoodType = fix.makeNamedTypeExpr("Box"_zc, fix.makeNodeList(goodTypeArgs.asPtr()));
  auto concreteImpl =
      makeStandaloneImplDecl(fix, boxGoodType, ifaces, makeClassMemberList(fix, ast::NodeList()));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(eqDecl);
  topDecls.add(goodDecl);
  topDecls.add(plainDecl);
  topDecls.add(boxDecl);
  topDecls.add(concreteImpl);
  auto tree = fix.buildSourceFile("test"_zc, topDecls.asPtr());

  binder::Binder binder(fix.symbols(), fix.diagnostics(), tree, fix.metadata());
  ZC_EXPECT(binder.bind());

  type::TypeEnv typeEnv;
  TraitResolver resolver(typeEnv, fix.symbols(), tree, fix.metadata(), fix.diagnostics());
  resolver.discoverImpls();

  type::NamedType boxGood("Box"_zc);
  boxGood.addTypeArg(zc::heap<type::NamedType>("Good"_zc));
  type::NamedType boxPlain("Box"_zc);
  boxPlain.addTypeArg(zc::heap<type::NamedType>("Plain"_zc));

  ZC_EXPECT(resolver.implements(boxGood, "Eq"_zc));
  ZC_EXPECT(!resolver.implements(boxPlain, "Eq"_zc));
}

ZC_TEST("TraitResolver.OrphanImplDiagnosticIsCrossModuleFallback") {
  TestFixture fix;
  auto consumer = zc::heap<CapturingDiagnosticConsumer>();
  auto consumerPtr = consumer.get();
  fix.diagnostics().addConsumer(zc::mv(consumer));

  zc::Vector<ast::NodeId> ifaceNodes;
  ifaceNodes.add(fix.makeNamedTypeExpr("ExternalIface"_zc));
  auto ifaces = makeImplIfaceList(fix, fix.makeNodeList(ifaceNodes.asPtr()));
  auto members = makeClassMemberList(fix, ast::NodeList());
  auto implDecl =
      makeStandaloneImplDecl(fix, fix.makeNamedTypeExpr("ExternalType"_zc), ifaces, members);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(implDecl);
  auto tree = fix.buildSourceFile("test"_zc, topDecls.asPtr());

  type::TypeEnv typeEnv;
  TraitResolver resolver(typeEnv, fix.symbols(), tree, fix.metadata(), fix.diagnostics());
  resolver.checkCoherence();

  ZC_EXPECT(fix.diagnostics().hasErrors());
  ZC_EXPECT(containsDiagnosticId(*consumerPtr, diagnostics::DiagID::OrphanImpl));
}

ZC_TEST("TraitResolver.NegativeMarkerImplSuppressesAutoDerivation") {
  TestFixture fix;
  auto safeField = fix.makeFieldDecl("value"_zc, fix.makeNamedTypeExpr("i32"_zc));
  zc::Vector<ast::NodeId> members;
  members.add(safeField);
  auto safeStruct =
      fix.makeStructDecl("SafeBox"_zc, makeClassMemberList(fix, fix.makeNodeList(members.asPtr())));
  auto negativeImpl =
      makeMarkerImpl(fix, "Sendable"_zc, fix.makeNamedTypeExpr("SafeBox"_zc), false, true);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(safeStruct);
  topDecls.add(negativeImpl);
  auto tree = fix.buildSourceFile("test"_zc, topDecls.asPtr());

  type::TypeEnv typeEnv;
  TraitResolver resolver(typeEnv, fix.symbols(), tree, fix.metadata(), fix.diagnostics());
  resolver.discoverImpls();

  type::NamedType safe("SafeBox"_zc);
  ZC_EXPECT(!resolver.implements(safe, "Sendable"_zc));
}

ZC_TEST("TraitResolver.AttributePathNegativeMarkerImplSuppressesAutoDerivation") {
  TestFixture fix;
  auto safeField = fix.makeFieldDecl("value"_zc, fix.makeNamedTypeExpr("i32"_zc));
  zc::Vector<ast::NodeId> members;
  members.add(safeField);
  auto safeStruct =
      fix.makeStructDecl("SafeBox"_zc, makeClassMemberList(fix, fix.makeNodeList(members.asPtr())));
  auto negativeImpl = makeAttributePathMarkerImpl(fix, "Sendable"_zc,
                                                  fix.makeNamedTypeExpr("SafeBox"_zc), false, true);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(safeStruct);
  topDecls.add(negativeImpl);
  auto tree = fix.buildSourceFile("test"_zc, topDecls.asPtr());

  type::TypeEnv typeEnv;
  TraitResolver resolver(typeEnv, fix.symbols(), tree, fix.metadata(), fix.diagnostics());
  resolver.discoverImpls();

  type::NamedType safe("SafeBox"_zc);
  ZC_EXPECT(!resolver.implements(safe, "Sendable"_zc));
}

ZC_TEST("TraitResolver.UnsafeMarkerImplOverridesStructuralRejection") {
  TestFixture fix;
  auto rawField =
      fix.makeFieldDecl("ptr"_zc, fix.makeRawPointerTypeExpr(fix.makeNamedTypeExpr("i32"_zc)));
  zc::Vector<ast::NodeId> members;
  members.add(rawField);
  auto wrapper = fix.makeStructDecl("RawWrapper"_zc,
                                    makeClassMemberList(fix, fix.makeNodeList(members.asPtr())));
  auto unsafeImpl =
      makeMarkerImpl(fix, "Sendable"_zc, fix.makeNamedTypeExpr("RawWrapper"_zc), true, false);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(wrapper);
  topDecls.add(unsafeImpl);
  auto tree = fix.buildSourceFile("test"_zc, topDecls.asPtr());

  type::TypeEnv typeEnv;
  TraitResolver resolver(typeEnv, fix.symbols(), tree, fix.metadata(), fix.diagnostics());
  resolver.discoverImpls();

  type::NamedType rawWrapper("RawWrapper"_zc);
  ZC_EXPECT(resolver.implements(rawWrapper, "Sendable"_zc));
}

ZC_TEST("TraitResolver.GenericMarkerImplWhereBoundControlsImplementation") {
  TestFixture fix;

  auto goodDecl = fix.makeClassDecl("Good"_zc);
  zc::Vector<ast::NodeId> unsafeMembers;
  unsafeMembers.add(
      fix.makeFieldDecl("ptr"_zc, fix.makeRawPointerTypeExpr(fix.makeNamedTypeExpr("i32"_zc))));
  auto unsafeDecl = makeClassDecl(
      fix, "Unsafe"_zc, makeClassMemberList(fix, fix.makeNodeList(unsafeMembers.asPtr())));

  zc::Vector<ast::NodeId> boxMembers;
  boxMembers.add(fix.makeFieldDecl("value"_zc, fix.makeNamedTypeExpr("T"_zc)));
  auto boxGenericParam = fix.makeGenericTypeParam("T"_zc);
  zc::Vector<ast::NodeId> boxGenericNodes;
  boxGenericNodes.add(boxGenericParam);
  auto boxTypeParams = fix.makeGenericParams(fix.makeNodeList(boxGenericNodes.asPtr()));
  auto boxDecl = makeClassDecl(
      fix, "Box"_zc, makeClassMemberList(fix, fix.makeNodeList(boxMembers.asPtr())), boxTypeParams);

  zc::Vector<ast::NodeId> boxTypeArgs;
  boxTypeArgs.add(fix.makeNamedTypeExpr("T"_zc));
  auto boxForTy = fix.makeNamedTypeExpr("Box"_zc, fix.makeNodeList(boxTypeArgs.asPtr()));
  auto wherePred =
      fix.makeWherePred(fix.makeNamedTypeExpr("T"_zc), fix.makeNamedTypeExpr("Sendable"_zc));
  zc::Vector<ast::NodeId> wherePreds;
  wherePreds.add(wherePred);
  auto whereClause = fix.makeWhereClause(fix.makeNodeList(wherePreds.asPtr()));
  auto implGenericParam = fix.makeGenericTypeParam("T"_zc);
  zc::Vector<ast::NodeId> implGenericNodes;
  implGenericNodes.add(implGenericParam);
  auto implTypeParams =
      fix.makeGenericParams(fix.makeNodeList(implGenericNodes.asPtr()), whereClause);
  auto markerImpl =
      makeMarkerImpl(fix, "Sendable"_zc, boxForTy, implTypeParams, whereClause, true, false);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(goodDecl);
  topDecls.add(unsafeDecl);
  topDecls.add(boxDecl);
  topDecls.add(markerImpl);
  auto tree = fix.buildSourceFile("test"_zc, topDecls.asPtr());

  type::TypeEnv typeEnv;
  TraitResolver resolver(typeEnv, fix.symbols(), tree, fix.metadata(), fix.diagnostics());
  resolver.discoverImpls();

  type::NamedType boxGood("Box"_zc);
  boxGood.addTypeArg(zc::heap<type::NamedType>("Good"_zc));
  type::NamedType boxUnsafe("Box"_zc);
  boxUnsafe.addTypeArg(zc::heap<type::NamedType>("Unsafe"_zc));

  ZC_EXPECT(resolver.implements(boxGood, "Sendable"_zc));
  ZC_EXPECT(!resolver.implements(boxUnsafe, "Sendable"_zc));
}

ZC_TEST("TraitResolver.ResolveAssociatedTypeRequiresUniqueBinding") {
  TestFixture fix;

  zc::Vector<ast::NodeId> members;
  members.add(makeAssociatedTypeDecl(fix, "Item"_zc, fix.makeNamedTypeExpr("i32"_zc)));
  auto memberList = makeClassMemberList(fix, fix.makeNodeList(members.asPtr()));
  auto implDecl = makeStandaloneImplDecl(fix, fix.makeNamedTypeExpr("Box"_zc), memberList);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(implDecl);
  auto tree = fix.buildSourceFile("test"_zc, topDecls.asPtr());

  type::TypeEnv typeEnv;
  TraitResolver resolver(typeEnv, fix.symbols(), tree, fix.metadata(), fix.diagnostics());
  type::NamedType box("Box"_zc);

  auto result = resolver.resolveAssociatedTypeWithStatus(box, "Item"_zc);
  ZC_EXPECT(result.kind == AssociatedTypeResolutionKind::Resolved);
  ZC_EXPECT(result.type != zc::none);
}

ZC_TEST("TraitResolver.ResolveAssociatedTypeWithInterfaceQualifier") {
  TestFixture fix;

  zc::Vector<ast::NodeId> iteratorIfaceNodes;
  iteratorIfaceNodes.add(fix.makeNamedTypeExpr("Iterator"_zc));
  auto iteratorIfaces = makeImplIfaceList(fix, fix.makeNodeList(iteratorIfaceNodes.asPtr()));
  zc::Vector<ast::NodeId> iteratorMembers;
  iteratorMembers.add(makeAssociatedTypeDecl(fix, "Item"_zc, fix.makeNamedTypeExpr("i32"_zc)));
  auto iteratorMemberList = makeClassMemberList(fix, fix.makeNodeList(iteratorMembers.asPtr()));
  auto iteratorImpl = makeStandaloneImplDecl(fix, fix.makeNamedTypeExpr("Box"_zc), iteratorIfaces,
                                             iteratorMemberList);

  zc::Vector<ast::NodeId> streamIfaceNodes;
  streamIfaceNodes.add(fix.makeNamedTypeExpr("Stream"_zc));
  auto streamIfaces = makeImplIfaceList(fix, fix.makeNodeList(streamIfaceNodes.asPtr()));
  zc::Vector<ast::NodeId> streamMembers;
  streamMembers.add(makeAssociatedTypeDecl(fix, "Item"_zc, fix.makeNamedTypeExpr("str"_zc)));
  auto streamMemberList = makeClassMemberList(fix, fix.makeNodeList(streamMembers.asPtr()));
  auto streamImpl =
      makeStandaloneImplDecl(fix, fix.makeNamedTypeExpr("Box"_zc), streamIfaces, streamMemberList);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(iteratorImpl);
  topDecls.add(streamImpl);
  auto tree = fix.buildSourceFile("test"_zc, topDecls.asPtr());

  type::TypeEnv typeEnv;
  TraitResolver resolver(typeEnv, fix.symbols(), tree, fix.metadata(), fix.diagnostics());
  type::NamedType box("Box"_zc);

  auto result = resolver.resolveAssociatedTypeWithStatus(box, "Iterator"_zc, "Item"_zc);
  ZC_EXPECT(result.kind == AssociatedTypeResolutionKind::Resolved);
  ZC_EXPECT(result.type != zc::none);
  ZC_IF_SOME(resolved, result.type) {
    ZC_EXPECT(isPrimitive(resolved));
    if (isPrimitive(resolved)) {
      auto& primitive = static_cast<const type::PrimitiveType&>(resolved);
      ZC_EXPECT(primitive.getPrimitiveKind() == type::PrimitiveKind::I32);
    }
  }
}

ZC_TEST("TraitResolver.ResolveAssociatedTypeReportsAmbiguousBinding") {
  TestFixture fix;

  zc::Vector<ast::NodeId> membersA;
  membersA.add(makeAssociatedTypeDecl(fix, "Item"_zc, fix.makeNamedTypeExpr("i32"_zc)));
  auto memberListA = makeClassMemberList(fix, fix.makeNodeList(membersA.asPtr()));
  auto implA = makeStandaloneImplDecl(fix, fix.makeNamedTypeExpr("Box"_zc), memberListA);

  zc::Vector<ast::NodeId> membersB;
  membersB.add(makeAssociatedTypeDecl(fix, "Item"_zc, fix.makeNamedTypeExpr("str"_zc)));
  auto memberListB = makeClassMemberList(fix, fix.makeNodeList(membersB.asPtr()));
  auto implB = makeStandaloneImplDecl(fix, fix.makeNamedTypeExpr("Box"_zc), memberListB);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(implA);
  topDecls.add(implB);
  auto tree = fix.buildSourceFile("test"_zc, topDecls.asPtr());

  type::TypeEnv typeEnv;
  TraitResolver resolver(typeEnv, fix.symbols(), tree, fix.metadata(), fix.diagnostics());
  type::NamedType box("Box"_zc);

  auto result = resolver.resolveAssociatedTypeWithStatus(box, "Item"_zc);
  ZC_EXPECT(result.kind == AssociatedTypeResolutionKind::Ambiguous);
  ZC_EXPECT(result.type == zc::none);
}

ZC_TEST("TraitResolver.ResolveAssociatedTypeEmitsAmbiguousDiagnostic") {
  TestFixture fix;

  zc::Vector<ast::NodeId> membersA;
  membersA.add(makeAssociatedTypeDecl(fix, "Item"_zc, fix.makeNamedTypeExpr("i32"_zc)));
  auto memberListA = makeClassMemberList(fix, fix.makeNodeList(membersA.asPtr()));
  auto implA = makeStandaloneImplDecl(fix, fix.makeNamedTypeExpr("Box"_zc), memberListA);

  zc::Vector<ast::NodeId> membersB;
  membersB.add(makeAssociatedTypeDecl(fix, "Item"_zc, fix.makeNamedTypeExpr("str"_zc)));
  auto memberListB = makeClassMemberList(fix, fix.makeNodeList(membersB.asPtr()));
  auto implB = makeStandaloneImplDecl(fix, fix.makeNamedTypeExpr("Box"_zc), memberListB);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(implA);
  topDecls.add(implB);
  auto tree = fix.buildSourceFile("test"_zc, topDecls.asPtr());

  type::TypeEnv typeEnv;
  TraitResolver resolver(typeEnv, fix.symbols(), tree, fix.metadata(), fix.diagnostics());
  type::NamedType box("Box"_zc);

  auto result = resolver.resolveAssociatedType(box, "Item"_zc);
  ZC_EXPECT(result == zc::none);
  ZC_EXPECT(fix.diagnostics().hasErrors());
}

ZC_TEST("TraitResolver.ResolveAssociatedTypeEmitsNotFoundDiagnostic") {
  TestFixture fix;
  zc::Vector<ast::NodeId> topDecls;
  auto tree = fix.buildSourceFile("test"_zc, topDecls.asPtr());

  type::TypeEnv typeEnv;
  TraitResolver resolver(typeEnv, fix.symbols(), tree, fix.metadata(), fix.diagnostics());
  type::NamedType box("Box"_zc);

  auto result = resolver.resolveAssociatedType(box, "Item"_zc);
  ZC_EXPECT(result == zc::none);
  ZC_EXPECT(fix.diagnostics().hasErrors());
}

}  // namespace checker
}  // namespace compiler
}  // namespace zomlang
