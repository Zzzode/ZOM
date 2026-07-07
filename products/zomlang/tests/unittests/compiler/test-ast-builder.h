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

#pragma once

#include "zc/core/memory.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/ast/tree.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/source/manager.h"
#include "zomlang/compiler/symbol/symbol-table.h"

namespace zomlang {
namespace compiler {
namespace tests {

/// \brief Test fixture helper for building AST trees and binder/checker infrastructure.
///
/// Provides convenient factory methods for constructing AST nodes and
/// setting up the compilation pipeline (SourceManager, DiagnosticEngine,
/// SymbolTable, BindingMetadata) for testing.
class TestFixture {
public:
  TestFixture()
      : sourceManager_(zc::heap<source::SourceManager>()),
        diagnostics_(zc::heap<diagnostics::DiagnosticEngine>(*sourceManager_)),
        symbols_(),
        metadata_() {}

  // ==========================================================================
  // Infrastructure accessors
  // ==========================================================================

  source::SourceManager& sourceManager() { return *sourceManager_; }
  diagnostics::DiagnosticEngine& diagnostics() { return *diagnostics_; }
  symbol::SymbolTable& symbols() { return symbols_; }
  ast::BindingMetadata& metadata() { return metadata_; }
  symbol::ScopeManager& scopes() { return symbols_.getScopeManager(); }

  // ==========================================================================
  // Tree builder helpers
  // ==========================================================================

  ast::TreeBuilder& builder() { return builder_; }

  ast::Tree finishTree() {
    // Do NOT call metadata_.resizeFor() here — Binder/DeclCollector handle it.
    // Calling it prematurely can cause issues with the traversal.
    return builder_.finish();
  }

  // ==========================================================================
  // AST node factory methods
  // ==========================================================================

  /// \brief Create a SourceFile node with the given statements.
  ast::NodeId makeSourceFile(ast::NodeId module, ast::NodeList statements,
                             zc::StringPtr fileName = "test.zom"_zc) {
    ast::NodePayload payload;
    auto fileNameId = builder_.internString(fileName);
    payload.words[ast::kSourceFileFileNameWord] = fileNameId.value;
    payload.words[ast::kSourceFileModuleWord] = module.value;
    payload.words[ast::kSourceFileStatementsFirstWord] = statements.first;
    payload.words[ast::kSourceFileStatementsSizeWord] = statements.size;
    auto id = builder_.makeNode(ast::SyntaxKind::SourceFile, source::SourceRange(), payload);
    builder_.setRoot(id);
    return id;
  }

  /// \brief Create a ModuleDeclaration node.
  ast::NodeId makeModuleDecl(zc::StringPtr name) {
    ast::NodePayload payload;
    auto pathId = builder_.internString(name);
    payload.words[ast::kModuleDeclarationPathWord] = pathId.value;
    return builder_.makeNode(ast::SyntaxKind::ModuleDeclaration, source::SourceRange(), payload);
  }

  /// \brief Create a StatementListItem wrapping a declaration.
  ast::NodeId makeStatementListItem(ast::NodeId item) {
    ast::NodePayload payload;
    payload.words[ast::kStatementListItemItemWord] = item.value;
    payload.words[ast::kStatementListItemAttrsWord] = 0;
    return builder_.makeNode(ast::SyntaxKind::StatementListItem, source::SourceRange(), payload);
  }

  /// \brief Create a FunctionDecl with name and body.
  ast::NodeId makeFunctionDecl(zc::StringPtr name, ast::NodeId body = ast::NodeId(),
                               ast::NodeId params = ast::NodeId(),
                               ast::NodeId retTy = ast::NodeId(),
                               ast::NodeId raisesTy = ast::NodeId(),
                               ast::NodeId typeParams = ast::NodeId()) {
    ast::NodePayload payload;
    auto nameId = builder_.internIdent(name);
    payload.words[ast::kFunctionDeclNameWord] = nameId.value;
    payload.words[ast::kFunctionDeclParamsIdWord] = params.value;
    payload.words[ast::kFunctionDeclTypeParamsIdWord] = typeParams.value;
    payload.words[ast::kFunctionDeclRetTyWord] = retTy.value;
    payload.words[ast::kFunctionDeclRaisesTyWord] = raisesTy.value;
    payload.words[ast::kFunctionDeclBodyWord] = body.value;
    return builder_.makeNode(ast::SyntaxKind::FunctionDecl, source::SourceRange(), payload);
  }

  /// \brief Create a ClassDecl with name, optional extends, and optional members.
  ast::NodeId makeClassDecl(zc::StringPtr name, ast::NodeId extends = ast::NodeId(),
                            ast::NodeId members = ast::NodeId(), uint8_t extensibility = 0) {
    ast::NodePayload payload;
    auto nameId = builder_.internIdent(name);
    payload.words[ast::kClassDeclNameWord] = nameId.value;
    payload.words[ast::kClassDeclExtensibilityWord] = extensibility;
    payload.words[ast::kClassDeclTypeParamsIdWord] = 0;
    payload.words[ast::kClassDeclExtendsWord] = extends.value;
    payload.words[ast::kClassDeclMembersIdWord] = members.value;
    return builder_.makeNode(ast::SyntaxKind::ClassDecl, source::SourceRange(), payload);
  }

  /// \brief Create an InterfaceDecl with name.
  ast::NodeId makeInterfaceDecl(zc::StringPtr name, ast::NodeId members = ast::NodeId()) {
    ast::NodePayload payload;
    auto nameId = builder_.internIdent(name);
    payload.words[ast::kInterfaceDeclNameWord] = nameId.value;
    payload.words[ast::kInterfaceDeclTypeParamsIdWord] = 0;
    payload.words[ast::kInterfaceDeclMembersIdWord] = members.value;
    return builder_.makeNode(ast::SyntaxKind::InterfaceDecl, source::SourceRange(), payload);
  }

  /// \brief Create a StructDecl with name.
  ast::NodeId makeStructDecl(zc::StringPtr name, ast::NodeId members = ast::NodeId()) {
    ast::NodePayload payload;
    auto nameId = builder_.internIdent(name);
    payload.words[ast::kStructDeclNameWord] = nameId.value;
    payload.words[ast::kStructDeclExtensibilityWord] = 0;
    payload.words[ast::kStructDeclTypeParamsIdWord] = 0;
    payload.words[ast::kStructDeclMembersIdWord] = members.value;
    return builder_.makeNode(ast::SyntaxKind::StructDecl, source::SourceRange(), payload);
  }

  /// \brief Create a ClassMemberList.
  ast::NodeId makeClassMemberList(ast::NodeList members) {
    ast::NodePayload payload;
    payload.words[ast::kClassMemberListNmembersWord] = members.size;
    payload.words[ast::kClassMemberListMembersFirstWord] = members.first;
    payload.words[ast::kClassMemberListMembersSizeWord] = members.size;
    return builder_.makeNode(ast::SyntaxKind::ClassMemberList, source::SourceRange(), payload);
  }

