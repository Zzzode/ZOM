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

#include "zc/core/common.h"
#include "zc/core/string.h"
#include "zomlang/compiler/ast/ast.h"
#include "zomlang/compiler/ast/operator.h"

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
class ModulePath;
class ImportDeclaration;
class ExportDeclaration;
class BindingElement;
class VariableDeclaration;
class FunctionDeclaration;
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
class TypeParameter;

// Expression
class BinaryExpression;
class PrefixUnaryExpression;
class PostfixUnaryExpression;
class AssignmentExpression;
class ConditionalExpression;
class CallExpression;
class MemberExpression;
class UpdateExpression;
class CastExpression;
class VoidExpression;
class TypeOfExpression;
class AwaitExpression;
class FunctionExpression;
class NewExpression;
class ParenthesizedExpression;
class Identifier;
class StringLiteral;
class NumericLiteral;
class BooleanLiteral;
class NilLiteral;
class DebuggerStatement;
class OptionalExpression;
class PropertyAccessExpression;
class ElementAccessExpression;
class LeftHandSideExpression;
class ArrayLiteralExpression;
class ObjectLiteralExpression;

// Types
class Type;
class ArrayType;
class ObjectType;
class TupleType;
class UnionType;
class IntersectionType;
class ParenthesizedType;
class TypeReference;
class PredefinedType;
class FunctionType;
class ReturnType;
class OptionalType;
class TypeQuery;

// Operators
class Operator;
class BinaryOperator;
class UnaryOperator;
class AssignmentOperator;
class UpdateOperator;

