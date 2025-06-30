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

#include "zomlang/compiler/ast/statement.h"

#include "zc/core/memory.h"
#include "zc/core/string.h"
#include "zomlang/compiler/ast/expression.h"
#include "zomlang/compiler/ast/type.h"

namespace zomlang {
namespace compiler {
namespace ast {

// ================================================================================
// Statement
Statement::Statement(SyntaxKind kind) noexcept : Node(kind) {}
Statement::~Statement() noexcept(false) = default;

// ================================================================================
// VariableDeclaration::Impl

struct VariableDeclaration::Impl {
  const zc::Own<Identifier> name;
  const zc::Maybe<zc::Own<Type>> type;
  const zc::Maybe<zc::Own<Expression>> initializer;

  Impl(zc::Own<Identifier> n, zc::Maybe<zc::Own<Type>> t, zc::Maybe<zc::Own<Expression>> init)
      : name(zc::mv(n)), type(zc::mv(t)), initializer(zc::mv(init)) {}
};

// ================================================================================
// VariableDeclaration

VariableDeclaration::VariableDeclaration(zc::Own<Identifier> name, zc::Maybe<zc::Own<Type>> type,
                                         zc::Maybe<zc::Own<Expression>> initializer)
    : Statement(SyntaxKind::kVariableDeclaration),
      impl(zc::heap<Impl>(zc::mv(name), zc::mv(type), zc::mv(initializer))) {}

VariableDeclaration::~VariableDeclaration() noexcept(false) = default;

const Identifier* VariableDeclaration::getName() const { return impl->name.get(); }

const Type* VariableDeclaration::getType() const {
  return impl->type.map([](const zc::Own<Type>& t) { return t.get(); }).orDefault(nullptr);
}

const Expression* VariableDeclaration::getInitializer() const {
  return impl->initializer.map([](const zc::Own<Expression>& expr) { return expr.get(); })
      .orDefault(nullptr);
}

// ================================================================================
// FunctionDeclaration::Impl

struct FunctionDeclaration::Impl {
  const zc::Own<Identifier> name;
  const zc::Vector<zc::Own<Identifier>> parameters;
  const zc::Maybe<zc::Own<Type>> returnType;
  const zc::Own<Statement> body;

  Impl(zc::Own<Identifier> n, zc::Vector<zc::Own<Identifier>>&& p, zc::Maybe<zc::Own<Type>> r,
       zc::Own<Statement> b)
      : name(zc::mv(n)), parameters(zc::mv(p)), returnType(zc::mv(r)), body(zc::mv(b)) {}
};

// ================================================================================
// FunctionDeclaration

FunctionDeclaration::FunctionDeclaration(zc::Own<Identifier> name,
                                         zc::Vector<zc::Own<Identifier>>&& parameters,
                                         zc::Maybe<zc::Own<Type>> returnType,
                                         zc::Own<Statement> body)
    : Statement(SyntaxKind::kFunctionDeclaration),
      impl(zc::heap<Impl>(zc::mv(name), zc::mv(parameters), zc::mv(returnType), zc::mv(body))) {}

FunctionDeclaration::~FunctionDeclaration() noexcept(false) = default;

const Identifier* FunctionDeclaration::getName() const { return impl->name.get(); }

zc::ArrayPtr<const zc::Own<Identifier>> FunctionDeclaration::getParameters() const {
  return impl->parameters;
}

const Type* FunctionDeclaration::getReturnType() const {
  return impl->returnType.map([](const zc::Own<Type>& t) { return t.get(); }).orDefault(nullptr);
}

const Statement* FunctionDeclaration::getBody() const { return impl->body.get(); }

// ================================================================================
// ClassDeclaration::Impl

struct ClassDeclaration::Impl {
  const zc::Own<Identifier> name;
  const zc::Maybe<zc::Own<Identifier>> superClass;
  const NodeList<Statement> members;

  Impl(zc::Own<Identifier> n, zc::Maybe<zc::Own<Identifier>> s, zc::Vector<zc::Own<Statement>>&& m)
      : name(zc::mv(n)), superClass(zc::mv(s)), members(zc::mv(m)) {}
};

// ================================================================================
// ClassDeclaration

ClassDeclaration::ClassDeclaration(zc::Own<Identifier> name,
                                   zc::Maybe<zc::Own<Identifier>> superClass,
                                   zc::Vector<zc::Own<Statement>>&& members)
    : Statement(SyntaxKind::kClassDeclaration),
      impl(zc::heap<Impl>(zc::mv(name), zc::mv(superClass), zc::mv(members))) {}

ClassDeclaration::~ClassDeclaration() noexcept(false) = default;

const Identifier* ClassDeclaration::getName() const { return impl->name.get(); }

const Identifier* ClassDeclaration::getSuperClass() const {
  return impl->superClass.map([](const zc::Own<Identifier>& id) { return id.get(); })
      .orDefault(nullptr);
}

const NodeList<Statement>& ClassDeclaration::getMembers() const { return impl->members; }

// ================================================================================
// BlockStatement::Impl

struct BlockStatement::Impl {
  const NodeList<Statement> statements;

