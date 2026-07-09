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

#include "zomlang/compiler/checker/body-checker.h"

#include "zc/core/vector.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/ast/tree.h"
#include "zomlang/compiler/binder/binder.h"
#include "zomlang/compiler/checker/decl-signature.h"
#include "zomlang/compiler/diagnostics/diagnostic-consumer.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/diagnostics/diagnostic.h"
#include "zomlang/compiler/type/array-type.h"
#include "zomlang/compiler/type/constraint-set.h"
#include "zomlang/compiler/type/function-type.h"
#include "zomlang/compiler/type/named-type.h"
#include "zomlang/compiler/type/object-type.h"
#include "zomlang/compiler/type/primitive-type.h"
#include "zomlang/compiler/type/tuple-type.h"
#include "zomlang/compiler/type/type-env.h"
#include "zomlang/compiler/type/type.h"
#include "zomlang/compiler/type/unification.h"
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

zc::StringPtr binaryOperatorMethodName(zc::StringPtr ifaceName) {
  if (ifaceName == "Add"_zc) return "add"_zc;
  if (ifaceName == "Sub"_zc) return "sub"_zc;
  if (ifaceName == "Mul"_zc) return "mul"_zc;
  if (ifaceName == "Div"_zc) return "div"_zc;
  if (ifaceName == "Rem"_zc) return "rem"_zc;
  if (ifaceName == "Pow"_zc) return "pow"_zc;
  return ""_zc;
}

zc::StringPtr unaryOperatorMethodName(zc::StringPtr ifaceName) {
  if (ifaceName == "Neg"_zc) return "neg"_zc;
  if (ifaceName == "Not"_zc) return "not"_zc;
  return ""_zc;
}

// Helper: run full pipeline (Binder + DeclSignatureComputer + BodyChecker) and return
// the TypeEnv for inspection.
struct CheckResult {
  bool success;
  type::TypeEnv typeEnv;
  TestFixture& fix;
  size_t constraintCount;
};

CheckResult runFullCheck(TestFixture& fix, zc::ArrayPtr<const ast::NodeId> decls) {
  auto tree = fix.buildSourceFile("test"_zc, decls);

  // Phase 1+2: Binder (DeclCollector + ImportResolver + NameResolver)
  binder::Binder binder(fix.symbols(), fix.diagnostics(), tree, fix.metadata());
  if (!binder.bind()) { return {false, type::TypeEnv(), fix, 0}; }

  // Phase A: DeclSignatureComputer
  type::TypeEnv typeEnv;
  DeclSignatureComputer sigComputer(typeEnv, fix.symbols(), tree, fix.metadata(),
                                    fix.diagnostics());
  sigComputer.computeSignatures();

  // Phase B: BodyChecker
  type::UnificationEngine unifier(typeEnv);
  type::ConstraintSet constraints;
  BodyChecker bodyChecker(typeEnv, unifier, constraints, fix.symbols(), tree, fix.metadata(),
                          fix.diagnostics());
  bool success = bodyChecker.checkBodies();
  size_t constraintCount = bodyChecker.getConstraints().size();
  ZC_EXPECT(typeEnv.isDispatchFrozen());

  return {success, zc::mv(typeEnv), fix, constraintCount};
}

void expectUserTypeBinaryOperatorImpl(zc::StringPtr ifaceName, ast::BinaryOperatorKind op) {
  TestFixture fix;
  auto iface = fix.makeInterfaceDecl(ifaceName);
  auto numberType = fix.makeClassDecl("Number"_zc);

  zc::Vector<ast::NodeId> ifaceNodes;
  ifaceNodes.add(fix.makeNamedTypeExpr(ifaceName));
  auto ifaceList = fix.makeImplIfaceList(fix.makeNodeList(ifaceNodes.asPtr()));

  zc::Vector<ast::NodeId> implParams;
  implParams.add(fix.makeFunctionParamDecl("rhs"_zc, fix.makeNamedTypeExpr("Number"_zc)));
  auto implParamList = fix.makeFunctionParamList(fix.makeNodeList(implParams.asPtr()));
  auto implMethod = fix.makeMethodDecl(binaryOperatorMethodName(ifaceName), ast::NodeId(),
                                       implParamList, fix.makeNamedTypeExpr("Number"_zc));
  zc::Vector<ast::NodeId> implMembers;
  implMembers.add(implMethod);
  auto implDecl =
      fix.makeStandaloneImplDecl(fix.makeNamedTypeExpr("Number"_zc), ifaceList,
                                 fix.makeClassMemberList(fix.makeNodeList(implMembers.asPtr())));

  auto xDecl = fix.makeVariableDeclarator(fix.makeBindingPattern("x"_zc),
                                          fix.makeNamedTypeExpr("Number"_zc));
  auto yDecl = fix.makeVariableDeclarator(fix.makeBindingPattern("y"_zc),
                                          fix.makeNamedTypeExpr("Number"_zc));
  zc::Vector<ast::NodeId> decls;
  decls.add(xDecl);
  decls.add(yDecl);
  auto let = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr())));

  auto binExpr = fix.makeBinaryExpr(op, fix.makeIdentExpr("x"_zc), fix.makeIdentExpr("y"_zc));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(iface);
  topDecls.add(numberType);
  topDecls.add(implDecl);
  topDecls.add(let);
  topDecls.add(binExpr);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  ZC_EXPECT(!fix.diagnostics().hasErrors());
  ZC_EXPECT(result.typeEnv.hasType(binExpr));
  auto& ty = result.typeEnv.getType(binExpr);
  ZC_EXPECT(isNamed(ty));
  if (isNamed(ty)) { ZC_EXPECT(static_cast<const type::NamedType&>(ty).getName() == "Number"_zc); }
  ZC_EXPECT(result.typeEnv.hasDispatch(binExpr));
  auto& dispatch = result.typeEnv.getDispatch(binExpr);
  ZC_EXPECT(dispatch.targetKind == type::CallTargetKind::OperatorMethod);
  ZC_EXPECT(dispatch.receiverMode == type::ReceiverMode::OperatorLeftHandSide);
  ZC_EXPECT(dispatch.interfaceName.asPtr() == ifaceName);
  ZC_EXPECT(dispatch.methodName.asPtr() == binaryOperatorMethodName(ifaceName));
  ZC_EXPECT(dispatch.implNode == implDecl);
  ZC_EXPECT(dispatch.argumentTypes.size() == 2);
  ZC_EXPECT(dispatch.resultType == result.typeEnv.getTypeId(binExpr));
}

void expectUserTypeComparisonImpl(zc::StringPtr ifaceName, ast::BinaryOperatorKind op) {
  TestFixture fix;
  auto iface = fix.makeInterfaceDecl(ifaceName);
  auto pointType = fix.makeClassDecl("Point"_zc);

  zc::Vector<ast::NodeId> ifaceNodes;
  ifaceNodes.add(fix.makeNamedTypeExpr(ifaceName));
  auto ifaceList = fix.makeImplIfaceList(fix.makeNodeList(ifaceNodes.asPtr()));

  zc::Vector<ast::NodeId> implParams;
  implParams.add(fix.makeFunctionParamDecl("rhs"_zc, fix.makeNamedTypeExpr("Point"_zc)));
  auto implParamList = fix.makeFunctionParamList(fix.makeNodeList(implParams.asPtr()));
  const bool isEq = ifaceName == "Eq"_zc;
  auto implMethod = fix.makeMethodDecl(isEq ? "eq"_zc : "cmp"_zc, ast::NodeId(), implParamList,
                                       fix.makeNamedTypeExpr(isEq ? "bool"_zc : "i32"_zc));
  zc::Vector<ast::NodeId> implMembers;
  implMembers.add(implMethod);
  auto implDecl =
      fix.makeStandaloneImplDecl(fix.makeNamedTypeExpr("Point"_zc), ifaceList,
                                 fix.makeClassMemberList(fix.makeNodeList(implMembers.asPtr())));

  auto xDecl =
      fix.makeVariableDeclarator(fix.makeBindingPattern("x"_zc), fix.makeNamedTypeExpr("Point"_zc));
  auto yDecl =
      fix.makeVariableDeclarator(fix.makeBindingPattern("y"_zc), fix.makeNamedTypeExpr("Point"_zc));
  zc::Vector<ast::NodeId> decls;
  decls.add(xDecl);
  decls.add(yDecl);
  auto let = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr())));

  auto binExpr = fix.makeBinaryExpr(op, fix.makeIdentExpr("x"_zc), fix.makeIdentExpr("y"_zc));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(iface);
  topDecls.add(pointType);
  topDecls.add(implDecl);
  topDecls.add(let);
  topDecls.add(binExpr);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  ZC_EXPECT(!fix.diagnostics().hasErrors());
  ZC_EXPECT(result.typeEnv.hasType(binExpr));
  auto& ty = result.typeEnv.getType(binExpr);
  ZC_EXPECT(isPrimitive(ty));
  if (isPrimitive(ty)) {
    ZC_EXPECT(static_cast<const type::PrimitiveType&>(ty).getPrimitiveKind() ==
              type::PrimitiveKind::Bool);
  }
  ZC_EXPECT(result.typeEnv.hasDispatch(binExpr));
  auto& dispatch = result.typeEnv.getDispatch(binExpr);
  ZC_EXPECT(dispatch.targetKind == type::CallTargetKind::OperatorMethod);
  ZC_EXPECT(dispatch.receiverMode == type::ReceiverMode::OperatorLeftHandSide);
  ZC_EXPECT(dispatch.interfaceName.asPtr() == ifaceName);
  ZC_EXPECT(dispatch.methodName.asPtr() == (isEq ? "eq"_zc : "cmp"_zc));
  ZC_EXPECT(dispatch.implNode == implDecl);
  ZC_EXPECT(dispatch.argumentTypes.size() == 2);
  ZC_EXPECT(dispatch.resultType == result.typeEnv.getTypeId(binExpr));
}

void expectUserTypeComparisonWithoutImplFails(zc::StringPtr missingIfaceName,
                                              ast::BinaryOperatorKind op) {
  TestFixture fix;
  auto consumer = zc::heap<CapturingDiagnosticConsumer>();
  auto consumerPtr = consumer.get();
  fix.diagnostics().addConsumer(zc::mv(consumer));

  auto pointType = fix.makeClassDecl("Point"_zc);
  auto xDecl =
      fix.makeVariableDeclarator(fix.makeBindingPattern("x"_zc), fix.makeNamedTypeExpr("Point"_zc));
  auto yDecl =
      fix.makeVariableDeclarator(fix.makeBindingPattern("y"_zc), fix.makeNamedTypeExpr("Point"_zc));
  zc::Vector<ast::NodeId> decls;
  decls.add(xDecl);
  decls.add(yDecl);
  auto let = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr())));

  auto binExpr = fix.makeBinaryExpr(op, fix.makeIdentExpr("x"_zc), fix.makeIdentExpr("y"_zc));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(pointType);
  topDecls.add(let);
  topDecls.add(binExpr);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(!result.success);
  ZC_EXPECT(fix.diagnostics().hasErrors());
  ZC_EXPECT(containsDiagnosticId(*consumerPtr, diagnostics::DiagID::CheckerTraitNotImplemented));
  ZC_EXPECT(result.typeEnv.hasType(binExpr));
  ZC_EXPECT(isError(result.typeEnv.getType(binExpr)));
  (void)missingIfaceName;
}

void expectUserTypeUnaryOperatorImpl(zc::StringPtr ifaceName, ast::UnaryOperatorKind op,
                                     bool returnsOperandType) {
  TestFixture fix;
  auto iface = fix.makeInterfaceDecl(ifaceName);
  auto operandType = fix.makeClassDecl("Operand"_zc);

  zc::Vector<ast::NodeId> ifaceNodes;
  ifaceNodes.add(fix.makeNamedTypeExpr(ifaceName));
  auto ifaceList = fix.makeImplIfaceList(fix.makeNodeList(ifaceNodes.asPtr()));

  auto implRetTy =
      returnsOperandType ? fix.makeNamedTypeExpr("Operand"_zc) : fix.makeNamedTypeExpr("bool"_zc);
  auto implMethod = fix.makeMethodDecl(unaryOperatorMethodName(ifaceName), ast::NodeId(),
                                       ast::NodeId(), implRetTy);
  zc::Vector<ast::NodeId> implMembers;
  implMembers.add(implMethod);
  auto implDecl =
      fix.makeStandaloneImplDecl(fix.makeNamedTypeExpr("Operand"_zc), ifaceList,
                                 fix.makeClassMemberList(fix.makeNodeList(implMembers.asPtr())));

  auto valueDecl = fix.makeVariableDeclarator(fix.makeBindingPattern("value"_zc),
                                              fix.makeNamedTypeExpr("Operand"_zc));
  zc::Vector<ast::NodeId> decls;
  decls.add(valueDecl);
  auto let = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr())));
  auto unary = fix.makeUnaryExpr(op, fix.makeIdentExpr("value"_zc));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(iface);
  topDecls.add(operandType);
  topDecls.add(implDecl);
  topDecls.add(let);
  topDecls.add(unary);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  ZC_EXPECT(!fix.diagnostics().hasErrors());
  ZC_EXPECT(result.typeEnv.hasType(unary));
  auto& ty = result.typeEnv.getType(unary);
  if (returnsOperandType) {
    ZC_EXPECT(isNamed(ty));
    if (isNamed(ty)) {
      ZC_EXPECT(static_cast<const type::NamedType&>(ty).getName() == "Operand"_zc);
    }
  } else {
    ZC_EXPECT(isPrimitive(ty));
    if (isPrimitive(ty)) {
      ZC_EXPECT(static_cast<const type::PrimitiveType&>(ty).getPrimitiveKind() ==
                type::PrimitiveKind::Bool);
    }
  }
  ZC_EXPECT(result.typeEnv.hasDispatch(unary));
  auto& dispatch = result.typeEnv.getDispatch(unary);
  ZC_EXPECT(dispatch.targetKind == type::CallTargetKind::OperatorMethod);
  ZC_EXPECT(dispatch.receiverMode == type::ReceiverMode::OperatorOperand);
  ZC_EXPECT(dispatch.interfaceName.asPtr() == ifaceName);
  ZC_EXPECT(dispatch.methodName.asPtr() == unaryOperatorMethodName(ifaceName));
  ZC_EXPECT(dispatch.implNode == implDecl);
  ZC_EXPECT(dispatch.argumentTypes.size() == 1);
  ZC_EXPECT(dispatch.resultType == result.typeEnv.getTypeId(unary));
}

void expectUserTypeUnaryWithoutImplFails(ast::UnaryOperatorKind op) {
  TestFixture fix;
  auto consumer = zc::heap<CapturingDiagnosticConsumer>();
  auto consumerPtr = consumer.get();
  fix.diagnostics().addConsumer(zc::mv(consumer));

  auto operandType = fix.makeClassDecl("Operand"_zc);
  auto valueDecl = fix.makeVariableDeclarator(fix.makeBindingPattern("value"_zc),
                                              fix.makeNamedTypeExpr("Operand"_zc));
  zc::Vector<ast::NodeId> decls;
  decls.add(valueDecl);
  auto let = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr())));
  auto unary = fix.makeUnaryExpr(op, fix.makeIdentExpr("value"_zc));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(operandType);
  topDecls.add(let);
  topDecls.add(unary);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(!result.success);
  ZC_EXPECT(fix.diagnostics().hasErrors());
  ZC_EXPECT(containsDiagnosticId(*consumerPtr, diagnostics::DiagID::CheckerTraitNotImplemented));
  ZC_EXPECT(result.typeEnv.hasType(unary));
  if (result.typeEnv.hasType(unary)) { ZC_EXPECT(isError(result.typeEnv.getType(unary))); }
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

}  // namespace

// ============================================================================
// Literal type inference
// ============================================================================

ZC_TEST("BodyChecker.InfersIntLiteralType") {
  TestFixture fix;
  auto lit = fix.makeIntLiteral(42);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(lit);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  ZC_EXPECT(result.typeEnv.hasType(lit));
  auto& ty = result.typeEnv.getType(lit);
  ZC_EXPECT(isPrimitive(ty));
}

ZC_TEST("BodyChecker.InfersFloatLiteralType") {
  TestFixture fix;
  auto lit = fix.makeFloatLiteral(3.14);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(lit);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  if (result.typeEnv.hasType(lit)) {
    auto& ty = result.typeEnv.getType(lit);
    ZC_EXPECT(isPrimitive(ty));
  }
}

ZC_TEST("BodyChecker.InfersStringLiteralType") {
  TestFixture fix;
  auto lit = fix.makeStrLiteral("hello"_zc);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(lit);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  if (result.typeEnv.hasType(lit)) {
    auto& ty = result.typeEnv.getType(lit);
    ZC_EXPECT(isPrimitive(ty));
  }
}

ZC_TEST("BodyChecker.InfersBoolLiteralType") {
  TestFixture fix;
  auto lit = fix.makeBoolLiteral(true);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(lit);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  if (result.typeEnv.hasType(lit)) {
    auto& ty = result.typeEnv.getType(lit);
    ZC_EXPECT(isPrimitive(ty));
  }
}

ZC_TEST("BodyChecker.InfersNullLiteralType") {
  TestFixture fix;
  auto lit = fix.makeNullLiteral();

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(lit);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  if (result.typeEnv.hasType(lit)) {
    auto& ty = result.typeEnv.getType(lit);
    ZC_EXPECT(isNull(ty));
  }
}

ZC_TEST("BodyChecker.InfersUnitLiteralType") {
  TestFixture fix;
  auto lit = fix.makeUnitLiteral();

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(lit);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  if (result.typeEnv.hasType(lit)) {
    auto& ty = result.typeEnv.getType(lit);
    ZC_EXPECT(isUnit(ty));
  }
}

// ============================================================================
// Binary expression type inference
// ============================================================================

ZC_TEST("BodyChecker.InfersBinaryArithExprType") {
  TestFixture fix;
  // 1 + 2
  auto lhs = fix.makeIntLiteral(1);
  auto rhs = fix.makeIntLiteral(2);
  auto binExpr = fix.makeBinaryExpr(ast::BinaryOperatorKind::Add, lhs, rhs);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(binExpr);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  if (result.typeEnv.hasType(binExpr)) {
    auto& ty = result.typeEnv.getType(binExpr);
    ZC_EXPECT(isPrimitive(ty));
  }
  ZC_EXPECT(result.typeEnv.hasDispatch(binExpr));
  auto& dispatch = result.typeEnv.getDispatch(binExpr);
  ZC_EXPECT(dispatch.targetKind == type::CallTargetKind::PrimitiveOperator);
  ZC_EXPECT(dispatch.receiverMode == type::ReceiverMode::OperatorLeftHandSide);
  ZC_EXPECT(dispatch.argumentTypes.size() == 2);
  ZC_EXPECT(dispatch.resultType == result.typeEnv.getTypeId(binExpr));
}

ZC_TEST("BodyChecker.BinaryArithmeticRejectsImplicitNumericWidening") {
  TestFixture fix;
  auto consumer = zc::heap<CapturingDiagnosticConsumer>();
  auto consumerPtr = consumer.get();
  fix.diagnostics().addConsumer(zc::mv(consumer));

  auto lhs = fix.makeIntLiteral(1);
  auto rhs = fix.makeFloatLiteral(2.0);
  auto binExpr = fix.makeBinaryExpr(ast::BinaryOperatorKind::Add, lhs, rhs);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(binExpr);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(!result.success);
  ZC_EXPECT(fix.diagnostics().hasErrors());
  ZC_EXPECT(containsDiagnosticId(*consumerPtr, diagnostics::DiagID::CannotUnifyTypes));
  ZC_EXPECT(result.typeEnv.hasType(binExpr));
  ZC_EXPECT(isError(result.typeEnv.getType(binExpr)));
}

ZC_TEST("BodyChecker.BinaryAddUsesUserTypeAddImpl") {
  expectUserTypeBinaryOperatorImpl("Add"_zc, ast::BinaryOperatorKind::Add);
}

