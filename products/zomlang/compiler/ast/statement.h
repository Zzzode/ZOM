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
#include "zomlang/compiler/ast/classof.h"

namespace zomlang {
namespace compiler {
namespace ast {

class Identifier;
class Expression;
class Pattern;
class PropertyDeclaration;
class TypeNode;
class ReturnTypeNode;

class Statement : public Node {
public:
  ZC_DISALLOW_COPY_AND_MOVE(Statement);

  /// \brief Accept a visitor for traversal
  /// This is a pure virtual method that must be implemented by concrete Statement subclasses
  virtual void accept(Visitor& visitor) const = 0;

  /// \brief Get the syntax kind of this statement
  /// This is a pure virtual method that must be implemented by concrete Statement subclasses
  virtual SyntaxKind getKind() const = 0;

  GENERATE_CLASSOF_IMPL(Statement);

protected:
  Statement() noexcept = default;
};

// Base implementation class for Statement nodes
class StatementImpl {
public:
  StatementImpl(SyntaxKind kind, const Statement& stmt) noexcept;
  ~StatementImpl() noexcept(false) = default;

private:
  struct Impl;
  zc::Own<Impl> impl;
};

class IterationStatement : public Statement {
public:
  ZC_DISALLOW_COPY_AND_MOVE(IterationStatement);

  /// \brief Get the body of this iteration statement
  virtual const Statement& getBody() const = 0;

protected:
  IterationStatement() noexcept = default;
};

class IterationStatementImpl : public StatementImpl {
public:
  IterationStatementImpl(SyntaxKind kind, const IterationStatement& stmt) noexcept;
  ~IterationStatementImpl() noexcept(false) = default;

  const Statement& getBody() const;

private:
  struct Impl;
  zc::Own<Impl> impl;
};

#define ITERATION_STATEMENT_METHOD_DECL() \
  NODE_METHOD_DECLARE()                   \
  const Statement& getBody() const override;

/// \brief Base class for all named declarations
///
/// This intermediate class provides a common interface for declarations
/// that have names, eliminating the need for static_cast in name extraction.
class Declaration {
public:
  ZC_DISALLOW_COPY_AND_MOVE(Declaration);

  /// \brief Get the symbol associated with this declaration
  virtual zc::Maybe<const symbol::Symbol&> getSymbol() const = 0;

  /// \brief Set the symbol associated with this declaration
  virtual void setSymbol(zc::Maybe<const symbol::Symbol&> symbol) = 0;

  GENERATE_CLASSOF_IMPL(Declaration);

protected:
  Declaration() noexcept = default;
};

class DeclarationImpl {
public:
  DeclarationImpl() noexcept;
  ~DeclarationImpl() noexcept(false) = default;

  /// \brief Get the symbol associated with this declaration
  zc::Maybe<const symbol::Symbol&> getSymbol() const;

  /// \brief Set the symbol associated with this declaration
  void setSymbol(zc::Maybe<const symbol::Symbol&> symbol);

private:
  struct Impl;
  zc::Own<Impl> impl;
};

#define DECLARATION_METHOD_DECL()                              \
  zc::Maybe<const symbol::Symbol&> getSymbol() const override; \
  void setSymbol(zc::Maybe<const symbol::Symbol&> symbol) override;

class NamedDeclaration : public Declaration {
public:
  ZC_DISALLOW_COPY_AND_MOVE(NamedDeclaration);

  /// \brief Get the name of this declaration
  virtual const Identifier& getName() const = 0;

  GENERATE_CLASSOF_IMPL(NamedDeclaration);

protected:
  NamedDeclaration() noexcept = default;
};

class NamedDeclarationImpl : public DeclarationImpl {
public:
  NamedDeclarationImpl(zc::Own<ast::Identifier> name) noexcept;
  ~NamedDeclarationImpl() noexcept(false) = default;

  const Identifier& getName() const;

private:
  struct Impl;
  zc::Own<Impl> impl;
};

#define NAMED_DECLARATION_METHOD_DECL() \
  DECLARATION_METHOD_DECL()             \
  const Identifier& getName() const override;

class DeclarationStatement : public NamedDeclaration, public Statement {
public:
  ZC_DISALLOW_COPY_AND_MOVE(DeclarationStatement);

protected:
  DeclarationStatement() noexcept = default;
};

class BindingElement final : public NamedDeclaration, public Node {
public:
  BindingElement(zc::Own<Identifier> name, zc::Maybe<zc::Own<TypeNode>> type = zc::none,
                 zc::Maybe<zc::Own<Expression>> initializer = zc::none) noexcept;
  ~BindingElement() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(BindingElement);