  /// \brief Create an EnumDeclaration with name.
  ast::NodeId makeEnumDecl(zc::StringPtr name, ast::NodeId variants = ast::NodeId()) {
    ast::NodePayload payload;
    auto nameId = builder_.internIdent(name);
    payload.words[ast::kEnumDeclarationNameWord] = nameId.value;
    payload.words[ast::kEnumDeclarationExtensibilityWord] = 0;
    payload.words[ast::kEnumDeclarationTypeParamsIdWord] = 0;
    payload.words[ast::kEnumDeclarationNvarsWord] = 0;
    payload.words[ast::kEnumDeclarationVariantsIdWord] = variants.value;
    payload.words[ast::kEnumDeclarationBaseReprWord] = 0;
    return builder_.makeNode(ast::SyntaxKind::EnumDeclaration, source::SourceRange(), payload);
  }

  /// \brief Create an AliasDecl (type alias).
  ast::NodeId makeAliasDecl(zc::StringPtr name, ast::NodeId target = ast::NodeId()) {
    ast::NodePayload payload;
    auto nameId = builder_.internIdent(name);
    payload.words[ast::kAliasDeclNameWord] = nameId.value;
    payload.words[ast::kAliasDeclTypeParamsIdWord] = 0;
    payload.words[ast::kAliasDeclTargetWord] = target.value;
    return builder_.makeNode(ast::SyntaxKind::AliasDecl, source::SourceRange(), payload);
  }

  /// \brief Create a LetStmt with a VariableDeclaratorList.
  ast::NodeId makeLetStmt(ast::NodeId declList, uint8_t kind = 0) {
    ast::NodePayload payload;
    payload.words[ast::kLetStmtKindWord] = kind;
    payload.words[ast::kLetStmtDeclarationsWord] = declList.value;
    return builder_.makeNode(ast::SyntaxKind::LetStmt, source::SourceRange(), payload);
  }

  /// \brief Create a VariableDeclarator with pattern, type, and initializer.
  ast::NodeId makeVariableDeclarator(ast::NodeId pattern, ast::NodeId ty = ast::NodeId(),
                                     ast::NodeId init = ast::NodeId()) {
    ast::NodePayload payload;
    payload.words[ast::kVariableDeclaratorPatternWord] = pattern.value;
    payload.words[ast::kVariableDeclaratorTyWord] = ty.value;
    payload.words[ast::kVariableDeclaratorInitWord] = init.value;
    return builder_.makeNode(ast::SyntaxKind::VariableDeclarator, source::SourceRange(), payload);
  }

  /// \brief Create a VariableDeclaratorList.
  ast::NodeId makeVariableDeclaratorList(ast::NodeList decls) {
    ast::NodePayload payload;
    payload.words[ast::kVariableDeclaratorListNDeclsWord] = decls.size;
    payload.words[ast::kVariableDeclaratorListDeclsFirstWord] = decls.first;
    payload.words[ast::kVariableDeclaratorListDeclsSizeWord] = decls.size;
    return builder_.makeNode(ast::SyntaxKind::VariableDeclaratorList, source::SourceRange(),
                             payload);
  }

  /// \brief Create a BindingPattern (e.g., `x` in `let x = ...`).
  ast::NodeId makeBindingPattern(zc::StringPtr name, bool isMut = false,
                                 ast::NodeId subPattern = ast::NodeId()) {
    ast::NodePayload payload;
    auto nameId = builder_.internIdent(name);
    payload.words[ast::kBindingPatternNameWord] = nameId.value;
    payload.words[ast::kBindingPatternIsMutWord] = isMut ? 1 : 0;
    payload.words[ast::kBindingPatternIsRefWord] = 0;
    payload.words[ast::kBindingPatternSubWord] = subPattern.value;
    return builder_.makeNode(ast::SyntaxKind::BindingPattern, source::SourceRange(), payload);
  }

  /// \brief Create a BlockStmt with statements.
  ast::NodeId makeBlockStmt(ast::NodeList stmts) {
    ast::NodePayload payload;
    payload.words[ast::kBlockStmtStmtsFirstWord] = stmts.first;
    payload.words[ast::kBlockStmtStmtsSizeWord] = stmts.size;
    return builder_.makeNode(ast::SyntaxKind::BlockStmt, source::SourceRange(), payload);
  }

  /// \brief Create an ExpressionStatement.
  ast::NodeId makeExpressionStatement(ast::NodeId expr) {
    ast::NodePayload payload;
    payload.words[ast::kExpressionStatementExpressionWord] = expr.value;
    return builder_.makeNode(ast::SyntaxKind::ExpressionStatement, source::SourceRange(), payload);
  }

  /// \brief Create an IdentExpr (identifier reference).
  ast::NodeId makeIdentExpr(zc::StringPtr name) {
    ast::NodePayload payload;
    auto nameId = builder_.internIdent(name);
    payload.words[ast::kIdentExprNameWord] = nameId.value;
    return builder_.makeNode(ast::SyntaxKind::IdentExpr, source::SourceRange(), payload);
  }

  /// \brief Create an IntLiteral.
  ast::NodeId makeIntLiteral(int64_t value) {
    ast::NodePayload payload;
    auto valId = builder_.internBigInt(zc::str(value));
    payload.words[ast::kIntLiteralBaseWord] = 10;
    payload.words[ast::kIntLiteralValueWord] = valId.value;
    return builder_.makeNode(ast::SyntaxKind::IntLiteral, source::SourceRange(), payload);
  }

  /// \brief Create a FloatLiteralExpr.
  ast::NodeId makeFloatLiteral(double value) {
    ast::NodePayload payload;
    auto valId = builder_.internFloat(zc::str(value));
    payload.words[ast::kFloatLiteralExprWidthWord] = 64;
    payload.words[ast::kFloatLiteralExprValueWord] = valId.value;
    return builder_.makeNode(ast::SyntaxKind::FloatLiteralExpr, source::SourceRange(), payload);
  }

  /// \brief Create a StrLiteral.
  ast::NodeId makeStrLiteral(zc::StringPtr value) {
    ast::NodePayload payload;
    auto valId = builder_.internString(value);
    payload.words[ast::kStrLiteralValueWord] = valId.value;
    payload.words[ast::kStrLiteralIsRawWord] = 0;
    payload.words[ast::kStrLiteralPrefixWord] = 0;
    return builder_.makeNode(ast::SyntaxKind::StrLiteral, source::SourceRange(), payload);
  }

  /// \brief Create a BoolLiteral.
  ast::NodeId makeBoolLiteral(bool value) {
    ast::NodePayload payload;
    payload.words[ast::kBoolLiteralValueWord] = value ? 1 : 0;
    return builder_.makeNode(ast::SyntaxKind::BoolLiteral, source::SourceRange(), payload);
  }

  /// \brief Create a NullLiteral.
  ast::NodeId makeNullLiteral() {
    return builder_.makeNode(ast::SyntaxKind::NullLiteral, source::SourceRange());
  }

  /// \brief Create a UnitLiteral.
  ast::NodeId makeUnitLiteral() {
    return builder_.makeNode(ast::SyntaxKind::UnitLiteral, source::SourceRange());
  }

  /// \brief Create a BinaryExpr.
  ast::NodeId makeBinaryExpr(ast::BinaryOperatorKind op, ast::NodeId lhs, ast::NodeId rhs) {
    ast::NodePayload payload;
    payload.words[ast::kBinaryExprOpWord] = static_cast<uint32_t>(op);
    payload.words[ast::kBinaryExprLhsWord] = lhs.value;
    payload.words[ast::kBinaryExprRhsWord] = rhs.value;
    return builder_.makeNode(ast::SyntaxKind::BinaryExpr, source::SourceRange(), payload);
  }