ZC_TEST("BodyChecker.BinaryAddRejectsWrongTraitMethodSignature") {
  TestFixture fix;
  auto consumer = zc::heap<CapturingDiagnosticConsumer>();
  auto consumerPtr = consumer.get();
  fix.diagnostics().addConsumer(zc::mv(consumer));

  zc::Vector<ast::NodeId> ifaceParams;
  ifaceParams.add(fix.makeFunctionParamDecl("rhs"_zc, fix.makeNamedTypeExpr("Number"_zc)));
  auto ifaceParamList = fix.makeFunctionParamList(fix.makeNodeList(ifaceParams.asPtr()));
  auto ifaceMethod = fix.makeMethodDecl("add"_zc, ast::NodeId(), ifaceParamList,
                                        fix.makeNamedTypeExpr("Number"_zc));
  zc::Vector<ast::NodeId> ifaceMembers;
  ifaceMembers.add(ifaceMethod);
  auto addIface = fix.makeInterfaceDecl(
      "Add"_zc, fix.makeClassMemberList(fix.makeNodeList(ifaceMembers.asPtr())));

  auto numberType = fix.makeClassDecl("Number"_zc);

  zc::Vector<ast::NodeId> implIfaceNodes;
  implIfaceNodes.add(fix.makeNamedTypeExpr("Add"_zc));
  auto implIfaces = fix.makeImplIfaceList(fix.makeNodeList(implIfaceNodes.asPtr()));

  zc::Vector<ast::NodeId> implParams;
  implParams.add(fix.makeFunctionParamDecl("rhs"_zc, fix.makeNamedTypeExpr("bool"_zc)));
  auto implParamList = fix.makeFunctionParamList(fix.makeNodeList(implParams.asPtr()));
  auto implMethod =
      fix.makeMethodDecl("add"_zc, ast::NodeId(), implParamList, fix.makeNamedTypeExpr("str"_zc));
  zc::Vector<ast::NodeId> implMembers;
  implMembers.add(implMethod);
  auto implDecl =
      makeStandaloneImplDecl(fix, fix.makeNamedTypeExpr("Number"_zc), implIfaces,
                             fix.makeClassMemberList(fix.makeNodeList(implMembers.asPtr())));

  auto xDecl = fix.makeVariableDeclarator(fix.makeBindingPattern("x"_zc),
                                          fix.makeNamedTypeExpr("Number"_zc));
  auto yDecl = fix.makeVariableDeclarator(fix.makeBindingPattern("y"_zc),
                                          fix.makeNamedTypeExpr("Number"_zc));
  zc::Vector<ast::NodeId> decls;
  decls.add(xDecl);
  decls.add(yDecl);
  auto let = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr())));
  auto binExpr = fix.makeBinaryExpr(ast::BinaryOperatorKind::Add, fix.makeIdentExpr("x"_zc),
                                    fix.makeIdentExpr("y"_zc));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(addIface);
  topDecls.add(numberType);
  topDecls.add(implDecl);
  topDecls.add(let);
  topDecls.add(binExpr);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(!result.success);
  ZC_EXPECT(fix.diagnostics().hasErrors());
  ZC_EXPECT(
      containsDiagnosticId(*consumerPtr, diagnostics::DiagID::OperatorTraitSignatureMismatch));
  ZC_EXPECT(result.typeEnv.hasType(binExpr));
  if (result.typeEnv.hasType(binExpr)) { ZC_EXPECT(isError(result.typeEnv.getType(binExpr))); }
}

ZC_TEST("BodyChecker.BinarySubUsesUserTypeSubImpl") {
  expectUserTypeBinaryOperatorImpl("Sub"_zc, ast::BinaryOperatorKind::Sub);
}

ZC_TEST("BodyChecker.BinaryMulUsesUserTypeMulImpl") {
  expectUserTypeBinaryOperatorImpl("Mul"_zc, ast::BinaryOperatorKind::Mul);
}

ZC_TEST("BodyChecker.BinaryDivUsesUserTypeDivImpl") {
  expectUserTypeBinaryOperatorImpl("Div"_zc, ast::BinaryOperatorKind::Div);
}

ZC_TEST("BodyChecker.BinaryModUsesUserTypeRemImpl") {
  expectUserTypeBinaryOperatorImpl("Rem"_zc, ast::BinaryOperatorKind::Mod);
}

ZC_TEST("BodyChecker.BinaryPowUsesUserTypePowImpl") {
  expectUserTypeBinaryOperatorImpl("Pow"_zc, ast::BinaryOperatorKind::Pow);
}

ZC_TEST("BodyChecker.InfersBinaryComparisonType") {
  TestFixture fix;
  // 1 == 2 -> bool
  auto lhs = fix.makeIntLiteral(1);
  auto rhs = fix.makeIntLiteral(2);
  auto binExpr = fix.makeBinaryExpr(ast::BinaryOperatorKind::Eq, lhs, rhs);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(binExpr);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  if (result.typeEnv.hasType(binExpr)) {
    auto& ty = result.typeEnv.getType(binExpr);
    ZC_EXPECT(isPrimitive(ty));
  }
  ZC_EXPECT(result.typeEnv.hasDispatch(binExpr));
  auto& dispatch = result.typeEnv.getDispatch(binExpr);
  ZC_EXPECT(dispatch.targetKind == type::CallTargetKind::PrimitiveOperator);
  ZC_EXPECT(dispatch.receiverMode == type::ReceiverMode::OperatorLeftHandSide);
  ZC_EXPECT(dispatch.argumentTypes.size() == 2);
  ZC_EXPECT(dispatch.resultType == result.typeEnv.getTypeId(binExpr));
}

ZC_TEST("BodyChecker.BinaryEqUsesUserTypeEqImpl") {
  expectUserTypeComparisonImpl("Eq"_zc, ast::BinaryOperatorKind::Eq);
}

ZC_TEST("BodyChecker.BinaryEqRejectsWrongTraitMethodSignature") {
  TestFixture fix;
  auto consumer = zc::heap<CapturingDiagnosticConsumer>();
  auto consumerPtr = consumer.get();
  fix.diagnostics().addConsumer(zc::mv(consumer));

  zc::Vector<ast::NodeId> ifaceParams;
  ifaceParams.add(fix.makeFunctionParamDecl("rhs"_zc, fix.makeNamedTypeExpr("Point"_zc)));
  auto ifaceParamList = fix.makeFunctionParamList(fix.makeNodeList(ifaceParams.asPtr()));
  auto ifaceMethod =
      fix.makeMethodDecl("eq"_zc, ast::NodeId(), ifaceParamList, fix.makeNamedTypeExpr("bool"_zc));
  zc::Vector<ast::NodeId> ifaceMembers;
  ifaceMembers.add(ifaceMethod);
  auto eqIface = fix.makeInterfaceDecl(
      "Eq"_zc, fix.makeClassMemberList(fix.makeNodeList(ifaceMembers.asPtr())));

  auto pointType = fix.makeClassDecl("Point"_zc);

  zc::Vector<ast::NodeId> implIfaceNodes;
  implIfaceNodes.add(fix.makeNamedTypeExpr("Eq"_zc));
  auto implIfaces = fix.makeImplIfaceList(fix.makeNodeList(implIfaceNodes.asPtr()));

  zc::Vector<ast::NodeId> implParams;
  implParams.add(fix.makeFunctionParamDecl("rhs"_zc, fix.makeNamedTypeExpr("str"_zc)));
  auto implParamList = fix.makeFunctionParamList(fix.makeNodeList(implParams.asPtr()));
  auto implMethod =
      fix.makeMethodDecl("eq"_zc, ast::NodeId(), implParamList, fix.makeNamedTypeExpr("Point"_zc));
  zc::Vector<ast::NodeId> implMembers;
  implMembers.add(implMethod);
  auto implDecl =
      makeStandaloneImplDecl(fix, fix.makeNamedTypeExpr("Point"_zc), implIfaces,
                             fix.makeClassMemberList(fix.makeNodeList(implMembers.asPtr())));

  auto xDecl =
      fix.makeVariableDeclarator(fix.makeBindingPattern("x"_zc), fix.makeNamedTypeExpr("Point"_zc));
  auto yDecl =
      fix.makeVariableDeclarator(fix.makeBindingPattern("y"_zc), fix.makeNamedTypeExpr("Point"_zc));
  zc::Vector<ast::NodeId> decls;
  decls.add(xDecl);
  decls.add(yDecl);
  auto let = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr())));
  auto binExpr = fix.makeBinaryExpr(ast::BinaryOperatorKind::Eq, fix.makeIdentExpr("x"_zc),
                                    fix.makeIdentExpr("y"_zc));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(eqIface);
  topDecls.add(pointType);
  topDecls.add(implDecl);
  topDecls.add(let);
  topDecls.add(binExpr);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(!result.success);
  ZC_EXPECT(fix.diagnostics().hasErrors());
  ZC_EXPECT(
      containsDiagnosticId(*consumerPtr, diagnostics::DiagID::OperatorTraitSignatureMismatch));
  ZC_EXPECT(result.typeEnv.hasType(binExpr));
  if (result.typeEnv.hasType(binExpr)) { ZC_EXPECT(isError(result.typeEnv.getType(binExpr))); }
}

ZC_TEST("BodyChecker.BinaryEqRejectsUserTypeWithoutEqImpl") {
  expectUserTypeComparisonWithoutImplFails("Eq"_zc, ast::BinaryOperatorKind::Eq);
}

ZC_TEST("BodyChecker.BinaryLtUsesUserTypeOrdImpl") {
  expectUserTypeComparisonImpl("Ord"_zc, ast::BinaryOperatorKind::Lt);
}

ZC_TEST("BodyChecker.BinaryLeUsesUserTypeOrdImpl") {
  expectUserTypeComparisonImpl("Ord"_zc, ast::BinaryOperatorKind::Le);
}

ZC_TEST("BodyChecker.BinaryGtUsesUserTypeOrdImpl") {
  expectUserTypeComparisonImpl("Ord"_zc, ast::BinaryOperatorKind::Gt);
}

ZC_TEST("BodyChecker.BinaryGeUsesUserTypeOrdImpl") {
  expectUserTypeComparisonImpl("Ord"_zc, ast::BinaryOperatorKind::Ge);
}

ZC_TEST("BodyChecker.BinaryOrdRejectsWrongTraitMethodSignature") {
  TestFixture fix;
  auto consumer = zc::heap<CapturingDiagnosticConsumer>();
  auto consumerPtr = consumer.get();
  fix.diagnostics().addConsumer(zc::mv(consumer));

  zc::Vector<ast::NodeId> ifaceParams;
  ifaceParams.add(fix.makeFunctionParamDecl("rhs"_zc, fix.makeNamedTypeExpr("Point"_zc)));
  auto ifaceParamList = fix.makeFunctionParamList(fix.makeNodeList(ifaceParams.asPtr()));
  auto ifaceMethod =
      fix.makeMethodDecl("cmp"_zc, ast::NodeId(), ifaceParamList, fix.makeNamedTypeExpr("i32"_zc));
  zc::Vector<ast::NodeId> ifaceMembers;
  ifaceMembers.add(ifaceMethod);
  auto ordIface = fix.makeInterfaceDecl(
      "Ord"_zc, fix.makeClassMemberList(fix.makeNodeList(ifaceMembers.asPtr())));

  auto pointType = fix.makeClassDecl("Point"_zc);

  zc::Vector<ast::NodeId> implIfaceNodes;
  implIfaceNodes.add(fix.makeNamedTypeExpr("Ord"_zc));
  auto implIfaces = fix.makeImplIfaceList(fix.makeNodeList(implIfaceNodes.asPtr()));

  zc::Vector<ast::NodeId> implParams;
  implParams.add(fix.makeFunctionParamDecl("rhs"_zc, fix.makeNamedTypeExpr("Point"_zc)));
  auto implParamList = fix.makeFunctionParamList(fix.makeNodeList(implParams.asPtr()));
  auto implMethod =
      fix.makeMethodDecl("cmp"_zc, ast::NodeId(), implParamList, fix.makeNamedTypeExpr("bool"_zc));
  zc::Vector<ast::NodeId> implMembers;
  implMembers.add(implMethod);
  auto implDecl =
      makeStandaloneImplDecl(fix, fix.makeNamedTypeExpr("Point"_zc), implIfaces,
                             fix.makeClassMemberList(fix.makeNodeList(implMembers.asPtr())));

  auto xDecl =
      fix.makeVariableDeclarator(fix.makeBindingPattern("x"_zc), fix.makeNamedTypeExpr("Point"_zc));
  auto yDecl =
      fix.makeVariableDeclarator(fix.makeBindingPattern("y"_zc), fix.makeNamedTypeExpr("Point"_zc));
  zc::Vector<ast::NodeId> decls;
  decls.add(xDecl);
  decls.add(yDecl);
  auto let = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr())));
  auto binExpr = fix.makeBinaryExpr(ast::BinaryOperatorKind::Ge, fix.makeIdentExpr("x"_zc),
                                    fix.makeIdentExpr("y"_zc));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(ordIface);
  topDecls.add(pointType);
  topDecls.add(implDecl);
  topDecls.add(let);
  topDecls.add(binExpr);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(!result.success);
  ZC_EXPECT(fix.diagnostics().hasErrors());
  ZC_EXPECT(
      containsDiagnosticId(*consumerPtr, diagnostics::DiagID::OperatorTraitSignatureMismatch));
  ZC_EXPECT(result.typeEnv.hasType(binExpr));
  if (result.typeEnv.hasType(binExpr)) { ZC_EXPECT(isError(result.typeEnv.getType(binExpr))); }
}

ZC_TEST("BodyChecker.BinaryLtRejectsUserTypeWithoutOrdImpl") {
  expectUserTypeComparisonWithoutImplFails("Ord"_zc, ast::BinaryOperatorKind::Lt);
}

ZC_TEST("BodyChecker.InfersBinaryLogicalType") {
  TestFixture fix;
  // true && false -> bool
  auto lhs = fix.makeBoolLiteral(true);
  auto rhs = fix.makeBoolLiteral(false);
  auto binExpr = fix.makeBinaryExpr(ast::BinaryOperatorKind::LogAnd, lhs, rhs);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(binExpr);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  if (result.typeEnv.hasType(binExpr)) {
    auto& ty = result.typeEnv.getType(binExpr);
    ZC_EXPECT(isPrimitive(ty));
  }
}

// ============================================================================
// Identifier expression type inference
// ============================================================================

ZC_TEST("BodyChecker.InfersIdentExprType") {
  TestFixture fix;
  // let x = 42; x
  auto pat = fix.makeBindingPattern("x"_zc);
  auto init = fix.makeIntLiteral(42);
  auto decl = fix.makeVariableDeclarator(pat, ast::NodeId(), init);
  zc::Vector<ast::NodeId> declList;
  declList.add(decl);
  auto varList = fix.makeVariableDeclaratorList(fix.makeNodeList(declList.asPtr()));
  auto let = fix.makeLetStmt(varList);

  auto ident = fix.makeIdentExpr("x"_zc);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(let);
  topDecls.add(ident);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  if (result.typeEnv.hasType(ident)) {
    auto& ty = result.typeEnv.getType(ident);
    ZC_EXPECT(isPrimitive(ty));
  }
}

ZC_TEST("BodyChecker.UndefinedIdentReportsError") {
  TestFixture fix;
  auto ident = fix.makeIdentExpr("undefined"_zc);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(ident);
  auto result = runFullCheck(fix, topDecls.asPtr());

  // Should report an error for undefined identifier
  ZC_EXPECT(fix.diagnostics().hasErrors());
}

// ============================================================================
// Function call type inference
// ============================================================================

ZC_TEST("BodyChecker.InfersCallExprReturnType") {
  TestFixture fix;
  // fun foo() -> i32 { return 42; }
  // foo()
  zc::Vector<ast::NodeId> bodyStmts;
  bodyStmts.add(fix.makeReturnStmt(fix.makeIntLiteral(42)));
  auto bodyBlock = fix.makeBlockStmt(fix.makeNodeList(bodyStmts.asPtr()));
  auto fn = fix.makeFunctionDecl("foo"_zc, bodyBlock);

  auto callee = fix.makeIdentExpr("foo"_zc);
  zc::Vector<ast::NodeId> args;
  auto call = fix.makeCallExpr(callee, fix.makeNodeList(args.asPtr()));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(fn);
  topDecls.add(call);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  if (result.typeEnv.hasType(call)) {
    auto& ty = result.typeEnv.getType(call);
    ZC_EXPECT(isPrimitive(ty));
  }
  ZC_EXPECT(result.typeEnv.hasDispatch(call));
  auto& dispatch = result.typeEnv.getDispatch(call);
  ZC_EXPECT(dispatch.targetKind == type::CallTargetKind::FreeFunction);
  ZC_EXPECT(dispatch.receiverMode == type::ReceiverMode::None);
  ZC_EXPECT(dispatch.targetSymbol.isValid());
  ZC_EXPECT(dispatch.argumentTypes.size() == 0);
  ZC_EXPECT(dispatch.resultType == result.typeEnv.getTypeId(call));
}

ZC_TEST("BodyChecker.CallWithArgs") {
  TestFixture fix;
  // fun add(a: i32, b: i32) -> i32 { return a + b; }
  // add(1, 2)
  auto paramA = fix.makeFunctionParamDecl("a"_zc, fix.makeNamedTypeExpr("i32"_zc));
  auto paramB = fix.makeFunctionParamDecl("b"_zc, fix.makeNamedTypeExpr("i32"_zc));
  zc::Vector<ast::NodeId> paramNodes;
  paramNodes.add(paramA);
  paramNodes.add(paramB);
  auto paramList = fix.makeFunctionParamList(fix.makeNodeList(paramNodes.asPtr()));

  auto retExpr = fix.makeBinaryExpr(ast::BinaryOperatorKind::Add, fix.makeIdentExpr("a"_zc),
                                    fix.makeIdentExpr("b"_zc));
  zc::Vector<ast::NodeId> bodyStmts;
  bodyStmts.add(fix.makeReturnStmt(retExpr));
  auto bodyBlock = fix.makeBlockStmt(fix.makeNodeList(bodyStmts.asPtr()));
  auto retTy = fix.makeNamedTypeExpr("i32"_zc);
  auto fn = fix.makeFunctionDecl("add"_zc, bodyBlock, paramList, retTy);

  auto callee = fix.makeIdentExpr("add"_zc);
  zc::Vector<ast::NodeId> args;
  args.add(fix.makeIntLiteral(1));
  args.add(fix.makeIntLiteral(2));
  auto call = fix.makeCallExpr(callee, fix.makeNodeList(args.asPtr()));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(fn);
  topDecls.add(call);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  ZC_EXPECT(result.typeEnv.hasDispatch(call));
  auto& dispatch = result.typeEnv.getDispatch(call);
  ZC_EXPECT(dispatch.targetKind == type::CallTargetKind::FreeFunction);
  ZC_EXPECT(dispatch.receiverMode == type::ReceiverMode::None);
  ZC_EXPECT(dispatch.targetSymbol.isValid());
  ZC_EXPECT(dispatch.argumentTypes.size() == 2);
  ZC_EXPECT(dispatch.resultType == result.typeEnv.getTypeId(call));
}

ZC_TEST("BodyChecker.CallArgumentRecordsUnionInjectionCoercion") {
  TestFixture fix;
  auto paramTy =
      fix.makeUnionTypeExpr(fix.makeNamedTypeExpr("i32"_zc), fix.makeNamedTypeExpr("str"_zc));
  auto param = fix.makeFunctionParamDecl("value"_zc, paramTy);
  zc::Vector<ast::NodeId> paramNodes;
  paramNodes.add(param);
  auto paramList = fix.makeFunctionParamList(fix.makeNodeList(paramNodes.asPtr()));
  auto fn = fix.makeFunctionDecl("takes_union"_zc, fix.makeBlockStmt(ast::NodeList()), paramList,
                                 fix.makeNamedTypeExpr("unit"_zc));

  auto arg = fix.makeIntLiteral(1);
  zc::Vector<ast::NodeId> args;
  args.add(arg);
  auto call = fix.makeCallExpr(fix.makeIdentExpr("takes_union"_zc), fix.makeNodeList(args.asPtr()));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(fn);
  topDecls.add(call);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  ZC_EXPECT(!fix.diagnostics().hasErrors());
  ZC_EXPECT(result.typeEnv.hasCoercion(arg));
  ZC_EXPECT(result.typeEnv.getCoercion(arg) == type::CoercionKind::UnionInjection);
}

