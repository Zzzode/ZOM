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
#include "zomlang/compiler/ast/ast.h"

namespace zomlang {
namespace compiler {
namespace ast {

class Identifier;
class Expression;
class Type;
class ReturnType;

class Statement : public Node {
public:
  explicit Statement(SyntaxKind kind = SyntaxKind::kStatement) noexcept;
  ~Statement() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(Statement);

  // Visitor pattern support
  void accept(Visitor& visitor) const;
};

// Type parameter declaration: T extends U
class TypeParameter : public Statement {
public:
  TypeParameter(zc::Own<Identifier> name, zc::Maybe<zc::Own<Type>> constraint = zc::none);
  ~TypeParameter() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(TypeParameter);

  const Identifier* getName() const;
  const Type* getConstraint() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class BindingElement : public Statement {
public:
  BindingElement(zc::Own<Identifier> name, zc::Maybe<zc::Own<Type>> type = zc::none,
                 zc::Maybe<zc::Own<Expression>> initializer = zc::none);
  ~BindingElement() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(BindingElement);

  const Identifier* getName() const;
  const Type* getType() const;
  const Expression* getInitializer() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class VariableDeclaration : public Statement {
public:
  VariableDeclaration(zc::Vector<zc::Own<BindingElement>>&& bindings);
  ~VariableDeclaration() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(VariableDeclaration);

  const NodeList<BindingElement>& getBindings() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class FunctionDeclaration : public Statement {
public:
  FunctionDeclaration(zc::Own<Identifier> name, zc::Vector<zc::Own<TypeParameter>>&& typeParameters,
                      zc::Vector<zc::Own<BindingElement>>&& parameters,
                      zc::Maybe<zc::Own<ReturnType>> returnType, zc::Own<Statement> body);
  ~FunctionDeclaration() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(FunctionDeclaration);

  const Identifier* getName() const;
  const NodeList<TypeParameter>& getTypeParameters() const;
  const NodeList<BindingElement>& getParameters() const;
  const ReturnType* getReturnType() const;
  const Statement* getBody() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class ClassDeclaration : public Statement {
public:
  ClassDeclaration(zc::Own<Identifier> name, zc::Maybe<zc::Own<Identifier>> superClass,
                   zc::Vector<zc::Own<Statement>>&& members);
  ~ClassDeclaration() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(ClassDeclaration);

  const Identifier* getName() const;
  const Identifier* getSuperClass() const;
  const NodeList<Statement>& getMembers() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class BlockStatement : public Statement {
public:
  explicit BlockStatement(zc::Vector<zc::Own<Statement>>&& statements);
  ~BlockStatement() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(BlockStatement);

  const NodeList<Statement>& getStatements() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class ExpressionStatement : public Statement {
public:
  explicit ExpressionStatement(zc::Own<Expression> expression);
  ~ExpressionStatement() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(ExpressionStatement);

  const Expression* getExpression() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class IfStatement : public Statement {
public:
  IfStatement(zc::Own<Expression> condition, zc::Own<Statement> thenStatement,
              zc::Maybe<zc::Own<Statement>> elseStatement = zc::none);
  ~IfStatement() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(IfStatement);

  const Expression* getCondition() const;
  const Statement* getThenStatement() const;
  const Statement* getElseStatement() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class WhileStatement : public Statement {
public:
  WhileStatement(zc::Own<Expression> condition, zc::Own<Statement> body);
  ~WhileStatement() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(WhileStatement);

  const Expression* getCondition() const;
  const Statement* getBody() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class ReturnStatement : public Statement {
public:
  explicit ReturnStatement(zc::Maybe<zc::Own<Expression>> expression = zc::none);
  ~ReturnStatement() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(ReturnStatement);

  const Expression* getExpression() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class EmptyStatement : public Statement {
public:
  EmptyStatement();
  ~EmptyStatement() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(EmptyStatement);
};

class ForStatement : public Statement {
public:
  ForStatement(zc::Maybe<zc::Own<Statement>> init, zc::Maybe<zc::Own<Expression>> condition,
               zc::Maybe<zc::Own<Expression>> update, zc::Own<Statement> body);
  ~ForStatement() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(ForStatement);

  const Statement* getInit() const;
  const Expression* getCondition() const;
  const Expression* getUpdate() const;
  const Statement* getBody() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class BreakStatement : public Statement {
public:
  explicit BreakStatement(zc::Maybe<zc::Own<Identifier>> label = zc::none);
  ~BreakStatement() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(BreakStatement);

  const Identifier* getLabel() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class ContinueStatement : public Statement {
public:
  explicit ContinueStatement(zc::Maybe<zc::Own<Identifier>> label = zc::none);
  ~ContinueStatement() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(ContinueStatement);

  const Identifier* getLabel() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class MatchStatement : public Statement {
public:
  MatchStatement(zc::Own<Expression> discriminant, zc::Vector<zc::Own<Statement>>&& clauses);
  ~MatchStatement() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(MatchStatement);

  const Expression* getDiscriminant() const;
  const NodeList<Statement>& getClauses() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class DebuggerStatement : public Statement {
public:
  DebuggerStatement();
  ~DebuggerStatement() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(DebuggerStatement);
};

// ImportDeclaration and ExportDeclaration are defined in module.h

class InterfaceDeclaration : public Statement {
public:
  InterfaceDeclaration(
      zc::Own<Identifier> name, zc::Vector<zc::Own<Statement>>&& members,
      zc::Vector<zc::Own<Identifier>>&& extends = zc::Vector<zc::Own<Identifier>>());
  ~InterfaceDeclaration() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(InterfaceDeclaration);

  const Identifier* getName() const;
  const NodeList<Statement>& getMembers() const;
  zc::ArrayPtr<const zc::Own<Identifier>> getExtends() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class StructDeclaration : public Statement {
public:
  StructDeclaration(zc::Own<Identifier> name, zc::Vector<zc::Own<Statement>>&& members);
  ~StructDeclaration() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(StructDeclaration);

  const Identifier* getName() const;
  const NodeList<Statement>& getMembers() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class EnumDeclaration : public Statement {
public:
  EnumDeclaration(zc::Own<Identifier> name, zc::Vector<zc::Own<Statement>>&& members);
  ~EnumDeclaration() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(EnumDeclaration);

  const Identifier* getName() const;
  const NodeList<Statement>& getMembers() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class ErrorDeclaration : public Statement {
public:
  ErrorDeclaration(zc::Own<Identifier> name, zc::Vector<zc::Own<Statement>>&& members);
  ~ErrorDeclaration() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(ErrorDeclaration);

  const Identifier* getName() const;
  const NodeList<Statement>& getMembers() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class AliasDeclaration : public Statement {
public:
  AliasDeclaration(zc::Own<Identifier> name, zc::Own<Type> type);
  ~AliasDeclaration() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(AliasDeclaration);

  const Identifier* getName() const;
  const Type* getType() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
