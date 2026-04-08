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
#include "zomlang/compiler/lexer/token.h"

namespace zomlang {
namespace compiler {
namespace ast {

namespace {
void dumpName(ASTDumper& dumper,
              zc::OneOf<zc::Maybe<const Identifier&>, zc::Maybe<const BindingPattern&>> name) {
  ZC_SWITCH_ONEOF(name) {
    ZC_CASE_ONEOF(maybeId, zc::Maybe<const Identifier&>) {
      ZC_IF_SOME(id, maybeId) { id.accept(dumper); }
    }
    ZC_CASE_ONEOF(maybePattern, zc::Maybe<const BindingPattern&>) {
      ZC_IF_SOME(pattern, maybePattern) { pattern.accept(dumper); }
    }
  }
}

void dumpName(ASTDumper& dumper, const Identifier& name) { name.accept(dumper); }
}  // namespace

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

  ZC_IF_SOME(moduleDeclaration, node.getModuleDeclaration()) {
    impl->serializer->writeChildStart("moduleDeclaration"_zc);
    moduleDeclaration.accept(*this);
    impl->serializer->writeChildEnd("moduleDeclaration"_zc);
  }

  const auto& statements = node.getStatements();
  impl->serializer->writeArrayStart("statements"_zc, statements.size());
  for (const auto& stmt : statements) {
    impl->serializer->writeArrayElement();
    stmt.accept(*this);
  }
  impl->serializer->writeArrayEnd("statements"_zc);