  /// \brief Get the type annotation for this binding element
  zc::Maybe<const TypeNode&> getType() const;

  /// \brief Get the initializer expression for this binding element
  zc::Maybe<const Expression&> getInitializer() const;

  NODE_METHOD_DECLARE();
  NAMED_DECLARATION_METHOD_DECL();

private:
  struct Impl;
  zc::Own<Impl> impl;
};

class ParameterDeclaration final : public NamedDeclaration, public Node {
public:
  explicit ParameterDeclaration(zc::Own<Identifier> name,
                                zc::Maybe<zc::Own<TypeNode>> type = zc::none,
                                zc::Maybe<zc::Own<Expression>> initializer = zc::none) noexcept;
  ~ParameterDeclaration() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(ParameterDeclaration);

  /// \brief Get the type annotation for this parameter declaration
  zc::Maybe<const TypeNode&> getType() const;

  /// \brief Get the initializer expression for this parameter declaration
  zc::Maybe<const Expression&> getInitializer() const;

  NODE_METHOD_DECLARE();
  NAMED_DECLARATION_METHOD_DECL();

private:
  struct Impl;
  zc::Own<Impl> impl;
};

class VariableDeclaration final : public NamedDeclaration, public Node {
public:
  VariableDeclaration(zc::Own<ast::Identifier> name, zc::Maybe<zc::Own<ast::TypeNode>> type,
                      zc::Own<ast::Expression> initializer) noexcept;
  ~VariableDeclaration() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(VariableDeclaration);

  /// \brief Get the type annotation for this variable declaration
  zc::Maybe<const TypeNode&> getType() const;

  /// \brief Get the initializer expression for this variable declaration
  zc::Maybe<const Expression&> getInitializer() const;

  NODE_METHOD_DECLARE();
  NAMED_DECLARATION_METHOD_DECL();

private:
  struct Impl;
  zc::Own<Impl> impl;
};

class VariableDeclarationList final : public Node {
public:
  VariableDeclarationList(zc::Vector<zc::Own<BindingElement>>&& bindings) noexcept;
  ~VariableDeclarationList() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(VariableDeclarationList);

  const NodeList<BindingElement>& getBindings() const;

  NODE_METHOD_DECLARE();

private:
  struct Impl;
  zc::Own<Impl> impl;
};

class VariableStatement final : public Statement {
public:
  VariableStatement(zc::Own<VariableDeclarationList> declarations) noexcept;
  ~VariableStatement() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(VariableStatement);

  const VariableDeclarationList& getDeclarations() const;

  NODE_METHOD_DECLARE();

private:
  struct Impl;
  zc::Own<Impl> impl;
};

class TypeParameterDeclaration final : public NamedDeclaration, public Node {
public:
  TypeParameterDeclaration(zc::Own<Identifier> name,
                           zc::Maybe<zc::Own<TypeNode>> constraint = zc::none) noexcept;
  ~TypeParameterDeclaration() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(TypeParameterDeclaration);

  /// \brief Get the constraint type of this type parameter declaration
  zc::Maybe<const TypeNode&> getConstraint() const;

  NODE_METHOD_DECLARE();
  NAMED_DECLARATION_METHOD_DECL();

private:
  struct Impl;
  zc::Own<Impl> impl;
};

class FunctionDeclaration final : public DeclarationStatement, public LocalsContainer {
public:
  FunctionDeclaration(zc::Own<Identifier> name,
                      zc::Vector<zc::Own<TypeParameterDeclaration>>&& typeParameters,
                      zc::Vector<zc::Own<BindingElement>>&& parameters,
                      zc::Maybe<zc::Own<ReturnTypeNode>> returnType,
                      zc::Own<Statement> body) noexcept;
  ~FunctionDeclaration() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(FunctionDeclaration);

  /// \brief Get the type parameters of this function
  const NodeList<TypeParameterDeclaration>& getTypeParameters() const;

  /// \brief Get the parameters of this function
  const NodeList<BindingElement>& getParameters() const;

  /// \brief Get the return type of this function
  zc::Maybe<const ReturnTypeNode&> getReturnType() const;

  /// \brief Get the body of this function
  const Statement& getBody() const;