  explicit Impl(zc::Vector<zc::Own<Statement>>&& stmts) : statements(zc::mv(stmts)) {}
};

// ================================================================================
// BlockStatement

BlockStatement::BlockStatement(zc::Vector<zc::Own<Statement>>&& statements)
    : Statement(SyntaxKind::kBlockStatement), impl(zc::heap<Impl>(zc::mv(statements))) {}

BlockStatement::~BlockStatement() noexcept(false) = default;

const NodeList<Statement>& BlockStatement::getStatements() const { return impl->statements; }

// ================================================================================
// ExpressionStatement::Impl

struct ExpressionStatement::Impl {
  const zc::Own<Expression> expression;

  explicit Impl(zc::Own<Expression> expr) : expression(zc::mv(expr)) {}
};

// ================================================================================
// ExpressionStatement

ExpressionStatement::ExpressionStatement(zc::Own<Expression> expression)
    : Statement(SyntaxKind::kExpressionStatement), impl(zc::heap<Impl>(zc::mv(expression))) {}

ExpressionStatement::~ExpressionStatement() noexcept(false) = default;

const Expression* ExpressionStatement::getExpression() const { return impl->expression.get(); }

// ================================================================================
// IfStatement::Impl

struct IfStatement::Impl {
  const zc::Own<Expression> test;
  const zc::Own<Statement> consequent;
  const zc::Maybe<zc::Own<Statement>> alternate;

  Impl(zc::Own<Expression> t, zc::Own<Statement> c, zc::Maybe<zc::Own<Statement>> a)
      : test(zc::mv(t)), consequent(zc::mv(c)), alternate(zc::mv(a)) {}
};

// ================================================================================
// IfStatement

IfStatement::IfStatement(zc::Own<Expression> test, zc::Own<Statement> consequent,
                         zc::Maybe<zc::Own<Statement>> alternate)
    : Statement(SyntaxKind::kIfStatement),
      impl(zc::heap<Impl>(zc::mv(test), zc::mv(consequent), zc::mv(alternate))) {}

IfStatement::~IfStatement() noexcept(false) = default;

const Expression* IfStatement::getCondition() const { return impl->test.get(); }

const Statement* IfStatement::getThenStatement() const { return impl->consequent.get(); }

const Statement* IfStatement::getElseStatement() const {
  return impl->alternate.map([](const zc::Own<Statement>& stmt) { return stmt.get(); })
      .orDefault(nullptr);
}

// ================================================================================
// WhileStatement::Impl

struct WhileStatement::Impl {
  const zc::Own<Expression> test;
  const zc::Own<Statement> body;

  Impl(zc::Own<Expression> t, zc::Own<Statement> b) : test(zc::mv(t)), body(zc::mv(b)) {}
};

// ================================================================================
// WhileStatement

WhileStatement::WhileStatement(zc::Own<Expression> test, zc::Own<Statement> body)
    : Statement(SyntaxKind::kWhileStatement), impl(zc::heap<Impl>(zc::mv(test), zc::mv(body))) {}

WhileStatement::~WhileStatement() noexcept(false) = default;

const Expression* WhileStatement::getCondition() const { return impl->test.get(); }

const Statement* WhileStatement::getBody() const { return impl->body.get(); }

// ================================================================================
// ReturnStatement::Impl

struct ReturnStatement::Impl {
  const zc::Maybe<zc::Own<Expression>> argument;

  explicit Impl(zc::Maybe<zc::Own<Expression>> arg) : argument(zc::mv(arg)) {}
};

// ================================================================================
// ReturnStatement

ReturnStatement::ReturnStatement(zc::Maybe<zc::Own<Expression>> argument)
    : Statement(SyntaxKind::kReturnStatement), impl(zc::heap<Impl>(zc::mv(argument))) {}

ReturnStatement::~ReturnStatement() noexcept(false) = default;

const Expression* ReturnStatement::getExpression() const {
  return impl->argument.map([](const zc::Own<Expression>& expr) { return expr.get(); })
      .orDefault(nullptr);
}

// ================================================================================
// EmptyStatement

EmptyStatement::EmptyStatement() : Statement(SyntaxKind::kEmptyStatement) {}

EmptyStatement::~EmptyStatement() noexcept(false) = default;

// ================================================================================
// ForStatement::Impl

struct ForStatement::Impl {
  const zc::Maybe<zc::Own<Statement>> init;
  const zc::Maybe<zc::Own<Expression>> condition;
  const zc::Maybe<zc::Own<Expression>> update;
  const zc::Own<Statement> body;

