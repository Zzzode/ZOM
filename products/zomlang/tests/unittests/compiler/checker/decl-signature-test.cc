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

#include "zomlang/compiler/checker/decl-signature.h"

#include "zc/core/vector.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/ast/tree.h"
#include "zomlang/compiler/binder/binder.h"
#include "zomlang/compiler/diagnostics/diagnostic-consumer.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/diagnostics/diagnostic.h"
#include "zomlang/compiler/source/manager.h"
#include "zomlang/compiler/symbol/scope.h"
#include "zomlang/compiler/symbol/symbol-table.h"
#include "zomlang/compiler/type/array-type.h"
#include "zomlang/compiler/type/existential-type.h"
#include "zomlang/compiler/type/function-type.h"
#include "zomlang/compiler/type/intersection-type.h"
#include "zomlang/compiler/type/object-type.h"
#include "zomlang/compiler/type/primitive-type.h"
#include "zomlang/compiler/type/raw-pointer-type.h"
#include "zomlang/compiler/type/reference-type.h"
#include "zomlang/compiler/type/tuple-type.h"
#include "zomlang/compiler/type/type-env.h"
#include "zomlang/compiler/type/type-var.h"
#include "zomlang/compiler/type/type.h"
#include "zomlang/compiler/type/union-type.h"
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

ast::NodeId makeAttributePath(TestFixture& fix, zc::ArrayPtr<const zc::StringPtr> segments) {
  zc::Vector<ast::IdentId> names;
  for (zc::StringPtr segment : segments) { names.add(fix.builder().internIdent(segment)); }
  ast::NodePayload payload;
  auto identList = fix.builder().makeIdentList(names.asPtr());
  payload.words[ast::kAttributePathSegmentsFirstWord] = identList.first;
  payload.words[ast::kAttributePathSegmentsSizeWord] = identList.size;
  payload.words[ast::kAttributePathLeadingWord] = 0;
  return fix.builder().makeNode(ast::SyntaxKind::AttributePath, source::SourceRange(), payload);
}

ast::NodeId makeAttribute(TestFixture& fix, ast::NodeId path) {
  ast::NodePayload payload;
  payload.words[ast::kAttributePathWord] = path.value;
  payload.words[ast::kAttributeArgsFirstWord] = 0;
  payload.words[ast::kAttributeArgsSizeWord] = 0;
  return fix.builder().makeNode(ast::SyntaxKind::Attribute, source::SourceRange(), payload);
}

ast::NodeId makeAttributeList(TestFixture& fix, ast::NodeList attrs) {
  ast::NodePayload payload;
  payload.words[ast::kAttributeListAttrsFirstWord] = attrs.first;
  payload.words[ast::kAttributeListAttrsSizeWord] = attrs.size;
  return fix.builder().makeNode(ast::SyntaxKind::AttributeList, source::SourceRange(), payload);
}

ast::NodeId makeMoveParamAttributeList(TestFixture& fix) {
  zc::Vector<zc::StringPtr> pathSegments;
  pathSegments.add("zom"_zc);
  pathSegments.add("param"_zc);
  pathSegments.add("move"_zc);
  auto path = makeAttributePath(fix, pathSegments.asPtr());

  zc::Vector<ast::NodeId> attrs;
  attrs.add(makeAttribute(fix, path));
  return makeAttributeList(fix, fix.makeNodeList(attrs.asPtr()));
}

ast::NodeId makeFunctionParamDecl(TestFixture& fix, zc::StringPtr name, ast::NodeId ty,
                                  ast::NodeId attrs) {
  ast::NodePayload payload;
  payload.words[ast::kFunctionParameterDeclNameWord] = fix.builder().internIdent(name).value;
  payload.words[ast::kFunctionParameterDeclTyWord] = ty.value;
  payload.words[ast::kFunctionParameterDeclDefaultWord] = 0;
  payload.words[ast::kFunctionParameterDeclAttrsWord] = attrs.value;
  return fix.builder().makeNode(ast::SyntaxKind::FunctionParameterDecl, source::SourceRange(),
                                payload);
}

// Helper: run Binder then DeclSignatureComputer, return TypeEnv.
type::TypeEnv computeSignatures(TestFixture& fix, zc::ArrayPtr<const ast::NodeId> decls) {
  auto tree = fix.buildSourceFile("test"_zc, decls);

  // Phase 1+2: Binder
  binder::Binder binder(fix.symbols(), fix.diagnostics(), tree, fix.metadata());
  binder.bind();

  // Phase A: DeclSignatureComputer
  type::TypeEnv typeEnv;
  DeclSignatureComputer sigComputer(typeEnv, fix.symbols(), tree, fix.metadata(),
                                    fix.diagnostics());
  sigComputer.computeSignatures();

  return typeEnv;
}

ast::NodeId makeAssociatedTypeDecl(TestFixture& fix, zc::StringPtr name) {
  ast::NodePayload payload;
  payload.words[ast::kAssociatedTypeDeclNameWord] = fix.builder().internIdent(name).value;
  payload.words[ast::kAssociatedTypeDeclTypeParamsIdWord] = 0;
  payload.words[ast::kAssociatedTypeDeclBoundWord] = 0;
  payload.words[ast::kAssociatedTypeDeclDefaultTyWord] = 0;
  return fix.builder().makeNode(ast::SyntaxKind::AssociatedTypeDecl, source::SourceRange(),
                                payload);
}

ast::NodeId makeGenericAssociatedTypeDecl(TestFixture& fix, zc::StringPtr name,
                                          ast::NodeId typeParams) {
  ast::NodePayload payload;
  payload.words[ast::kAssociatedTypeDeclNameWord] = fix.builder().internIdent(name).value;
  payload.words[ast::kAssociatedTypeDeclTypeParamsIdWord] = typeParams.value;
  payload.words[ast::kAssociatedTypeDeclBoundWord] = 0;
  payload.words[ast::kAssociatedTypeDeclDefaultTyWord] = 0;
  return fix.builder().makeNode(ast::SyntaxKind::AssociatedTypeDecl, source::SourceRange(),
                                payload);
}

ast::NodeId makeAssociatedTypeBinding(TestFixture& fix, zc::StringPtr name, ast::NodeId defaultTy) {
  ast::NodePayload payload;
  payload.words[ast::kAssociatedTypeDeclNameWord] = fix.builder().internIdent(name).value;
  payload.words[ast::kAssociatedTypeDeclTypeParamsIdWord] = 0;
  payload.words[ast::kAssociatedTypeDeclBoundWord] = 0;
  payload.words[ast::kAssociatedTypeDeclDefaultTyWord] = defaultTy.value;
  return fix.builder().makeNode(ast::SyntaxKind::AssociatedTypeDecl, source::SourceRange(),
                                payload);
}

