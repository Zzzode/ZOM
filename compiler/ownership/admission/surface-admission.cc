// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "compiler/ownership/admission/surface-admission.h"

#include "compiler/ast/generated/node-payload.h"
#include "compiler/ast/generated/node-traverse.h"

namespace zomlang::compiler::ownership {
namespace {

bool occursAfter(const SurfaceFailure& left, const SurfaceFailure& right) noexcept {
  if (left.primarySpan.byteStart() != right.primarySpan.byteStart()) {
    return left.primarySpan.byteStart() > right.primarySpan.byteStart();
  }
  if (left.primarySpan.byteEnd() != right.primarySpan.byteEnd()) {
    return left.primarySpan.byteEnd() > right.primarySpan.byteEnd();
  }
  if (left.traversalOrdinal != right.traversalOrdinal) {
    return left.traversalOrdinal > right.traversalOrdinal;
  }
  return static_cast<uint8_t>(left.kind) > static_cast<uint8_t>(right.kind);
}

void insertFailure(zc::Vector<SurfaceFailure>& failures, SurfaceFailure&& failure) {
  failures.add(zc::mv(failure));
  for (size_t index = failures.size() - 1;
       index != 0 && occursAfter(failures[index - 1], failures[index]); --index) {
    auto prior = zc::mv(failures[index - 1]);
    failures[index - 1] = zc::mv(failures[index]);
    failures[index] = zc::mv(prior);
  }
}

bool isAdmittedPrimitiveBinary(const ast::Tree& tree, ast::NodeId value);
bool isAdmittedFieldComparisonBinary(const ast::Tree& tree, ast::NodeId value,
                                     ast::NodeId receiver);
bool isAdmittedReceiverCall(const ast::Tree& tree, ast::NodeId expression);

// A compound assignment operator that desugars to a primitive binary write
// (`x += 1` -> `x = x + 1`). The ten arithmetic, remainder, bitwise, and
// shift compound operators are admitted; power, unsigned-shift, logical, and
// null-coalescing compound operators stay unsupported in this slice.
bool isAdmittedCompoundAssignment(ast::AssignmentOperatorKind op) noexcept {
  switch (op) {
    case ast::AssignmentOperatorKind::AddAssign:
    case ast::AssignmentOperatorKind::SubAssign:
    case ast::AssignmentOperatorKind::MulAssign:
    case ast::AssignmentOperatorKind::DivAssign:
    case ast::AssignmentOperatorKind::ModAssign:
    case ast::AssignmentOperatorKind::BitAndAssign:
    case ast::AssignmentOperatorKind::BitOrAssign:
    case ast::AssignmentOperatorKind::BitXorAssign:
    case ast::AssignmentOperatorKind::ShlAssign:
    case ast::AssignmentOperatorKind::ShrAssign:
      return true;
    default:
      return false;
  }
}

bool isScalarLiteral(ast::SyntaxKind kind) noexcept {
  switch (kind) {
    case ast::SyntaxKind::NullLiteral:
    case ast::SyntaxKind::BoolLiteral:
    case ast::SyntaxKind::IntLiteral:
    case ast::SyntaxKind::FloatLiteralExpr:
    case ast::SyntaxKind::BigIntLiteral:
    case ast::SyntaxKind::StringLiteralExpr:
    case ast::SyntaxKind::UnitLiteral:
    case ast::SyntaxKind::CharacterLiteralExpr:
    case ast::SyntaxKind::NoSubstitutionTemplateLiteralExpr:
      return true;
    default:
      return false;
  }
}

zc::Maybe<ast::NodeId> statementItem(const ast::Tree& tree, ast::NodeId statement);

bool isAdmittedExpressionStatement(const ast::Tree& tree, const ast::Node& statement) {
  const ast::NodeId expression(statement.payload.words[ast::kExpressionStatementExpressionWord]);
  if (!tree.contains(expression)) { return false; }
  if (tree.node(expression).kind == ast::SyntaxKind::SpawnExpression) return true;
  // A standalone receiver method call statement whose result is discarded (a
  // unit-returning effect call). Callee shape and argument type matching are
  // checker/HIR decisions kept out of surface admission. A direct free-function
  // call statement stays unadmitted in this slice.
  if (tree.node(expression).kind == ast::SyntaxKind::CallExpression) {
    return isAdmittedReceiverCall(tree, expression);
  }
  // A postfix increment/decrement (`x++` / `x--`) on an identifier operand
  // desugars to a binary write (`x = x + 1` / `x = x - 1`). The operand
  // mutability and integer type are checker decisions kept out of surface
  // admission.
  if (tree.node(expression).kind == ast::SyntaxKind::PostfixExpression) {
    const auto postfixOp = static_cast<ast::PostfixOperatorKind>(
        tree.node(expression).payload.words[ast::kPostfixExpressionOpWord]);
    if (postfixOp != ast::PostfixOperatorKind::Increment &&
        postfixOp != ast::PostfixOperatorKind::Decrement) {
      return false;
    }
    const ast::NodeId operand(
        tree.node(expression).payload.words[ast::kPostfixExpressionOperandWord]);
    return tree.contains(operand) && tree.node(operand).kind == ast::SyntaxKind::IdentExpr;
  }
  // A prefix increment/decrement (`++x` / `--x`) on an identifier operand
  // desugars to a binary write (`x = x + 1` / `x = x - 1`), the same as the
  // postfix form. The operand mutability and integer type are checker
  // decisions kept out of surface admission.
  if (tree.node(expression).kind == ast::SyntaxKind::UnaryExpression) {
    const auto unaryOp = static_cast<ast::UnaryOperatorKind>(
        tree.node(expression).payload.words[ast::kUnaryExpressionOpWord]);
    if (unaryOp != ast::UnaryOperatorKind::PreIncrement &&
        unaryOp != ast::UnaryOperatorKind::PreDecrement) {
      return false;
    }
    const ast::NodeId operand(
        tree.node(expression).payload.words[ast::kUnaryExpressionOperandWord]);
    return tree.contains(operand) && tree.node(operand).kind == ast::SyntaxKind::IdentExpr;
  }
  if (tree.node(expression).kind != ast::SyntaxKind::AssignmentExpr) {
    // An unsafe block expression statement (`unsafe { *ptr = <literal>; }`) is
    // admitted when its sole inner statement is a raw-pointer write: an
    // assignment whose target is a Deref unary over an identifier and whose
    // value is a scalar literal. Pointer mutability and pointee type matching
    // are checker decisions, kept out of surface admission.
    if (tree.node(expression).kind != ast::SyntaxKind::UnsafeBlockExpr) return false;
    const ast::NodeId unsafeBody(
        tree.node(expression).payload.words[ast::kUnsafeBlockExprBodyWord]);
    if (!tree.contains(unsafeBody) || tree.node(unsafeBody).kind != ast::SyntaxKind::BlockStmt) {
      return false;
    }
    const ast::NodeList unsafeStatements{
        tree.node(unsafeBody).payload.words[ast::kBlockStmtStmtsFirstWord],
        tree.node(unsafeBody).payload.words[ast::kBlockStmtStmtsSizeWord]};
    if (!tree.contains(unsafeStatements) || unsafeStatements.size != 1) return false;
    auto innerItem = statementItem(tree, tree.list(unsafeStatements)[0]);
    if (innerItem == zc::none) return false;
    ast::NodeId innerStatement;
    ZC_IF_SOME(item, innerItem) { innerStatement = item; }
    if (!tree.contains(innerStatement) ||
        tree.node(innerStatement).kind != ast::SyntaxKind::ExpressionStatement) {
      return false;
    }
    const ast::NodeId innerExpr(
        tree.node(innerStatement).payload.words[ast::kExpressionStatementExpressionWord]);
    if (!tree.contains(innerExpr) || tree.node(innerExpr).kind != ast::SyntaxKind::AssignmentExpr) {
      return false;
    }
    const auto innerOp = static_cast<ast::AssignmentOperatorKind>(
        tree.node(innerExpr).payload.words[ast::kAssignmentExprOpWord]);
    if (innerOp != ast::AssignmentOperatorKind::Assign) return false;
    const ast::NodeId innerTarget(tree.node(innerExpr).payload.words[ast::kAssignmentExprLhsWord]);
    const ast::NodeId innerValue(tree.node(innerExpr).payload.words[ast::kAssignmentExprRhsWord]);
    if (!tree.contains(innerTarget) || !tree.contains(innerValue)) return false;
    if (tree.node(innerTarget).kind != ast::SyntaxKind::UnaryExpression) return false;
    const auto derefOp = static_cast<ast::UnaryOperatorKind>(
        tree.node(innerTarget).payload.words[ast::kUnaryExpressionOpWord]);
    if (derefOp != ast::UnaryOperatorKind::Deref) return false;
    const ast::NodeId derefOperand(
        tree.node(innerTarget).payload.words[ast::kUnaryExpressionOperandWord]);
    return tree.contains(derefOperand) &&
           tree.node(derefOperand).kind == ast::SyntaxKind::IdentExpr &&
           isScalarLiteral(tree.node(innerValue).kind);
  }
  const auto& assignment = tree.node(expression);
  const auto assignmentOp = static_cast<ast::AssignmentOperatorKind>(
      assignment.payload.words[ast::kAssignmentExprOpWord]);
  // A compound assignment (`x += 1`) desugars to a binary write (`x = x + 1`);
  // the target and value structural checks below are the same as a plain write.
  if (assignmentOp != ast::AssignmentOperatorKind::Assign &&
      !isAdmittedCompoundAssignment(assignmentOp)) {
    return false;
  }
  const ast::NodeId target(assignment.payload.words[ast::kAssignmentExprLhsWord]);
  const ast::NodeId value(assignment.payload.words[ast::kAssignmentExprRhsWord]);
  if (!tree.contains(target) || !tree.contains(value)) return false;
  switch (tree.node(value).kind) {
    case ast::SyntaxKind::NullLiteral:
    case ast::SyntaxKind::BoolLiteral:
    case ast::SyntaxKind::IntLiteral:
    case ast::SyntaxKind::FloatLiteralExpr:
    case ast::SyntaxKind::BigIntLiteral:
    case ast::SyntaxKind::StringLiteralExpr:
    case ast::SyntaxKind::UnitLiteral:
    case ast::SyntaxKind::CharacterLiteralExpr:
    case ast::SyntaxKind::NoSubstitutionTemplateLiteralExpr:
    // An identifier write value is a parameter or local reference resolved
    // downstream; admit it structurally here. Which references are actually
    // supported is a checker/HIR decision, kept out of surface admission.
    case ast::SyntaxKind::IdentExpr:
      break;
    // A primitive binary write value (`x = a + b`) is admitted structurally for a
    // scalar-local target only; operator and operand support is a checker/HIR
    // decision. A field write value stays literal-only in this slice. A compound
    // assignment's RHS is the binary's second operand, not a nested binary, so
    // binary values stay plain-assignment-only.
    case ast::SyntaxKind::BinaryExpr:
      return assignmentOp == ast::AssignmentOperatorKind::Assign &&
             tree.node(target).kind == ast::SyntaxKind::IdentExpr &&
             isAdmittedPrimitiveBinary(tree, value);
    default:
      return false;
  }
  if (tree.node(target).kind == ast::SyntaxKind::IdentExpr) return true;
  // A constant-index array element write (`a[0] = <literal>`) is admitted
  // structurally; the base mutability, index bounds, and element type match
  // are checker decisions, kept out of surface admission.
  if (tree.node(target).kind == ast::SyntaxKind::IndexExpression) {
    const ast::NodeId elemBase(tree.node(target).payload.words[ast::kIndexExpressionObjectWord]);
    const ast::NodeId elemIndex(tree.node(target).payload.words[ast::kIndexExpressionIndexWord]);
    return tree.contains(elemBase) && tree.node(elemBase).kind == ast::SyntaxKind::IdentExpr &&
           tree.contains(elemIndex) && tree.node(elemIndex).kind == ast::SyntaxKind::IntLiteral;
  }
  if (tree.node(target).kind != ast::SyntaxKind::MemberExpression) return false;
  const ast::NodeId object(tree.node(target).payload.words[ast::kMemberExpressionObjectWord]);
  // A receiver field write (`this.field = <literal>`) is admitted structurally;
  // whether the enclosing method's receiver is mutable is a checker decision,
  // kept out of surface admission.
  return tree.contains(object) && (tree.node(object).kind == ast::SyntaxKind::IdentExpr ||
                                   tree.node(object).kind == ast::SyntaxKind::ThisExpr);
}

zc::Maybe<ast::NodeId> statementItem(const ast::Tree& tree, ast::NodeId statement) {
  if (!tree.contains(statement)) return zc::none;
  if (tree.node(statement).kind != ast::SyntaxKind::StatementListItem) return statement;
  const ast::NodeId item(tree.node(statement).payload.words[ast::kStatementListItemItemWord]);
  if (!tree.contains(item)) return zc::none;
  return item;
}

bool hasAdmittedArguments(const ast::Tree& tree, const ast::Node& call,
                          ast::NodeId receiver = ast::NodeId()) {
  const ast::NodeList typeArguments{call.payload.words[ast::kCallExpressionTypeArgsFirstWord],
                                    call.payload.words[ast::kCallExpressionTypeArgsSizeWord]};
  const ast::NodeList arguments{call.payload.words[ast::kCallExpressionArgsFirstWord],
                                call.payload.words[ast::kCallExpressionArgsSizeWord]};
  if (!tree.contains(typeArguments) || !tree.contains(arguments) || !typeArguments.empty()) {
    return false;
  }
  const bool hasReceiver = tree.contains(receiver);
  for (const auto argument : tree.list(arguments)) {
    // Admit a scalar-literal or an identifier argument (a parameter or local
    // reference resolved downstream). Type and callee-argument matching is a
    // checker/HIR decision and stays out of surface admission.
    if (!tree.contains(argument) || (!isScalarLiteral(tree.node(argument).kind) &&
                                     tree.node(argument).kind != ast::SyntaxKind::IdentExpr)) {
      // A primitive binary argument (`cell.compare(cell.value > 0)`) is
      // admitted structurally: the binary operands follow the same
      // literal-or-identifier shape as a primitive binary return. Operator and
      // operand support is a checker decision kept out of surface admission.
      if (tree.contains(argument) && tree.node(argument).kind == ast::SyntaxKind::BinaryExpr &&
          isAdmittedPrimitiveBinary(tree, argument)) {
        continue;
      }
      // A field-comparison argument (`cell.compare(cell.value > 0)`) is
      // admitted structurally: one operand is a dot member expression on the
      // receiver local and the other is a scalar literal. The operator, field
      // type, and comparison result type are checker decisions.
      if (hasReceiver && tree.contains(argument) &&
          tree.node(argument).kind == ast::SyntaxKind::BinaryExpr &&
          isAdmittedFieldComparisonBinary(tree, argument, receiver)) {
        continue;
      }
      // A field-projection argument on the receiver local
      // (`cell.echo(cell.value)`) is admitted structurally: the argument is a
      // dot member expression whose object names the same local as the call
      // receiver. The binding match and field type are checker decisions.
      // A qualified enum variant argument (`matchEnum(Color::Red)`) is
      // admitted structurally: the argument is a qualified member expression
      // whose object is an identifier naming the enum. The variant resolution
      // and type matching are checker decisions.
      if (tree.contains(argument) &&
          tree.node(argument).kind == ast::SyntaxKind::MemberExpression &&
          static_cast<ast::MemberAccessKind>(
              tree.node(argument).payload.words[ast::kMemberExpressionAccessWord]) ==
              ast::MemberAccessKind::Qualified) {
        const ast::NodeId qualifiedObject(
            tree.node(argument).payload.words[ast::kMemberExpressionObjectWord]);
        if (tree.contains(qualifiedObject) &&
            tree.node(qualifiedObject).kind == ast::SyntaxKind::IdentExpr) {
          continue;
        }
      }
      if (!hasReceiver || !tree.contains(argument) ||
          tree.node(argument).kind != ast::SyntaxKind::MemberExpression ||
          static_cast<ast::MemberAccessKind>(
              tree.node(argument).payload.words[ast::kMemberExpressionAccessWord]) !=
              ast::MemberAccessKind::Dot) {
        return false;
      }
      const ast::NodeId fieldObject(
          tree.node(argument).payload.words[ast::kMemberExpressionObjectWord]);
      if (!tree.contains(fieldObject) ||
          tree.node(fieldObject).kind != ast::SyntaxKind::IdentExpr ||
          tree.node(fieldObject).payload.words[ast::kIdentExprNameWord] !=
              tree.node(receiver).payload.words[ast::kIdentExprNameWord]) {
        return false;
      }
    }
  }
  return true;
}

bool isAdmittedReceiverCall(const ast::Tree& tree, ast::NodeId expression) {
  if (!tree.contains(expression) || tree.node(expression).kind != ast::SyntaxKind::CallExpression) {
    return false;
  }
  const auto& call = tree.node(expression);
  const ast::NodeId callee(call.payload.words[ast::kCallExpressionCalleeWord]);
  if (!tree.contains(callee) || tree.node(callee).kind != ast::SyntaxKind::MemberExpression ||
      static_cast<ast::MemberAccessKind>(
          tree.node(callee).payload.words[ast::kMemberExpressionAccessWord]) !=
          ast::MemberAccessKind::Dot) {
    return false;
  }
  const ast::NodeId receiver(tree.node(callee).payload.words[ast::kMemberExpressionObjectWord]);
  // A receiver call on the implicit `this` receiver (`this.method()`) is
  // admitted structurally; whether the enclosing method or destructor's
  // receiver mode is compatible is a checker decision, kept out of surface
  // admission.
  return tree.contains(receiver) &&
         (tree.node(receiver).kind == ast::SyntaxKind::IdentExpr ||
          tree.node(receiver).kind == ast::SyntaxKind::ThisExpr) &&
         hasAdmittedArguments(tree, call, receiver);
}

// Structurally admits a primitive binary operation whose operands are each an
// identifier (a parameter reference resolved downstream), a scalar literal, or
// (for at most one operand) a nested one-level primitive binary of the same
// shape, with at least one identifier or nested operand. This is
// operator-agnostic: it admits every relational, arithmetic, and bitwise
// BinaryExpr of this shape and the checker decides which operators are actually
// supported and in which position, keeping the operator-support contract in a
// single place. A literal-vs-literal operation has no parameter to lower and
// fails closed here. Two-level nesting (an operand of a nested operand being a
// binary) and both operands nested stay unsupported.
bool isAdmittedPrimitiveBinary(const ast::Tree& tree, ast::NodeId value) {
  if (!tree.contains(value) || tree.node(value).kind != ast::SyntaxKind::BinaryExpr) return false;
  const ast::NodeId left(tree.node(value).payload.words[ast::kBinaryExprLhsWord]);
  const ast::NodeId right(tree.node(value).payload.words[ast::kBinaryExprRhsWord]);
  if (!tree.contains(left) || !tree.contains(right)) return false;
  auto isLeaf = [&](ast::NodeId operand) {
    return tree.node(operand).kind == ast::SyntaxKind::IdentExpr ||
           isScalarLiteral(tree.node(operand).kind);
  };
  // A nested operand is a one-level binary whose own operands are leaves, with at
  // least one identifier leaf.
  auto isNested = [&](ast::NodeId operand) {
    if (tree.node(operand).kind != ast::SyntaxKind::BinaryExpr) return false;
    const ast::NodeId innerLeft(tree.node(operand).payload.words[ast::kBinaryExprLhsWord]);
    const ast::NodeId innerRight(tree.node(operand).payload.words[ast::kBinaryExprRhsWord]);
    if (!tree.contains(innerLeft) || !tree.contains(innerRight)) return false;
    const bool innerLeftIdent = tree.node(innerLeft).kind == ast::SyntaxKind::IdentExpr;
    const bool innerRightIdent = tree.node(innerRight).kind == ast::SyntaxKind::IdentExpr;
    return isLeaf(innerLeft) && isLeaf(innerRight) && (innerLeftIdent || innerRightIdent);
  };
  const bool leftNested = isNested(left);
  const bool rightNested = isNested(right);
  // At most one operand may be nested in this slice.
  if (leftNested && rightNested) return false;
  const bool leftIdent = tree.node(left).kind == ast::SyntaxKind::IdentExpr;
  const bool rightIdent = tree.node(right).kind == ast::SyntaxKind::IdentExpr;
  const bool leftOk = leftIdent || isScalarLiteral(tree.node(left).kind) || leftNested;
  const bool rightOk = rightIdent || isScalarLiteral(tree.node(right).kind) || rightNested;
  const bool result = leftOk && rightOk && (leftIdent || rightIdent || leftNested || rightNested);
  return result;
}

// A field-comparison binary is a binary expression whose one operand is a dot
// member expression on the receiver local and whose other operand is a scalar
// literal (`cell.value > 0`). The operator, field type, and comparison result
// type are checker decisions kept out of surface admission.
bool isAdmittedFieldComparisonBinary(const ast::Tree& tree, ast::NodeId value,
                                     ast::NodeId receiver) {
  if (!tree.contains(value) || tree.node(value).kind != ast::SyntaxKind::BinaryExpr) return false;
  const ast::NodeId left(tree.node(value).payload.words[ast::kBinaryExprLhsWord]);
  const ast::NodeId right(tree.node(value).payload.words[ast::kBinaryExprRhsWord]);
  if (!tree.contains(left) || !tree.contains(right)) return false;
  auto isReceiverField = [&](ast::NodeId operand) {
    if (!tree.contains(operand) || tree.node(operand).kind != ast::SyntaxKind::MemberExpression) {
      return false;
    }
    if (static_cast<ast::MemberAccessKind>(
            tree.node(operand).payload.words[ast::kMemberExpressionAccessWord]) !=
        ast::MemberAccessKind::Dot) {
      return false;
    }
    const ast::NodeId fieldObject(
        tree.node(operand).payload.words[ast::kMemberExpressionObjectWord]);
    return tree.contains(fieldObject) &&
           tree.node(fieldObject).kind == ast::SyntaxKind::IdentExpr &&
           tree.node(fieldObject).payload.words[ast::kIdentExprNameWord] ==
               tree.node(receiver).payload.words[ast::kIdentExprNameWord];
  };
  const bool leftField = isReceiverField(left);
  const bool rightField = isReceiverField(right);
  if (leftField == rightField) return false;
  const ast::NodeId literalOperand = leftField ? right : left;
  return isScalarLiteral(tree.node(literalOperand).kind);
}

bool isAdmittedLoopBodyWrite(const ast::Tree& tree, ast::NodeId statement) {
  auto item = statementItem(tree, statement);
  if (item == zc::none) return false;
  ast::NodeId writeStmt;
  ZC_IF_SOME(value, item) { writeStmt = value; }
  if (tree.node(writeStmt).kind != ast::SyntaxKind::ExpressionStatement) return false;
  const ast::NodeId assignment(
      tree.node(writeStmt).payload.words[ast::kExpressionStatementExpressionWord]);
  if (!tree.contains(assignment) || tree.node(assignment).kind != ast::SyntaxKind::AssignmentExpr ||
      static_cast<ast::AssignmentOperatorKind>(
          tree.node(assignment).payload.words[ast::kAssignmentExprOpWord]) !=
          ast::AssignmentOperatorKind::Assign) {
    return false;
  }
  const ast::NodeId target(tree.node(assignment).payload.words[ast::kAssignmentExprLhsWord]);
  const ast::NodeId value(tree.node(assignment).payload.words[ast::kAssignmentExprRhsWord]);
  if (!tree.contains(target) || !tree.contains(value) ||
      tree.node(target).kind != ast::SyntaxKind::IdentExpr) {
    return false;
  }
  return isScalarLiteral(tree.node(value).kind) ||
         tree.node(value).kind == ast::SyntaxKind::IdentExpr ||
         (tree.node(value).kind == ast::SyntaxKind::BinaryExpr &&
          isAdmittedPrimitiveBinary(tree, value));
}

/// \brief Structurally admits one `while`-body trailing control-flow statement.
///
/// The statement is a `break;` or `continue;`, labeled or unlabeled. Label
/// resolution is the binder's contract; the ownership surface only checks the
/// statement kind. Structure only; the checker/HIR decide the loop exit and
/// back-edge lowering.
bool isAdmittedLoopBodyControlFlow(const ast::Tree& tree, ast::NodeId statement) {
  auto item = statementItem(tree, statement);
  if (item == zc::none) return false;
  ast::NodeId stmt;
  ZC_IF_SOME(value, item) { stmt = value; }
  const auto kind = tree.node(stmt).kind;
  return kind == ast::SyntaxKind::BreakStmt || kind == ast::SyntaxKind::ContinueStatement;
}

/// \brief Structurally admits one if-guarded `break;` loop-body statement.
///
/// The statement is `if (<ident|lit> <cmp> <ident|lit>) { break; }`: an `if`
/// with no `else` whose condition is a binary comparison over an identifier and
/// a scalar literal and whose body is a block holding exactly one
/// `break;` (labeled or unlabeled). The guard evaluates before the accumulator
/// writes in source order, so the loop body splits into a guard block and a
/// continuation block downstream. Structure only; the checker/HIR decide the
/// comparison operator and the loop-exit lowering.
bool isAdmittedLoopBodyGuardedBreak(const ast::Tree& tree, ast::NodeId statement) {
  auto item = statementItem(tree, statement);
  if (item == zc::none) return false;
  ast::NodeId stmt;
  ZC_IF_SOME(value, item) { stmt = value; }
  if (tree.node(stmt).kind != ast::SyntaxKind::IfStmt) return false;
  const auto& ifNode = tree.node(stmt);
  // No else branch in this slice.
  if (ifNode.payload.words[ast::kIfStmtElseStmtWord] != 0) return false;
  const ast::NodeId cond(ifNode.payload.words[ast::kIfStmtCondWord]);
  const ast::NodeId thenStmt(ifNode.payload.words[ast::kIfStmtThenStmtWord]);
  if (!tree.contains(cond) || tree.node(cond).kind != ast::SyntaxKind::BinaryExpr ||
      !tree.contains(thenStmt) || tree.node(thenStmt).kind != ast::SyntaxKind::BlockStmt) {
    return false;
  }
  const ast::NodeId condLeft(tree.node(cond).payload.words[ast::kBinaryExprLhsWord]);
  const ast::NodeId condRight(tree.node(cond).payload.words[ast::kBinaryExprRhsWord]);
  if (!tree.contains(condLeft) || !tree.contains(condRight)) return false;
  const bool condLeftIdent = tree.node(condLeft).kind == ast::SyntaxKind::IdentExpr;
  const bool condRightIdent = tree.node(condRight).kind == ast::SyntaxKind::IdentExpr;
  const bool condLeftOk = condLeftIdent || isScalarLiteral(tree.node(condLeft).kind);
  const bool condRightOk = condRightIdent || isScalarLiteral(tree.node(condRight).kind);
  if (!condLeftOk || !condRightOk || (!condLeftIdent && !condRightIdent)) return false;
  const auto& thenBlock = tree.node(thenStmt);
  const ast::NodeList thenStmts{thenBlock.payload.words[ast::kBlockStmtStmtsFirstWord],
                                thenBlock.payload.words[ast::kBlockStmtStmtsSizeWord]};
  if (!tree.contains(thenStmts) || thenStmts.size != 1) return false;
  return isAdmittedLoopBodyControlFlow(tree, tree.list(thenStmts)[0]);
}

/// \brief Shape-matches a `while` loop that has an admitted semantic contract.
///
/// The admitted loop condition is a bare identifier (resolved to a bool
/// parameter downstream). The body is either empty or a block whose statements
/// are admitted scalar-local write statements (`<ident> = <lit | ident |
/// binary>;`) optionally followed by one trailing unlabeled `break;` or
/// `continue;`. Both forms are genuine reducible loops that Built MIR lowers to
/// a four-block CFG with a reducible back-edge; a non-empty body carries its
/// writes in the loop's body block before the back-edge Goto, and a trailing
/// break exits to the loop exit instead. Structure only; the checker/HIR decide
/// which local each write targets and which operators are supported.
bool isAdmittedLoopStatement(const ast::Tree& tree, ast::NodeId whileStmt) {
  if (!tree.contains(whileStmt) || tree.node(whileStmt).kind != ast::SyntaxKind::WhileStmt) {
    return false;
  }
  const auto& loop = tree.node(whileStmt);
  const ast::NodeId condition(loop.payload.words[ast::kWhileStmtCondWord]);
  const ast::NodeId body(loop.payload.words[ast::kWhileStmtBodyWord]);
  // Condition: a binary comparison with identifier/literal operands (at least
  // one identifier), or a bare identifier.
  if (tree.contains(condition) && tree.node(condition).kind == ast::SyntaxKind::BinaryExpr) {
    const ast::NodeId condLeft(tree.node(condition).payload.words[ast::kBinaryExprLhsWord]);
    const ast::NodeId condRight(tree.node(condition).payload.words[ast::kBinaryExprRhsWord]);
    if (!tree.contains(condLeft) || !tree.contains(condRight)) return false;
    const bool condLeftIdent = tree.node(condLeft).kind == ast::SyntaxKind::IdentExpr;
    const bool condRightIdent = tree.node(condRight).kind == ast::SyntaxKind::IdentExpr;
    const bool condLeftOk = condLeftIdent || isScalarLiteral(tree.node(condLeft).kind);
    const bool condRightOk = condRightIdent || isScalarLiteral(tree.node(condRight).kind);
    if (!condLeftOk || !condRightOk || (!condLeftIdent && !condRightIdent)) return false;
  } else if (!tree.contains(condition) || tree.node(condition).kind != ast::SyntaxKind::IdentExpr) {
    return false;
  }
  if (!tree.contains(body) || tree.node(body).kind != ast::SyntaxKind::BlockStmt) return false;
  const auto& block = tree.node(body);
  const ast::NodeList statements{block.payload.words[ast::kBlockStmtStmtsFirstWord],
                                 block.payload.words[ast::kBlockStmtStmtsSizeWord]};
  if (!tree.contains(statements)) return false;
  const auto statementNodes = tree.list(statements);
  bool hasWrite = false;
  for (size_t index = 0; index < statementNodes.size(); ++index) {
    if (isAdmittedLoopBodyWrite(tree, statementNodes[index])) {
      hasWrite = true;
      continue;
    }
    // A break/continue is admitted only as the trailing body statement.
    if (index + 1 == statementNodes.size() &&
        isAdmittedLoopBodyControlFlow(tree, statementNodes[index])) {
      continue;
    }
    return false;
  }
  return hasWrite;
}

/// \brief Shape-matches a `do { <body> } while (<cond>);` loop that has an
/// admitted semantic contract.
///
/// The admitted do-while body contract is identical to the while-loop body:
/// a block whose statements are admitted scalar-local write statements
/// (`<ident> = <lit | ident | binary>;`) optionally followed by one trailing
/// unlabeled `break;` or `continue;`. The condition admits a binary comparison
/// whose operands are each an identifier or a scalar literal (with at least
/// one identifier operand), or a bare identifier. Which comparison operators
/// are supported is a checker decision. The do-while desugars to a
/// body-before-condition reducible four-block CFG downstream. Structure only;
/// the checker/HIR decide which local each write targets and which operators
/// are supported.
bool isAdmittedDoWhileStatement(const ast::Tree& tree, ast::NodeId doWhileStmt) {
  if (!tree.contains(doWhileStmt) ||
      tree.node(doWhileStmt).kind != ast::SyntaxKind::DoWhileStatement) {
    return false;
  }
  const auto& loop = tree.node(doWhileStmt);
  const ast::NodeId body(loop.payload.words[ast::kDoWhileStatementBodyWord]);
  const ast::NodeId condition(loop.payload.words[ast::kDoWhileStatementCondWord]);
  if (!tree.contains(body) || tree.node(body).kind != ast::SyntaxKind::BlockStmt) return false;
  // Condition: a binary comparison with identifier/literal operands (at least
  // one identifier), or a bare identifier.
  if (tree.contains(condition) && tree.node(condition).kind == ast::SyntaxKind::BinaryExpr) {
    const ast::NodeId condLeft(tree.node(condition).payload.words[ast::kBinaryExprLhsWord]);
    const ast::NodeId condRight(tree.node(condition).payload.words[ast::kBinaryExprRhsWord]);
    if (!tree.contains(condLeft) || !tree.contains(condRight)) return false;
    const bool condLeftIdent = tree.node(condLeft).kind == ast::SyntaxKind::IdentExpr;
    const bool condRightIdent = tree.node(condRight).kind == ast::SyntaxKind::IdentExpr;
    const bool condLeftOk = condLeftIdent || isScalarLiteral(tree.node(condLeft).kind);
    const bool condRightOk = condRightIdent || isScalarLiteral(tree.node(condRight).kind);
    if (!condLeftOk || !condRightOk || (!condLeftIdent && !condRightIdent)) return false;
  } else if (!tree.contains(condition) || tree.node(condition).kind != ast::SyntaxKind::IdentExpr) {
    return false;
  }
  const auto& block = tree.node(body);
  const ast::NodeList statements{block.payload.words[ast::kBlockStmtStmtsFirstWord],
                                 block.payload.words[ast::kBlockStmtStmtsSizeWord]};
  if (!tree.contains(statements)) return false;
  const auto statementNodes = tree.list(statements);
  bool hasWrite = false;
  for (size_t index = 0; index < statementNodes.size(); ++index) {
    if (isAdmittedLoopBodyWrite(tree, statementNodes[index])) {
      hasWrite = true;
      continue;
    }
    // A break/continue is admitted only as the trailing body statement.
    if (index + 1 == statementNodes.size() &&
        isAdmittedLoopBodyControlFlow(tree, statementNodes[index])) {
      continue;
    }
    return false;
  }
  return hasWrite;
}

/// \brief Shape-matches a C-style `for` loop that has an admitted semantic
/// contract.
///
/// The admitted for-loop shape is a bounded slice:
///   `for (let id = <literal>; <ident> <cmp> <literal>; <ident> = <binary>)
///    { <body> }`
///
/// - `init` is a `let` declaration with exactly one declarator whose pattern is
///   a bare identifier and whose initializer is a scalar literal.
/// - `cond` is a binary comparison whose operands are each an identifier or a
///   scalar literal, with at least one identifier operand. Which comparison
///   operators are supported is a checker decision.
/// - `update` is an assignment expression whose target is a bare identifier
///   and whose value is an admitted primitive binary (arithmetic).
/// - `body` is either an empty block or exactly one admitted loop-body write
///   (an `<ident> = <ident | literal | admitted primitive binary>;` assignment
///   that accumulates into a local declared outside the loop) optionally
///   followed by one trailing unlabeled `break;` or `continue;`.
///
/// Structure only; the checker/HIR decide which local each write targets and
/// which operators are supported. The loop desugars to `let id = <literal>;
/// while (<ident> <cmp> <literal>) { <body> <ident> = <binary>; }` downstream.
bool isAdmittedForStatement(const ast::Tree& tree, ast::NodeId forStmt) {
  if (!tree.contains(forStmt) || tree.node(forStmt).kind != ast::SyntaxKind::ForStmt) {
    return false;
  }
  const auto& loop = tree.node(forStmt);
  const ast::NodeId init(loop.payload.words[ast::kForStmtInitWord]);
  const ast::NodeId cond(loop.payload.words[ast::kForStmtCondWord]);
  const ast::NodeId update(loop.payload.words[ast::kForStmtUpdateWord]);
  const ast::NodeId body(loop.payload.words[ast::kForStmtBodyWord]);
  // Init: let declaration with one declarator, identifier pattern, scalar
  // literal initializer.
  if (!tree.contains(init) || tree.node(init).kind != ast::SyntaxKind::LetStmt) return false;
  const auto& letNode = tree.node(init);
  if (static_cast<ast::BindingDeclarationKind>(letNode.payload.words[ast::kLetStmtKindWord]) !=
      ast::BindingDeclarationKind::Let) {
    return false;
  }
  const ast::NodeId declarations(letNode.payload.words[ast::kLetStmtDeclarationsWord]);
  if (!tree.contains(declarations) ||
      tree.node(declarations).kind != ast::SyntaxKind::VariableDeclaratorList) {
    return false;
  }
  const ast::NodeList declarators{
      tree.node(declarations).payload.words[ast::kVariableDeclaratorListDeclsFirstWord],
      tree.node(declarations).payload.words[ast::kVariableDeclaratorListDeclsSizeWord]};
  if (!tree.contains(declarators) || declarators.size != 1) return false;
  const auto declarator = tree.list(declarators)[0];
  if (!tree.contains(declarator) ||
      tree.node(declarator).kind != ast::SyntaxKind::VariableDeclarator) {
    return false;
  }
  const ast::NodeId pattern(
      tree.node(declarator).payload.words[ast::kVariableDeclaratorPatternWord]);
  const ast::NodeId initializer(
      tree.node(declarator).payload.words[ast::kVariableDeclaratorInitWord]);
  if (!tree.contains(pattern) || tree.node(pattern).kind != ast::SyntaxKind::IdentifierPattern ||
      !tree.contains(initializer) || !isScalarLiteral(tree.node(initializer).kind)) {
    return false;
  }
  // Cond: binary comparison with identifier/literal operands, at least one
  // identifier.
  if (!tree.contains(cond) || tree.node(cond).kind != ast::SyntaxKind::BinaryExpr) return false;
  const ast::NodeId condLeft(tree.node(cond).payload.words[ast::kBinaryExprLhsWord]);
  const ast::NodeId condRight(tree.node(cond).payload.words[ast::kBinaryExprRhsWord]);
  if (!tree.contains(condLeft) || !tree.contains(condRight)) return false;
  const bool condLeftIdent = tree.node(condLeft).kind == ast::SyntaxKind::IdentExpr;
  const bool condRightIdent = tree.node(condRight).kind == ast::SyntaxKind::IdentExpr;
  const bool condLeftOk = condLeftIdent || isScalarLiteral(tree.node(condLeft).kind);
  const bool condRightOk = condRightIdent || isScalarLiteral(tree.node(condRight).kind);
  if (!condLeftOk || !condRightOk || (!condLeftIdent && !condRightIdent)) return false;
  // Update: assignment with identifier target and admitted primitive binary
  // value.
  if (!tree.contains(update) || tree.node(update).kind != ast::SyntaxKind::AssignmentExpr ||
      static_cast<ast::AssignmentOperatorKind>(
          tree.node(update).payload.words[ast::kAssignmentExprOpWord]) !=
          ast::AssignmentOperatorKind::Assign) {
    return false;
  }
  const ast::NodeId updateTarget(tree.node(update).payload.words[ast::kAssignmentExprLhsWord]);
  const ast::NodeId updateValue(tree.node(update).payload.words[ast::kAssignmentExprRhsWord]);
  if (!tree.contains(updateTarget) || tree.node(updateTarget).kind != ast::SyntaxKind::IdentExpr ||
      !tree.contains(updateValue) || !isAdmittedPrimitiveBinary(tree, updateValue)) {
    return false;
  }
  // Body: an empty block, N admitted loop-body writes optionally followed by
  // one trailing break/continue (labeled or unlabeled), or one if-guarded
  // break followed by N admitted loop-body writes. The guarded break
  // evaluates before the writes in source order, so it must lead the body.
  // A break/continue is admitted only as the trailing body statement,
  // matching the while-loop body slice.
  if (!tree.contains(body) || tree.node(body).kind != ast::SyntaxKind::BlockStmt) return false;
  const auto& block = tree.node(body);
  const ast::NodeList statements{block.payload.words[ast::kBlockStmtStmtsFirstWord],
                                 block.payload.words[ast::kBlockStmtStmtsSizeWord]};
  if (!tree.contains(statements)) return false;
  if (statements.empty()) return true;
  const auto statementNodes = tree.list(statements);
  // Nested for-loop: the sole body statement is itself an admitted for-loop.
  // This admits the outer loop of a nested accumulator shape; the inner loop
  // is validated recursively by isAdmittedForStatement.
  if (statements.size == 1) {
    auto innerItem = statementItem(tree, statementNodes[0]);
    if (innerItem != zc::none) {
      ast::NodeId innerStmt;
      ZC_IF_SOME(item, innerItem) { innerStmt = item; }
      if (tree.node(innerStmt).kind == ast::SyntaxKind::ForStmt) {
        return isAdmittedForStatement(tree, innerStmt);
      }
    }
  }
  // If-guarded break leading the body: the remaining statements must all be
  // admitted loop-body writes, and at least one write must follow the break.
  // A body with only a guarded break has no accumulator to lower and fails
  // closed here.
  if (isAdmittedLoopBodyGuardedBreak(tree, statementNodes[0])) {
    if (statements.size < 2) return false;
    for (size_t i = 1; i < statements.size; ++i) {
      if (!isAdmittedLoopBodyWrite(tree, statementNodes[i])) return false;
    }
    return true;
  }
  // Every statement except the last must be an admitted loop-body write.
  for (size_t i = 0; i + 1 < statements.size; ++i) {
    if (!isAdmittedLoopBodyWrite(tree, statementNodes[i])) return false;
  }
  // The last statement is either an admitted loop-body write or a trailing
  // break/continue.
  const auto lastStmt = statementNodes[statements.size - 1];
  return isAdmittedLoopBodyWrite(tree, lastStmt) || isAdmittedLoopBodyControlFlow(tree, lastStmt);
}

/// \brief Structurally admits one `for (let id in [lit, ...]) { ... }` loop.
///
/// The binding is an identifier pattern, the collection is an array literal
/// with one or more scalar literal elements, and the body is a block with one
/// or more admitted loop-body writes. The loop variable is immutable and is
/// substituted per element at HIR construction time (compile-time unrolling).
bool isAdmittedForInStatement(const ast::Tree& tree, ast::NodeId forInStmt) {
  if (!tree.contains(forInStmt) || tree.node(forInStmt).kind != ast::SyntaxKind::ForInStatement) {
    return false;
  }
  const auto& loop = tree.node(forInStmt);
  const ast::NodeId binding(loop.payload.words[ast::kForInStatementBindingWord]);
  const ast::NodeId collection(loop.payload.words[ast::kForInStatementExpressionWord]);
  const ast::NodeId body(loop.payload.words[ast::kForInStatementBodyWord]);
  // Binding: identifier pattern.
  if (!tree.contains(binding) || tree.node(binding).kind != ast::SyntaxKind::IdentifierPattern) {
    return false;
  }
  // Collection: array literal with one or more scalar literal elements.
  if (!tree.contains(collection) || tree.node(collection).kind != ast::SyntaxKind::ArrayLiteral) {
    return false;
  }
  const auto& arrayLit = tree.node(collection);
  const ast::NodeList elements{arrayLit.payload.words[ast::kArrayLiteralElemsFirstWord],
                               arrayLit.payload.words[ast::kArrayLiteralElemsSizeWord]};
  if (!tree.contains(elements) || elements.size == 0) return false;
  for (const auto& element : tree.list(elements)) {
    if (!tree.contains(element) || !isScalarLiteral(tree.node(element).kind)) return false;
  }
  // Body: a block with one or more admitted loop-body writes.
  if (!tree.contains(body) || tree.node(body).kind != ast::SyntaxKind::BlockStmt) return false;
  const auto& block = tree.node(body);
  const ast::NodeList statements{block.payload.words[ast::kBlockStmtStmtsFirstWord],
                                 block.payload.words[ast::kBlockStmtStmtsSizeWord]};
  if (!tree.contains(statements) || statements.size == 0) return false;
  for (const auto& stmt : tree.list(statements)) {
    if (!isAdmittedLoopBodyWrite(tree, stmt)) return false;
  }
  return true;
}

// (wildcard) arm is not admitted because the default covers the remaining
// domain unconditionally.
bool isAdmittedMatchGuard(const ast::Tree& tree, ast::NodeId guard) {
  if (!tree.contains(guard) || tree.node(guard).kind != ast::SyntaxKind::BinaryExpr) {
    return false;
  }
  const ast::NodeId left(tree.node(guard).payload.words[ast::kBinaryExprLhsWord]);
  const ast::NodeId right(tree.node(guard).payload.words[ast::kBinaryExprRhsWord]);
  if (!tree.contains(left) || !tree.contains(right)) return false;
  const bool leftIdent = tree.node(left).kind == ast::SyntaxKind::IdentExpr;
  const bool rightIdent = tree.node(right).kind == ast::SyntaxKind::IdentExpr;
  if (leftIdent == rightIdent) return false;
  const ast::NodeId literalOperand = leftIdent ? right : left;
  return isScalarLiteral(tree.node(literalOperand).kind);
}

// Structurally admits a match statement: `match (b) { when true => return
// <lit>; when false => return <lit>; }` (block-bodied arms are also admitted)
// or `match (x) { when <int> => return <lit>; default => return <lit>; }` or
// `match (c) { when Color.Red => return <lit>; ... }` with N (>= 2) enum arms.
// The scrutinee is a bare identifier (a bool, integer, or enum parameter
// reference); each arm pattern is a bool literal (one true, one false, no
// duplicates, or a single literal arm whose uncovered counterpart the checker
// rejects with ZOM4022), an integer literal (N, paired with a default arm), an
// enum unit-variant pattern (N >= 2, no default needed on the closed domain),
// or a `default` (wildcard) arm; the literal arm may carry a guard that is a
// single identifier-vs-scalar-literal binary; each arm body tails a
// scalar-literal return. The HIR builder lowers the bool shape to the same
// conditional path as a bare-parameter `if` and the integer shape to the
// equality conditional path, so the admitted surface is exactly the
// conditional surface.
bool isAdmittedMatchStatement(const ast::Tree& tree, ast::NodeId node) {
  const auto& matchNode = tree.node(node);
  if (matchNode.kind != ast::SyntaxKind::MatchStmt) return false;
  const ast::NodeId scrutinee(matchNode.payload.words[ast::kMatchStmtScrutineeWord]);
  if (!tree.contains(scrutinee) || tree.node(scrutinee).kind != ast::SyntaxKind::IdentExpr) {
    return false;
  }
  const ast::NodeList arms{matchNode.payload.words[ast::kMatchStmtArmsFirstWord],
                           matchNode.payload.words[ast::kMatchStmtArmsSizeWord]};
  if (!tree.contains(arms) || arms.size < 1) return false;
  bool sawTrue = false;
  bool sawFalse = false;
  size_t intLiteralCount = 0;
  bool sawDefault = false;
  bool sawGuard = false;
  bool sawEnumPattern = false;
  bool sawDestructurePattern = false;
  for (size_t index = 0; index < arms.size; ++index) {
    const ast::NodeId armId = tree.list(arms)[index];
    if (!tree.contains(armId)) return false;
    const auto& arm = tree.node(armId);
    if (arm.kind != ast::SyntaxKind::MatchArmStmt) return false;
    const ast::NodeId guard(arm.payload.words[ast::kMatchArmStmtGuardWord]);
    const bool hasGuard = tree.contains(guard);
    const ast::NodeId pattern(arm.payload.words[ast::kMatchArmStmtPatternWord]);
    if (!tree.contains(pattern)) return false;
    const bool isDefault = tree.node(pattern).kind == ast::SyntaxKind::WildcardPattern;
    if (hasGuard) {
      if (isDefault || sawGuard) return false;
      if (!isAdmittedMatchGuard(tree, guard)) return false;
      sawGuard = true;
    }
    if (tree.node(pattern).kind == ast::SyntaxKind::LiteralPattern) {
      const ast::NodeId literal(tree.node(pattern).payload.words[ast::kLiteralPatternLiteralWord]);
      if (!tree.contains(literal)) return false;
      if (tree.node(literal).kind == ast::SyntaxKind::BoolLiteral) {
        if (intLiteralCount > 0) return false;
        const bool value = tree.node(literal).payload.words[ast::kBoolLiteralValueWord] != 0;
        if (value) {
          if (sawTrue) return false;
          sawTrue = true;
        } else {
          if (sawFalse) return false;
          sawFalse = true;
        }
      } else if (tree.node(literal).kind == ast::SyntaxKind::IntLiteral) {
        // The integer domain is open, so N literal arms paired with a single
        // default arm are admitted; literal arms without a default stay
        // fail-closed.
        if (sawTrue || sawFalse || sawEnumPattern) return false;
        ++intLiteralCount;
      } else {
        return false;
      }
    } else if (tree.node(pattern).kind == ast::SyntaxKind::WildcardPattern) {
      if (sawDefault) return false;
      sawDefault = true;
    } else if (tree.node(pattern).kind == ast::SyntaxKind::EnumPattern) {
      // An enum variant pattern. Unit-variant patterns (e.g., `Color.Red`)
      // are admitted when the scrutinee is an enum type. Tuple-variant
      // patterns (e.g., `Result.Ok(v)`) are admitted when the payload is a
      // single IdentifierPattern; the variant resolution and type matching
      // are checker decisions. Mixing unit and tuple variants is rejected.
      const uint32_t argsSize = tree.node(pattern).payload.words[ast::kEnumPatternArgsSizeWord];
      if (argsSize != 0) {
        if (sawEnumPattern && !sawDestructurePattern) return false;
        if (argsSize != 1) return false;
        const ast::NodeList args{tree.node(pattern).payload.words[ast::kEnumPatternArgsFirstWord],
                                 argsSize};
        if (!tree.contains(args) || args.size != 1) return false;
        const ast::NodeId binding = tree.list(args)[0];
        if (!tree.contains(binding) ||
            tree.node(binding).kind != ast::SyntaxKind::IdentifierPattern) {
          return false;
        }
        sawDestructurePattern = true;
      } else {
        if (sawDestructurePattern) return false;
      }
      if (intLiteralCount > 0 || sawTrue || sawFalse || sawDefault) return false;
      sawEnumPattern = true;
    } else {
      return false;
    }
    const ast::NodeId body(arm.payload.words[ast::kMatchArmStmtBodyWord]);
    if (!tree.contains(body)) return false;
    ast::NodeId returnStmt;
    if (tree.node(body).kind == ast::SyntaxKind::ReturnStmt) {
      returnStmt = body;
    } else if (tree.node(body).kind == ast::SyntaxKind::BlockStmt) {
      const ast::NodeList stmts{tree.node(body).payload.words[ast::kBlockStmtStmtsFirstWord],
                                tree.node(body).payload.words[ast::kBlockStmtStmtsSizeWord]};
      if (!tree.contains(stmts) || stmts.size != 1) return false;
      auto item = statementItem(tree, tree.list(stmts)[0]);
      if (item == zc::none) return false;
      ZC_IF_SOME(itemValue, item) { returnStmt = itemValue; }
      if (tree.node(returnStmt).kind != ast::SyntaxKind::ReturnStmt) return false;
    } else {
      return false;
    }
    const ast::NodeId returnValue(tree.node(returnStmt).payload.words[ast::kReturnStmtValueWord]);
    if (!tree.contains(returnValue)) { return false; }
    // Destructure arms return the pattern binding directly (an IdentExpr);
    // unit-variant and literal arms return scalar literals.
    if (sawDestructurePattern) {
      if (tree.node(returnValue).kind != ast::SyntaxKind::IdentExpr) { return false; }
    } else {
      if (!isScalarLiteral(tree.node(returnValue).kind)) { return false; }
    }
  }
  // Bool: one literal arm (the checker rejects the uncovered arm with
  // ZOM4022), two literal arms (true + false), or one literal arm plus one
  // default arm. Integer: N literal arms plus one default arm. Enum: N (>= 2)
  // unit-variant pattern arms on the same enum type; the closed domain needs
  // no default arm.
  if (intLiteralCount > 0) { return sawDefault && arms.size == intLiteralCount + 1; }
  if (sawEnumPattern) return arms.size >= 2;
  if (arms.size == 1) return !sawDefault && (sawTrue != sawFalse);
  if (arms.size != 2) return false;
  return sawDefault ? (sawTrue != sawFalse) : (sawTrue && sawFalse);
}

}  // namespace

struct SurfaceSourceRejected::Impl final {
  explicit Impl(zc::Vector<SurfaceFailure>&& failures) noexcept : failureValues(zc::mv(failures)) {}

