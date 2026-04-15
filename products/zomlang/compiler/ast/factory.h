// Copyright (c) 2024-2025 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations under
// the License.

#pragma once

#include <cstdint>

#include "zc/core/common.h"
#include "zc/core/one-of.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/ast/ast.h"
#include "zomlang/compiler/ast/kinds.h"
#include "zomlang/compiler/ast/statement.h"

namespace zomlang {
namespace compiler {

namespace source {
class SourceRange;
}  // namespace source

namespace ast {
/// Forward declaration
class SourceFile;
class Statement;
class Expression;
class ModuleDeclaration;
class ModulePath;
class ImportSpecifier;
class ImportDeclaration;
class ExportSpecifier;
class ExportDeclaration;
class BindingElement;
class VariableDeclarationList;
class FunctionDeclaration;
class MethodDeclaration;
class ClassDeclaration;
class InterfaceDeclaration;
class BlockStatement;
class ExpressionStatement;
class IfStatement;
class WhileStatement;
class ReturnStatement;
class BreakStatement;
class ContinueStatement;
class EmptyStatement;
class AliasDeclaration;
class MatchStatement;
class ErrorDeclaration;
class StructDeclaration;
class EnumDeclaration;
class ForStatement;
class TypeParameterDeclaration;
class ClassElement;
class InterfaceElement;

// Expression
class BinaryExpression;
class PrefixUnaryExpression;
class PostfixUnaryExpression;
class ConditionalExpression;
class CallExpression;
class MemberExpression;
class UpdateExpression;
class CastExpression;
class AsExpression;
class ForcedAsExpression;
class ConditionalAsExpression;
class VoidExpression;
class TypeOfExpression;
class AwaitExpression;
class NonNullExpression;
class FunctionExpression;
class NewExpression;
class ParenthesizedExpression;
class Identifier;
class StringLiteral;
class IntegerLiteral;
class FloatLiteral;
class BooleanLiteral;
class NullLiteral;
class DebuggerStatement;
class PropertyAccessExpression;
class ElementAccessExpression;
class LeftHandSideExpression;
class ArrayLiteralExpression;
class ObjectLiteralExpression;
class ExpressionWithTypeArguments;

class NamedTupleElement;
class EnumMember;
class GetAccessor;
class SetAccessor;

// Types
class TypeNode;
class ArrayTypeNode;
class ObjectTypeNode;
class TupleTypeNode;
class UnionTypeNode;
class IntersectionTypeNode;
class ParenthesizedTypeNode;
class TypeReferenceNode;
class PredefinedTypeNode;
class FunctionTypeNode;
class ReturnTypeNode;
class OptionalTypeNode;
class TypeQuery;

// Pattern classes
class Pattern;
class PrimaryPattern;
class WildcardPattern;
class IdentifierPattern;
class TuplePattern;
class StructurePattern;
class ArrayPattern;
class IsPattern;
class ExpressionPattern;
class EnumPattern;

// Match clause classes
class MatchClause;
class DefaultClause;
class BindingPattern;
class ArrayBindingPattern;
class ObjectBindingPattern;

namespace factory {
/// Create a new SourceFile Node.
zc::Own<SourceFile> createSourceFile(zc::String&& fileName,
                                     zc::Maybe<zc::Own<ModuleDeclaration>> moduleDeclaration,
                                     zc::Vector<zc::Own<ast::Statement>>&& statements);

template <typename Node>
  requires std::is_base_of_v<ast::Node, Node>
const ast::NodeList<Node> createNodeList(zc::Vector<zc::Own<Node>>&& list) {
  return ast::NodeList<Node>(zc::mv(list));
}

template <typename Node, typename... Args>
  requires std::is_base_of_v<ast::Node, Node>
zc::Own<Node> createNodeWithRange(const source::SourceRange& range, Args&&... args) {
  zc::Own<Node> node = zc::heap<Node>(zc::fwd<Args>(args)...);
  node->setSourceRange(range);
  return node;  // NRVO optimization
}

/// Create a ModulePath node
zc::Own<ModulePath> createModulePath(zc::Vector<zc::Own<ast::Identifier>>&& segments);

zc::Own<ModuleDeclaration> createModuleDeclaration(zc::Own<ModulePath>&& modulePath);

zc::Own<ImportSpecifier> createImportSpecifier(
    zc::Own<ast::Identifier>&& importedName, zc::Maybe<zc::Own<ast::Identifier>> alias = zc::none);

/// Create an ImportDeclaration node
zc::Own<ImportDeclaration> createImportDeclaration(
    zc::Own<ModulePath> modulePath, zc::Maybe<zc::Own<ast::Identifier>> alias,
    zc::Vector<zc::Own<ImportSpecifier>>&& specifiers);

zc::Own<ExportSpecifier> createExportSpecifier(
    zc::Own<ast::Identifier>&& exportedName, zc::Maybe<zc::Own<ast::Identifier>> alias = zc::none);

/// Create an ExportDeclaration node
zc::Own<ExportDeclaration> createExportDeclaration(
    zc::Maybe<zc::Own<ModulePath>> modulePath, zc::Vector<zc::Own<ExportSpecifier>>&& specifiers,
    zc::Maybe<zc::Own<ast::Statement>> declaration = zc::none);

/// Statement factory functions
zc::Own<BindingElement> createBindingElement(
    zc::Maybe<zc::Own<TokenNode>> dotDotDotToken, zc::Maybe<zc::Own<Identifier>> propertyName,
    zc::OneOf<zc::Own<Identifier>, zc::Own<BindingPattern>> nameOrPattern,
    zc::Maybe<zc::Own<Expression>> initialize);

zc::Own<ParameterDeclaration> createParameterDeclaration(
    zc::Vector<ast::SyntaxKind> modifiers, zc::Maybe<zc::Own<TokenNode>> dotDotDotToken,
    zc::OneOf<zc::Own<Identifier>, zc::Own<BindingPattern>> name,
    zc::Maybe<zc::Own<TokenNode>> questionToken, zc::Maybe<zc::Own<TypeNode>> type = zc::none,
    zc::Maybe<zc::Own<Expression>> init = zc::none);

zc::Own<VariableDeclaration> createVariableDeclaration(
    zc::OneOf<zc::Own<Identifier>, zc::Own<BindingPattern>> name,
    zc::Maybe<zc::Own<TypeNode>> type = zc::none, zc::Maybe<zc::Own<Expression>> init = zc::none);

zc::Own<VariableDeclarationList> createVariableDeclarationList(
    zc::Vector<zc::Own<VariableDeclaration>>&& bindings);

zc::Own<VariableStatement> createVariableStatement(zc::Own<VariableDeclarationList> declarations);

zc::Own<FunctionDeclaration> createFunctionDeclaration(
    zc::Own<Identifier> name,
    zc::Maybe<zc::Vector<zc::Own<TypeParameterDeclaration>>> typeParameters,
    zc::Vector<zc::Own<ParameterDeclaration>>&& parameters,
    zc::Maybe<zc::Own<ReturnTypeNode>> returnType, zc::Own<Statement> body);

zc::Own<MethodDeclaration> createMethodDeclaration(
    zc::Vector<ast::SyntaxKind> modifiers, zc::Own<Identifier> name,
    zc::Maybe<zc::Own<ast::TokenNode>> optional,
    zc::Maybe<zc::Vector<zc::Own<TypeParameterDeclaration>>> typeParameters,
    zc::Vector<zc::Own<ParameterDeclaration>>&& parameters,
    zc::Maybe<zc::Own<ReturnTypeNode>> returnType, zc::Maybe<zc::Own<Statement>> body);

zc::Own<GetAccessor> createGetAccessorDeclaration(
    zc::Vector<ast::SyntaxKind> modifiers, zc::Own<Identifier> name,
    zc::Maybe<zc::Vector<zc::Own<TypeParameterDeclaration>>> typeParameters,
    zc::Vector<zc::Own<ParameterDeclaration>>&& parameters,
    zc::Maybe<zc::Own<ReturnTypeNode>> returnType, zc::Maybe<zc::Own<Statement>> body);

zc::Own<SetAccessor> createSetAccessorDeclaration(
    zc::Vector<ast::SyntaxKind> modifiers, zc::Own<Identifier> name,
    zc::Maybe<zc::Vector<zc::Own<TypeParameterDeclaration>>> typeParameters,
    zc::Vector<zc::Own<ParameterDeclaration>>&& parameters,
    zc::Maybe<zc::Own<ReturnTypeNode>> returnType, zc::Maybe<zc::Own<Statement>> body);

zc::Own<InitDeclaration> createInitDeclaration(
    zc::Vector<ast::SyntaxKind> modifiers,
    zc::Maybe<zc::Vector<zc::Own<TypeParameterDeclaration>>> typeParameters,
    zc::Vector<zc::Own<ParameterDeclaration>>&& parameters,
    zc::Maybe<zc::Own<ReturnTypeNode>> returnType, zc::Maybe<zc::Own<Statement>> body);

zc::Own<DeinitDeclaration> createDeinitDeclaration(zc::Vector<ast::SyntaxKind> modifiers,
                                                   zc::Maybe<zc::Own<Statement>> body);

zc::Own<MethodSignature> createMethodSignature(
    zc::Vector<ast::SyntaxKind> modifiers, zc::Own<Identifier> name,
    zc::Maybe<zc::Own<ast::TokenNode>> optional,
    zc::Maybe<zc::Vector<zc::Own<TypeParameterDeclaration>>> typeParameters,
    zc::Vector<zc::Own<ParameterDeclaration>>&& parameters,
    zc::Maybe<zc::Own<ReturnTypeNode>> returnType = zc::none);

zc::Own<PropertySignature> createPropertySignature(
    zc::Vector<ast::SyntaxKind> modifiers, zc::Own<Identifier> name,
    zc::Maybe<zc::Own<ast::TokenNode>> optional, zc::Maybe<zc::Own<TypeNode>> type,
    zc::Maybe<zc::Own<Expression>> initializer = zc::none);

zc::Own<SemicolonClassElement> createSemicolonClassElement();
zc::Own<SemicolonInterfaceElement> createSemicolonInterfaceElement();

zc::Own<PropertyDeclaration> createPropertyDeclaration(zc::Vector<ast::SyntaxKind> modifiers,
                                                       zc::Own<Identifier> name,
                                                       zc::Maybe<zc::Own<TypeNode>> type,
                                                       zc::Maybe<zc::Own<Expression>> initializer);

zc::Own<ClassDeclaration> createClassDeclaration(
    zc::Own<Identifier> name,
    zc::Maybe<zc::Vector<zc::Own<TypeParameterDeclaration>>> typeParameters,
    zc::Maybe<zc::Vector<zc::Own<HeritageClause>>> heritageClauses,
    zc::Vector<zc::Own<ClassElement>>&& members);

zc::Own<InterfaceDeclaration> createInterfaceDeclaration(
    zc::Own<Identifier> name,
    zc::Maybe<zc::Vector<zc::Own<TypeParameterDeclaration>>> typeParameters,
    zc::Maybe<zc::Vector<zc::Own<HeritageClause>>> heritageClauses,
    zc::Vector<zc::Own<InterfaceElement>>&& members);

zc::Own<StructDeclaration> createStructDeclaration(
    zc::Own<Identifier> name,
    zc::Maybe<zc::Vector<zc::Own<TypeParameterDeclaration>>> typeParameters,
    zc::Maybe<zc::Vector<zc::Own<HeritageClause>>> heritageClauses,
    zc::Vector<zc::Own<ClassElement>>&& body);

zc::Own<EnumMember> createEnumMember(zc::Own<Identifier> name,
                                     zc::Maybe<zc::Own<Expression>> initializer = zc::none,
                                     zc::Maybe<zc::Own<TupleTypeNode>> tupleType = zc::none);

zc::Own<EnumDeclaration> createEnumDeclaration(zc::Own<Identifier> name,
                                               zc::Vector<zc::Own<EnumMember>>&& body);

zc::Own<ErrorDeclaration> createErrorDeclaration(zc::Own<Identifier> name,
                                                 zc::Vector<zc::Own<Statement>>&& body);

zc::Own<BlockStatement> createBlockStatement(zc::Vector<zc::Own<Statement>>&& statements);

zc::Own<ExpressionStatement> createExpressionStatement(zc::Own<Expression> expression);

zc::Own<IfStatement> createIfStatement(zc::Own<Expression> test, zc::Own<Statement> consequent,
                                       zc::Maybe<zc::Own<Statement>> alternate = zc::none);

zc::Own<WhileStatement> createWhileStatement(zc::Own<Expression> test, zc::Own<Statement> body);

zc::Own<ReturnStatement> createReturnStatement(zc::Maybe<zc::Own<Expression>> argument = zc::none);

zc::Own<BreakStatement> createBreakStatement(zc::Maybe<zc::Own<Identifier>> label = zc::none);

zc::Own<ContinueStatement> createContinueStatement(zc::Maybe<zc::Own<Identifier>> label = zc::none);

zc::Own<EmptyStatement> createEmptyStatement();

zc::Own<MatchStatement> createMatchStatement(zc::Own<Expression> discriminant,
                                             zc::Vector<zc::Own<Statement>>&& clauses);

zc::Own<MatchClause> createMatchClause(zc::Own<Pattern> pattern,
                                       zc::Maybe<zc::Own<Expression>> guard,
                                       zc::Own<Statement> body);

zc::Own<DefaultClause> createDefaultClause(zc::Vector<zc::Own<Statement>>&& statements);

zc::Own<ArrayBindingPattern> createArrayBindingPattern(
    zc::Vector<zc::Own<BindingElement>>&& elements);

zc::Own<ObjectBindingPattern> createObjectBindingPattern(
    zc::Vector<zc::Own<BindingElement>>&& elements);

zc::Own<ForStatement> createForStatement(zc::Maybe<zc::Own<Statement>> init,
                                         zc::Maybe<zc::Own<Expression>> condition,
                                         zc::Maybe<zc::Own<Expression>> update,
                                         zc::Own<Statement> body);

zc::Own<ForInStatement> createForInStatement(zc::Own<Statement> initializer,
                                             zc::Own<Expression> expression,
                                             zc::Own<Statement> body);

zc::Own<LabeledStatement> createLabeledStatement(zc::Own<Identifier> label,
                                                 zc::Own<Statement> statement);

/// Expression factory functions
zc::Own<BinaryExpression> createBinaryExpression(zc::Own<Expression> left, zc::Own<TokenNode> op,
                                                 zc::Own<Expression> right);

zc::Own<PrefixUnaryExpression> createPrefixUnaryExpression(SyntaxKind op,
                                                           zc::Own<Expression> operand);

zc::Own<PostfixUnaryExpression> createPostfixUnaryExpression(SyntaxKind op,
                                                             zc::Own<Expression> operand);

zc::Own<ConditionalExpression> createConditionalExpression(
    zc::Own<Expression> test, zc::Maybe<zc::Own<TokenNode>> questionToken,
    zc::Own<Expression> consequent, zc::Maybe<zc::Own<TokenNode>> colonToken,
    zc::Own<Expression> alternate);

zc::Own<CallExpression> createCallExpression(
    zc::Own<Expression> callee, zc::Maybe<zc::Own<TokenNode>> questionDotToken,
    zc::Maybe<zc::Vector<zc::Own<ast::TypeNode>>> typeArguments,
    zc::Vector<zc::Own<Expression>>&& arguments, bool isOptionalChain = false);

zc::Own<PropertyAccessExpression> createPropertyAccessExpression(
    zc::Own<LeftHandSideExpression> expression, zc::Own<Identifier> name, bool questionDot = false,
    bool isOptionalChain = false);

zc::Own<ElementAccessExpression> createElementAccessExpression(
    zc::Own<LeftHandSideExpression> expression, zc::Own<Expression> index, bool questionDot = false,
    bool isOptionalChain = false);

zc::Own<AsExpression> createAsExpression(zc::Own<Expression> expression,
                                         zc::Own<TypeNode> targetType);

zc::Own<ForcedAsExpression> createForcedAsExpression(zc::Own<Expression> expression,
                                                     zc::Own<TypeNode> targetType);

zc::Own<ConditionalAsExpression> createConditionalAsExpression(zc::Own<Expression> expression,
                                                               zc::Own<TypeNode> targetType);

zc::Own<VoidExpression> createVoidExpression(zc::Own<Expression> expression);

zc::Own<TypeOfExpression> createTypeOfExpression(zc::Own<Expression> expression);

zc::Own<AwaitExpression> createAwaitExpression(zc::Own<Expression> expression);

zc::Own<NonNullExpression> createNonNullExpression(zc::Own<Expression> expression);

zc::Own<ExpressionWithTypeArguments> createExpressionWithTypeArguments(
    zc::Own<LeftHandSideExpression> expression,
    zc::Maybe<zc::Vector<zc::Own<TypeNode>>> typeArguments);

zc::Own<CaptureElement> createCaptureElement(bool isByReference,
                                             zc::Maybe<zc::Own<Identifier>> identifier,
                                             bool isThis);

zc::Own<FunctionExpression> createFunctionExpression(
    zc::Maybe<zc::Vector<zc::Own<TypeParameterDeclaration>>> typeParameters,
    zc::Vector<zc::Own<ParameterDeclaration>>&& parameters,
    zc::Vector<zc::Own<CaptureElement>>&& captures, zc::Maybe<zc::Own<TypeNode>> returnType,
    zc::Own<Statement> body);

zc::Own<NewExpression> createNewExpression(zc::Own<Expression> callee,
                                           zc::Maybe<zc::Vector<zc::Own<TypeNode>>> typeArguments,
                                           zc::Maybe<zc::Vector<zc::Own<Expression>>> arguments);

zc::Own<ParenthesizedExpression> createParenthesizedExpression(zc::Own<Expression> expression);

zc::Own<ArrayLiteralExpression> createArrayLiteralExpression(
    zc::Vector<zc::Own<Expression>>&& elements, bool multiLine = false);

zc::Own<ObjectLiteralExpression> createObjectLiteralExpression(
    zc::Vector<zc::Own<ObjectLiteralElement>>&& properties, bool multiLine = false);

zc::Own<PropertyAssignment> createPropertyAssignment(
    zc::Own<Identifier> name, zc::Maybe<zc::Own<Expression>> initializer,
    zc::Maybe<zc::Own<TokenNode>> questionToken = zc::none);

zc::Own<ShorthandPropertyAssignment> createShorthandPropertyAssignment(
    zc::Own<Identifier> name, zc::Maybe<zc::Own<Expression>> objectAssignmentInitializer = zc::none,
    zc::Maybe<zc::Own<TokenNode>> equalsToken = zc::none);

zc::Own<SpreadAssignment> createSpreadAssignment(zc::Own<Expression> expression);

zc::Own<SpreadElement> createSpreadElement(zc::Own<Expression> expression);

zc::Own<Identifier> createIdentifier(zc::StringPtr name);

zc::Own<Identifier> createMissingIdentifier();

zc::Own<StringLiteral> createStringLiteral(zc::StringPtr value);

zc::Own<IntegerLiteral> createIntegerLiteral(int64_t value);

zc::Own<FloatLiteral> createFloatLiteral(double value);

zc::Own<BooleanLiteral> createBooleanLiteral(bool value);

zc::Own<NullLiteral> createNullLiteral();

zc::Own<ThisExpression> createThisExpression();

zc::Own<TemplateSpan> createTemplateSpan(zc::Own<Expression> expression,
                                         zc::Own<StringLiteral> literal);

zc::Own<TemplateLiteralExpression> createTemplateLiteralExpression(
    zc::Own<StringLiteral> head, zc::Vector<zc::Own<TemplateSpan>>&& spans);

zc::Own<TypeQueryNode> createTypeQueryNode(zc::Own<Expression> expression);

zc::Own<NamedTupleElement> createNamedTupleElement(zc::Own<Identifier> name,
                                                   zc::Own<TypeNode> type);

/// Pattern factory functions
// zc::Own<PrimaryPattern> createPrimaryPattern(zc::Own<Expression> expression);

zc::Own<WildcardPattern> createWildcardPattern();

zc::Own<IdentifierPattern> createIdentifierPattern(zc::Own<Identifier> identifier);

zc::Own<TuplePattern> createTuplePattern(zc::Vector<zc::Own<Pattern>>&& elements);

zc::Own<StructurePattern> createStructurePattern(zc::Vector<zc::Own<Pattern>>&& fields);

zc::Own<PatternProperty> createPatternProperty(zc::Own<Identifier> name,
                                               zc::Maybe<zc::Own<Pattern>> pattern = zc::none);

zc::Own<ArrayPattern> createArrayPattern(zc::Vector<zc::Own<Pattern>>&& elements);

zc::Own<IsPattern> createIsPattern(zc::Own<TypeNode> type);

zc::Own<ExpressionPattern> createExpressionPattern(zc::Own<Expression> expression);

zc::Own<EnumPattern> createEnumPattern(zc::Own<TypeReferenceNode> typeReference,
                                       zc::Own<Identifier> propertyName,
                                       zc::Maybe<zc::Own<TuplePattern>> tuplePattern = zc::none);

zc::Own<AliasDeclaration> createAliasDeclaration(
    zc::Own<Identifier> name,
    zc::Maybe<zc::Vector<zc::Own<TypeParameterDeclaration>>> typeParameters,
    zc::Own<TypeNode> type);

zc::Own<DebuggerStatement> createDebuggerStatement();

/// Type
zc::Own<ArrayTypeNode> createArrayType(zc::Own<TypeNode> elementType);

zc::Own<ObjectTypeNode> createObjectType(zc::Vector<zc::Own<Node>>&& members);

zc::Own<TupleTypeNode> createTupleType(zc::Vector<zc::Own<TypeNode>>&& elementTypes);

zc::Own<UnionTypeNode> createUnionType(zc::Vector<zc::Own<TypeNode>>&& types);

zc::Own<IntersectionTypeNode> createIntersectionType(zc::Vector<zc::Own<TypeNode>>&& types);

zc::Own<ParenthesizedTypeNode> createParenthesizedType(zc::Own<TypeNode> type);

zc::Own<TypeReferenceNode> createTypeReference(
    zc::Own<Identifier> typeName, zc::Maybe<zc::Vector<zc::Own<TypeNode>>> typeArguments);

zc::Own<PredefinedTypeNode> createPredefinedType(zc::StringPtr name);

zc::Own<ReturnTypeNode> createReturnType(zc::Own<TypeNode> type,
                                         zc::Maybe<zc::Own<TypeNode>> errorType);

zc::Own<FunctionTypeNode> createFunctionType(
    zc::Maybe<zc::Vector<zc::Own<TypeParameterDeclaration>>> typeParameters,
    zc::Vector<zc::Own<ParameterDeclaration>>&& parameters, zc::Own<ReturnTypeNode> returnType);

zc::Own<OptionalTypeNode> createOptionalType(zc::Own<TypeNode> type);

/// Type query factory function
zc::Own<TypeQueryNode> createTypeQuery(zc::Own<Expression> expr);

zc::Own<TypeParameterDeclaration> createTypeParameterDeclaration(
    zc::Own<Identifier> name, zc::Maybe<zc::Own<TypeNode>> constraint = zc::none);

/// \brief Create a token node with the given kind.
zc::Own<TokenNode> createTokenNode(SyntaxKind kind);

zc::Own<HeritageClause> createHeritageClause(
    SyntaxKind token, zc::Vector<zc::Own<ExpressionWithTypeArguments>>&& types);

template <typename Node>
  requires std::is_base_of_v<ast::Node, Node>
const zc::Vector<zc::Own<Node>> createMissingList() {
  return {};
}

}  // namespace factory
}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