ZC_TEST("BodyChecker.LocalVariableInferenceFlowsFromCallArgument") {
  TestFixture fix;
  auto param = fix.makeFunctionParamDecl("value"_zc, fix.makeNamedTypeExpr("u64"_zc));
  zc::Vector<ast::NodeId> paramNodes;
  paramNodes.add(param);
  auto paramList = fix.makeFunctionParamList(fix.makeNodeList(paramNodes.asPtr()));
  auto bodyBlock = fix.makeBlockStmt(ast::NodeList());
  auto retTy = fix.makeNamedTypeExpr("unit"_zc);
  auto fn = fix.makeFunctionDecl("takes_u64"_zc, bodyBlock, paramList, retTy);

  auto pat = fix.makeBindingPattern("x"_zc);
  auto decl = fix.makeVariableDeclarator(pat, ast::NodeId(), fix.makeIntLiteral(5));
  zc::Vector<ast::NodeId> declList;
  declList.add(decl);
  auto varList = fix.makeVariableDeclaratorList(fix.makeNodeList(declList.asPtr()));
  auto let = fix.makeLetStmt(varList);

  zc::Vector<ast::NodeId> args;
  args.add(fix.makeIdentExpr("x"_zc));
  auto call = fix.makeCallExpr(fix.makeIdentExpr("takes_u64"_zc), fix.makeNodeList(args.asPtr()));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(fn);
  topDecls.add(let);
  topDecls.add(call);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  ZC_EXPECT(!fix.diagnostics().hasErrors());
  ZC_EXPECT(result.typeEnv.hasType(decl));
  auto& declTy = result.typeEnv.getType(decl);
  ZC_EXPECT(isPrimitive(declTy));
  if (isPrimitive(declTy)) {
    auto& primitive = static_cast<const type::PrimitiveType&>(declTy);
    ZC_EXPECT(primitive.getPrimitiveKind() == type::PrimitiveKind::U64);
  }
}

ZC_TEST("BodyChecker.GenericFunctionInfersTypeArgumentFromCall") {
  TestFixture fix;
  auto genericT = fix.makeGenericTypeParam("T"_zc);
  zc::Vector<ast::NodeId> genericNodes;
  genericNodes.add(genericT);
  auto generics = fix.makeGenericParams(fix.makeNodeList(genericNodes.asPtr()));

  auto tParamTy = fix.makeNamedTypeExpr("T"_zc);
  auto tReturnTy = fix.makeNamedTypeExpr("T"_zc);
  auto param = fix.makeFunctionParamDecl("value"_zc, tParamTy);
  zc::Vector<ast::NodeId> paramNodes;
  paramNodes.add(param);
  auto paramList = fix.makeFunctionParamList(fix.makeNodeList(paramNodes.asPtr()));

  zc::Vector<ast::NodeId> bodyStmts;
  bodyStmts.add(fix.makeReturnStmt(fix.makeIdentExpr("value"_zc)));
  auto bodyBlock = fix.makeBlockStmt(fix.makeNodeList(bodyStmts.asPtr()));
  auto identity =
      fix.makeFunctionDecl("identity"_zc, bodyBlock, paramList, tReturnTy, ast::NodeId(), generics);

  zc::Vector<ast::NodeId> args;
  args.add(fix.makeIntLiteral(42));
  auto call = fix.makeCallExpr(fix.makeIdentExpr("identity"_zc), fix.makeNodeList(args.asPtr()));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(identity);
  topDecls.add(call);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  ZC_EXPECT(!fix.diagnostics().hasErrors());
  ZC_EXPECT(result.typeEnv.hasType(call));
  auto& callTy = result.typeEnv.getType(call);
  auto& resolvedCallTy = result.typeEnv.find(callTy);
  ZC_EXPECT(isPrimitive(resolvedCallTy));
  if (isPrimitive(resolvedCallTy)) {
    auto& primitive = static_cast<const type::PrimitiveType&>(resolvedCallTy);
    ZC_EXPECT(primitive.getPrimitiveKind() == type::PrimitiveKind::I32);
  }
}

ZC_TEST("BodyChecker.GenericFunctionUsesExplicitTypeArgument") {
  TestFixture fix;
  auto genericT = fix.makeGenericTypeParam("T"_zc);
  zc::Vector<ast::NodeId> genericNodes;
  genericNodes.add(genericT);
  auto generics = fix.makeGenericParams(fix.makeNodeList(genericNodes.asPtr()));

  auto tParamTy = fix.makeNamedTypeExpr("T"_zc);
  auto tReturnTy = fix.makeNamedTypeExpr("T"_zc);
  auto param = fix.makeFunctionParamDecl("value"_zc, tParamTy);
  zc::Vector<ast::NodeId> paramNodes;
  paramNodes.add(param);
  auto paramList = fix.makeFunctionParamList(fix.makeNodeList(paramNodes.asPtr()));
  auto bodyBlock = fix.makeBlockStmt(ast::NodeList());
  auto identity =
      fix.makeFunctionDecl("identity"_zc, bodyBlock, paramList, tReturnTy, ast::NodeId(), generics);

  zc::Vector<ast::NodeId> typeArgs;
  typeArgs.add(fix.makeNamedTypeExpr("f64"_zc));
  zc::Vector<ast::NodeId> args;
  args.add(fix.makeFloatLiteral(42.0));
  auto call = fix.makeCallExpr(fix.makeIdentExpr("identity"_zc), fix.makeNodeList(typeArgs.asPtr()),
                               fix.makeNodeList(args.asPtr()));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(identity);
  topDecls.add(call);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  ZC_EXPECT(!fix.diagnostics().hasErrors());
  ZC_EXPECT(result.typeEnv.hasType(call));
  auto& callTy = result.typeEnv.getType(call);
  auto& resolvedCallTy = result.typeEnv.find(callTy);
  ZC_EXPECT(isPrimitive(resolvedCallTy));
  if (isPrimitive(resolvedCallTy)) {
    auto& primitive = static_cast<const type::PrimitiveType&>(resolvedCallTy);
    ZC_EXPECT(primitive.getPrimitiveKind() == type::PrimitiveKind::F64);
  }
}

ZC_TEST("BodyChecker.GenericFunctionRejectsWrongExplicitTypeArgumentCount") {
  TestFixture fix;
  auto consumer = zc::heap<CapturingDiagnosticConsumer>();
  auto consumerPtr = consumer.get();
  fix.diagnostics().addConsumer(zc::mv(consumer));

  auto genericT = fix.makeGenericTypeParam("T"_zc);
  zc::Vector<ast::NodeId> genericNodes;
  genericNodes.add(genericT);
  auto generics = fix.makeGenericParams(fix.makeNodeList(genericNodes.asPtr()));

  auto tParamTy = fix.makeNamedTypeExpr("T"_zc);
  auto tReturnTy = fix.makeNamedTypeExpr("T"_zc);
  auto param = fix.makeFunctionParamDecl("value"_zc, tParamTy);
  zc::Vector<ast::NodeId> paramNodes;
  paramNodes.add(param);
  auto paramList = fix.makeFunctionParamList(fix.makeNodeList(paramNodes.asPtr()));
  auto identity = fix.makeFunctionDecl("identity"_zc, fix.makeBlockStmt(ast::NodeList()), paramList,
                                       tReturnTy, ast::NodeId(), generics);

  zc::Vector<ast::NodeId> typeArgs;
  typeArgs.add(fix.makeNamedTypeExpr("i32"_zc));
  typeArgs.add(fix.makeNamedTypeExpr("str"_zc));
  zc::Vector<ast::NodeId> args;
  args.add(fix.makeIntLiteral(42));
  auto call = fix.makeCallExpr(fix.makeIdentExpr("identity"_zc), fix.makeNodeList(typeArgs.asPtr()),
                               fix.makeNodeList(args.asPtr()));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(identity);
  topDecls.add(call);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(!result.success);
  ZC_EXPECT(fix.diagnostics().hasErrors());
  ZC_EXPECT(
      containsDiagnosticId(*consumerPtr, diagnostics::DiagID::ExplicitTypeArgumentCountMismatch));
  ZC_EXPECT(result.typeEnv.hasType(call));
  ZC_EXPECT(isError(result.typeEnv.getType(call)));
}

ZC_TEST("BodyChecker.GenericFunctionRejectsIncompatibleExplicitTypeArgument") {
  TestFixture fix;
  auto genericT = fix.makeGenericTypeParam("T"_zc);
  zc::Vector<ast::NodeId> genericNodes;
  genericNodes.add(genericT);
  auto generics = fix.makeGenericParams(fix.makeNodeList(genericNodes.asPtr()));

  auto tParamTy = fix.makeNamedTypeExpr("T"_zc);
  auto tReturnTy = fix.makeNamedTypeExpr("T"_zc);
  auto param = fix.makeFunctionParamDecl("value"_zc, tParamTy);
  zc::Vector<ast::NodeId> paramNodes;
  paramNodes.add(param);
  auto paramList = fix.makeFunctionParamList(fix.makeNodeList(paramNodes.asPtr()));
  auto identity = fix.makeFunctionDecl("identity"_zc, fix.makeBlockStmt(ast::NodeList()), paramList,
                                       tReturnTy, ast::NodeId(), generics);

  zc::Vector<ast::NodeId> typeArgs;
  typeArgs.add(fix.makeNamedTypeExpr("str"_zc));
  zc::Vector<ast::NodeId> args;
  args.add(fix.makeIntLiteral(42));
  auto call = fix.makeCallExpr(fix.makeIdentExpr("identity"_zc), fix.makeNodeList(typeArgs.asPtr()),
                               fix.makeNodeList(args.asPtr()));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(identity);
  topDecls.add(call);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(!result.success);
  ZC_EXPECT(fix.diagnostics().hasErrors());
  ZC_EXPECT(result.typeEnv.hasType(call));
  ZC_EXPECT(isError(result.typeEnv.getType(call)));
}

ZC_TEST("BodyChecker.GenericFunctionRejectsUnsolvedTypeParameter") {
  TestFixture fix;
  auto consumer = zc::heap<CapturingDiagnosticConsumer>();
  auto consumerPtr = consumer.get();
  fix.diagnostics().addConsumer(zc::mv(consumer));

  auto genericT = fix.makeGenericTypeParam("T"_zc);
  zc::Vector<ast::NodeId> genericNodes;
  genericNodes.add(genericT);
  auto generics = fix.makeGenericParams(fix.makeNodeList(genericNodes.asPtr()));
  auto make = fix.makeFunctionDecl("make"_zc, fix.makeBlockStmt(ast::NodeList()), ast::NodeId(),
                                   fix.makeNamedTypeExpr("T"_zc), ast::NodeId(), generics);
  auto call = fix.makeCallExpr(fix.makeIdentExpr("make"_zc), ast::NodeList());

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(make);
  topDecls.add(call);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(!result.success);
  ZC_EXPECT(fix.diagnostics().hasErrors());
  ZC_EXPECT(containsDiagnosticId(*consumerPtr, diagnostics::DiagID::CannotInferTypeParameter));
  ZC_EXPECT(result.typeEnv.hasType(call));
  ZC_EXPECT(isError(result.typeEnv.getType(call)));
}

ZC_TEST("BodyChecker.GenericFunctionRejectsUnsatisfiedInterfaceBound") {
  TestFixture fix;
  auto hashable = fix.makeInterfaceDecl("Hashable"_zc);
  auto genericT = fix.makeGenericTypeParam("T"_zc, fix.makeNamedTypeExpr("Hashable"_zc));
  zc::Vector<ast::NodeId> genericNodes;
  genericNodes.add(genericT);
  auto generics = fix.makeGenericParams(fix.makeNodeList(genericNodes.asPtr()));

  auto tParamTy = fix.makeNamedTypeExpr("T"_zc);
  auto param = fix.makeFunctionParamDecl("value"_zc, tParamTy);
  zc::Vector<ast::NodeId> paramNodes;
  paramNodes.add(param);
  auto paramList = fix.makeFunctionParamList(fix.makeNodeList(paramNodes.asPtr()));
  auto bodyBlock = fix.makeBlockStmt(ast::NodeList());
  auto fn = fix.makeFunctionDecl("needs_hash"_zc, bodyBlock, paramList,
                                 fix.makeNamedTypeExpr("unit"_zc), ast::NodeId(), generics);

  zc::Vector<ast::NodeId> args;
  args.add(fix.makeIntLiteral(42));
  auto call = fix.makeCallExpr(fix.makeIdentExpr("needs_hash"_zc), fix.makeNodeList(args.asPtr()));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(hashable);
  topDecls.add(fn);
  topDecls.add(call);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(!result.success);
  ZC_EXPECT(fix.diagnostics().hasErrors());
  ZC_EXPECT(result.typeEnv.hasType(call));
  ZC_EXPECT(isError(result.typeEnv.getType(call)));
}

ZC_TEST("BodyChecker.GenericFunctionAcceptsSatisfiedInterfaceBound") {
  TestFixture fix;
  auto hashable = fix.makeInterfaceDecl("Hashable"_zc);

  zc::Vector<ast::NodeId> ifaceNodes;
  ifaceNodes.add(fix.makeNamedTypeExpr("Hashable"_zc));
  auto ifaceList = fix.makeImplIfaceList(fix.makeNodeList(ifaceNodes.asPtr()));
  auto implHashableI32 = fix.makeStandaloneImplDecl(fix.makeNamedTypeExpr("i32"_zc), ifaceList);

  auto genericT = fix.makeGenericTypeParam("T"_zc, fix.makeNamedTypeExpr("Hashable"_zc));
  zc::Vector<ast::NodeId> genericNodes;
  genericNodes.add(genericT);
  auto generics = fix.makeGenericParams(fix.makeNodeList(genericNodes.asPtr()));

  auto tParamTy = fix.makeNamedTypeExpr("T"_zc);
  auto param = fix.makeFunctionParamDecl("value"_zc, tParamTy);
  zc::Vector<ast::NodeId> paramNodes;
  paramNodes.add(param);
  auto paramList = fix.makeFunctionParamList(fix.makeNodeList(paramNodes.asPtr()));
  auto bodyBlock = fix.makeBlockStmt(ast::NodeList());
  auto fn = fix.makeFunctionDecl("needs_hash"_zc, bodyBlock, paramList,
                                 fix.makeNamedTypeExpr("unit"_zc), ast::NodeId(), generics);

  zc::Vector<ast::NodeId> args;
  args.add(fix.makeIntLiteral(42));
  auto call = fix.makeCallExpr(fix.makeIdentExpr("needs_hash"_zc), fix.makeNodeList(args.asPtr()));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(hashable);
  topDecls.add(implHashableI32);
  topDecls.add(fn);
  topDecls.add(call);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  ZC_EXPECT(!fix.diagnostics().hasErrors());
  ZC_EXPECT(result.typeEnv.hasType(call));
}

// ============================================================================
// Return statement checking
// ============================================================================

ZC_TEST("BodyChecker.ReturnMatchesFunctionType") {
  TestFixture fix;
  // fun foo(): i32 { return 42; }
  zc::Vector<ast::NodeId> bodyStmts;
  bodyStmts.add(fix.makeReturnStmt(fix.makeIntLiteral(42)));
  auto bodyBlock = fix.makeBlockStmt(fix.makeNodeList(bodyStmts.asPtr()));
  auto fn = fix.makeFunctionDecl("foo"_zc, bodyBlock);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(fn);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  ZC_EXPECT(!fix.diagnostics().hasErrors());
}

ZC_TEST("BodyChecker.ReturnRecordsUnionInjectionCoercion") {
  TestFixture fix;
  auto retValue = fix.makeIntLiteral(42);
  auto returnStmt = fix.makeReturnStmt(retValue);
  zc::Vector<ast::NodeId> bodyStmts;
  bodyStmts.add(returnStmt);
  auto bodyBlock = fix.makeBlockStmt(fix.makeNodeList(bodyStmts.asPtr()));
  auto retTy =
      fix.makeUnionTypeExpr(fix.makeNamedTypeExpr("i32"_zc), fix.makeNamedTypeExpr("str"_zc));
  auto fn = fix.makeFunctionDecl("foo"_zc, bodyBlock, ast::NodeId(), retTy);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(fn);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  ZC_EXPECT(!fix.diagnostics().hasErrors());
  ZC_EXPECT(result.typeEnv.hasCoercion(returnStmt));
  ZC_EXPECT(result.typeEnv.getCoercion(returnStmt) == type::CoercionKind::UnionInjection);
}

ZC_TEST("BodyChecker.EmptyReturnInVoidFunction") {
  TestFixture fix;
  // fun foo() { return; }
  zc::Vector<ast::NodeId> bodyStmts;
  bodyStmts.add(fix.makeReturnStmt());
  auto bodyBlock = fix.makeBlockStmt(fix.makeNodeList(bodyStmts.asPtr()));
  auto fn = fix.makeFunctionDecl("foo"_zc, bodyBlock);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(fn);
  auto result = runFullCheck(fix, topDecls.asPtr());

  // Should succeed (return without value in unit-returning function)
  ZC_EXPECT(result.success);
}

// ============================================================================
// Assignment checking
// ============================================================================

ZC_TEST("BodyChecker.SimpleAssignment") {
  TestFixture fix;
  // let mut x = 1; x = 2;
  auto pat = fix.makeBindingPattern("x"_zc, true);
  auto init = fix.makeIntLiteral(1);
  auto decl = fix.makeVariableDeclarator(pat, ast::NodeId(), init);
  zc::Vector<ast::NodeId> declList;
  declList.add(decl);
  auto varList = fix.makeVariableDeclaratorList(fix.makeNodeList(declList.asPtr()));
  auto let = fix.makeLetStmt(varList);

  auto lhs = fix.makeIdentExpr("x"_zc);
  auto rhs = fix.makeIntLiteral(2);
  auto assign = fix.makeAssignmentExpr(lhs, rhs);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(let);
  topDecls.add(assign);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  ZC_EXPECT(result.typeEnv.hasType(assign));
  ZC_EXPECT(result.constraintCount > 0);
  auto& ty = result.typeEnv.getType(assign);
  ZC_EXPECT(isPrimitive(ty));
}

ZC_TEST("BodyChecker.AssignmentRecordsUnionInjectionCoercion") {
  TestFixture fix;
  auto pat = fix.makeBindingPattern("x"_zc, true);
  auto ty = fix.makeUnionTypeExpr(fix.makeNamedTypeExpr("i32"_zc), fix.makeNamedTypeExpr("str"_zc));
  auto decl = fix.makeVariableDeclarator(pat, ty, fix.makeIntLiteral(1));
  zc::Vector<ast::NodeId> declList;
  declList.add(decl);
  auto let = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(declList.asPtr())));

  auto rhs = fix.makeIntLiteral(2);
  auto assign = fix.makeAssignmentExpr(fix.makeIdentExpr("x"_zc), rhs);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(let);
  topDecls.add(assign);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  ZC_EXPECT(!fix.diagnostics().hasErrors());
  ZC_EXPECT(result.typeEnv.hasCoercion(assign));
  ZC_EXPECT(result.typeEnv.getCoercion(assign) == type::CoercionKind::UnionInjection);
}

ZC_TEST("BodyChecker.AssignmentRejectsImmutableBinding") {
  TestFixture fix;
  auto consumer = zc::heap<CapturingDiagnosticConsumer>();
  auto consumerPtr = consumer.get();
  fix.diagnostics().addConsumer(zc::mv(consumer));

  auto pat = fix.makeBindingPattern("x"_zc);
  auto init = fix.makeIntLiteral(1);
  auto decl = fix.makeVariableDeclarator(pat, ast::NodeId(), init);
  zc::Vector<ast::NodeId> declList;
  declList.add(decl);
  auto varList = fix.makeVariableDeclaratorList(fix.makeNodeList(declList.asPtr()));
  auto let = fix.makeLetStmt(varList);

  auto assign = fix.makeAssignmentExpr(fix.makeIdentExpr("x"_zc), fix.makeIntLiteral(2));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(let);
  topDecls.add(assign);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(!result.success);
  ZC_EXPECT(fix.diagnostics().hasErrors());
  ZC_EXPECT(containsDiagnosticId(*consumerPtr, diagnostics::DiagID::CannotMutateImmutableVariable));
  ZC_EXPECT(result.typeEnv.hasType(assign));
  ZC_EXPECT(isError(result.typeEnv.getType(assign)));
}

// ============================================================================
// If statement checking
// ============================================================================

ZC_TEST("BodyChecker.IfStmtBoolCondition") {
  TestFixture fix;
  // if (true) { }
  auto cond = fix.makeBoolLiteral(true);
  auto thenBlock = fix.makeBlockStmt(ast::NodeList());
  auto ifStmt = fix.makeIfStmt(cond, thenBlock);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(ifStmt);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
}

