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

  ZC_IF_SOME(alias, node.getAlias()) { impl->serializer->writeProperty("alias", alias); }

  impl->serializer->writeNodeEnd("ImportDeclaration"_zc);
}

void ASTDumper::visit(const ExportDeclaration& node) {
  impl->serializer->writeNodeStart("ExportDeclaration"_zc);

  impl->serializer->writeProperty("identifier", node.getIdentifier());

  if (node.isRename()) {
    ZC_IF_SOME(alias, node.getAlias()) { impl->serializer->writeProperty("alias", alias); }

    ZC_IF_SOME(modulePath, node.getModulePath()) {
      impl->serializer->writeChildStart("modulePath"_zc);
      modulePath.accept(*this);
      impl->serializer->writeChildEnd("modulePath"_zc);
    }
  }

  impl->serializer->writeNodeEnd("ExportDeclaration"_zc);
}

void ASTDumper::visit(const VariableDeclaration& node) {
  impl->serializer->writeNodeStart("VariableDeclaration"_zc);

  const auto& bindings = node.getBindings();
  impl->serializer->writeArrayStart("bindings"_zc, bindings.size());
  for (const auto& binding : bindings) {
    impl->serializer->writeArrayElement();
    binding.accept(*this);
  }
  impl->serializer->writeArrayEnd("bindings"_zc);

  impl->serializer->writeNodeEnd("VariableDeclaration"_zc);
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

  if (auto* returnType = node.getReturnType()) {
    impl->serializer->writeChildStart("returnType"_zc);
    returnType->accept(*this);
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
  impl->serializer->writeProperty("name", node.getName());
  impl->serializer->writeNodeEnd("Identifier"_zc);
}

void ASTDumper::visit(const ParenthesizedExpression& node) {
  impl->serializer->writeNodeStart("ParenthesizedExpression"_zc);

  impl->serializer->writeChildStart("expression"_zc);
  node.getExpression().accept(*this);
  impl->serializer->writeChildEnd("expression"_zc);

  impl->serializer->writeNodeEnd("ParenthesizedExpression"_zc);
}

void ASTDumper::visit(const Operator& node) {
  impl->serializer->writeNodeStart("Operator"_zc);
  impl->serializer->writeProperty("symbol", node.getSymbol());
  impl->serializer->writeProperty("type", zc::str(static_cast<int>(node.getType())));
  impl->serializer->writeNodeEnd("Operator"_zc);
}

void ASTDumper::visit(const BinaryOperator& node) {
  impl->serializer->writeNodeStart("BinaryOperator"_zc);
  impl->serializer->writeProperty("symbol", node.getSymbol());
  impl->serializer->writeProperty("precedence", zc::str(static_cast<int>(node.getPrecedence())));
  impl->serializer->writeNodeEnd("BinaryOperator"_zc);
}

void ASTDumper::visit(const UnaryOperator& node) {
  impl->serializer->writeNodeStart("UnaryOperator"_zc);
  impl->serializer->writeProperty("symbol", node.getSymbol());
  impl->serializer->writeNodeEnd("UnaryOperator"_zc);
}

void ASTDumper::visit(const AssignmentOperator& node) {
  impl->serializer->writeNodeStart("AssignmentOperator"_zc);
  impl->serializer->writeProperty("symbol", node.getSymbol());
  impl->serializer->writeNodeEnd("AssignmentOperator"_zc);
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
  if (auto* initializer = node.getInitializer()) {
    initializer->accept(*this);
  } else {
    impl->serializer->writeNull();
  }
  impl->serializer->writeChildEnd("initializer"_zc);

  impl->serializer->writeNodeEnd("BindingElement"_zc);
}

void ASTDumper::visit(const ModulePath& node) {
  impl->serializer->writeNodeStart("ModulePath"_zc);

  const auto& identifiers = node.getIdentifiers();
  impl->serializer->writeArrayStart("identifiers"_zc, identifiers.size());
  for (const auto& id : identifiers) {
    impl->serializer->writeArrayElement();
    impl->serializer->writeNodeStart("Identifier"_zc);
    impl->serializer->writeProperty("value", id);
    impl->serializer->writeNodeEnd("Identifier"_zc);
  }
  impl->serializer->writeArrayEnd("identifiers"_zc);

  impl->serializer->writeNodeEnd("ModulePath"_zc);
}

// Additional visit methods for missing AST node types
void ASTDumper::visit(const TypeParameter& node) {
  impl->serializer->writeNodeStart("TypeParameter"_zc);
  impl->serializer->writeNodeEnd("TypeParameter"_zc);
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
  node.getOperator().accept(*this);
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
  node.getOperator().accept(*this);
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

void ASTDumper::visit(const AssignmentExpression& node) {
  impl->serializer->writeNodeStart("AssignmentExpression"_zc);
  impl->serializer->writeNodeEnd("AssignmentExpression"_zc);
}

void ASTDumper::visit(const ConditionalExpression& node) {
  impl->serializer->writeNodeStart("ConditionalExpression"_zc);
  impl->serializer->writeNodeEnd("ConditionalExpression"_zc);
}

void ASTDumper::visit(const CallExpression& node) {
  impl->serializer->writeNodeStart("CallExpression"_zc);
  impl->serializer->writeNodeEnd("CallExpression"_zc);
}

void ASTDumper::visit(const OptionalExpression& node) {
  impl->serializer->writeNodeStart("OptionalExpression"_zc);
  impl->serializer->writeNodeEnd("OptionalExpression"_zc);
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
void ASTDumper::visit(const Type& node) {
  impl->serializer->writeNodeStart("Type"_zc);
  impl->serializer->writeNodeEnd("Type"_zc);
}

void ASTDumper::visit(const TypeReference& node) {
  impl->serializer->writeNodeStart("TypeReference"_zc);
  impl->serializer->writeProperty("name", node.getName());
  impl->serializer->writeNodeEnd("TypeReference"_zc);
}

void ASTDumper::visit(const ArrayType& node) {
  impl->serializer->writeNodeStart("ArrayType"_zc);

  impl->serializer->writeChildStart("elementType"_zc);
  node.getElementType().accept(*this);
  impl->serializer->writeChildEnd("elementType"_zc);

  impl->serializer->writeNodeEnd("ArrayType"_zc);
}

void ASTDumper::visit(const UnionType& node) {
  impl->serializer->writeNodeStart("UnionType"_zc);

  const auto& types = node.getTypes();
  impl->serializer->writeArrayStart("types"_zc, types.size());
  for (const auto& type : types) {
    impl->serializer->writeArrayElement();
    type.accept(*this);
  }
  impl->serializer->writeArrayEnd("types"_zc);

  impl->serializer->writeNodeEnd("UnionType"_zc);
}

void ASTDumper::visit(const IntersectionType& node) {
  impl->serializer->writeNodeStart("IntersectionType"_zc);

  const auto& types = node.getTypes();
  impl->serializer->writeArrayStart("types"_zc, types.size());
  for (const auto& type : types) {
    impl->serializer->writeArrayElement();
    type.accept(*this);
  }
  impl->serializer->writeArrayEnd("types"_zc);

  impl->serializer->writeNodeEnd("IntersectionType"_zc);
}

void ASTDumper::visit(const ParenthesizedType& node) {
  impl->serializer->writeNodeStart("ParenthesizedType"_zc);

  impl->serializer->writeChildStart("type"_zc);
  node.getType().accept(*this);
  impl->serializer->writeChildEnd("type"_zc);

  impl->serializer->writeNodeEnd("ParenthesizedType"_zc);
}

void ASTDumper::visit(const PredefinedType& node) {
  impl->serializer->writeNodeStart("PredefinedType"_zc);
  impl->serializer->writeProperty("name", node.getName());
  impl->serializer->writeNodeEnd("PredefinedType"_zc);
}

void ASTDumper::visit(const ObjectType& node) {
  impl->serializer->writeNodeStart("ObjectType"_zc);

  const auto& members = node.getMembers();
  impl->serializer->writeArrayStart("members"_zc, members.size());
  for (const auto& member : members) {
    impl->serializer->writeArrayElement();
    member.accept(*this);
  }
  impl->serializer->writeArrayEnd("members"_zc);

  impl->serializer->writeNodeEnd("ObjectType"_zc);
}

void ASTDumper::visit(const TupleType& node) {
  impl->serializer->writeNodeStart("TupleType"_zc);

  const auto& elementTypes = node.getElementTypes();
  impl->serializer->writeArrayStart("elementTypes"_zc, elementTypes.size());
  for (const auto& elementType : elementTypes) {
    impl->serializer->writeArrayElement();
    elementType.accept(*this);
  }
  impl->serializer->writeArrayEnd("elementTypes"_zc);

  impl->serializer->writeNodeEnd("TupleType"_zc);
}

void ASTDumper::visit(const ReturnType& node) {
  impl->serializer->writeNodeStart("ReturnType"_zc);

  impl->serializer->writeChildStart("type"_zc);
  node.getType().accept(*this);
  impl->serializer->writeChildEnd("type"_zc);

  ZC_IF_SOME(errorType, node.getErrorType()) {
    impl->serializer->writeChildStart("errorType"_zc);
    errorType.accept(*this);
    impl->serializer->writeChildEnd("errorType"_zc);
  }

  impl->serializer->writeNodeEnd("ReturnType"_zc);
}

void ASTDumper::visit(const FunctionType& node) {
  impl->serializer->writeNodeStart("FunctionType"_zc);

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

  impl->serializer->writeNodeEnd("FunctionType"_zc);
}

void ASTDumper::visit(const OptionalType& node) {
  impl->serializer->writeNodeStart("OptionalType"_zc);

  impl->serializer->writeChildStart("type"_zc);
  node.getType().accept(*this);
  impl->serializer->writeChildEnd("type"_zc);

  impl->serializer->writeNodeEnd("OptionalType"_zc);
}

void ASTDumper::visit(const TypeQuery& node) {
  impl->serializer->writeNodeStart("TypeQuery"_zc);

  impl->serializer->writeChildStart("expression"_zc);
  node.getExpression().accept(*this);
  impl->serializer->writeChildEnd("expression"_zc);

  impl->serializer->writeNodeEnd("TypeQuery"_zc);
}

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