  NODE_METHOD_DECLARE();
  NAMED_DECLARATION_METHOD_DECL();
  LOCALS_CONTAINER_METHOD_DECL();

private:
  struct Impl;
  zc::Own<Impl> impl;
};

class ClassElement : public NamedDeclaration {
public:
  ZC_DISALLOW_COPY_AND_MOVE(ClassElement);

protected:
  ClassElement() noexcept = default;
};

class InterfaceElement : public NamedDeclaration {
public:
  ZC_DISALLOW_COPY_AND_MOVE(InterfaceElement);

protected:
  InterfaceElement() noexcept = default;
};

class ClassDeclaration final : public DeclarationStatement {
public:
  ClassDeclaration(zc::Own<Identifier> name,
                   zc::Vector<zc::Own<TypeParameterDeclaration>>&& typeParameters,
                   zc::Maybe<zc::Vector<zc::Own<HeritageClause>>> heritageClauses,
                   zc::Vector<zc::Own<ClassElement>>&& members) noexcept;
  ~ClassDeclaration() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(ClassDeclaration);

  const NodeList<TypeParameterDeclaration>& getTypeParameters() const;
  zc::Maybe<const zc::Vector<zc::Own<HeritageClause>>&> getHeritageClauses() const;
  const NodeList<ClassElement>& getMembers() const;

  NODE_METHOD_DECLARE();
  NAMED_DECLARATION_METHOD_DECL();

private:
  struct Impl;
  zc::Own<Impl> impl;
};

class HeritageClause final : public Node {
public:
  HeritageClause(ast::SyntaxKind token,
                 zc::Vector<zc::Own<ExpressionWithTypeArguments>>&& types) noexcept;
  ~HeritageClause() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(HeritageClause);

  ast::SyntaxKind getToken() const;
  const zc::Vector<zc::Own<ExpressionWithTypeArguments>>& getTypes() const;

  NODE_METHOD_DECLARE();

private:
  struct Impl;
  zc::Own<Impl> impl;
};

class BlockStatement final : public Statement, public LocalsContainer {
public:
  explicit BlockStatement(zc::Vector<zc::Own<Statement>>&& statements) noexcept;
  ~BlockStatement() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(BlockStatement);

  const NodeList<Statement>& getStatements() const;

  NODE_METHOD_DECLARE();
  LOCALS_CONTAINER_METHOD_DECL();

private:
  struct Impl;
  zc::Own<Impl> impl;
};

class ExpressionStatement final : public Statement {
public:
  explicit ExpressionStatement(zc::Own<Expression> expression) noexcept;
  ~ExpressionStatement() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(ExpressionStatement);

  const Expression& getExpression() const;

  NODE_METHOD_DECLARE();

private:
  struct Impl;
  zc::Own<Impl> impl;
};

class IfStatement final : public Statement {
public:
  IfStatement(zc::Own<Expression> condition, zc::Own<Statement> thenStatement,
              zc::Maybe<zc::Own<Statement>> elseStatement = zc::none) noexcept;
  ~IfStatement() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(IfStatement);

  const Expression& getCondition() const;

  const Statement& getThenStatement() const;

  zc::Maybe<const Statement&> getElseStatement() const;

  NODE_METHOD_DECLARE();

private:
  struct Impl;
  zc::Own<Impl> impl;
};

class WhileStatement final : public IterationStatement {
public:
  WhileStatement(zc::Own<Expression> condition, zc::Own<Statement> body) noexcept;
  ~WhileStatement() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(WhileStatement);

  const Expression& getCondition() const;

  ITERATION_STATEMENT_METHOD_DECL();

private:
  struct Impl;
  zc::Own<Impl> impl;
};

class ReturnStatement final : public Statement {
public:
  explicit ReturnStatement(zc::Maybe<zc::Own<Expression>> expression = zc::none) noexcept;
  ~ReturnStatement() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(ReturnStatement);

  zc::Maybe<const Expression&> getExpression() const;

  NODE_METHOD_DECLARE();

private:
  struct Impl;
  zc::Own<Impl> impl;
};

class EmptyStatement final : public Statement {
public:
  EmptyStatement() noexcept;
  ~EmptyStatement() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(EmptyStatement);

  NODE_METHOD_DECLARE();

private:
  struct Impl;
  zc::Own<Impl> impl;
};

class ForStatement final : public IterationStatement, public LocalsContainer {
public:
  ForStatement(zc::Maybe<zc::Own<Statement>> init, zc::Maybe<zc::Own<Expression>> condition,
               zc::Maybe<zc::Own<Expression>> update, zc::Own<Statement> body) noexcept;
  ~ForStatement() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(ForStatement);

  zc::Maybe<const Expression&> getInitializer() const;
  zc::Maybe<const Expression&> getCondition() const;
  zc::Maybe<const Expression&> getUpdate() const;

