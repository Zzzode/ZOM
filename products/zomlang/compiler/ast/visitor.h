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

namespace zomlang {
namespace compiler {
namespace ast {

// Forward declarations for all AST node types
class Node;
class Statement;
class Expression;
class Type;
class Identifier;

// Type node types
class TypeReference;
class ArrayType;
class UnionType;
class IntersectionType;
class ParenthesizedType;
class PredefinedType;
class ObjectType;
class TupleType;
class ReturnType;
class FunctionType;
class OptionalType;
class TypeQuery;

// Operator types
class Operator;
class BinaryOperator;
class UnaryOperator;
class AssignmentOperator;

// Module types
class SourceFile;
class ModulePath;
class ImportDeclaration;
class ExportDeclaration;

// Statement types
class TypeParameter;
class BindingElement;
class VariableDeclaration;
class FunctionDeclaration;
class ClassDeclaration;
class InterfaceDeclaration;
class StructDeclaration;
class EnumDeclaration;
class ErrorDeclaration;
class AliasDeclaration;
class BlockStatement;
class EmptyStatement;
class ExpressionStatement;
class IfStatement;
class WhileStatement;
class ForStatement;
class BreakStatement;
class ContinueStatement;
class ReturnStatement;
class MatchStatement;
class DebuggerStatement;

// Expression types
class UnaryExpression;
class UpdateExpression;
class PrefixUnaryExpression;
class PostfixUnaryExpression;
class LeftHandSideExpression;
class MemberExpression;
class PrimaryExpression;
class PropertyAccessExpression;
class ElementAccessExpression;
class NewExpression;
class ParenthesizedExpression;
class BinaryExpression;
class AssignmentExpression;
class ConditionalExpression;
class CallExpression;
class OptionalExpression;
class LiteralExpression;
class StringLiteral;
class IntegerLiteral;
class FloatLiteral;
class BooleanLiteral;
class NullLiteral;
class CastExpression;
class AsExpression;
class ForcedAsExpression;
class ConditionalAsExpression;
class VoidExpression;
class TypeOfExpression;
class AwaitExpression;
class FunctionExpression;
class ArrayLiteralExpression;
class ObjectLiteralExpression;

/// \brief Base visitor interface for AST traversal using the visitor pattern.
///
/// This class provides a generic visitor interface that can be used to traverse
/// and process AST nodes. Concrete visitors should inherit from this class and
/// override the visit methods for the node types they are interested in.
///
/// The visitor pattern allows for separation of concerns between the AST structure
/// and the operations performed on it, making it easier to add new operations
/// without modifying the AST node classes.
class Visitor {
public:
  ZC_DISALLOW_COPY_AND_MOVE(Visitor);

  // Base visit methods
  virtual void visit(const Node& node) = 0;
  virtual void visit(const Statement& statement) = 0;
  virtual void visit(const Expression& expression) = 0;
  virtual void visit(const Type& type) = 0;

  // Statement visitor methods
  virtual void visit(const TypeParameter& node) = 0;
  virtual void visit(const BindingElement& node) = 0;
  virtual void visit(const VariableDeclaration& node) = 0;
  virtual void visit(const FunctionDeclaration& node) = 0;
  virtual void visit(const ClassDeclaration& node) = 0;
  virtual void visit(const InterfaceDeclaration& node) = 0;
  virtual void visit(const StructDeclaration& node) = 0;
  virtual void visit(const EnumDeclaration& node) = 0;
  virtual void visit(const ErrorDeclaration& node) = 0;
  virtual void visit(const AliasDeclaration& node) = 0;
  virtual void visit(const BlockStatement& node) = 0;
  virtual void visit(const EmptyStatement& node) = 0;
  virtual void visit(const ExpressionStatement& node) = 0;
  virtual void visit(const IfStatement& node) = 0;
  virtual void visit(const WhileStatement& node) = 0;
  virtual void visit(const ForStatement& node) = 0;
  virtual void visit(const BreakStatement& node) = 0;
  virtual void visit(const ContinueStatement& node) = 0;
  virtual void visit(const ReturnStatement& node) = 0;
  virtual void visit(const MatchStatement& node) = 0;
  virtual void visit(const DebuggerStatement& node) = 0;