  impl->serializer->writeNodeEnd("SourceFile"_zc);
}

void ASTDumper::visit(const ModuleDeclaration& node) {
  impl->serializer->writeNodeStart("ModuleDeclaration"_zc);

  impl->serializer->writeChildStart("modulePath"_zc);
  node.getModulePath().accept(*this);
  impl->serializer->writeChildEnd("modulePath"_zc);

  impl->serializer->writeNodeEnd("ModuleDeclaration"_zc);
}

void ASTDumper::visit(const ImportDeclaration& node) {
  impl->serializer->writeNodeStart("ImportDeclaration"_zc);

  impl->serializer->writeChildStart("modulePath"_zc);
  node.getModulePath().accept(*this);
  impl->serializer->writeChildEnd("modulePath"_zc);

  ZC_IF_SOME(alias, node.getAlias()) { impl->serializer->writeProperty("alias", alias.getText()); }

  const auto& specifiers = node.getSpecifiers();
  impl->serializer->writeArrayStart("specifiers"_zc, specifiers.size());
  for (const auto& specifier : specifiers) {
    impl->serializer->writeArrayElement();
    specifier.accept(*this);
  }
  impl->serializer->writeArrayEnd("specifiers"_zc);

  impl->serializer->writeNodeEnd("ImportDeclaration"_zc);
}

void ASTDumper::visit(const ImportSpecifier& node) {
  impl->serializer->writeNodeStart("ImportSpecifier"_zc);

  impl->serializer->writeChildStart("importedName"_zc);
  node.getImportedName().accept(*this);
  impl->serializer->writeChildEnd("importedName"_zc);

  impl->serializer->writeChildStart("alias"_zc);
  ZC_IF_SOME(alias, node.getAlias()) { alias.accept(*this); }
  else { impl->serializer->writeNull(); }
  impl->serializer->writeChildEnd("alias"_zc);

  impl->serializer->writeNodeEnd("ImportSpecifier"_zc);
}

void ASTDumper::visit(const ExportDeclaration& node) {
  impl->serializer->writeNodeStart("ExportDeclaration"_zc);

  impl->serializer->writeChildStart("modulePath"_zc);
  ZC_IF_SOME(modulePath, node.getModulePath()) { modulePath.accept(*this); }
  else { impl->serializer->writeNull(); }
  impl->serializer->writeChildEnd("modulePath"_zc);

  const auto& specifiers = node.getSpecifiers();
  impl->serializer->writeArrayStart("specifiers"_zc, specifiers.size());
  for (const auto& specifier : specifiers) {
    impl->serializer->writeArrayElement();
    specifier.accept(*this);
  }
  impl->serializer->writeArrayEnd("specifiers"_zc);

  ZC_IF_SOME(declaration, node.getDeclaration()) {
    impl->serializer->writeChildStart("declaration"_zc);
    declaration.accept(*this);
    impl->serializer->writeChildEnd("declaration"_zc);
  }

  impl->serializer->writeNodeEnd("ExportDeclaration"_zc);
}

void ASTDumper::visit(const ExportSpecifier& node) {
  impl->serializer->writeNodeStart("ExportSpecifier"_zc);

  impl->serializer->writeChildStart("exportedName"_zc);
  node.getExportedName().accept(*this);
  impl->serializer->writeChildEnd("exportedName"_zc);

  impl->serializer->writeChildStart("alias"_zc);
  ZC_IF_SOME(alias, node.getAlias()) { alias.accept(*this); }
  else { impl->serializer->writeNull(); }
  impl->serializer->writeChildEnd("alias"_zc);

  impl->serializer->writeNodeEnd("ExportSpecifier"_zc);
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
  dumpName(*this, node.getName());
  impl->serializer->writeChildEnd("name"_zc);

  const auto& typeParameters = node.getTypeParameters();
  if (typeParameters.size() > 0) {
    impl->serializer->writeArrayStart("typeParameters"_zc, typeParameters.size());
    for (const auto& param : typeParameters) {
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

void ASTDumper::visit(const TemplateLiteralExpression& node) {
  impl->serializer->writeNodeStart("TemplateLiteralExpression"_zc);

  impl->serializer->writeChildStart("head"_zc);
  node.getHead().accept(*this);
  impl->serializer->writeChildEnd("head"_zc);

  const auto& spans = node.getSpans();
  impl->serializer->writeArrayStart("spans"_zc, spans.size());
  for (const auto& span : spans) {
    impl->serializer->writeArrayElement();
    span.accept(*this);
  }
  impl->serializer->writeArrayEnd("spans"_zc);

  impl->serializer->writeNodeEnd("TemplateLiteralExpression"_zc);
}

void ASTDumper::visit(const TemplateSpan& node) {
  impl->serializer->writeNodeStart("TemplateSpan"_zc);

  impl->serializer->writeChildStart("expression"_zc);
  node.getExpression().accept(*this);
  impl->serializer->writeChildEnd("expression"_zc);

  impl->serializer->writeChildStart("literal"_zc);
  node.getLiteral().accept(*this);
  impl->serializer->writeChildEnd("literal"_zc);

  impl->serializer->writeNodeEnd("TemplateSpan"_zc);
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

void ASTDumper::visit(const BigIntLiteral& node) {
  impl->serializer->writeNodeStart("BigIntLiteral"_zc);
  impl->serializer->writeProperty("text"_zc, node.getText());
  impl->serializer->writeNodeEnd("BigIntLiteral"_zc);
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

void ASTDumper::visit(const PatternProperty& node) {
  impl->serializer->writeNodeStart("PatternProperty"_zc);
  impl->serializer->writeProperty("name", node.getName().getText());
  ZC_IF_SOME(pattern, node.getPattern()) {
    impl->serializer->writeChildStart("pattern"_zc);
    pattern.accept(*this);
    impl->serializer->writeChildEnd("pattern"_zc);
  }
  impl->serializer->writeNodeEnd("PatternProperty"_zc);
}

void ASTDumper::visit(const ParenthesizedExpression& node) {
  impl->serializer->writeNodeStart("ParenthesizedExpression"_zc);

  impl->serializer->writeChildStart("expression"_zc);
  node.getExpression().accept(*this);
  impl->serializer->writeChildEnd("expression"_zc);

  impl->serializer->writeNodeEnd("ParenthesizedExpression"_zc);
}

void ASTDumper::visit(const Node& node) { node.accept(*this); }

void ASTDumper::visit(const Statement& node) { node.accept(*this); }

void ASTDumper::visit(const IterationStatement& node) { node.accept(*this); }

void ASTDumper::visit(const DeclarationStatement& node) {
  static_cast<const Statement&>(node).accept(*this);
}

void ASTDumper::visit(const Expression& node) { node.accept(*this); }

void ASTDumper::visit(const BindingElement& node) {
  impl->serializer->writeNodeStart("BindingElement"_zc);

  impl->serializer->writeChildStart("name"_zc);
  dumpName(*this, node.getName());
  impl->serializer->writeChildEnd("name"_zc);

  ZC_IF_SOME(pattern, node.getBindingPattern()) {
    impl->serializer->writeChildStart("bindingPattern"_zc);
    pattern.accept(*this);
    impl->serializer->writeChildEnd("bindingPattern"_zc);
  }

  impl->serializer->writeChildStart("initializer"_zc);
  ZC_IF_SOME(init, node.getInitializer()) { init.accept(*this); }
  else { impl->serializer->writeNull(); }
  impl->serializer->writeChildEnd("initializer"_zc);

  impl->serializer->writeNodeEnd("BindingElement"_zc);
}
void ASTDumper::visit(const ClassElement& node) { node.accept(*this); }

void ASTDumper::visit(const InterfaceElement& node) { node.accept(*this); }

void ASTDumper::visit(const ModulePath& node) {
  impl->serializer->writeNodeStart("ModulePath"_zc);

  const auto& segments = node.getSegments();
  impl->serializer->writeArrayStart("segments"_zc, segments.size());
  for (const auto& segment : segments) {
    impl->serializer->writeArrayElement();
    segment.accept(*this);
  }
  impl->serializer->writeArrayEnd("segments"_zc);

  impl->serializer->writeNodeEnd("ModulePath"_zc);
}

// Additional visit methods for missing AST node types
void ASTDumper::visit(const TypeParameterDeclaration& node) {
  impl->serializer->writeNodeStart("TypeParameterDeclaration"_zc);

  impl->serializer->writeChildStart("name"_zc);
  dumpName(*this, node.getName());
  impl->serializer->writeChildEnd("name"_zc);

  impl->serializer->writeChildStart("constraint"_zc);
  ZC_IF_SOME(constraint, node.getConstraint()) { constraint.accept(*this); }
  else { impl->serializer->writeNull(); }
  impl->serializer->writeChildEnd("constraint"_zc);

  impl->serializer->writeNodeEnd("TypeParameterDeclaration"_zc);
}

void ASTDumper::visit(const ClassDeclaration& node) {
  impl->serializer->writeNodeStart("ClassDeclaration"_zc);

  impl->serializer->writeChildStart("name"_zc);
  dumpName(*this, node.getName());
  impl->serializer->writeChildEnd("name"_zc);

  const auto& typeParameters = node.getTypeParameters();
  if (!typeParameters.empty()) {
    impl->serializer->writeArrayStart("typeParameters"_zc, typeParameters.size());
    for (const auto& param : typeParameters) {
      impl->serializer->writeArrayElement();
      param.accept(*this);
    }
    impl->serializer->writeArrayEnd("typeParameters"_zc);
  }

  const auto& heritageClauses = node.getHeritageClauses();
  if (!heritageClauses.empty()) {
    impl->serializer->writeArrayStart("heritageClauses"_zc, heritageClauses.size());
    for (const auto& clause : heritageClauses) {
      impl->serializer->writeArrayElement();
      clause.accept(*this);
    }
    impl->serializer->writeArrayEnd("heritageClauses"_zc);
  }

  const auto& members = node.getMembers();
  impl->serializer->writeArrayStart("members"_zc, members.size());
  for (const auto& member : members) {
    impl->serializer->writeArrayElement();
    member.accept(*this);
  }
  impl->serializer->writeArrayEnd("members"_zc);

  impl->serializer->writeNodeEnd("ClassDeclaration"_zc);
}

void ASTDumper::visit(const InterfaceDeclaration& node) {
  impl->serializer->writeNodeStart("InterfaceDeclaration"_zc);

  impl->serializer->writeChildStart("name"_zc);
  dumpName(*this, node.getName());
  impl->serializer->writeChildEnd("name"_zc);

  const auto& typeParameters = node.getTypeParameters();
  if (!typeParameters.empty()) {
    impl->serializer->writeArrayStart("typeParameters"_zc, typeParameters.size());
    for (const auto& param : typeParameters) {
      impl->serializer->writeArrayElement();
      param.accept(*this);
    }
    impl->serializer->writeArrayEnd("typeParameters"_zc);
  }

  const auto& heritageClauses = node.getHeritageClauses();
  if (!heritageClauses.empty()) {
    impl->serializer->writeArrayStart("heritageClauses"_zc, heritageClauses.size());
    for (const auto& clause : heritageClauses) {
      impl->serializer->writeArrayElement();
      clause.accept(*this);
    }
    impl->serializer->writeArrayEnd("heritageClauses"_zc);
  }

  const auto& members = node.getMembers();
  impl->serializer->writeArrayStart("members"_zc, members.size());
  for (const auto& member : members) {
    impl->serializer->writeArrayElement();
    member.accept(*this);
  }
  impl->serializer->writeArrayEnd("members"_zc);

  impl->serializer->writeNodeEnd("InterfaceDeclaration"_zc);
}

void ASTDumper::visit(const StructDeclaration& node) {
  impl->serializer->writeNodeStart("StructDeclaration"_zc);

  impl->serializer->writeChildStart("name"_zc);
  dumpName(*this, node.getName());
  impl->serializer->writeChildEnd("name"_zc);

  const auto& typeParameters = node.getTypeParameters();
  if (!typeParameters.empty()) {
    impl->serializer->writeArrayStart("typeParameters"_zc, typeParameters.size());
    for (const auto& param : typeParameters) {
      impl->serializer->writeArrayElement();
      param.accept(*this);
    }
    impl->serializer->writeArrayEnd("typeParameters"_zc);
  }

  const auto& heritageClauses = node.getHeritageClauses();
  if (!heritageClauses.empty()) {
    impl->serializer->writeArrayStart("heritageClauses"_zc, heritageClauses.size());
    for (const auto& clause : heritageClauses) {
      impl->serializer->writeArrayElement();
      clause.accept(*this);
    }
    impl->serializer->writeArrayEnd("heritageClauses"_zc);
  }

  const auto& members = node.getMembers();
  impl->serializer->writeArrayStart("members"_zc, members.size());
  for (const auto& member : members) {
    impl->serializer->writeArrayElement();
    member.accept(*this);
  }
  impl->serializer->writeArrayEnd("members"_zc);

  impl->serializer->writeNodeEnd("StructDeclaration"_zc);
}

void ASTDumper::visit(const EnumMember& node) {
  impl->serializer->writeNodeStart("EnumMember"_zc);

  impl->serializer->writeChildStart("name"_zc);
  dumpName(*this, node.getName());
  impl->serializer->writeChildEnd("name"_zc);

  impl->serializer->writeChildStart("initializer"_zc);
  ZC_IF_SOME(initializer, node.getInitializer()) { initializer.accept(*this); }
  else { impl->serializer->writeNull(); }
  impl->serializer->writeChildEnd("initializer"_zc);

  impl->serializer->writeChildStart("tupleType"_zc);
  ZC_IF_SOME(tupleType, node.getTupleType()) { tupleType.accept(*this); }
  else { impl->serializer->writeNull(); }
  impl->serializer->writeChildEnd("tupleType"_zc);

  impl->serializer->writeNodeEnd("EnumMember"_zc);
}

void ASTDumper::visit(const EnumDeclaration& node) {
  impl->serializer->writeNodeStart("EnumDeclaration"_zc);

  impl->serializer->writeChildStart("name"_zc);
  dumpName(*this, node.getName());
  impl->serializer->writeChildEnd("name"_zc);

  const auto& members = node.getMembers();
  impl->serializer->writeArrayStart("members"_zc, members.size());
  for (const auto& member : members) {
    impl->serializer->writeArrayElement();
    member.accept(*this);
  }
  impl->serializer->writeArrayEnd("members"_zc);

  impl->serializer->writeNodeEnd("EnumDeclaration"_zc);
}

void ASTDumper::visit(const ErrorDeclaration& node) {
  impl->serializer->writeNodeStart("ErrorDeclaration"_zc);

  impl->serializer->writeChildStart("name"_zc);
  dumpName(*this, node.getName());
  impl->serializer->writeChildEnd("name"_zc);

  const auto& members = node.getMembers();
  impl->serializer->writeArrayStart("members"_zc, members.size());
  for (const auto& member : members) {
    impl->serializer->writeArrayElement();
    member.accept(*this);
  }
  impl->serializer->writeArrayEnd("members"_zc);

  impl->serializer->writeNodeEnd("ErrorDeclaration"_zc);
}

void ASTDumper::visit(const AliasDeclaration& node) {
  impl->serializer->writeNodeStart("AliasDeclaration"_zc);

  impl->serializer->writeChildStart("name"_zc);
  dumpName(*this, node.getName());
  impl->serializer->writeChildEnd("name"_zc);

  const auto& typeParameters = node.getTypeParameters();
  impl->serializer->writeArrayStart("typeParameters"_zc, typeParameters.size());
  for (const auto& param : typeParameters) {
    impl->serializer->writeArrayElement();
    param.accept(*this);
  }
  impl->serializer->writeArrayEnd("typeParameters"_zc);

  impl->serializer->writeChildStart("type"_zc);
  node.getType().accept(*this);
  impl->serializer->writeChildEnd("type"_zc);

  impl->serializer->writeNodeEnd("AliasDeclaration"_zc);
}

void ASTDumper::visit(const IfStatement& node) {
  impl->serializer->writeNodeStart("IfStatement"_zc);

  impl->serializer->writeChildStart("condition"_zc);
  node.getCondition().accept(*this);
  impl->serializer->writeChildEnd("condition"_zc);

  impl->serializer->writeChildStart("thenStatement"_zc);
  node.getThenStatement().accept(*this);
  impl->serializer->writeChildEnd("thenStatement"_zc);

  impl->serializer->writeChildStart("elseStatement"_zc);
  ZC_IF_SOME(elseStatement, node.getElseStatement()) { elseStatement.accept(*this); }
  else { impl->serializer->writeNull(); }
  impl->serializer->writeChildEnd("elseStatement"_zc);

  impl->serializer->writeNodeEnd("IfStatement"_zc);
}

void ASTDumper::visit(const WhileStatement& node) {
  impl->serializer->writeNodeStart("WhileStatement"_zc);

  impl->serializer->writeChildStart("condition"_zc);
  node.getCondition().accept(*this);
  impl->serializer->writeChildEnd("condition"_zc);

  impl->serializer->writeChildStart("body"_zc);
  node.getBody().accept(*this);
  impl->serializer->writeChildEnd("body"_zc);

  impl->serializer->writeNodeEnd("WhileStatement"_zc);
}

void ASTDumper::visit(const ForStatement& node) {
  impl->serializer->writeNodeStart("ForStatement"_zc);

  impl->serializer->writeChildStart("initializer"_zc);
  ZC_IF_SOME(init, node.getInitializer()) { init.accept(*this); }
  else { impl->serializer->writeNull(); }
  impl->serializer->writeChildEnd("initializer"_zc);

  impl->serializer->writeChildStart("condition"_zc);
  ZC_IF_SOME(cond, node.getCondition()) { cond.accept(*this); }
  else { impl->serializer->writeNull(); }
  impl->serializer->writeChildEnd("condition"_zc);

  impl->serializer->writeChildStart("update"_zc);
  ZC_IF_SOME(upd, node.getUpdate()) { upd.accept(*this); }
  else { impl->serializer->writeNull(); }
  impl->serializer->writeChildEnd("update"_zc);

  impl->serializer->writeChildStart("body"_zc);
  node.getBody().accept(*this);
  impl->serializer->writeChildEnd("body"_zc);

  impl->serializer->writeNodeEnd("ForStatement"_zc);
}

void ASTDumper::visit(const ForInStatement& node) {
  impl->serializer->writeNodeStart("ForInStatement"_zc);

  impl->serializer->writeChildStart("initializer"_zc);
  node.getInitializer().accept(*this);
  impl->serializer->writeChildEnd("initializer"_zc);

  impl->serializer->writeChildStart("expression"_zc);
  node.getExpression().accept(*this);
  impl->serializer->writeChildEnd("expression"_zc);

  impl->serializer->writeChildStart("body"_zc);
  node.getBody().accept(*this);
  impl->serializer->writeChildEnd("body"_zc);

  impl->serializer->writeNodeEnd("ForInStatement"_zc);
}

void ASTDumper::visit(const LabeledStatement& node) {
  impl->serializer->writeNodeStart("LabeledStatement"_zc);

  impl->serializer->writeChildStart("label"_zc);
  node.getLabel().accept(*this);
  impl->serializer->writeChildEnd("label"_zc);

  impl->serializer->writeChildStart("statement"_zc);
  node.getStatement().accept(*this);
  impl->serializer->writeChildEnd("statement"_zc);

  impl->serializer->writeNodeEnd("LabeledStatement"_zc);
}

void ASTDumper::visit(const BreakStatement& node) {
  impl->serializer->writeNodeStart("BreakStatement"_zc);

  impl->serializer->writeChildStart("label"_zc);
  ZC_IF_SOME(label, node.getLabel()) { label.accept(*this); }
  else { impl->serializer->writeNull(); }
  impl->serializer->writeChildEnd("label"_zc);

  impl->serializer->writeNodeEnd("BreakStatement"_zc);
}

void ASTDumper::visit(const ContinueStatement& node) {
  impl->serializer->writeNodeStart("ContinueStatement"_zc);

  impl->serializer->writeChildStart("label"_zc);
  ZC_IF_SOME(label, node.getLabel()) { label.accept(*this); }
  else { impl->serializer->writeNull(); }
  impl->serializer->writeChildEnd("label"_zc);

  impl->serializer->writeNodeEnd("ContinueStatement"_zc);
}

void ASTDumper::visit(const ReturnStatement& node) {
  impl->serializer->writeNodeStart("ReturnStatement"_zc);

  impl->serializer->writeChildStart("expression"_zc);
  ZC_IF_SOME(expr, node.getExpression()) { expr.accept(*this); }
  else { impl->serializer->writeNull(); }
  impl->serializer->writeChildEnd("expression"_zc);

  impl->serializer->writeNodeEnd("ReturnStatement"_zc);
}

void ASTDumper::visit(const MatchStatement& node) {
  impl->serializer->writeNodeStart("MatchStatement"_zc);

  impl->serializer->writeChildStart("discriminant"_zc);
  node.getDiscriminant().accept(*this);
  impl->serializer->writeChildEnd("discriminant"_zc);

  const auto& clauses = node.getClauses();
  impl->serializer->writeArrayStart("clauses"_zc, clauses.size());
  for (const auto& clause : clauses) {
    impl->serializer->writeArrayElement();
    clause.accept(*this);
  }
  impl->serializer->writeArrayEnd("clauses"_zc);

  impl->serializer->writeNodeEnd("MatchStatement"_zc);
}

void ASTDumper::visit(const MatchClause& node) {
  impl->serializer->writeNodeStart("MatchClause"_zc);

  impl->serializer->writeChildStart("pattern"_zc);
  node.getPattern().accept(*this);
  impl->serializer->writeChildEnd("pattern"_zc);

  impl->serializer->writeChildStart("guard"_zc);
  ZC_IF_SOME(guard, node.getGuard()) { guard.accept(*this); }
  else { impl->serializer->writeNull(); }
  impl->serializer->writeChildEnd("guard"_zc);

  impl->serializer->writeChildStart("body"_zc);
  node.getBody().accept(*this);
  impl->serializer->writeChildEnd("body"_zc);

  impl->serializer->writeNodeEnd("MatchClause"_zc);
}

void ASTDumper::visit(const DefaultClause& node) {
  impl->serializer->writeNodeStart("DefaultClause"_zc);

  const auto& statements = node.getStatements();
  impl->serializer->writeArrayStart("statements"_zc, statements.size());
  for (const auto& stmt : statements) {
    impl->serializer->writeArrayElement();
    stmt.accept(*this);
  }
  impl->serializer->writeArrayEnd("statements"_zc);

  impl->serializer->writeNodeEnd("DefaultClause"_zc);
}

void ASTDumper::visit(const DebuggerStatement& node) {
  impl->serializer->writeNodeStart("DebuggerStatement"_zc);
  impl->serializer->writeNodeEnd("DebuggerStatement"_zc);
}

void ASTDumper::visit(const UnaryExpression& node) { node.accept(*this); }

void ASTDumper::visit(const UpdateExpression& node) { node.accept(*this); }

void ASTDumper::visit(const PrefixUnaryExpression& node) {
  impl->serializer->writeNodeStart("PrefixUnaryExpression"_zc);

  impl->serializer->writeChildStart("operator"_zc);
  impl->serializer->writeNodeStart("TokenNode"_zc);
  ZC_IF_SOME(operatorText, lexer::Token::getStaticTextForTokenKind(node.getOperator())) {
    impl->serializer->writeProperty("symbol", operatorText);
  }
  impl->serializer->writeNodeEnd("TokenNode"_zc);
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
  impl->serializer->writeNodeStart("TokenNode"_zc);
  ZC_IF_SOME(operatorText, lexer::Token::getStaticTextForTokenKind(node.getOperator())) {
    impl->serializer->writeProperty("symbol", operatorText);
  }
  impl->serializer->writeNodeEnd("TokenNode"_zc);
  impl->serializer->writeChildEnd("operator"_zc);

  impl->serializer->writeNodeEnd("PostfixUnaryExpression"_zc);
}

void ASTDumper::visit(const LeftHandSideExpression& node) { node.accept(*this); }

void ASTDumper::visit(const MemberExpression& node) { node.accept(*this); }

void ASTDumper::visit(const PrimaryExpression& node) { node.accept(*this); }

void ASTDumper::visit(const PropertyAccessExpression& node) {
  impl->serializer->writeNodeStart("PropertyAccessExpression"_zc);

  impl->serializer->writeChildStart("expression"_zc);
  node.getExpression().accept(*this);
  impl->serializer->writeChildEnd("expression"_zc);

  impl->serializer->writeChildStart("name"_zc);
  dumpName(*this, node.getName());
  impl->serializer->writeChildEnd("name"_zc);

  impl->serializer->writeProperty("questionDot"_zc, node.isQuestionDot() ? "true"_zc : "false"_zc);

  impl->serializer->writeNodeEnd("PropertyAccessExpression"_zc);
}

void ASTDumper::visit(const ElementAccessExpression& node) {
  impl->serializer->writeNodeStart("ElementAccessExpression"_zc);

  impl->serializer->writeChildStart("expression"_zc);
  node.getExpression().accept(*this);
  impl->serializer->writeChildEnd("expression"_zc);

  impl->serializer->writeChildStart("index"_zc);
  node.getIndex().accept(*this);
  impl->serializer->writeChildEnd("index"_zc);

  impl->serializer->writeProperty("questionDot"_zc, node.isQuestionDot() ? "true"_zc : "false"_zc);

  impl->serializer->writeNodeEnd("ElementAccessExpression"_zc);
}

void ASTDumper::visit(const NewExpression& node) {
  impl->serializer->writeNodeStart("NewExpression"_zc);

  impl->serializer->writeChildStart("callee"_zc);
  node.getCallee().accept(*this);
  impl->serializer->writeChildEnd("callee"_zc);

  ZC_IF_SOME(args, node.getArguments()) {
    impl->serializer->writeArrayStart("arguments"_zc, args.size());
    for (const auto& arg : args) {
      impl->serializer->writeArrayElement();
      arg->accept(*this);
    }
    impl->serializer->writeArrayEnd("arguments"_zc);
  }
  else {
    impl->serializer->writeChildStart("arguments"_zc);
    impl->serializer->writeNull();
    impl->serializer->writeChildEnd("arguments"_zc);
  }

  impl->serializer->writeNodeEnd("NewExpression"_zc);
}

void ASTDumper::visit(const ConditionalExpression& node) {
  impl->serializer->writeNodeStart("ConditionalExpression"_zc);

  impl->serializer->writeChildStart("test"_zc);
  node.getTest().accept(*this);
  impl->serializer->writeChildEnd("test"_zc);

  impl->serializer->writeChildStart("consequent"_zc);
  node.getConsequent().accept(*this);
  impl->serializer->writeChildEnd("consequent"_zc);

  impl->serializer->writeChildStart("alternate"_zc);
  node.getAlternate().accept(*this);
  impl->serializer->writeChildEnd("alternate"_zc);

  impl->serializer->writeNodeEnd("ConditionalExpression"_zc);
}

void ASTDumper::visit(const CallExpression& node) {
  impl->serializer->writeNodeStart("CallExpression"_zc);

  impl->serializer->writeChildStart("callee"_zc);
  node.getCallee().accept(*this);
  impl->serializer->writeChildEnd("callee"_zc);

  const auto& args = node.getArguments();
  impl->serializer->writeArrayStart("arguments"_zc, args.size());
  for (const auto& arg : args) {
    impl->serializer->writeArrayElement();
    arg.accept(*this);
  }
  impl->serializer->writeArrayEnd("arguments"_zc);

  impl->serializer->writeNodeEnd("CallExpression"_zc);
}

void ASTDumper::visit(const LiteralExpression& node) { node.accept(*this); }

void ASTDumper::visit(const CastExpression& node) { node.accept(*this); }

void ASTDumper::visit(const AsExpression& node) {
  impl->serializer->writeNodeStart("AsExpression"_zc);

  impl->serializer->writeChildStart("expression"_zc);
  node.getExpression().accept(*this);
  impl->serializer->writeChildEnd("expression"_zc);

  impl->serializer->writeChildStart("targetType"_zc);
  node.getTargetType().accept(*this);
  impl->serializer->writeChildEnd("targetType"_zc);

  impl->serializer->writeNodeEnd("AsExpression"_zc);
}

void ASTDumper::visit(const ForcedAsExpression& node) {
  impl->serializer->writeNodeStart("ForcedAsExpression"_zc);

  impl->serializer->writeChildStart("expression"_zc);
  node.getExpression().accept(*this);
  impl->serializer->writeChildEnd("expression"_zc);

  impl->serializer->writeChildStart("targetType"_zc);
  node.getTargetType().accept(*this);
  impl->serializer->writeChildEnd("targetType"_zc);

  impl->serializer->writeNodeEnd("ForcedAsExpression"_zc);
}

void ASTDumper::visit(const ConditionalAsExpression& node) {
  impl->serializer->writeNodeStart("ConditionalAsExpression"_zc);

  impl->serializer->writeChildStart("expression"_zc);
  node.getExpression().accept(*this);
  impl->serializer->writeChildEnd("expression"_zc);

  impl->serializer->writeChildStart("targetType"_zc);
  node.getTargetType().accept(*this);
  impl->serializer->writeChildEnd("targetType"_zc);

  impl->serializer->writeNodeEnd("ConditionalAsExpression"_zc);
}

void ASTDumper::visit(const NonNullExpression& node) {
  impl->serializer->writeNodeStart("NonNullExpression"_zc);

  impl->serializer->writeChildStart("expression"_zc);
  node.getExpression().accept(*this);
  impl->serializer->writeChildEnd("expression"_zc);

  impl->serializer->writeNodeEnd("NonNullExpression"_zc);
}

void ASTDumper::visit(const ExpressionWithTypeArguments& node) {
  impl->serializer->writeNodeStart("ExpressionWithTypeArguments"_zc);

  impl->serializer->writeChildStart("expression"_zc);
  node.getExpression().accept(*this);
  impl->serializer->writeChildEnd("expression"_zc);

  const auto& typeArguments = node.getTypeArguments();
  ZC_IF_SOME(args, typeArguments) {
    if (!args.empty()) {
      impl->serializer->writeArrayStart("typeArguments"_zc, args.size());
      for (const auto& typeArg : args) {
        impl->serializer->writeArrayElement();
        typeArg.accept(*this);
      }
      impl->serializer->writeArrayEnd("typeArguments"_zc);
    }
  }

  impl->serializer->writeNodeEnd("ExpressionWithTypeArguments"_zc);
}

void ASTDumper::visit(const HeritageClause& node) {
  impl->serializer->writeNodeStart("HeritageClause"_zc);

  impl->serializer->writeChildStart("token"_zc);
  impl->serializer->writeNodeStart("TokenNode"_zc);
  ZC_IF_SOME(text, lexer::Token::getStaticTextForTokenKind(node.getToken())) {
    impl->serializer->writeProperty("symbol", text);
  }
  impl->serializer->writeNodeEnd("TokenNode"_zc);
  impl->serializer->writeChildEnd("token"_zc);

  const auto& types = node.getTypes();
  impl->serializer->writeArrayStart("types"_zc, types.size());
  for (const auto& t : types) {
    impl->serializer->writeArrayElement();
    t->accept(*this);
  }
  impl->serializer->writeArrayEnd("types"_zc);

  impl->serializer->writeNodeEnd("HeritageClause"_zc);
}

void ASTDumper::visit(const VoidExpression& node) {
  impl->serializer->writeNodeStart("VoidExpression"_zc);

  impl->serializer->writeChildStart("expression"_zc);
  node.getExpression().accept(*this);
  impl->serializer->writeChildEnd("expression"_zc);

  impl->serializer->writeNodeEnd("VoidExpression"_zc);
}

void ASTDumper::visit(const TypeOfExpression& node) {
  impl->serializer->writeNodeStart("TypeOfExpression"_zc);

  impl->serializer->writeChildStart("expression"_zc);
  node.getExpression().accept(*this);
  impl->serializer->writeChildEnd("expression"_zc);

  impl->serializer->writeNodeEnd("TypeOfExpression"_zc);
}

void ASTDumper::visit(const AwaitExpression& node) {
  impl->serializer->writeNodeStart("AwaitExpression"_zc);

  impl->serializer->writeChildStart("expression"_zc);
  node.getExpression().accept(*this);
  impl->serializer->writeChildEnd("expression"_zc);

  impl->serializer->writeNodeEnd("AwaitExpression"_zc);
}

void ASTDumper::visit(const FunctionExpression& node) {
  impl->serializer->writeNodeStart("FunctionExpression"_zc);

  ZC_IF_SOME(typeParams, node.getTypeParameters()) {
    impl->serializer->writeArrayStart("typeParameters"_zc, typeParams.size());
    for (const auto& param : typeParams) {
      impl->serializer->writeArrayElement();
      param->accept(*this);
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

  const auto& captures = node.getCaptures();
  impl->serializer->writeArrayStart("captures"_zc, captures.size());
  for (const auto& capture : captures) {
    impl->serializer->writeArrayElement();
    capture.accept(*this);
  }
  impl->serializer->writeArrayEnd("captures"_zc);

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

  const auto& elements = node.getElements();
  impl->serializer->writeArrayStart("elements"_zc, elements.size());
  for (const auto& element : elements) {
    impl->serializer->writeArrayElement();
    element.accept(*this);
  }
  impl->serializer->writeArrayEnd("elements"_zc);

  impl->serializer->writeNodeEnd("ArrayLiteralExpression"_zc);
}

void ASTDumper::visit(const ObjectLiteralExpression& node) {
  impl->serializer->writeNodeStart("ObjectLiteralExpression"_zc);

  const auto& properties = node.getProperties();
  impl->serializer->writeArrayStart("properties"_zc, properties.size());
  for (const auto& prop : properties) {
    impl->serializer->writeArrayElement();
    prop.accept(*this);
  }
  impl->serializer->writeArrayEnd("properties"_zc);

  impl->serializer->writeNodeEnd("ObjectLiteralExpression"_zc);
}

void ASTDumper::visit(const ObjectLiteralElement& node) {
  // Abstract
}

void ASTDumper::visit(const PropertyAssignment& node) {
  impl->serializer->writeNodeStart("PropertyAssignment"_zc);

  impl->serializer->writeChildStart("name"_zc);
  node.getNameIdentifier().accept(*this);
  impl->serializer->writeChildEnd("name"_zc);

  impl->serializer->writeChildStart("initializer"_zc);
  ZC_IF_SOME(init, node.getInitializer()) { init.accept(*this); }
  else { impl->serializer->writeNull(); }
  impl->serializer->writeChildEnd("initializer"_zc);

  ZC_IF_SOME(questionToken, node.getQuestionToken()) {
    impl->serializer->writeChildStart("questionToken"_zc);
    questionToken.accept(*this);
    impl->serializer->writeChildEnd("questionToken"_zc);
  }

  impl->serializer->writeNodeEnd("PropertyAssignment"_zc);
}

void ASTDumper::visit(const ShorthandPropertyAssignment& node) {
  impl->serializer->writeNodeStart("ShorthandPropertyAssignment"_zc);

  impl->serializer->writeChildStart("name"_zc);
  node.getNameIdentifier().accept(*this);
  impl->serializer->writeChildEnd("name"_zc);

  ZC_IF_SOME(init, node.getObjectAssignmentInitializer()) {
    impl->serializer->writeChildStart("objectAssignmentInitializer"_zc);
    init.accept(*this);
    impl->serializer->writeChildEnd("objectAssignmentInitializer"_zc);
  }

  ZC_IF_SOME(equalsToken, node.getEqualsToken()) {
    impl->serializer->writeChildStart("equalsToken"_zc);
    equalsToken.accept(*this);
    impl->serializer->writeChildEnd("equalsToken"_zc);
  }

  impl->serializer->writeNodeEnd("ShorthandPropertyAssignment"_zc);
}

void ASTDumper::visit(const SpreadAssignment& node) {
  impl->serializer->writeNodeStart("SpreadAssignment"_zc);

  impl->serializer->writeChildStart("expression"_zc);
  node.getExpression().accept(*this);
  impl->serializer->writeChildEnd("expression"_zc);

  impl->serializer->writeNodeEnd("SpreadAssignment"_zc);
}

void ASTDumper::visit(const SpreadElement& node) {
  impl->serializer->writeNodeStart("SpreadElement"_zc);

  impl->serializer->writeChildStart("expression"_zc);
  node.getExpression().accept(*this);
  impl->serializer->writeChildEnd("expression"_zc);

  impl->serializer->writeNodeEnd("SpreadElement"_zc);
}

// Type node visit methods
void ASTDumper::visit(const TypeNode& node) { node.accept(*this); }

void ASTDumper::visit(const TokenNode& node) {
  impl->serializer->writeNodeStart("TokenNode"_zc);

  // Get the operator symbol from the TokenNode's SyntaxKind
  ZC_IF_SOME(operatorText, lexer::Token::getStaticTextForTokenKind(node.getKind())) {
    impl->serializer->writeProperty("symbol", operatorText);
  }

  impl->serializer->writeNodeEnd("TokenNode"_zc);
}

void ASTDumper::visit(const TypeReferenceNode& node) {
  impl->serializer->writeNodeStart("TypeReferenceNode"_zc);
  impl->serializer->writeProperty("name"_zc, node.getName().getText());
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

void ASTDumper::visit(const PredefinedTypeNode& node) { node.accept(*this); }

void ASTDumper::visit(const Declaration& node) { node.accept(*this); }

void ASTDumper::visit(const NamedDeclaration& node) { node.accept(*this); }

void ASTDumper::visit(const Pattern& node) { node.accept(*this); }

void ASTDumper::visit(const PrimaryPattern& node) { node.accept(*this); }

void ASTDumper::visit(const BindingPattern& node) { node.accept(*this); }

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

void ASTDumper::visit(const FunctionTypeNode& node) {
  impl->serializer->writeNodeStart("FunctionTypeNode"_zc);

  ZC_IF_SOME(typeParams, node.getTypeParameters()) {
    impl->serializer->writeArrayStart("typeParameters"_zc, typeParams.size());
    for (const auto& param : typeParams) {
      impl->serializer->writeArrayElement();
      param->accept(*this);
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

void ASTDumper::visit(const NamedTupleElement& node) {
  impl->serializer->writeNodeStart("NamedTupleElement"_zc);

  impl->serializer->writeChildStart("name"_zc);
  dumpName(*this, node.getName());
  impl->serializer->writeChildEnd("name"_zc);

  impl->serializer->writeChildStart("type"_zc);
  node.getType().accept(*this);
  impl->serializer->writeChildEnd("type"_zc);

  impl->serializer->writeNodeEnd("NamedTupleElement"_zc);
}

// Missing declaration visit methods
void ASTDumper::visit(const MethodDeclaration& node) {
  impl->serializer->writeNodeStart("MethodDeclaration"_zc);

  const auto& modifiers = node.getModifiers();
  impl->serializer->writeArrayStart("modifiers"_zc, modifiers.size());
  for (const auto& modifier : modifiers) {
    impl->serializer->writeArrayElement();
    impl->serializer->writeNodeStart("TokenNode"_zc);
    ZC_IF_SOME(text, lexer::Token::getStaticTextForTokenKind(modifier)) {
      impl->serializer->writeProperty("symbol"_zc, text);
    }
    impl->serializer->writeNodeEnd("TokenNode"_zc);
  }
  impl->serializer->writeArrayEnd("modifiers"_zc);

  impl->serializer->writeChildStart("name"_zc);
  dumpName(*this, node.getName());
  impl->serializer->writeChildEnd("name"_zc);

  impl->serializer->writeProperty("optional"_zc, node.isOptional() ? "true"_zc : "false"_zc);

  const auto& typeParameters = node.getTypeParameters();
  if (!typeParameters.empty()) {
    impl->serializer->writeArrayStart("typeParameters"_zc, typeParameters.size());
    for (const auto& param : typeParameters) {
      impl->serializer->writeArrayElement();
      param.accept(*this);
    }
    impl->serializer->writeArrayEnd("typeParameters"_zc);
  }

  const auto& parameters = node.getParameters();
  impl->serializer->writeArrayStart("parameters"_zc, parameters.size());
  for (const auto& param : parameters) {
    impl->serializer->writeArrayElement();
    param.accept(*this);
  }
  impl->serializer->writeArrayEnd("parameters"_zc);

  impl->serializer->writeChildStart("returnType"_zc);
  ZC_IF_SOME(returnType, node.getReturnType()) { returnType.accept(*this); }
  else { impl->serializer->writeNull(); }
  impl->serializer->writeChildEnd("returnType"_zc);

  impl->serializer->writeChildStart("body"_zc);
  ZC_IF_SOME(body, node.getBody()) { body.accept(*this); }
  else { impl->serializer->writeNull(); }
  impl->serializer->writeChildEnd("body"_zc);

  impl->serializer->writeNodeEnd("MethodDeclaration"_zc);
}

void ASTDumper::visit(const GetAccessor& node) {
  impl->serializer->writeNodeStart("GetAccessor"_zc);

  const auto& modifiers = node.getModifiers();
  impl->serializer->writeArrayStart("modifiers"_zc, modifiers.size());
  for (const auto& modifier : modifiers) {
    impl->serializer->writeArrayElement();
    impl->serializer->writeNodeStart("TokenNode"_zc);
    ZC_IF_SOME(text, lexer::Token::getStaticTextForTokenKind(modifier)) {
      impl->serializer->writeProperty("symbol"_zc, text);
    }
    impl->serializer->writeNodeEnd("TokenNode"_zc);
  }
  impl->serializer->writeArrayEnd("modifiers"_zc);

  impl->serializer->writeChildStart("name"_zc);
  dumpName(*this, node.getName());
  impl->serializer->writeChildEnd("name"_zc);

  const auto& typeParameters = node.getTypeParameters();
  if (!typeParameters.empty()) {
    impl->serializer->writeArrayStart("typeParameters"_zc, typeParameters.size());
    for (const auto& param : typeParameters) {
      impl->serializer->writeArrayElement();
      param.accept(*this);
    }
    impl->serializer->writeArrayEnd("typeParameters"_zc);
  }

  const auto& parameters = node.getParameters();
  impl->serializer->writeArrayStart("parameters"_zc, parameters.size());
  for (const auto& param : parameters) {
    impl->serializer->writeArrayElement();
    param.accept(*this);
  }
  impl->serializer->writeArrayEnd("parameters"_zc);

  impl->serializer->writeChildStart("returnType"_zc);
  ZC_IF_SOME(returnType, node.getReturnType()) { returnType.accept(*this); }
  else { impl->serializer->writeNull(); }
  impl->serializer->writeChildEnd("returnType"_zc);

  impl->serializer->writeChildStart("body"_zc);
  ZC_IF_SOME(body, node.getBody()) { body.accept(*this); }
  else { impl->serializer->writeNull(); }
  impl->serializer->writeChildEnd("body"_zc);

  impl->serializer->writeNodeEnd("GetAccessor"_zc);
}

void ASTDumper::visit(const SetAccessor& node) {
  impl->serializer->writeNodeStart("SetAccessor"_zc);

  const auto& modifiers = node.getModifiers();
  impl->serializer->writeArrayStart("modifiers"_zc, modifiers.size());
  for (const auto& modifier : modifiers) {
    impl->serializer->writeArrayElement();
    impl->serializer->writeNodeStart("TokenNode"_zc);
    ZC_IF_SOME(text, lexer::Token::getStaticTextForTokenKind(modifier)) {
      impl->serializer->writeProperty("symbol"_zc, text);
    }
    impl->serializer->writeNodeEnd("TokenNode"_zc);
  }
  impl->serializer->writeArrayEnd("modifiers"_zc);

  impl->serializer->writeChildStart("name"_zc);
  dumpName(*this, node.getName());
  impl->serializer->writeChildEnd("name"_zc);

  const auto& typeParameters = node.getTypeParameters();
  if (!typeParameters.empty()) {
    impl->serializer->writeArrayStart("typeParameters"_zc, typeParameters.size());
    for (const auto& param : typeParameters) {
      impl->serializer->writeArrayElement();
      param.accept(*this);
    }
    impl->serializer->writeArrayEnd("typeParameters"_zc);
  }

  const auto& parameters = node.getParameters();
  impl->serializer->writeArrayStart("parameters"_zc, parameters.size());
  for (const auto& param : parameters) {
    impl->serializer->writeArrayElement();
    param.accept(*this);
  }
  impl->serializer->writeArrayEnd("parameters"_zc);

  impl->serializer->writeChildStart("returnType"_zc);
  ZC_IF_SOME(returnType, node.getReturnType()) { returnType.accept(*this); }
  else { impl->serializer->writeNull(); }
  impl->serializer->writeChildEnd("returnType"_zc);

  impl->serializer->writeChildStart("body"_zc);
  ZC_IF_SOME(body, node.getBody()) { body.accept(*this); }
  else { impl->serializer->writeNull(); }
  impl->serializer->writeChildEnd("body"_zc);

  impl->serializer->writeNodeEnd("SetAccessor"_zc);
}

void ASTDumper::visit(const InitDeclaration& node) {
  impl->serializer->writeNodeStart("InitDeclaration"_zc);

  const auto& modifiers = node.getModifiers();
  impl->serializer->writeArrayStart("modifiers"_zc, modifiers.size());
  for (const auto& modifier : modifiers) {
    impl->serializer->writeArrayElement();
    impl->serializer->writeNodeStart("TokenNode"_zc);
    ZC_IF_SOME(text, lexer::Token::getStaticTextForTokenKind(modifier)) {
      impl->serializer->writeProperty("symbol"_zc, text);
    }
    impl->serializer->writeNodeEnd("TokenNode"_zc);
  }
  impl->serializer->writeArrayEnd("modifiers"_zc);

  impl->serializer->writeChildStart("name"_zc);
  dumpName(*this, node.getName());
  impl->serializer->writeChildEnd("name"_zc);

  const auto& typeParameters = node.getTypeParameters();
  if (!typeParameters.empty()) {
    impl->serializer->writeArrayStart("typeParameters"_zc, typeParameters.size());
    for (const auto& param : typeParameters) {
      impl->serializer->writeArrayElement();
      param.accept(*this);
    }
    impl->serializer->writeArrayEnd("typeParameters"_zc);
  }

  const auto& parameters = node.getParameters();
  impl->serializer->writeArrayStart("parameters"_zc, parameters.size());
  for (const auto& param : parameters) {
    impl->serializer->writeArrayElement();
    param.accept(*this);
  }
  impl->serializer->writeArrayEnd("parameters"_zc);

  impl->serializer->writeChildStart("returnType"_zc);
  ZC_IF_SOME(returnType, node.getReturnType()) { returnType.accept(*this); }
  else { impl->serializer->writeNull(); }
  impl->serializer->writeChildEnd("returnType"_zc);

  impl->serializer->writeChildStart("body"_zc);
  ZC_IF_SOME(body, node.getBody()) { body.accept(*this); }
  else { impl->serializer->writeNull(); }
  impl->serializer->writeChildEnd("body"_zc);

  impl->serializer->writeNodeEnd("InitDeclaration"_zc);
}

void ASTDumper::visit(const DeinitDeclaration& node) {
  impl->serializer->writeNodeStart("DeinitDeclaration"_zc);

  const auto& modifiers = node.getModifiers();
  impl->serializer->writeArrayStart("modifiers"_zc, modifiers.size());
  for (const auto& modifier : modifiers) {
    impl->serializer->writeArrayElement();
    impl->serializer->writeNodeStart("TokenNode"_zc);
    ZC_IF_SOME(text, lexer::Token::getStaticTextForTokenKind(modifier)) {
      impl->serializer->writeProperty("symbol"_zc, text);
    }
    impl->serializer->writeNodeEnd("TokenNode"_zc);
  }
  impl->serializer->writeArrayEnd("modifiers"_zc);

  impl->serializer->writeChildStart("name"_zc);
  dumpName(*this, node.getName());
  impl->serializer->writeChildEnd("name"_zc);

  impl->serializer->writeChildStart("body"_zc);
  ZC_IF_SOME(body, node.getBody()) { body.accept(*this); }
  else { impl->serializer->writeNull(); }
  impl->serializer->writeChildEnd("body"_zc);

  impl->serializer->writeNodeEnd("DeinitDeclaration"_zc);
}

void ASTDumper::visit(const ParameterDeclaration& node) {
  impl->serializer->writeNodeStart("ParameterDeclaration"_zc);

  impl->serializer->writeChildStart("name"_zc);
  dumpName(*this, node.getName());
  impl->serializer->writeChildEnd("name"_zc);

  impl->serializer->writeChildStart("type"_zc);
  ZC_IF_SOME(type, node.getType()) { type.accept(*this); }
  else { impl->serializer->writeNull(); }
  impl->serializer->writeChildEnd("type"_zc);

  impl->serializer->writeChildStart("initializer"_zc);
  ZC_IF_SOME(init, node.getInitializer()) { init.accept(*this); }
  else { impl->serializer->writeNull(); }
  impl->serializer->writeChildEnd("initializer"_zc);

  impl->serializer->writeNodeEnd("ParameterDeclaration"_zc);
}

void ASTDumper::visit(const PropertyDeclaration& node) {
  impl->serializer->writeNodeStart("PropertyDeclaration"_zc);

  const auto& modifiers = node.getModifiers();
  impl->serializer->writeArrayStart("modifiers"_zc, modifiers.size());
  for (const auto& modifier : modifiers) {
    impl->serializer->writeArrayElement();
    impl->serializer->writeNodeStart("TokenNode"_zc);
    ZC_IF_SOME(text, lexer::Token::getStaticTextForTokenKind(modifier)) {
      impl->serializer->writeProperty("symbol"_zc, text);
    }
    impl->serializer->writeNodeEnd("TokenNode"_zc);
  }
  impl->serializer->writeArrayEnd("modifiers"_zc);

  impl->serializer->writeChildStart("name"_zc);
  dumpName(*this, node.getName());
  impl->serializer->writeChildEnd("name"_zc);

  impl->serializer->writeChildStart("type"_zc);
  ZC_IF_SOME(type, node.getType()) { type.accept(*this); }
  else { impl->serializer->writeNull(); }
  impl->serializer->writeChildEnd("type"_zc);

  impl->serializer->writeChildStart("initializer"_zc);
  ZC_IF_SOME(init, node.getInitializer()) { init.accept(*this); }
  else { impl->serializer->writeNull(); }
  impl->serializer->writeChildEnd("initializer"_zc);

  impl->serializer->writeNodeEnd("PropertyDeclaration"_zc);
}

void ASTDumper::visit(const MissingDeclaration& node) {
  impl->serializer->writeNodeStart("MissingDeclaration"_zc);
  impl->serializer->writeNodeEnd("MissingDeclaration"_zc);
}
void ASTDumper::visit(const SemicolonClassElement& node) {
  impl->serializer->writeNodeStart("SemicolonClassElement"_zc);
  impl->serializer->writeNodeEnd("SemicolonClassElement"_zc);
}
void ASTDumper::visit(const SemicolonInterfaceElement& node) {
  impl->serializer->writeNodeStart("SemicolonInterfaceElement"_zc);
  impl->serializer->writeNodeEnd("SemicolonInterfaceElement"_zc);
}

void ASTDumper::visit(const InterfaceBody& node) {
  impl->serializer->writeNodeStart("InterfaceBody"_zc);
  impl->serializer->writeNodeEnd("InterfaceBody"_zc);
}

void ASTDumper::visit(const StructBody& node) {
  impl->serializer->writeNodeStart("StructBody"_zc);
  impl->serializer->writeNodeEnd("StructBody"_zc);
}

void ASTDumper::visit(const ErrorBody& node) {
  impl->serializer->writeNodeStart("ErrorBody"_zc);
  impl->serializer->writeNodeEnd("ErrorBody"_zc);
}

void ASTDumper::visit(const EnumBody& node) {
  impl->serializer->writeNodeStart("EnumBody"_zc);
  impl->serializer->writeNodeEnd("EnumBody"_zc);
}

void ASTDumper::visit(const ArrayBindingPattern& node) {
  impl->serializer->writeNodeStart("ArrayBindingPattern"_zc);

  const auto& elements = node.getElements();
  impl->serializer->writeArrayStart("elements"_zc, elements.size());
  for (const auto& element : elements) {
    impl->serializer->writeArrayElement();
    element.accept(*this);
  }
  impl->serializer->writeArrayEnd("elements"_zc);

  impl->serializer->writeNodeEnd("ArrayBindingPattern"_zc);
}

void ASTDumper::visit(const ObjectBindingPattern& node) {
  impl->serializer->writeNodeStart("ObjectBindingPattern"_zc);

  const auto& properties = node.getProperties();
  impl->serializer->writeArrayStart("properties"_zc, properties.size());
  for (const auto& prop : properties) {
    impl->serializer->writeArrayElement();
    prop.accept(*this);
  }
  impl->serializer->writeArrayEnd("properties"_zc);

  impl->serializer->writeNodeEnd("ObjectBindingPattern"_zc);
}

void ASTDumper::visit(const ThisExpression& node) {
  impl->serializer->writeNodeStart("ThisExpression"_zc);
  impl->serializer->writeNodeEnd("ThisExpression"_zc);
}

void ASTDumper::visit(const SuperExpression& node) {
  impl->serializer->writeNodeStart("SuperExpression"_zc);
  impl->serializer->writeNodeEnd("SuperExpression"_zc);
}

void ASTDumper::visit(const BoolTypeNode& node) {
  impl->serializer->writeNodeStart("BoolTypeNode"_zc);
  impl->serializer->writeProperty("name"_zc, node.getName());
  impl->serializer->writeNodeEnd("BoolTypeNode"_zc);
}

void ASTDumper::visit(const I8TypeNode& node) {
  impl->serializer->writeNodeStart("I8TypeNode"_zc);
  impl->serializer->writeProperty("name"_zc, node.getName());
  impl->serializer->writeNodeEnd("I8TypeNode"_zc);
}

void ASTDumper::visit(const I16TypeNode& node) {
  impl->serializer->writeNodeStart("I16TypeNode"_zc);
  impl->serializer->writeProperty("name"_zc, node.getName());
  impl->serializer->writeNodeEnd("I16TypeNode"_zc);
}

void ASTDumper::visit(const I32TypeNode& node) {
  impl->serializer->writeNodeStart("I32TypeNode"_zc);
  impl->serializer->writeProperty("name"_zc, node.getName());
  impl->serializer->writeNodeEnd("I32TypeNode"_zc);
}

void ASTDumper::visit(const I64TypeNode& node) {
  impl->serializer->writeNodeStart("I64TypeNode"_zc);
  impl->serializer->writeProperty("name"_zc, node.getName());
  impl->serializer->writeNodeEnd("I64TypeNode"_zc);
}

void ASTDumper::visit(const U8TypeNode& node) {
  impl->serializer->writeNodeStart("U8TypeNode"_zc);
  impl->serializer->writeProperty("name"_zc, node.getName());
  impl->serializer->writeNodeEnd("U8TypeNode"_zc);
}

void ASTDumper::visit(const U16TypeNode& node) {
  impl->serializer->writeNodeStart("U16TypeNode"_zc);
  impl->serializer->writeProperty("name"_zc, node.getName());
  impl->serializer->writeNodeEnd("U16TypeNode"_zc);
}

void ASTDumper::visit(const U32TypeNode& node) {
  impl->serializer->writeNodeStart("U32TypeNode"_zc);
  impl->serializer->writeProperty("name"_zc, node.getName());
  impl->serializer->writeNodeEnd("U32TypeNode"_zc);
}

void ASTDumper::visit(const U64TypeNode& node) {
  impl->serializer->writeNodeStart("U64TypeNode"_zc);
  impl->serializer->writeProperty("name"_zc, node.getName());
  impl->serializer->writeNodeEnd("U64TypeNode"_zc);
}

void ASTDumper::visit(const F32TypeNode& node) {
  impl->serializer->writeNodeStart("F32TypeNode"_zc);
  impl->serializer->writeProperty("name"_zc, node.getName());
  impl->serializer->writeNodeEnd("F32TypeNode"_zc);
}

void ASTDumper::visit(const F64TypeNode& node) {
  impl->serializer->writeNodeStart("F64TypeNode"_zc);
  impl->serializer->writeProperty("name"_zc, node.getName());
  impl->serializer->writeNodeEnd("F64TypeNode"_zc);
}

void ASTDumper::visit(const StrTypeNode& node) {
  impl->serializer->writeNodeStart("StrTypeNode"_zc);
  impl->serializer->writeProperty("name"_zc, node.getName());
  impl->serializer->writeNodeEnd("StrTypeNode"_zc);
}

void ASTDumper::visit(const UnitTypeNode& node) {
  impl->serializer->writeNodeStart("UnitTypeNode"_zc);
  impl->serializer->writeProperty("name"_zc, node.getName());
  impl->serializer->writeNodeEnd("UnitTypeNode"_zc);
}

void ASTDumper::visit(const NullTypeNode& node) {
  impl->serializer->writeNodeStart("NullTypeNode"_zc);
  impl->serializer->writeProperty("name"_zc, node.getName());
  impl->serializer->writeNodeEnd("NullTypeNode"_zc);
}

// Signature node visit methods
void ASTDumper::visit(const PropertySignature& node) {
  impl->serializer->writeNodeStart("PropertySignature"_zc);

  const auto& modifiers = node.getModifiers();
  impl->serializer->writeArrayStart("modifiers"_zc, modifiers.size());
  for (const auto& modifier : modifiers) {
    impl->serializer->writeArrayElement();
    impl->serializer->writeNodeStart("TokenNode"_zc);
    ZC_IF_SOME(text, lexer::Token::getStaticTextForTokenKind(modifier)) {
      impl->serializer->writeProperty("symbol"_zc, text);
    }
    impl->serializer->writeNodeEnd("TokenNode"_zc);
  }
  impl->serializer->writeArrayEnd("modifiers"_zc);

  impl->serializer->writeChildStart("name"_zc);
  dumpName(*this, node.getName());
  impl->serializer->writeChildEnd("name"_zc);

  impl->serializer->writeProperty("optional"_zc, node.isOptional() ? "true"_zc : "false"_zc);

  impl->serializer->writeChildStart("type"_zc);
  ZC_IF_SOME(type, node.getType()) { type.accept(*this); }
  else { impl->serializer->writeNull(); }
  impl->serializer->writeChildEnd("type"_zc);

  impl->serializer->writeChildStart("initializer"_zc);
  ZC_IF_SOME(init, node.getInitializer()) { init.accept(*this); }
  else { impl->serializer->writeNull(); }
  impl->serializer->writeChildEnd("initializer"_zc);

  impl->serializer->writeNodeEnd("PropertySignature"_zc);
}

void ASTDumper::visit(const MethodSignature& node) {
  impl->serializer->writeNodeStart("MethodSignature"_zc);

  const auto& modifiers = node.getModifiers();
  impl->serializer->writeArrayStart("modifiers"_zc, modifiers.size());
  for (const auto& modifier : modifiers) {
    impl->serializer->writeArrayElement();
    impl->serializer->writeNodeStart("TokenNode"_zc);
    ZC_IF_SOME(text, lexer::Token::getStaticTextForTokenKind(modifier)) {
      impl->serializer->writeProperty("symbol"_zc, text);
    }
    impl->serializer->writeNodeEnd("TokenNode"_zc);
  }
  impl->serializer->writeArrayEnd("modifiers"_zc);

  impl->serializer->writeChildStart("name"_zc);
  dumpName(*this, node.getName());
  impl->serializer->writeChildEnd("name"_zc);

  impl->serializer->writeProperty("optional"_zc, node.isOptional() ? "true"_zc : "false"_zc);

  const auto& typeParameters = node.getTypeParameters();
  if (!typeParameters.empty()) {
    impl->serializer->writeArrayStart("typeParameters"_zc, typeParameters.size());
    for (const auto& param : typeParameters) {
      impl->serializer->writeArrayElement();
      param.accept(*this);
    }
    impl->serializer->writeArrayEnd("typeParameters"_zc);
  }

  const auto& parameters = node.getParameters();
  impl->serializer->writeArrayStart("parameters"_zc, parameters.size());
  for (const auto& param : parameters) {
    impl->serializer->writeArrayElement();
    param.accept(*this);
  }
  impl->serializer->writeArrayEnd("parameters"_zc);

  impl->serializer->writeChildStart("returnType"_zc);
  ZC_IF_SOME(returnType, node.getReturnType()) { returnType.accept(*this); }
  else { impl->serializer->writeNull(); }
  impl->serializer->writeChildEnd("returnType"_zc);

  impl->serializer->writeNodeEnd("MethodSignature"_zc);
}

// Body/Structure node visit methods
void ASTDumper::visit(const ClassBody& node) {
  impl->serializer->writeNodeStart("ClassBody"_zc);
  impl->serializer->writeNodeEnd("ClassBody"_zc);
}

// Missing visitor method implementations
void ASTDumper::visit(const VariableDeclaration& node) {
  impl->serializer->writeNodeStart("VariableDeclaration"_zc);

  impl->serializer->writeChildStart("name"_zc);
  dumpName(*this, node.getName());
  impl->serializer->writeChildEnd("name"_zc);

  impl->serializer->writeChildStart("type"_zc);
  ZC_IF_SOME(type, node.getType()) { type.accept(*this); }
  else { impl->serializer->writeNull(); }
  impl->serializer->writeChildEnd("type"_zc);

  impl->serializer->writeChildStart("initializer"_zc);
  ZC_IF_SOME(init, node.getInitializer()) { init.accept(*this); }
  else { impl->serializer->writeNull(); }
  impl->serializer->writeChildEnd("initializer"_zc);

  impl->serializer->writeNodeEnd("VariableDeclaration"_zc);
}

void ASTDumper::visit(const WildcardPattern& node) {
  impl->serializer->writeNodeStart("WildcardPattern"_zc);

  impl->serializer->writeChildStart("typeAnnotation"_zc);
  ZC_IF_SOME(type, node.getTypeAnnotation()) { type.accept(*this); }
  else { impl->serializer->writeNull(); }
  impl->serializer->writeChildEnd("typeAnnotation"_zc);

  impl->serializer->writeNodeEnd("WildcardPattern"_zc);
}

void ASTDumper::visit(const IdentifierPattern& node) {
  impl->serializer->writeNodeStart("IdentifierPattern"_zc);

  impl->serializer->writeChildStart("identifier"_zc);
  node.getIdentifier().accept(*this);
  impl->serializer->writeChildEnd("identifier"_zc);

  impl->serializer->writeChildStart("typeAnnotation"_zc);
  ZC_IF_SOME(type, node.getTypeAnnotation()) { type.accept(*this); }
  else { impl->serializer->writeNull(); }
  impl->serializer->writeChildEnd("typeAnnotation"_zc);

  impl->serializer->writeNodeEnd("IdentifierPattern"_zc);
}

void ASTDumper::visit(const TuplePattern& node) {
  impl->serializer->writeNodeStart("TuplePattern"_zc);

  const auto& elements = node.getElements();
  impl->serializer->writeArrayStart("elements"_zc, elements.size());
  for (const auto& element : elements) {
    impl->serializer->writeArrayElement();
    element.accept(*this);
  }
  impl->serializer->writeArrayEnd("elements"_zc);

  impl->serializer->writeNodeEnd("TuplePattern"_zc);
}

void ASTDumper::visit(const StructurePattern& node) {
  impl->serializer->writeNodeStart("StructurePattern"_zc);

  const auto& properties = node.getProperties();
  impl->serializer->writeArrayStart("properties"_zc, properties.size());
  for (const auto& prop : properties) {
    impl->serializer->writeArrayElement();
    prop.accept(*this);
  }
  impl->serializer->writeArrayEnd("properties"_zc);

  impl->serializer->writeNodeEnd("StructurePattern"_zc);
}

void ASTDumper::visit(const ArrayPattern& node) {
  impl->serializer->writeNodeStart("ArrayPattern"_zc);

  const auto& elements = node.getElements();
  impl->serializer->writeArrayStart("elements"_zc, elements.size());
  for (const auto& element : elements) {
    impl->serializer->writeArrayElement();
    element.accept(*this);
  }
  impl->serializer->writeArrayEnd("elements"_zc);

  impl->serializer->writeNodeEnd("ArrayPattern"_zc);
}

void ASTDumper::visit(const IsPattern& node) {
  impl->serializer->writeNodeStart("IsPattern"_zc);

  impl->serializer->writeChildStart("type"_zc);
  node.getType().accept(*this);
  impl->serializer->writeChildEnd("type"_zc);

  impl->serializer->writeNodeEnd("IsPattern"_zc);
}

void ASTDumper::visit(const ExpressionPattern& node) {
  impl->serializer->writeNodeStart("ExpressionPattern"_zc);

  impl->serializer->writeChildStart("expression"_zc);
  node.getExpression().accept(*this);
  impl->serializer->writeChildEnd("expression"_zc);

  impl->serializer->writeNodeEnd("ExpressionPattern"_zc);
}

void ASTDumper::visit(const EnumPattern& node) {
  impl->serializer->writeNodeStart("EnumPattern"_zc);

  impl->serializer->writeChildStart("typeReference"_zc);
  ZC_IF_SOME(typeReference, node.getTypeReference()) { typeReference.accept(*this); }
  else { impl->serializer->writeNull(); }
  impl->serializer->writeChildEnd("typeReference"_zc);

  impl->serializer->writeChildStart("propertyName"_zc);
  node.getPropertyName().accept(*this);
  impl->serializer->writeChildEnd("propertyName"_zc);

  impl->serializer->writeChildStart("tuplePattern"_zc);
  node.getTuplePattern().accept(*this);
  impl->serializer->writeChildEnd("tuplePattern"_zc);

  impl->serializer->writeNodeEnd("EnumPattern"_zc);
}

void ASTDumper::visit(const CaptureElement& node) {
  impl->serializer->writeNodeStart("CaptureElement"_zc);
  impl->serializer->writeProperty("isByReference"_zc,
                                  node.isByReference() ? "true"_zc : "false"_zc);
  impl->serializer->writeProperty("isThis"_zc, node.isThis() ? "true"_zc : "false"_zc);

  ZC_IF_SOME(id, node.getIdentifier()) {
    impl->serializer->writeChildStart("identifier"_zc);
    id.accept(*this);
    impl->serializer->writeChildEnd("identifier"_zc);
  }
  impl->serializer->writeNodeEnd("CaptureElement"_zc);
}

// Private helper methods implementation
void ASTDumper::writeIndent(int indent) {}

void ASTDumper::writeLine(const zc::StringPtr text, int indent) {}

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