  Impl(zc::Maybe<zc::Own<Statement>> i, zc::Maybe<zc::Own<Expression>> c,
       zc::Maybe<zc::Own<Expression>> u, zc::Own<Statement> b)
      : init(zc::mv(i)), condition(zc::mv(c)), update(zc::mv(u)), body(zc::mv(b)) {}
};

// ================================================================================
// ForStatement

ForStatement::ForStatement(zc::Maybe<zc::Own<Statement>> init,
                           zc::Maybe<zc::Own<Expression>> condition,
                           zc::Maybe<zc::Own<Expression>> update, zc::Own<Statement> body)
    : Statement(SyntaxKind::kForStatement),
      impl(zc::heap<Impl>(zc::mv(init), zc::mv(condition), zc::mv(update), zc::mv(body))) {}

ForStatement::~ForStatement() noexcept(false) = default;

const Statement* ForStatement::getInit() const {
  return impl->init.map([](const zc::Own<Statement>& stmt) { return stmt.get(); })
      .orDefault(nullptr);
}

const Expression* ForStatement::getCondition() const {
  return impl->condition.map([](const zc::Own<Expression>& expr) { return expr.get(); })
      .orDefault(nullptr);
}

const Expression* ForStatement::getUpdate() const {
  return impl->update.map([](const zc::Own<Expression>& expr) { return expr.get(); })
      .orDefault(nullptr);
}

const Statement* ForStatement::getBody() const { return impl->body.get(); }

// ================================================================================
// MatchStatement::Impl

struct MatchStatement::Impl {
  const zc::Own<Expression> discriminant;
  const NodeList<Statement> clauses;

  Impl(zc::Own<Expression> d, zc::Vector<zc::Own<Statement>>&& c)
      : discriminant(zc::mv(d)), clauses(zc::mv(c)) {}
};

// ================================================================================
// MatchStatement

MatchStatement::MatchStatement(zc::Own<Expression> discriminant,
                               zc::Vector<zc::Own<Statement>>&& clauses)
    : Statement(SyntaxKind::kMatchStatement),
      impl(zc::heap<Impl>(zc::mv(discriminant), zc::mv(clauses))) {}

MatchStatement::~MatchStatement() noexcept(false) = default;

const Expression* MatchStatement::getDiscriminant() const { return impl->discriminant.get(); }

const NodeList<Statement>& MatchStatement::getClauses() const { return impl->clauses; }

// ================================================================================
// AliasDeclaration::Impl

struct AliasDeclaration::Impl {
  const zc::Own<Identifier> name;
  const zc::Own<Type> type;

  Impl(zc::Own<Identifier> n, zc::Own<Type> t) : name(zc::mv(n)), type(zc::mv(t)) {}
};

// ================================================================================
// AliasDeclaration

AliasDeclaration::AliasDeclaration(zc::Own<Identifier> name, zc::Own<Type> type)
    : Statement(SyntaxKind::kAliasDeclaration), impl(zc::heap<Impl>(zc::mv(name), zc::mv(type))) {}

AliasDeclaration::~AliasDeclaration() noexcept(false) = default;

const Identifier* AliasDeclaration::getName() const { return impl->name.get(); }

const Type* AliasDeclaration::getType() const { return impl->type.get(); }

// ================================================================================
// DebuggerStatement

DebuggerStatement::DebuggerStatement() : Statement(SyntaxKind::kDebuggerStatement) {}

DebuggerStatement::~DebuggerStatement() noexcept(false) = default;

// ================================================================================
// BreakStatement::Impl

struct BreakStatement::Impl {
  const zc::Maybe<zc::Own<Identifier>> label;

  explicit Impl(zc::Maybe<zc::Own<Identifier>> l) : label(zc::mv(l)) {}
};

// ================================================================================
// BreakStatement

BreakStatement::BreakStatement(zc::Maybe<zc::Own<Identifier>> label)
    : Statement(SyntaxKind::kBreakStatement), impl(zc::heap<Impl>(zc::mv(label))) {}

BreakStatement::~BreakStatement() noexcept(false) = default;

const Identifier* BreakStatement::getLabel() const {
  return impl->label.map([](const zc::Own<Identifier>& id) { return id.get(); }).orDefault(nullptr);
}

// ================================================================================
// ContinueStatement::Impl

struct ContinueStatement::Impl {
  const zc::Maybe<zc::Own<Identifier>> label;