  ITERATION_STATEMENT_METHOD_DECL();
  LOCALS_CONTAINER_METHOD_DECL();

private:
  struct Impl;
  zc::Own<Impl> impl;
};

class BreakStatement final : public Statement {
public:
  explicit BreakStatement(zc::Maybe<zc::Own<Identifier>> label = zc::none) noexcept;
  ~BreakStatement() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(BreakStatement);

  zc::Maybe<const Identifier&> getLabel() const;

  NODE_METHOD_DECLARE();

private:
  struct Impl;
  zc::Own<Impl> impl;
};

class ContinueStatement final : public Statement {
public:
  explicit ContinueStatement(zc::Maybe<zc::Own<Identifier>> label = zc::none) noexcept;
  ~ContinueStatement() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(ContinueStatement);

  zc::Maybe<const Identifier&> getLabel() const;

  NODE_METHOD_DECLARE();

private:
  struct Impl;
  zc::Own<Impl> impl;
};

class MatchStatement final : public Statement {
public:
  MatchStatement(zc::Own<Expression> discriminant,
                 zc::Vector<zc::Own<Statement>>&& clauses) noexcept;
  ~MatchStatement() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(MatchStatement);

  const Expression& getDiscriminant() const;
  const NodeList<Statement>& getClauses() const;

  NODE_METHOD_DECLARE();

private:
  struct Impl;
  zc::Own<Impl> impl;
};

class DebuggerStatement final : public Statement {
public:
  DebuggerStatement() noexcept;
  ~DebuggerStatement() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(DebuggerStatement);

  NODE_METHOD_DECLARE();

private:
  struct Impl;
  zc::Own<Impl> impl;
};

// ImportDeclaration and ExportDeclaration are defined in module.h

class InterfaceDeclaration final : public DeclarationStatement {
public:
  InterfaceDeclaration(zc::Own<Identifier> name,
                       zc::Vector<zc::Own<TypeParameterDeclaration>>&& typeParameters,
                       zc::Maybe<zc::Vector<zc::Own<HeritageClause>>> heritageClauses,
                       zc::Vector<zc::Own<InterfaceElement>>&& members) noexcept;
  ~InterfaceDeclaration() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(InterfaceDeclaration);

  const NodeList<InterfaceElement>& getMembers() const;
  zc::ArrayPtr<const zc::Own<Identifier>> getExtends() const;

  NODE_METHOD_DECLARE();
  NAMED_DECLARATION_METHOD_DECL();

private:
  struct Impl;
  zc::Own<Impl> impl;
};

class StructDeclaration final : public DeclarationStatement {
public:
  StructDeclaration(zc::Own<Identifier> name, zc::Vector<zc::Own<Statement>>&& members) noexcept;
  ~StructDeclaration() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(StructDeclaration);

  const NodeList<Statement>& getMembers() const;

  NODE_METHOD_DECLARE();
  NAMED_DECLARATION_METHOD_DECL();

private:
  struct Impl;
  zc::Own<Impl> impl;
};

class EnumDeclaration final : public DeclarationStatement {
public:
  EnumDeclaration(zc::Own<Identifier> name, zc::Vector<zc::Own<Statement>>&& members) noexcept;
  ~EnumDeclaration() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(EnumDeclaration);

  const NodeList<Statement>& getMembers() const;

  NODE_METHOD_DECLARE();
  NAMED_DECLARATION_METHOD_DECL();

private:
  struct Impl;
  zc::Own<Impl> impl;
};

class ErrorDeclaration final : public DeclarationStatement {
public:
  ErrorDeclaration(zc::Own<Identifier> name, zc::Vector<zc::Own<Statement>>&& members) noexcept;
  ~ErrorDeclaration() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(ErrorDeclaration);

  const NodeList<Statement>& getMembers() const;

  NODE_METHOD_DECLARE();
  NAMED_DECLARATION_METHOD_DECL();

private:
  struct Impl;
  zc::Own<Impl> impl;
};

class AliasDeclaration final : public NamedDeclaration, public Statement {
public:
  AliasDeclaration(zc::Own<Identifier> name, zc::Own<TypeNode> type) noexcept;
  ~AliasDeclaration() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(AliasDeclaration);

  const TypeNode& getType() const;

  NODE_METHOD_DECLARE();
  NAMED_DECLARATION_METHOD_DECL();

private:
  struct Impl;
  zc::Own<Impl> impl;
};

class MatchClause final : public Statement {
public:
  MatchClause(zc::Own<Pattern> pattern, zc::Maybe<zc::Own<Expression>> guard,
              zc::Own<Statement> body) noexcept;
  ~MatchClause() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(MatchClause);

