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

#include "zomlang/compiler/ast/expression.h"
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

zc::Own<ModulePath> createModulePath(zc::Vector<zc::String>&& identifiers) {
  return zc::heap<ModulePath>(zc::mv(identifiers));
}

zc::Own<ImportDeclaration> createImportDeclaration(zc::Own<ModulePath> modulePath,
                                                   zc::Maybe<zc::String> alias) {
  return zc::heap<ImportDeclaration>(zc::mv(modulePath), zc::mv(alias));
}

zc::Own<ExportDeclaration> createExportDeclaration(zc::String&& identifier) {
  return zc::heap<ExportDeclaration>(zc::mv(identifier));
}

zc::Own<ExportDeclaration> createExportDeclaration(zc::String&& identifier, zc::String&& alias,
                                                   zc::Own<ModulePath> modulePath) {
  return zc::heap<ExportDeclaration>(zc::mv(identifier), zc::mv(alias), zc::mv(modulePath));
}

// Statement factory functions
zc::Own<VariableDeclaration> createVariableDeclaration(zc::Own<Identifier> name,
                                                       zc::Maybe<zc::Own<Type>> type,
                                                       zc::Maybe<zc::Own<Expression>> init) {
  return zc::heap<VariableDeclaration>(zc::mv(name), zc::mv(type), zc::mv(init));
}

zc::Own<FunctionDeclaration> createFunctionDeclaration(zc::Own<Identifier> name,
                                                       zc::Vector<zc::Own<Statement>>&& body) {
  zc::Vector<zc::Own<Identifier>> parameters;
  zc::Maybe<zc::Own<Type>> returnType = zc::none;
  auto bodyStatement = zc::heap<BlockStatement>(zc::mv(body));
  return zc::heap<FunctionDeclaration>(zc::mv(name), zc::mv(parameters), zc::mv(returnType),
                                       zc::mv(bodyStatement));
}

zc::Own<ClassDeclaration> createClassDeclaration(zc::Own<Identifier> name,
                                                 zc::Vector<zc::Own<Statement>>&& body) {
  return zc::heap<ClassDeclaration>(zc::mv(name), zc::none, zc::mv(body));
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

// Expression factory functions
zc::Own<Expression> createBinaryExpression(zc::Own<Expression> left, zc::Own<BinaryOperator> op,
                                           zc::Own<Expression> right) {
  return zc::heap<BinaryExpression>(zc::mv(left), zc::mv(op), zc::mv(right));
}

zc::Own<PrefixUnaryExpression> createPrefixUnaryExpression(zc::Own<UnaryOperator> op,
                                                           zc::Own<Expression> operand) {
  return zc::heap<PrefixUnaryExpression>(zc::mv(op), zc::mv(operand));
}

zc::Own<PostfixUnaryExpression> createPostfixUnaryExpression(zc::Own<UnaryOperator> op,
                                                             zc::Own<Expression> operand) {
  return zc::heap<PostfixUnaryExpression>(zc::mv(op), zc::mv(operand));
}

zc::Own<AssignmentExpression> createAssignmentExpression(zc::Own<Expression> left,
                                                         zc::Own<AssignmentOperator> op,
                                                         zc::Own<Expression> right) {
  return zc::heap<AssignmentExpression>(zc::mv(left), zc::mv(op), zc::mv(right));
}

zc::Own<ConditionalExpression> createConditionalExpression(zc::Own<Expression> test,
                                                           zc::Own<Expression> consequent,
                                                           zc::Own<Expression> alternate) {
  return zc::heap<ConditionalExpression>(zc::mv(test), zc::mv(consequent), zc::mv(alternate));
}

zc::Own<CallExpression> createCallExpression(zc::Own<Expression> callee,
                                             zc::Vector<zc::Own<Expression>>&& arguments) {
  return zc::heap<CallExpression>(zc::mv(callee), zc::mv(arguments));
}

zc::Own<PropertyAccessExpression> createPropertyAccessExpression(
    zc::Own<LeftHandSideExpression> expression, zc::Own<Identifier> name, bool questionDot) {
  return zc::heap<PropertyAccessExpression>(zc::mv(expression), zc::mv(name), questionDot);
}

zc::Own<ElementAccessExpression> createElementAccessExpression(
    zc::Own<LeftHandSideExpression> expression, zc::Own<Expression> index, bool questionDot) {
  return zc::heap<ElementAccessExpression>(zc::mv(expression), zc::mv(index), questionDot);
}

zc::Own<OptionalExpression> createOptionalExpression(zc::Own<Expression> object,
                                                     zc::Own<Expression> property) {
  return zc::heap<OptionalExpression>(zc::mv(object), zc::mv(property));
}

zc::Own<CastExpression> createCastExpression(zc::Own<Expression> expression,
                                             zc::String&& targetType, bool isOptional) {
  return zc::heap<CastExpression>(zc::mv(expression), zc::mv(targetType), isOptional);
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

zc::Own<NewExpression> createNewExpression(zc::Own<Expression> callee,
                                           zc::Vector<zc::Own<Expression>>&& arguments) {
  return zc::heap<NewExpression>(zc::mv(callee), zc::mv(arguments));
}

zc::Own<ParenthesizedExpression> createParenthesizedExpression(zc::Own<Expression> expression) {
  return zc::heap<ParenthesizedExpression>(zc::mv(expression));
}

zc::Own<Identifier> createIdentifier(zc::String&& name) {
  return zc::heap<Identifier>(zc::mv(name));
}

zc::Own<StringLiteral> createStringLiteral(zc::String&& value) {
  return zc::heap<StringLiteral>(zc::mv(value));
}

zc::Own<NumericLiteral> createNumericLiteral(double value) {
  return zc::heap<NumericLiteral>(value);
}

zc::Own<BooleanLiteral> createBooleanLiteral(bool value) { return zc::heap<BooleanLiteral>(value); }

zc::Own<NilLiteral> createNilLiteral() { return zc::heap<NilLiteral>(); }

zc::Own<AliasDeclaration> createAliasDeclaration(zc::Own<Identifier> name, zc::Own<Type> type) {
  return zc::heap<AliasDeclaration>(zc::mv(name), zc::mv(type));
}

zc::Own<DebuggerStatement> createDebuggerStatement() { return zc::heap<DebuggerStatement>(); }

// Type factory functions
zc::Own<TypeReference> createTypeReference(zc::Own<Identifier> typeName) {
  return zc::heap<TypeReference>(zc::mv(typeName));
}

zc::Own<ArrayType> createArrayType(zc::Own<Type> elementType) {
  return zc::heap<ArrayType>(zc::mv(elementType));
}

zc::Own<UnionType> createUnionType(zc::Vector<zc::Own<Type>>&& types) {
  return zc::heap<UnionType>(zc::mv(types));
}

zc::Own<IntersectionType> createIntersectionType(zc::Vector<zc::Own<Type>>&& types) {
  return zc::heap<IntersectionType>(zc::mv(types));
}

zc::Own<ParenthesizedType> createParenthesizedType(zc::Own<Type> type) {
  return zc::heap<ParenthesizedType>(zc::mv(type));
}

zc::Own<PredefinedType> createPredefinedType(zc::String&& name) {
  return zc::heap<PredefinedType>(zc::mv(name));
}

zc::Own<ObjectType> createObjectType(zc::Vector<zc::Own<Node>>&& members) {
  return zc::heap<ObjectType>(zc::mv(members));
}

zc::Own<TupleType> createTupleType(zc::Vector<zc::Own<Type>>&& elementTypes) {
  return zc::heap<TupleType>(zc::mv(elementTypes));
}

zc::Own<FunctionType> createFunctionType(zc::Vector<zc::Own<Type>>&& parameterTypes,
                                         zc::Own<Type> returnType) {
  return zc::heap<FunctionType>(zc::mv(parameterTypes), zc::mv(returnType));
}

zc::Own<TypeAnnotation> createTypeAnnotation(zc::Own<Type> type) {
  return zc::heap<TypeAnnotation>(zc::mv(type));
}

// Operator factory functions
zc::Own<BinaryOperator> createBinaryOperator(zc::String&& symbol, OperatorPrecedence precedence,
                                             OperatorAssociativity associativity) {
  return zc::heap<BinaryOperator>(zc::mv(symbol), precedence, associativity);
}

zc::Own<UnaryOperator> createUnaryOperator(zc::String&& symbol, bool prefix) {
  return zc::heap<UnaryOperator>(zc::mv(symbol), prefix);
}

zc::Own<AssignmentOperator> createAssignmentOperator(zc::String&& symbol) {
  return zc::heap<AssignmentOperator>(zc::mv(symbol));
}

// Predefined operator factory functions for common operators
zc::Own<BinaryOperator> createAddOperator() {
  return createBinaryOperator(zc::str("+"), OperatorPrecedence::kAdditive,
                              OperatorAssociativity::kLeft);
}

zc::Own<BinaryOperator> createSubtractOperator() {
  return createBinaryOperator(zc::str("-"), OperatorPrecedence::kAdditive,
                              OperatorAssociativity::kLeft);
}

zc::Own<BinaryOperator> createMultiplyOperator() {
  return createBinaryOperator(zc::str("*"), OperatorPrecedence::kMultiplicative,
                              OperatorAssociativity::kLeft);
}

zc::Own<BinaryOperator> createDivideOperator() {
  return createBinaryOperator(zc::str("/"), OperatorPrecedence::kMultiplicative,
                              OperatorAssociativity::kLeft);
}

zc::Own<BinaryOperator> createModuloOperator() {
  return createBinaryOperator(zc::str("%"), OperatorPrecedence::kMultiplicative,
                              OperatorAssociativity::kLeft);
}

zc::Own<BinaryOperator> createEqualOperator() {
  return createBinaryOperator(zc::str("=="), OperatorPrecedence::kEquality,
                              OperatorAssociativity::kLeft);
}

zc::Own<BinaryOperator> createNotEqualOperator() {
  return createBinaryOperator(zc::str("!="), OperatorPrecedence::kEquality,
                              OperatorAssociativity::kLeft);
}

zc::Own<BinaryOperator> createLessOperator() {
  return createBinaryOperator(zc::str("<"), OperatorPrecedence::kRelational,
                              OperatorAssociativity::kLeft);
}

zc::Own<BinaryOperator> createGreaterOperator() {
  return createBinaryOperator(zc::str(">"), OperatorPrecedence::kRelational,
                              OperatorAssociativity::kLeft);
}

zc::Own<BinaryOperator> createLessEqualOperator() {
  return createBinaryOperator(zc::str("<="), OperatorPrecedence::kRelational,
                              OperatorAssociativity::kLeft);
}

zc::Own<BinaryOperator> createGreaterEqualOperator() {
  return createBinaryOperator(zc::str(">="), OperatorPrecedence::kRelational,
                              OperatorAssociativity::kLeft);
}

zc::Own<BinaryOperator> createLogicalAndOperator() {
  return createBinaryOperator(zc::str("&&"), OperatorPrecedence::kLogicalAnd,
                              OperatorAssociativity::kLeft);
}

zc::Own<BinaryOperator> createLogicalOrOperator() {
  return createBinaryOperator(zc::str("||"), OperatorPrecedence::kLogicalOr,
                              OperatorAssociativity::kLeft);
}

zc::Own<UnaryOperator> createUnaryPlusOperator() { return createUnaryOperator(zc::str("+"), true); }

zc::Own<UnaryOperator> createUnaryMinusOperator() {
  return createUnaryOperator(zc::str("-"), true);
}

zc::Own<UnaryOperator> createLogicalNotOperator() {
  return createUnaryOperator(zc::str("!"), true);
}

zc::Own<UnaryOperator> createBitwiseNotOperator() {
  return createUnaryOperator(zc::str("~"), true);
}

zc::Own<UnaryOperator> createVoidOperator() { return createUnaryOperator(zc::str("void"), true); }

zc::Own<UnaryOperator> createTypeOfOperator() {
  return createUnaryOperator(zc::str("typeof"), true);
}

zc::Own<AssignmentOperator> createAssignOperator() {
  return createAssignmentOperator(zc::str("="));
}

zc::Own<AssignmentOperator> createAddAssignOperator() {
  return createAssignmentOperator(zc::str("+="));
}

zc::Own<AssignmentOperator> createSubtractAssignOperator() {
  return createAssignmentOperator(zc::str("-="));
}

zc::Own<UnaryOperator> createPreIncrementOperator() {
  return createUnaryOperator(zc::str("++"), true);
}

zc::Own<UnaryOperator> createPostIncrementOperator() {
  return createUnaryOperator(zc::str("++"), false);
}

zc::Own<UnaryOperator> createPreDecrementOperator() {
  return createUnaryOperator(zc::str("--"), true);
}

zc::Own<UnaryOperator> createPostDecrementOperator() {
  return createUnaryOperator(zc::str("--"), false);
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

}  // namespace factory
}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