  explicit Impl(zc::Maybe<zc::Own<Identifier>> l) : label(zc::mv(l)) {}
};

// ================================================================================
// ContinueStatement

ContinueStatement::ContinueStatement(zc::Maybe<zc::Own<Identifier>> label)
    : Statement(SyntaxKind::kContinueStatement), impl(zc::heap<Impl>(zc::mv(label))) {}

ContinueStatement::~ContinueStatement() noexcept(false) = default;

const Identifier* ContinueStatement::getLabel() const {
  return impl->label.map([](const zc::Own<Identifier>& id) { return id.get(); }).orDefault(nullptr);
}

// ================================================================================
// InterfaceDeclaration::Impl

struct InterfaceDeclaration::Impl {
  const zc::Own<Identifier> name;
  const NodeList<Statement> members;
  const zc::Vector<zc::Own<Identifier>> extends;

  Impl(zc::Own<Identifier> n, zc::Vector<zc::Own<Statement>>&& m,
       zc::Vector<zc::Own<Identifier>>&& e)
      : name(zc::mv(n)), members(zc::mv(m)), extends(zc::mv(e)) {}
};

// ================================================================================
// InterfaceDeclaration

InterfaceDeclaration::InterfaceDeclaration(zc::Own<Identifier> name,
                                           zc::Vector<zc::Own<Statement>>&& members,
                                           zc::Vector<zc::Own<Identifier>>&& extends)
    : Statement(SyntaxKind::kInterfaceDeclaration),
      impl(zc::heap<Impl>(zc::mv(name), zc::mv(members), zc::mv(extends))) {}

InterfaceDeclaration::~InterfaceDeclaration() noexcept(false) = default;

const Identifier* InterfaceDeclaration::getName() const { return impl->name.get(); }

const NodeList<Statement>& InterfaceDeclaration::getMembers() const { return impl->members; }

zc::ArrayPtr<const zc::Own<Identifier>> InterfaceDeclaration::getExtends() const {
  return impl->extends;
}

// ================================================================================
// StructDeclaration::Impl

struct StructDeclaration::Impl {
  const zc::Own<Identifier> name;
  const NodeList<Statement> members;

  Impl(zc::Own<Identifier> n, zc::Vector<zc::Own<Statement>>&& m)
      : name(zc::mv(n)), members(zc::mv(m)) {}
};

// ================================================================================
// StructDeclaration

StructDeclaration::StructDeclaration(zc::Own<Identifier> name,
                                     zc::Vector<zc::Own<Statement>>&& members)
    : Statement(SyntaxKind::kStructDeclaration),
      impl(zc::heap<Impl>(zc::mv(name), zc::mv(members))) {}

StructDeclaration::~StructDeclaration() noexcept(false) = default;

const Identifier* StructDeclaration::getName() const { return impl->name.get(); }

const NodeList<Statement>& StructDeclaration::getMembers() const { return impl->members; }

// ================================================================================
// EnumDeclaration::Impl

struct EnumDeclaration::Impl {
  const zc::Own<Identifier> name;
  const NodeList<Statement> members;

  Impl(zc::Own<Identifier> n, zc::Vector<zc::Own<Statement>>&& m)
      : name(zc::mv(n)), members(zc::mv(m)) {}
};

// ================================================================================
// EnumDeclaration

EnumDeclaration::EnumDeclaration(zc::Own<Identifier> name, zc::Vector<zc::Own<Statement>>&& members)
    : Statement(SyntaxKind::kEnumDeclaration),
      impl(zc::heap<Impl>(zc::mv(name), zc::mv(members))) {}

EnumDeclaration::~EnumDeclaration() noexcept(false) = default;

const Identifier* EnumDeclaration::getName() const { return impl->name.get(); }

const NodeList<Statement>& EnumDeclaration::getMembers() const { return impl->members; }

// ================================================================================
// ErrorDeclaration::Impl

struct ErrorDeclaration::Impl {
  const zc::Own<Identifier> name;
  const NodeList<Statement> members;

  Impl(zc::Own<Identifier> n, zc::Vector<zc::Own<Statement>>&& m)
      : name(zc::mv(n)), members(zc::mv(m)) {}
};

// ================================================================================
// ErrorDeclaration

ErrorDeclaration::ErrorDeclaration(zc::Own<Identifier> name,
                                   zc::Vector<zc::Own<Statement>>&& members)
    : Statement(SyntaxKind::kErrorDeclaration),
      impl(zc::heap<Impl>(zc::mv(name), zc::mv(members))) {}

ErrorDeclaration::~ErrorDeclaration() noexcept(false) = default;

const Identifier* ErrorDeclaration::getName() const { return impl->name.get(); }

const NodeList<Statement>& ErrorDeclaration::getMembers() const { return impl->members; }

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