ast::NodeId makeDynTypeWithAssocBinding(TestFixture& fix, zc::StringPtr ifaceName,
                                        zc::StringPtr assocName, ast::NodeId bindingTy) {
  ast::NodePayload bindingPayload;
  bindingPayload.words[ast::kDynTypeAssocBindingNameWord] =
      fix.builder().internIdent(assocName).value;
  bindingPayload.words[ast::kDynTypeAssocBindingTyWord] = bindingTy.value;
  auto binding = fix.builder().makeNode(ast::SyntaxKind::DynTypeAssocBinding, source::SourceRange(),
                                        bindingPayload);
  zc::Vector<ast::NodeId> bindings;
  bindings.add(binding);
  auto bindingNodeList = fix.makeNodeList(bindings.asPtr());
  ast::NodePayload bindingListPayload;
  bindingListPayload.words[ast::kDynTypeAssocBindingListNBindingsWord] = bindings.size();
  bindingListPayload.words[ast::kDynTypeAssocBindingListBindingsFirstWord] = bindingNodeList.first;
  bindingListPayload.words[ast::kDynTypeAssocBindingListBindingsSizeWord] = bindingNodeList.size;
  auto bindingList = fix.builder().makeNode(ast::SyntaxKind::DynTypeAssocBindingList,
                                            source::SourceRange(), bindingListPayload);

  zc::Vector<ast::NodeId> ifaces;
  ifaces.add(fix.makeNamedTypeExpr(ifaceName));
  auto ifaceNodeList = fix.makeNodeList(ifaces.asPtr());
  ast::NodePayload ifaceListPayload;
  ifaceListPayload.words[ast::kDynTypeIfaceListNIfacesWord] = ifaces.size();
  ifaceListPayload.words[ast::kDynTypeIfaceListIfacesFirstWord] = ifaceNodeList.first;
  ifaceListPayload.words[ast::kDynTypeIfaceListIfacesSizeWord] = ifaceNodeList.size;
  auto ifaceList = fix.builder().makeNode(ast::SyntaxKind::DynTypeIfaceList, source::SourceRange(),
                                          ifaceListPayload);

  ast::NodePayload dynPayload;
  dynPayload.words[ast::kDynTypeExprIfacesIdWord] = ifaceList.value;
  dynPayload.words[ast::kDynTypeExprMarkersIdWord] = 0;
  dynPayload.words[ast::kDynTypeExprAssocBindingsIdWord] = bindingList.value;
  dynPayload.words[ast::kDynTypeExprHasLifetimeWord] = 0;
  dynPayload.words[ast::kDynTypeExprLifetimeWord] = 0;
  return fix.builder().makeNode(ast::SyntaxKind::DynTypeExpr, source::SourceRange(), dynPayload);
}

ast::NodeId makeImplIfaceList(TestFixture& fix, ast::NodeList ifaces) {
  ast::NodePayload payload;
  payload.words[ast::kImplIfaceListNIfacesWord] = ifaces.size;
  payload.words[ast::kImplIfaceListIfacesFirstWord] = ifaces.first;
  payload.words[ast::kImplIfaceListIfacesSizeWord] = ifaces.size;
  return fix.builder().makeNode(ast::SyntaxKind::ImplIfaceList, source::SourceRange(), payload);
}

ast::NodeId makeModulePathNamedTypeExpr(TestFixture& fix, zc::StringPtr name) {
  zc::Vector<ast::IdentId> segments;
  segments.add(fix.builder().internIdent(name));
  auto segmentList = fix.builder().makeIdentList(segments.asPtr());

  ast::NodePayload pathPayload;
  pathPayload.words[ast::kModulePathSegmentsFirstWord] = segmentList.first;
  pathPayload.words[ast::kModulePathSegmentsSizeWord] = segmentList.size;
  auto path =
      fix.builder().makeNode(ast::SyntaxKind::ModulePath, source::SourceRange(), pathPayload);

  ast::NodePayload payload;
  payload.words[ast::kNamedTypeExprPathWord] = path.value;
  payload.words[ast::kNamedTypeExprArgsFirstWord] = 0;
  payload.words[ast::kNamedTypeExprArgsSizeWord] = 0;
  return fix.builder().makeNode(ast::SyntaxKind::NamedTypeExpr, source::SourceRange(), payload);
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

const type::Type& aliasType(type::TypeEnv& typeEnv, ast::NodeId alias) {
  ZC_EXPECT(typeEnv.hasType(alias));
  return typeEnv.getType(alias);
}

}  // namespace

// ============================================================================
// Function signature computation
// ============================================================================

ZC_TEST("DeclSignature.FunctionDeclHasSignature") {
  TestFixture fix;
  auto fn = fix.makeFunctionDecl("foo"_zc);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(fn);
  auto typeEnv = computeSignatures(fix, topDecls.asPtr());

  // The function declaration should have a type assigned
  ZC_EXPECT(typeEnv.hasType(fn));
  auto& ty = typeEnv.getType(fn);
  ZC_EXPECT(isFunction(ty));
}

ZC_TEST("DeclSignature.MethodPreservesRaisesType") {
  TestFixture fix;
  auto raisesTy = fix.makeNamedTypeExpr("Failure"_zc);
  auto method =
      fix.makeMethodDecl("run"_zc, ast::NodeId(), ast::NodeId(), fix.makeNamedTypeExpr("unit"_zc),
                         false, ast::NodeId(), raisesTy);
  zc::Vector<ast::NodeId> members;
  members.add(method);
  auto iface = fix.makeInterfaceDecl("Runner"_zc,
                                     fix.makeClassMemberList(fix.makeNodeList(members.asPtr())));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(iface);
  auto typeEnv = computeSignatures(fix, topDecls.asPtr());

  ZC_EXPECT(typeEnv.hasType(method));
  const auto& ty = typeEnv.getType(method);
  ZC_EXPECT(isFunction(ty));
  if (isFunction(ty)) {
    const auto& fnTy = static_cast<const type::FunctionType&>(ty);
    ZC_EXPECT(fnTy.getRaisesType() != zc::none);
  }
}

ZC_TEST("DeclSignature.FunctionWithParamsHasParamTypes") {
  TestFixture fix;
  auto paramA = fix.makeFunctionParamDecl("a"_zc);
  auto paramB = fix.makeFunctionParamDecl("b"_zc);
  zc::Vector<ast::NodeId> paramNodes;
  paramNodes.add(paramA);
  paramNodes.add(paramB);
  auto paramList = fix.makeFunctionParamList(fix.makeNodeList(paramNodes.asPtr()));

  auto fn = fix.makeFunctionDecl("add"_zc, ast::NodeId(), paramList);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(fn);
  auto typeEnv = computeSignatures(fix, topDecls.asPtr());

  ZC_EXPECT(typeEnv.hasType(fn));
  auto& ty = typeEnv.getType(fn);
  ZC_EXPECT(isFunction(ty));

  auto& fnTy = static_cast<const type::FunctionType&>(ty);
  ZC_EXPECT(fnTy.getParamCount() == 2);
}

