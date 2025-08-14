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
#include "zomlang/compiler/ast/ast.h"
#include "zomlang/compiler/ast/statement.h"
#include "zomlang/compiler/ast/expression.h"
#include "zomlang/compiler/ast/operator.h"
#include "zomlang/compiler/ast/module.h"

namespace zomlang {
namespace compiler {
namespace ast {

// Forward declarations for all AST node types
class Node;
class Statement;
class Expression;
class Type;
class Identifier;

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
  virtual ~Visitor() noexcept(false) = default;

  ZC_DISALLOW_COPY_AND_MOVE(Visitor);

  // Base visit methods
  virtual void visit(const Node& node) {}
  virtual void visit(const Statement& statement) { visit(static_cast<const Node&>(statement)); }
  virtual void visit(const Expression& expression) { visit(static_cast<const Node&>(expression)); }

  // Statement visitor methods
  virtual void visit(const TypeParameter& node) { visit(static_cast<const Statement&>(node)); }
  virtual void visit(const BindingElement& node) { visit(static_cast<const Statement&>(node)); }
  virtual void visit(const VariableDeclaration& node) { visit(static_cast<const Statement&>(node)); }
  virtual void visit(const FunctionDeclaration& node) { visit(static_cast<const Statement&>(node)); }
  virtual void visit(const ClassDeclaration& node) { visit(static_cast<const Statement&>(node)); }
  virtual void visit(const InterfaceDeclaration& node) { visit(static_cast<const Statement&>(node)); }
  virtual void visit(const StructDeclaration& node) { visit(static_cast<const Statement&>(node)); }
  virtual void visit(const EnumDeclaration& node) { visit(static_cast<const Statement&>(node)); }
  virtual void visit(const ErrorDeclaration& node) { visit(static_cast<const Statement&>(node)); }
  virtual void visit(const AliasDeclaration& node) { visit(static_cast<const Statement&>(node)); }
  virtual void visit(const BlockStatement& node) { visit(static_cast<const Statement&>(node)); }
  virtual void visit(const EmptyStatement& node) { visit(static_cast<const Statement&>(node)); }
  virtual void visit(const ExpressionStatement& node) { visit(static_cast<const Statement&>(node)); }
  virtual void visit(const IfStatement& node) { visit(static_cast<const Statement&>(node)); }
  virtual void visit(const WhileStatement& node) { visit(static_cast<const Statement&>(node)); }
  virtual void visit(const ForStatement& node) { visit(static_cast<const Statement&>(node)); }
  virtual void visit(const BreakStatement& node) { visit(static_cast<const Statement&>(node)); }
  virtual void visit(const ContinueStatement& node) { visit(static_cast<const Statement&>(node)); }
  virtual void visit(const ReturnStatement& node) { visit(static_cast<const Statement&>(node)); }
  virtual void visit(const MatchStatement& node) { visit(static_cast<const Statement&>(node)); }
  virtual void visit(const DebuggerStatement& node) { visit(static_cast<const Statement&>(node)); }