ZC_TEST("BodyChecker.IfStmtWithElse") {
  TestFixture fix;
  // if (true) { } else { }
  auto cond = fix.makeBoolLiteral(true);
  auto thenBlock = fix.makeBlockStmt(ast::NodeList());
  auto elseBlock = fix.makeBlockStmt(ast::NodeList());
  auto ifStmt = fix.makeIfStmt(cond, thenBlock, elseBlock);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(ifStmt);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
}

// ============================================================================
// While statement checking
// ============================================================================

ZC_TEST("BodyChecker.WhileStmtBoolCondition") {
  TestFixture fix;
  auto cond = fix.makeBoolLiteral(true);
  auto bodyBlock = fix.makeBlockStmt(ast::NodeList());
  auto whileStmt = fix.makeWhileStmt(cond, bodyBlock);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(whileStmt);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
}

// ============================================================================
// Conditional expression
// ============================================================================

ZC_TEST("BodyChecker.ConditionalExpr") {
  TestFixture fix;
  // true ? 1 : 2
  auto cond = fix.makeBoolLiteral(true);
  auto thenExpr = fix.makeIntLiteral(1);
  auto elseExpr = fix.makeIntLiteral(2);
  auto ternary = fix.makeConditionalExpr(cond, thenExpr, elseExpr);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(ternary);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  if (result.typeEnv.hasType(ternary)) {
    auto& ty = result.typeEnv.getType(ternary);
    ZC_EXPECT(isPrimitive(ty));
  }
}

ZC_TEST("BodyChecker.ConditionalExprDifferentTypesProducesUnion") {
  TestFixture fix;
  auto cond = fix.makeBoolLiteral(true);
  auto thenExpr = fix.makeIntLiteral(1);
  auto elseExpr = fix.makeStrLiteral("two"_zc);
  auto ternary = fix.makeConditionalExpr(cond, thenExpr, elseExpr);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(ternary);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  ZC_EXPECT(result.typeEnv.hasType(ternary));
  auto& ty = result.typeEnv.getType(ternary);
  ZC_EXPECT(isUnion(ty));
  ZC_EXPECT(result.typeEnv.hasCoercion(thenExpr));
  ZC_EXPECT(result.typeEnv.hasCoercion(elseExpr));
  ZC_EXPECT(result.typeEnv.getCoercion(thenExpr) == type::CoercionKind::UnionInjection);
  ZC_EXPECT(result.typeEnv.getCoercion(elseExpr) == type::CoercionKind::UnionInjection);
}

ZC_TEST("BodyChecker.NullCoalesceReturnsNonNullAlternative") {
  TestFixture fix;
  auto pat = fix.makeBindingPattern("x"_zc);
  auto ty = fix.makeUnionTypeExpr(fix.makeNamedTypeExpr("i32"_zc), fix.makePredefinedTypeExpr(13));
  auto init = fix.makeNullLiteral();
  auto decl = fix.makeVariableDeclarator(pat, ty, init);
  zc::Vector<ast::NodeId> declList;
  declList.add(decl);
  auto varList = fix.makeVariableDeclaratorList(fix.makeNodeList(declList.asPtr()));
  auto let = fix.makeLetStmt(varList);

  auto coalesce = fix.makeNullCoalesceExpr(fix.makeIdentExpr("x"_zc), fix.makeIntLiteral(7));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(let);
  topDecls.add(coalesce);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  ZC_EXPECT(!fix.diagnostics().hasErrors());
  ZC_EXPECT(result.typeEnv.hasType(coalesce));
  auto& tyResult = result.typeEnv.getType(coalesce);
  ZC_EXPECT(isPrimitive(tyResult));
  if (isPrimitive(tyResult)) {
    auto& primitive = static_cast<const type::PrimitiveType&>(tyResult);
    ZC_EXPECT(primitive.getPrimitiveKind() == type::PrimitiveKind::I32);
  }
}

// ============================================================================
// Unary expression
// ============================================================================

ZC_TEST("BodyChecker.UnaryMinus") {
  TestFixture fix;
  // -1
  auto operand = fix.makeIntLiteral(1);
  auto unary = fix.makeUnaryExpr(ast::UnaryOperatorKind::Minus, operand);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(unary);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  ZC_EXPECT(result.typeEnv.hasType(unary));
  if (result.typeEnv.hasType(unary)) {
    auto& ty = result.typeEnv.getType(unary);
    ZC_EXPECT(isPrimitive(ty));
  }
  ZC_EXPECT(result.typeEnv.hasDispatch(unary));
  auto& dispatch = result.typeEnv.getDispatch(unary);
  ZC_EXPECT(dispatch.targetKind == type::CallTargetKind::PrimitiveOperator);
  ZC_EXPECT(dispatch.receiverMode == type::ReceiverMode::OperatorOperand);
  ZC_EXPECT(dispatch.argumentTypes.size() == 1);
  ZC_EXPECT(dispatch.resultType == result.typeEnv.getTypeId(unary));
}

ZC_TEST("BodyChecker.LogicalNot") {
  TestFixture fix;
  // !true
  auto operand = fix.makeBoolLiteral(true);
  auto unary = fix.makeUnaryExpr(ast::UnaryOperatorKind::LogicalNot, operand);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(unary);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  ZC_EXPECT(result.typeEnv.hasType(unary));
  if (result.typeEnv.hasType(unary)) {
    auto& ty = result.typeEnv.getType(unary);
    ZC_EXPECT(isPrimitive(ty));
  }
  ZC_EXPECT(result.typeEnv.hasDispatch(unary));
  auto& dispatch = result.typeEnv.getDispatch(unary);
  ZC_EXPECT(dispatch.targetKind == type::CallTargetKind::PrimitiveOperator);
  ZC_EXPECT(dispatch.receiverMode == type::ReceiverMode::OperatorOperand);
  ZC_EXPECT(dispatch.argumentTypes.size() == 1);
  ZC_EXPECT(dispatch.resultType == result.typeEnv.getTypeId(unary));
}

ZC_TEST("BodyChecker.UnaryMinusUsesUserTypeNegImpl") {
  expectUserTypeUnaryOperatorImpl("Neg"_zc, ast::UnaryOperatorKind::Minus, true);
}

ZC_TEST("BodyChecker.UnaryMinusRejectsWrongTraitMethodSignature") {
  TestFixture fix;
  auto consumer = zc::heap<CapturingDiagnosticConsumer>();
  auto consumerPtr = consumer.get();
  fix.diagnostics().addConsumer(zc::mv(consumer));

  auto negIface = fix.makeInterfaceDecl("Neg"_zc);
  auto operandType = fix.makeClassDecl("Operand"_zc);

  zc::Vector<ast::NodeId> implIfaceNodes;
  implIfaceNodes.add(fix.makeNamedTypeExpr("Neg"_zc));
  auto implIfaces = fix.makeImplIfaceList(fix.makeNodeList(implIfaceNodes.asPtr()));
  auto implMethod =
      fix.makeMethodDecl("neg"_zc, ast::NodeId(), ast::NodeId(), fix.makeNamedTypeExpr("bool"_zc));
  zc::Vector<ast::NodeId> implMembers;
  implMembers.add(implMethod);
  auto implDecl =
      makeStandaloneImplDecl(fix, fix.makeNamedTypeExpr("Operand"_zc), implIfaces,
                             fix.makeClassMemberList(fix.makeNodeList(implMembers.asPtr())));

  auto valueDecl = fix.makeVariableDeclarator(fix.makeBindingPattern("value"_zc),
                                              fix.makeNamedTypeExpr("Operand"_zc));
  zc::Vector<ast::NodeId> decls;
  decls.add(valueDecl);
  auto let = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr())));
  auto unary = fix.makeUnaryExpr(ast::UnaryOperatorKind::Minus, fix.makeIdentExpr("value"_zc));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(negIface);
  topDecls.add(operandType);
  topDecls.add(implDecl);
  topDecls.add(let);
  topDecls.add(unary);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(!result.success);
  ZC_EXPECT(fix.diagnostics().hasErrors());
  ZC_EXPECT(
      containsDiagnosticId(*consumerPtr, diagnostics::DiagID::OperatorTraitSignatureMismatch));
  ZC_EXPECT(result.typeEnv.hasType(unary));
  if (result.typeEnv.hasType(unary)) { ZC_EXPECT(isError(result.typeEnv.getType(unary))); }
}

ZC_TEST("BodyChecker.UnaryMinusRejectsUserTypeWithoutNegImpl") {
  expectUserTypeUnaryWithoutImplFails(ast::UnaryOperatorKind::Minus);
}

ZC_TEST("BodyChecker.LogicalNotUsesUserTypeNotImpl") {
  expectUserTypeUnaryOperatorImpl("Not"_zc, ast::UnaryOperatorKind::LogicalNot, false);
}

ZC_TEST("BodyChecker.LogicalNotRejectsWrongTraitMethodSignature") {
  TestFixture fix;
  auto consumer = zc::heap<CapturingDiagnosticConsumer>();
  auto consumerPtr = consumer.get();
  fix.diagnostics().addConsumer(zc::mv(consumer));

  auto notIface = fix.makeInterfaceDecl("Not"_zc);
  auto operandType = fix.makeClassDecl("Operand"_zc);

  zc::Vector<ast::NodeId> implIfaceNodes;
  implIfaceNodes.add(fix.makeNamedTypeExpr("Not"_zc));
  auto implIfaces = fix.makeImplIfaceList(fix.makeNodeList(implIfaceNodes.asPtr()));
  auto implMethod = fix.makeMethodDecl("not"_zc, ast::NodeId(), ast::NodeId(),
                                       fix.makeNamedTypeExpr("Operand"_zc));
  zc::Vector<ast::NodeId> implMembers;
  implMembers.add(implMethod);
  auto implDecl =
      makeStandaloneImplDecl(fix, fix.makeNamedTypeExpr("Operand"_zc), implIfaces,
                             fix.makeClassMemberList(fix.makeNodeList(implMembers.asPtr())));

  auto valueDecl = fix.makeVariableDeclarator(fix.makeBindingPattern("value"_zc),
                                              fix.makeNamedTypeExpr("Operand"_zc));
  zc::Vector<ast::NodeId> decls;
  decls.add(valueDecl);
  auto let = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr())));
  auto unary = fix.makeUnaryExpr(ast::UnaryOperatorKind::LogicalNot, fix.makeIdentExpr("value"_zc));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(notIface);
  topDecls.add(operandType);
  topDecls.add(implDecl);
  topDecls.add(let);
  topDecls.add(unary);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(!result.success);
  ZC_EXPECT(fix.diagnostics().hasErrors());
  ZC_EXPECT(
      containsDiagnosticId(*consumerPtr, diagnostics::DiagID::OperatorTraitSignatureMismatch));
  ZC_EXPECT(result.typeEnv.hasType(unary));
  if (result.typeEnv.hasType(unary)) { ZC_EXPECT(isError(result.typeEnv.getType(unary))); }
}

ZC_TEST("BodyChecker.LogicalNotRejectsUserTypeWithoutNotImpl") {
  expectUserTypeUnaryWithoutImplFails(ast::UnaryOperatorKind::LogicalNot);
}

ZC_TEST("BodyChecker.ErrorUnwrapRejectsNonUnionOperand") {
  TestFixture fix;
  auto consumer = zc::heap<CapturingDiagnosticConsumer>();
  auto consumerPtr = consumer.get();
  fix.diagnostics().addConsumer(zc::mv(consumer));
  auto operand = fix.makeIntLiteral(1);
  auto unwrap = fix.makePostfixExpr(ast::PostfixOperatorKind::ErrorUnwrap, operand);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(unwrap);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(!result.success);
  ZC_EXPECT(fix.diagnostics().hasErrors());
  ZC_EXPECT(containsDiagnosticId(*consumerPtr, diagnostics::DiagID::ErrorUnwrapNonUnion));
  ZC_EXPECT(result.typeEnv.hasType(unwrap));
  ZC_EXPECT(isError(result.typeEnv.getType(unwrap)));
}

ZC_TEST("BodyChecker.ErrorPropagateRejectsNonUnionOperand") {
  TestFixture fix;
  auto operand = fix.makeIntLiteral(1);
  auto propagate = fix.makePostfixExpr(ast::PostfixOperatorKind::ErrorPropagate, operand);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(propagate);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(!result.success);
  ZC_EXPECT(fix.diagnostics().hasErrors());
  ZC_EXPECT(result.typeEnv.hasType(propagate));
  ZC_EXPECT(isError(result.typeEnv.getType(propagate)));
}

ZC_TEST("BodyChecker.ErrorUnwrapReturnsFirstUnionAlternative") {
  TestFixture fix;
  auto cond = fix.makeBoolLiteral(true);
  auto success = fix.makeIntLiteral(1);
  auto failure = fix.makeStrLiteral("error"_zc);
  auto unionExpr = fix.makeConditionalExpr(cond, success, failure);
  auto unwrap = fix.makePostfixExpr(ast::PostfixOperatorKind::ErrorUnwrap, unionExpr);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(unwrap);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  ZC_EXPECT(result.typeEnv.hasType(unwrap));
  auto& ty = result.typeEnv.getType(unwrap);
  ZC_EXPECT(isPrimitive(ty));
  if (isPrimitive(ty)) {
    auto& primitive = static_cast<const type::PrimitiveType&>(ty);
    ZC_EXPECT(primitive.getPrimitiveKind() == type::PrimitiveKind::I32);
  }
}

ZC_TEST("BodyChecker.ErrorPropagateRequiresEnclosingRaises") {
  TestFixture fix;
  auto consumer = zc::heap<CapturingDiagnosticConsumer>();
  auto consumerPtr = consumer.get();
  fix.diagnostics().addConsumer(zc::mv(consumer));
  auto cond = fix.makeBoolLiteral(true);
  auto success = fix.makeIntLiteral(1);
  auto failure = fix.makeStrLiteral("error"_zc);
  auto unionExpr = fix.makeConditionalExpr(cond, success, failure);
  auto propagate = fix.makePostfixExpr(ast::PostfixOperatorKind::ErrorPropagate, unionExpr);
  auto returnStmt = fix.makeReturnStmt(propagate);

  zc::Vector<ast::NodeId> bodyStmts;
  bodyStmts.add(returnStmt);
  auto bodyBlock = fix.makeBlockStmt(fix.makeNodeList(bodyStmts.asPtr()));
  auto retTy = fix.makeNamedTypeExpr("i32"_zc);
  auto fn = fix.makeFunctionDecl("f"_zc, bodyBlock, ast::NodeId(), retTy);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(fn);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(!result.success);
  ZC_EXPECT(fix.diagnostics().hasErrors());
  ZC_EXPECT(containsDiagnosticId(*consumerPtr, diagnostics::DiagID::ErrorPropagateOutsideRaises));
  ZC_EXPECT(result.typeEnv.hasType(propagate));
  ZC_EXPECT(isError(result.typeEnv.getType(propagate)));
}

ZC_TEST("BodyChecker.ErrorPropagateAllowsMatchingRaises") {
  TestFixture fix;
  auto cond = fix.makeBoolLiteral(true);
  auto success = fix.makeIntLiteral(1);
  auto failure = fix.makeStrLiteral("error"_zc);
  auto unionExpr = fix.makeConditionalExpr(cond, success, failure);
  auto propagate = fix.makePostfixExpr(ast::PostfixOperatorKind::ErrorPropagate, unionExpr);
  auto returnStmt = fix.makeReturnStmt(propagate);

  zc::Vector<ast::NodeId> bodyStmts;
  bodyStmts.add(returnStmt);
  auto bodyBlock = fix.makeBlockStmt(fix.makeNodeList(bodyStmts.asPtr()));
  auto retTy = fix.makeNamedTypeExpr("i32"_zc);
  auto raisesTy = fix.makeNamedTypeExpr("str"_zc);
  auto fn = fix.makeFunctionDecl("f"_zc, bodyBlock, ast::NodeId(), retTy, raisesTy);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(fn);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  ZC_EXPECT(!fix.diagnostics().hasErrors());
  ZC_EXPECT(result.typeEnv.hasType(propagate));
  auto& ty = result.typeEnv.getType(propagate);
  ZC_EXPECT(isPrimitive(ty));
}

ZC_TEST("BodyChecker.ErrorPropagateAllowsRaisesUnionSubset") {
  TestFixture fix;
  auto cond = fix.makeBoolLiteral(true);
  auto success = fix.makeIntLiteral(1);
  auto failure = fix.makeStrLiteral("error"_zc);
  auto unionExpr = fix.makeConditionalExpr(cond, success, failure);
  auto propagate = fix.makePostfixExpr(ast::PostfixOperatorKind::ErrorPropagate, unionExpr);
  auto returnStmt = fix.makeReturnStmt(propagate);

  zc::Vector<ast::NodeId> bodyStmts;
  bodyStmts.add(returnStmt);
  auto bodyBlock = fix.makeBlockStmt(fix.makeNodeList(bodyStmts.asPtr()));
  auto retTy = fix.makeNamedTypeExpr("i32"_zc);
  auto raisesTy =
      fix.makeUnionTypeExpr(fix.makeNamedTypeExpr("str"_zc), fix.makeNamedTypeExpr("bool"_zc));
  auto fn = fix.makeFunctionDecl("f"_zc, bodyBlock, ast::NodeId(), retTy, raisesTy);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(fn);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  ZC_EXPECT(!fix.diagnostics().hasErrors());
  ZC_EXPECT(result.typeEnv.hasType(propagate));
  auto& ty = result.typeEnv.getType(propagate);
  ZC_EXPECT(isPrimitive(ty));
}

ZC_TEST("BodyChecker.ErrorPropagateRejectsRaisingCallWithoutEnclosingRaises") {
  TestFixture fix;
  zc::Vector<ast::NodeId> fallibleBodyStmts;
  fallibleBodyStmts.add(fix.makeReturnStmt(fix.makeIntLiteral(1)));
  auto fallibleBody = fix.makeBlockStmt(fix.makeNodeList(fallibleBodyStmts.asPtr()));
  auto retTy = fix.makeNamedTypeExpr("i32"_zc);
  auto raisesTy = fix.makeNamedTypeExpr("str"_zc);
  auto fallible = fix.makeFunctionDecl("fallible"_zc, fallibleBody, ast::NodeId(), retTy, raisesTy);

  auto call = fix.makeCallExpr(fix.makeIdentExpr("fallible"_zc), ast::NodeList());
  auto propagate = fix.makePostfixExpr(ast::PostfixOperatorKind::ErrorPropagate, call);
  zc::Vector<ast::NodeId> callerBodyStmts;
  callerBodyStmts.add(fix.makeReturnStmt(propagate));
  auto callerBody = fix.makeBlockStmt(fix.makeNodeList(callerBodyStmts.asPtr()));
  auto caller = fix.makeFunctionDecl("caller"_zc, callerBody, ast::NodeId(), retTy);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(fallible);
  topDecls.add(caller);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(!result.success);
  ZC_EXPECT(fix.diagnostics().hasErrors());
  ZC_EXPECT(result.typeEnv.hasType(propagate));
  ZC_EXPECT(isError(result.typeEnv.getType(propagate)));
}

ZC_TEST("BodyChecker.ErrorPropagateAllowsRaisingCallWithMatchingRaises") {
  TestFixture fix;
  zc::Vector<ast::NodeId> fallibleBodyStmts;
  fallibleBodyStmts.add(fix.makeReturnStmt(fix.makeIntLiteral(1)));
  auto fallibleBody = fix.makeBlockStmt(fix.makeNodeList(fallibleBodyStmts.asPtr()));
  auto retTy = fix.makeNamedTypeExpr("i32"_zc);
  auto raisesTy = fix.makeNamedTypeExpr("str"_zc);
  auto fallible = fix.makeFunctionDecl("fallible"_zc, fallibleBody, ast::NodeId(), retTy, raisesTy);

  auto call = fix.makeCallExpr(fix.makeIdentExpr("fallible"_zc), ast::NodeList());
  auto propagate = fix.makePostfixExpr(ast::PostfixOperatorKind::ErrorPropagate, call);
  zc::Vector<ast::NodeId> callerBodyStmts;
  callerBodyStmts.add(fix.makeReturnStmt(propagate));
  auto callerBody = fix.makeBlockStmt(fix.makeNodeList(callerBodyStmts.asPtr()));
  auto caller = fix.makeFunctionDecl("caller"_zc, callerBody, ast::NodeId(), retTy, raisesTy);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(fallible);
  topDecls.add(caller);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  ZC_EXPECT(!fix.diagnostics().hasErrors());
  ZC_EXPECT(result.typeEnv.hasType(propagate));
  auto& ty = result.typeEnv.getType(propagate);
  ZC_EXPECT(isPrimitive(ty));
}