ZC_TEST("DeclSignature.FunctionGenericParamPreservesBound") {
  TestFixture fix;
  auto iface = fix.makeInterfaceDecl("Hashable"_zc);
  auto generic = fix.makeGenericTypeParam("T"_zc, fix.makeNamedTypeExpr("Hashable"_zc));
  zc::Vector<ast::NodeId> genericNodes;
  genericNodes.add(generic);
  auto generics = fix.makeGenericParams(fix.makeNodeList(genericNodes.asPtr()));

  auto param = fix.makeFunctionParamDecl("value"_zc, fix.makeNamedTypeExpr("T"_zc));
  zc::Vector<ast::NodeId> paramNodes;
  paramNodes.add(param);
  auto paramList = fix.makeFunctionParamList(fix.makeNodeList(paramNodes.asPtr()));
  auto fn = fix.makeFunctionDecl("f"_zc, ast::NodeId(), paramList, fix.makeNamedTypeExpr("unit"_zc),
                                 ast::NodeId(), generics);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(iface);
  topDecls.add(fn);
  auto typeEnv = computeSignatures(fix, topDecls.asPtr());

  ZC_EXPECT(typeEnv.hasType(fn));
  auto& ty = typeEnv.getType(fn);
  ZC_EXPECT(isFunction(ty));
  if (isFunction(ty)) {
    auto& fnTy = static_cast<const type::FunctionType&>(ty);
    ZC_EXPECT(fnTy.getGenericParamCount() == 1);
    if (fnTy.getGenericParamCount() == 1) {
      ZC_EXPECT(fnTy.getGenericParam(0).upperBounds.size() == 1);
    }
  }
}

ZC_TEST("DeclSignature.FunctionGenericParamPreservesWhereBound") {
  TestFixture fix;
  auto iface = fix.makeInterfaceDecl("Hashable"_zc);
  auto generic = fix.makeGenericTypeParam("T"_zc);
  auto wherePred =
      fix.makeWherePred(fix.makeNamedTypeExpr("T"_zc), fix.makeNamedTypeExpr("Hashable"_zc));
  zc::Vector<ast::NodeId> wherePreds;
  wherePreds.add(wherePred);
  auto whereClause = fix.makeWhereClause(fix.makeNodeList(wherePreds.asPtr()));
  zc::Vector<ast::NodeId> genericNodes;
  genericNodes.add(generic);
  auto generics = fix.makeGenericParams(fix.makeNodeList(genericNodes.asPtr()), whereClause);

  auto param = fix.makeFunctionParamDecl("value"_zc, fix.makeNamedTypeExpr("T"_zc));
  zc::Vector<ast::NodeId> paramNodes;
  paramNodes.add(param);
  auto paramList = fix.makeFunctionParamList(fix.makeNodeList(paramNodes.asPtr()));
  auto fn = fix.makeFunctionDecl("f"_zc, ast::NodeId(), paramList, fix.makeNamedTypeExpr("unit"_zc),
                                 ast::NodeId(), generics);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(iface);
  topDecls.add(fn);
  auto typeEnv = computeSignatures(fix, topDecls.asPtr());

  ZC_EXPECT(typeEnv.hasType(fn));
  auto& ty = typeEnv.getType(fn);
  ZC_EXPECT(isFunction(ty));
  if (isFunction(ty)) {
    auto& fnTy = static_cast<const type::FunctionType&>(ty);
    ZC_EXPECT(fnTy.getGenericParamCount() == 1);
    if (fnTy.getGenericParamCount() == 1) {
      ZC_EXPECT(fnTy.getGenericParam(0).upperBounds.size() == 1);
    }
  }
}

ZC_TEST("DeclSignature.FunctionParamAndReturnShareGenericTypeVar") {
  TestFixture fix;
  auto generic = fix.makeGenericTypeParam("T"_zc);
  zc::Vector<ast::NodeId> genericNodes;
  genericNodes.add(generic);
  auto generics = fix.makeGenericParams(fix.makeNodeList(genericNodes.asPtr()));

  auto param = fix.makeFunctionParamDecl("value"_zc, fix.makeNamedTypeExpr("T"_zc));
  zc::Vector<ast::NodeId> paramNodes;
  paramNodes.add(param);
  auto paramList = fix.makeFunctionParamList(fix.makeNodeList(paramNodes.asPtr()));
  auto fn = fix.makeFunctionDecl("id"_zc, ast::NodeId(), paramList, fix.makeNamedTypeExpr("T"_zc),
                                 ast::NodeId(), generics);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(fn);
  auto typeEnv = computeSignatures(fix, topDecls.asPtr());

  ZC_EXPECT(typeEnv.hasType(fn));
  auto& ty = typeEnv.getType(fn);
  ZC_EXPECT(isFunction(ty));
  if (isFunction(ty)) {
    auto& fnTy = static_cast<const type::FunctionType&>(ty);
    ZC_EXPECT(fnTy.getParamCount() == 1);
    ZC_EXPECT(isTypeVar(fnTy.getParamType(0)));
    ZC_EXPECT(isTypeVar(fnTy.getReturnType()));
    if (isTypeVar(fnTy.getParamType(0)) && isTypeVar(fnTy.getReturnType())) {
      auto& paramVar = static_cast<const type::TypeVar&>(fnTy.getParamType(0));
      auto& retVar = static_cast<const type::TypeVar&>(fnTy.getReturnType());
      ZC_EXPECT(paramVar.getId() == retVar.getId());
    }
  }
  ZC_EXPECT(typeEnv.hasType(generic));
  ZC_EXPECT(isTypeVar(typeEnv.getType(generic)));
}

ZC_TEST("DeclSignature.FunctionReturnTypeComputed") {
  TestFixture fix;
  zc::Vector<ast::NodeId> bodyStmts;
  bodyStmts.add(fix.makeReturnStmt(fix.makeIntLiteral(42)));
  auto bodyBlock = fix.makeBlockStmt(fix.makeNodeList(bodyStmts.asPtr()));
  auto fn = fix.makeFunctionDecl("getAnswer"_zc, bodyBlock);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(fn);
  auto typeEnv = computeSignatures(fix, topDecls.asPtr());

  ZC_EXPECT(typeEnv.hasType(fn));
  auto& ty = typeEnv.getType(fn);
  if (isFunction(ty)) {
    auto& fnTy = static_cast<const type::FunctionType&>(ty);
    // Return type should exist and not be error
    ZC_EXPECT(!isError(fnTy.getReturnType()));
  }
}

// ============================================================================
// Class signature computation
// ============================================================================

ZC_TEST("DeclSignature.ClassDeclHasSignature") {
  TestFixture fix;
  auto cls = fix.makeClassDecl("MyClass"_zc);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(cls);
  auto typeEnv = computeSignatures(fix, topDecls.asPtr());

  ZC_EXPECT(typeEnv.hasType(cls));
  auto& ty = typeEnv.getType(cls);
  // Class should produce a Named or Object type
  ZC_EXPECT(ty.getKind() == type::TypeKind::Named || ty.getKind() == type::TypeKind::Object);
}