  /// \brief Create a CallExpression.
  ast::NodeId makeCallExpr(ast::NodeId callee, ast::NodeList args) {
    return makeCallExpr(callee, ast::NodeList(), args);
  }

  /// \brief Create a CallExpression with explicit type arguments.
  ast::NodeId makeCallExpr(ast::NodeId callee, ast::NodeList typeArgs, ast::NodeList args) {
    ast::NodePayload payload;
    payload.words[ast::kCallExpressionCalleeWord] = callee.value;
    payload.words[ast::kCallExpressionTypeArgsFirstWord] = typeArgs.first;
    payload.words[ast::kCallExpressionTypeArgsSizeWord] = typeArgs.size;
    payload.words[ast::kCallExpressionArgsFirstWord] = args.first;
    payload.words[ast::kCallExpressionArgsSizeWord] = args.size;
    return builder_.makeNode(ast::SyntaxKind::CallExpression, source::SourceRange(), payload);
  }

  /// \brief Create a ReturnStmt.
  ast::NodeId makeReturnStmt(ast::NodeId value = ast::NodeId()) {
    ast::NodePayload payload;
    payload.words[ast::kReturnStmtValueWord] = value.value;
    return builder_.makeNode(ast::SyntaxKind::ReturnStmt, source::SourceRange(), payload);
  }

  /// \brief Create an IfStmt.
  ast::NodeId makeIfStmt(ast::NodeId cond, ast::NodeId thenStmt,
                         ast::NodeId elseStmt = ast::NodeId()) {
    ast::NodePayload payload;
    payload.words[ast::kIfStmtCondWord] = cond.value;
    payload.words[ast::kIfStmtThenStmtWord] = thenStmt.value;
    payload.words[ast::kIfStmtElseStmtWord] = elseStmt.value;
    return builder_.makeNode(ast::SyntaxKind::IfStmt, source::SourceRange(), payload);
  }

  /// \brief Create a WhileStmt.
  ast::NodeId makeWhileStmt(ast::NodeId cond, ast::NodeId body) {
    ast::NodePayload payload;
    payload.words[ast::kWhileStmtCondWord] = cond.value;
    payload.words[ast::kWhileStmtBodyWord] = body.value;
    return builder_.makeNode(ast::SyntaxKind::WhileStmt, source::SourceRange(), payload);
  }

  /// \brief Create a MatchStmt with scrutinee and arms.
  ast::NodeId makeMatchStmt(ast::NodeId scrutinee, ast::NodeList arms) {
    ast::NodePayload payload;
    payload.words[ast::kMatchStmtScrutineeWord] = scrutinee.value;
    payload.words[ast::kMatchStmtArmsFirstWord] = arms.first;
    payload.words[ast::kMatchStmtArmsSizeWord] = arms.size;
    return builder_.makeNode(ast::SyntaxKind::MatchStmt, source::SourceRange(), payload);
  }

  /// \brief Create a MatchArmStmt with pattern, optional guard, and body.
  ast::NodeId makeMatchArmStmt(ast::NodeId pattern, ast::NodeId body,
                               ast::NodeId guard = ast::NodeId()) {
    ast::NodePayload payload;
    payload.words[ast::kMatchArmStmtPatternWord] = pattern.value;
    payload.words[ast::kMatchArmStmtGuardWord] = guard.value;
    payload.words[ast::kMatchArmStmtBodyWord] = body.value;
    return builder_.makeNode(ast::SyntaxKind::MatchArmStmt, source::SourceRange(), payload);
  }

  /// \brief Create a WildcardPattern.
  ast::NodeId makeWildcardPattern() {
    ast::NodePayload payload;
    payload.words[ast::kWildcardPatternTyWord] = 0;
    return builder_.makeNode(ast::SyntaxKind::WildcardPattern, source::SourceRange(), payload);
  }

  /// \brief Create a LiteralPattern with a literal value.
  ast::NodeId makeLiteralPattern(ast::NodeId literal) {
    ast::NodePayload payload;
    payload.words[ast::kLiteralPatternLiteralWord] = literal.value;
    return builder_.makeNode(ast::SyntaxKind::LiteralPattern, source::SourceRange(), payload);
  }

  /// \brief Create an IdentifierPattern (for enum variant patterns).
  ast::NodeId makeIdentifierPattern(zc::StringPtr name) {
    ast::NodePayload payload;
    auto nameId = builder_.internIdent(name);
    payload.words[ast::kIdentifierPatternNameWord] = nameId.value;
    payload.words[ast::kIdentifierPatternTyWord] = 0;
    return builder_.makeNode(ast::SyntaxKind::IdentifierPattern, source::SourceRange(), payload);
  }

  /// \brief Create an ImportDeclaration.
  ast::NodeId makeImportDecl(zc::StringPtr path, zc::StringPtr alias = zc::StringPtr(),
                             ast::NodeList specifiers = ast::NodeList()) {
    // Build a ModulePath node from the path string. The path may contain "::" separators.
    zc::Vector<ast::IdentId> segIds;
    const char* data = path.cStr();
    size_t len = path.size();
    size_t start = 0;
    for (size_t i = 0; i <= len; ++i) {
      if (i + 1 < len && data[i] == ':' && data[i + 1] == ':') {
        if (i > start) {
          // heapString creates a NUL-terminated copy that StringPtr requires
          segIds.add(builder_.internIdent(zc::heapString(data + start, i - start)));
        }
        start = i + 2;
        ++i;  // skip second colon
      } else if (i == len) {
        if (i > start) {
          segIds.add(builder_.internIdent(zc::heapString(data + start, i - start)));
        }
      }
    }
    if (segIds.empty()) {
      // Fallback: use entire path as one segment
      segIds.add(builder_.internIdent(path));
    }
    auto identList = builder_.makeIdentList(segIds.asPtr());

    // Create ModulePath node manually (TreeBuilder doesn't have makeModulePath)
    ast::NodePayload modPathPayload;
    modPathPayload.words[ast::kModulePathSegmentsFirstWord] = identList.first;
    modPathPayload.words[ast::kModulePathSegmentsSizeWord] = identList.size;
    auto modulePath =
        builder_.makeNode(ast::SyntaxKind::ModulePath, source::SourceRange(), modPathPayload);

    ast::NodePayload payload;
    payload.words[ast::kImportDeclarationPathWord] = modulePath.value;
    if (alias.size() > 0) {
      auto aliasId = builder_.internIdent(alias);
      payload.words[ast::kImportDeclarationAliasWord] = aliasId.value;
    }
    payload.words[ast::kImportDeclarationSpecifiersFirstWord] = specifiers.first;
    payload.words[ast::kImportDeclarationSpecifiersSizeWord] = specifiers.size;
    return builder_.makeNode(ast::SyntaxKind::ImportDeclaration, source::SourceRange(), payload);
  }