  const Pattern& getPattern() const;
  zc::Maybe<const Expression&> getGuard() const;
  const Statement& getBody() const;

  NODE_METHOD_DECLARE();

private:
  struct Impl;
  zc::Own<Impl> impl;
};

class DefaultClause final : public Statement {
public:
  explicit DefaultClause(zc::Vector<zc::Own<Statement>>&& statements) noexcept;
  ~DefaultClause() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(DefaultClause);

  const NodeList<Statement>& getStatements() const;

  NODE_METHOD_DECLARE();

private:
  struct Impl;
  zc::Own<Impl> impl;
};

class BindingPattern : public Node {
public:
  ZC_DISALLOW_COPY_AND_MOVE(BindingPattern);

  virtual const NodeList<BindingElement>& getElements() const = 0;

  GENERATE_CLASSOF_IMPL(BindingPattern);

protected:
  BindingPattern() noexcept = default;
};

#define BINDING_PATTERN_METHOD_DECLARE() \
  const NodeList<BindingElement>& getElements() const override;

class ArrayBindingPattern final : public BindingPattern {
public:
  explicit ArrayBindingPattern(zc::Vector<zc::Own<BindingElement>>&& elements) noexcept;
  ~ArrayBindingPattern() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(ArrayBindingPattern);

  NODE_METHOD_DECLARE();
  BINDING_PATTERN_METHOD_DECLARE();

private:
  struct Impl;
  zc::Own<Impl> impl;
};

class ObjectBindingPattern final : public BindingPattern {
public:
  explicit ObjectBindingPattern(zc::Vector<zc::Own<BindingElement>>&& properties) noexcept;
  ~ObjectBindingPattern() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(ObjectBindingPattern);

  const NodeList<BindingElement>& getProperties() const;

  NODE_METHOD_DECLARE();
  BINDING_PATTERN_METHOD_DECLARE();

private:
  struct Impl;
  zc::Own<Impl> impl;
};

class PropertySignature final : public InterfaceElement, public Node {
public:
  PropertySignature(zc::Own<Identifier> name, bool optional,
                    zc::Maybe<zc::Own<TypeNode>> type = zc::none,
                    zc::Maybe<zc::Own<Expression>> initializer = zc::none) noexcept;
  ~PropertySignature() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(PropertySignature);

  bool isOptional() const;

  zc::Maybe<const TypeNode&> getType() const;
  zc::Maybe<const Expression&> getInitializer() const;

  NODE_METHOD_DECLARE();
  NAMED_DECLARATION_METHOD_DECL();

private:
  struct Impl;
  zc::Own<Impl> impl;
};

class MethodSignature final : public InterfaceElement, public LocalsContainer, public Node {
public:
  MethodSignature(zc::Own<Identifier> name, bool optional,
                  zc::Vector<zc::Own<TypeParameterDeclaration>>&& typeParameters,
                  zc::Vector<zc::Own<BindingElement>>&& parameters,
                  zc::Maybe<zc::Own<ReturnTypeNode>> returnType = zc::none) noexcept;
  ~MethodSignature() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(MethodSignature);

  bool isOptional() const;
  const NodeList<TypeParameterDeclaration>& getTypeParameters() const;
  const NodeList<BindingElement>& getParameters() const;
  zc::Maybe<const ReturnTypeNode&> getReturnType() const;

  NODE_METHOD_DECLARE();
  NAMED_DECLARATION_METHOD_DECL();
  LOCALS_CONTAINER_METHOD_DECL();

private:
  struct Impl;
  zc::Own<Impl> impl;
};

class SemicolonClassElement final : public ClassElement, public Node {
public:
  SemicolonClassElement() noexcept;
  ~SemicolonClassElement() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(SemicolonClassElement);

  NODE_METHOD_DECLARE();
  NAMED_DECLARATION_METHOD_DECL();

private:
  struct Impl;
  zc::Own<Impl> impl;
};

class PropertyDeclaration final : public ClassElement, public Node {
public:
  PropertyDeclaration(zc::Own<Identifier> name, zc::Maybe<zc::Own<TypeNode>> type,
                      zc::Maybe<zc::Own<Expression>> initializer = zc::none) noexcept;
  ~PropertyDeclaration() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(PropertyDeclaration);

  zc::Maybe<const TypeNode&> getType() const;
  zc::Maybe<const Expression&> getInitializer() const;

  NODE_METHOD_DECLARE();
  NAMED_DECLARATION_METHOD_DECL();

private:
  struct Impl;
  zc::Own<Impl> impl;
};

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