ZC_TEST("DeclSignature.ClassWithSuperclass") {
  TestFixture fix;
  auto superRef = fix.makeIdentExpr("BaseClass"_zc);
  auto cls = fix.makeClassDecl("Derived"_zc, superRef);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(cls);
  auto typeEnv = computeSignatures(fix, topDecls.asPtr());

  ZC_EXPECT(typeEnv.hasType(cls));
}

// ============================================================================
// Interface signature computation
// ============================================================================

ZC_TEST("DeclSignature.InterfaceDeclHasSignature") {
  TestFixture fix;
  auto iface = fix.makeInterfaceDecl("Drawable"_zc);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(iface);
  auto typeEnv = computeSignatures(fix, topDecls.asPtr());

  ZC_EXPECT(typeEnv.hasType(iface));
  auto& ty = typeEnv.getType(iface);
  ZC_EXPECT(ty.getKind() == type::TypeKind::Interface || ty.getKind() == type::TypeKind::Named);
}

// ============================================================================
// Variable signature computation
// ============================================================================

ZC_TEST("DeclSignature.VariableDeclWithTypeAnnotation") {
  TestFixture fix;
  auto pat = fix.makeBindingPattern("x"_zc);
  auto ty = fix.makeNamedTypeExpr("i32"_zc);
  auto init = fix.makeIntLiteral(42);
  auto decl = fix.makeVariableDeclarator(pat, ty, init);
  zc::Vector<ast::NodeId> declList;
  declList.add(decl);
  auto varList = fix.makeVariableDeclaratorList(fix.makeNodeList(declList.asPtr()));
  auto let = fix.makeLetStmt(varList);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(let);
  auto typeEnv = computeSignatures(fix, topDecls.asPtr());

  // The variable declarator or pattern should have a type
  ZC_EXPECT(typeEnv.hasType(decl) || typeEnv.hasType(pat) || typeEnv.hasType(let));
}

ZC_TEST("DeclSignature.ConstDeclarationStoresAnnotatedSignature") {
  TestFixture fix;
  auto pat = fix.makeBindingPattern("MAX"_zc);
  auto ty = fix.makeNamedTypeExpr("i32"_zc);
  auto init = fix.makeIntLiteral(1);
  auto decl = fix.makeVariableDeclarator(pat, ty, init);
  zc::Vector<ast::NodeId> declList;
  declList.add(decl);
  auto varList = fix.makeVariableDeclaratorList(fix.makeNodeList(declList.asPtr()));
  auto let = fix.makeLetStmt(varList, static_cast<uint8_t>(ast::BindingDeclarationKind::Const));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(let);
  auto typeEnv = computeSignatures(fix, topDecls.asPtr());

  ZC_EXPECT(typeEnv.hasType(decl));
  auto& declType = typeEnv.getType(decl);
  auto kind = type::primitiveKindOf(declType);
  ZC_EXPECT(kind != zc::none);
  ZC_IF_SOME(value, kind) { ZC_EXPECT(value == type::PrimitiveKind::I32); }
}

ZC_TEST("DeclSignature.VariableDeclWithoutTypeAnnotation") {
  TestFixture fix;
  auto pat = fix.makeBindingPattern("y"_zc);
  auto init = fix.makeStrLiteral("hello"_zc);
  auto decl = fix.makeVariableDeclarator(pat, ast::NodeId(), init);
  zc::Vector<ast::NodeId> declList;
  declList.add(decl);
  auto varList = fix.makeVariableDeclaratorList(fix.makeNodeList(declList.asPtr()));
  auto let = fix.makeLetStmt(varList);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(let);
  auto typeEnv = computeSignatures(fix, topDecls.asPtr());

  ZC_EXPECT(typeEnv.hasType(decl));
  ZC_EXPECT(isError(typeEnv.getType(decl)));
}

// ============================================================================
// Type alias signature
// ============================================================================

ZC_TEST("DeclSignature.TypeAliasHasSignature") {
  TestFixture fix;
  // type MyInt = i32;
  auto aliasedTy = fix.makeNamedTypeExpr("i32"_zc);
  auto alias = fix.makeAliasDecl("MyInt"_zc, aliasedTy);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(alias);
  auto typeEnv = computeSignatures(fix, topDecls.asPtr());

  // Type alias should be recorded
  ZC_EXPECT(typeEnv.hasType(alias) || !fix.diagnostics().hasErrors());
}

ZC_TEST("DeclSignature.RecursiveTypeAliasCycleReportsError") {
  TestFixture fix;
  auto aTarget = fix.makeNamedTypeExpr("B"_zc);
  auto bTarget = fix.makeNamedTypeExpr("A"_zc);
  auto aliasA = fix.makeAliasDecl("A"_zc, aTarget);
  auto aliasB = fix.makeAliasDecl("B"_zc, bTarget);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(aliasA);
  topDecls.add(aliasB);
  auto typeEnv = computeSignatures(fix, topDecls.asPtr());

  ZC_EXPECT(fix.diagnostics().hasErrors());
  ZC_EXPECT(typeEnv.hasType(aliasA));
  ZC_EXPECT(isError(typeEnv.getType(aliasA)));
}

// ============================================================================
// Type expression resolution: predefined types
// ============================================================================

ZC_TEST("DeclSignature.ResolvePredefinedI32Type") {
  TestFixture fix;
  auto tyExpr = fix.makeNamedTypeExpr("i32"_zc);

  // Use type alias to trigger type expression resolution
  auto alias = fix.makeAliasDecl("T"_zc, tyExpr);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(alias);
  auto typeEnv = computeSignatures(fix, topDecls.asPtr());

  ZC_EXPECT(typeEnv.hasType(tyExpr) || typeEnv.hasType(alias));
  if (typeEnv.hasType(tyExpr)) {
    auto& ty = typeEnv.getType(tyExpr);
    ZC_EXPECT(isPrimitive(ty) || ty.getKind() == type::TypeKind::Named);
  }
}

ZC_TEST("DeclSignature.ResolvePredefinedBoolType") {
  TestFixture fix;
  auto tyExpr = fix.makeNamedTypeExpr("bool"_zc);
  auto alias = fix.makeAliasDecl("B"_zc, tyExpr);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(alias);
  auto typeEnv = computeSignatures(fix, topDecls.asPtr());

  if (typeEnv.hasType(tyExpr)) {
    auto& ty = typeEnv.getType(tyExpr);
    ZC_EXPECT(isPrimitive(ty) || ty.getKind() == type::TypeKind::Named);
  }
}

ZC_TEST("DeclSignature.ResolvePredefinedStrType") {
  TestFixture fix;
  auto tyExpr = fix.makeNamedTypeExpr("str"_zc);
  auto alias = fix.makeAliasDecl("S"_zc, tyExpr);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(alias);
  auto typeEnv = computeSignatures(fix, topDecls.asPtr());

  if (typeEnv.hasType(tyExpr)) {
    auto& ty = typeEnv.getType(tyExpr);
    ZC_EXPECT(isPrimitive(ty) || ty.getKind() == type::TypeKind::Named);
  }
}