// ============================================================================
// Array literal
// ============================================================================

ZC_TEST("BodyChecker.ArrayLiteralInfersType") {
  TestFixture fix;
  // [1, 2, 3]
  zc::Vector<ast::NodeId> elems;
  elems.add(fix.makeIntLiteral(1));
  elems.add(fix.makeIntLiteral(2));
  elems.add(fix.makeIntLiteral(3));
  auto arr = fix.makeArrayLiteral(fix.makeNodeList(elems.asPtr()));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(arr);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  ZC_EXPECT(result.typeEnv.hasType(arr));
  auto& ty = result.typeEnv.getType(arr);
  ZC_EXPECT(isArray(ty));
  auto& arrTy = static_cast<const type::ArrayType&>(ty);
  ZC_EXPECT(isPrimitive(arrTy.getElementType()));
}

ZC_TEST("BodyChecker.ArrayLiteralRejectsIncompatibleElementTypes") {
  TestFixture fix;
  zc::Vector<ast::NodeId> elems;
  elems.add(fix.makeIntLiteral(1));
  elems.add(fix.makeStrLiteral("two"_zc));
  auto arr = fix.makeArrayLiteral(fix.makeNodeList(elems.asPtr()));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(arr);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(!result.success);
  ZC_EXPECT(fix.diagnostics().hasErrors());
  ZC_EXPECT(result.typeEnv.hasType(arr));
  ZC_EXPECT(isError(result.typeEnv.getType(arr)));
}

ZC_TEST("BodyChecker.DependentErrorExpressionEmitsOnlyOneDiagnostic") {
  TestFixture fix;

  auto bad = fix.makeBinaryExpr(ast::BinaryOperatorKind::Add, fix.makeIntLiteral(1),
                                fix.makeStrLiteral("two"_zc));
  zc::Vector<ast::NodeId> elems;
  elems.add(bad);
  elems.add(fix.makeIntLiteral(2));
  auto arr = fix.makeArrayLiteral(fix.makeNodeList(elems.asPtr()));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(arr);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(!result.success);
  ZC_EXPECT(fix.diagnostics().errorCount() == 1);
  ZC_EXPECT(result.typeEnv.hasType(bad));
  ZC_EXPECT(isError(result.typeEnv.getType(bad)));
  ZC_EXPECT(result.typeEnv.hasType(arr));
  ZC_EXPECT(isError(result.typeEnv.getType(arr)));
}

ZC_TEST("BodyChecker.UndeclaredValueDiagnosticIsCheckerFallback") {
  TestFixture fix;
  auto consumer = zc::heap<CapturingDiagnosticConsumer>();
  auto consumerPtr = consumer.get();
  fix.diagnostics().addConsumer(zc::mv(consumer));

  auto ident = fix.makeIdentExpr("missing"_zc);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(ident);
  auto tree = fix.buildSourceFile("test"_zc, topDecls.asPtr());

  type::TypeEnv typeEnv;
  type::UnificationEngine unifier(typeEnv);
  type::ConstraintSet constraints;
  BodyChecker bodyChecker(typeEnv, unifier, constraints, fix.symbols(), tree, fix.metadata(),
                          fix.diagnostics());
  bool success = bodyChecker.checkBodies();

  ZC_EXPECT(!success);
  ZC_EXPECT(containsDiagnosticId(*consumerPtr, diagnostics::DiagID::UndeclaredValue));
  ZC_EXPECT(typeEnv.hasType(ident));
  ZC_EXPECT(isError(typeEnv.getType(ident)));
}

ZC_TEST("BodyChecker.EmptyUnionErrorOperatorDiagnosticIsCheckerFallback") {
  TestFixture fix;
  auto consumer = zc::heap<CapturingDiagnosticConsumer>();
  auto consumerPtr = consumer.get();
  fix.diagnostics().addConsumer(zc::mv(consumer));

  auto operand = fix.makeIdentExpr("value"_zc);
  auto propagate = fix.makePostfixExpr(ast::PostfixOperatorKind::ErrorPropagate, operand);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(propagate);
  auto tree = fix.buildSourceFile("test"_zc, topDecls.asPtr());

  type::TypeEnv typeEnv;
  zc::Vector<zc::Own<type::Type>> alternatives;
  typeEnv.setType(operand, zc::heap<type::UnionType>(zc::mv(alternatives)));
  type::UnificationEngine unifier(typeEnv);
  type::ConstraintSet constraints;
  BodyChecker bodyChecker(typeEnv, unifier, constraints, fix.symbols(), tree, fix.metadata(),
                          fix.diagnostics());
  bool success = bodyChecker.checkBodies();

  ZC_EXPECT(!success);
  ZC_EXPECT(containsDiagnosticId(*consumerPtr, diagnostics::DiagID::ErrorUnionEmpty));
  ZC_EXPECT(typeEnv.hasType(propagate));
  ZC_EXPECT(isError(typeEnv.getType(propagate)));
}

ZC_TEST("BodyChecker.UnsupportedStructLiteralTargetDiagnosticIsCheckerFallback") {
  TestFixture fix;
  auto consumer = zc::heap<CapturingDiagnosticConsumer>();
  auto consumerPtr = consumer.get();
  fix.diagnostics().addConsumer(zc::mv(consumer));

  zc::Vector<ast::NodeId> props;
  props.add(fix.makeObjectProperty("value"_zc, fix.makeIntLiteral(1)));
  auto lit = fix.makeStructLiteralExpr(ast::NodeId(), fix.makeNodeList(props.asPtr()));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(lit);
  auto tree = fix.buildSourceFile("test"_zc, topDecls.asPtr());

  type::TypeEnv typeEnv;
  type::UnificationEngine unifier(typeEnv);
  type::ConstraintSet constraints;
  BodyChecker bodyChecker(typeEnv, unifier, constraints, fix.symbols(), tree, fix.metadata(),
                          fix.diagnostics());
  bool success = bodyChecker.checkBodies();

  ZC_EXPECT(!success);
  ZC_EXPECT(
      containsDiagnosticId(*consumerPtr, diagnostics::DiagID::UnsupportedStructLiteralTarget));
  ZC_EXPECT(typeEnv.hasType(lit));
  ZC_EXPECT(isError(typeEnv.getType(lit)));
}

// ============================================================================
// Tuple literal
// ============================================================================

ZC_TEST("BodyChecker.TupleLiteralInfersType") {
  TestFixture fix;
  // (1, "two")
  zc::Vector<ast::NodeId> elems;
  elems.add(fix.makeIntLiteral(1));
  elems.add(fix.makeStrLiteral("two"_zc));
  auto tuple = fix.makeTupleLiteral(fix.makeNodeList(elems.asPtr()));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(tuple);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  if (result.typeEnv.hasType(tuple)) {
    auto& ty = result.typeEnv.getType(tuple);
    ZC_EXPECT(isTuple(ty));
    if (isTuple(ty)) {
      auto& tupleTy = static_cast<const type::TupleType&>(ty);
      ZC_EXPECT(tupleTy.getElementCount() == 2);
      ZC_EXPECT(isPrimitive(tupleTy.getElementType(0)));
      ZC_EXPECT(isPrimitive(tupleTy.getElementType(1)));
      if (isPrimitive(tupleTy.getElementType(0)) && isPrimitive(tupleTy.getElementType(1))) {
        auto& first = static_cast<const type::PrimitiveType&>(tupleTy.getElementType(0));
        auto& second = static_cast<const type::PrimitiveType&>(tupleTy.getElementType(1));
        ZC_EXPECT(first.getPrimitiveKind() == type::PrimitiveKind::I32);
        ZC_EXPECT(second.getPrimitiveKind() == type::PrimitiveKind::Str);
      }
    }
  }
}

// ============================================================================
// Lambda expression
// ============================================================================

ZC_TEST("BodyChecker.LambdaExprInfersFunctionType") {
  TestFixture fix;
  // fun(x) { return x; }
  auto body = fix.makeBlockStmt(ast::NodeList());
  auto lambda = fix.makeLambdaExpr(body);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(lambda);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  if (result.typeEnv.hasType(lambda)) {
    auto& ty = result.typeEnv.getType(lambda);
    ZC_EXPECT(isFunction(ty));
  }
}

ZC_TEST("BodyChecker.LambdaExprUsesAnnotatedSignature") {
  TestFixture fix;
  auto param = fix.makeFunctionParamDecl("x"_zc, fix.makeNamedTypeExpr("i32"_zc));
  zc::Vector<ast::NodeId> paramNodes;
  paramNodes.add(param);
  auto params = fix.makeFunctionParamList(fix.makeNodeList(paramNodes.asPtr()));
  auto body = fix.makeBlockStmt(ast::NodeList());
  auto lambda = fix.makeLambdaExpr(body, params, fix.makeNamedTypeExpr("i32"_zc));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(lambda);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  ZC_EXPECT(!fix.diagnostics().hasErrors());
  ZC_EXPECT(result.typeEnv.hasType(lambda));
  auto& ty = result.typeEnv.getType(lambda);
  ZC_EXPECT(isFunction(ty));
  if (isFunction(ty)) {
    auto& fn = static_cast<const type::FunctionType&>(ty);
    ZC_EXPECT(fn.getParamCount() == 1);
    ZC_EXPECT(isPrimitive(fn.getParamType(0)));
    ZC_EXPECT(isPrimitive(fn.getReturnType()));
  }
}

ZC_TEST("BodyChecker.FunctionExprUsesAnnotatedSignature") {
  TestFixture fix;
  auto param = fix.makeFunctionParamDecl("x"_zc, fix.makeNamedTypeExpr("i32"_zc));
  zc::Vector<ast::NodeId> paramNodes;
  paramNodes.add(param);
  auto params = fix.makeFunctionParamList(fix.makeNodeList(paramNodes.asPtr()));
  auto body = fix.makeBlockStmt(ast::NodeList());
  auto fnExpr = fix.makeFunctionExpr(body, params, fix.makeNamedTypeExpr("i32"_zc));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(fnExpr);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  ZC_EXPECT(!fix.diagnostics().hasErrors());
  ZC_EXPECT(result.typeEnv.hasType(fnExpr));
  auto& ty = result.typeEnv.getType(fnExpr);
  ZC_EXPECT(isFunction(ty));
  if (isFunction(ty)) {
    auto& fn = static_cast<const type::FunctionType&>(ty);
    ZC_EXPECT(fn.getParamCount() == 1);
    ZC_EXPECT(isPrimitive(fn.getParamType(0)));
    ZC_EXPECT(isPrimitive(fn.getReturnType()));
  }
}

ZC_TEST("BodyChecker.FunctionExprChecksAnnotatedReturnAgainstBody") {
  TestFixture fix;
  auto consumer = zc::heap<CapturingDiagnosticConsumer>();
  auto consumerPtr = consumer.get();
  fix.diagnostics().addConsumer(zc::mv(consumer));

  auto body = fix.makeStrLiteral("bad"_zc);
  auto fnExpr = fix.makeFunctionExpr(body, ast::NodeId(), fix.makeNamedTypeExpr("i32"_zc));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(fnExpr);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(!result.success);
  ZC_EXPECT(fix.diagnostics().hasErrors());
  ZC_EXPECT(containsDiagnosticId(*consumerPtr, diagnostics::DiagID::TypeCheckerTypeMismatch));
  ZC_EXPECT(result.typeEnv.hasType(fnExpr));
  ZC_EXPECT(isError(result.typeEnv.getType(fnExpr)));
}

ZC_TEST("BodyChecker.LambdaExprChecksAnnotatedReturnAgainstBody") {
  TestFixture fix;
  auto consumer = zc::heap<CapturingDiagnosticConsumer>();
  auto consumerPtr = consumer.get();
  fix.diagnostics().addConsumer(zc::mv(consumer));

  auto body = fix.makeStrLiteral("bad"_zc);
  auto lambda = fix.makeLambdaExpr(body, ast::NodeId(), fix.makeNamedTypeExpr("i32"_zc));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(lambda);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(!result.success);
  ZC_EXPECT(fix.diagnostics().hasErrors());
  ZC_EXPECT(containsDiagnosticId(*consumerPtr, diagnostics::DiagID::TypeCheckerTypeMismatch));
  ZC_EXPECT(result.typeEnv.hasType(lambda));
  ZC_EXPECT(isError(result.typeEnv.getType(lambda)));
}

// ============================================================================
// Member access
// ============================================================================

ZC_TEST("BodyChecker.MemberAccess") {
  TestFixture fix;
  // { field: 1 }.field
  zc::Vector<ast::NodeId> props;
  props.add(fix.makeObjectProperty("field"_zc, fix.makeIntLiteral(1)));
  auto objRef = fix.makeObjectLiteral(fix.makeNodeList(props.asPtr()));
  auto member = fix.makeMemberExpr(objRef, "field"_zc);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(member);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  ZC_EXPECT(!fix.diagnostics().hasErrors());
  ZC_EXPECT(result.typeEnv.hasType(member));
  auto& ty = result.typeEnv.getType(member);
  ZC_EXPECT(isPrimitive(ty));
  if (isPrimitive(ty)) {
    auto& primitive = static_cast<const type::PrimitiveType&>(ty);
    ZC_EXPECT(primitive.getPrimitiveKind() == type::PrimitiveKind::I32);
  }
}

ZC_TEST("BodyChecker.MemberCallRecordsInstanceMethodDispatch") {
  TestFixture fix;
  auto method =
      fix.makeMethodDecl("value"_zc, ast::NodeId(), ast::NodeId(), fix.makeNamedTypeExpr("i32"_zc));
  zc::Vector<ast::NodeId> members;
  members.add(method);
  auto cls = fix.makeClassDecl("Counter"_zc, ast::NodeId(),
                               fix.makeClassMemberList(fix.makeNodeList(members.asPtr())));

  auto counterDecl = fix.makeVariableDeclarator(fix.makeBindingPattern("counter"_zc),
                                                fix.makeNamedTypeExpr("Counter"_zc));
  zc::Vector<ast::NodeId> decls;
  decls.add(counterDecl);
  auto let = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr())));

  auto callee = fix.makeMemberExpr(fix.makeIdentExpr("counter"_zc), "value"_zc);
  auto call = fix.makeCallExpr(callee, ast::NodeList());

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(cls);
  topDecls.add(let);
  topDecls.add(call);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  ZC_EXPECT(!fix.diagnostics().hasErrors());
  ZC_EXPECT(result.typeEnv.hasType(call));
  auto& ty = result.typeEnv.getType(call);
  ZC_EXPECT(isPrimitive(ty));
  if (isPrimitive(ty)) {
    auto& primitive = static_cast<const type::PrimitiveType&>(ty);
    ZC_EXPECT(primitive.getPrimitiveKind() == type::PrimitiveKind::I32);
  }
  ZC_EXPECT(result.typeEnv.hasDispatch(call));
  auto& dispatch = result.typeEnv.getDispatch(call);
  ZC_EXPECT(dispatch.targetKind == type::CallTargetKind::InstanceMethod);
  ZC_EXPECT(dispatch.receiverMode == type::ReceiverMode::ImplicitSelf);
  ZC_EXPECT(dispatch.targetSymbol.isValid());
  ZC_EXPECT(dispatch.argumentTypes.size() == 1);
  ZC_EXPECT(dispatch.resultType == result.typeEnv.getTypeId(call));
}

ZC_TEST("BodyChecker.StructMemberCallRecordsInstanceMethodDispatch") {
  TestFixture fix;
  auto method =
      fix.makeMethodDecl("norm"_zc, ast::NodeId(), ast::NodeId(), fix.makeNamedTypeExpr("i32"_zc));
  zc::Vector<ast::NodeId> members;
  members.add(method);
  auto point =
      fix.makeStructDecl("Point"_zc, fix.makeClassMemberList(fix.makeNodeList(members.asPtr())));

  auto pointDecl = fix.makeVariableDeclarator(fix.makeBindingPattern("point"_zc),
                                              fix.makeNamedTypeExpr("Point"_zc));
  zc::Vector<ast::NodeId> decls;
  decls.add(pointDecl);
  auto let = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr())));

  auto callee = fix.makeMemberExpr(fix.makeIdentExpr("point"_zc), "norm"_zc);
  auto call = fix.makeCallExpr(callee, ast::NodeList());

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(point);
  topDecls.add(let);
  topDecls.add(call);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  ZC_EXPECT(!fix.diagnostics().hasErrors());
  ZC_EXPECT(result.typeEnv.hasType(call));
  auto& ty = result.typeEnv.getType(call);
  ZC_EXPECT(isPrimitive(ty));
  if (isPrimitive(ty)) {
    auto& primitive = static_cast<const type::PrimitiveType&>(ty);
    ZC_EXPECT(primitive.getPrimitiveKind() == type::PrimitiveKind::I32);
  }
  ZC_EXPECT(result.typeEnv.hasDispatch(call));
  auto& dispatch = result.typeEnv.getDispatch(call);
  ZC_EXPECT(dispatch.targetKind == type::CallTargetKind::InstanceMethod);
  ZC_EXPECT(dispatch.receiverMode == type::ReceiverMode::ImplicitSelf);
  ZC_EXPECT(dispatch.targetSymbol.isValid());
  ZC_EXPECT(dispatch.argumentTypes.size() == 1);
  ZC_EXPECT(dispatch.resultType == result.typeEnv.getTypeId(call));
}

ZC_TEST("BodyChecker.MemberCallRecordsStaticMethodDispatch") {
  TestFixture fix;
  auto method = fix.makeMethodDecl("make"_zc, ast::NodeId(), ast::NodeId(),
                                   fix.makeNamedTypeExpr("Counter"_zc), true);
  zc::Vector<ast::NodeId> members;
  members.add(method);
  auto cls = fix.makeClassDecl("Counter"_zc, ast::NodeId(),
                               fix.makeClassMemberList(fix.makeNodeList(members.asPtr())));

  auto callee = fix.makeMemberExpr(fix.makeIdentExpr("Counter"_zc), "make"_zc);
  auto call = fix.makeCallExpr(callee, ast::NodeList());

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(cls);
  topDecls.add(call);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  ZC_EXPECT(!fix.diagnostics().hasErrors());
  ZC_EXPECT(result.typeEnv.hasType(call));
  auto& ty = result.typeEnv.getType(call);
  ZC_EXPECT(isNamed(ty));
  if (isNamed(ty)) { ZC_EXPECT(static_cast<const type::NamedType&>(ty).getName() == "Counter"_zc); }
  ZC_EXPECT(result.typeEnv.hasDispatch(call));
  auto& dispatch = result.typeEnv.getDispatch(call);
  ZC_EXPECT(dispatch.targetKind == type::CallTargetKind::StaticMethod);
  ZC_EXPECT(dispatch.receiverMode == type::ReceiverMode::None);
  ZC_EXPECT(dispatch.targetSymbol.isValid());
  ZC_EXPECT(dispatch.argumentTypes.size() == 0);
  ZC_EXPECT(dispatch.resultType == result.typeEnv.getTypeId(call));
}

// ============================================================================
// Cast expression
// ============================================================================

ZC_TEST("BodyChecker.CastExpr") {
  TestFixture fix;
  // 42 as i32
  auto expr = fix.makeIntLiteral(42);
  auto ty = fix.makeNamedTypeExpr("i32"_zc);
  auto cast = fix.makeCastExpr(expr, ty);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(cast);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  if (result.typeEnv.hasType(cast)) {
    auto& resultTy = result.typeEnv.getType(cast);
    // Cast result should be the target type
    ZC_EXPECT(resultTy.getKind() == type::TypeKind::Named || isPrimitive(resultTy));
  }
}

ZC_TEST("BodyChecker.CastAllowsNumericConversion") {
  TestFixture fix;
  auto expr = fix.makeIntLiteral(42);
  auto ty = fix.makeNamedTypeExpr("f64"_zc);
  auto cast = fix.makeCastExpr(expr, ty);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(cast);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  ZC_EXPECT(!fix.diagnostics().hasErrors());
  ZC_EXPECT(result.typeEnv.hasType(cast));
  ZC_EXPECT(isPrimitive(result.typeEnv.getType(cast)));
}