  /// \brief Create an ExportDeclaration wrapping a declaration.
  ast::NodeId makeExportDecl(ast::NodeId decl) {
    ast::NodePayload payload;
    payload.words[ast::kExportDeclarationDeclarationWord] = decl.value;
    payload.words[ast::kExportDeclarationPathWord] = 0;
    payload.words[ast::kExportDeclarationSpecifiersFirstWord] = 0;
    return builder_.makeNode(ast::SyntaxKind::ExportDeclaration, source::SourceRange(), payload);
  }

  /// \brief Create a NamedTypeExpr referencing a type by name.
  ///
  /// The path must be an IdentExpr node (not a raw IdentId), because
  /// NameResolver::resolveNamedTypeExpr expects tree.contains(pathId) to be true
  /// and then reads the node kind to dispatch.
  ast::NodeId makeNamedTypeExpr(zc::StringPtr name) {
    auto identExpr = makeIdentExpr(name);
    ast::NodePayload payload;
    payload.words[ast::kNamedTypeExprPathWord] = identExpr.value;
    payload.words[ast::kNamedTypeExprArgsFirstWord] = 0;
    payload.words[ast::kNamedTypeExprArgsSizeWord] = 0;
    return builder_.makeNode(ast::SyntaxKind::NamedTypeExpr, source::SourceRange(), payload);
  }

  /// \brief Create a NamedTypeExpr with type arguments.
  ast::NodeId makeNamedTypeExpr(zc::StringPtr name, ast::NodeList args) {
    auto identExpr = makeIdentExpr(name);
    ast::NodePayload payload;
    payload.words[ast::kNamedTypeExprPathWord] = identExpr.value;
    payload.words[ast::kNamedTypeExprArgsFirstWord] = args.first;
    payload.words[ast::kNamedTypeExprArgsSizeWord] = args.size;
    return builder_.makeNode(ast::SyntaxKind::NamedTypeExpr, source::SourceRange(), payload);
  }

  /// \brief Create a MemberExpression.
  ast::NodeId makeMemberExpr(ast::NodeId object, zc::StringPtr property) {
    ast::NodePayload payload;
    payload.words[ast::kMemberExpressionObjectWord] = object.value;
    auto propId = builder_.internIdent(property);
    payload.words[ast::kMemberExpressionPropertyWord] = propId.value;
    return builder_.makeNode(ast::SyntaxKind::MemberExpression, source::SourceRange(), payload);
  }

  /// \brief Create a FunctionParameterDecl.
  ast::NodeId makeFunctionParamDecl(zc::StringPtr name, ast::NodeId ty = ast::NodeId(),
                                    ast::NodeId def = ast::NodeId()) {
    ast::NodePayload payload;
    auto nameId = builder_.internIdent(name);
    payload.words[ast::kFunctionParameterDeclNameWord] = nameId.value;
    payload.words[ast::kFunctionParameterDeclTyWord] = ty.value;
    payload.words[ast::kFunctionParameterDeclDefaultWord] = def.value;
    payload.words[ast::kFunctionParameterDeclAttrsWord] = 0;
    return builder_.makeNode(ast::SyntaxKind::FunctionParameterDecl, source::SourceRange(),
                             payload);
  }

  /// \brief Create a FunctionParameterList.
  ast::NodeId makeFunctionParamList(ast::NodeList params) {
    ast::NodePayload payload;
    payload.words[ast::kFunctionParameterListNparamsWord] = params.size;
    payload.words[ast::kFunctionParameterListParamsFirstWord] = params.first;
    payload.words[ast::kFunctionParameterListParamsSizeWord] = params.size;
    return builder_.makeNode(ast::SyntaxKind::FunctionParameterList, source::SourceRange(),
                             payload);
  }

  /// \brief Create a GenericTypeParam.
  ast::NodeId makeGenericTypeParam(zc::StringPtr name, ast::NodeId bound = ast::NodeId(),
                                   ast::NodeId defaultTy = ast::NodeId(), uint8_t variance = 0) {
    ast::NodePayload payload;
    payload.words[ast::kGenericTypeParamNameWord] = builder_.internIdent(name).value;
    payload.words[ast::kGenericTypeParamBoundWord] = bound.value;
    payload.words[ast::kGenericTypeParamDefaultTyWord] = defaultTy.value;
    payload.words[ast::kGenericTypeParamVarianceWord] = variance;
    return builder_.makeNode(ast::SyntaxKind::GenericTypeParam, source::SourceRange(), payload);
  }

  /// \brief Create a GenericParams list.
  ast::NodeId makeGenericParams(ast::NodeList params, ast::NodeId whereClause = ast::NodeId()) {
    ast::NodePayload payload;
    payload.words[ast::kGenericParamsNparamsWord] = params.size;
    payload.words[ast::kGenericParamsParamsFirstWord] = params.first;
    payload.words[ast::kGenericParamsParamsSizeWord] = params.size;
    payload.words[ast::kGenericParamsWhereWord] = whereClause.value;
    return builder_.makeNode(ast::SyntaxKind::GenericParams, source::SourceRange(), payload);
  }

  /// \brief Create a WherePred.
  ast::NodeId makeWherePred(ast::NodeId ty, ast::NodeId bound,
                            ast::WhereBoundKind kind = ast::WhereBoundKind::Implements) {
    ast::NodePayload payload;
    payload.words[ast::kWherePredKindWord] = static_cast<uint32_t>(kind);
    payload.words[ast::kWherePredTyWord] = ty.value;
    payload.words[ast::kWherePredBoundWord] = bound.value;
    return builder_.makeNode(ast::SyntaxKind::WherePred, source::SourceRange(), payload);
  }

  /// \brief Create a WhereClause.
  ast::NodeId makeWhereClause(ast::NodeList predicates) {
    ast::NodePayload payload;
    payload.words[ast::kWhereClausePredsFirstWord] = predicates.first;
    payload.words[ast::kWhereClausePredsSizeWord] = predicates.size;
    return builder_.makeNode(ast::SyntaxKind::WhereClause, source::SourceRange(), payload);
  }

  /// \brief Create a MethodDecl.
  ast::NodeId makeMethodDecl(zc::StringPtr name, ast::NodeId body = ast::NodeId(),
                             ast::NodeId params = ast::NodeId(), ast::NodeId retTy = ast::NodeId(),
                             bool isStatic = false, ast::NodeId typeParams = ast::NodeId()) {
    ast::NodePayload payload;
    auto nameId = builder_.internIdent(name);
    payload.words[ast::kMethodDeclNameWord] = nameId.value;
    payload.words[ast::kMethodDeclParamsIdWord] = params.value;
    payload.words[ast::kMethodDeclTypeParamsIdWord] = typeParams.value;
    payload.words[ast::kMethodDeclRetTyWord] = retTy.value;
    payload.words[ast::kMethodDeclBodyWord] = body.value;
    payload.words[ast::kMethodDeclIsStaticWord] = isStatic ? 1 : 0;
    payload.words[ast::kMethodDeclVisibilityWord] = 0;
    return builder_.makeNode(ast::SyntaxKind::MethodDecl, source::SourceRange(), payload);
  }