ZC_TEST("DeclSignature.UnsupportedTypeExpressionDiagnosticIsInternalFallback") {
  TestFixture fix;
  auto consumer = zc::heap<CapturingDiagnosticConsumer>();
  auto consumerPtr = consumer.get();
  fix.diagnostics().addConsumer(zc::mv(consumer));

  auto unsupportedTy = fix.makeIntLiteral(1);
  auto alias = fix.makeAliasDecl("Bad"_zc, unsupportedTy);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(alias);
  auto typeEnv = computeSignatures(fix, topDecls.asPtr());

  ZC_EXPECT(fix.diagnostics().hasErrors());
  ZC_EXPECT(containsDiagnosticId(*consumerPtr, diagnostics::DiagID::UnsupportedTypeExpression));
  ZC_EXPECT(typeEnv.hasType(alias));
  ZC_EXPECT(isError(typeEnv.getType(alias)));
}

// ============================================================================
// Function type expression
// ============================================================================

ZC_TEST("DeclSignature.ResolveFunctionTypeExpr") {
  TestFixture fix;
  auto paramTy = fix.makeNamedTypeExpr("i32"_zc);
  auto retTy = fix.makeNamedTypeExpr("bool"_zc);
  zc::Vector<ast::NodeId> paramTys;
  paramTys.add(paramTy);
  auto fnTyExpr = fix.makeFunctionTypeExpr(fix.makeNodeList(paramTys.asPtr()), retTy);
  auto alias = fix.makeAliasDecl("Callback"_zc, fnTyExpr);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(alias);
  auto typeEnv = computeSignatures(fix, topDecls.asPtr());

  auto& ty = aliasType(typeEnv, alias);
  ZC_EXPECT(isFunction(ty));
  if (isFunction(ty)) {
    auto& fnTy = static_cast<const type::FunctionType&>(ty);
    ZC_EXPECT(fnTy.getParamCount() == 1);
    ZC_EXPECT(isPrimitive(fnTy.getReturnType()));
  }
}

// ============================================================================
// Tuple type expression
// ============================================================================

ZC_TEST("DeclSignature.ResolveTupleTypeExpr") {
  TestFixture fix;
  auto elem1 = fix.makeNamedTypeExpr("i32"_zc);
  auto elem2 = fix.makeNamedTypeExpr("str"_zc);
  zc::Vector<ast::NodeId> elems;
  elems.add(elem1);
  elems.add(elem2);
  auto tupleTyExpr = fix.makeTupleTypeExpr(fix.makeNodeList(elems.asPtr()));
  auto alias = fix.makeAliasDecl("Pair"_zc, tupleTyExpr);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(alias);
  auto typeEnv = computeSignatures(fix, topDecls.asPtr());

  auto& ty = aliasType(typeEnv, alias);
  ZC_EXPECT(isTuple(ty));
  if (isTuple(ty)) {
    auto& tupleTy = static_cast<const type::TupleType&>(ty);
    ZC_EXPECT(tupleTy.getElementCount() == 2);
  }
}

// ============================================================================
// Array type expression
// ============================================================================

ZC_TEST("DeclSignature.ResolveArrayTypeExpr") {
  TestFixture fix;
  auto elemTy = fix.makeNamedTypeExpr("i32"_zc);
  auto arrTyExpr = fix.makeArrayTypeExpr(elemTy);
  auto alias = fix.makeAliasDecl("IntArray"_zc, arrTyExpr);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(alias);
  auto typeEnv = computeSignatures(fix, topDecls.asPtr());

  auto& ty = aliasType(typeEnv, alias);
  ZC_EXPECT(isArray(ty));
}

// ============================================================================
// Optional type expression
// ============================================================================

ZC_TEST("DeclSignature.ResolveOptionalTypeExpr") {
  TestFixture fix;
  auto innerTy = fix.makeNamedTypeExpr("i32"_zc);
  auto optTyExpr = fix.makeOptionalTypeExpr(innerTy);
  auto alias = fix.makeAliasDecl("MaybeInt"_zc, optTyExpr);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(alias);
  auto typeEnv = computeSignatures(fix, topDecls.asPtr());

  auto& ty = aliasType(typeEnv, alias);
  ZC_EXPECT(isUnion(ty));
  if (isUnion(ty)) {
    auto& unionTy = static_cast<const type::UnionType&>(ty);
    ZC_EXPECT(unionTy.isNullable());
  }
}

// ============================================================================
// Reference type expression
// ============================================================================

ZC_TEST("DeclSignature.ResolveReferenceTypeExpr") {
  TestFixture fix;
  auto innerTy = fix.makeNamedTypeExpr("i32"_zc);
  auto refTyExpr = fix.makeReferenceTypeExpr(innerTy);
  auto alias = fix.makeAliasDecl("IntRef"_zc, refTyExpr);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(alias);
  auto typeEnv = computeSignatures(fix, topDecls.asPtr());

  auto& ty = aliasType(typeEnv, alias);
  ZC_EXPECT(isReference(ty));
  if (isReference(ty)) {
    auto& refTy = static_cast<const type::ReferenceType&>(ty);
    ZC_EXPECT(refTy.getMutability() == type::Mutability::Const);
  }
}

ZC_TEST("DeclSignature.ResolveMutableReferenceTypeExpr") {
  TestFixture fix;
  auto innerTy = fix.makeNamedTypeExpr("i32"_zc);
  auto refTyExpr = fix.makeReferenceTypeExpr(innerTy, true);
  auto alias = fix.makeAliasDecl("MutIntRef"_zc, refTyExpr);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(alias);
  auto typeEnv = computeSignatures(fix, topDecls.asPtr());

  auto& ty = aliasType(typeEnv, alias);
  ZC_EXPECT(isReference(ty));
  if (isReference(ty)) {
    auto& refTy = static_cast<const type::ReferenceType&>(ty);
    ZC_EXPECT(refTy.getMutability() == type::Mutability::Mutable);
  }
}

// ============================================================================
// Raw pointer type expression
// ============================================================================

ZC_TEST("DeclSignature.ResolveRawPointerTypeExpr") {
  TestFixture fix;
  auto innerTy = fix.makeNamedTypeExpr("i32"_zc);
  auto ptrTyExpr = fix.makeRawPointerTypeExpr(innerTy);
  auto alias = fix.makeAliasDecl("IntPtr"_zc, ptrTyExpr);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(alias);
  auto typeEnv = computeSignatures(fix, topDecls.asPtr());

  auto& ty = aliasType(typeEnv, alias);
  ZC_EXPECT(isRawPointer(ty));
  if (isRawPointer(ty)) {
    auto& ptrTy = static_cast<const type::RawPointerType&>(ty);
    ZC_EXPECT(ptrTy.getMutability() == type::Mutability::Const);
  }
}

