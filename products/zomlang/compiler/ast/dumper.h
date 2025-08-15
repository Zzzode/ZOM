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

/// AST dump output format
enum class DumpFormat {
  kJSON,  // JSON format
  kTEXT,  // Human-readable text format with indentation
  kXML    // XML format
};

/// AST dumper class for outputting AST in various formats using visitor pattern
class ASTDumper : public Visitor {
public:
  explicit ASTDumper(zc::OutputStream& output, DumpFormat format = DumpFormat::kJSON) noexcept;
  ~ASTDumper() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(ASTDumper);

  /// Dump a single AST node
  void dump(const Node& node);

  /// Set current indentation level
  void setIndent(int indent) { currentIndent = indent; }

  // Visitor pattern overrides for specific node types
  void visit(const SourceFile& sourceFile) override;
  void visit(const ImportDeclaration& importDecl) override;
  void visit(const ExportDeclaration& exportDecl) override;
  void visit(const VariableDeclaration& varDecl) override;
  void visit(const FunctionDeclaration& funcDecl) override;
  void visit(const BlockStatement& blockStmt) override;
  void visit(const ExpressionStatement& exprStmt) override;
  void visit(const EmptyStatement& emptyStmt) override;
  void visit(const BinaryExpression& binExpr) override;
  void visit(const FunctionExpression& funcExpr) override;
  void visit(const StringLiteral& strLit) override;
  void visit(const IntegerLiteral& intLit) override;
  void visit(const FloatLiteral& floatLit) override;
  void visit(const BooleanLiteral& boolLit) override;
  void visit(const NullLiteral& nullLit) override;
  void visit(const CallExpression& callExpr) override;
  void visit(const NewExpression& newExpr) override;
  void visit(const ArrayLiteralExpression& arrLit) override;
  void visit(const ObjectLiteralExpression& objLit) override;
  void visit(const ParenthesizedExpression& parenExpr) override;
  void visit(const Identifier& identifier) override;
  void visit(const PrefixUnaryExpression& prefixUnaryExpr) override;
  void visit(const AssignmentExpression& assignmentExpr) override;
  void visit(const ConditionalExpression& conditionalExpr) override;
  void visit(const CastExpression& castExpr) override;
  void visit(const AsExpression& asExpr) override;
  void visit(const ForcedAsExpression& forcedAsExpr) override;
  void visit(const ConditionalAsExpression& conditionalAsExpr) override;

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