  /// \brief Create a FieldDecl.
  ast::NodeId makeFieldDecl(zc::StringPtr name, ast::NodeId ty = ast::NodeId(),
                            ast::NodeId init = ast::NodeId(), bool isMut = false,
                            bool isStatic = false) {
    ast::NodePayload payload;
    auto nameId = builder_.internIdent(name);
    payload.words[ast::kFieldDeclNameWord] = nameId.value;
    payload.words[ast::kFieldDeclTyWord] = ty.value;
    payload.words[ast::kFieldDeclInitWord] = init.value;
    payload.words[ast::kFieldDeclIsMutWord] = isMut ? 1 : 0;
    payload.words[ast::kFieldDeclIsStaticWord] = isStatic ? 1 : 0;
    payload.words[ast::kFieldDeclVisibilityWord] = 0;
    return builder_.makeNode(ast::SyntaxKind::FieldDecl, source::SourceRange(), payload);
  }

  /// \brief Create a LambdaExpression.
  ast::NodeId makeLambdaExpr(ast::NodeId body, ast::NodeId params = ast::NodeId(),
                             ast::NodeId retTy = ast::NodeId()) {
    ast::NodePayload payload;
    payload.words[ast::kLambdaExpressionParamsIdWord] = params.value;
    payload.words[ast::kLambdaExpressionRetTyWord] = retTy.value;
    payload.words[ast::kLambdaExpressionRaisesTyWord] = 0;
    payload.words[ast::kLambdaExpressionBodyWord] = body.value;
    payload.words[ast::kLambdaExpressionExprBodyWord] = 0;
    return builder_.makeNode(ast::SyntaxKind::LambdaExpression, source::SourceRange(), payload);
  }

  /// \brief Create a FunctionExpression.
  ast::NodeId makeFunctionExpr(ast::NodeId body, ast::NodeId params = ast::NodeId(),
                               ast::NodeId retTy = ast::NodeId()) {
    ast::NodePayload payload;
    payload.words[ast::kFunctionExpressionParamsIdWord] = params.value;
    payload.words[ast::kFunctionExpressionTypeParamsIdWord] = 0;
    payload.words[ast::kFunctionExpressionCapturesIdWord] = 0;
    payload.words[ast::kFunctionExpressionRetTyWord] = retTy.value;
    payload.words[ast::kFunctionExpressionRaisesTyWord] = 0;
    payload.words[ast::kFunctionExpressionBodyWord] = body.value;
    return builder_.makeNode(ast::SyntaxKind::FunctionExpression, source::SourceRange(), payload);
  }

  /// \brief Create a ConditionalExpr (ternary).
  ast::NodeId makeConditionalExpr(ast::NodeId cond, ast::NodeId thenExpr, ast::NodeId elseExpr) {
    ast::NodePayload payload;
    payload.words[ast::kConditionalExprCondWord] = cond.value;
    payload.words[ast::kConditionalExprThenExprWord] = thenExpr.value;
    payload.words[ast::kConditionalExprElseExprWord] = elseExpr.value;
    return builder_.makeNode(ast::SyntaxKind::ConditionalExpr, source::SourceRange(), payload);
  }

  /// \brief Create a NullCoalesceExpr.
  ast::NodeId makeNullCoalesceExpr(ast::NodeId primary, ast::NodeId fallback) {
    ast::NodePayload payload;
    payload.words[ast::kNullCoalesceExprPrimaryWord] = primary.value;
    payload.words[ast::kNullCoalesceExprFallbackWord] = fallback.value;
    return builder_.makeNode(ast::SyntaxKind::NullCoalesceExpr, source::SourceRange(), payload);
  }

  /// \brief Create an AssignmentExpr.
  ast::NodeId makeAssignmentExpr(ast::NodeId lhs, ast::NodeId rhs, uint8_t op = 0) {
    ast::NodePayload payload;
    payload.words[ast::kAssignmentExprOpWord] = op;
    payload.words[ast::kAssignmentExprLhsWord] = lhs.value;
    payload.words[ast::kAssignmentExprRhsWord] = rhs.value;
    return builder_.makeNode(ast::SyntaxKind::AssignmentExpr, source::SourceRange(), payload);
  }

  /// \brief Create an ArrayLiteral.
  ast::NodeId makeArrayLiteral(ast::NodeList elems) {
    ast::NodePayload payload;
    payload.words[ast::kArrayLiteralElemsFirstWord] = elems.first;
    payload.words[ast::kArrayLiteralElemsSizeWord] = elems.size;
    return builder_.makeNode(ast::SyntaxKind::ArrayLiteral, source::SourceRange(), payload);
  }

  /// \brief Create a TupleLiteral.
  ast::NodeId makeTupleLiteral(ast::NodeList elems) {
    ast::NodePayload payload;
    payload.words[ast::kTupleLiteralElemsFirstWord] = elems.first;
    payload.words[ast::kTupleLiteralElemsSizeWord] = elems.size;
    return builder_.makeNode(ast::SyntaxKind::TupleLiteral, source::SourceRange(), payload);
  }

  /// \brief Create an ObjectLiteralExpr.
  ast::NodeId makeObjectLiteral(ast::NodeList properties) {
    ast::NodePayload payload;
    payload.words[ast::kObjectLiteralExprPropertiesFirstWord] = properties.first;
    payload.words[ast::kObjectLiteralExprPropertiesSizeWord] = properties.size;
    return builder_.makeNode(ast::SyntaxKind::ObjectLiteralExpr, source::SourceRange(), payload);
  }

  /// \brief Create an ObjectProperty.
  ast::NodeId makeObjectProperty(zc::StringPtr name, ast::NodeId value, bool shortForm = false) {
    ast::NodePayload payload;
    payload.words[ast::kObjectPropertyNameWord] = builder_.internIdent(name).value;
    payload.words[ast::kObjectPropertyValueWord] = value.value;
    payload.words[ast::kObjectPropertyShortFormWord] = shortForm ? 1 : 0;
    return builder_.makeNode(ast::SyntaxKind::ObjectProperty, source::SourceRange(), payload);
  }

  /// \brief Create a StructLiteralExpr.
  ast::NodeId makeStructLiteralExpr(ast::NodeId ty, ast::NodeList properties) {
    ast::NodePayload payload;
    payload.words[ast::kStructLiteralExprTyWord] = ty.value;
    payload.words[ast::kStructLiteralExprPropertiesFirstWord] = properties.first;
    payload.words[ast::kStructLiteralExprPropertiesSizeWord] = properties.size;
    return builder_.makeNode(ast::SyntaxKind::StructLiteralExpr, source::SourceRange(), payload);
  }

  /// \brief Create a UnaryExpression.
  ast::NodeId makeUnaryExpr(ast::UnaryOperatorKind op, ast::NodeId operand) {
    ast::NodePayload payload;
    payload.words[ast::kUnaryExpressionOpWord] = static_cast<uint32_t>(op);
    payload.words[ast::kUnaryExpressionOperandWord] = operand.value;
    return builder_.makeNode(ast::SyntaxKind::UnaryExpression, source::SourceRange(), payload);
  }

  /// \brief Create a PostfixExpression.
  ast::NodeId makePostfixExpr(ast::PostfixOperatorKind op, ast::NodeId operand) {
    ast::NodePayload payload;
    payload.words[ast::kPostfixExpressionOpWord] = static_cast<uint32_t>(op);
    payload.words[ast::kPostfixExpressionOperandWord] = operand.value;
    return builder_.makeNode(ast::SyntaxKind::PostfixExpression, source::SourceRange(), payload);
  }