  // Expression visitor methods
  virtual void visit(const UnaryExpression& node) = 0;
  virtual void visit(const UpdateExpression& node) = 0;
  virtual void visit(const PrefixUnaryExpression& node) = 0;
  virtual void visit(const PostfixUnaryExpression& node) = 0;
  virtual void visit(const LeftHandSideExpression& node) = 0;
  virtual void visit(const MemberExpression& node) = 0;
  virtual void visit(const PrimaryExpression& node) = 0;
  virtual void visit(const Identifier& node) = 0;
  virtual void visit(const PropertyAccessExpression& node) = 0;
  virtual void visit(const ElementAccessExpression& node) = 0;
  virtual void visit(const NewExpression& node) = 0;
  virtual void visit(const ParenthesizedExpression& node) = 0;
  virtual void visit(const BinaryExpression& node) = 0;
  virtual void visit(const AssignmentExpression& node) = 0;
  virtual void visit(const ConditionalExpression& node) = 0;
  virtual void visit(const CallExpression& node) = 0;
  virtual void visit(const OptionalExpression& node) = 0;
  virtual void visit(const LiteralExpression& node) = 0;
  virtual void visit(const StringLiteral& node) = 0;
  virtual void visit(const IntegerLiteral& node) = 0;
  virtual void visit(const FloatLiteral& node) = 0;
  virtual void visit(const BooleanLiteral& node) = 0;
  virtual void visit(const NullLiteral& node) = 0;
  virtual void visit(const CastExpression& node) = 0;
  virtual void visit(const AsExpression& node) = 0;
  virtual void visit(const ForcedAsExpression& node) = 0;
  virtual void visit(const ConditionalAsExpression& node) = 0;
  virtual void visit(const VoidExpression& node) = 0;
  virtual void visit(const TypeOfExpression& node) = 0;
  virtual void visit(const AwaitExpression& node) = 0;
  virtual void visit(const FunctionExpression& node) = 0;
  virtual void visit(const ArrayLiteralExpression& node) = 0;
  virtual void visit(const ObjectLiteralExpression& node) = 0;

  // Visit methods for operators
  virtual void visit(const Operator& op) = 0;
  virtual void visit(const BinaryOperator& binaryOp) = 0;
  virtual void visit(const UnaryOperator& unaryOp) = 0;
  virtual void visit(const AssignmentOperator& assignOp) = 0;

  // Module visitor methods
  virtual void visit(const SourceFile& sourceFile) = 0;
  virtual void visit(const ModulePath& modulePath) = 0;
  virtual void visit(const ImportDeclaration& importDecl) = 0;
  virtual void visit(const ExportDeclaration& exportDecl) = 0;

  // Type visitor methods
  virtual void visit(const TypeReference& typeRef) = 0;
  virtual void visit(const ArrayType& arrayType) = 0;
  virtual void visit(const UnionType& unionType) = 0;
  virtual void visit(const IntersectionType& intersectionType) = 0;
  virtual void visit(const ParenthesizedType& parenType) = 0;
  virtual void visit(const PredefinedType& predefinedType) = 0;
  virtual void visit(const ObjectType& objectType) = 0;
  virtual void visit(const TupleType& tupleType) = 0;
  virtual void visit(const ReturnType& returnType) = 0;
  virtual void visit(const FunctionType& functionType) = 0;
  virtual void visit(const OptionalType& optionalType) = 0;
  virtual void visit(const TypeQuery& typeQuery) = 0;

protected:
  Visitor() = default;
};

/// \brief Utility class for visitors that need to return values.
///
/// Since virtual functions cannot be templates, visitors that need to return
/// values should store their results in member variables and access them
/// after the visit operation completes.
///
/// Example usage:
/// \code
/// class EvaluationVisitor : public Visitor {
/// private:
///   int result = 0;
/// public:
///   void visit(const IntegerLiteral& node) override {
///     result = node.getValue();
///   }
///   int getResult() const { return result; }
/// };
/// \endcode
class VisitorResult {
public:
  ZC_DISALLOW_COPY_AND_MOVE(VisitorResult);

protected:
  VisitorResult() = default;
};

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
