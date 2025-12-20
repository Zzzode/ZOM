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

#include "zomlang/compiler/ast/factory.h"

#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/ast/ast.h"
#include "zomlang/compiler/ast/expression.h"
#include "zomlang/compiler/ast/kinds.h"
#include "zomlang/compiler/ast/module.h"
#include "zomlang/compiler/ast/statement.h"
#include "zomlang/compiler/ast/type.h"

namespace zomlang {
namespace compiler {
namespace ast {
namespace factory {

zc::Own<SourceFile> createSourceFile(zc::String&& fileName,
                                     zc::Vector<zc::Own<ast::Statement>>&& statements) {
  return zc::heap<SourceFile>(zc::mv(fileName), zc::mv(statements));
}

zc::Own<ModulePath> createModulePath(zc::Own<ast::StringLiteral>&& stringLiteral) {
  return zc::heap<ModulePath>(zc::mv(stringLiteral));
}

zc::Own<ImportDeclaration> createImportDeclaration(zc::Own<ModulePath> modulePath,
                                                   zc::Maybe<zc::Own<ast::Identifier>> alias) {
  return zc::heap<ImportDeclaration>(zc::mv(modulePath), zc::mv(alias));
}

zc::Own<ExportDeclaration> createExportDeclaration(zc::Own<ast::Expression>&& exportPath,
                                                   zc::Maybe<zc::Own<ast::Identifier>> alias) {
  return zc::heap<ExportDeclaration>(zc::mv(exportPath), zc::mv(alias));
}

// Statement factory functions
zc::Own<BindingElement> createBindingElement(zc::Own<Identifier> name,
                                             zc::Maybe<zc::Own<TypeNode>> type,
                                             zc::Maybe<zc::Own<Expression>> initializer) {
  return zc::heap<BindingElement>(zc::mv(name), zc::mv(type), zc::mv(initializer));
}

zc::Own<VariableDeclarationList> createVariableDeclarationList(
    zc::Vector<zc::Own<BindingElement>>&& bindings) {
  return zc::heap<VariableDeclarationList>(zc::mv(bindings));
}

zc::Own<VariableStatement> createVariableStatement(zc::Own<VariableDeclarationList> declarations) {
  return zc::heap<VariableStatement>(zc::mv(declarations));
}

zc::Own<FunctionDeclaration> createFunctionDeclaration(
    zc::Own<Identifier> name, zc::Vector<zc::Own<TypeParameterDeclaration>>&& typeParameters,
    zc::Vector<zc::Own<BindingElement>>&& parameters, zc::Maybe<zc::Own<ReturnTypeNode>> returnType,
    zc::Own<Statement> body) {
  return zc::heap<FunctionDeclaration>(zc::mv(name), zc::mv(typeParameters), zc::mv(parameters),
                                       zc::mv(returnType), zc::mv(body));
}

zc::Own<MethodSignature> createMethodSignature(
    zc::Own<Identifier> name, bool optional,
    zc::Vector<zc::Own<TypeParameterDeclaration>>&& typeParameters,
    zc::Vector<zc::Own<BindingElement>>&& parameters,
    zc::Maybe<zc::Own<ReturnTypeNode>> returnType) {
  return zc::heap<MethodSignature>(zc::mv(name), optional, zc::mv(typeParameters),
                                   zc::mv(parameters), zc::mv(returnType));
}

zc::Own<PropertySignature> createPropertySignature(zc::Own<Identifier> name, bool optional,
                                                   zc::Maybe<zc::Own<TypeNode>> type,
                                                   zc::Maybe<zc::Own<Expression>> initializer) {
  return zc::heap<PropertySignature>(zc::mv(name), optional, zc::mv(type), zc::mv(initializer));
}

zc::Own<SemicolonClassElement> createSemicolonClassElement() {
  return zc::heap<SemicolonClassElement>();
}

zc::Own<ClassDeclaration> createClassDeclaration(
    zc::Own<Identifier> name, zc::Vector<zc::Own<TypeParameterDeclaration>>&& typeParameters,
    zc::Maybe<zc::Vector<zc::Own<HeritageClause>>> heritageClauses,
    zc::Vector<zc::Own<ClassElement>>&& members) {
  return zc::heap<ClassDeclaration>(zc::mv(name), zc::mv(typeParameters), zc::mv(heritageClauses),
                                    zc::mv(members));
}

zc::Own<InterfaceDeclaration> createInterfaceDeclaration(
    zc::Own<Identifier> name, zc::Vector<zc::Own<TypeParameterDeclaration>>&& typeParameters,
    zc::Maybe<zc::Vector<zc::Own<HeritageClause>>> heritageClauses,
    zc::Vector<zc::Own<InterfaceElement>>&& members) {
  return zc::heap<InterfaceDeclaration>(zc::mv(name), zc::mv(typeParameters),
                                        zc::mv(heritageClauses), zc::mv(members));
}

zc::Own<StructDeclaration> createStructDeclaration(zc::Own<Identifier> name,
                                                   zc::Vector<zc::Own<Statement>>&& body) {
  return zc::heap<StructDeclaration>(zc::mv(name), zc::mv(body));
}

zc::Own<EnumDeclaration> createEnumDeclaration(zc::Own<Identifier> name,
                                               zc::Vector<zc::Own<Statement>>&& body) {
  return zc::heap<EnumDeclaration>(zc::mv(name), zc::mv(body));
}

zc::Own<ErrorDeclaration> createErrorDeclaration(zc::Own<Identifier> name,
                                                 zc::Vector<zc::Own<Statement>>&& body) {
  return zc::heap<ErrorDeclaration>(zc::mv(name), zc::mv(body));
}

zc::Own<BlockStatement> createBlockStatement(zc::Vector<zc::Own<Statement>>&& statements) {
  return zc::heap<BlockStatement>(zc::mv(statements));
}

zc::Own<ExpressionStatement> createExpressionStatement(zc::Own<Expression> expression) {
  return zc::heap<ExpressionStatement>(zc::mv(expression));
}

zc::Own<IfStatement> createIfStatement(zc::Own<Expression> test, zc::Own<Statement> consequent,
                                       zc::Maybe<zc::Own<Statement>> alternate) {
  return zc::heap<IfStatement>(zc::mv(test), zc::mv(consequent), zc::mv(alternate));
}

zc::Own<WhileStatement> createWhileStatement(zc::Own<Expression> test, zc::Own<Statement> body) {
  return zc::heap<WhileStatement>(zc::mv(test), zc::mv(body));
}

zc::Own<ReturnStatement> createReturnStatement(zc::Maybe<zc::Own<Expression>> argument) {
  return zc::heap<ReturnStatement>(zc::mv(argument));
}

zc::Own<EmptyStatement> createEmptyStatement() { return zc::heap<EmptyStatement>(); }

zc::Own<MatchStatement> createMatchStatement(zc::Own<Expression> discriminant,
                                             zc::Vector<zc::Own<Statement>>&& clauses) {
  return zc::heap<MatchStatement>(zc::mv(discriminant), zc::mv(clauses));
}

zc::Own<ForStatement> createForStatement(zc::Maybe<zc::Own<Statement>> init,
                                         zc::Maybe<zc::Own<Expression>> condition,
                                         zc::Maybe<zc::Own<Expression>> update,
                                         zc::Own<Statement> body) {
  return zc::heap<ForStatement>(zc::mv(init), zc::mv(condition), zc::mv(update), zc::mv(body));
}

/// Expression factory functions
zc::Own<BinaryExpression> createBinaryExpression(zc::Own<Expression> left, zc::Own<TokenNode> op,
                                                 zc::Own<Expression> right) {
  return zc::heap<BinaryExpression>(zc::mv(left), zc::mv(op), zc::mv(right));
}

zc::Own<PrefixUnaryExpression> createPrefixUnaryExpression(SyntaxKind op,
                                                           zc::Own<Expression> operand) {
  return zc::heap<PrefixUnaryExpression>(zc::mv(op), zc::mv(operand));
}

zc::Own<PostfixUnaryExpression> createPostfixUnaryExpression(SyntaxKind op,
                                                             zc::Own<Expression> operand) {
  return zc::heap<PostfixUnaryExpression>(zc::mv(op), zc::mv(operand));
}

zc::Own<ConditionalExpression> createConditionalExpression(zc::Own<Expression> test,
                                                           zc::Own<Expression> consequent,
                                                           zc::Own<Expression> alternate) {
  return zc::heap<ConditionalExpression>(zc::mv(test), zc::mv(consequent), zc::mv(alternate));
}

zc::Own<CallExpression> createCallExpression(
    zc::Own<Expression> callee, zc::Maybe<zc::Own<TokenNode>> questionDotToken,
    zc::Maybe<zc::Vector<zc::Own<ast::TypeNode>>> typeArguments,
    zc::Vector<zc::Own<Expression>>&& arguments) {
  return zc::heap<CallExpression>(zc::mv(callee), zc::mv(questionDotToken), zc::mv(typeArguments),
                                  zc::mv(arguments));
}

zc::Own<PropertyAccessExpression> createPropertyAccessExpression(
    zc::Own<LeftHandSideExpression> expression, zc::Own<Identifier> name, bool questionDot) {
  return zc::heap<PropertyAccessExpression>(zc::mv(expression), zc::mv(name), questionDot);
}

zc::Own<ElementAccessExpression> createElementAccessExpression(
    zc::Own<LeftHandSideExpression> expression, zc::Own<Expression> index, bool questionDot) {
  return zc::heap<ElementAccessExpression>(zc::mv(expression), zc::mv(index), questionDot);
}

zc::Own<AsExpression> createAsExpression(zc::Own<Expression> expression,
                                         zc::Own<TypeNode> targetType) {
  return zc::heap<AsExpression>(zc::mv(expression), zc::mv(targetType));
}

zc::Own<ForcedAsExpression> createForcedAsExpression(zc::Own<Expression> expression,
                                                     zc::Own<TypeNode> targetType) {
  return zc::heap<ForcedAsExpression>(zc::mv(expression), zc::mv(targetType));
}

zc::Own<ConditionalAsExpression> createConditionalAsExpression(zc::Own<Expression> expression,
                                                               zc::Own<TypeNode> targetType) {
  return zc::heap<ConditionalAsExpression>(zc::mv(expression), zc::mv(targetType));
}

zc::Own<VoidExpression> createVoidExpression(zc::Own<Expression> expression) {
  return zc::heap<VoidExpression>(zc::mv(expression));
}

zc::Own<TypeOfExpression> createTypeOfExpression(zc::Own<Expression> expression) {
  return zc::heap<TypeOfExpression>(zc::mv(expression));
}

zc::Own<AwaitExpression> createAwaitExpression(zc::Own<Expression> expression) {
  return zc::heap<AwaitExpression>(zc::mv(expression));
}

zc::Own<NonNullExpression> createNonNullExpression(zc::Own<Expression> expression) {
  return zc::heap<NonNullExpression>(zc::mv(expression));
}

zc::Own<ExpressionWithTypeArguments> createExpressionWithTypeArguments(
    zc::Own<Expression> expression, zc::Maybe<zc::Vector<zc::Own<TypeNode>>> typeArguments) {
  return zc::heap<ExpressionWithTypeArguments>(zc::mv(expression), zc::mv(typeArguments));
}

zc::Own<CaptureElement> createCaptureElement(bool isByReference,
                                             zc::Maybe<zc::Own<Identifier>> identifier,
                                             bool isThis) {
  return zc::heap<CaptureElement>(isByReference, zc::mv(identifier), isThis);
}

zc::Own<FunctionExpression> createFunctionExpression(
    zc::Vector<zc::Own<TypeParameterDeclaration>>&& typeParameters,
    zc::Vector<zc::Own<BindingElement>>&& parameters,
    zc::Vector<zc::Own<CaptureElement>>&& captures, zc::Maybe<zc::Own<TypeNode>> returnType,
    zc::Own<Statement> body) {
  return zc::heap<FunctionExpression>(zc::mv(typeParameters), zc::mv(parameters), zc::mv(captures),
                                      zc::mv(returnType), zc::mv(body));
}

zc::Own<NewExpression> createNewExpression(zc::Own<Expression> callee,
                                           zc::Vector<zc::Own<Expression>>&& arguments) {
  return zc::heap<NewExpression>(zc::mv(callee), zc::mv(arguments));
}

zc::Own<ParenthesizedExpression> createParenthesizedExpression(zc::Own<Expression> expression) {
  return zc::heap<ParenthesizedExpression>(zc::mv(expression));
}

zc::Own<ArrayLiteralExpression> createArrayLiteralExpression(
    zc::Vector<zc::Own<Expression>>&& elements) {
  return zc::heap<ArrayLiteralExpression>(zc::mv(elements));
}

zc::Own<ObjectLiteralExpression> createObjectLiteralExpression(
    zc::Vector<zc::Own<Expression>>&& properties) {
  return zc::heap<ObjectLiteralExpression>(zc::mv(properties));
}

zc::Own<Identifier> createIdentifier(zc::StringPtr name) {
  return zc::heap<Identifier>(zc::mv(name));
}

zc::Own<Identifier> createMissingIdentifier() { return zc::heap<Identifier>(""_zc); }

zc::Own<StringLiteral> createStringLiteral(zc::StringPtr value) {
  return zc::heap<StringLiteral>(zc::mv(value));
}

zc::Own<IntegerLiteral> createIntegerLiteral(int64_t value) {
  return zc::heap<IntegerLiteral>(value);
}

zc::Own<FloatLiteral> createFloatLiteral(double value) { return zc::heap<FloatLiteral>(value); }

zc::Own<BooleanLiteral> createBooleanLiteral(bool value) { return zc::heap<BooleanLiteral>(value); }

zc::Own<NullLiteral> createNullLiteral() { return zc::heap<NullLiteral>(); }

zc::Own<TemplateSpan> createTemplateSpan(zc::Own<Expression> expression,
                                         zc::Own<StringLiteral> literal) {
  return zc::heap<TemplateSpan>(zc::mv(expression), zc::mv(literal));
}

zc::Own<TemplateLiteralExpression> createTemplateLiteralExpression(
    zc::Own<StringLiteral> head, zc::Vector<zc::Own<TemplateSpan>>&& spans) {
  return zc::heap<TemplateLiteralExpression>(zc::mv(head), zc::mv(spans));
}

zc::Own<AliasDeclaration> createAliasDeclaration(zc::Own<Identifier> name, zc::Own<TypeNode> type) {
  return zc::heap<AliasDeclaration>(zc::mv(name), zc::mv(type));
}

zc::Own<DebuggerStatement> createDebuggerStatement() { return zc::heap<DebuggerStatement>(); }

// Type factory functions
zc::Own<TypeReferenceNode> createTypeReference(
    zc::Own<Identifier> typeName, zc::Maybe<zc::Vector<zc::Own<TypeNode>>> typeArguments) {
  return zc::heap<TypeReferenceNode>(zc::mv(typeName), zc::mv(typeArguments));
}

zc::Own<ArrayTypeNode> createArrayType(zc::Own<TypeNode> elementType) {
  return zc::heap<ArrayTypeNode>(zc::mv(elementType));
}

zc::Own<UnionTypeNode> createUnionType(zc::Vector<zc::Own<TypeNode>>&& types) {
  return zc::heap<UnionTypeNode>(zc::mv(types));
}

zc::Own<IntersectionTypeNode> createIntersectionType(zc::Vector<zc::Own<TypeNode>>&& types) {
  return zc::heap<IntersectionTypeNode>(zc::mv(types));
}

zc::Own<ParenthesizedTypeNode> createParenthesizedType(zc::Own<TypeNode> type) {
  return zc::heap<ParenthesizedTypeNode>(zc::mv(type));
}

zc::Own<PredefinedTypeNode> createPredefinedType(zc::StringPtr name) {
  if (name == "bool") {
    return zc::heap<BoolTypeNode>();
  } else if (name == "i8") {
    return zc::heap<I8TypeNode>();
  } else if (name == "i16") {
    return zc::heap<I16TypeNode>();
  } else if (name == "i32") {
    return zc::heap<I32TypeNode>();
  } else if (name == "i64") {
    return zc::heap<I64TypeNode>();
  } else if (name == "u8") {
    return zc::heap<U8TypeNode>();
  } else if (name == "u16") {
    return zc::heap<U16TypeNode>();
  } else if (name == "u32") {
    return zc::heap<U32TypeNode>();
  } else if (name == "u64") {
    return zc::heap<U64TypeNode>();
  } else if (name == "f32") {
    return zc::heap<F32TypeNode>();
  } else if (name == "f64") {
    return zc::heap<F64TypeNode>();
  } else if (name == "str") {
    return zc::heap<StrTypeNode>();
  } else if (name == "unit") {
    return zc::heap<UnitTypeNode>();
  } else if (name == "null") {
    return zc::heap<NullTypeNode>();
  } else {
    ZC_UNREACHABLE;
  }
}

zc::Own<ObjectTypeNode> createObjectType(zc::Vector<zc::Own<Node>>&& members) {
  return zc::heap<ObjectTypeNode>(zc::mv(members));
}

zc::Own<TupleTypeNode> createTupleType(zc::Vector<zc::Own<TypeNode>>&& elementTypes) {
  return zc::heap<TupleTypeNode>(zc::mv(elementTypes));
}

zc::Own<ReturnTypeNode> createReturnType(zc::Own<TypeNode> type,
                                         zc::Maybe<zc::Own<TypeNode>> errorType) {
  return zc::heap<ReturnTypeNode>(zc::mv(type), zc::mv(errorType));
}

zc::Own<FunctionTypeNode> createFunctionType(
    zc::Vector<zc::Own<TypeParameterDeclaration>>&& typeParameters,
    zc::Vector<zc::Own<BindingElement>>&& parameters, zc::Own<ReturnTypeNode> returnType) {
  return zc::heap<FunctionTypeNode>(zc::mv(typeParameters), zc::mv(parameters), zc::mv(returnType));
}

zc::Own<TypeParameterDeclaration> createTypeParameterDeclaration(
    zc::Own<Identifier> name, zc::Maybe<zc::Own<TypeNode>> constraint) {
  return zc::heap<TypeParameterDeclaration>(zc::mv(name), zc::mv(constraint));
}

zc::Own<OptionalTypeNode> createOptionalType(zc::Own<TypeNode> type) {
  return zc::heap<OptionalTypeNode>(zc::mv(type));
}

zc::Own<TypeQueryNode> createTypeQuery(zc::Own<Expression> expr) {
  return zc::heap<TypeQueryNode>(zc::mv(expr));
}

zc::Own<BreakStatement> createBreakStatement(zc::Maybe<zc::Own<Identifier>> label) {
  if (label == zc::none) {
    return zc::heap<BreakStatement>(zc::none);
  } else {
    ZC_IF_SOME(labelPtr, label) { return zc::heap<BreakStatement>(zc::mv(labelPtr)); }
    return zc::heap<BreakStatement>(zc::none);
  }
}

zc::Own<ContinueStatement> createContinueStatement(zc::Maybe<zc::Own<Identifier>> label) {
  if (label == zc::none) {
    return zc::heap<ContinueStatement>(zc::none);
  } else {
    ZC_IF_SOME(labelPtr, label) { return zc::heap<ContinueStatement>(zc::mv(labelPtr)); }
    return zc::heap<ContinueStatement>(zc::none);
  }
}

// Pattern factory functions
// Note: PrimaryPattern is a base class, typically not instantiated directly
// This function is temporarily disabled
// zc::Own<PrimaryPattern> createPrimaryPattern(zc::Own<Expression> expression) {
//   return zc::heap<PrimaryPattern>();
// }

zc::Own<WildcardPattern> createWildcardPattern() { return zc::heap<WildcardPattern>(); }

zc::Own<IdentifierPattern> createIdentifierPattern(zc::Own<Identifier> identifier) {
  return zc::heap<IdentifierPattern>(zc::mv(identifier));
}

zc::Own<TuplePattern> createTuplePattern(zc::Vector<zc::Own<Pattern>>&& elements) {
  return zc::heap<TuplePattern>(zc::mv(elements));
}

zc::Own<StructurePattern> createStructurePattern(
    zc::Maybe<zc::Own<TypeReferenceNode>> typeReference, zc::Vector<zc::Own<Pattern>>&& fields) {
  // TODO: StructurePattern constructor only accepts properties parameter, needs redesign
  // For now, only pass fields parameter
  static_cast<void>(typeReference);
  return zc::heap<StructurePattern>(zc::mv(fields));
}

zc::Own<ArrayPattern> createArrayPattern(zc::Vector<zc::Own<Pattern>>&& elements) {
  return zc::heap<ArrayPattern>(zc::mv(elements));
}

zc::Own<IsPattern> createIsPattern(zc::Own<Pattern> pattern, zc::Own<TypeNode> type) {
  // TODO: IsPattern constructor only accepts type parameter, needs redesign to support pattern
  // For now, only pass type parameter
  static_cast<void>(pattern);
  return zc::heap<IsPattern>(zc::mv(type));
}

zc::Own<ExpressionPattern> createExpressionPattern(zc::Own<Expression> expression) {
  return zc::heap<ExpressionPattern>(zc::mv(expression));
}

zc::Own<EnumPattern> createEnumPattern(zc::Own<TypeReferenceNode> typeReference,
                                       zc::Own<Identifier> propertyName,
                                       zc::Maybe<zc::Own<TuplePattern>> tuplePattern) {
  // TODO: Fix type conversion from TypeReferenceNode to Type
  // For now, cast TypeReferenceNode to Type since TypeReferenceNode inherits from Type
  zc::Maybe<zc::Own<TypeNode>> typeRef = zc::mv(typeReference);

  // TODO: Handle optional TuplePattern - for now create empty one if none provided
  zc::Own<TuplePattern> tuple;
  ZC_IF_SOME(tp, tuplePattern) { tuple = zc::mv(tp); }
  else { tuple = zc::heap<TuplePattern>(zc::Vector<zc::Own<Pattern>>()); }

  return zc::heap<EnumPattern>(zc::mv(typeRef), zc::mv(propertyName), zc::mv(tuple));
}

// Match clause factory functions
zc::Own<MatchClause> createMatchClause(zc::Own<Pattern> pattern,
                                       zc::Maybe<zc::Own<Expression>> guard,
                                       zc::Own<Statement> body) {
  return zc::heap<MatchClause>(zc::mv(pattern), zc::mv(guard), zc::mv(body));
}

zc::Own<DefaultClause> createDefaultClause(zc::Vector<zc::Own<Statement>>&& statements) {
  return zc::heap<DefaultClause>(zc::mv(statements));
}

zc::Own<ArrayBindingPattern> createArrayBindingPattern(
    zc::Vector<zc::Own<BindingElement>>&& elements) {
  return zc::heap<ArrayBindingPattern>(zc::mv(elements));
}

zc::Own<ObjectBindingPattern> createObjectBindingPattern(
    zc::Vector<zc::Own<BindingElement>>&& elements) {
  return zc::heap<ObjectBindingPattern>(zc::mv(elements));
}

// Token node factory functions for operators
zc::Own<TokenNode> createTokenNode(SyntaxKind kind) { return zc::heap<TokenNode>(kind); }

zc::Own<HeritageClause> createHeritageClause(
    SyntaxKind token, zc::Vector<zc::Own<ExpressionWithTypeArguments>>&& types) {
  return zc::heap<HeritageClause>(token, zc::mv(types));
}

zc::Own<PropertyDeclaration> createPropertyDeclaration(zc::Own<Identifier> name,
                                                       zc::Maybe<zc::Own<TypeNode>> type,
                                                       zc::Maybe<zc::Own<Expression>> initializer) {
  return zc::heap<PropertyDeclaration>(zc::mv(name), zc::mv(type), zc::mv(initializer));
}

}  // namespace factory
}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