  /// \brief Create a CastExpression.
  ast::NodeId makeCastExpr(ast::NodeId expr, ast::NodeId ty) {
    ast::NodePayload payload;
    payload.words[ast::kCastExpressionModeWord] = 0;
    payload.words[ast::kCastExpressionExprWord] = expr.value;
    payload.words[ast::kCastExpressionTyWord] = ty.value;
    return builder_.makeNode(ast::SyntaxKind::CastExpression, source::SourceRange(), payload);
  }

  /// \brief Create an IsExpression.
  ast::NodeId makeIsExpr(ast::NodeId expr, ast::NodeId ty) {
    ast::NodePayload payload;
    payload.words[ast::kIsExpressionExprWord] = expr.value;
    payload.words[ast::kIsExpressionTyWord] = ty.value;
    return builder_.makeNode(ast::SyntaxKind::IsExpression, source::SourceRange(), payload);
  }

  /// \brief Create a ThisExpr.
  ast::NodeId makeThisExpr() {
    return builder_.makeNode(ast::SyntaxKind::ThisExpr, source::SourceRange());
  }

  /// \brief Create a SuperExpr.
  ast::NodeId makeSuperExpr() {
    return builder_.makeNode(ast::SyntaxKind::SuperExpr, source::SourceRange());
  }

  /// \brief Create a NewExpression.
  ast::NodeId makeNewExpr(ast::NodeId callee, ast::NodeList args = ast::NodeList()) {
    ast::NodePayload payload;
    payload.words[ast::kNewExpressionCalleeWord] = callee.value;
    payload.words[ast::kNewExpressionTypeArgsFirstWord] = 0;
    payload.words[ast::kNewExpressionTypeArgsSizeWord] = 0;
    payload.words[ast::kNewExpressionArgsFirstWord] = args.first;
    payload.words[ast::kNewExpressionArgsSizeWord] = args.size;
    return builder_.makeNode(ast::SyntaxKind::NewExpression, source::SourceRange(), payload);
  }

  /// \brief Create an IndexExpression.
  ast::NodeId makeIndexExpr(ast::NodeId object, ast::NodeId index) {
    ast::NodePayload payload;
    payload.words[ast::kIndexExpressionObjectWord] = object.value;
    payload.words[ast::kIndexExpressionIndexWord] = index.value;
    return builder_.makeNode(ast::SyntaxKind::IndexExpression, source::SourceRange(), payload);
  }

  /// \brief Create a ForStmt.
  ast::NodeId makeForStmt(ast::NodeId init, ast::NodeId cond, ast::NodeId update,
                          ast::NodeId body) {
    ast::NodePayload payload;
    payload.words[ast::kForStmtInitWord] = init.value;
    payload.words[ast::kForStmtCondWord] = cond.value;
    payload.words[ast::kForStmtUpdateWord] = update.value;
    payload.words[ast::kForStmtBodyWord] = body.value;
    return builder_.makeNode(ast::SyntaxKind::ForStmt, source::SourceRange(), payload);
  }

  /// \brief Create a MarkerDeclaration.
  ast::NodeId makeMarkerDecl(zc::StringPtr name) {
    ast::NodePayload payload;
    auto nameId = builder_.internIdent(name);
    payload.words[ast::kMarkerDeclarationNameWord] = nameId.value;
    payload.words[ast::kMarkerDeclarationIsAutoWord] = 0;
    payload.words[ast::kMarkerDeclarationTypeParamsIdWord] = 0;
    payload.words[ast::kMarkerDeclarationNMarkersWord] = 0;
    payload.words[ast::kMarkerDeclarationMarkersFirstWord] = 0;
    payload.words[ast::kMarkerDeclarationMarkersSizeWord] = 0;
    return builder_.makeNode(ast::SyntaxKind::MarkerDeclaration, source::SourceRange(), payload);
  }

  /// \brief Create a StandaloneImplDecl.
  ast::NodeId makeStandaloneImplDecl(ast::NodeId forTy = ast::NodeId(),
                                     ast::NodeId ifaces = ast::NodeId()) {
    ast::NodePayload payload;
    payload.words[ast::kStandaloneImplDeclIsUnsafeWord] = 0;
    payload.words[ast::kStandaloneImplDeclIfacesIdWord] = ifaces.value;
    payload.words[ast::kStandaloneImplDeclForTyWord] = forTy.value;
    payload.words[ast::kStandaloneImplDeclWhereWord] = 0;
    payload.words[ast::kStandaloneImplDeclTypeParamsIdWord] = 0;
    payload.words[ast::kStandaloneImplDeclMembersIdWord] = 0;
    return builder_.makeNode(ast::SyntaxKind::StandaloneImplDecl, source::SourceRange(), payload);
  }

  /// \brief Create an ImplIfaceList.
  ast::NodeId makeImplIfaceList(ast::NodeList ifaces) {
    ast::NodePayload payload;
    payload.words[ast::kImplIfaceListNIfacesWord] = ifaces.size;
    payload.words[ast::kImplIfaceListIfacesFirstWord] = ifaces.first;
    payload.words[ast::kImplIfaceListIfacesSizeWord] = ifaces.size;
    return builder_.makeNode(ast::SyntaxKind::ImplIfaceList, source::SourceRange(), payload);
  }

  // ==========================================================================
  // Pattern factory methods
  // ==========================================================================

  /// \brief Create a MatchArmStmt (alias used by tests).
  ast::NodeId makeMatchArm(ast::NodeId pattern, ast::NodeId body) {
    return makeMatchArmStmt(pattern, body);
  }

  /// \brief Create a LiteralPattern wrapping a BoolLiteral.
  ast::NodeId makeBoolLiteralPattern(bool value) {
    auto lit = makeBoolLiteral(value);
    return makeLiteralPattern(lit);
  }

  /// \brief Create a LiteralPattern wrapping an IntLiteral.
  ast::NodeId makeIntLiteralPattern(int64_t value) {
    auto lit = makeIntLiteral(value);
    return makeLiteralPattern(lit);
  }

  /// \brief Create a LiteralPattern wrapping a StrLiteral.
  ast::NodeId makeStringLiteralPattern(zc::StringPtr value) {
    auto lit = makeStrLiteral(value);
    return makeLiteralPattern(lit);
  }

  /// \brief Create a pattern matching unit (via LiteralPattern wrapping UnitLiteral).
  ast::NodeId makeUnitPattern() {
    auto lit = makeUnitLiteral();
    return makeLiteralPattern(lit);
  }

  /// \brief Create a pattern matching null (via LiteralPattern wrapping NullLiteral).
  ast::NodeId makeNullPattern() {
    auto lit = makeNullLiteral();
    return makeLiteralPattern(lit);
  }

  /// \brief Create an IsPattern (type test pattern, e.g. `x is i32` in match arms).
  ast::NodeId makeTypedPattern(zc::StringPtr typeName) {
    auto tyExpr = makeNamedTypeExpr(typeName);
    ast::NodePayload payload;
    payload.words[ast::kIsPatternTyWord] = tyExpr.value;
    return builder_.makeNode(ast::SyntaxKind::IsPattern, source::SourceRange(), payload);
  }