  // Expression visitor methods
  virtual void visit(const UnaryExpression& node) { visit(static_cast<const Expression&>(node)); }
  virtual void visit(const UpdateExpression& node) { visit(static_cast<const UnaryExpression&>(node)); }
  virtual void visit(const PrefixUnaryExpression& node) { visit(static_cast<const UpdateExpression&>(node)); }
  virtual void visit(const PostfixUnaryExpression& node) { visit(static_cast<const UpdateExpression&>(node)); }
  virtual void visit(const LeftHandSideExpression& node) { visit(static_cast<const UpdateExpression&>(node)); }
  virtual void visit(const MemberExpression& node) { visit(static_cast<const LeftHandSideExpression&>(node)); }
  virtual void visit(const PrimaryExpression& node) { visit(static_cast<const MemberExpression&>(node)); }
  virtual void visit(const Identifier& node) { visit(static_cast<const PrimaryExpression&>(node)); }
  virtual void visit(const PropertyAccessExpression& node) { visit(static_cast<const MemberExpression&>(node)); }
  virtual void visit(const ElementAccessExpression& node) { visit(static_cast<const MemberExpression&>(node)); }
  virtual void visit(const NewExpression& node) { visit(static_cast<const PrimaryExpression&>(node)); }
  virtual void visit(const ParenthesizedExpression& node) { visit(static_cast<const PrimaryExpression&>(node)); }
  virtual void visit(const BinaryExpression& node) { visit(static_cast<const Expression&>(node)); }
  virtual void visit(const AssignmentExpression& node) { visit(static_cast<const Expression&>(node)); }
  virtual void visit(const ConditionalExpression& node) { visit(static_cast<const Expression&>(node)); }
  virtual void visit(const CallExpression& node) { visit(static_cast<const LeftHandSideExpression&>(node)); }
  virtual void visit(const OptionalExpression& node) { visit(static_cast<const LeftHandSideExpression&>(node)); }
  virtual void visit(const LiteralExpression& node) { visit(static_cast<const PrimaryExpression&>(node)); }
  virtual void visit(const StringLiteral& node) { visit(static_cast<const LiteralExpression&>(node)); }
  virtual void visit(const IntegerLiteral& node) { visit(static_cast<const LiteralExpression&>(node)); }
  virtual void visit(const FloatLiteral& node) { visit(static_cast<const LiteralExpression&>(node)); }
  virtual void visit(const BooleanLiteral& node) { visit(static_cast<const LiteralExpression&>(node)); }
  virtual void visit(const NullLiteral& node) { visit(static_cast<const LiteralExpression&>(node)); }
  virtual void visit(const CastExpression& node) { visit(static_cast<const Expression&>(node)); }
  virtual void visit(const AsExpression& node) { visit(static_cast<const CastExpression&>(node)); }
  virtual void visit(const ForcedAsExpression& node) { visit(static_cast<const CastExpression&>(node)); }
  virtual void visit(const ConditionalAsExpression& node) { visit(static_cast<const CastExpression&>(node)); }
  virtual void visit(const VoidExpression& node) { visit(static_cast<const UnaryExpression&>(node)); }
  virtual void visit(const TypeOfExpression& node) { visit(static_cast<const UnaryExpression&>(node)); }
  virtual void visit(const AwaitExpression& node) { visit(static_cast<const Expression&>(node)); }
  virtual void visit(const FunctionExpression& node) { visit(static_cast<const PrimaryExpression&>(node)); }
  virtual void visit(const ArrayLiteralExpression& node) { visit(static_cast<const PrimaryExpression&>(node)); }
  virtual void visit(const ObjectLiteralExpression& node) { visit(static_cast<const PrimaryExpression&>(node)); }

  // Visit methods for operators
  virtual void visit(const Operator& op) { visit(static_cast<const Node&>(op)); }
  virtual void visit(const BinaryOperator& binaryOp) { visit(static_cast<const Operator&>(binaryOp)); }
  virtual void visit(const UnaryOperator& unaryOp) { visit(static_cast<const Operator&>(unaryOp)); }
  virtual void visit(const AssignmentOperator& assignOp) { visit(static_cast<const Operator&>(assignOp)); }

  // Visit methods for module types
  virtual void visit(const SourceFile& sourceFile) { visit(static_cast<const Node&>(sourceFile)); }
  virtual void visit(const ModulePath& modulePath) { visit(static_cast<const Node&>(modulePath)); }
  virtual void visit(const ImportDeclaration& importDecl) { visit(static_cast<const Statement&>(importDecl)); }
  virtual void visit(const ExportDeclaration& exportDecl) { visit(static_cast<const Statement&>(exportDecl)); }

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
  virtual ~VisitorResult() noexcept(false) = default;
  ZC_DISALLOW_COPY_AND_MOVE(VisitorResult);

protected:
  VisitorResult() = default;
};

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang