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
#include "zomlang/compiler/ast/node-id.h"

namespace zomlang {
namespace compiler {

namespace ast {
class Tree;
class BindingMetadata;
}  // namespace ast

namespace diagnostics {
class DiagnosticEngine;
}  // namespace diagnostics

namespace symbol {
class Symbol;
class Scope;
class SymbolTable;
}  // namespace symbol

namespace type {
class ConstraintSet;
class Type;
class TypeEnv;
class UnificationEngine;
}  // namespace type

namespace checker {

/// \brief BodyChecker - Phase B of the type checker.
///
/// Performs body checking for all declarations in the compilation unit.
/// This is the second phase of the two-phase type checking process as
/// specified in RFC 0005:
///
///   Phase A: Signature Computation (DeclSignatureComputer)
///   Phase B: Body Checking (BodyChecker)
///
/// Phase B requires that all type signatures have been computed in Phase A.
/// It then walks function bodies, expressions, and statements to:
/// 1. Infer types for all expressions
/// 2. Check statement type correctness
/// 3. Validate return statements against expected return types
/// 4. Verify assignment compatibility
///
/// The body checker uses the UnificationEngine for type constraint solving
/// and records all inferred types in the TypeEnv keyed by NodeId.
///
/// Expression type inference rules (RFC 0005):
/// - Integer literals -> i32 (default), context-dependent widening
/// - Float literals -> f64 (default)
/// - String literals -> str
/// - Boolean literals -> bool
/// - Binary operations -> operand types determine result
/// - Function calls -> return type of callee
/// - Identifiers -> type of the referenced symbol
class BodyChecker final {
public:
  /// \brief Construct a BodyChecker.
  ///
  /// \param typeEnv   The type environment for storing inferred types.
  /// \param unifier   The unification engine for type constraint solving.
  /// \param symbols   The symbol table (populated by the binder).
  /// \param tree      The immutable AST tree to process.
  /// \param metadata  Binding metadata from the binder (symbol/scope bindings).
  /// \param diags     Diagnostic engine for error reporting.
  BodyChecker(type::TypeEnv& typeEnv, type::UnificationEngine& unifier,
              type::ConstraintSet& constraints, symbol::SymbolTable& symbols, const ast::Tree& tree,
              const ast::BindingMetadata& metadata, diagnostics::DiagnosticEngine& diags) noexcept;

  ~BodyChecker() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(BodyChecker);

  /// \brief Run Phase B: check all function/initializer bodies.
  ///
  /// Walks the AST and for each function body, variable initializer,
  /// and expression statement:
  /// 1. Infers types for all sub-expressions
  /// 2. Validates statement correctness
  /// 3. Checks return statements
  /// 4. Verifies assignment compatibility
  ///
  /// \return true if no fatal errors were produced.
  bool checkBodies();

  /// \brief Return constraints emitted by body checking.
  const type::ConstraintSet& getConstraints() const;

private:
  struct Impl;
  zc::Own<Impl> impl;

  // ==========================================================================
  // Expression type inference
  // ==========================================================================

  /// \brief Infer the type of an expression, dispatching by SyntaxKind.
  const type::Type& checkExpr(ast::NodeId expr);

  /// \brief Infer type of an identifier expression.
  ///
  /// Looks up the identifier in the symbol table and returns its type.
  const type::Type& checkIdentExpr(ast::NodeId expr);

  /// \brief Infer type of a literal expression.
  ///
  /// Handles IntLiteral, FloatLiteralExpr, StrLiteral, BoolLiteral,
  /// NullLiteral, UnitLiteral, BigIntLiteral.
  const type::Type& checkLiteral(ast::NodeId expr);

  /// \brief Infer type of a binary expression.
  ///
  /// Checks both operands and determines result type based on operator:
  /// - Arithmetic (+, -, *, /, %) -> operand type (with numeric widening)
  /// - Comparison (==, !=, <, >, <=, >=) -> bool
  /// - Logical (&&, ||) -> bool
  /// - Bitwise (&, |, ^, <<, >>) -> operand type
  const type::Type& checkBinaryExpr(ast::NodeId expr);

  /// \brief Infer type of a unary expression.
  ///
  /// Handles prefix operators: +, -, !, ~, *, &, ++, --
  const type::Type& checkUnaryExpr(ast::NodeId expr);

  /// \brief Infer type of a postfix expression.
  ///
  /// Handles postfix ++, --, ?!, and !!.
  const type::Type& checkPostfixExpr(ast::NodeId expr);

  /// \brief Infer type of a function call expression.
  ///
  /// Checks callee type, validates argument count and types,
  /// returns the callee's return type.
  const type::Type& checkCallExpr(ast::NodeId expr);

  /// \brief Infer type of a member access expression (obj.prop).
  ///
  /// Looks up the property in the object type's members.
  const type::Type& checkMemberExpr(ast::NodeId expr);

  /// \brief Infer type of an index expression (obj[index]).
  ///
  /// For arrays: returns element type, index must be integer.
  /// For tuples: returns element type at constant index.
  const type::Type& checkIndexExpr(ast::NodeId expr);

  /// \brief Infer type of a new expression (new Class(args)).
  ///
  /// Returns the named type of the constructed class.
  const type::Type& checkNewExpr(ast::NodeId expr);

  /// \brief Infer type of a cast expression (expr as Type).
  ///
  /// Validates that the cast is valid and returns the target type.
  const type::Type& checkCastExpr(ast::NodeId expr);

  /// \brief Infer type of a conditional expression (cond ? then : else).
  ///
  /// Condition must be bool. Result is the join of then and else types.
  const type::Type& checkConditionalExpr(ast::NodeId expr);

  /// \brief Infer type of an assignment expression.
  ///
  /// Checks LHS is assignable, RHS type is compatible.
  /// Returns the RHS type.
  const type::Type& checkAssignmentExpr(ast::NodeId expr);

  /// \brief Infer type of a lambda expression.
  ///
  /// Returns a FunctionType inferred from parameters and body.
  const type::Type& checkLambdaExpr(ast::NodeId expr);

  /// \brief Infer type of a function expression (anonymous function).
  ///
  /// Returns a FunctionType from the explicit signature and body.
  const type::Type& checkFunctionExpr(ast::NodeId expr);

  /// \brief Infer type of an object literal ({ x: 1, y: 2 }).
  ///
  /// Returns an ObjectType with member types.
  const type::Type& checkObjectLiteral(ast::NodeId expr);

  /// \brief Infer type of a struct literal.
  const type::Type& checkStructLiteralExpr(ast::NodeId expr);

  /// \brief Infer type of an array literal ([1, 2, 3]).
  ///
  /// Returns an ArrayType with the element type being the join
  /// of all element types.
  const type::Type& checkArrayLiteral(ast::NodeId expr);

  /// \brief Infer type of a tuple literal ((1, "two", 3.0)).
  ///
  /// Returns a TupleType with the element types.
  const type::Type& checkTupleLiteral(ast::NodeId expr);

  /// \brief Infer type of an is-expression (expr is Type).
  ///
  /// Returns bool. Validates the type test is meaningful.
  const type::Type& checkIsExpr(ast::NodeId expr);

  /// \brief Infer type of an as-expression (expr as Type).
  ///
  /// Alias for checkCastExpr.
  const type::Type& checkAsExpr(ast::NodeId expr);

  /// \brief Infer type of a this expression.
  ///
  /// Returns the enclosing class type.
  const type::Type& checkThisExpr(ast::NodeId expr);

  /// \brief Infer type of a super expression.
  ///
  /// Returns the superclass type.
  const type::Type& checkSuperExpr(ast::NodeId expr);

  // ==========================================================================
  // Statement checking
  // ==========================================================================

  /// \brief Check a statement, dispatching by SyntaxKind.
  void checkStmt(ast::NodeId stmt);

  /// \brief Check a block statement { stmts... }.
  ///
  /// Creates a new scope and checks all contained statements.
  void checkBlockStmt(ast::NodeId stmt);

  /// \brief Check an if statement.
  ///
  /// Condition must be bool. Then and else branches are checked.
  void checkIfStmt(ast::NodeId stmt);

  /// \brief Check a while statement.
  ///
  /// Condition must be bool. Body is checked.
  void checkWhileStmt(ast::NodeId stmt);

  /// \brief Check a for statement (C-style for loop).
  ///
  /// Init, condition, update, and body are checked.
  void checkForStmt(ast::NodeId stmt);

  /// \brief Check a return statement.
  ///
  /// Validates the return value type matches the expected return type.
  void checkReturnStmt(ast::NodeId stmt);

  /// \brief Check a let/var declaration statement.
  ///
  /// Infers type from initializer and checks against declared type.
  void checkLetStmt(ast::NodeId stmt);

  /// \brief Check a match statement.
  ///
  /// Checks scrutinee type and all pattern arms.
  void checkMatchStmt(ast::NodeId stmt);

  /// \brief Check a function declaration body.
  void checkFunctionDecl(ast::NodeId decl);

  // ==========================================================================
  // Helpers
  // ==========================================================================

  /// \brief Check if source type is assignable to target type.
  ///
  /// Reports a diagnostic if assignment is not valid.
  /// Uses unification and subtype checking per RFC 0005 rules.
  void checkAssignable(const type::Type& target, const type::Type& source, ast::NodeId node,
                       ast::NodeId coercionSite = ast::NodeId());

  /// \brief Get the expected return type for the current function context.
  const type::Type& expectedReturnType() const;

  /// \brief Set the expected return type for the current function context.
  void setExpectedReturnType(const type::Type& ty);

  /// \brief Report an error diagnostic at the given node.
  void reportError(ast::NodeId node, zc::StringPtr message);

  /// \brief Report a type mismatch diagnostic.
  void reportTypeMismatch(ast::NodeId node, const type::Type& expected, const type::Type& actual);

  /// \brief Report an equality-unification diagnostic.
  void reportCannotUnify(ast::NodeId node, const type::Type& expected, const type::Type& actual,
                         zc::StringPtr context);

  /// \brief Report an infinite-type diagnostic.
  void reportInfiniteType(ast::NodeId node, zc::StringPtr description);

  /// \brief Look up a symbol by name in the current scope chain.
  zc::Maybe<symbol::Symbol&> lookupSymbol(zc::StringPtr name);

  /// \brief Get the current scope from the symbol table.
  const symbol::Scope& currentScope();

  /// \brief Get the type associated with a symbol.
  ///
  /// Returns the type from TypeEnv if the symbol has a declaration node,
  /// or from the symbol's TypeSymbol if available.
  /// Returns an empty Maybe if no type can be determined.
  zc::Maybe<const type::Type&> getSymbolType(symbol::Symbol& sym);

  /// \brief Store an inferred type for an expression node.
  ///
  /// Takes ownership of the type and stores it in the TypeEnv.
  /// Returns a reference to the stored type.
  const type::Type& storeType(ast::NodeId node, zc::Own<type::Type> ty);

  /// \brief Get or create the error type singleton.
  const type::Type& errorType();

  /// \brief Clone a type, creating a new owned copy.
  zc::Own<type::Type> cloneType(const type::Type& ty);

  /// \brief Bind instantiated generic type variables by parameter name.
  void bindTypeVarsByName(const type::Type& ty, zc::StringPtr name, const type::Type& value);

  /// \brief Find an instantiated generic type variable by parameter name.
  zc::Maybe<const type::Type&> findTypeVarByName(const type::Type& ty, zc::StringPtr name);

  /// \brief Resolve a type expression AST node to a concrete type.
  ///
  /// Handles NamedTypeExpr (both simple IdentExpr paths and qualified
  /// ModulePath paths) and maps primitive type names to PrimitiveType.
  zc::Own<type::Type> resolveTypeExpr(ast::NodeId tyExpr);

  /// \brief Create a PrimitiveType from a type name string.
  ///
  /// Returns an empty zc::Own if the name does not correspond to a primitive type.
  zc::Own<type::Type> makePrimitiveType(zc::StringPtr name);
};

}  // namespace checker
}  // namespace compiler
}  // namespace zomlang