  /// \brief Create an ExpressionPattern.
  ast::NodeId makeExpressionPattern(ast::NodeId expr) {
    ast::NodePayload payload;
    payload.words[ast::kExpressionPatternExprWord] = expr.value;
    return builder_.makeNode(ast::SyntaxKind::ExpressionPattern, source::SourceRange(), payload);
  }

  /// \brief Create a TuplePattern.
  ast::NodeId makeTuplePattern(ast::NodeList patterns) {
    ast::NodePayload payload;
    payload.words[ast::kTuplePatternPatsFirstWord] = patterns.first;
    payload.words[ast::kTuplePatternPatsSizeWord] = patterns.size;
    return builder_.makeNode(ast::SyntaxKind::TuplePattern, source::SourceRange(), payload);
  }

  /// \brief Create an EnumPattern (e.g. `Color::Red`).
  ast::NodeId makeEnumPattern(zc::StringPtr path, ast::NodeList args = ast::NodeList()) {
    ast::NodePayload payload;
    auto pathId = builder_.internIdent(path);
    payload.words[ast::kEnumPatternPathWord] = pathId.value;
    payload.words[ast::kEnumPatternArgsFirstWord] = args.first;
    payload.words[ast::kEnumPatternArgsSizeWord] = args.size;
    return builder_.makeNode(ast::SyntaxKind::EnumPattern, source::SourceRange(), payload);
  }

  // ==========================================================================
  // Type expression factory methods
  // ==========================================================================

  /// \brief Create a TypeAliasDecl (alias used by tests).
  ast::NodeId makeTypeAliasDecl(zc::StringPtr name, ast::NodeId target) {
    return makeAliasDecl(name, target);
  }

  /// \brief Create a FunctionTypeExpr (e.g. `fn(i32) -> bool`).
  ast::NodeId makeFunctionTypeExpr(ast::NodeList params, ast::NodeId retTy) {
    ast::NodePayload payload;
    payload.words[ast::kFunctionTypeExprParamsFirstWord] = params.first;
    payload.words[ast::kFunctionTypeExprParamsSizeWord] = params.size;
    payload.words[ast::kFunctionTypeExprRetTyWord] = retTy.value;
    payload.words[ast::kFunctionTypeExprRaisesWord] = 0;
    return builder_.makeNode(ast::SyntaxKind::FunctionTypeExpr, source::SourceRange(), payload);
  }

  /// \brief Create a TupleTypeExpr (e.g. `(i32, str)`).
  ast::NodeId makeTupleTypeExpr(ast::NodeList elems) {
    ast::NodePayload payload;
    payload.words[ast::kTupleTypeExprElemsFirstWord] = elems.first;
    payload.words[ast::kTupleTypeExprElemsSizeWord] = elems.size;
    return builder_.makeNode(ast::SyntaxKind::TupleTypeExpr, source::SourceRange(), payload);
  }

  /// \brief Create an ArrayTypeExpr (e.g. `[i32]`).
  ast::NodeId makeArrayTypeExpr(ast::NodeId elemTy) {
    ast::NodePayload payload;
    payload.words[ast::kArrayTypeExprElemWord] = elemTy.value;
    payload.words[ast::kArrayTypeExprLenExprWord] = 0;
    return builder_.makeNode(ast::SyntaxKind::ArrayTypeExpr, source::SourceRange(), payload);
  }

  /// \brief Create a SliceArrayTypeExpr (e.g. `[i32]`).
  ast::NodeId makeSliceArrayTypeExpr(ast::NodeId elemTy) {
    ast::NodePayload payload;
    payload.words[ast::kSliceArrayTypeExprElemWord] = elemTy.value;
    return builder_.makeNode(ast::SyntaxKind::SliceArrayTypeExpr, source::SourceRange(), payload);
  }

  /// \brief Create an OptionalTypeExpr (e.g. `i32?`).
  ast::NodeId makeOptionalTypeExpr(ast::NodeId innerTy) {
    ast::NodePayload payload;
    payload.words[ast::kOptionalTypeExprInnerWord] = innerTy.value;
    payload.words[ast::kOptionalTypeExprDoubleWord] = 0;
    return builder_.makeNode(ast::SyntaxKind::OptionalTypeExpr, source::SourceRange(), payload);
  }

  /// \brief Create a reference type expression.
  ast::NodeId makeReferenceTypeExpr(ast::NodeId innerTy, bool isMut = false) {
    auto operand = innerTy;
    if (isMut) {
      zc::Vector<ast::NodeId> args;
      args.add(innerTy);
      operand = makeNamedTypeExpr("mut"_zc, makeNodeList(args.asPtr()));
    }
    return makeUnaryExpr(ast::UnaryOperatorKind::Ref, operand);
  }

  /// \brief Create a raw pointer type expression.
  ast::NodeId makeRawPointerTypeExpr(ast::NodeId innerTy, bool isMut = false) {
    auto operand = innerTy;
    if (isMut) {
      zc::Vector<ast::NodeId> args;
      args.add(innerTy);
      operand = makeNamedTypeExpr("mut"_zc, makeNodeList(args.asPtr()));
    }
    return makeUnaryExpr(ast::UnaryOperatorKind::Deref, operand);
  }

  /// \brief Create an UnsafeBlockExpr.
  ast::NodeId makeUnsafeBlockExpr(ast::NodeId body) {
    ast::NodePayload payload;
    payload.words[ast::kUnsafeBlockExprBodyWord] = body.value;
    return builder_.makeNode(ast::SyntaxKind::UnsafeBlockExpr, source::SourceRange(), payload);
  }

  /// \brief Create a UnionTypeExpr (e.g. `i32 | str`).
  ast::NodeId makeUnionTypeExpr(ast::NodeId left, ast::NodeId right) {
    // Build a list of alternatives
    zc::Vector<ast::NodeId> alts;
    alts.add(left);
    alts.add(right);
    auto altList = builder_.makeList(alts.asPtr());
    ast::NodePayload payload;
    payload.words[ast::kUnionTypeExprAltsFirstWord] = altList.first;
    payload.words[ast::kUnionTypeExprAltsSizeWord] = altList.size;
    return builder_.makeNode(ast::SyntaxKind::UnionTypeExpr, source::SourceRange(), payload);
  }

  /// \brief Create an IntersectionTypeExpr (e.g. `Drawable & Serializable`).
  ast::NodeId makeIntersectionTypeExpr(ast::NodeId left, ast::NodeId right) {
    zc::Vector<ast::NodeId> alts;
    alts.add(left);
    alts.add(right);
    auto altList = builder_.makeList(alts.asPtr());
    ast::NodePayload payload;
    payload.words[ast::kIntersectionTypeExprAltsFirstWord] = altList.first;
    payload.words[ast::kIntersectionTypeExprAltsSizeWord] = altList.size;
    return builder_.makeNode(ast::SyntaxKind::IntersectionTypeExpr, source::SourceRange(), payload);
  }

