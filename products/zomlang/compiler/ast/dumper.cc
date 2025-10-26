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

#include "zomlang/compiler/ast/dumper.h"

#include "zomlang/compiler/ast/expression.h"
#include "zomlang/compiler/ast/module.h"
#include "zomlang/compiler/ast/operator.h"
#include "zomlang/compiler/ast/serializer.h"
#include "zomlang/compiler/ast/statement.h"
#include "zomlang/compiler/ast/type.h"

namespace zomlang {
namespace compiler {
namespace ast {

struct ASTDumper::Impl {
  zc::Own<Serializer> serializer;

  explicit Impl(zc::Own<Serializer> s) : serializer(zc::mv(s)) {}
};

ASTDumper::ASTDumper(zc::Own<Serializer> serializer) noexcept
    : impl(zc::heap<Impl>(zc::mv(serializer))) {}

ASTDumper::~ASTDumper() noexcept(false) = default;

void ASTDumper::dump(const Node& node) { node.accept(*this); }

void ASTDumper::visit(const SourceFile& node) {
  impl->serializer->writeNodeStart("SourceFile"_zc);

  impl->serializer->writeProperty("fileName", node.getFileName());

  const auto& statements = node.getStatements();
  impl->serializer->writeArrayStart("statements"_zc, statements.size());
  for (const auto& stmt : statements) {
    impl->serializer->writeArrayElement();
    stmt.accept(*this);
  }
  impl->serializer->writeArrayEnd("statements"_zc);

  impl->serializer->writeNodeEnd("SourceFile"_zc);
}

void ASTDumper::visit(const ImportDeclaration& node) {
  impl->serializer->writeNodeStart("ImportDeclaration"_zc);

  impl->serializer->writeChildStart("modulePath"_zc);
  node.getModulePath().accept(*this);
  impl->serializer->writeChildEnd("modulePath"_zc);

  ZC_IF_SOME(alias, node.getAlias()) { impl->serializer->writeProperty("alias", alias.getText()); }

  impl->serializer->writeNodeEnd("ImportDeclaration"_zc);
}

void ASTDumper::visit(const ExportDeclaration& node) {
  impl->serializer->writeNodeStart("ExportDeclaration"_zc);

  impl->serializer->writeChildStart("exportPath"_zc);
  node.getExportPath().accept(*this);
  impl->serializer->writeChildEnd("exportPath"_zc);

  ZC_IF_SOME(alias, node.getAlias()) {
    impl->serializer->writeChildStart("alias"_zc);
    alias.accept(*this);
    impl->serializer->writeChildEnd("alias"_zc);
  }

  impl->serializer->writeNodeEnd("ExportDeclaration"_zc);
}

void ASTDumper::visit(const VariableDeclarationList& node) {
  impl->serializer->writeNodeStart("VariableDeclarationList"_zc);

  const auto& bindings = node.getBindings();
  impl->serializer->writeArrayStart("bindings"_zc, bindings.size());
  for (const auto& binding : bindings) {
    impl->serializer->writeArrayElement();
    binding.accept(*this);
  }
  impl->serializer->writeArrayEnd("bindings"_zc);

  impl->serializer->writeNodeEnd("VariableDeclarationList"_zc);
}

void ASTDumper::visit(const VariableStatement& node) {
  impl->serializer->writeNodeStart("VariableStatement"_zc);

  impl->serializer->writeChildStart("declarations"_zc);
  node.getDeclarations().accept(*this);
  impl->serializer->writeChildEnd("declarations"_zc);

  impl->serializer->writeNodeEnd("VariableStatement"_zc);
}

void ASTDumper::visit(const FunctionDeclaration& node) {
  impl->serializer->writeNodeStart("FunctionDeclaration"_zc);

  impl->serializer->writeChildStart("name"_zc);
  node.getName().accept(*this);
  impl->serializer->writeChildEnd("name"_zc);

  const auto& typeParams = node.getTypeParameters();
  if (!typeParams.empty()) {
    impl->serializer->writeArrayStart("typeParameters"_zc, typeParams.size());
    for (const auto& param : typeParams) {
      impl->serializer->writeArrayElement();
      param.accept(*this);
    }
    impl->serializer->writeArrayEnd("typeParameters"_zc);
  }

  const auto& params = node.getParameters();
  impl->serializer->writeArrayStart("parameters"_zc, params.size());
  for (const auto& param : params) {
    impl->serializer->writeArrayElement();
    param.accept(*this);
  }
  impl->serializer->writeArrayEnd("parameters"_zc);

  ZC_IF_SOME(returnType, node.getReturnType()) {
    impl->serializer->writeChildStart("returnType"_zc);
    returnType.accept(*this);
    impl->serializer->writeChildEnd("returnType"_zc);
  }

  impl->serializer->writeChildStart("body"_zc);
  node.getBody().accept(*this);
  impl->serializer->writeChildEnd("body"_zc);

  impl->serializer->writeNodeEnd("FunctionDeclaration"_zc);
}

void ASTDumper::visit(const BlockStatement& node) {
  impl->serializer->writeNodeStart("BlockStatement"_zc);

  const auto& statements = node.getStatements();
  impl->serializer->writeArrayStart("children"_zc, statements.size());
  for (const auto& stmt : statements) {
    impl->serializer->writeArrayElement();
    stmt.accept(*this);
  }
  impl->serializer->writeArrayEnd("children"_zc);

  impl->serializer->writeNodeEnd("BlockStatement"_zc);
}

void ASTDumper::visit(const ExpressionStatement& node) {
  impl->serializer->writeNodeStart("ExpressionStatement"_zc);

  impl->serializer->writeChildStart("expression"_zc);
  node.getExpression().accept(*this);
  impl->serializer->writeChildEnd("expression"_zc);

  impl->serializer->writeNodeEnd("ExpressionStatement"_zc);
}

void ASTDumper::visit(const EmptyStatement& node) {
  impl->serializer->writeNodeStart("EmptyStatement"_zc);
  impl->serializer->writeNodeEnd("EmptyStatement"_zc);
}

void ASTDumper::visit(const BinaryExpression& node) {
  impl->serializer->writeNodeStart("BinaryExpression"_zc);

  impl->serializer->writeChildStart("left"_zc);
  node.getLeft().accept(*this);
  impl->serializer->writeChildEnd("left"_zc);

  impl->serializer->writeChildStart("operator"_zc);
  node.getOperator().accept(*this);
  impl->serializer->writeChildEnd("operator"_zc);

  impl->serializer->writeChildStart("right"_zc);
  node.getRight().accept(*this);
  impl->serializer->writeChildEnd("right"_zc);

  impl->serializer->writeNodeEnd("BinaryExpression"_zc);
}

void ASTDumper::visit(const StringLiteral& node) {
  impl->serializer->writeNodeStart("StringLiteral"_zc);
  impl->serializer->writeProperty("value", node.getValue());
  impl->serializer->writeNodeEnd("StringLiteral"_zc);
}

void ASTDumper::visit(const IntegerLiteral& node) {
  impl->serializer->writeNodeStart("IntegerLiteral"_zc);
  impl->serializer->writeProperty("value", zc::str(node.getValue()));
  impl->serializer->writeNodeEnd("IntegerLiteral"_zc);
}

void ASTDumper::visit(const FloatLiteral& node) {
  impl->serializer->writeNodeStart("FloatLiteral"_zc);
  impl->serializer->writeProperty("value", zc::str(node.getValue()));
  impl->serializer->writeNodeEnd("FloatLiteral"_zc);
}

void ASTDumper::visit(const BooleanLiteral& node) {
  impl->serializer->writeNodeStart("BooleanLiteral"_zc);
  impl->serializer->writeProperty("value", node.getValue() ? "true" : "false");
  impl->serializer->writeNodeEnd("BooleanLiteral"_zc);
}

void ASTDumper::visit(const NullLiteral& node) {
  impl->serializer->writeNodeStart("NullLiteral"_zc);
  impl->serializer->writeNodeEnd("NullLiteral"_zc);
}

void ASTDumper::visit(const Identifier& node) {
  impl->serializer->writeNodeStart("Identifier"_zc);
  impl->serializer->writeProperty("name", node.getText());
  impl->serializer->writeNodeEnd("Identifier"_zc);
}

void ASTDumper::visit(const ParenthesizedExpression& node) {
  impl->serializer->writeNodeStart("ParenthesizedExpression"_zc);

  impl->serializer->writeChildStart("expression"_zc);
  node.getExpression().accept(*this);
  impl->serializer->writeChildEnd("expression"_zc);

  impl->serializer->writeNodeEnd("ParenthesizedExpression"_zc);
}

void ASTDumper::visit(const Node& node) {
  impl->serializer->writeNodeStart("Node"_zc);
  impl->serializer->writeProperty("kind", zc::str(static_cast<int>(node.getKind())));
  impl->serializer->writeNodeEnd("Node"_zc);
}

void ASTDumper::visit(const Statement& node) {
  impl->serializer->writeNodeStart("Statement"_zc);
  impl->serializer->writeProperty("kind", zc::str(static_cast<int>(node.getKind())));
  impl->serializer->writeNodeEnd("Statement"_zc);
}

void ASTDumper::visit(const Expression& node) {
  impl->serializer->writeNodeStart("Expression"_zc);
  impl->serializer->writeProperty("kind", zc::str(static_cast<int>(node.getKind())));
  impl->serializer->writeNodeEnd("Expression"_zc);
}

void ASTDumper::visit(const BindingElement& node) {
  impl->serializer->writeNodeStart("BindingElement"_zc);

  impl->serializer->writeChildStart("name"_zc);
  node.getName().accept(*this);
  impl->serializer->writeChildEnd("name"_zc);

  ZC_IF_SOME(type, node.getType()) {
    impl->serializer->writeChildStart("varType"_zc);
    type.accept(*this);
    impl->serializer->writeChildEnd("varType"_zc);
  }

  impl->serializer->writeChildStart("initializer"_zc);
  ZC_IF_SOME(init, node.getInitializer()) { init.accept(*this); }
  else { impl->serializer->writeNull(); }
  impl->serializer->writeChildEnd("initializer"_zc);

  impl->serializer->writeNodeEnd("BindingElement"_zc);
}

void ASTDumper::visit(const ModulePath& node) {
  impl->serializer->writeNodeStart("ModulePath"_zc);

  impl->serializer->writeChildStart("stringLiteral"_zc);
  node.getStringLiteral().accept(*this);
  impl->serializer->writeChildEnd("stringLiteral"_zc);

  impl->serializer->writeNodeEnd("ModulePath"_zc);
}

// Additional visit methods for missing AST node types
void ASTDumper::visit(const TypeParameterDeclaration& node) {
  impl->serializer->writeNodeStart("TypeParameterDeclaration"_zc);
  impl->serializer->writeNodeEnd("TypeParameterDeclaration"_zc);
}

void ASTDumper::visit(const ClassDeclaration& node) {
  impl->serializer->writeNodeStart("ClassDeclaration"_zc);
  impl->serializer->writeNodeEnd("ClassDeclaration"_zc);
}

void ASTDumper::visit(const InterfaceDeclaration& node) {
  impl->serializer->writeNodeStart("InterfaceDeclaration"_zc);
  impl->serializer->writeNodeEnd("InterfaceDeclaration"_zc);
}

void ASTDumper::visit(const StructDeclaration& node) {
  impl->serializer->writeNodeStart("StructDeclaration"_zc);
  impl->serializer->writeNodeEnd("StructDeclaration"_zc);
}

void ASTDumper::visit(const EnumDeclaration& node) {
  impl->serializer->writeNodeStart("EnumDeclaration"_zc);
  impl->serializer->writeNodeEnd("EnumDeclaration"_zc);
}

void ASTDumper::visit(const ErrorDeclaration& node) {
  impl->serializer->writeNodeStart("ErrorDeclaration"_zc);
  impl->serializer->writeNodeEnd("ErrorDeclaration"_zc);
}

void ASTDumper::visit(const AliasDeclaration& node) {
  impl->serializer->writeNodeStart("AliasDeclaration"_zc);
  impl->serializer->writeNodeEnd("AliasDeclaration"_zc);
}

void ASTDumper::visit(const IfStatement& node) {
  impl->serializer->writeNodeStart("IfStatement"_zc);
  impl->serializer->writeNodeEnd("IfStatement"_zc);
}

void ASTDumper::visit(const WhileStatement& node) {
  impl->serializer->writeNodeStart("WhileStatement"_zc);
  impl->serializer->writeNodeEnd("WhileStatement"_zc);
}

void ASTDumper::visit(const ForStatement& node) {
  impl->serializer->writeNodeStart("ForStatement"_zc);
  impl->serializer->writeNodeEnd("ForStatement"_zc);
}

void ASTDumper::visit(const BreakStatement& node) {
  impl->serializer->writeNodeStart("BreakStatement"_zc);
  impl->serializer->writeNodeEnd("BreakStatement"_zc);
}

void ASTDumper::visit(const ContinueStatement& node) {
  impl->serializer->writeNodeStart("ContinueStatement"_zc);
  impl->serializer->writeNodeEnd("ContinueStatement"_zc);
}

void ASTDumper::visit(const ReturnStatement& node) {
  impl->serializer->writeNodeStart("ReturnStatement"_zc);
  impl->serializer->writeNodeEnd("ReturnStatement"_zc);
}

void ASTDumper::visit(const MatchStatement& node) {
  impl->serializer->writeNodeStart("MatchStatement"_zc);
  impl->serializer->writeNodeEnd("MatchStatement"_zc);
}

void ASTDumper::visit(const MatchClause& node) {
  impl->serializer->writeNodeStart("MatchClause"_zc);
  impl->serializer->writeNodeEnd("MatchClause"_zc);
}

void ASTDumper::visit(const DefaultClause& node) {
  impl->serializer->writeNodeStart("DefaultClause"_zc);
  impl->serializer->writeNodeEnd("DefaultClause"_zc);
}

void ASTDumper::visit(const DebuggerStatement& node) {
  impl->serializer->writeNodeStart("DebuggerStatement"_zc);
  impl->serializer->writeNodeEnd("DebuggerStatement"_zc);
}

void ASTDumper::visit(const UnaryExpression& node) {
  impl->serializer->writeNodeStart("UnaryExpression"_zc);
  impl->serializer->writeNodeEnd("UnaryExpression"_zc);
}

void ASTDumper::visit(const UpdateExpression& node) {
  impl->serializer->writeNodeStart("UpdateExpression"_zc);
  impl->serializer->writeNodeEnd("UpdateExpression"_zc);
}

void ASTDumper::visit(const PrefixUnaryExpression& node) {
  impl->serializer->writeNodeStart("PrefixUnaryExpression"_zc);

  impl->serializer->writeChildStart("operator"_zc);
  impl->serializer->writeProperty("kind", zc::str(static_cast<int>(node.getOperator())));
  impl->serializer->writeChildEnd("operator"_zc);

  impl->serializer->writeChildStart("operand"_zc);
  node.getOperand().accept(*this);
  impl->serializer->writeChildEnd("operand"_zc);

  impl->serializer->writeNodeEnd("PrefixUnaryExpression"_zc);
}

void ASTDumper::visit(const PostfixUnaryExpression& node) {
  impl->serializer->writeNodeStart("PostfixUnaryExpression"_zc);

  impl->serializer->writeChildStart("operand"_zc);
  node.getOperand().accept(*this);
  impl->serializer->writeChildEnd("operand"_zc);

  impl->serializer->writeChildStart("operator"_zc);
  impl->serializer->writeProperty("kind", zc::str(static_cast<int>(node.getOperator())));
  impl->serializer->writeChildEnd("operator"_zc);

  impl->serializer->writeNodeEnd("PostfixUnaryExpression"_zc);
}

void ASTDumper::visit(const LeftHandSideExpression& node) {
  impl->serializer->writeNodeStart("LeftHandSideExpression"_zc);
  impl->serializer->writeNodeEnd("LeftHandSideExpression"_zc);
}

void ASTDumper::visit(const MemberExpression& node) {
  impl->serializer->writeNodeStart("MemberExpression"_zc);
  impl->serializer->writeNodeEnd("MemberExpression"_zc);
}

void ASTDumper::visit(const PrimaryExpression& node) {
  impl->serializer->writeNodeStart("PrimaryExpression"_zc);
  impl->serializer->writeNodeEnd("PrimaryExpression"_zc);
}

void ASTDumper::visit(const PropertyAccessExpression& node) {
  impl->serializer->writeNodeStart("PropertyAccessExpression"_zc);
  impl->serializer->writeNodeEnd("PropertyAccessExpression"_zc);
}

void ASTDumper::visit(const ElementAccessExpression& node) {
  impl->serializer->writeNodeStart("ElementAccessExpression"_zc);
  impl->serializer->writeNodeEnd("ElementAccessExpression"_zc);
}

void ASTDumper::visit(const NewExpression& node) {
  impl->serializer->writeNodeStart("NewExpression"_zc);
  impl->serializer->writeNodeEnd("NewExpression"_zc);
}

void ASTDumper::visit(const ConditionalExpression& node) {
  impl->serializer->writeNodeStart("ConditionalExpression"_zc);
  impl->serializer->writeNodeEnd("ConditionalExpression"_zc);
}

void ASTDumper::visit(const CallExpression& node) {
  impl->serializer->writeNodeStart("CallExpression"_zc);
  impl->serializer->writeNodeEnd("CallExpression"_zc);
}

void ASTDumper::visit(const LiteralExpression& node) {
  impl->serializer->writeNodeStart("LiteralExpression"_zc);
  impl->serializer->writeNodeEnd("LiteralExpression"_zc);
}

void ASTDumper::visit(const CastExpression& node) {
  impl->serializer->writeNodeStart("CastExpression"_zc);
  impl->serializer->writeNodeEnd("CastExpression"_zc);
}

void ASTDumper::visit(const AsExpression& node) {
  impl->serializer->writeNodeStart("AsExpression"_zc);
  impl->serializer->writeNodeEnd("AsExpression"_zc);
}

void ASTDumper::visit(const ForcedAsExpression& node) {
  impl->serializer->writeNodeStart("ForcedAsExpression"_zc);
  impl->serializer->writeNodeEnd("ForcedAsExpression"_zc);
}

void ASTDumper::visit(const ConditionalAsExpression& node) {
  impl->serializer->writeNodeStart("ConditionalAsExpression"_zc);
  impl->serializer->writeNodeEnd("ConditionalAsExpression"_zc);
}

void ASTDumper::visit(const VoidExpression& node) {
  impl->serializer->writeNodeStart("VoidExpression"_zc);
  impl->serializer->writeNodeEnd("VoidExpression"_zc);
}

void ASTDumper::visit(const TypeOfExpression& node) {
  impl->serializer->writeNodeStart("TypeOfExpression"_zc);
  impl->serializer->writeNodeEnd("TypeOfExpression"_zc);
}

void ASTDumper::visit(const AwaitExpression& node) {
  impl->serializer->writeNodeStart("AwaitExpression"_zc);
  impl->serializer->writeNodeEnd("AwaitExpression"_zc);
}

void ASTDumper::visit(const FunctionExpression& node) {
  impl->serializer->writeNodeStart("FunctionExpression"_zc);

  const auto& typeParams = node.getTypeParameters();
  impl->serializer->writeArrayStart("typeParameters"_zc, typeParams.size());
  for (const auto& param : typeParams) {
    impl->serializer->writeArrayElement();
    param.accept(*this);
  }
  impl->serializer->writeArrayEnd("typeParameters"_zc);

  const auto& params = node.getParameters();
  impl->serializer->writeArrayStart("parameters"_zc, params.size());
  for (const auto& param : params) {
    impl->serializer->writeArrayElement();
    param.accept(*this);
  }
  impl->serializer->writeArrayEnd("parameters"_zc);

  ZC_IF_SOME(returnType, node.getReturnType()) {
    impl->serializer->writeChildStart("returnType"_zc);
    returnType.accept(*this);
    impl->serializer->writeChildEnd("returnType"_zc);
  }

  impl->serializer->writeChildStart("body"_zc);
  node.getBody().accept(*this);
  impl->serializer->writeChildEnd("body"_zc);

  impl->serializer->writeNodeEnd("FunctionExpression"_zc);
}

void ASTDumper::visit(const ArrayLiteralExpression& node) {
  impl->serializer->writeNodeStart("ArrayLiteralExpression"_zc);
  impl->serializer->writeNodeEnd("ArrayLiteralExpression"_zc);
}

void ASTDumper::visit(const ObjectLiteralExpression& node) {
  impl->serializer->writeNodeStart("ObjectLiteralExpression"_zc);
  impl->serializer->writeNodeEnd("ObjectLiteralExpression"_zc);
}

// Type node visit methods
void ASTDumper::visit(const TypeNode& node) {
  impl->serializer->writeNodeStart("TypeNode"_zc);
  impl->serializer->writeNodeEnd("TypeNode"_zc);
}

void ASTDumper::visit(const TokenNode& node) {
  impl->serializer->writeNodeStart("TokenNode"_zc);
  impl->serializer->writeNodeEnd("TokenNode"_zc);
}

void ASTDumper::visit(const TypeReferenceNode& node) {
  impl->serializer->writeNodeStart("TypeReferenceNode"_zc);
  impl->serializer->writeProperty("name", node.getName().getText());
  impl->serializer->writeNodeEnd("TypeReferenceNode"_zc);
}

void ASTDumper::visit(const ArrayTypeNode& node) {
  impl->serializer->writeNodeStart("ArrayTypeNode"_zc);

  impl->serializer->writeChildStart("elementType"_zc);
  node.getElementType().accept(*this);
  impl->serializer->writeChildEnd("elementType"_zc);

  impl->serializer->writeNodeEnd("ArrayTypeNode"_zc);
}

void ASTDumper::visit(const UnionTypeNode& node) {
  impl->serializer->writeNodeStart("UnionTypeNode"_zc);

  const auto& types = node.getTypes();
  impl->serializer->writeArrayStart("types"_zc, types.size());
  for (const auto& type : types) {
    impl->serializer->writeArrayElement();
    type.accept(*this);
  }
  impl->serializer->writeArrayEnd("types"_zc);

  impl->serializer->writeNodeEnd("UnionTypeNode"_zc);
}

void ASTDumper::visit(const IntersectionTypeNode& node) {
  impl->serializer->writeNodeStart("IntersectionTypeNode"_zc);

  const auto& types = node.getTypes();
  impl->serializer->writeArrayStart("types"_zc, types.size());
  for (const auto& type : types) {
    impl->serializer->writeArrayElement();
    type.accept(*this);
  }
  impl->serializer->writeArrayEnd("types"_zc);

  impl->serializer->writeNodeEnd("IntersectionTypeNode"_zc);
}

void ASTDumper::visit(const ParenthesizedTypeNode& node) {
  impl->serializer->writeNodeStart("ParenthesizedTypeNode"_zc);

  impl->serializer->writeChildStart("type"_zc);
  node.getType().accept(*this);
  impl->serializer->writeChildEnd("type"_zc);

  impl->serializer->writeNodeEnd("ParenthesizedTypeNode"_zc);
}

void ASTDumper::visit(const PredefinedTypeNode& node) {
  impl->serializer->writeNodeStart("PredefinedTypeNode"_zc);
  impl->serializer->writeProperty("name", "Predefined");
  impl->serializer->writeNodeEnd("PredefinedTypeNode"_zc);
}

void ASTDumper::visit(const ObjectTypeNode& node) {
  impl->serializer->writeNodeStart("ObjectTypeNode"_zc);

  const auto& members = node.getMembers();
  impl->serializer->writeArrayStart("members"_zc, members.size());
  for (const auto& member : members) {
    impl->serializer->writeArrayElement();
    member.accept(*this);
  }
  impl->serializer->writeArrayEnd("members"_zc);

  impl->serializer->writeNodeEnd("ObjectTypeNode"_zc);
}

void ASTDumper::visit(const TupleTypeNode& node) {
  impl->serializer->writeNodeStart("TupleTypeNode"_zc);

  const auto& elementTypes = node.getElementTypes();
  impl->serializer->writeArrayStart("elementTypes"_zc, elementTypes.size());
  for (const auto& elementType : elementTypes) {
    impl->serializer->writeArrayElement();
    elementType.accept(*this);
  }
  impl->serializer->writeArrayEnd("elementTypes"_zc);

  impl->serializer->writeNodeEnd("TupleTypeNode"_zc);
}

void ASTDumper::visit(const ReturnTypeNode& node) {
  impl->serializer->writeNodeStart("ReturnTypeNode"_zc);

  impl->serializer->writeChildStart("type"_zc);
  node.getType().accept(*this);
  impl->serializer->writeChildEnd("type"_zc);

  ZC_IF_SOME(errorType, node.getErrorType()) {
    impl->serializer->writeChildStart("errorType"_zc);
    errorType.accept(*this);
    impl->serializer->writeChildEnd("errorType"_zc);
  }

  impl->serializer->writeNodeEnd("ReturnTypeNode"_zc);
}

void ASTDumper::visit(const FunctionTypeNode& node) {
  impl->serializer->writeNodeStart("FunctionTypeNode"_zc);

  const auto& typeParams = node.getTypeParameters();
  impl->serializer->writeArrayStart("typeParameters"_zc, typeParams.size());
  for (const auto& param : typeParams) {
    impl->serializer->writeArrayElement();
    param.accept(*this);
  }
  impl->serializer->writeArrayEnd("typeParameters"_zc);

  const auto& params = node.getParameters();
  impl->serializer->writeArrayStart("parameters"_zc, params.size());
  for (const auto& param : params) {
    impl->serializer->writeArrayElement();
    param.accept(*this);
  }
  impl->serializer->writeArrayEnd("parameters"_zc);

  impl->serializer->writeChildStart("returnType"_zc);
  node.getReturnType().accept(*this);
  impl->serializer->writeChildEnd("returnType"_zc);

  impl->serializer->writeNodeEnd("FunctionTypeNode"_zc);
}

void ASTDumper::visit(const OptionalTypeNode& node) {
  impl->serializer->writeNodeStart("OptionalTypeNode"_zc);

  impl->serializer->writeChildStart("type"_zc);
  node.getType().accept(*this);
  impl->serializer->writeChildEnd("type"_zc);

  impl->serializer->writeNodeEnd("OptionalTypeNode"_zc);
}

void ASTDumper::visit(const TypeQueryNode& node) {
  impl->serializer->writeNodeStart("TypeQueryNode"_zc);

  impl->serializer->writeChildStart("expression"_zc);
  node.getExpression().accept(*this);
  impl->serializer->writeChildEnd("expression"_zc);

  impl->serializer->writeNodeEnd("TypeQueryNode"_zc);
}

// Missing declaration visit methods
void ASTDumper::visit(const MethodDeclaration& node) {
  impl->serializer->writeNodeStart("MethodDeclaration"_zc);
  impl->serializer->writeNodeEnd("MethodDeclaration"_zc);
}

void ASTDumper::visit(const ConstructorDeclaration& node) {
  impl->serializer->writeNodeStart("ConstructorDeclaration"_zc);
  impl->serializer->writeNodeEnd("ConstructorDeclaration"_zc);
}

void ASTDumper::visit(const ParameterDeclaration& node) {
  impl->serializer->writeNodeStart("ParameterDeclaration"_zc);
  impl->serializer->writeNodeEnd("ParameterDeclaration"_zc);
}

void ASTDumper::visit(const PropertyDeclaration& node) {
  impl->serializer->writeNodeStart("PropertyDeclaration"_zc);
  impl->serializer->writeNodeEnd("PropertyDeclaration"_zc);
}

void ASTDumper::visit(const MissingDeclaration& node) {
  // TODO: Implement MissingDeclaration dumping
  impl->serializer->writeNodeStart("MissingDeclaration"_zc);
  impl->serializer->writeNodeEnd("MissingDeclaration"_zc);
}

void ASTDumper::visit(const InterfaceBody& node) {
  // TODO: Implement InterfaceBody dumping
  impl->serializer->writeNodeStart("InterfaceBody"_zc);
  impl->serializer->writeNodeEnd("InterfaceBody"_zc);
}

void ASTDumper::visit(const StructBody& node) {
  // TODO: Implement StructBody dumping
  impl->serializer->writeNodeStart("StructBody"_zc);
  impl->serializer->writeNodeEnd("StructBody"_zc);
}

void ASTDumper::visit(const ErrorBody& node) {
  // TODO: Implement ErrorBody dumping
  impl->serializer->writeNodeStart("ErrorBody"_zc);
  impl->serializer->writeNodeEnd("ErrorBody"_zc);
}

void ASTDumper::visit(const EnumBody& node) {
  impl->serializer->writeNodeStart("EnumBody"_zc);
  // TODO: Implement EnumBody dumping
  impl->serializer->writeNodeEnd("EnumBody"_zc);
}

void ASTDumper::visit(const ArrayBindingPattern& node) {
  impl->serializer->writeNodeStart("ArrayBindingPattern"_zc);
  // TODO: Implement ArrayBindingPattern dumping
  impl->serializer->writeNodeEnd("ArrayBindingPattern"_zc);
}

void ASTDumper::visit(const ObjectBindingPattern& node) {
  impl->serializer->writeNodeStart("ObjectBindingPattern"_zc);
  // TODO: Implement ObjectBindingPattern dumping
  impl->serializer->writeNodeEnd("ObjectBindingPattern"_zc);
}

void ASTDumper::visit(const ThisExpression& node) {
  impl->serializer->writeNodeStart("ThisExpression"_zc);
  // TODO: Implement ThisExpression dumping
  impl->serializer->writeNodeEnd("ThisExpression"_zc);
}

void ASTDumper::visit(const SuperExpression& node) {
  impl->serializer->writeNodeStart("SuperExpression"_zc);
  // TODO: Implement SuperExpression dumping
  impl->serializer->writeNodeEnd("SuperExpression"_zc);
}

void ASTDumper::visit(const BoolTypeNode& node) {
  impl->serializer->writeNodeStart("BoolTypeNode"_zc);
  // TODO: Implement BoolTypeNode dumping
  impl->serializer->writeNodeEnd("BoolTypeNode"_zc);
}

void ASTDumper::visit(const I8TypeNode& node) {
  impl->serializer->writeNodeStart("I8TypeNode"_zc);
  // TODO: Implement I8TypeNode dumping
  impl->serializer->writeNodeEnd("I8TypeNode"_zc);
}

void ASTDumper::visit(const I16TypeNode& node) {
  impl->serializer->writeNodeStart("I16TypeNode"_zc);
  // TODO: Implement I16TypeNode dumping
  impl->serializer->writeNodeEnd("I16TypeNode"_zc);
}

void ASTDumper::visit(const I32TypeNode& node) {
  impl->serializer->writeNodeStart("I32TypeNode"_zc);
  // TODO: Implement I32TypeNode dumping
  impl->serializer->writeNodeEnd("I32TypeNode"_zc);
}

void ASTDumper::visit(const I64TypeNode& node) {
  impl->serializer->writeNodeStart("I64TypeNode"_zc);
  // TODO: Implement I64TypeNode dumping
  impl->serializer->writeNodeEnd("I64TypeNode"_zc);
}

void ASTDumper::visit(const U8TypeNode& node) {
  impl->serializer->writeNodeStart("U8TypeNode"_zc);
  // TODO: Implement U8TypeNode dumping
  impl->serializer->writeNodeEnd("U8TypeNode"_zc);
}

void ASTDumper::visit(const U16TypeNode& node) {
  impl->serializer->writeNodeStart("U16TypeNode"_zc);
  // TODO: Implement U16TypeNode dumping
  impl->serializer->writeNodeEnd("U16TypeNode"_zc);
}

void ASTDumper::visit(const U32TypeNode& node) {
  impl->serializer->writeNodeStart("U32TypeNode"_zc);
  // TODO: Implement U32TypeNode dumping
  impl->serializer->writeNodeEnd("U32TypeNode"_zc);
}

void ASTDumper::visit(const U64TypeNode& node) {
  impl->serializer->writeNodeStart("U64TypeNode"_zc);
  // TODO: Implement U64TypeNode dumping
  impl->serializer->writeNodeEnd("U64TypeNode"_zc);
}

void ASTDumper::visit(const F32TypeNode& node) {
  impl->serializer->writeNodeStart("F32TypeNode"_zc);
  // TODO: Implement F32TypeNode dumping
  impl->serializer->writeNodeEnd("F32TypeNode"_zc);
}

void ASTDumper::visit(const F64TypeNode& node) {
  impl->serializer->writeNodeStart("F64TypeNode"_zc);
  // TODO: Implement F64TypeNode dumping
  impl->serializer->writeNodeEnd("F64TypeNode"_zc);
}

void ASTDumper::visit(const StrTypeNode& node) {
  impl->serializer->writeNodeStart("StrTypeNode"_zc);
  // TODO: Implement StrTypeNode dumping
  impl->serializer->writeNodeEnd("StrTypeNode"_zc);
}

void ASTDumper::visit(const UnitTypeNode& node) {
  impl->serializer->writeNodeStart("UnitTypeNode"_zc);
  // TODO: Implement UnitTypeNode dumping
  impl->serializer->writeNodeEnd("UnitTypeNode"_zc);
}

// Signature node visit methods
void ASTDumper::visit(const PropertySignature& node) {
  impl->serializer->writeNodeStart("PropertySignature"_zc);
  // TODO: Implement PropertySignature dumping
  impl->serializer->writeNodeEnd("PropertySignature"_zc);
}

void ASTDumper::visit(const MethodSignature& node) {
  impl->serializer->writeNodeStart("MethodSignature"_zc);
  // TODO: Implement MethodSignature dumping
  impl->serializer->writeNodeEnd("MethodSignature"_zc);
}

// Body/Structure node visit methods
void ASTDumper::visit(const ClassBody& node) {
  writeNodeHeader("ClassBody");
  // TODO: Implement ClassBody dumping
  writeNodeFooter("ClassBody");
}

// Missing visitor method implementations
void ASTDumper::visit(const Module& node) {
  writeNodeHeader("Module");
  // TODO: Implement Module dumping
  writeNodeFooter("Module");
}

void ASTDumper::visit(const VariableDeclaration& node) {
  writeNodeHeader("VariableDeclaration");
  // TODO: Implement VariableDeclaration dumping
  writeNodeFooter("VariableDeclaration");
}

void ASTDumper::visit(const WildcardPattern& node) {
  writeNodeHeader("WildcardPattern");
  // TODO: Implement WildcardPattern dumping
  writeNodeFooter("WildcardPattern");
}

void ASTDumper::visit(const IdentifierPattern& node) {
  writeNodeHeader("IdentifierPattern");
  // TODO: Implement IdentifierPattern dumping
  writeNodeFooter("IdentifierPattern");
}

void ASTDumper::visit(const TuplePattern& node) {
  writeNodeHeader("TuplePattern");
  // TODO: Implement TuplePattern dumping
  writeNodeFooter("TuplePattern");
}

void ASTDumper::visit(const StructurePattern& node) {
  writeNodeHeader("StructurePattern");
  // TODO: Implement StructurePattern dumping
  writeNodeFooter("StructurePattern");
}

void ASTDumper::visit(const ArrayPattern& node) {
  writeNodeHeader("ArrayPattern");
  // TODO: Implement ArrayPattern dumping
  writeNodeFooter("ArrayPattern");
}

void ASTDumper::visit(const IsPattern& node) {
  writeNodeHeader("IsPattern");
  // TODO: Implement IsPattern dumping
  writeNodeFooter("IsPattern");
}

void ASTDumper::visit(const ExpressionPattern& node) {
  writeNodeHeader("ExpressionPattern");
  // TODO: Implement ExpressionPattern dumping
  writeNodeFooter("ExpressionPattern");
}

void ASTDumper::visit(const EnumPattern& node) {
  writeNodeHeader("EnumPattern");
  // TODO: Implement EnumPattern dumping
  writeNodeFooter("EnumPattern");
}

// Private helper methods implementation
void ASTDumper::writeIndent(int indent) {
  // TODO: Implement indentation if needed by serializer
}

void ASTDumper::writeLine(const zc::StringPtr text, int indent) {
  // TODO: Implement line writing if needed by serializer
}

void ASTDumper::writeNodeHeader(const zc::StringPtr nodeType, int indent) {
  impl->serializer->writeNodeStart(nodeType);
}

void ASTDumper::writeNodeFooter(const zc::StringPtr nodeType, int indent) {
  impl->serializer->writeNodeEnd(nodeType);
}

void ASTDumper::writeProperty(const zc::StringPtr name, const zc::StringPtr value, int indent) {
  impl->serializer->writeProperty(name, value);
}

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