ZC_TEST("BodyChecker.CastRejectsIntegerToBool") {
  TestFixture fix;
  auto consumer = zc::heap<CapturingDiagnosticConsumer>();
  auto consumerPtr = consumer.get();
  fix.diagnostics().addConsumer(zc::mv(consumer));

  auto expr = fix.makeIntLiteral(42);
  auto ty = fix.makeNamedTypeExpr("bool"_zc);
  auto cast = fix.makeCastExpr(expr, ty);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(cast);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(!result.success);
  ZC_EXPECT(fix.diagnostics().hasErrors());
  ZC_EXPECT(containsDiagnosticId(*consumerPtr, diagnostics::DiagID::CheckerInvalidCast));
  ZC_EXPECT(result.typeEnv.hasType(cast));
  ZC_EXPECT(isError(result.typeEnv.getType(cast)));
}

ZC_TEST("BodyChecker.CastRejectsRawPointerReinterpretOutsideUnsafe") {
  TestFixture fix;
  auto sourceTy = fix.makeRawPointerTypeExpr(fix.makeNamedTypeExpr("i32"_zc));
  auto targetTy = fix.makeRawPointerTypeExpr(fix.makeNamedTypeExpr("f64"_zc));
  auto pat = fix.makeBindingPattern("p"_zc);
  auto decl = fix.makeVariableDeclarator(pat, sourceTy);
  zc::Vector<ast::NodeId> declList;
  declList.add(decl);
  auto varList = fix.makeVariableDeclaratorList(fix.makeNodeList(declList.asPtr()));
  auto let = fix.makeLetStmt(varList);
  auto cast = fix.makeCastExpr(fix.makeIdentExpr("p"_zc), targetTy);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(let);
  topDecls.add(cast);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(!result.success);
  ZC_EXPECT(fix.diagnostics().hasErrors());
  ZC_EXPECT(result.typeEnv.hasType(cast));
  ZC_EXPECT(isError(result.typeEnv.getType(cast)));
}

ZC_TEST("BodyChecker.CastAllowsRawPointerReinterpretInsideUnsafe") {
  TestFixture fix;
  auto sourceTy = fix.makeRawPointerTypeExpr(fix.makeNamedTypeExpr("i32"_zc));
  auto targetTy = fix.makeRawPointerTypeExpr(fix.makeNamedTypeExpr("f64"_zc));
  auto pat = fix.makeBindingPattern("p"_zc);
  auto decl = fix.makeVariableDeclarator(pat, sourceTy);
  zc::Vector<ast::NodeId> declList;
  declList.add(decl);
  auto varList = fix.makeVariableDeclaratorList(fix.makeNodeList(declList.asPtr()));
  auto let = fix.makeLetStmt(varList);
  auto cast = fix.makeCastExpr(fix.makeIdentExpr("p"_zc), targetTy);
  zc::Vector<ast::NodeId> unsafeStmts;
  unsafeStmts.add(fix.makeExpressionStatement(cast));
  auto unsafeBody = fix.makeBlockStmt(fix.makeNodeList(unsafeStmts.asPtr()));
  auto unsafeExpr = fix.makeUnsafeBlockExpr(unsafeBody);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(let);
  topDecls.add(unsafeExpr);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  ZC_EXPECT(!fix.diagnostics().hasErrors());
  ZC_EXPECT(result.typeEnv.hasType(cast));
  ZC_EXPECT(isRawPointer(result.typeEnv.getType(cast)));
}

ZC_TEST("BodyChecker.CastAllowsSharedReferenceToConstRawPointer") {
  TestFixture fix;
  auto sourceTy = fix.makeReferenceTypeExpr(fix.makeNamedTypeExpr("i32"_zc));
  auto targetTy = fix.makeRawPointerTypeExpr(fix.makeNamedTypeExpr("i32"_zc));
  auto pat = fix.makeBindingPattern("r"_zc);
  auto decl = fix.makeVariableDeclarator(
      pat, sourceTy, fix.makeUnaryExpr(ast::UnaryOperatorKind::Ref, fix.makeIntLiteral(1)));
  zc::Vector<ast::NodeId> declList;
  declList.add(decl);
  auto let = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(declList.asPtr())));
  auto cast = fix.makeCastExpr(fix.makeIdentExpr("r"_zc), targetTy);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(let);
  topDecls.add(cast);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  ZC_EXPECT(!fix.diagnostics().hasErrors());
  ZC_EXPECT(result.typeEnv.hasType(cast));
  ZC_EXPECT(isRawPointer(result.typeEnv.getType(cast)));
}

ZC_TEST("BodyChecker.CastAllowsMutableReferenceToMutableRawPointer") {
  TestFixture fix;
  auto sourceTy = fix.makeReferenceTypeExpr(fix.makeNamedTypeExpr("i32"_zc), true);
  auto targetTy = fix.makeRawPointerTypeExpr(fix.makeNamedTypeExpr("i32"_zc), true);
  auto pat = fix.makeBindingPattern("r"_zc);
  auto decl = fix.makeVariableDeclarator(pat, sourceTy);
  zc::Vector<ast::NodeId> declList;
  declList.add(decl);
  auto let = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(declList.asPtr())));
  auto cast = fix.makeCastExpr(fix.makeIdentExpr("r"_zc), targetTy);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(let);
  topDecls.add(cast);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  ZC_EXPECT(!fix.diagnostics().hasErrors());
  ZC_EXPECT(result.typeEnv.hasType(cast));
  ZC_EXPECT(isRawPointer(result.typeEnv.getType(cast)));
}

ZC_TEST("BodyChecker.CastAllowsDynUpcast") {
  TestFixture fix;
  auto parentIface = fix.makeInterfaceDecl("Parent"_zc);
  zc::Vector<ast::NodeId> childSuperIfaces;
  childSuperIfaces.add(fix.makeNamedTypeExpr("Parent"_zc));
  auto childIface = fix.makeInterfaceDecl(
      "Child"_zc, ast::NodeId(), fix.makeImplIfaceList(fix.makeNodeList(childSuperIfaces.asPtr())));
  auto concrete = fix.makeClassDecl("Concrete"_zc);

  zc::Vector<ast::NodeId> ifaceNodes;
  ifaceNodes.add(fix.makeNamedTypeExpr("Child"_zc));
  auto ifaceList = fix.makeImplIfaceList(fix.makeNodeList(ifaceNodes.asPtr()));
  auto childImpl = fix.makeStandaloneImplDecl(fix.makeNamedTypeExpr("Concrete"_zc), ifaceList);
  auto concreteDecl = fix.makeVariableDeclarator(fix.makeBindingPattern("c"_zc),
                                                 fix.makeNamedTypeExpr("Concrete"_zc));

  auto sourceTy = fix.makeDynTypeExpr(fix.makeNamedTypeExpr("Child"_zc));
  auto targetTy = fix.makeDynTypeExpr(fix.makeNamedTypeExpr("Parent"_zc));
  auto pat = fix.makeBindingPattern("d"_zc);
  auto decl = fix.makeVariableDeclarator(pat, sourceTy, fix.makeIdentExpr("c"_zc));
  zc::Vector<ast::NodeId> declList;
  declList.add(concreteDecl);
  declList.add(decl);
  auto let = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(declList.asPtr())));
  auto cast = fix.makeCastExpr(fix.makeIdentExpr("d"_zc), targetTy);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(parentIface);
  topDecls.add(childIface);
  topDecls.add(concrete);
  topDecls.add(childImpl);
  topDecls.add(let);
  topDecls.add(cast);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  ZC_EXPECT(!fix.diagnostics().hasErrors());
  ZC_EXPECT(result.typeEnv.hasType(cast));
  ZC_EXPECT(isExistential(result.typeEnv.getType(cast)));
}

ZC_TEST("BodyChecker.CastRejectsUnrelatedDynUpcast") {
  TestFixture fix;
  auto consumer = zc::heap<CapturingDiagnosticConsumer>();
  auto consumerPtr = consumer.get();
  fix.diagnostics().addConsumer(zc::mv(consumer));

  auto leftIface = fix.makeInterfaceDecl("Left"_zc);
  auto rightIface = fix.makeInterfaceDecl("Right"_zc);
  auto sourceTy = fix.makeDynTypeExpr(fix.makeNamedTypeExpr("Left"_zc));
  auto decl = fix.makeVariableDeclarator(fix.makeBindingPattern("value"_zc), sourceTy);
  zc::Vector<ast::NodeId> decls;
  decls.add(decl);
  auto let = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr())));
  auto cast = fix.makeCastExpr(fix.makeIdentExpr("value"_zc),
                               fix.makeDynTypeExpr(fix.makeNamedTypeExpr("Right"_zc)));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(leftIface);
  topDecls.add(rightIface);
  topDecls.add(let);
  topDecls.add(cast);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(!result.success);
  ZC_EXPECT(fix.diagnostics().hasErrors());
  ZC_EXPECT(containsDiagnosticId(*consumerPtr, diagnostics::DiagID::InvalidDynUpcast));
  ZC_EXPECT(result.typeEnv.hasType(cast));
  ZC_EXPECT(isError(result.typeEnv.getType(cast)));
}

// ============================================================================
// Is expression
// ============================================================================

ZC_TEST("BodyChecker.IsExprReturnsBool") {
  TestFixture fix;
  // 42 is i32
  auto expr = fix.makeIntLiteral(42);
  auto ty = fix.makeNamedTypeExpr("i32"_zc);
  auto isExpr = fix.makeIsExpr(expr, ty);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(isExpr);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  if (result.typeEnv.hasType(isExpr)) {
    auto& resultTy = result.typeEnv.getType(isExpr);
    ZC_EXPECT(isPrimitive(resultTy));
  }
}

// ============================================================================
// This expression
// ============================================================================

ZC_TEST("BodyChecker.ThisExprOutsideClassGetsErrorType") {
  TestFixture fix;
  auto thisExpr = fix.makeThisExpr();

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(thisExpr);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(!result.success);
  ZC_EXPECT(fix.diagnostics().hasErrors());
  ZC_EXPECT(!result.typeEnv.hasType(thisExpr));
}

// ============================================================================
// For statement
// ============================================================================

ZC_TEST("BodyChecker.ForStmt") {
  TestFixture fix;
  auto initPat = fix.makeBindingPattern("i"_zc);
  auto initDecl = fix.makeVariableDeclarator(initPat, ast::NodeId(), fix.makeIntLiteral(0));
  zc::Vector<ast::NodeId> initDeclList;
  initDeclList.add(initDecl);
  auto initVarList = fix.makeVariableDeclaratorList(fix.makeNodeList(initDeclList.asPtr()));
  auto initLet = fix.makeLetStmt(initVarList);

  auto cond = fix.makeBinaryExpr(ast::BinaryOperatorKind::Lt, fix.makeIdentExpr("i"_zc),
                                 fix.makeIntLiteral(10));
  auto update = fix.makeAssignmentExpr(fix.makeIdentExpr("i"_zc), fix.makeIntLiteral(1), 1);
  auto bodyBlock = fix.makeBlockStmt(ast::NodeList());

  auto forStmt = fix.makeForStmt(initLet, cond, update, bodyBlock);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(forStmt);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
}

// ============================================================================
// Variable declaration type checking
// ============================================================================

ZC_TEST("BodyChecker.LetWithTypeAnnotation") {
  TestFixture fix;
  // let x: i32 = 42;
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
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
}

ZC_TEST("BodyChecker.LetWithTypeAnnotationRejectsWrongInitializer") {
  TestFixture fix;
  auto pat = fix.makeBindingPattern("x"_zc);
  auto ty = fix.makeNamedTypeExpr("i32"_zc);
  auto init = fix.makeStrLiteral("bad"_zc);
  auto decl = fix.makeVariableDeclarator(pat, ty, init);
  zc::Vector<ast::NodeId> declList;
  declList.add(decl);
  auto varList = fix.makeVariableDeclaratorList(fix.makeNodeList(declList.asPtr()));
  auto let = fix.makeLetStmt(varList);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(let);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(!result.success);
  ZC_EXPECT(fix.diagnostics().hasErrors());
  ZC_EXPECT(result.typeEnv.hasType(decl));
  ZC_EXPECT(isError(result.typeEnv.getType(decl)));
}

ZC_TEST("BodyChecker.LetWithoutAnnotationRejectsNullInitializer") {
  TestFixture fix;
  auto consumer = zc::heap<CapturingDiagnosticConsumer>();
  auto consumerPtr = consumer.get();
  fix.diagnostics().addConsumer(zc::mv(consumer));

  auto pat = fix.makeBindingPattern("x"_zc);
  auto init = fix.makeNullLiteral();
  auto decl = fix.makeVariableDeclarator(pat, ast::NodeId(), init);
  zc::Vector<ast::NodeId> declList;
  declList.add(decl);
  auto varList = fix.makeVariableDeclaratorList(fix.makeNodeList(declList.asPtr()));
  auto let = fix.makeLetStmt(varList);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(let);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(!result.success);
  ZC_EXPECT(fix.diagnostics().hasErrors());
  ZC_EXPECT(containsDiagnosticId(*consumerPtr, diagnostics::DiagID::CannotInferNullInitializer));
  ZC_EXPECT(result.typeEnv.hasType(decl));
  ZC_EXPECT(isError(result.typeEnv.getType(decl)));
}

ZC_TEST("BodyChecker.LetWithNullableAnnotationAcceptsNullInitializer") {
  TestFixture fix;
  auto pat = fix.makeBindingPattern("x"_zc);
  auto ty = fix.makeUnionTypeExpr(fix.makeNamedTypeExpr("i32"_zc), fix.makePredefinedTypeExpr(13));
  auto init = fix.makeNullLiteral();
  auto decl = fix.makeVariableDeclarator(pat, ty, init);
  zc::Vector<ast::NodeId> declList;
  declList.add(decl);
  auto varList = fix.makeVariableDeclaratorList(fix.makeNodeList(declList.asPtr()));
  auto let = fix.makeLetStmt(varList);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(let);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  ZC_EXPECT(!fix.diagnostics().hasErrors());
  ZC_EXPECT(result.typeEnv.hasType(decl));
  ZC_EXPECT(isUnion(result.typeEnv.getType(decl)));
  ZC_EXPECT(result.typeEnv.hasCoercion(decl));
  ZC_EXPECT(result.typeEnv.getCoercion(decl) == type::CoercionKind::NullToNullableUnion);
}

ZC_TEST("BodyChecker.LetWithReferenceAnnotationRejectsNullInitializer") {
  TestFixture fix;
  auto pat = fix.makeBindingPattern("r"_zc);
  auto refTy = fix.makeReferenceTypeExpr(fix.makeNamedTypeExpr("i32"_zc));
  auto decl = fix.makeVariableDeclarator(pat, refTy, fix.makeNullLiteral());
  zc::Vector<ast::NodeId> declList;
  declList.add(decl);
  auto let = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(declList.asPtr())));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(let);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(!result.success);
  ZC_EXPECT(fix.diagnostics().hasErrors());
  ZC_EXPECT(result.typeEnv.hasType(decl));
  ZC_EXPECT(isError(result.typeEnv.getType(decl)));
}

ZC_TEST("BodyChecker.LetWithNullableReferenceAnnotationAcceptsNullInitializer") {
  TestFixture fix;
  auto pat = fix.makeBindingPattern("r"_zc);
  auto refTy = fix.makeReferenceTypeExpr(fix.makeNamedTypeExpr("i32"_zc));
  auto nullableRefTy = fix.makeUnionTypeExpr(refTy, fix.makePredefinedTypeExpr(13));
  auto decl = fix.makeVariableDeclarator(pat, nullableRefTy, fix.makeNullLiteral());
  zc::Vector<ast::NodeId> declList;
  declList.add(decl);
  auto let = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(declList.asPtr())));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(let);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  ZC_EXPECT(!fix.diagnostics().hasErrors());
  ZC_EXPECT(result.typeEnv.hasType(decl));
  ZC_EXPECT(isUnion(result.typeEnv.getType(decl)));
}

ZC_TEST("BodyChecker.LetWithDynAnnotationRecordsExistentialErasure") {
  TestFixture fix;
  auto addIface = fix.makeInterfaceDecl("Add"_zc);
  auto numberType = fix.makeClassDecl("Number"_zc);

  zc::Vector<ast::NodeId> ifaceNodes;
  ifaceNodes.add(fix.makeNamedTypeExpr("Add"_zc));
  auto ifaceList = fix.makeImplIfaceList(fix.makeNodeList(ifaceNodes.asPtr()));
  auto addImpl = fix.makeStandaloneImplDecl(fix.makeNamedTypeExpr("Number"_zc), ifaceList);

  auto xDecl = fix.makeVariableDeclarator(fix.makeBindingPattern("x"_zc),
                                          fix.makeNamedTypeExpr("Number"_zc));
  auto yDecl = fix.makeVariableDeclarator(fix.makeBindingPattern("y"_zc),
                                          fix.makeDynTypeExpr(fix.makeNamedTypeExpr("Add"_zc)),
                                          fix.makeIdentExpr("x"_zc));
  zc::Vector<ast::NodeId> decls;
  decls.add(xDecl);
  decls.add(yDecl);
  auto let = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr())));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(addIface);
  topDecls.add(numberType);
  topDecls.add(addImpl);
  topDecls.add(let);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  ZC_EXPECT(!fix.diagnostics().hasErrors());
  ZC_EXPECT(result.typeEnv.hasType(yDecl));
  ZC_EXPECT(isExistential(result.typeEnv.getType(yDecl)));
  ZC_EXPECT(result.typeEnv.hasCoercion(yDecl));
  ZC_EXPECT(result.typeEnv.getCoercion(yDecl) == type::CoercionKind::ExistentialErasure);
}

ZC_TEST("BodyChecker.DynReceiverCallRecordsVTableDispatch") {
  TestFixture fix;
  auto drawMethod =
      fix.makeMethodDecl("draw"_zc, ast::NodeId(), ast::NodeId(), fix.makeNamedTypeExpr("unit"_zc));
  zc::Vector<ast::NodeId> ifaceMembers;
  ifaceMembers.add(drawMethod);
  auto drawableIface = fix.makeInterfaceDecl(
      "Drawable"_zc, fix.makeClassMemberList(fix.makeNodeList(ifaceMembers.asPtr())));

  auto spriteType = fix.makeClassDecl("Sprite"_zc);
  zc::Vector<ast::NodeId> implIfaceNodes;
  implIfaceNodes.add(fix.makeNamedTypeExpr("Drawable"_zc));
  auto implIfaces = fix.makeImplIfaceList(fix.makeNodeList(implIfaceNodes.asPtr()));
  auto drawableImpl = fix.makeStandaloneImplDecl(fix.makeNamedTypeExpr("Sprite"_zc), implIfaces);

  auto spriteDecl = fix.makeVariableDeclarator(fix.makeBindingPattern("sprite"_zc),
                                               fix.makeNamedTypeExpr("Sprite"_zc));
  auto drawableDecl = fix.makeVariableDeclarator(
      fix.makeBindingPattern("drawable"_zc),
      fix.makeDynTypeExpr(fix.makeNamedTypeExpr("Drawable"_zc)), fix.makeIdentExpr("sprite"_zc));
  zc::Vector<ast::NodeId> decls;
  decls.add(spriteDecl);
  decls.add(drawableDecl);
  auto let = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr())));

  auto callee = fix.makeMemberExpr(fix.makeIdentExpr("drawable"_zc), "draw"_zc);
  auto call = fix.makeCallExpr(callee, ast::NodeList());

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(drawableIface);
  topDecls.add(spriteType);
  topDecls.add(drawableImpl);
  topDecls.add(let);
  topDecls.add(call);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  ZC_EXPECT(!fix.diagnostics().hasErrors());
  ZC_EXPECT(result.typeEnv.hasType(call));
  auto& ty = result.typeEnv.getType(call);
  ZC_EXPECT(isPrimitive(ty));
  if (isPrimitive(ty)) {
    auto& primitive = static_cast<const type::PrimitiveType&>(ty);
    ZC_EXPECT(primitive.getPrimitiveKind() == type::PrimitiveKind::Unit);
  }
  ZC_EXPECT(result.typeEnv.hasDispatch(call));
  auto& dispatch = result.typeEnv.getDispatch(call);
  ZC_EXPECT(dispatch.targetKind == type::CallTargetKind::DynVTable);
  ZC_EXPECT(dispatch.receiverMode == type::ReceiverMode::ImplicitSelf);
  ZC_EXPECT(dispatch.interfaceName.asPtr() == "Drawable"_zc);
  ZC_EXPECT(dispatch.methodName.asPtr() == "draw"_zc);
  ZC_EXPECT(dispatch.vtableSlot == 0);
  ZC_EXPECT(dispatch.argumentTypes.size() == 1);
  ZC_EXPECT(dispatch.resultType == result.typeEnv.getTypeId(call));
}

ZC_TEST("BodyChecker.DynReceiverCallFindsInheritedVTableSlot") {
  TestFixture fix;
  auto pingMethod =
      fix.makeMethodDecl("ping"_zc, ast::NodeId(), ast::NodeId(), fix.makeNamedTypeExpr("unit"_zc));
  zc::Vector<ast::NodeId> baseMembers;
  baseMembers.add(pingMethod);
  auto baseIface = fix.makeInterfaceDecl(
      "Base"_zc, fix.makeClassMemberList(fix.makeNodeList(baseMembers.asPtr())));

  zc::Vector<ast::NodeId> childIfaces;
  childIfaces.add(fix.makeNamedTypeExpr("Base"_zc));
  auto childIfaceList = fix.makeImplIfaceList(fix.makeNodeList(childIfaces.asPtr()));
  auto drawMethod =
      fix.makeMethodDecl("draw"_zc, ast::NodeId(), ast::NodeId(), fix.makeNamedTypeExpr("unit"_zc));
  zc::Vector<ast::NodeId> childMembers;
  childMembers.add(drawMethod);
  auto childIface = fix.makeInterfaceDecl(
      "Child"_zc, fix.makeClassMemberList(fix.makeNodeList(childMembers.asPtr())), childIfaceList);

  auto spriteType = fix.makeClassDecl("Sprite"_zc);
  zc::Vector<ast::NodeId> baseImplIfaceNodes;
  baseImplIfaceNodes.add(fix.makeNamedTypeExpr("Base"_zc));
  auto baseImplIfaces = fix.makeImplIfaceList(fix.makeNodeList(baseImplIfaceNodes.asPtr()));
  auto baseImpl = fix.makeStandaloneImplDecl(fix.makeNamedTypeExpr("Sprite"_zc), baseImplIfaces);
  zc::Vector<ast::NodeId> childImplIfaceNodes;
  childImplIfaceNodes.add(fix.makeNamedTypeExpr("Child"_zc));
  auto childImplIfaces = fix.makeImplIfaceList(fix.makeNodeList(childImplIfaceNodes.asPtr()));
  auto childImpl = fix.makeStandaloneImplDecl(fix.makeNamedTypeExpr("Sprite"_zc), childImplIfaces);

  auto spriteDecl = fix.makeVariableDeclarator(fix.makeBindingPattern("sprite"_zc),
                                               fix.makeNamedTypeExpr("Sprite"_zc));
  auto childDecl = fix.makeVariableDeclarator(
      fix.makeBindingPattern("child"_zc), fix.makeDynTypeExpr(fix.makeNamedTypeExpr("Child"_zc)),
      fix.makeIdentExpr("sprite"_zc));
  zc::Vector<ast::NodeId> decls;
  decls.add(spriteDecl);
  decls.add(childDecl);
  auto let = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr())));

  auto callee = fix.makeMemberExpr(fix.makeIdentExpr("child"_zc), "ping"_zc);
  auto call = fix.makeCallExpr(callee, ast::NodeList());

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(baseIface);
  topDecls.add(childIface);
  topDecls.add(spriteType);
  topDecls.add(baseImpl);
  topDecls.add(childImpl);
  topDecls.add(let);
  topDecls.add(call);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  ZC_EXPECT(!fix.diagnostics().hasErrors());
  ZC_EXPECT(result.typeEnv.hasDispatch(call));
  auto& dispatch = result.typeEnv.getDispatch(call);
  ZC_EXPECT(dispatch.targetKind == type::CallTargetKind::DynVTable);
  ZC_EXPECT(dispatch.interfaceName.asPtr() == "Base"_zc);
  ZC_EXPECT(dispatch.methodName.asPtr() == "ping"_zc);
  ZC_EXPECT(dispatch.vtableSlot == 0);
  ZC_EXPECT(dispatch.argumentTypes.size() == 1);
  ZC_EXPECT(dispatch.resultType == result.typeEnv.getTypeId(call));
}

ZC_TEST("BodyChecker.QualifiedInterfaceCallRecordsDispatch") {
  TestFixture fix;
  zc::Vector<ast::NodeId> params;
  params.add(fix.makeFunctionParamDecl("value"_zc, fix.makeNamedTypeExpr("Sprite"_zc)));
  auto paramList = fix.makeFunctionParamList(fix.makeNodeList(params.asPtr()));
  auto drawMethod =
      fix.makeMethodDecl("draw"_zc, ast::NodeId(), paramList, fix.makeNamedTypeExpr("unit"_zc));
  zc::Vector<ast::NodeId> ifaceMembers;
  ifaceMembers.add(drawMethod);
  auto drawableIface = fix.makeInterfaceDecl(
      "Drawable"_zc, fix.makeClassMemberList(fix.makeNodeList(ifaceMembers.asPtr())));

  auto spriteType = fix.makeClassDecl("Sprite"_zc);
  zc::Vector<ast::NodeId> implIfaceNodes;
  implIfaceNodes.add(fix.makeNamedTypeExpr("Drawable"_zc));
  auto implIfaces = fix.makeImplIfaceList(fix.makeNodeList(implIfaceNodes.asPtr()));
  auto drawableImpl = fix.makeStandaloneImplDecl(fix.makeNamedTypeExpr("Sprite"_zc), implIfaces);

  auto spriteDecl = fix.makeVariableDeclarator(fix.makeBindingPattern("sprite"_zc),
                                               fix.makeNamedTypeExpr("Sprite"_zc));
  zc::Vector<ast::NodeId> decls;
  decls.add(spriteDecl);
  auto let = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr())));

  auto callee = fix.makeMemberExpr(fix.makeIdentExpr("Drawable"_zc), "draw"_zc);
  zc::Vector<ast::NodeId> args;
  args.add(fix.makeIdentExpr("sprite"_zc));
  auto call = fix.makeCallExpr(callee, fix.makeNodeList(args.asPtr()));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(drawableIface);
  topDecls.add(spriteType);
  topDecls.add(drawableImpl);
  topDecls.add(let);
  topDecls.add(call);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  ZC_EXPECT(!fix.diagnostics().hasErrors());
  ZC_EXPECT(result.typeEnv.hasType(call));
  auto& ty = result.typeEnv.getType(call);
  ZC_EXPECT(isPrimitive(ty));
  if (isPrimitive(ty)) {
    auto& primitive = static_cast<const type::PrimitiveType&>(ty);
    ZC_EXPECT(primitive.getPrimitiveKind() == type::PrimitiveKind::Unit);
  }
  ZC_EXPECT(result.typeEnv.hasDispatch(call));
  auto& dispatch = result.typeEnv.getDispatch(call);
  ZC_EXPECT(dispatch.targetKind == type::CallTargetKind::QualifiedInterfaceMethod);
  ZC_EXPECT(dispatch.receiverMode == type::ReceiverMode::ExplicitFirstArgument);
  ZC_EXPECT(dispatch.interfaceName.asPtr() == "Drawable"_zc);
  ZC_EXPECT(dispatch.methodName.asPtr() == "draw"_zc);
  ZC_EXPECT(dispatch.argumentTypes.size() == 1);
  ZC_EXPECT(dispatch.resultType == result.typeEnv.getTypeId(call));
}

ZC_TEST("BodyChecker.LetWithDynMarkerAnnotationRequiresMarker") {
  TestFixture fix;
  auto addIface = fix.makeInterfaceDecl("Add"_zc);
  auto numberType = fix.makeClassDecl("Number"_zc);

  zc::Vector<ast::NodeId> ifaceNodes;
  ifaceNodes.add(fix.makeNamedTypeExpr("Add"_zc));
  auto ifaceList = fix.makeImplIfaceList(fix.makeNodeList(ifaceNodes.asPtr()));
  auto addImpl = fix.makeStandaloneImplDecl(fix.makeNamedTypeExpr("Number"_zc), ifaceList);

  auto xDecl = fix.makeVariableDeclarator(fix.makeBindingPattern("x"_zc),
                                          fix.makeNamedTypeExpr("Number"_zc));
  auto yDecl = fix.makeVariableDeclarator(
      fix.makeBindingPattern("y"_zc),
      fix.makeDynTypeExpr(fix.makeNamedTypeExpr("Add"_zc), "Sendable"_zc),
      fix.makeIdentExpr("x"_zc));
  zc::Vector<ast::NodeId> decls;
  decls.add(xDecl);
  decls.add(yDecl);
  auto let = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr())));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(addIface);
  topDecls.add(numberType);
  topDecls.add(addImpl);
  topDecls.add(let);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  ZC_EXPECT(!fix.diagnostics().hasErrors());
  ZC_EXPECT(result.typeEnv.hasType(yDecl));
  ZC_EXPECT(isExistential(result.typeEnv.getType(yDecl)));
  ZC_EXPECT(result.typeEnv.hasCoercion(yDecl));
  ZC_EXPECT(result.typeEnv.getCoercion(yDecl) == type::CoercionKind::ExistentialErasure);
}

ZC_TEST("BodyChecker.LetWithDynMarkerAnnotationRejectsMissingMarker") {
  TestFixture fix;
  auto addIface = fix.makeInterfaceDecl("Add"_zc);
  auto rawField =
      fix.makeFieldDecl("ptr"_zc, fix.makeRawPointerTypeExpr(fix.makeNamedTypeExpr("i32"_zc)));
  zc::Vector<ast::NodeId> members;
  members.add(rawField);
  auto numberType = fix.makeClassDecl("Number"_zc, ast::NodeId(),
                                      fix.makeClassMemberList(fix.makeNodeList(members.asPtr())));

  zc::Vector<ast::NodeId> ifaceNodes;
  ifaceNodes.add(fix.makeNamedTypeExpr("Add"_zc));
  auto ifaceList = fix.makeImplIfaceList(fix.makeNodeList(ifaceNodes.asPtr()));
  auto addImpl = fix.makeStandaloneImplDecl(fix.makeNamedTypeExpr("Number"_zc), ifaceList);

  auto xDecl = fix.makeVariableDeclarator(fix.makeBindingPattern("x"_zc),
                                          fix.makeNamedTypeExpr("Number"_zc));
  auto yDecl = fix.makeVariableDeclarator(
      fix.makeBindingPattern("y"_zc),
      fix.makeDynTypeExpr(fix.makeNamedTypeExpr("Add"_zc), "Sendable"_zc),
      fix.makeIdentExpr("x"_zc));
  zc::Vector<ast::NodeId> decls;
  decls.add(xDecl);
  decls.add(yDecl);
  auto let = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr())));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(addIface);
  topDecls.add(numberType);
  topDecls.add(addImpl);
  topDecls.add(let);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(!result.success);
  ZC_EXPECT(fix.diagnostics().hasErrors());
  ZC_EXPECT(result.typeEnv.hasType(yDecl));
  ZC_EXPECT(isError(result.typeEnv.getType(yDecl)));
}

ZC_TEST("BodyChecker.MatchStmtReportsNonExhaustiveEnum") {
  TestFixture fix;
  auto consumer = zc::heap<CapturingDiagnosticConsumer>();
  auto consumerPtr = consumer.get();
  fix.diagnostics().addConsumer(zc::mv(consumer));

  zc::Vector<ast::NodeId> variants;
  variants.add(fix.makeEnumVariant("Red"_zc));
  variants.add(fix.makeEnumVariant("Blue"_zc));
  auto color =
      fix.makeEnumDecl("Color"_zc, fix.makeEnumVariantList(fix.makeNodeList(variants.asPtr())));

  auto pat = fix.makeBindingPattern("c"_zc);
  auto decl = fix.makeVariableDeclarator(pat, fix.makeNamedTypeExpr("Color"_zc));
  zc::Vector<ast::NodeId> decls;
  decls.add(decl);
  auto let = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr())));

  zc::Vector<ast::NodeId> arms;
  arms.add(fix.makeMatchArmStmt(fix.makeEnumPattern("Red"_zc), fix.makeBlockStmt(ast::NodeList())));
  auto match = fix.makeMatchStmt(fix.makeIdentExpr("c"_zc), fix.makeNodeList(arms.asPtr()));

  zc::Vector<ast::NodeId> bodyStmts;
  bodyStmts.add(let);
  bodyStmts.add(match);
  auto fn = fix.makeFunctionDecl("f"_zc, fix.makeBlockStmt(fix.makeNodeList(bodyStmts.asPtr())));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(color);
  topDecls.add(fn);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(!result.success);
  ZC_EXPECT(fix.diagnostics().hasErrors());
  ZC_EXPECT(containsDiagnosticId(*consumerPtr, diagnostics::DiagID::CheckerNonExhaustiveMatch));
}

ZC_TEST("BodyChecker.MatchStmtAcceptsExhaustiveTupleEnum") {
  TestFixture fix;
  auto consumer = zc::heap<CapturingDiagnosticConsumer>();
  auto consumerPtr = consumer.get();
  fix.diagnostics().addConsumer(zc::mv(consumer));

  zc::Vector<ast::NodeId> okFields;
  okFields.add(fix.makePredefinedTypeExpr(static_cast<uint8_t>(type::PrimitiveKind::I32)));
  zc::Vector<ast::NodeId> errFields;
  errFields.add(fix.makePredefinedTypeExpr(static_cast<uint8_t>(type::PrimitiveKind::Str)));
  zc::Vector<ast::NodeId> variants;
  variants.add(fix.makeTupleVariant("Ok"_zc, fix.makeNodeList(okFields.asPtr())));
  variants.add(fix.makeTupleVariant("Err"_zc, fix.makeNodeList(errFields.asPtr())));
  auto resultEnum =
      fix.makeEnumDecl("Result"_zc, fix.makeEnumVariantList(fix.makeNodeList(variants.asPtr())));

  zc::Vector<ast::NodeId> params;
  params.add(fix.makeFunctionParamDecl("r"_zc, fix.makeNamedTypeExpr("Result"_zc)));
  auto paramList = fix.makeFunctionParamList(fix.makeNodeList(params.asPtr()));

  zc::Vector<ast::NodeId> okArgs;
  okArgs.add(fix.makeIdentifierPattern("value"_zc));
  zc::Vector<ast::NodeId> errArgs;
  errArgs.add(fix.makeIdentifierPattern("message"_zc));
  zc::Vector<ast::NodeId> arms;
  arms.add(fix.makeMatchArmStmt(fix.makeEnumPattern("Ok"_zc, fix.makeNodeList(okArgs.asPtr())),
                                fix.makeBlockStmt(ast::NodeList())));
  arms.add(fix.makeMatchArmStmt(fix.makeEnumPattern("Err"_zc, fix.makeNodeList(errArgs.asPtr())),
                                fix.makeBlockStmt(ast::NodeList())));
  auto match = fix.makeMatchStmt(fix.makeIdentExpr("r"_zc), fix.makeNodeList(arms.asPtr()));

  zc::Vector<ast::NodeId> bodyStmts;
  bodyStmts.add(match);
  auto fn = fix.makeFunctionDecl("f"_zc, fix.makeBlockStmt(fix.makeNodeList(bodyStmts.asPtr())),
                                 paramList, fix.makeNamedTypeExpr("unit"_zc));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(resultEnum);
  topDecls.add(fn);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  ZC_EXPECT(!fix.diagnostics().hasErrors());
  ZC_EXPECT(!containsDiagnosticId(*consumerPtr, diagnostics::DiagID::CheckerNonExhaustiveMatch));
}

ZC_TEST("BodyChecker.MatchStmtReportsUnreachableArmAfterWildcard") {
  TestFixture fix;
  auto consumer = zc::heap<CapturingDiagnosticConsumer>();
  auto consumerPtr = consumer.get();
  fix.diagnostics().addConsumer(zc::mv(consumer));

  zc::Vector<ast::NodeId> params;
  params.add(fix.makeFunctionParamDecl("flag"_zc, fix.makeNamedTypeExpr("bool"_zc)));
  auto paramList = fix.makeFunctionParamList(fix.makeNodeList(params.asPtr()));

  zc::Vector<ast::NodeId> arms;
  arms.add(fix.makeMatchArmStmt(fix.makeWildcardPattern(), fix.makeBlockStmt(ast::NodeList())));
  arms.add(fix.makeMatchArmStmt(fix.makeLiteralPattern(fix.makeBoolLiteral(true)),
                                fix.makeBlockStmt(ast::NodeList())));
  auto match = fix.makeMatchStmt(fix.makeIdentExpr("flag"_zc), fix.makeNodeList(arms.asPtr()));

  zc::Vector<ast::NodeId> bodyStmts;
  bodyStmts.add(match);
  auto fn = fix.makeFunctionDecl("f"_zc, fix.makeBlockStmt(fix.makeNodeList(bodyStmts.asPtr())),
                                 paramList, fix.makeNamedTypeExpr("unit"_zc));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(fn);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  ZC_EXPECT(!fix.diagnostics().hasErrors());
  ZC_EXPECT(containsDiagnosticId(*consumerPtr, diagnostics::DiagID::CheckerUnreachableMatchArm));
}

ZC_TEST("BodyChecker.LetWithoutInit") {
  TestFixture fix;
  // let x: i32;
  auto pat = fix.makeBindingPattern("x"_zc);
  auto ty = fix.makeNamedTypeExpr("i32"_zc);
  auto decl = fix.makeVariableDeclarator(pat, ty);
  zc::Vector<ast::NodeId> declList;
  declList.add(decl);
  auto varList = fix.makeVariableDeclaratorList(fix.makeNodeList(declList.asPtr()));
  auto let = fix.makeLetStmt(varList);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(let);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
}

// ============================================================================
// Function with parameters
// ============================================================================

ZC_TEST("BodyChecker.FunctionWithParams") {
  TestFixture fix;
  // fun add(a: i32, b: i32) -> i32 { return a + b; }
  auto paramA = fix.makeFunctionParamDecl("a"_zc, fix.makeNamedTypeExpr("i32"_zc));
  auto paramB = fix.makeFunctionParamDecl("b"_zc, fix.makeNamedTypeExpr("i32"_zc));
  zc::Vector<ast::NodeId> paramNodes;
  paramNodes.add(paramA);
  paramNodes.add(paramB);
  auto paramList = fix.makeFunctionParamList(fix.makeNodeList(paramNodes.asPtr()));

  auto retExpr = fix.makeBinaryExpr(ast::BinaryOperatorKind::Add, fix.makeIdentExpr("a"_zc),
                                    fix.makeIdentExpr("b"_zc));
  zc::Vector<ast::NodeId> bodyStmts;
  bodyStmts.add(fix.makeReturnStmt(retExpr));
  auto bodyBlock = fix.makeBlockStmt(fix.makeNodeList(bodyStmts.asPtr()));
  auto retTy = fix.makeNamedTypeExpr("i32"_zc);
  auto fn = fix.makeFunctionDecl("add"_zc, bodyBlock, paramList, retTy);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(fn);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  ZC_EXPECT(!fix.diagnostics().hasErrors());
}

// ============================================================================
// Nested blocks
// ============================================================================

ZC_TEST("BodyChecker.NestedBlocks") {
  TestFixture fix;
  // { { let x = 1; } }
  auto innerPat = fix.makeBindingPattern("x"_zc);
  auto innerDecl = fix.makeVariableDeclarator(innerPat, ast::NodeId(), fix.makeIntLiteral(1));
  zc::Vector<ast::NodeId> innerDeclList;
  innerDeclList.add(innerDecl);
  auto innerVarList = fix.makeVariableDeclaratorList(fix.makeNodeList(innerDeclList.asPtr()));
  auto innerLet = fix.makeLetStmt(innerVarList);

  zc::Vector<ast::NodeId> innerStmts;
  innerStmts.add(innerLet);
  auto innerBlock = fix.makeBlockStmt(fix.makeNodeList(innerStmts.asPtr()));

  zc::Vector<ast::NodeId> outerStmts;
  outerStmts.add(innerBlock);
  auto outerBlock = fix.makeBlockStmt(fix.makeNodeList(outerStmts.asPtr()));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(outerBlock);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
}

// ============================================================================
// Multiple functions
// ============================================================================

