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
#include "zc/core/memory.h"
#include "zc/core/string.h"
#include "zomlang/compiler/ast/serializer.h"
#include "zomlang/compiler/ast/statement.h"
#include "zomlang/compiler/ast/visitor.h"

namespace zomlang {
namespace compiler {
namespace ast {

// Forward declarations for types not in visitor.h
class ModulePath;
class BindingElement;
class TypeNode;
class TypeReferenceNode;
class ArrayTypeNode;
class UnionTypeNode;
class IntersectionTypeNode;
class ParenthesizedTypeNode;
class PredefinedTypeNode;
class ObjectTypeNode;
class TupleTypeNode;
class ReturnTypeNode;
class FunctionTypeNode;
class OptionalTypeNode;
class TypeQueryNode;

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
  void visit(const VariableDeclarationList& varDecl) final;
  void visit(const VariableStatement& varStmt) final;
  void visit(const FunctionDeclaration& funcDecl) final;
  void visit(const BlockStatement& blockStmt) final;
  void visit(const ExpressionStatement& exprStmt) final;
  void visit(const EmptyStatement& emptyStmt) final;
  void visit(const BinaryExpression& binExpr) final;
  void visit(const FunctionExpression& funcExpr) final;
  void visit(const StringLiteral& strLit) final;
  void visit(const TemplateLiteralExpression& templateLit) final;
  void visit(const TemplateSpan& templateSpan) final;
  void visit(const IntegerLiteral& intLit) final;
  void visit(const FloatLiteral& floatLit) final;
  void visit(const BigIntLiteral& bigIntLit) final;
  void visit(const BooleanLiteral& boolLit) final;
  void visit(const NullLiteral& nullLit) final;
  void visit(const CallExpression& callExpr) final;
  void visit(const NewExpression& newExpr) final;
  void visit(const ArrayLiteralExpression& arrLit) final;
  void visit(const ObjectLiteralExpression& objLit) final;
  void visit(const PropertyAssignment& node) final;
  void visit(const ShorthandPropertyAssignment& node) final;
  void visit(const SpreadAssignment& node) final;
  void visit(const SpreadElement& node) final;
  void visit(const ParenthesizedExpression& parenExpr) final;
  void visit(const Identifier& identifier) final;
  void visit(const PrefixUnaryExpression& prefixUnaryExpr) final;
  void visit(const ConditionalExpression& conditionalExpr) final;
  void visit(const CastExpression& castExpr) final;
  void visit(const AsExpression& asExpr) final;
  void visit(const ForcedAsExpression& forcedAsExpr) final;
  void visit(const ConditionalAsExpression& conditionalAsExpr) final;
  void visit(const NonNullExpression& nonNullExpr) final;
  void visit(const ExpressionWithTypeArguments& exprWithTypeArgs) final;
  void visit(const HeritageClause& node) final;

  // Interface node visitor methods (abstract base classes)
  void visit(const PredefinedTypeNode& node) final;
  void visit(const Declaration& node) final;
  void visit(const NamedDeclaration& node) final;
  void visit(const ObjectLiteralElement& node) final;
  void visit(const Pattern& node) final;
  void visit(const PrimaryPattern& node) final;
  void visit(const BindingPattern& node) final;
  void visit(const ClassElement& node) final;
  void visit(const InterfaceElement& node) final;

  // Missing visitor methods that need implementation
  void visit(const Node& node) final;
  void visit(const Statement& statement) final;
  void visit(const IterationStatement& node) final;
  void visit(const DeclarationStatement& node) final;
  void visit(const Expression& expression) final;
  void visit(const TypeParameterDeclaration& node) final;
  void visit(const BindingElement& node) final;
  void visit(const ClassDeclaration& node) final;
  void visit(const InterfaceDeclaration& node) final;
  void visit(const StructDeclaration& node) final;
  void visit(const EnumMember& node) final;
  void visit(const EnumDeclaration& node) final;
  void visit(const ErrorDeclaration& node) final;
  void visit(const AliasDeclaration& node) final;
  void visit(const IfStatement& node) final;
  void visit(const WhileStatement& node) final;
  void visit(const ForStatement& node) final;
  void visit(const ForInStatement& node) final;
  void visit(const LabeledStatement& node) final;
  void visit(const PatternProperty& node) final;
  void visit(const BreakStatement& node) final;
  void visit(const ContinueStatement& node) final;
  void visit(const ReturnStatement& node) final;
  void visit(const MatchStatement& node) final;
  void visit(const MatchClause& node) final;
  void visit(const DefaultClause& node) final;
  void visit(const DebuggerStatement& node) final;
  void visit(const UnaryExpression& node) final;
  void visit(const UpdateExpression& node) final;
  void visit(const PostfixUnaryExpression& node) final;
  void visit(const LeftHandSideExpression& node) final;
  void visit(const MemberExpression& node) final;
  void visit(const PrimaryExpression& node) final;
  void visit(const PropertyAccessExpression& node) final;
  void visit(const ElementAccessExpression& node) final;
  void visit(const LiteralExpression& node) final;
  void visit(const VoidExpression& node) final;
  void visit(const TypeOfExpression& node) final;
  void visit(const AwaitExpression& node) final;
  void visit(const ModulePath& modulePath) final;

