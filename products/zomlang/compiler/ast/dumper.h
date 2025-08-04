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

namespace zomlang {
namespace compiler {
namespace ast {

class Node;
class SourceFile;
class Statement;
class Expression;
class ImportDeclaration;
class ExportDeclaration;
class ModulePath;
class BindingElement;
class VariableDeclaration;
class BlockStatement;
class ExpressionStatement;
class BinaryExpression;
class FunctionExpression;
class FunctionDeclaration;
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
class StringLiteral;
class NumericLiteral;
class BooleanLiteral;
class NilLiteral;
class CallExpression;
class NewExpression;
class ArrayLiteralExpression;
class ObjectLiteralExpression;
class ParenthesizedExpression;

/// AST dump output format
enum class DumpFormat {
  kJSON,  // JSON format
  kTEXT,  // Human-readable text format with indentation
  kXML    // XML format
};

/// AST dumper class for outputting AST in various formats
class ASTDumper {
public:
  explicit ASTDumper(zc::OutputStream& output, DumpFormat format = DumpFormat::kJSON) noexcept;
  ~ASTDumper() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(ASTDumper);

  /// Dump a single AST node
  void dump(const Node& node);

  /// Dump a source file (top-level entry point)
  void dumpSourceFile(const SourceFile& sourceFile);

private:
  struct Impl;
  zc::Own<Impl> impl;

  // Internal dump methods for specific node types
  void dumpNode(const Node& node, int indent = 0);
  void dumpStatement(const Statement& stmt, int indent = 0);
  void dumpExpression(const Expression& expr, int indent = 0);
  void dumpImportDeclaration(const ImportDeclaration& importDecl, int indent = 0);
  void dumpExportDeclaration(const ExportDeclaration& exportDecl, int indent = 0);
  void dumpModulePath(const ModulePath& modulePath, int indent = 0);
  void dumpBindingElement(const BindingElement& bindingElement, int indent = 0);
  void dumpVariableDeclaration(const VariableDeclaration& varDecl, int indent = 0);
  void dumpFunctionDeclaration(const FunctionDeclaration& funcDecl, int indent = 0);
  void dumpType(const Type& type, int indent = 0);
  void dumpTypeReference(const TypeReference& typeRef, int indent = 0);
  void dumpArrayType(const ArrayType& arrayType, int indent = 0);
  void dumpUnionType(const UnionType& unionType, int indent = 0);
  void dumpIntersectionType(const IntersectionType& intersectionType, int indent = 0);
  void dumpParenthesizedType(const ParenthesizedType& parenType, int indent = 0);
  void dumpPredefinedType(const PredefinedType& predefinedType, int indent = 0);
  void dumpObjectType(const ObjectType& objectType, int indent = 0);
  void dumpTupleType(const TupleType& tupleType, int indent = 0);
  void dumpReturnType(const ReturnType& returnType, int indent = 0);
  void dumpFunctionType(const FunctionType& functionType, int indent = 0);
  void dumpOptionalType(const OptionalType& optionalType, int indent = 0);
  void dumpTypeQuery(const TypeQuery& typeQuery, int indent = 0);
  void dumpBlockStatement(const BlockStatement& blockStmt, int indent = 0);
  void dumpExpressionStatement(const ExpressionStatement& exprStmt, int indent = 0);
  void dumpBinaryExpression(const BinaryExpression& binExpr, int indent = 0);
  void dumpFunctionExpression(const FunctionExpression& funcExpr, int indent = 0);
  void dumpStringLiteral(const StringLiteral& strLit, int indent = 0);
  void dumpNumericLiteral(const NumericLiteral& numLit, int indent = 0);
  void dumpBooleanLiteral(const BooleanLiteral& boolLit, int indent = 0);
  void dumpNilLiteral(const NilLiteral& nilLit, int indent = 0);
  void dumpCallExpression(const CallExpression& callExpr, int indent = 0);
  void dumpNewExpression(const NewExpression& newExpr, int indent = 0);
  void dumpArrayLiteralExpression(const ArrayLiteralExpression& arrLit, int indent = 0);
  void dumpObjectLiteralExpression(const ObjectLiteralExpression& objLit, int indent = 0);
  void dumpParenthesizedExpression(const ParenthesizedExpression& parenExpr, int indent = 0);

  // Helper methods
  void writeIndent(int indent);
  void writeLine(const zc::StringPtr text, int indent = 0);
  void writeNodeHeader(const zc::StringPtr nodeType, int indent = 0);
  void writeNodeFooter(const zc::StringPtr nodeType, int indent = 0);
  void writeProperty(const zc::StringPtr name, const zc::StringPtr value, int indent = 0);
};

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