  zc::Vector<SurfaceFailure> failureValues;
};

SurfaceSourceRejected::SurfaceSourceRejected(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}
SurfaceSourceRejected::~SurfaceSourceRejected() noexcept(false) = default;
SurfaceSourceRejected::SurfaceSourceRejected(SurfaceSourceRejected&&) noexcept = default;
SurfaceSourceRejected& SurfaceSourceRejected::operator=(SurfaceSourceRejected&&) noexcept = default;
zc::ArrayPtr<const SurfaceFailure> SurfaceSourceRejected::failures() const noexcept {
  return impl->failureValues.asPtr();
}

struct AdmittedBoundModule::Impl final {
  explicit Impl(driver::module_graph_query::CheckerBoundModuleView&& boundModule) noexcept
      : boundModuleValue(zc::mv(boundModule)) {}

  driver::module_graph_query::CheckerBoundModuleView boundModuleValue;
};

AdmittedBoundModule::AdmittedBoundModule(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}
AdmittedBoundModule::~AdmittedBoundModule() noexcept(false) = default;
AdmittedBoundModule::AdmittedBoundModule(AdmittedBoundModule&&) noexcept = default;
AdmittedBoundModule& AdmittedBoundModule::operator=(AdmittedBoundModule&&) noexcept = default;
const driver::module_graph_query::CheckerBoundModuleView& AdmittedBoundModule::boundModule()
    const noexcept {
  return impl->boundModuleValue;
}
AdmittedBoundModule AdmittedBoundModule::retain() const {
  return AdmittedBoundModule(zc::heap<Impl>(impl->boundModuleValue.retain()));
}
AdmittedBoundModule::operator const driver::module_graph_query::CheckerBoundModuleView&()
    const noexcept {
  return boundModule();
}
identity::SemanticContextBrand AdmittedBoundModule::semanticContext() const noexcept {
  return boundModule().semanticContext();
}
identity::CompilationUnitId AdmittedBoundModule::compilationUnit() const noexcept {
  return boundModule().compilationUnit();
}
identity::CrateId AdmittedBoundModule::crate() const noexcept { return boundModule().crate(); }
identity::ModuleId AdmittedBoundModule::module() const noexcept { return boundModule().module(); }
identity::SourceFileId AdmittedBoundModule::sourceFile() const noexcept {
  return boundModule().sourceFile();
}
const identity::ContextFingerprint& AdmittedBoundModule::semanticFingerprint() const noexcept {
  return boundModule().semanticFingerprint();
}
const ast::Tree& AdmittedBoundModule::tree() const noexcept { return boundModule().tree(); }
const binder::CanonicalParsedModule& AdmittedBoundModule::parsedModule() const noexcept {
  return boundModule().parsedModule();
}
const binder::ImmutableDefinitionInventory& AdmittedBoundModule::definitions() const noexcept {
  return boundModule().definitions();
}
zc::ArrayPtr<const binder::MaterializedDependencyExportSurface>
AdmittedBoundModule::dependencySurfaces() const noexcept {
  return boundModule().dependencySurfaces();
}
zc::Maybe<const binder::MaterializedDependencyExportSurface&> AdmittedBoundModule::preludeSurface()
    const noexcept {
  return boundModule().preludeSurface();
}
zc::ArrayPtr<const binder::ImportBindingFact> AdmittedBoundModule::resolvedImports()
    const noexcept {
  return boundModule().resolvedImports();
}
zc::ArrayPtr<const binder::ModuleAliasBindingFact> AdmittedBoundModule::resolvedModuleAliases()
    const noexcept {
  return boundModule().resolvedModuleAliases();
}
const binder::ImmutableBindingMetadata& AdmittedBoundModule::bindings() const noexcept {
  return boundModule().bindings();
}
const binder::VerifiedExportSurface& AdmittedBoundModule::bindingSurface() const noexcept {
  return boundModule().bindingSurface();
}

SurfaceAdmissionResult SurfaceAdmissionBuilder::admit(
    driver::module_graph_query::CheckerBoundModuleView&& boundModule) {
  zc::Vector<SurfaceFailure> failures;
  uint32_t traversalOrdinal = 0;
  // Custom traversal that skips the interior of unsafe blocks: their tail
  // expression is admitted as the block's value, not as a standalone
  // expression statement.
  auto walk = [&](ast::NodeId nodeId, auto&& self) -> void {
    const auto& syntax = boundModule.tree().node(nodeId);
    ZC_IREQUIRE(traversalOrdinal != UINT32_MAX, "ownership surface traversal overflow");
    const uint32_t ordinal = traversalOrdinal++;
    SurfaceSyntaxKind kind;
    bool rejected = false;
    if (syntax.kind == ast::SyntaxKind::SpawnExpression) {
      kind = SurfaceSyntaxKind::Spawn;
      rejected = true;
    } else if (syntax.kind == ast::SyntaxKind::SuspendStatement) {
      kind = SurfaceSyntaxKind::Suspend;
      rejected = true;
    } else if (syntax.kind == ast::SyntaxKind::MatchStmt &&
               !isAdmittedMatchStatement(boundModule.tree(), nodeId)) {
      kind = SurfaceSyntaxKind::Match;
      rejected = true;
    } else if ((syntax.kind == ast::SyntaxKind::WhileStmt &&
                !isAdmittedLoopStatement(boundModule.tree(), nodeId)) ||
               (syntax.kind == ast::SyntaxKind::ForStmt &&
                !isAdmittedForStatement(boundModule.tree(), nodeId)) ||
               (syntax.kind == ast::SyntaxKind::ForInStatement &&
                !isAdmittedForInStatement(boundModule.tree(), nodeId)) ||
               (syntax.kind == ast::SyntaxKind::DoWhileStatement &&
                !isAdmittedDoWhileStatement(boundModule.tree(), nodeId))) {
      kind = SurfaceSyntaxKind::Loop;
      rejected = true;
    } else if (syntax.kind == ast::SyntaxKind::BreakStmt ||
               syntax.kind == ast::SyntaxKind::ContinueStatement) {
      kind = SurfaceSyntaxKind::LoopControl;
      rejected = true;
    } else if (syntax.kind == ast::SyntaxKind::LabeledStatement) {
      const ast::NodeId labeledStmt(syntax.payload.words[ast::kLabeledStatementStatementWord]);
      const bool wrapsAdmittedLoop =
          boundModule.tree().contains(labeledStmt) &&
          ((boundModule.tree().node(labeledStmt).kind == ast::SyntaxKind::WhileStmt &&
            isAdmittedLoopStatement(boundModule.tree(), labeledStmt)) ||
           (boundModule.tree().node(labeledStmt).kind == ast::SyntaxKind::ForStmt &&
            isAdmittedForStatement(boundModule.tree(), labeledStmt)));
      if (!wrapsAdmittedLoop) {
        kind = SurfaceSyntaxKind::Label;
        rejected = true;
      }
    } else if (syntax.kind == ast::SyntaxKind::ReturnStmt &&
               !boundModule.tree().contains(
                   ast::NodeId(syntax.payload.words[ast::kReturnStmtValueWord]))) {
      kind = SurfaceSyntaxKind::VoidReturn;
      rejected = true;
    } else if (syntax.kind == ast::SyntaxKind::ExpressionStatement &&
               !isAdmittedExpressionStatement(boundModule.tree(), syntax)) {
      kind = SurfaceSyntaxKind::ExpressionStatement;
      rejected = true;
    }
    if (rejected) {
      auto span = boundModule.parsedModule().spanFor(syntax.range);
      ZC_IREQUIRE(span != zc::none, "ownership surface syntax must retain a source span");
      ZC_IF_SOME(value, span) {
        insertFailure(failures, SurfaceFailure{kind, value.clone(), ordinal});
      }
    }
    if (syntax.kind == ast::SyntaxKind::UnsafeBlockExpr) return;
    // A labeled statement wrapping an admitted loop: traverse into the loop
    // (which has its own body-skip suppression) and skip the label node.
    if (syntax.kind == ast::SyntaxKind::LabeledStatement) {
      const ast::NodeId labeledStmt(syntax.payload.words[ast::kLabeledStatementStatementWord]);
      if (boundModule.tree().contains(labeledStmt)) {
        const auto& labeled = boundModule.tree().node(labeledStmt);
        if ((labeled.kind == ast::SyntaxKind::WhileStmt &&
             isAdmittedLoopStatement(boundModule.tree(), labeledStmt)) ||
            (labeled.kind == ast::SyntaxKind::ForStmt &&
             isAdmittedForStatement(boundModule.tree(), labeledStmt))) {
          self(labeledStmt, self);
          return;
        }
      }
    }
    // An admitted while-loop body is validated by isAdmittedLoopStatement; skip
    // the body block so its trailing break/continue is not rejected as
    // LoopControl. The condition (a bare identifier) is still traversed.
    if (syntax.kind == ast::SyntaxKind::WhileStmt &&
        isAdmittedLoopStatement(boundModule.tree(), nodeId)) {
      const ast::NodeId condition(syntax.payload.words[ast::kWhileStmtCondWord]);
      if (boundModule.tree().contains(condition)) { self(condition, self); }
      return;
    }
    // An admitted do-while loop body is validated by
    // isAdmittedDoWhileStatement; skip the body block so its trailing
    // break/continue is not rejected as LoopControl. The condition (a
    // comparison or bare identifier) is still traversed.
    if (syntax.kind == ast::SyntaxKind::DoWhileStatement &&
        isAdmittedDoWhileStatement(boundModule.tree(), nodeId)) {
      const ast::NodeId condition(syntax.payload.words[ast::kDoWhileStatementCondWord]);
      if (boundModule.tree().contains(condition)) { self(condition, self); }
      return;
    }
    // An admitted for-loop's init, cond, and update are traversed so their
    // inner expressions are checked; the body (empty, one admitted loop-body
    // write, or one nested admitted for-loop) is skipped, matching the
    // while-loop traversal. A nested for-loop body has its own init/cond/
    // update traversed recursively.
    if (syntax.kind == ast::SyntaxKind::ForStmt &&
        isAdmittedForStatement(boundModule.tree(), nodeId)) {
      const ast::NodeId init(syntax.payload.words[ast::kForStmtInitWord]);
      const ast::NodeId cond(syntax.payload.words[ast::kForStmtCondWord]);
      const ast::NodeId update(syntax.payload.words[ast::kForStmtUpdateWord]);
      if (boundModule.tree().contains(init)) { self(init, self); }
      if (boundModule.tree().contains(cond)) { self(cond, self); }
      if (boundModule.tree().contains(update)) { self(update, self); }
      const ast::NodeId body(syntax.payload.words[ast::kForStmtBodyWord]);
      if (boundModule.tree().contains(body) &&
          boundModule.tree().node(body).kind == ast::SyntaxKind::BlockStmt) {
        const auto& bodyBlock = boundModule.tree().node(body);
        const ast::NodeList bodyStmts{bodyBlock.payload.words[ast::kBlockStmtStmtsFirstWord],
                                      bodyBlock.payload.words[ast::kBlockStmtStmtsSizeWord]};
        if (boundModule.tree().contains(bodyStmts) && bodyStmts.size == 1) {
          auto innerItem = statementItem(boundModule.tree(), boundModule.tree().list(bodyStmts)[0]);
          if (innerItem != zc::none) {
            ast::NodeId innerStmt;
            ZC_IF_SOME(item, innerItem) { innerStmt = item; }
            if (boundModule.tree().contains(innerStmt) &&
                boundModule.tree().node(innerStmt).kind == ast::SyntaxKind::ForStmt) {
              self(innerStmt, self);
            }
          }
        }
      }
      return;
    }
    ast::visitChildNodeIds(boundModule.tree(), syntax,
                           [&](ast::NodeId child) { self(child, self); });
  };
  walk(boundModule.tree().root(), walk);
  if (failures.size() != 0) {
    return SurfaceSourceRejected(zc::heap<SurfaceSourceRejected::Impl>(zc::mv(failures)));
  }
  return AdmittedBoundModule(zc::heap<AdmittedBoundModule::Impl>(zc::mv(boundModule)));
}

}  // namespace zomlang::compiler::ownership