  // Type node visitor methods
  void visit(const TypeNode& type) final;
  void visit(const TokenNode& token) final;
  void visit(const TypeReferenceNode& typeRef) final;
  void visit(const ArrayTypeNode& arrayType) final;
  void visit(const UnionTypeNode& unionType) final;
  void visit(const IntersectionTypeNode& intersectionType) final;
  void visit(const ParenthesizedTypeNode& parenType) final;
  void visit(const ObjectTypeNode& objectType) final;
  void visit(const TupleTypeNode& tupleType) final;
  void visit(const ReturnTypeNode& returnType) final;
  void visit(const FunctionTypeNode& functionType) final;
  void visit(const OptionalTypeNode& optionalType) final;
  void visit(const TypeQueryNode& typeQuery) final;
  void visit(const NamedTupleElement& node) final;

  // Missing declaration visitor methods
  void visit(const MethodDeclaration& node) final;
  void visit(const GetAccessor& node) final;
  void visit(const SetAccessor& node) final;
  void visit(const InitDeclaration& node) final;
  void visit(const DeinitDeclaration& node) final;
  void visit(const ParameterDeclaration& node) final;
  void visit(const PropertyDeclaration& node) final;
  void visit(const MissingDeclaration& node) final;
  void visit(const SemicolonClassElement& node) final;
  void visit(const SemicolonInterfaceElement& node) final;

  // Body node visitor methods
  void visit(const InterfaceBody& node) final;
  void visit(const StructBody& node) final;
  void visit(const ErrorBody& node) final;
  void visit(const EnumBody& node) final;
  void visit(const ArrayBindingPattern& node) final;
  void visit(const ObjectBindingPattern& node) final;
  void visit(const ThisExpression& node) final;
  void visit(const SuperExpression& node) final;
  void visit(const BoolTypeNode& node) final;
  void visit(const I8TypeNode& node) final;
  void visit(const I16TypeNode& node) final;
  void visit(const I32TypeNode& node) final;
  void visit(const I64TypeNode& node) final;
  void visit(const U8TypeNode& node) final;
  void visit(const U16TypeNode& node) final;
  void visit(const U32TypeNode& node) final;
  void visit(const U64TypeNode& node) final;
  void visit(const F32TypeNode& node) final;
  void visit(const F64TypeNode& node) final;
  void visit(const StrTypeNode& node) final;
  void visit(const UnitTypeNode& node) final;
  void visit(const NullTypeNode& node) final;

  // Method signature visitor methods
  void visit(const PropertySignature& node) final;
  void visit(const MethodSignature& node) final;

  // Body/Structure visitor methods (additional to existing ones)
  void visit(const ClassBody& node) final;

  // Missing visitor methods for patterns and other nodes
  void visit(const Module& node) final;
  void visit(const VariableDeclaration& node) final;
  void visit(const WildcardPattern& node) final;
  void visit(const IdentifierPattern& node) final;
  void visit(const TuplePattern& node) final;
  void visit(const StructurePattern& node) final;
  void visit(const ArrayPattern& node) final;
  void visit(const IsPattern& node) final;
  void visit(const ExpressionPattern& node) final;
  void visit(const EnumPattern& node) final;
  void visit(const CaptureElement& node) final;

private:
  struct Impl;
  zc::Own<Impl> impl;
  int currentIndent = 0;

  // Helper methods for dumping specific components
  void dumpModulePath(const ModulePath& modulePath);
  void dumpBindingElement(const BindingElement& bindingElement);
  void dumpType(const TypeNode& type);
  void dumpTypeReference(const TypeReferenceNode& typeRef);
  void dumpArrayType(const ArrayTypeNode& arrayType);
  void dumpUnionType(const UnionTypeNode& unionType);
  void dumpIntersectionType(const IntersectionTypeNode& intersectionType);
  void dumpParenthesizedType(const ParenthesizedTypeNode& parenType);
  void dumpPredefinedType(const PredefinedTypeNode& predefinedType);
  void dumpObjectType(const ObjectTypeNode& objectType);
  void dumpTupleType(const TupleTypeNode& tupleType);
  void dumpReturnType(const ReturnTypeNode& returnType);
  void dumpFunctionType(const FunctionTypeNode& functionType);
  void dumpOptionalType(const OptionalTypeNode& optionalType);
  void dumpTypeQuery(const TypeQueryNode& typeQuery);

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