  /// \brief Create a DynTypeExpr (e.g. `dyn Drawable`).
  ast::NodeId makeDynTypeExpr(ast::NodeId ifaceTy) {
    zc::Vector<ast::NodeId> ifaces;
    ifaces.add(ifaceTy);
    auto ifaceList = builder_.makeList(ifaces.asPtr());
    ast::NodePayload ifaceListPayload;
    ifaceListPayload.words[ast::kDynTypeIfaceListNIfacesWord] = ifaceList.size;
    ifaceListPayload.words[ast::kDynTypeIfaceListIfacesFirstWord] = ifaceList.first;
    ifaceListPayload.words[ast::kDynTypeIfaceListIfacesSizeWord] = ifaceList.size;
    auto ifaceListNode = builder_.makeNode(ast::SyntaxKind::DynTypeIfaceList, source::SourceRange(),
                                           ifaceListPayload);

    ast::NodePayload payload;
    payload.words[ast::kDynTypeExprIfacesIdWord] = ifaceListNode.value;
    payload.words[ast::kDynTypeExprMarkersIdWord] = 0;
    payload.words[ast::kDynTypeExprHasLifetimeWord] = 0;
    payload.words[ast::kDynTypeExprLifetimeWord] = 0;
    return builder_.makeNode(ast::SyntaxKind::DynTypeExpr, source::SourceRange(), payload);
  }

  /// \brief Create an AssociatedTypeProjectionExpr.
  ast::NodeId makeAssociatedTypeProjectionExpr(ast::NodeId baseTy, ast::NodeId ifaceTy,
                                               zc::StringPtr name) {
    ast::NodePayload payload;
    payload.words[ast::kAssociatedTypeProjectionExprBaseTyWord] = baseTy.value;
    payload.words[ast::kAssociatedTypeProjectionExprIfaceTyWord] = ifaceTy.value;
    payload.words[ast::kAssociatedTypeProjectionExprNameWord] = builder_.internIdent(name).value;
    return builder_.makeNode(ast::SyntaxKind::AssociatedTypeProjectionExpr, source::SourceRange(),
                             payload);
  }

  /// \brief Create a PredefinedTypeExpr (e.g. `i32`, `bool`).
  ast::NodeId makePredefinedTypeExpr(uint8_t kind) {
    ast::NodePayload payload;
    payload.words[ast::kPredefinedTypeExprKindWord] = kind;
    return builder_.makeNode(ast::SyntaxKind::PredefinedTypeExpr, source::SourceRange(), payload);
  }

  /// \brief Create an ObjectTypeExpr (e.g. `{ x: i32, y: i32 }`).
  ast::NodeId makeObjectTypeExpr(ast::NodeList members) {
    ast::NodePayload payload;
    payload.words[ast::kObjectTypeExprMembersFirstWord] = members.first;
    payload.words[ast::kObjectTypeExprMembersSizeWord] = members.size;
    return builder_.makeNode(ast::SyntaxKind::ObjectTypeExpr, source::SourceRange(), payload);
  }

  /// \brief Create an ObjectTypeMember.
  ast::NodeId makeObjectTypeMember(zc::StringPtr name, ast::NodeId ty, bool isMut = false,
                                   bool isOptional = false) {
    ast::NodePayload payload;
    payload.words[ast::kObjectTypeMemberNameWord] = builder_.internIdent(name).value;
    payload.words[ast::kObjectTypeMemberTyWord] = ty.value;
    payload.words[ast::kObjectTypeMemberIsMutWord] = isMut ? 1 : 0;
    payload.words[ast::kObjectTypeMemberIsOptionalWord] = isOptional ? 1 : 0;
    return builder_.makeNode(ast::SyntaxKind::ObjectTypeMember, source::SourceRange(), payload);
  }

  /// \brief Create a BottomTypeExpr (e.g. `never`).
  ast::NodeId makeBottomTypeExpr() {
    return builder_.makeNode(ast::SyntaxKind::BottomTypeExpr, source::SourceRange());
  }

  // ==========================================================================
  // Enum variant factory methods
  // ==========================================================================

  /// \brief Create a UnitVariant (simple enum variant with no data).
  ast::NodeId makeEnumVariant(zc::StringPtr name, int64_t disc = -1) {
    ast::NodePayload payload;
    auto nameId = builder_.internIdent(name);
    payload.words[ast::kUnitVariantNameWord] = nameId.value;
    payload.words[ast::kUnitVariantHasDiscWord] = (disc >= 0) ? 1 : 0;
    payload.words[ast::kUnitVariantDiscWord] = static_cast<uint32_t>(disc);
    return builder_.makeNode(ast::SyntaxKind::UnitVariant, source::SourceRange(), payload);
  }

  /// \brief Create a TupleVariant (enum variant with tuple fields).
  ast::NodeId makeTupleVariant(zc::StringPtr name, ast::NodeList tys) {
    ast::NodePayload payload;
    auto nameId = builder_.internIdent(name);
    payload.words[ast::kTupleVariantNameWord] = nameId.value;
    payload.words[ast::kTupleVariantNfieldsWord] = tys.size;
    payload.words[ast::kTupleVariantTysFirstWord] = tys.first;
    payload.words[ast::kTupleVariantTysSizeWord] = tys.size;
    payload.words[ast::kTupleVariantHasDiscWord] = 0;
    payload.words[ast::kTupleVariantDiscWord] = 0;
    return builder_.makeNode(ast::SyntaxKind::TupleVariant, source::SourceRange(), payload);
  }

  /// \brief Create an EnumVariantList wrapping a list of variants.
  ast::NodeId makeEnumVariantList(ast::NodeList variants) {
    ast::NodePayload payload;
    payload.words[ast::kEnumVariantListNvarsWord] = variants.size;
    payload.words[ast::kEnumVariantListVariantsFirstWord] = variants.first;
    payload.words[ast::kEnumVariantListVariantsSizeWord] = variants.size;
    return builder_.makeNode(ast::SyntaxKind::EnumVariantList, source::SourceRange(), payload);
  }

  /// \brief Helper: build a complete source file from a list of top-level declarations.
  ///
  /// NOTE: We intentionally do NOT create a ModuleDeclaration node here.
  /// Passing an invalid NodeId for the module avoids potential traversal cycles
  /// that can cause stack overflow in DeclCollector. The Binder handles this
  /// gracefully — it checks `tree.contains(moduleNode)` before visiting.
  ast::Tree buildSourceFile(zc::StringPtr moduleName, zc::ArrayPtr<const ast::NodeId> decls) {
    zc::Vector<ast::NodeId> items;
    for (size_t i = 0; i < decls.size(); i++) { items.add(makeStatementListItem(decls[i])); }
    ast::NodeList stmtList = builder_.makeList(items.asPtr());
    // Pass invalid NodeId for module — matches the working binder-test.cc pattern
    makeSourceFile(ast::NodeId(), stmtList);
    return finishTree();
  }

  // ==========================================================================
  // List helpers
  // ==========================================================================

  ast::NodeList makeNodeList(zc::ArrayPtr<const ast::NodeId> nodes) {
    return builder_.makeList(nodes);
  }

private:
  zc::Own<source::SourceManager> sourceManager_;
  zc::Own<diagnostics::DiagnosticEngine> diagnostics_;
  symbol::SymbolTable symbols_;
  ast::BindingMetadata metadata_;
  ast::TreeBuilder builder_;
};

}  // namespace tests
}  // namespace compiler
}  // namespace zomlang
