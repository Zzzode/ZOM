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
#include "zc/core/io.h"
#include "zc/core/memory.h"
#include "zc/core/string.h"
#include "zomlang/compiler/ast/serializer.h"
#include "zomlang/compiler/ast/visitor.h"

namespace zomlang {
namespace compiler {
namespace ast {

// Forward declarations for types not in visitor.h
class ModulePath;
class BindingElement;
class Type;
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

/// AST dumper class for outputting AST using pluggable serializers
class ASTDumper final : public Visitor {
public:
  explicit ASTDumper(zc::Own<Serializer> serializer) noexcept;
  ~ASTDumper() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(ASTDumper);

  /// Dump a single AST node
  void dump(const Node& node);

  /// Set current indentation level
  void setIndent(int indent) { currentIndent = indent; }

  // Visitor pattern finals for specific node types
  void visit(const SourceFile& sourceFile) final;
  void visit(const ImportDeclaration& importDecl) final;
  void visit(const ExportDeclaration& exportDecl) final;
  void visit(const VariableDeclaration& varDecl) final;
  void visit(const FunctionDeclaration& funcDecl) final;
  void visit(const BlockStatement& blockStmt) final;
  void visit(const ExpressionStatement& exprStmt) final;
  void visit(const EmptyStatement& emptyStmt) final;
  void visit(const BinaryExpression& binExpr) final;
  void visit(const FunctionExpression& funcExpr) final;
  void visit(const StringLiteral& strLit) final;
  void visit(const IntegerLiteral& intLit) final;
  void visit(const FloatLiteral& floatLit) final;
  void visit(const BooleanLiteral& boolLit) final;
  void visit(const NullLiteral& nullLit) final;
  void visit(const CallExpression& callExpr) final;
  void visit(const NewExpression& newExpr) final;
  void visit(const ArrayLiteralExpression& arrLit) final;
  void visit(const ObjectLiteralExpression& objLit) final;
  void visit(const ParenthesizedExpression& parenExpr) final;
  void visit(const Identifier& identifier) final;
  void visit(const PrefixUnaryExpression& prefixUnaryExpr) final;
  void visit(const AssignmentExpression& assignmentExpr) final;
  void visit(const ConditionalExpression& conditionalExpr) final;
  void visit(const CastExpression& castExpr) final;
  void visit(const AsExpression& asExpr) final;
  void visit(const ForcedAsExpression& forcedAsExpr) final;
  void visit(const ConditionalAsExpression& conditionalAsExpr) final;

  // Missing visitor methods that need implementation
  void visit(const Node& node) final;
  void visit(const Statement& statement) final;
  void visit(const Expression& expression) final;
  void visit(const TypeParameter& node) final;
  void visit(const BindingElement& node) final;
  void visit(const ClassDeclaration& node) final;
  void visit(const InterfaceDeclaration& node) final;
  void visit(const StructDeclaration& node) final;
  void visit(const EnumDeclaration& node) final;
  void visit(const ErrorDeclaration& node) final;
  void visit(const AliasDeclaration& node) final;
  void visit(const IfStatement& node) final;
  void visit(const WhileStatement& node) final;
  void visit(const ForStatement& node) final;
  void visit(const BreakStatement& node) final;
  void visit(const ContinueStatement& node) final;
  void visit(const ReturnStatement& node) final;
  void visit(const MatchStatement& node) final;
  void visit(const DebuggerStatement& node) final;
  void visit(const UnaryExpression& node) final;
  void visit(const UpdateExpression& node) final;
  void visit(const PostfixUnaryExpression& node) final;
  void visit(const LeftHandSideExpression& node) final;
  void visit(const MemberExpression& node) final;
  void visit(const PrimaryExpression& node) final;
  void visit(const PropertyAccessExpression& node) final;
  void visit(const ElementAccessExpression& node) final;
  void visit(const OptionalExpression& node) final;
  void visit(const LiteralExpression& node) final;
  void visit(const VoidExpression& node) final;
  void visit(const TypeOfExpression& node) final;
  void visit(const AwaitExpression& node) final;
  void visit(const Operator& op) final;
  void visit(const BinaryOperator& binaryOp) final;
  void visit(const UnaryOperator& unaryOp) final;
  void visit(const AssignmentOperator& assignOp) final;
  void visit(const ModulePath& modulePath) final;

  // Type node visitor methods
  void visit(const Type& type) final;
  void visit(const TypeReference& typeRef) final;
  void visit(const ArrayType& arrayType) final;
  void visit(const UnionType& unionType) final;
  void visit(const IntersectionType& intersectionType) final;
  void visit(const ParenthesizedType& parenType) final;
  void visit(const PredefinedType& predefinedType) final;
  void visit(const ObjectType& objectType) final;
  void visit(const TupleType& tupleType) final;
  void visit(const ReturnType& returnType) final;
  void visit(const FunctionType& functionType) final;
  void visit(const OptionalType& optionalType) final;
  void visit(const TypeQuery& typeQuery) final;

private:
  struct Impl;
  zc::Own<Impl> impl;
  int currentIndent = 0;

  // Helper methods for dumping specific components
  void dumpModulePath(const ModulePath& modulePath);
  void dumpBindingElement(const BindingElement& bindingElement);
  void dumpType(const Type& type);
  void dumpTypeReference(const TypeReference& typeRef);
  void dumpArrayType(const ArrayType& arrayType);
  void dumpUnionType(const UnionType& unionType);
  void dumpIntersectionType(const IntersectionType& intersectionType);
  void dumpParenthesizedType(const ParenthesizedType& parenType);
  void dumpPredefinedType(const PredefinedType& predefinedType);
  void dumpObjectType(const ObjectType& objectType);
  void dumpTupleType(const TupleType& tupleType);
  void dumpReturnType(const ReturnType& returnType);
  void dumpFunctionType(const FunctionType& functionType);
  void dumpOptionalType(const OptionalType& optionalType);
  void dumpTypeQuery(const TypeQuery& typeQuery);

  // Helper methods for output formatting
  void writeIndent(int indent);
  void writeLine(const zc::StringPtr text, int indent = -1);
  void writeNodeHeader(const zc::StringPtr nodeType, int indent = -1);
  void writeNodeFooter(const zc::StringPtr nodeType, int indent = -1);
  void writeProperty(const zc::StringPtr name, const zc::StringPtr value, int indent = -1);
};

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