ZC_TEST("DeclSignature.ResolveMutableRawPointerTypeExpr") {
  TestFixture fix;
  auto innerTy = fix.makeNamedTypeExpr("i32"_zc);
  auto ptrTyExpr = fix.makeRawPointerTypeExpr(innerTy, true);
  auto alias = fix.makeAliasDecl("MutIntPtr"_zc, ptrTyExpr);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(alias);
  auto typeEnv = computeSignatures(fix, topDecls.asPtr());

  auto& ty = aliasType(typeEnv, alias);
  ZC_EXPECT(isRawPointer(ty));
  if (isRawPointer(ty)) {
    auto& ptrTy = static_cast<const type::RawPointerType&>(ty);
    ZC_EXPECT(ptrTy.getMutability() == type::Mutability::Mutable);
  }
}

// ============================================================================
// Union type expression
// ============================================================================

ZC_TEST("DeclSignature.ResolveUnionTypeExpr") {
  TestFixture fix;
  auto left = fix.makeNamedTypeExpr("i32"_zc);
  auto right = fix.makeNamedTypeExpr("str"_zc);
  auto unionTyExpr = fix.makeUnionTypeExpr(left, right);
  auto alias = fix.makeAliasDecl("IntOrStr"_zc, unionTyExpr);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(alias);
  auto typeEnv = computeSignatures(fix, topDecls.asPtr());

  auto& ty = aliasType(typeEnv, alias);
  ZC_EXPECT(isUnion(ty));
  if (isUnion(ty)) {
    auto& unionTy = static_cast<const type::UnionType&>(ty);
    ZC_EXPECT(unionTy.getAlternativeCount() == 2);
  }
}

// ============================================================================
// Intersection type expression
// ============================================================================

ZC_TEST("DeclSignature.ResolveIntersectionTypeExpr") {
  TestFixture fix;
  auto left = fix.makeNamedTypeExpr("Drawable"_zc);
  auto right = fix.makeNamedTypeExpr("Serializable"_zc);
  auto interTyExpr = fix.makeIntersectionTypeExpr(left, right);
  auto alias = fix.makeAliasDecl("DrawableSerializable"_zc, interTyExpr);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(alias);
  auto typeEnv = computeSignatures(fix, topDecls.asPtr());

  auto& ty = aliasType(typeEnv, alias);
  ZC_EXPECT(isIntersection(ty));
  if (isIntersection(ty)) {
    auto& interTy = static_cast<const type::IntersectionType&>(ty);
    ZC_EXPECT(interTy.getConjunctCount() == 2);
  }
}

// ============================================================================
// Dynamic type expression
// ============================================================================

ZC_TEST("DeclSignature.ResolveDynTypeExpr") {
  TestFixture fix;
  auto ifaceTy = fix.makeNamedTypeExpr("Drawable"_zc);
  auto dynTyExpr = fix.makeDynTypeExpr(ifaceTy);
  auto alias = fix.makeAliasDecl("DynDrawable"_zc, dynTyExpr);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(alias);
  auto typeEnv = computeSignatures(fix, topDecls.asPtr());

  auto& ty = aliasType(typeEnv, alias);
  ZC_EXPECT(isExistential(ty));
}

ZC_TEST("DeclSignature.DynRejectsStaticInterfaceMethod") {
  TestFixture fix;
  auto consumer = zc::heap<CapturingDiagnosticConsumer>();
  auto consumerPtr = consumer.get();
  fix.diagnostics().addConsumer(zc::mv(consumer));

  auto method = fix.makeMethodDecl("create"_zc, ast::NodeId(), ast::NodeId(),
                                   fix.makeNamedTypeExpr("Factory"_zc), true);
  zc::Vector<ast::NodeId> members;
  members.add(method);
  auto iface = fix.makeInterfaceDecl("Factory"_zc,
                                     fix.makeClassMemberList(fix.makeNodeList(members.asPtr())));
  auto alias =
      fix.makeAliasDecl("DynFactory"_zc, fix.makeDynTypeExpr(fix.makeNamedTypeExpr("Factory"_zc)));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(iface);
  topDecls.add(alias);
  computeSignatures(fix, topDecls.asPtr());

  ZC_EXPECT(containsDiagnosticId(*consumerPtr, diagnostics::DiagID::DynStaticMethod));
}

ZC_TEST("DeclSignature.DynRejectsGenericInterfaceMethod") {
  TestFixture fix;
  auto consumer = zc::heap<CapturingDiagnosticConsumer>();
  auto consumerPtr = consumer.get();
  fix.diagnostics().addConsumer(zc::mv(consumer));

  zc::Vector<ast::NodeId> genericNodes;
  genericNodes.add(fix.makeGenericTypeParam("U"_zc));
  auto methodTypeParams = fix.makeGenericParams(fix.makeNodeList(genericNodes.asPtr()));
  auto method = fix.makeMethodDecl("map"_zc, ast::NodeId(), ast::NodeId(),
                                   fix.makeNamedTypeExpr("U"_zc), false, methodTypeParams);
  zc::Vector<ast::NodeId> members;
  members.add(method);
  auto iface = fix.makeInterfaceDecl("Mapper"_zc,
                                     fix.makeClassMemberList(fix.makeNodeList(members.asPtr())));
  auto alias =
      fix.makeAliasDecl("DynMapper"_zc, fix.makeDynTypeExpr(fix.makeNamedTypeExpr("Mapper"_zc)));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(iface);
  topDecls.add(alias);
  computeSignatures(fix, topDecls.asPtr());

  ZC_EXPECT(containsDiagnosticId(*consumerPtr, diagnostics::DiagID::DynGenericMethod));
}

ZC_TEST("DeclSignature.DynRejectsBareSelfReturn") {
  TestFixture fix;
  auto consumer = zc::heap<CapturingDiagnosticConsumer>();
  auto consumerPtr = consumer.get();
  fix.diagnostics().addConsumer(zc::mv(consumer));

  auto method = fix.makeMethodDecl("clone"_zc, ast::NodeId(), ast::NodeId(),
                                   fix.makeNamedTypeExpr("Self"_zc));
  zc::Vector<ast::NodeId> members;
  members.add(method);
  auto iface = fix.makeInterfaceDecl("Cloneable"_zc,
                                     fix.makeClassMemberList(fix.makeNodeList(members.asPtr())));
  auto alias = fix.makeAliasDecl("DynCloneable"_zc,
                                 fix.makeDynTypeExpr(fix.makeNamedTypeExpr("Cloneable"_zc)));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(iface);
  topDecls.add(alias);
  computeSignatures(fix, topDecls.asPtr());

  ZC_EXPECT(containsDiagnosticId(*consumerPtr, diagnostics::DiagID::DynSelfReturn));
}

ZC_TEST("DeclSignature.DynRejectsMoveSelfReceiver") {
  TestFixture fix;
  auto consumer = zc::heap<CapturingDiagnosticConsumer>();
  auto consumerPtr = consumer.get();
  fix.diagnostics().addConsumer(zc::mv(consumer));

  zc::Vector<ast::NodeId> params;
  params.add(makeFunctionParamDecl(fix, "this"_zc, fix.makeNamedTypeExpr("Self"_zc),
                                   makeMoveParamAttributeList(fix)));
  auto paramList = fix.makeFunctionParamList(fix.makeNodeList(params.asPtr()));
  auto method =
      fix.makeMethodDecl("consume"_zc, ast::NodeId(), paramList, fix.makeNamedTypeExpr("unit"_zc));
  zc::Vector<ast::NodeId> members;
  members.add(method);
  auto iface = fix.makeInterfaceDecl("Consumable"_zc,
                                     fix.makeClassMemberList(fix.makeNodeList(members.asPtr())));
  auto alias = fix.makeAliasDecl("DynConsumable"_zc,
                                 fix.makeDynTypeExpr(fix.makeNamedTypeExpr("Consumable"_zc)));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(iface);
  topDecls.add(alias);
  computeSignatures(fix, topDecls.asPtr());

  ZC_EXPECT(containsDiagnosticId(*consumerPtr, diagnostics::DiagID::DynMoveSelf));
}

ZC_TEST("DeclSignature.DynRejectsUnsizedMethodParameter") {
  TestFixture fix;
  auto consumer = zc::heap<CapturingDiagnosticConsumer>();
  auto consumerPtr = consumer.get();
  fix.diagnostics().addConsumer(zc::mv(consumer));

  zc::Vector<ast::NodeId> params;
  params.add(fix.makeFunctionParamDecl(
      "items"_zc, fix.makeSliceArrayTypeExpr(fix.makeNamedTypeExpr("i32"_zc))));
  auto paramList = fix.makeFunctionParamList(fix.makeNodeList(params.asPtr()));
  auto method =
      fix.makeMethodDecl("write"_zc, ast::NodeId(), paramList, fix.makeNamedTypeExpr("unit"_zc));
  zc::Vector<ast::NodeId> members;
  members.add(method);
  auto iface = fix.makeInterfaceDecl("Writer"_zc,
                                     fix.makeClassMemberList(fix.makeNodeList(members.asPtr())));
  auto alias =
      fix.makeAliasDecl("DynWriter"_zc, fix.makeDynTypeExpr(fix.makeNamedTypeExpr("Writer"_zc)));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(iface);
  topDecls.add(alias);
  computeSignatures(fix, topDecls.asPtr());

  ZC_EXPECT(containsDiagnosticId(*consumerPtr, diagnostics::DiagID::DynUnsizedParameter));
}

ZC_TEST("DeclSignature.DynRejectsUnboundAssociatedType") {
  TestFixture fix;
  auto consumer = zc::heap<CapturingDiagnosticConsumer>();
  auto consumerPtr = consumer.get();
  fix.diagnostics().addConsumer(zc::mv(consumer));

  zc::Vector<ast::NodeId> members;
  members.add(makeAssociatedTypeDecl(fix, "Item"_zc));
  auto iface = fix.makeInterfaceDecl("Iterator"_zc,
                                     fix.makeClassMemberList(fix.makeNodeList(members.asPtr())));
  auto alias = fix.makeAliasDecl("DynIterator"_zc,
                                 fix.makeDynTypeExpr(fix.makeNamedTypeExpr("Iterator"_zc)));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(iface);
  topDecls.add(alias);
  computeSignatures(fix, topDecls.asPtr());

  ZC_EXPECT(containsDiagnosticId(*consumerPtr, diagnostics::DiagID::DynUnassociatedType));
}

ZC_TEST("DeclSignature.DynAcceptsAssociatedTypeBinding") {
  TestFixture fix;
  auto consumer = zc::heap<CapturingDiagnosticConsumer>();
  auto consumerPtr = consumer.get();
  fix.diagnostics().addConsumer(zc::mv(consumer));

  zc::Vector<ast::NodeId> members;
  members.add(makeAssociatedTypeDecl(fix, "Item"_zc));
  auto iface = fix.makeInterfaceDecl("Iterator"_zc,
                                     fix.makeClassMemberList(fix.makeNodeList(members.asPtr())));

  auto dynTy =
      makeDynTypeWithAssocBinding(fix, "Iterator"_zc, "Item"_zc, fix.makeNamedTypeExpr("i32"_zc));
  auto alias = fix.makeAliasDecl("DynIterator"_zc, dynTy);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(iface);
  topDecls.add(alias);
  computeSignatures(fix, topDecls.asPtr());

  ZC_EXPECT(!containsDiagnosticId(*consumerPtr, diagnostics::DiagID::DynUnassociatedType));
}

ZC_TEST("DeclSignature.DynRejectsUnknownAssociatedTypeBinding") {
  TestFixture fix;
  auto consumer = zc::heap<CapturingDiagnosticConsumer>();
  auto consumerPtr = consumer.get();
  fix.diagnostics().addConsumer(zc::mv(consumer));

  auto iface = fix.makeInterfaceDecl("Drawable"_zc, fix.makeClassMemberList(ast::NodeList()));
  auto dynTy =
      makeDynTypeWithAssocBinding(fix, "Drawable"_zc, "Item"_zc, fix.makeNamedTypeExpr("i32"_zc));
  auto alias = fix.makeAliasDecl("DynDrawable"_zc, dynTy);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(iface);
  topDecls.add(alias);
  computeSignatures(fix, topDecls.asPtr());

  ZC_EXPECT(containsDiagnosticId(*consumerPtr, diagnostics::DiagID::NoAssociatedTypeProjection));
}

ZC_TEST("DeclSignature.DynRejectsGenericAssociatedType") {
  TestFixture fix;
  auto consumer = zc::heap<CapturingDiagnosticConsumer>();
  auto consumerPtr = consumer.get();
  fix.diagnostics().addConsumer(zc::mv(consumer));

  zc::Vector<ast::NodeId> genericNodes;
  genericNodes.add(fix.makeGenericTypeParam("T"_zc));
  auto assocTypeParams = fix.makeGenericParams(fix.makeNodeList(genericNodes.asPtr()));

  zc::Vector<ast::NodeId> members;
  members.add(makeGenericAssociatedTypeDecl(fix, "Iter"_zc, assocTypeParams));
  auto iface = fix.makeInterfaceDecl("Iterable"_zc,
                                     fix.makeClassMemberList(fix.makeNodeList(members.asPtr())));
  auto alias = fix.makeAliasDecl("DynIterable"_zc,
                                 fix.makeDynTypeExpr(fix.makeNamedTypeExpr("Iterable"_zc)));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(iface);
  topDecls.add(alias);
  computeSignatures(fix, topDecls.asPtr());

  ZC_EXPECT(containsDiagnosticId(*consumerPtr, diagnostics::DiagID::DynGatNotAllowed));
}