namespace factory {
/// Create a new SourceFile Node.
zc::Own<SourceFile> createSourceFile(zc::String&& fileName,
                                     zc::Vector<zc::Own<ast::Statement>>&& statements);

template <typename Node>
  requires DerivedFromNode<Node>
const ast::NodeList<Node> createNodeList(zc::Vector<zc::Own<Node>>&& list) {
  return ast::NodeList<Node>(zc::mv(list));
}

template <typename Node, typename... Args>
  requires DerivedFromNode<Node>
zc::Own<Node> createNodeWithRange(const source::SourceRange& range, Args&&... args) {
  zc::Own<Node> node = zc::heap<Node>(zc::fwd<Args>(args)...);
  node->setSourceRange(range);
  return zc::mv(node);
}

/// Create a ModulePath node
zc::Own<ModulePath> createModulePath(zc::Vector<zc::String>&& identifiers);

/// Create an ImportDeclaration node
zc::Own<ImportDeclaration> createImportDeclaration(zc::Own<ModulePath> modulePath,
                                                   zc::Maybe<zc::String> alias = zc::none);

/// Create a simple ExportDeclaration node (export identifier)
zc::Own<ExportDeclaration> createExportDeclaration(zc::String&& identifier);

/// Create a rename ExportDeclaration node (export identifier as alias from modulePath)
zc::Own<ExportDeclaration> createExportDeclaration(zc::String&& identifier, zc::String&& alias,
                                                   zc::Own<ModulePath> modulePath);

/// Statement factory functions
zc::Own<BindingElement> createBindingElement(zc::Own<Identifier> name,
                                             zc::Maybe<zc::Own<Type>> type = zc::none,
                                             zc::Maybe<zc::Own<Expression>> init = zc::none);

zc::Own<VariableDeclaration> createVariableDeclaration(
    zc::Vector<zc::Own<BindingElement>>&& bindings);

zc::Own<FunctionDeclaration> createFunctionDeclaration(
    zc::Own<Identifier> name, zc::Vector<zc::Own<TypeParameter>>&& typeParameters,
    zc::Vector<zc::Own<BindingElement>>&& parameters, zc::Maybe<zc::Own<ReturnType>> returnType,
    zc::Own<Statement> body);

zc::Own<ClassDeclaration> createClassDeclaration(zc::Own<Identifier> name,
                                                 zc::Vector<zc::Own<Statement>>&& body);

zc::Own<InterfaceDeclaration> createInterfaceDeclaration(zc::Own<Identifier> name,
                                                         zc::Vector<zc::Own<Statement>>&& body);

zc::Own<StructDeclaration> createStructDeclaration(zc::Own<Identifier> name,
                                                   zc::Vector<zc::Own<Statement>>&& body);

zc::Own<EnumDeclaration> createEnumDeclaration(zc::Own<Identifier> name,
                                               zc::Vector<zc::Own<Statement>>&& body);

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

zc::Own<ForStatement> createForStatement(zc::Maybe<zc::Own<Statement>> init,
                                         zc::Maybe<zc::Own<Expression>> condition,
                                         zc::Maybe<zc::Own<Expression>> update,
                                         zc::Own<Statement> body);

/// Expression factory functions
zc::Own<Expression> createBinaryExpression(zc::Own<Expression> left, zc::Own<BinaryOperator> op,
                                           zc::Own<Expression> right);

zc::Own<PrefixUnaryExpression> createPrefixUnaryExpression(zc::Own<UnaryOperator> op,
                                                           zc::Own<Expression> operand);

zc::Own<PostfixUnaryExpression> createPostfixUnaryExpression(zc::Own<UnaryOperator> op,
                                                             zc::Own<Expression> operand);

zc::Own<AssignmentExpression> createAssignmentExpression(zc::Own<Expression> left,
                                                         zc::Own<AssignmentOperator> op,
                                                         zc::Own<Expression> right);

zc::Own<ConditionalExpression> createConditionalExpression(zc::Own<Expression> test,
                                                           zc::Own<Expression> consequent,
                                                           zc::Own<Expression> alternate);

zc::Own<CallExpression> createCallExpression(zc::Own<Expression> callee,
                                             zc::Vector<zc::Own<Expression>>&& arguments);

zc::Own<PropertyAccessExpression> createPropertyAccessExpression(
    zc::Own<LeftHandSideExpression> expression, zc::Own<Identifier> name, bool questionDot = false);

zc::Own<ElementAccessExpression> createElementAccessExpression(
    zc::Own<LeftHandSideExpression> expression, zc::Own<Expression> index,
    bool questionDot = false);

zc::Own<OptionalExpression> createOptionalExpression(zc::Own<Expression> object,
                                                     zc::Own<Expression> property);

zc::Own<CastExpression> createCastExpression(zc::Own<Expression> expression,
                                             zc::String&& targetType, bool isOptional = false);

zc::Own<VoidExpression> createVoidExpression(zc::Own<Expression> expression);

zc::Own<TypeOfExpression> createTypeOfExpression(zc::Own<Expression> expression);

zc::Own<AwaitExpression> createAwaitExpression(zc::Own<Expression> expression);

zc::Own<FunctionExpression> createFunctionExpression(
    zc::Vector<zc::Own<TypeParameter>>&& typeParameters,
    zc::Vector<zc::Own<BindingElement>>&& parameters, zc::Maybe<zc::Own<Type>> returnType,
    zc::Own<Statement> body);

zc::Own<NewExpression> createNewExpression(zc::Own<Expression> callee,
                                           zc::Vector<zc::Own<Expression>>&& arguments);

zc::Own<ParenthesizedExpression> createParenthesizedExpression(zc::Own<Expression> expression);

zc::Own<ArrayLiteralExpression> createArrayLiteralExpression(
    zc::Vector<zc::Own<Expression>>&& elements);

zc::Own<ObjectLiteralExpression> createObjectLiteralExpression(
    zc::Vector<zc::Own<Expression>>&& properties);

zc::Own<Identifier> createIdentifier(zc::String&& name);

zc::Own<StringLiteral> createStringLiteral(zc::String&& value);

zc::Own<NumericLiteral> createNumericLiteral(double value);

zc::Own<BooleanLiteral> createBooleanLiteral(bool value);

zc::Own<NilLiteral> createNilLiteral();

zc::Own<AliasDeclaration> createAliasDeclaration(zc::Own<Identifier> name, zc::Own<Type> type);

zc::Own<DebuggerStatement> createDebuggerStatement();

/// Type
zc::Own<ArrayType> createArrayType(zc::Own<Type> elementType);

zc::Own<ObjectType> createObjectType(zc::Vector<zc::Own<Node>>&& members);

zc::Own<TupleType> createTupleType(zc::Vector<zc::Own<Type>>&& elementTypes);

zc::Own<UnionType> createUnionType(zc::Vector<zc::Own<Type>>&& types);

zc::Own<IntersectionType> createIntersectionType(zc::Vector<zc::Own<Type>>&& types);

zc::Own<ParenthesizedType> createParenthesizedType(zc::Own<Type> type);

zc::Own<TypeReference> createTypeReference(zc::Own<Identifier> typeName,
                                           zc::Maybe<zc::Vector<zc::Own<Type>>> typeArguments);

zc::Own<PredefinedType> createPredefinedType(zc::String&& name);

zc::Own<ReturnType> createReturnType(zc::Own<Type> type, zc::Maybe<zc::Own<Type>> errorType);

zc::Own<FunctionType> createFunctionType(zc::Vector<zc::Own<TypeParameter>>&& typeParameters,
                                         zc::Vector<zc::Own<BindingElement>>&& parameters,
                                         zc::Own<ReturnType> returnType);

zc::Own<OptionalType> createOptionalType(zc::Own<Type> type);

/// Type query factory function
zc::Own<TypeQuery> createTypeQuery(zc::Own<Expression> expr);

zc::Own<TypeParameter> createTypeParameterDeclaration(
    zc::Own<Identifier> name, zc::Maybe<zc::Own<Type>> constraint = zc::none);

/// Operator factory functions
zc::Own<BinaryOperator> createBinaryOperator(
    zc::String&& symbol, OperatorPrecedence precedence,
    OperatorAssociativity associativity = OperatorAssociativity::kLeft);

zc::Own<UnaryOperator> createUnaryOperator(zc::String&& symbol, bool prefix = true);

zc::Own<AssignmentOperator> createAssignmentOperator(zc::String&& symbol);

// Predefined operator factory functions for common operators
zc::Own<BinaryOperator> createAddOperator();           // +
zc::Own<BinaryOperator> createSubtractOperator();      // -
zc::Own<BinaryOperator> createMultiplyOperator();      // *
zc::Own<BinaryOperator> createDivideOperator();        // /
zc::Own<BinaryOperator> createModuloOperator();        // %
zc::Own<BinaryOperator> createEqualOperator();         // ==
zc::Own<BinaryOperator> createNotEqualOperator();      // !=
zc::Own<BinaryOperator> createLessOperator();          // <
zc::Own<BinaryOperator> createGreaterOperator();       // >
zc::Own<BinaryOperator> createLessEqualOperator();     // <=
zc::Own<BinaryOperator> createGreaterEqualOperator();  // >=
zc::Own<BinaryOperator> createLogicalAndOperator();    // &&
zc::Own<BinaryOperator> createLogicalOrOperator();     // ||

zc::Own<UnaryOperator> createUnaryPlusOperator();      // +
zc::Own<UnaryOperator> createUnaryMinusOperator();     // -
zc::Own<UnaryOperator> createLogicalNotOperator();     // !
zc::Own<UnaryOperator> createBitwiseNotOperator();     // ~
zc::Own<UnaryOperator> createPreIncrementOperator();   // ++x
zc::Own<UnaryOperator> createPostIncrementOperator();  // x++
zc::Own<UnaryOperator> createPreDecrementOperator();   // --x
zc::Own<UnaryOperator> createPostDecrementOperator();  // x--
zc::Own<UnaryOperator> createVoidOperator();           // void
zc::Own<UnaryOperator> createTypeOfOperator();         // typeof

zc::Own<AssignmentOperator> createAssignOperator();          // =
zc::Own<AssignmentOperator> createAddAssignOperator();       // +=
zc::Own<AssignmentOperator> createSubtractAssignOperator();  // -=

}  // namespace factory
}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