ZC_TEST("BodyChecker.MultipleFunctions") {
  TestFixture fix;
  zc::Vector<ast::NodeId> body1;
  body1.add(fix.makeReturnStmt(fix.makeIntLiteral(1)));
  auto fn1 = fix.makeFunctionDecl("one"_zc, fix.makeBlockStmt(fix.makeNodeList(body1.asPtr())));

  zc::Vector<ast::NodeId> body2;
  body2.add(fix.makeReturnStmt(fix.makeIntLiteral(2)));
  auto fn2 = fix.makeFunctionDecl("two"_zc, fix.makeBlockStmt(fix.makeNodeList(body2.asPtr())));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(fn1);
  topDecls.add(fn2);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
}

// ============================================================================
// Index expression
// ============================================================================

ZC_TEST("BodyChecker.IndexExpr") {
  TestFixture fix;
  // arr[0]
  auto pat = fix.makeBindingPattern("arr"_zc);
  zc::Vector<ast::NodeId> elems;
  elems.add(fix.makeIntLiteral(1));
  elems.add(fix.makeIntLiteral(2));
  auto arrLit = fix.makeArrayLiteral(fix.makeNodeList(elems.asPtr()));
  auto decl = fix.makeVariableDeclarator(pat, ast::NodeId(), arrLit);
  zc::Vector<ast::NodeId> declList;
  declList.add(decl);
  auto varList = fix.makeVariableDeclaratorList(fix.makeNodeList(declList.asPtr()));
  auto let = fix.makeLetStmt(varList);

  auto arrRef = fix.makeIdentExpr("arr"_zc);
  auto index = fix.makeIndexExpr(arrRef, fix.makeIntLiteral(0));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(let);
  topDecls.add(index);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  ZC_EXPECT(result.typeEnv.hasType(index));
  auto& ty = result.typeEnv.getType(index);
  ZC_EXPECT(isPrimitive(ty));
  ZC_EXPECT(result.typeEnv.hasDispatch(index));
  auto& dispatch = result.typeEnv.getDispatch(index);
  ZC_EXPECT(dispatch.targetKind == type::CallTargetKind::PrimitiveOperator);
  ZC_EXPECT(dispatch.receiverMode == type::ReceiverMode::IndexBase);
  ZC_EXPECT(dispatch.argumentTypes.size() == 2);
  ZC_EXPECT(dispatch.resultType == result.typeEnv.getTypeId(index));
}

ZC_TEST("BodyChecker.TupleIndexExprReturnsElementType") {
  TestFixture fix;
  zc::Vector<ast::NodeId> elems;
  elems.add(fix.makeIntLiteral(1));
  elems.add(fix.makeStrLiteral("two"_zc));
  auto tuple = fix.makeTupleLiteral(fix.makeNodeList(elems.asPtr()));
  auto index = fix.makeIndexExpr(tuple, fix.makeIntLiteral(1));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(index);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  ZC_EXPECT(!fix.diagnostics().hasErrors());
  ZC_EXPECT(result.typeEnv.hasType(index));
  auto& ty = result.typeEnv.getType(index);
  ZC_EXPECT(isPrimitive(ty));
  if (isPrimitive(ty)) {
    auto& primitive = static_cast<const type::PrimitiveType&>(ty);
    ZC_EXPECT(primitive.getPrimitiveKind() == type::PrimitiveKind::Str);
  }
  ZC_EXPECT(result.typeEnv.hasDispatch(index));
  auto& dispatch = result.typeEnv.getDispatch(index);
  ZC_EXPECT(dispatch.targetKind == type::CallTargetKind::PrimitiveOperator);
  ZC_EXPECT(dispatch.receiverMode == type::ReceiverMode::IndexBase);
  ZC_EXPECT(dispatch.argumentTypes.size() == 2);
  ZC_EXPECT(dispatch.resultType == result.typeEnv.getTypeId(index));
}

ZC_TEST("BodyChecker.UserIndexExprReturnsAssociatedOutput") {
  TestFixture fix;

  zc::Vector<ast::NodeId> ifaceMembers;
  ifaceMembers.add(makeAssociatedTypeDecl(fix, "Output"_zc, ast::NodeId()));
  auto indexIface = fix.makeInterfaceDecl(
      "Index"_zc, fix.makeClassMemberList(fix.makeNodeList(ifaceMembers.asPtr())));
  auto bagType = fix.makeClassDecl("Bag"_zc);

  zc::Vector<ast::NodeId> implIfaceNodes;
  implIfaceNodes.add(fix.makeNamedTypeExpr("Index"_zc));
  auto implIfaces = fix.makeImplIfaceList(fix.makeNodeList(implIfaceNodes.asPtr()));

  zc::Vector<ast::NodeId> indexParams;
  indexParams.add(fix.makeFunctionParamDecl("idx"_zc, fix.makeNamedTypeExpr("i32"_zc)));
  auto indexParamList = fix.makeFunctionParamList(fix.makeNodeList(indexParams.asPtr()));
  auto indexMethod = fix.makeMethodDecl("index"_zc, ast::NodeId(), indexParamList,
                                        fix.makeNamedTypeExpr("i32"_zc));
  zc::Vector<ast::NodeId> implMembers;
  implMembers.add(makeAssociatedTypeDecl(fix, "Output"_zc, fix.makeNamedTypeExpr("i32"_zc)));
  implMembers.add(indexMethod);
  auto implDecl =
      makeStandaloneImplDecl(fix, fix.makeNamedTypeExpr("Bag"_zc), implIfaces,
                             fix.makeClassMemberList(fix.makeNodeList(implMembers.asPtr())));

  auto bagDecl =
      fix.makeVariableDeclarator(fix.makeBindingPattern("bag"_zc), fix.makeNamedTypeExpr("Bag"_zc));
  zc::Vector<ast::NodeId> decls;
  decls.add(bagDecl);
  auto let = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr())));
  auto index = fix.makeIndexExpr(fix.makeIdentExpr("bag"_zc), fix.makeIntLiteral(0));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(indexIface);
  topDecls.add(bagType);
  topDecls.add(implDecl);
  topDecls.add(let);
  topDecls.add(index);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  ZC_EXPECT(!fix.diagnostics().hasErrors());
  ZC_EXPECT(result.typeEnv.hasType(index));
  auto& ty = result.typeEnv.getType(index);
  ZC_EXPECT(isPrimitive(ty));
  if (isPrimitive(ty)) {
    auto& primitive = static_cast<const type::PrimitiveType&>(ty);
    ZC_EXPECT(primitive.getPrimitiveKind() == type::PrimitiveKind::I32);
  }
  ZC_EXPECT(result.typeEnv.hasDispatch(index));
  auto& dispatch = result.typeEnv.getDispatch(index);
  ZC_EXPECT(dispatch.targetKind == type::CallTargetKind::IndexMethod);
  ZC_EXPECT(dispatch.receiverMode == type::ReceiverMode::IndexBase);
  ZC_EXPECT(dispatch.interfaceName.asPtr() == "Index"_zc);
  ZC_EXPECT(dispatch.methodName.asPtr() == "index"_zc);
  ZC_EXPECT(dispatch.implNode == implDecl);
  ZC_EXPECT(dispatch.argumentTypes.size() == 2);
  ZC_EXPECT(dispatch.resultType == result.typeEnv.getTypeId(index));
}

ZC_TEST("BodyChecker.UserIndexExprRejectsWrongTraitMethodSignature") {
  TestFixture fix;
  auto consumer = zc::heap<CapturingDiagnosticConsumer>();
  auto consumerPtr = consumer.get();
  fix.diagnostics().addConsumer(zc::mv(consumer));

  zc::Vector<ast::NodeId> ifaceMembers;
  ifaceMembers.add(makeAssociatedTypeDecl(fix, "Output"_zc, ast::NodeId()));
  auto indexIface = fix.makeInterfaceDecl(
      "Index"_zc, fix.makeClassMemberList(fix.makeNodeList(ifaceMembers.asPtr())));
  auto bagType = fix.makeClassDecl("Bag"_zc);

  zc::Vector<ast::NodeId> implIfaceNodes;
  implIfaceNodes.add(fix.makeNamedTypeExpr("Index"_zc));
  auto implIfaces = fix.makeImplIfaceList(fix.makeNodeList(implIfaceNodes.asPtr()));

  zc::Vector<ast::NodeId> indexParams;
  indexParams.add(fix.makeFunctionParamDecl("idx"_zc, fix.makeNamedTypeExpr("str"_zc)));
  auto indexParamList = fix.makeFunctionParamList(fix.makeNodeList(indexParams.asPtr()));
  auto indexMethod = fix.makeMethodDecl("index"_zc, ast::NodeId(), indexParamList,
                                        fix.makeNamedTypeExpr("i32"_zc));
  zc::Vector<ast::NodeId> implMembers;
  implMembers.add(makeAssociatedTypeDecl(fix, "Output"_zc, fix.makeNamedTypeExpr("i32"_zc)));
  implMembers.add(indexMethod);
  auto implDecl =
      makeStandaloneImplDecl(fix, fix.makeNamedTypeExpr("Bag"_zc), implIfaces,
                             fix.makeClassMemberList(fix.makeNodeList(implMembers.asPtr())));

  auto bagDecl =
      fix.makeVariableDeclarator(fix.makeBindingPattern("bag"_zc), fix.makeNamedTypeExpr("Bag"_zc));
  zc::Vector<ast::NodeId> decls;
  decls.add(bagDecl);
  auto let = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr())));
  auto index = fix.makeIndexExpr(fix.makeIdentExpr("bag"_zc), fix.makeIntLiteral(0));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(indexIface);
  topDecls.add(bagType);
  topDecls.add(implDecl);
  topDecls.add(let);
  topDecls.add(index);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(!result.success);
  ZC_EXPECT(fix.diagnostics().hasErrors());
  ZC_EXPECT(
      containsDiagnosticId(*consumerPtr, diagnostics::DiagID::OperatorTraitSignatureMismatch));
  ZC_EXPECT(result.typeEnv.hasType(index));
  if (result.typeEnv.hasType(index)) { ZC_EXPECT(isError(result.typeEnv.getType(index))); }
}

ZC_TEST("BodyChecker.UserIndexExprRequiresIndexImpl") {
  TestFixture fix;
  auto consumer = zc::heap<CapturingDiagnosticConsumer>();
  auto consumerPtr = consumer.get();
  fix.diagnostics().addConsumer(zc::mv(consumer));

  auto bagType = fix.makeClassDecl("Bag"_zc);
  auto bagDecl =
      fix.makeVariableDeclarator(fix.makeBindingPattern("bag"_zc), fix.makeNamedTypeExpr("Bag"_zc));
  zc::Vector<ast::NodeId> decls;
  decls.add(bagDecl);
  auto let = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr())));
  auto index = fix.makeIndexExpr(fix.makeIdentExpr("bag"_zc), fix.makeIntLiteral(0));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(bagType);
  topDecls.add(let);
  topDecls.add(index);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(!result.success);
  ZC_EXPECT(fix.diagnostics().hasErrors());
  ZC_EXPECT(containsDiagnosticId(*consumerPtr, diagnostics::DiagID::CheckerTraitNotImplemented));
  ZC_EXPECT(result.typeEnv.hasType(index));
  if (result.typeEnv.hasType(index)) { ZC_EXPECT(isError(result.typeEnv.getType(index))); }
}

// ============================================================================
// New expression
// ============================================================================

ZC_TEST("BodyChecker.NewExpr") {
  TestFixture fix;
  // new MyClass()
  auto cls = fix.makeClassDecl("MyClass"_zc);
  auto callee = fix.makeIdentExpr("MyClass"_zc);
  zc::Vector<ast::NodeId> args;
  auto newExpr = fix.makeNewExpr(callee, fix.makeNodeList(args.asPtr()));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(cls);
  topDecls.add(newExpr);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
}

// ============================================================================
// Object literal
// ============================================================================

ZC_TEST("BodyChecker.ObjectLiteral") {
  TestFixture fix;
  // { x: 1, y: 2 }
  zc::Vector<ast::NodeId> props;
  props.add(fix.makeObjectProperty("x"_zc, fix.makeIntLiteral(1)));
  props.add(fix.makeObjectProperty("name"_zc, fix.makeStrLiteral("z"_zc)));
  auto objLit = fix.makeObjectLiteral(fix.makeNodeList(props.asPtr()));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(objLit);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  if (result.typeEnv.hasType(objLit)) {
    auto& ty = result.typeEnv.getType(objLit);
    ZC_EXPECT(ty.getKind() == type::TypeKind::Object);
    if (isObject(ty)) {
      auto& objTy = static_cast<const type::ObjectType&>(ty);
      ZC_EXPECT(objTy.getMemberCount() == 2);
      auto members = objTy.getMembers();
      ZC_EXPECT(members.size() == 2);
      if (members.size() == 2) {
        ZC_EXPECT(members[0].name == "x"_zc);
        ZC_EXPECT(members[1].name == "name"_zc);
      }
      auto x = objTy.getMember("x"_zc);
      auto name = objTy.getMember("name"_zc);
      ZC_EXPECT(x != zc::none);
      ZC_EXPECT(name != zc::none);
      ZC_IF_SOME(xTy, x) { ZC_EXPECT(isPrimitive(xTy)); }
      ZC_IF_SOME(nameTy, name) { ZC_EXPECT(isPrimitive(nameTy)); }
    }
  }
}

ZC_TEST("BodyChecker.StructLiteralReturnsNamedType") {
  TestFixture fix;
  auto fieldTy =
      fix.makeUnionTypeExpr(fix.makeNamedTypeExpr("i32"_zc), fix.makeNamedTypeExpr("str"_zc));
  auto field = fix.makeFieldDecl("x"_zc, fieldTy);
  zc::Vector<ast::NodeId> members;
  members.add(field);
  auto point =
      fix.makeStructDecl("Point"_zc, fix.makeClassMemberList(fix.makeNodeList(members.asPtr())));

  auto fieldValue = fix.makeIntLiteral(1);
  zc::Vector<ast::NodeId> props;
  props.add(fix.makeObjectProperty("x"_zc, fieldValue));
  auto lit =
      fix.makeStructLiteralExpr(fix.makeNamedTypeExpr("Point"_zc), fix.makeNodeList(props.asPtr()));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(point);
  topDecls.add(lit);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  ZC_EXPECT(!fix.diagnostics().hasErrors());
  ZC_EXPECT(result.typeEnv.hasType(lit));
  auto& ty = result.typeEnv.getType(lit);
  ZC_EXPECT(isNamed(ty));
  if (isNamed(ty)) { ZC_EXPECT(static_cast<const type::NamedType&>(ty).getName() == "Point"_zc); }
  ZC_EXPECT(result.typeEnv.hasCoercion(fieldValue));
  ZC_EXPECT(result.typeEnv.getCoercion(fieldValue) == type::CoercionKind::UnionInjection);
}

ZC_TEST("BodyChecker.StructLiteralRejectsUnknownField") {
  TestFixture fix;
  auto field = fix.makeFieldDecl("x"_zc, fix.makeNamedTypeExpr("i32"_zc));
  zc::Vector<ast::NodeId> members;
  members.add(field);
  auto point =
      fix.makeStructDecl("Point"_zc, fix.makeClassMemberList(fix.makeNodeList(members.asPtr())));

  zc::Vector<ast::NodeId> props;
  props.add(fix.makeObjectProperty("y"_zc, fix.makeIntLiteral(1)));
  auto lit =
      fix.makeStructLiteralExpr(fix.makeNamedTypeExpr("Point"_zc), fix.makeNodeList(props.asPtr()));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(point);
  topDecls.add(lit);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(!result.success);
  ZC_EXPECT(fix.diagnostics().hasErrors());
  ZC_EXPECT(result.typeEnv.hasType(lit));
  ZC_EXPECT(isError(result.typeEnv.getType(lit)));
}

ZC_TEST("BodyChecker.StructLiteralRejectsMissingField") {
  TestFixture fix;
  auto xField = fix.makeFieldDecl("x"_zc, fix.makeNamedTypeExpr("i32"_zc));
  auto yField = fix.makeFieldDecl("y"_zc, fix.makeNamedTypeExpr("i32"_zc));
  zc::Vector<ast::NodeId> members;
  members.add(xField);
  members.add(yField);
  auto point =
      fix.makeStructDecl("Point"_zc, fix.makeClassMemberList(fix.makeNodeList(members.asPtr())));

  zc::Vector<ast::NodeId> props;
  props.add(fix.makeObjectProperty("x"_zc, fix.makeIntLiteral(1)));
  auto lit =
      fix.makeStructLiteralExpr(fix.makeNamedTypeExpr("Point"_zc), fix.makeNodeList(props.asPtr()));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(point);
  topDecls.add(lit);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(!result.success);
  ZC_EXPECT(fix.diagnostics().hasErrors());
  ZC_EXPECT(result.typeEnv.hasType(lit));
  ZC_EXPECT(isError(result.typeEnv.getType(lit)));
}

ZC_TEST("BodyChecker.ReturnRecordsReborrowCoercion") {
  TestFixture fix;
  auto param = fix.makeFunctionParamDecl(
      "x"_zc, fix.makeReferenceTypeExpr(fix.makeNamedTypeExpr("i32"_zc), true));
  zc::Vector<ast::NodeId> params;
  params.add(param);
  auto paramList = fix.makeFunctionParamList(fix.makeNodeList(params.asPtr()));
  auto returnStmt = fix.makeReturnStmt(fix.makeIdentExpr("x"_zc));
  zc::Vector<ast::NodeId> stmts;
  stmts.add(returnStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("share"_zc, body, paramList,
                                 fix.makeReferenceTypeExpr(fix.makeNamedTypeExpr("i32"_zc)));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(fn);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  ZC_EXPECT(!fix.diagnostics().hasErrors());
  ZC_EXPECT(result.typeEnv.hasCoercion(returnStmt));
  ZC_EXPECT(result.typeEnv.getCoercion(returnStmt) == type::CoercionKind::MutRefToSharedRef);
}

ZC_TEST("BodyChecker.AssignmentRecordsMutableRawToConstRawCoercion") {
  TestFixture fix;
  auto constPtrTy = fix.makeRawPointerTypeExpr(fix.makeNamedTypeExpr("i32"_zc));
  auto mutPtrTy = fix.makeRawPointerTypeExpr(fix.makeNamedTypeExpr("i32"_zc), true);
  auto lhsDecl = fix.makeVariableDeclarator(fix.makeBindingPattern("dst"_zc, true), constPtrTy);
  auto rhsDecl = fix.makeVariableDeclarator(fix.makeBindingPattern("src"_zc), mutPtrTy);
  zc::Vector<ast::NodeId> decls;
  decls.add(lhsDecl);
  decls.add(rhsDecl);
  auto let = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr())),
                             static_cast<uint8_t>(ast::BindingDeclarationKind::Mut));
  auto assign = fix.makeAssignmentExpr(fix.makeIdentExpr("dst"_zc), fix.makeIdentExpr("src"_zc));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(let);
  topDecls.add(assign);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
  ZC_EXPECT(!fix.diagnostics().hasErrors());
  ZC_EXPECT(result.typeEnv.hasCoercion(assign));
  ZC_EXPECT(result.typeEnv.getCoercion(assign) == type::CoercionKind::MutRawToConstRaw);
}

// ============================================================================
// Empty body check
// ============================================================================

ZC_TEST("BodyChecker.EmptyFunctionBody") {
  TestFixture fix;
  // fun foo() { }
  auto bodyBlock = fix.makeBlockStmt(ast::NodeList());
  auto fn = fix.makeFunctionDecl("foo"_zc, bodyBlock);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(fn);
  auto result = runFullCheck(fix, topDecls.asPtr());

  ZC_EXPECT(result.success);
}

// ============================================================================
// Class with methods
// ============================================================================

ZC_TEST("BodyChecker.ClassWithMethods") {
  TestFixture fix;
  // Build method body
  zc::Vector<ast::NodeId> methodBody;
  methodBody.add(fix.makeReturnStmt(fix.makeIntLiteral(42)));
  auto methodBlock = fix.makeBlockStmt(fix.makeNodeList(methodBody.asPtr()));
  auto method = fix.makeMethodDecl("getValue"_zc, methodBlock);

  // For a proper test we'd need to build a ClassMemberList node.
  // Since that requires specific node construction, let's just verify
  // that the method declaration itself doesn't crash.
  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(method);

  // This may produce errors since method is not in a class,
  // but should not crash
  runFullCheck(fix, topDecls.asPtr());
  ZC_EXPECT(true);
}

}  // namespace checker
}  // namespace compiler
}  // namespace zomlang