ZC_TEST("DeclSignature.DynRejectsObjectUnsafeSuperinterface") {
  TestFixture fix;
  auto consumer = zc::heap<CapturingDiagnosticConsumer>();
  auto consumerPtr = consumer.get();
  fix.diagnostics().addConsumer(zc::mv(consumer));

  auto method = fix.makeMethodDecl("clone"_zc, ast::NodeId(), ast::NodeId(),
                                   fix.makeNamedTypeExpr("Self"_zc));
  zc::Vector<ast::NodeId> superMembers;
  superMembers.add(method);
  auto baseIface = fix.makeInterfaceDecl(
      "Base"_zc, fix.makeClassMemberList(fix.makeNodeList(superMembers.asPtr())));

  zc::Vector<ast::NodeId> superIfaces;
  superIfaces.add(makeModulePathNamedTypeExpr(fix, "Base"_zc));
  auto childIface = fix.makeInterfaceDecl(
      "Child"_zc, ast::NodeId(), makeImplIfaceList(fix, fix.makeNodeList(superIfaces.asPtr())));
  auto alias =
      fix.makeAliasDecl("DynChild"_zc, fix.makeDynTypeExpr(fix.makeNamedTypeExpr("Child"_zc)));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(baseIface);
  topDecls.add(childIface);
  topDecls.add(alias);
  computeSignatures(fix, topDecls.asPtr());

  ZC_EXPECT(containsDiagnosticId(*consumerPtr, diagnostics::DiagID::DynSuperNotObjectSafe));
}

ZC_TEST("DeclSignature.ResolveQualifiedAssociatedTypeProjection") {
  TestFixture fix;
  zc::Vector<ast::NodeId> iteratorIfaceNodes;
  iteratorIfaceNodes.add(fix.makeNamedTypeExpr("Iterator"_zc));
  auto iteratorIfaces = makeImplIfaceList(fix, fix.makeNodeList(iteratorIfaceNodes.asPtr()));
  zc::Vector<ast::NodeId> iteratorMembers;
  iteratorMembers.add(makeAssociatedTypeBinding(fix, "Item"_zc, fix.makeNamedTypeExpr("i32"_zc)));
  auto iteratorImpl =
      makeStandaloneImplDecl(fix, fix.makeNamedTypeExpr("Box"_zc), iteratorIfaces,
                             fix.makeClassMemberList(fix.makeNodeList(iteratorMembers.asPtr())));

  zc::Vector<ast::NodeId> streamIfaceNodes;
  streamIfaceNodes.add(fix.makeNamedTypeExpr("Stream"_zc));
  auto streamIfaces = makeImplIfaceList(fix, fix.makeNodeList(streamIfaceNodes.asPtr()));
  zc::Vector<ast::NodeId> streamMembers;
  streamMembers.add(makeAssociatedTypeBinding(fix, "Item"_zc, fix.makeNamedTypeExpr("str"_zc)));
  auto streamImpl =
      makeStandaloneImplDecl(fix, fix.makeNamedTypeExpr("Box"_zc), streamIfaces,
                             fix.makeClassMemberList(fix.makeNodeList(streamMembers.asPtr())));

  auto projection = fix.makeAssociatedTypeProjectionExpr(
      fix.makeNamedTypeExpr("Box"_zc), fix.makeNamedTypeExpr("Iterator"_zc), "Item"_zc);
  auto alias = fix.makeAliasDecl("BoxItem"_zc, projection);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(iteratorImpl);
  topDecls.add(streamImpl);
  topDecls.add(alias);
  auto typeEnv = computeSignatures(fix, topDecls.asPtr());

  auto& ty = aliasType(typeEnv, alias);
  auto kind = type::primitiveKindOf(ty);
  ZC_EXPECT(kind != zc::none);
  ZC_IF_SOME(value, kind) { ZC_EXPECT(value == type::PrimitiveKind::I32); }
}

ZC_TEST("DeclSignature.ResolveObjectTypeExpr") {
  TestFixture fix;
  zc::Vector<ast::NodeId> members;
  members.add(fix.makeObjectTypeMember("x"_zc, fix.makeNamedTypeExpr("i32"_zc)));
  members.add(fix.makeObjectTypeMember("name"_zc, fix.makeNamedTypeExpr("str"_zc)));
  auto objTyExpr = fix.makeObjectTypeExpr(fix.makeNodeList(members.asPtr()));
  auto alias = fix.makeAliasDecl("Record"_zc, objTyExpr);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(alias);
  auto typeEnv = computeSignatures(fix, topDecls.asPtr());

  auto& ty = aliasType(typeEnv, alias);
  ZC_EXPECT(isObject(ty));
  if (isObject(ty)) {
    auto& objTy = static_cast<const type::ObjectType&>(ty);
    ZC_EXPECT(objTy.getMemberCount() == 2);
    ZC_EXPECT(objTy.hasMember("x"_zc));
    ZC_EXPECT(objTy.hasMember("name"_zc));
  }
}

// ============================================================================
// Multiple declarations
// ============================================================================

ZC_TEST("DeclSignature.MultipleFunctionDecls") {
  TestFixture fix;
  auto fn1 = fix.makeFunctionDecl("foo"_zc);
  auto fn2 = fix.makeFunctionDecl("bar"_zc);
  auto fn3 = fix.makeFunctionDecl("baz"_zc);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(fn1);
  topDecls.add(fn2);
  topDecls.add(fn3);
  auto typeEnv = computeSignatures(fix, topDecls.asPtr());

  ZC_EXPECT(typeEnv.hasType(fn1));
  ZC_EXPECT(typeEnv.hasType(fn2));
  ZC_EXPECT(typeEnv.hasType(fn3));
}

ZC_TEST("DeclSignature.MixedDecls") {
  TestFixture fix;
  auto fn = fix.makeFunctionDecl("myFunc"_zc);
  auto cls = fix.makeClassDecl("MyClass"_zc);
  auto iface = fix.makeInterfaceDecl("MyInterface"_zc);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(fn);
  topDecls.add(cls);
  topDecls.add(iface);
  auto typeEnv = computeSignatures(fix, topDecls.asPtr());

  ZC_EXPECT(typeEnv.hasType(fn));
  ZC_EXPECT(typeEnv.hasType(cls));
  ZC_EXPECT(typeEnv.hasType(iface));
}

// ============================================================================
// Enum declaration signature
// ============================================================================

ZC_TEST("DeclSignature.EnumDeclHasSignature") {
  TestFixture fix;
  // NOTE: makeEnumVariant / makeEnumVariantList not available in TestFixture,
  // so we test with an empty enum declaration.
  auto enumDecl = fix.makeEnumDecl("Color"_zc);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(enumDecl);
  auto typeEnv = computeSignatures(fix, topDecls.asPtr());

  ZC_EXPECT(typeEnv.hasType(enumDecl));
}

// ============================================================================
// No errors for valid declarations
// ============================================================================

ZC_TEST("DeclSignature.ValidDeclsNoErrors") {
  TestFixture fix;
  auto fn = fix.makeFunctionDecl("foo"_zc);
  auto cls = fix.makeClassDecl("Bar"_zc);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(fn);
  topDecls.add(cls);
  computeSignatures(fix, topDecls.asPtr());

  ZC_EXPECT(!fix.diagnostics().hasErrors());
}

}  // namespace checker
}  // namespace compiler
}  // namespace zomlang
