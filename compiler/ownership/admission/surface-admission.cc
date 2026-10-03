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
bool isAdmittedPrimitiveUnary(const ast::Tree& tree, ast::NodeId value);
bool isAdmittedCast(const ast::Tree& tree, ast::NodeId value);
bool isAdmittedTernary(const ast::Tree& tree, ast::NodeId value);
bool isAdmittedEnumVariant(const ast::Tree& tree, ast::NodeId value);
bool isAdmittedEnumVariantConstruction(const ast::Tree& tree, ast::NodeId value);
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
  if (tree.node(expression).kind != ast::SyntaxKind::AssignmentExpr) return false;
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
  if (tree.node(target).kind != ast::SyntaxKind::MemberExpression) return false;
  const ast::NodeId object(tree.node(target).payload.words[ast::kMemberExpressionObjectWord]);
  // A receiver field write (`this.field = <literal>`) is admitted structurally;
  // whether the enclosing method's receiver is mutable is a checker decision,
  // kept out of surface admission.
  return tree.contains(object) && (tree.node(object).kind == ast::SyntaxKind::IdentExpr ||
                                   tree.node(object).kind == ast::SyntaxKind::ThisExpr);
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

bool isAdmittedDirectCall(const ast::Tree& tree, ast::NodeId expression) {
  if (!tree.contains(expression) || tree.node(expression).kind != ast::SyntaxKind::CallExpression) {
    return false;
  }
  const auto& call = tree.node(expression);
  const ast::NodeId callee(call.payload.words[ast::kCallExpressionCalleeWord]);
  return tree.contains(callee) && tree.node(callee).kind == ast::SyntaxKind::IdentExpr &&
         hasAdmittedArguments(tree, call);
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
  return tree.contains(receiver) && tree.node(receiver).kind == ast::SyntaxKind::IdentExpr &&
         hasAdmittedArguments(tree, call, receiver);
}

bool isAdmittedReferenceReborrow(const ast::Tree& tree, ast::NodeId expression) {
  if (!tree.contains(expression) ||
      tree.node(expression).kind != ast::SyntaxKind::UnaryExpression) {
    return false;
  }
  const auto& borrow = tree.node(expression);
  const auto borrowOperator =
      static_cast<ast::UnaryOperatorKind>(borrow.payload.words[ast::kUnaryExpressionOpWord]);
  if (borrowOperator != ast::UnaryOperatorKind::Ref &&
      borrowOperator != ast::UnaryOperatorKind::RefMut) {
    return false;
  }
  const ast::NodeId dereference(borrow.payload.words[ast::kUnaryExpressionOperandWord]);
  if (!tree.contains(dereference) ||
      tree.node(dereference).kind != ast::SyntaxKind::UnaryExpression ||
      static_cast<ast::UnaryOperatorKind>(
          tree.node(dereference).payload.words[ast::kUnaryExpressionOpWord]) !=
          ast::UnaryOperatorKind::Deref) {
    return false;
  }
  const ast::NodeId reference(
      tree.node(dereference).payload.words[ast::kUnaryExpressionOperandWord]);
  return tree.contains(reference) && tree.node(reference).kind == ast::SyntaxKind::IdentExpr;
}

bool isAdmittedLocalBorrow(const ast::Tree& tree, ast::NodeId expression) {
  if (!tree.contains(expression) ||
      tree.node(expression).kind != ast::SyntaxKind::UnaryExpression) {
    return false;
  }
  const auto& borrow = tree.node(expression);
  const auto borrowOperator =
      static_cast<ast::UnaryOperatorKind>(borrow.payload.words[ast::kUnaryExpressionOpWord]);
  if (borrowOperator != ast::UnaryOperatorKind::Ref &&
      borrowOperator != ast::UnaryOperatorKind::RefMut) {
    return false;
  }
  const ast::NodeId operand(borrow.payload.words[ast::kUnaryExpressionOperandWord]);
  return tree.contains(operand) && tree.node(operand).kind == ast::SyntaxKind::IdentExpr;
}

bool isAdmittedErrorPostfix(const ast::Tree& tree, ast::NodeId expression) {
  if (!tree.contains(expression) ||
      tree.node(expression).kind != ast::SyntaxKind::PostfixExpression) {
    return false;
  }
  const auto& postfix = tree.node(expression);
  const auto operation =
      static_cast<ast::PostfixOperatorKind>(postfix.payload.words[ast::kPostfixExpressionOpWord]);
  if (operation != ast::PostfixOperatorKind::ErrorPropagate &&
      operation != ast::PostfixOperatorKind::ErrorUnwrap) {
    return false;
  }
  const ast::NodeId operand(postfix.payload.words[ast::kPostfixExpressionOperandWord]);
  return tree.contains(operand) && (tree.node(operand).kind == ast::SyntaxKind::IdentExpr ||
                                    isAdmittedDirectCall(tree, operand));
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
  // A leaf operand is an identifier or a scalar literal, never a further binary.
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
  return leftOk && rightOk && (leftIdent || rightIdent || leftNested || rightNested);
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

// A primitive unary return is one of the four arithmetic/logical/bitwise unary
// operators (`+` `-` `~` `!`) over one operand that is an identifier or a
// scalar literal, with at least one identifier so the checker has a typed
// anchor. Operator/type support is a checker decision kept out of surface
// admission.
bool isAdmittedPrimitiveUnary(const ast::Tree& tree, ast::NodeId value) {
  if (!tree.contains(value) || tree.node(value).kind != ast::SyntaxKind::UnaryExpression) {
    return false;
  }
  const auto op = static_cast<ast::UnaryOperatorKind>(
      tree.node(value).payload.words[ast::kUnaryExpressionOpWord]);
  if (op != ast::UnaryOperatorKind::Plus && op != ast::UnaryOperatorKind::Minus &&
      op != ast::UnaryOperatorKind::LogicalNot && op != ast::UnaryOperatorKind::BitNot) {
    return false;
  }
  const ast::NodeId operand(tree.node(value).payload.words[ast::kUnaryExpressionOperandWord]);
  if (!tree.contains(operand)) return false;
  return tree.node(operand).kind == ast::SyntaxKind::IdentExpr ||
         isScalarLiteral(tree.node(operand).kind);
}

bool isAdmittedCast(const ast::Tree& tree, ast::NodeId value) {
  if (!tree.contains(value) || tree.node(value).kind != ast::SyntaxKind::CastExpression) {
    return false;
  }
  // Only the `as` mode is admitted; `as?` and `as!` stay unsupported.
  if (tree.node(value).payload.words[ast::kCastExpressionModeWord] != 0) { return false; }
  const ast::NodeId inner(tree.node(value).payload.words[ast::kCastExpressionExprWord]);
  if (!tree.contains(inner)) return false;
  // The inner expression must be a scalar literal in this slice. Identifier
  // and composite operand casts are checker/HIR decisions kept out of
  // surface admission.
  return isScalarLiteral(tree.node(inner).kind);
}

bool isAdmittedTernary(const ast::Tree& tree, ast::NodeId value) {
  if (!tree.contains(value) || tree.node(value).kind != ast::SyntaxKind::ConditionalExpr) {
    return false;
  }
  const auto& ternary = tree.node(value);
  const ast::NodeId cond(ternary.payload.words[ast::kConditionalExprCondWord]);
  const ast::NodeId thenExpr(ternary.payload.words[ast::kConditionalExprThenExprWord]);
  const ast::NodeId elseExpr(ternary.payload.words[ast::kConditionalExprElseExprWord]);
  if (!tree.contains(cond) || !tree.contains(thenExpr) || !tree.contains(elseExpr)) {
    return false;
  }
  // The condition must be a bool literal or a bare identifier (bool parameter
  // or local).
  if (tree.node(cond).kind != ast::SyntaxKind::IdentExpr &&
      tree.node(cond).kind != ast::SyntaxKind::BoolLiteral) {
    return false;
  }
  // Both branches must be scalar literals in this slice.
  return isScalarLiteral(tree.node(thenExpr).kind) && isScalarLiteral(tree.node(elseExpr).kind);
}

// A two-arm boolean match expression `match (scrutinee) { when true => e1;
// when false => e2; }` normalizes to the ternary conditional-select path. The
// scrutinee must be a bool literal or bare identifier; both arm bodies must be
// scalar literals. Every other match-expr shape stays rejected by its existing
// drain.
bool isAdmittedMatchExpression(const ast::Tree& tree, ast::NodeId value) {
  if (!tree.contains(value) || tree.node(value).kind != ast::SyntaxKind::MatchExpr) {
    return false;
  }
  const auto& matchNode = tree.node(value);
  const ast::NodeId scrutinee(matchNode.payload.words[ast::kMatchExprScrutineeWord]);
  const ast::NodeList arms{matchNode.payload.words[ast::kMatchExprArmsFirstWord],
                           matchNode.payload.words[ast::kMatchExprArmsSizeWord]};
  if (!tree.contains(scrutinee) || !tree.contains(arms) || arms.size != 2) { return false; }
  if (tree.node(scrutinee).kind != ast::SyntaxKind::IdentExpr &&
      tree.node(scrutinee).kind != ast::SyntaxKind::BoolLiteral) {
    return false;
  }
  bool sawTrue = false;
  bool sawFalse = false;
  for (size_t index = 0; index < arms.size; ++index) {
    const ast::NodeId armId = tree.list(arms)[index];
    if (!tree.contains(armId) || tree.node(armId).kind != ast::SyntaxKind::MatchArmExpr) {
      return false;
    }
    const auto& arm = tree.node(armId);
    const ast::NodeId guard(arm.payload.words[ast::kMatchArmExprGuardWord]);
    if (tree.contains(guard)) return false;
    const ast::NodeId pattern(arm.payload.words[ast::kMatchArmExprPatternWord]);
    if (!tree.contains(pattern) || tree.node(pattern).kind != ast::SyntaxKind::LiteralPattern) {
      return false;
    }
    const ast::NodeId literal(tree.node(pattern).payload.words[ast::kLiteralPatternLiteralWord]);
    if (!tree.contains(literal) || tree.node(literal).kind != ast::SyntaxKind::BoolLiteral) {
      return false;
    }
    if (tree.node(literal).payload.words[ast::kBoolLiteralValueWord] != 0) {
      sawTrue = true;
    } else {
      sawFalse = true;
    }
    const ast::NodeId body(arm.payload.words[ast::kMatchArmExprBodyWord]);
    if (!tree.contains(body) || !isScalarLiteral(tree.node(body).kind)) { return false; }
  }
  return sawTrue && sawFalse;
}

// A qualified enum variant access (`Color::Red`) lowers to an integer constant
// (the variant discriminant). The base must be a bare identifier naming the
// enum type; the checker verifies the binding and discriminant.
bool isAdmittedEnumVariant(const ast::Tree& tree, ast::NodeId value) {
  if (!tree.contains(value) || tree.node(value).kind != ast::SyntaxKind::MemberExpression) {
    return false;
  }
  const auto& member = tree.node(value);
  if (static_cast<ast::MemberAccessKind>(member.payload.words[ast::kMemberExpressionAccessWord]) !=
      ast::MemberAccessKind::Qualified) {
    return false;
  }
  const ast::NodeId object(member.payload.words[ast::kMemberExpressionObjectWord]);
  return tree.contains(object) && tree.node(object).kind == ast::SyntaxKind::IdentExpr;
}

bool isAdmittedEnumVariantConstruction(const ast::Tree& tree, ast::NodeId value) {
  if (!tree.contains(value) || tree.node(value).kind != ast::SyntaxKind::CallExpression) {
    return false;
  }
  const auto& call = tree.node(value);
  const ast::NodeId callee(call.payload.words[ast::kCallExpressionCalleeWord]);
  if (!tree.contains(callee) || tree.node(callee).kind != ast::SyntaxKind::MemberExpression) {
    return false;
  }
  if (static_cast<ast::MemberAccessKind>(
          tree.node(callee).payload.words[ast::kMemberExpressionAccessWord]) !=
      ast::MemberAccessKind::Qualified) {
    return false;
  }
  const ast::NodeId object(tree.node(callee).payload.words[ast::kMemberExpressionObjectWord]);
  return tree.contains(object) && tree.node(object).kind == ast::SyntaxKind::IdentExpr;
}

bool isAdmittedReturnValue(const ast::Tree& tree, ast::NodeId value) {
  if (!tree.contains(value)) return false;
  return isScalarLiteral(tree.node(value).kind) ||
         tree.node(value).kind == ast::SyntaxKind::IdentExpr || isAdmittedDirectCall(tree, value) ||
         isAdmittedReceiverCall(tree, value) || isAdmittedReferenceReborrow(tree, value) ||
         isAdmittedLocalBorrow(tree, value) || isAdmittedErrorPostfix(tree, value) ||
         isAdmittedPrimitiveBinary(tree, value) || isAdmittedPrimitiveUnary(tree, value) ||
         tree.node(value).kind == ast::SyntaxKind::UnsafeBlockExpr;
}

bool isAdmittedAggregateInitializer(const ast::Tree& tree, ast::NodeId initializer) {
  if (!tree.contains(initializer) ||
      tree.node(initializer).kind != ast::SyntaxKind::StructLiteralExpr) {
    return false;
  }
  const auto& aggregate = tree.node(initializer);
  const ast::NodeId type(aggregate.payload.words[ast::kStructLiteralExprTyWord]);
  if (!tree.contains(type) || tree.node(type).kind != ast::SyntaxKind::NamedTypeExpr) {
    return false;
  }
  const auto& namedType = tree.node(type);
  const ast::NodeList arguments{namedType.payload.words[ast::kNamedTypeExprArgsFirstWord],
                                namedType.payload.words[ast::kNamedTypeExprArgsSizeWord]};
  const ast::NodeId path(namedType.payload.words[ast::kNamedTypeExprPathWord]);
  if (!tree.contains(arguments) || !arguments.empty() || !tree.contains(path) ||
      tree.node(path).kind != ast::SyntaxKind::ModulePath) {
    return false;
  }
  const auto& modulePath = tree.node(path);
  const ast::IdentList segments{modulePath.payload.words[ast::kModulePathSegmentsFirstWord],
                                modulePath.payload.words[ast::kModulePathSegmentsSizeWord]};
  if (!tree.contains(segments) || segments.size != 1) return false;
  const auto name = tree.ident(tree.identList(segments)[0]);
  bool declaredStruct = false;
  ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId, const ast::Node& syntax) {
    if (syntax.kind == ast::SyntaxKind::StructDecl &&
        tree.ident(ast::IdentId(syntax.payload.words[ast::kStructDeclNameWord])) == name) {
      declaredStruct = true;
    }
  });
  if (!declaredStruct) return false;
  const ast::NodeList properties{
      aggregate.payload.words[ast::kStructLiteralExprPropertiesFirstWord],
      aggregate.payload.words[ast::kStructLiteralExprPropertiesSizeWord]};
  if (!tree.contains(properties)) return false;
  for (const auto property : tree.list(properties)) {
    if (!tree.contains(property) || tree.node(property).kind != ast::SyntaxKind::ObjectProperty) {
      return false;
    }
    const ast::NodeId value(tree.node(property).payload.words[ast::kObjectPropertyValueWord]);
    if (!tree.contains(value) || !isScalarLiteral(tree.node(value).kind)) return false;
  }
  return true;
}

// Structurally admits a local initializer. A primitive-binary initializer is
// admitted on its structural shape alone: the checker derives the result type
// from the operand leaves (arithmetic yields the operand type, comparison
// yields bool), so no binding annotation is required. Literal-literal binaries
// have no typed operand to anchor inference and stay rejected by
// isAdmittedPrimitiveBinary.
bool isAdmittedLocalInitializer(const ast::Tree& tree, ast::NodeId declarator,
                                ast::NodeId initializer) {
  (void)declarator;
  if (!tree.contains(initializer)) return true;
  return isScalarLiteral(tree.node(initializer).kind) ||
         tree.node(initializer).kind == ast::SyntaxKind::IdentExpr ||
         isAdmittedDirectCall(tree, initializer) ||
         isAdmittedAggregateInitializer(tree, initializer) ||
         isAdmittedPrimitiveBinary(tree, initializer) ||
         isAdmittedPrimitiveUnary(tree, initializer) || isAdmittedCast(tree, initializer) ||
         isAdmittedTernary(tree, initializer) || isAdmittedMatchExpression(tree, initializer) ||
         isAdmittedEnumVariant(tree, initializer) ||
         isAdmittedEnumVariantConstruction(tree, initializer);
}

bool matchesLocalReference(const ast::Tree& tree, ast::NodeId pattern, ast::NodeId reference) {
  if (!tree.contains(pattern) || !tree.contains(reference) ||
      tree.node(pattern).kind != ast::SyntaxKind::IdentifierPattern ||
      tree.node(reference).kind != ast::SyntaxKind::IdentExpr) {
    return false;
  }
  return tree.node(pattern).payload.words[ast::kIdentifierPatternNameWord] ==
         tree.node(reference).payload.words[ast::kIdentExprNameWord];
}

zc::Maybe<ast::NodeId> localDeclarator(const ast::Tree& tree, ast::NodeId statement) {
  auto item = statementItem(tree, statement);
  if (item == zc::none) return zc::none;
  ast::NodeId local;
  ZC_IF_SOME(value, item) { local = value; }
  if (tree.node(local).kind != ast::SyntaxKind::LetStmt) return zc::none;
  const ast::NodeId declarations(tree.node(local).payload.words[ast::kLetStmtDeclarationsWord]);
  if (!tree.contains(declarations) ||
      tree.node(declarations).kind != ast::SyntaxKind::VariableDeclaratorList) {
    return zc::none;
  }
  const auto& declarationList = tree.node(declarations);
  const ast::NodeList declarators{
      declarationList.payload.words[ast::kVariableDeclaratorListDeclsFirstWord],
      declarationList.payload.words[ast::kVariableDeclaratorListDeclsSizeWord]};
  if (!tree.contains(declarators) || declarators.size != 1) return zc::none;
  const ast::NodeId declarator(tree.list(declarators)[0]);
  if (!tree.contains(declarator) ||
      tree.node(declarator).kind != ast::SyntaxKind::VariableDeclarator) {
    return zc::none;
  }
  const ast::NodeId pattern(
      tree.node(declarator).payload.words[ast::kVariableDeclaratorPatternWord]);
  const ast::NodeId initializer(
      tree.node(declarator).payload.words[ast::kVariableDeclaratorInitWord]);
  if (!tree.contains(pattern) || tree.node(pattern).kind != ast::SyntaxKind::IdentifierPattern ||
      !tree.contains(initializer)) {
    return zc::none;
  }
  return declarator;
}

/// \brief Structurally admits one `while`-body write statement.
///
/// The statement is an `<ident> = <scalar literal | identifier | admitted
/// primitive binary>;` assignment. This is exactly the scalar-local write shape
/// the leading-declaration mut-local path admits; a field write is not admitted
/// in a loop body. Structure only; the checker/HIR decide which references and
/// operators are supported and which local the write targets.
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
/// The statement is an unlabeled `break;` or `continue;`. A labeled break or
/// continue stays outside this slice. Structure only; the checker/HIR decide
/// the loop exit and back-edge lowering.
bool isAdmittedLoopBodyControlFlow(const ast::Tree& tree, ast::NodeId statement) {
  auto item = statementItem(tree, statement);
  if (item == zc::none) return false;
  ast::NodeId stmt;
  ZC_IF_SOME(value, item) { stmt = value; }
  const auto kind = tree.node(stmt).kind;
  if (kind != ast::SyntaxKind::BreakStmt && kind != ast::SyntaxKind::ContinueStatement) {
    return false;
  }
  const auto labelWord = kind == ast::SyntaxKind::BreakStmt ? ast::kBreakStmtLabelWord
                                                            : ast::kContinueStatementLabelWord;
  return tree.node(stmt).payload.words[labelWord] == 0;
}

/// \brief Structurally admits one if-guarded `break;` loop-body statement.
///
/// The statement is `if (<ident|lit> <cmp> <ident|lit>) { break; }`: an `if`
/// with no `else` whose condition is a binary comparison over an identifier and
/// a scalar literal and whose body is a block holding exactly one unlabeled
/// `break;`. The guard evaluates before the accumulator writes in source order,
/// so the loop body splits into a guard block and a continuation block
/// downstream. Structure only; the checker/HIR decide the comparison operator
/// and the loop-exit lowering.
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
  if (!tree.contains(condition) || tree.node(condition).kind != ast::SyntaxKind::IdentExpr) {
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
  // one trailing unlabeled break/continue, or one if-guarded break followed by
  // N admitted loop-body writes. The guarded break evaluates before the writes
  // in source order, so it must lead the body. A break/continue is admitted
  // only as the trailing body statement, matching the while-loop body slice.
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
  // admitted loop-body writes.
  if (isAdmittedLoopBodyGuardedBreak(tree, statementNodes[0])) {
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

// A nested arithmetic operand is a one-level binary whose own operands are
// leaves (identifier or scalar literal), with at least one identifier leaf.
// Used as a comparison operand in conditional bodies so the HIR builder can
// synthesize a temp binding for the arithmetic result.
bool isAdmittedNestedArithmeticOperand(const ast::Tree& tree, ast::NodeId operand) {
  if (!tree.contains(operand) || tree.node(operand).kind != ast::SyntaxKind::BinaryExpr) {
    return false;
  }
  const ast::NodeId innerLeft(tree.node(operand).payload.words[ast::kBinaryExprLhsWord]);
  const ast::NodeId innerRight(tree.node(operand).payload.words[ast::kBinaryExprRhsWord]);
  if (!tree.contains(innerLeft) || !tree.contains(innerRight)) return false;
  auto isLeaf = [&](ast::NodeId node) {
    return tree.node(node).kind == ast::SyntaxKind::IdentExpr ||
           isScalarLiteral(tree.node(node).kind);
  };
  const bool innerLeftIdent = tree.node(innerLeft).kind == ast::SyntaxKind::IdentExpr;
  const bool innerRightIdent = tree.node(innerRight).kind == ast::SyntaxKind::IdentExpr;
  return isLeaf(innerLeft) && isLeaf(innerRight) && (innerLeftIdent || innerRightIdent);
}

bool isAdmittedConditionalBody(const ast::Tree& tree, ast::NodeId ifStmt) {
  const auto& ifNode = tree.node(ifStmt);
  // Admit two structural condition shapes: a bare identifier (a bool parameter
  // reference) or a binary comparison whose operands are each an identifier or a
  // scalar literal, with at least one identifier operand. A literal-vs-literal
  // comparison has no parameter to lower and would constant-fold, so it fails
  // closed here. Which comparison operators are actually supported is a checker
  // decision (this admits every relational-shaped comparison and the checker
  // rejects the unsupported operators), keeping the operator-support contract in
  // a single place. Every other condition shape fails closed.
  const ast::NodeId condition(ifNode.payload.words[ast::kIfStmtCondWord]);
  if (!tree.contains(condition)) return false;
  if (tree.node(condition).kind == ast::SyntaxKind::BinaryExpr) {
    const ast::NodeId left(tree.node(condition).payload.words[ast::kBinaryExprLhsWord]);
    const ast::NodeId right(tree.node(condition).payload.words[ast::kBinaryExprRhsWord]);
    if (!tree.contains(left) || !tree.contains(right)) return false;
    const bool leftIdent = tree.node(left).kind == ast::SyntaxKind::IdentExpr;
    const bool rightIdent = tree.node(right).kind == ast::SyntaxKind::IdentExpr;
    const bool leftNested = isAdmittedNestedArithmeticOperand(tree, left);
    const bool rightNested = isAdmittedNestedArithmeticOperand(tree, right);
    // At most one operand may be nested in this slice.
    if (leftNested && rightNested) return false;
    const bool leftOk = leftIdent || isScalarLiteral(tree.node(left).kind) || leftNested;
    const bool rightOk = rightIdent || isScalarLiteral(tree.node(right).kind) || rightNested;
    if (!leftOk || !rightOk || (!leftIdent && !rightIdent && !leftNested && !rightNested)) {
      return false;
    }
  } else if (tree.node(condition).kind == ast::SyntaxKind::UnaryExpression) {
    // Admit the unary `!x` condition: a LogicalNot whose operand is an
    // identifier or a scalar literal. The HIR builder desugars `!x` to
    // `x == false`, reusing the comparison condition path.
    const auto unaryOp = static_cast<ast::UnaryOperatorKind>(
        tree.node(condition).payload.words[ast::kUnaryExpressionOpWord]);
    if (unaryOp != ast::UnaryOperatorKind::LogicalNot) return false;
    const ast::NodeId operand(tree.node(condition).payload.words[ast::kUnaryExpressionOperandWord]);
    if (!tree.contains(operand)) return false;
    if (tree.node(operand).kind != ast::SyntaxKind::IdentExpr &&
        !isScalarLiteral(tree.node(operand).kind)) {
      return false;
    }
  } else if (tree.node(condition).kind != ast::SyntaxKind::IdentExpr) {
    return false;
  }
  const ast::NodeId thenStmt(ifNode.payload.words[ast::kIfStmtThenStmtWord]);
  const ast::NodeId elseStmt(ifNode.payload.words[ast::kIfStmtElseStmtWord]);
  if (!tree.contains(thenStmt) || !tree.contains(elseStmt)) return false;
  if (tree.node(thenStmt).kind != ast::SyntaxKind::BlockStmt ||
      tree.node(elseStmt).kind != ast::SyntaxKind::BlockStmt) {
    return false;
  }
  auto branchReturns = [&](ast::NodeId branch) -> bool {
    const auto& branchNode = tree.node(branch);
    const ast::NodeList branchStmts{branchNode.payload.words[ast::kBlockStmtStmtsFirstWord],
                                    branchNode.payload.words[ast::kBlockStmtStmtsSizeWord]};
    if (!tree.contains(branchStmts) || branchStmts.empty()) return false;
    auto tail = statementItem(tree, tree.list(branchStmts)[branchStmts.size - 1]);
    if (tail == zc::none) return false;
    ast::NodeId tailStmt;
    ZC_IF_SOME(value, tail) { tailStmt = value; }
    if (tree.node(tailStmt).kind != ast::SyntaxKind::ReturnStmt) return false;
    const ast::NodeId returnValue(tree.node(tailStmt).payload.words[ast::kReturnStmtValueWord]);
    return tree.contains(returnValue) && isAdmittedReturnValue(tree, returnValue);
  };
  return branchReturns(thenStmt) && branchReturns(elseStmt);
}

// Structurally admits a match-arm guard: a single binary expression whose one
// operand is a bare identifier (a parameter reference resolved downstream) and
// whose other operand is a scalar literal. The operator and type support are
// checker decisions kept out of surface admission. A guard on the default
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
// duplicates), an integer literal (N, paired with a default arm), an enum
// unit-variant pattern (N >= 2, no default needed on the closed domain), or a
// `default` (wildcard) arm; the literal arm may carry a guard that is a single
// identifier-vs-scalar-literal binary; each arm body tails a scalar-literal
// return. The HIR builder lowers the bool shape to the same conditional path
// as a bare-parameter `if` and the integer shape to the equality conditional
// path, so the admitted surface is exactly the conditional surface.
bool isAdmittedMatchStatement(const ast::Tree& tree, ast::NodeId node) {
  const auto& matchNode = tree.node(node);
  if (matchNode.kind != ast::SyntaxKind::MatchStmt) return false;
  const ast::NodeId scrutinee(matchNode.payload.words[ast::kMatchStmtScrutineeWord]);
  if (!tree.contains(scrutinee) || tree.node(scrutinee).kind != ast::SyntaxKind::IdentExpr) {
    return false;
  }
  const ast::NodeList arms{matchNode.payload.words[ast::kMatchStmtArmsFirstWord],
                           matchNode.payload.words[ast::kMatchStmtArmsSizeWord]};
  if (!tree.contains(arms) || arms.size < 2) return false;
  bool sawTrue = false;
  bool sawFalse = false;
  size_t intLiteralCount = 0;
  bool sawDefault = false;
  bool sawGuard = false;
  bool sawEnumPattern = false;
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
      // A unit enum variant pattern (e.g., `Color.Red`) is admitted when the
      // scrutinee is an enum type. The variant resolution and type matching
      // are checker decisions.
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
    if (!tree.contains(returnValue) || !isScalarLiteral(tree.node(returnValue).kind)) {
      return false;
    }
  }
  // Bool: two literal arms (true + false), or one literal arm plus one default
  // arm. Integer: N literal arms plus one default arm. Enum: N (>= 2)
  // unit-variant pattern arms on the same enum type; the closed domain needs
  // no default arm.
  if (intLiteralCount > 0) { return sawDefault && arms.size == intLiteralCount + 1; }
  if (sawEnumPattern) return arms.size >= 2;
  if (arms.size != 2) return false;
  return sawDefault ? (sawTrue != sawFalse) : (sawTrue && sawFalse);
}

// Structurally admits `return <identifier>.<field>;` in a statement-less
// function body: one dot field projection off a bare identifier. In such a body
// the identifier is a by-value callable parameter; the checker resolves it and a
// non-parameter identifier keeps its owner on the capability drain. A chained
// projection (`a.b.c`) is intentionally not admitted.
bool isAdmittedParameterFieldReturn(const ast::Tree& tree, ast::NodeId value) {
  if (!tree.contains(value) || tree.node(value).kind != ast::SyntaxKind::MemberExpression) {
    return false;
  }
  const auto& member = tree.node(value);
  if (static_cast<ast::MemberAccessKind>(member.payload.words[ast::kMemberExpressionAccessWord]) !=
      ast::MemberAccessKind::Dot) {
    return false;
  }
  const ast::NodeId object(member.payload.words[ast::kMemberExpressionObjectWord]);
  return tree.contains(object) && tree.node(object).kind == ast::SyntaxKind::IdentExpr;
}

bool isAdmittedFunctionBody(const ast::Tree& tree, const ast::Node& function) {
  const ast::NodeId body(function.payload.words[ast::kFunctionDeclBodyWord]);
  if (!tree.contains(body)) return true;
  if (tree.node(body).kind != ast::SyntaxKind::BlockStmt) return false;
  const auto& block = tree.node(body);
  const ast::NodeList statements{block.payload.words[ast::kBlockStmtStmtsFirstWord],
                                 block.payload.words[ast::kBlockStmtStmtsSizeWord]};
  if (!tree.contains(statements) || statements.empty()) return false;
  if (statements.size == 1) {
    auto item = statementItem(tree, tree.list(statements)[0]);
    if (item != zc::none) {
      ast::NodeId stmt;
      ZC_IF_SOME(value, item) { stmt = value; }
      if (tree.node(stmt).kind == ast::SyntaxKind::IfStmt) {
        return isAdmittedConditionalBody(tree, stmt);
      }
      if (tree.node(stmt).kind == ast::SyntaxKind::MatchStmt) {
        return isAdmittedMatchStatement(tree, stmt);
      }
    }
  }
  // Leading scalar-local bindings followed by one comparison conditional:
  // zero-or-more (in practice one or more here, since zero is the sole-if
  // shape above) leading `let id = <scalar literal | identifier>;` statements
  // followed by a single explicit-else `if` whose relational comparison reads
  // identifiers or scalar literals (at least one identifier) and whose arms
  // each tail-return a scalar literal. Every other leading-let + if shape
  // (non-comparison condition, non-literal arms, other initializer kinds)
  // fails closed to its existing drain.
  if (statements.size >= 2) {
    auto tailItem = statementItem(tree, tree.list(statements)[statements.size - 1]);
    if (tailItem != zc::none) {
      ast::NodeId tailStmt;
      ZC_IF_SOME(value, tailItem) { tailStmt = value; }
      if (tree.node(tailStmt).kind == ast::SyntaxKind::IfStmt) {
        bool allLeadingBindings = true;
        for (size_t index = 0; index + 1 < statements.size; ++index) {
          auto declaratorNode = localDeclarator(tree, tree.list(statements)[index]);
          if (declaratorNode == zc::none) {
            allLeadingBindings = false;
            break;
          }
          ast::NodeId declarator;
          ZC_IF_SOME(value, declaratorNode) { declarator = value; }
          const ast::NodeId leadingInitializer(
              tree.node(declarator).payload.words[ast::kVariableDeclaratorInitWord]);
          if (!tree.contains(leadingInitializer) ||
              (!isScalarLiteral(tree.node(leadingInitializer).kind) &&
               tree.node(leadingInitializer).kind != ast::SyntaxKind::IdentExpr)) {
            allLeadingBindings = false;
            break;
          }
        }
        if (allLeadingBindings && isAdmittedConditionalBody(tree, tailStmt)) {
          const ast::NodeId condition(tree.node(tailStmt).payload.words[ast::kIfStmtCondWord]);
          const ast::NodeId thenStmt(tree.node(tailStmt).payload.words[ast::kIfStmtThenStmtWord]);
          const ast::NodeId elseStmt(tree.node(tailStmt).payload.words[ast::kIfStmtElseStmtWord]);
          auto armTailLiteral = [&](ast::NodeId branch) -> bool {
            const auto& branchNode = tree.node(branch);
            const ast::NodeList branchStmts{branchNode.payload.words[ast::kBlockStmtStmtsFirstWord],
                                            branchNode.payload.words[ast::kBlockStmtStmtsSizeWord]};
            if (!tree.contains(branchStmts) || branchStmts.empty()) return false;
            auto tail = statementItem(tree, tree.list(branchStmts)[branchStmts.size - 1]);
            if (tail == zc::none) return false;
            ast::NodeId tailReturn;
            ZC_IF_SOME(value, tail) { tailReturn = value; }
            if (tree.node(tailReturn).kind != ast::SyntaxKind::ReturnStmt) return false;
            const ast::NodeId returnValue(
                tree.node(tailReturn).payload.words[ast::kReturnStmtValueWord]);
            return tree.contains(returnValue) && isScalarLiteral(tree.node(returnValue).kind);
          };
          if (tree.contains(condition) &&
              (tree.node(condition).kind == ast::SyntaxKind::BinaryExpr ||
               (tree.node(condition).kind == ast::SyntaxKind::UnaryExpression &&
                static_cast<ast::UnaryOperatorKind>(
                    tree.node(condition).payload.words[ast::kUnaryExpressionOpWord]) ==
                    ast::UnaryOperatorKind::LogicalNot)) &&
              armTailLiteral(thenStmt) && armTailLiteral(elseStmt)) {
            return true;
          }
        }
      }
    }
  }
  if (statements.size == 2) {
    // A leading admitted `while` loop followed by a scalar return is an admitted
    // function body. The loop condition is a bool parameter reference and the
    // loop body is empty, so the loop lowers to a reducible four-block CFG.
    auto leadingItem = statementItem(tree, tree.list(statements)[0]);
    if (leadingItem != zc::none) {
      ast::NodeId leadingStmt;
      ZC_IF_SOME(value, leadingItem) { leadingStmt = value; }
      if (tree.node(leadingStmt).kind == ast::SyntaxKind::WhileStmt) {
        if (!isAdmittedLoopStatement(tree, leadingStmt)) return false;
        auto tailItem = statementItem(tree, tree.list(statements)[1]);
        if (tailItem == zc::none) return false;
        ast::NodeId tailStmt;
        ZC_IF_SOME(value, tailItem) { tailStmt = value; }
        if (tree.node(tailStmt).kind != ast::SyntaxKind::ReturnStmt) return false;
        const ast::NodeId returnValue(tree.node(tailStmt).payload.words[ast::kReturnStmtValueWord]);
        return tree.contains(returnValue) && isScalarLiteral(tree.node(returnValue).kind);
      }
    }
  }
  auto finalStatement = statementItem(tree, tree.list(statements)[statements.size - 1]);
  if (finalStatement == zc::none) return false;
  ZC_IF_SOME(statement, finalStatement) {
    if (tree.node(statement).kind != ast::SyntaxKind::ReturnStmt) return false;
  }
  ast::NodeId returnNode;
  ZC_IF_SOME(statement, finalStatement) { returnNode = statement; }
  const ast::NodeId returnValue(tree.node(returnNode).payload.words[ast::kReturnStmtValueWord]);
  if (!tree.contains(returnValue)) return true;
  if (statements.size == 1)
    return isAdmittedReturnValue(tree, returnValue) ||
           isAdmittedParameterFieldReturn(tree, returnValue);

  if (statements.size == 3) {
    // A leading mutable-local declaration, an admitted `while` loop whose body
    // writes that local, and a `return <ident>;` is an admitted function body.
    // The loop lowers to a reducible four-block CFG whose body block carries the
    // write assignments before the back-edge Goto. The mut-local declaration and
    // the local return reuse the sequential/mut-local shape's admission; only the
    // middle statement being an admitted loop distinguishes this shape.
    auto middleItem = statementItem(tree, tree.list(statements)[1]);
    if (middleItem != zc::none) {
      ast::NodeId middleStmt;
      ZC_IF_SOME(value, middleItem) { middleStmt = value; }
      if (tree.node(middleStmt).kind == ast::SyntaxKind::WhileStmt) {
        auto declarator = localDeclarator(tree, tree.list(statements)[0]);
        if (declarator == zc::none) return false;
        ast::NodeId declaratorNode;
        ZC_IF_SOME(value, declarator) { declaratorNode = value; }
        auto leadingItem = statementItem(tree, tree.list(statements)[0]);
        ast::NodeId letNode;
        ZC_IF_SOME(value, leadingItem) { letNode = value; }
        if (static_cast<ast::BindingDeclarationKind>(
                tree.node(letNode).payload.words[ast::kLetStmtKindWord]) !=
            ast::BindingDeclarationKind::Mut) {
          return false;
        }
        const ast::NodeId pattern(
            tree.node(declaratorNode).payload.words[ast::kVariableDeclaratorPatternWord]);
        const ast::NodeId initializer(
            tree.node(declaratorNode).payload.words[ast::kVariableDeclaratorInitWord]);
        if (!isAdmittedLocalInitializer(tree, declaratorNode, initializer)) return false;
        if (!isAdmittedLoopStatement(tree, middleStmt)) return false;
        // Every loop-body write targets the declared local (checked by identifier
        // name; the target type and mutability are a checker decision). A
        // trailing break/continue carries no write target and is skipped.
        const ast::NodeId body(tree.node(middleStmt).payload.words[ast::kWhileStmtBodyWord]);
        const auto& bodyBlock = tree.node(body);
        const ast::NodeList bodyStatements{bodyBlock.payload.words[ast::kBlockStmtStmtsFirstWord],
                                           bodyBlock.payload.words[ast::kBlockStmtStmtsSizeWord]};
        for (const auto statement : tree.list(bodyStatements)) {
          auto item = statementItem(tree, statement);
          if (item == zc::none) return false;
          ast::NodeId writeStmt;
          ZC_IF_SOME(value, item) { writeStmt = value; }
          if (tree.node(writeStmt).kind == ast::SyntaxKind::BreakStmt ||
              tree.node(writeStmt).kind == ast::SyntaxKind::ContinueStatement) {
            continue;
          }
          const ast::NodeId assignment(
              tree.node(writeStmt).payload.words[ast::kExpressionStatementExpressionWord]);
          const ast::NodeId target(
              tree.node(assignment).payload.words[ast::kAssignmentExprLhsWord]);
          if (!matchesLocalReference(tree, pattern, target)) return false;
        }
        return tree.node(returnValue).kind == ast::SyntaxKind::IdentExpr &&
               matchesLocalReference(tree, pattern, returnValue);
      }
    }
  }

  // Sequential local shape: N leading `let <ident>: T = <initializer>;`
  // statements followed by a single `return <ident>;`. Each initializer is a
  // scalar literal, a closed nominal aggregate, an identifier reference (a
  // parameter or an earlier local), or a primitive binary operation. The
  // returned value is an identifier (a parameter or one of the declared
  // locals). N>=2 is always this shape; an N==1 (two-statement) body is claimed
  // here only when its single binding is a primitive binary, so that a binary
  // result followed by a parameter return admits through the same rail. Any
  // other single-`let` body falls through to the dedicated single-local shape.
  bool sequentialLocalShape = statements.size >= 3;
  if (!sequentialLocalShape && statements.size == 2) {
    auto soleDeclarator = localDeclarator(tree, tree.list(statements)[0]);
    ZC_IF_SOME(declarator, soleDeclarator) {
      const ast::NodeId soleInitializer(
          tree.node(declarator).payload.words[ast::kVariableDeclaratorInitWord]);
      // An unannotated binary-, unary-, cast-, or ternary-result binding
      // drains through the single-local gate below as ZOM4099; the sequential
      // rail requires the annotation.
      if (isAdmittedLocalInitializer(tree, declarator, soleInitializer) &&
          tree.contains(soleInitializer) &&
          (tree.node(soleInitializer).kind == ast::SyntaxKind::BinaryExpr ||
           tree.node(soleInitializer).kind == ast::SyntaxKind::UnaryExpression ||
           tree.node(soleInitializer).kind == ast::SyntaxKind::CastExpression ||
           tree.node(soleInitializer).kind == ast::SyntaxKind::ConditionalExpr ||
           tree.node(soleInitializer).kind == ast::SyntaxKind::MatchExpr)) {
        sequentialLocalShape = true;
      }
    }
  }
  if (sequentialLocalShape) {
    bool allLeadingLets = true;
    for (size_t index = 0; index + 1 < statements.size; ++index) {
      if (localDeclarator(tree, tree.list(statements)[index]) == zc::none) {
        allLeadingLets = false;
        break;
      }
    }
    if (allLeadingLets) {
      if (tree.node(returnValue).kind != ast::SyntaxKind::IdentExpr) return false;
      for (size_t index = 0; index + 1 < statements.size; ++index) {
        auto declarator = localDeclarator(tree, tree.list(statements)[index]);
        ast::NodeId declaratorNode;
        ZC_IF_SOME(value, declarator) { declaratorNode = value; }
        const ast::NodeId initializer(
            tree.node(declaratorNode).payload.words[ast::kVariableDeclaratorInitWord]);
        if (!tree.contains(initializer)) return false;
        if (!isAdmittedLocalInitializer(tree, declaratorNode, initializer)) { return false; }
      }
      return true;
    }
  }

  auto localStatement = statementItem(tree, tree.list(statements)[0]);
  if (localStatement == zc::none) return false;
  ast::NodeId localNode;
  ZC_IF_SOME(statement, localStatement) { localNode = statement; }
  if (tree.node(localNode).kind != ast::SyntaxKind::LetStmt) return false;
  const auto& declaration = tree.node(localNode);
  const ast::NodeId declarations(declaration.payload.words[ast::kLetStmtDeclarationsWord]);
  if (!tree.contains(declarations) ||
      tree.node(declarations).kind != ast::SyntaxKind::VariableDeclaratorList) {
    return false;
  }
  const auto& declarationList = tree.node(declarations);
  const ast::NodeList declarators{
      declarationList.payload.words[ast::kVariableDeclaratorListDeclsFirstWord],
      declarationList.payload.words[ast::kVariableDeclaratorListDeclsSizeWord]};
  if (!tree.contains(declarators) || declarators.size != 1) return false;
  const ast::NodeId declarator(tree.list(declarators)[0]);
  if (!tree.contains(declarator) ||
      tree.node(declarator).kind != ast::SyntaxKind::VariableDeclarator) {
    return false;
  }
  const ast::NodeId pattern(
      tree.node(declarator).payload.words[ast::kVariableDeclaratorPatternWord]);
  const ast::NodeId initializer(
      tree.node(declarator).payload.words[ast::kVariableDeclaratorInitWord]);
  if (!tree.contains(pattern) || tree.node(pattern).kind != ast::SyntaxKind::IdentifierPattern ||
      !isAdmittedLocalInitializer(tree, declarator, initializer)) {
    return false;
  }
  ast::NodeId returnReference = returnValue;
  const bool returnsField = tree.node(returnValue).kind == ast::SyntaxKind::MemberExpression;
  const bool returnsReceiverCall = isAdmittedReceiverCall(tree, returnValue);
  // A by-value aggregate direct call: `let p = P { .. }; return f(p);`. The
  // sole call argument must be a bare identifier naming this same local; its
  // nominal type and i32 field set are checker/HIR decisions.
  bool returnsDirectLocalCall = false;
  if (!returnsReceiverCall && isAdmittedDirectCall(tree, returnValue)) {
    const auto& directCall = tree.node(returnValue);
    const ast::NodeList directArguments{directCall.payload.words[ast::kCallExpressionArgsFirstWord],
                                        directCall.payload.words[ast::kCallExpressionArgsSizeWord]};
    if (tree.contains(directArguments) && directArguments.size == 1) {
      const ast::NodeId directArgument = tree.list(directArguments)[0];
      if (tree.contains(directArgument) &&
          tree.node(directArgument).kind == ast::SyntaxKind::IdentExpr) {
        returnsDirectLocalCall = true;
        returnReference = directArgument;
      }
    }
  }
  if (returnsField) {
    returnReference =
        ast::NodeId(tree.node(returnValue).payload.words[ast::kMemberExpressionObjectWord]);
  } else if (returnsReceiverCall) {
    const ast::NodeId callee(tree.node(returnValue).payload.words[ast::kCallExpressionCalleeWord]);
    returnReference =
        ast::NodeId(tree.node(callee).payload.words[ast::kMemberExpressionObjectWord]);
  } else if (isAdmittedReferenceReborrow(tree, returnValue)) {
    const ast::NodeId dereference(
        tree.node(returnValue).payload.words[ast::kUnaryExpressionOperandWord]);
    returnReference =
        ast::NodeId(tree.node(dereference).payload.words[ast::kUnaryExpressionOperandWord]);
  } else if (isAdmittedLocalBorrow(tree, returnValue)) {
    returnReference =
        ast::NodeId(tree.node(returnValue).payload.words[ast::kUnaryExpressionOperandWord]);
  }
  if (!matchesLocalReference(tree, pattern, returnReference)) return false;
  (void)returnsDirectLocalCall;
  if (!tree.contains(initializer) && statements.size == 2) {
    // A local borrow requires the referent to be initialized at the borrow point.
    if (isAdmittedLocalBorrow(tree, returnValue)) return false;
    return true;
  }
  if (statements.size == 2) return true;
  // A `let cell = ...; cell.set(...); return cell.get();` body: one leading
  // let whose binding is the receiver of the single intermediate discarded
  // receiver-call expression statement and the trailing receiver-call return.
  // The let need not be `mut`; receiver mutability is a checker decision. The
  // HIR/MIR lowering admits exactly one intermediate statement (three total),
  // so the cap stays in lockstep with the lowering shape; a body with more
  // discarded calls drains ZOM4099 at this boundary.
  if (returnsReceiverCall && statements.size == 3) {
    bool callsOnSameLocal = true;
    for (size_t index = 1; index + 1 < statements.size; ++index) {
      auto callStatement = statementItem(tree, tree.list(statements)[index]);
      if (callStatement == zc::none) {
        callsOnSameLocal = false;
        break;
      }
      ast::NodeId callStatementNode;
      ZC_IF_SOME(statement, callStatement) { callStatementNode = statement; }
      if (tree.node(callStatementNode).kind != ast::SyntaxKind::ExpressionStatement) {
        callsOnSameLocal = false;
        break;
      }
      const ast::NodeId callExpression(
          tree.node(callStatementNode).payload.words[ast::kExpressionStatementExpressionWord]);
      if (!isAdmittedReceiverCall(tree, callExpression)) {
        callsOnSameLocal = false;
        break;
      }
      const ast::NodeId callee(
          tree.node(callExpression).payload.words[ast::kCallExpressionCalleeWord]);
      const ast::NodeId callReceiver(
          tree.node(callee).payload.words[ast::kMemberExpressionObjectWord]);
      if (!matchesLocalReference(tree, pattern, callReceiver)) {
        callsOnSameLocal = false;
        break;
      }
    }
    if (callsOnSameLocal) return true;
  }
  if (static_cast<ast::BindingDeclarationKind>(declaration.payload.words[ast::kLetStmtKindWord]) !=
      ast::BindingDeclarationKind::Mut) {
    return false;
  }
  for (size_t index = 1; index + 1 < statements.size; ++index) {
    auto writeStatement = statementItem(tree, tree.list(statements)[index]);
    if (writeStatement == zc::none) return false;
    ast::NodeId writeNode;
    ZC_IF_SOME(statement, writeStatement) { writeNode = statement; }
    if (tree.node(writeNode).kind != ast::SyntaxKind::ExpressionStatement) return false;
    const ast::NodeId assignment(
        tree.node(writeNode).payload.words[ast::kExpressionStatementExpressionWord]);
    if (!tree.contains(assignment)) return false;
    // A postfix increment/decrement (`x++` / `x--`) desugars to a binary write
    // (`x = x + 1` / `x = x - 1`). The operand must name the declared local;
    // the mutability and integer type are checker decisions.
    if (tree.node(assignment).kind == ast::SyntaxKind::PostfixExpression) {
      const auto postfixOp = static_cast<ast::PostfixOperatorKind>(
          tree.node(assignment).payload.words[ast::kPostfixExpressionOpWord]);
      if (postfixOp != ast::PostfixOperatorKind::Increment &&
          postfixOp != ast::PostfixOperatorKind::Decrement) {
        return false;
      }
      const ast::NodeId postfixOperand(
          tree.node(assignment).payload.words[ast::kPostfixExpressionOperandWord]);
      if (!tree.contains(postfixOperand) ||
          tree.node(postfixOperand).kind != ast::SyntaxKind::IdentExpr ||
          !matchesLocalReference(tree, pattern, postfixOperand)) {
        return false;
      }
      continue;
    }
    if (tree.node(assignment).kind != ast::SyntaxKind::AssignmentExpr) return false;
    const auto writeOp = static_cast<ast::AssignmentOperatorKind>(
        tree.node(assignment).payload.words[ast::kAssignmentExprOpWord]);
    // A compound assignment (`x += 1`) desugars to a binary write
    // (`x = x + 1`); the target and value structural checks below are the
    // same as a plain write.
    const bool compoundWrite = isAdmittedCompoundAssignment(writeOp);
    if (writeOp != ast::AssignmentOperatorKind::Assign && !compoundWrite) { return false; }
    const ast::NodeId target(tree.node(assignment).payload.words[ast::kAssignmentExprLhsWord]);
    const ast::NodeId value(tree.node(assignment).payload.words[ast::kAssignmentExprRhsWord]);
    if (!tree.contains(target) || !tree.contains(value)) return false;
    // A scalar-local write value may be a scalar literal, an identifier reference
    // (a parameter or local, resolved downstream), or a primitive binary
    // operation of the same operand shape; a field write value stays literal-only
    // in this slice. Structure only; the checker/HIR decide which forms are
    // supported. A compound assignment's RHS is the binary's second operand, not
    // a nested binary, so binary values stay plain-assignment-only.
    const bool identValue = tree.node(value).kind == ast::SyntaxKind::IdentExpr;
    const bool binaryValue = !compoundWrite &&
                             tree.node(value).kind == ast::SyntaxKind::BinaryExpr &&
                             isAdmittedPrimitiveBinary(tree, value);
    if (!isScalarLiteral(tree.node(value).kind) && !identValue && !binaryValue) return false;
    if (tree.node(target).kind == ast::SyntaxKind::IdentExpr) {
      if (!matchesLocalReference(tree, pattern, target)) return false;
      // The current HIR write descriptor admits parameter references, not a
      // read from the mutable destination local itself. Reject that shape at
      // the surface boundary instead of allowing it to fail later as an HIR
      // invariant.
      if (identValue && matchesLocalReference(tree, pattern, value)) return false;
      continue;
    }
    if (identValue || binaryValue) return false;
    if (!returnsField || tree.node(target).kind != ast::SyntaxKind::MemberExpression) {
      return false;
    }
    const ast::NodeId object(tree.node(target).payload.words[ast::kMemberExpressionObjectWord]);
    if (!matchesLocalReference(tree, pattern, object)) return false;
  }
  return true;
}

bool hasSpecificSurfaceFailure(const ast::Tree& tree, ast::NodeId body) {
  bool found = false;
  ast::visitTreePreOrder(tree, body, [&](ast::NodeId nodeId, const ast::Node& syntax) {
    if (found) return;
    if (syntax.kind == ast::SyntaxKind::SpawnExpression ||
        syntax.kind == ast::SyntaxKind::SuspendStatement ||
        (syntax.kind == ast::SyntaxKind::MatchStmt && !isAdmittedMatchStatement(tree, nodeId)) ||
        syntax.kind == ast::SyntaxKind::WhileStmt || syntax.kind == ast::SyntaxKind::ForStmt ||
        syntax.kind == ast::SyntaxKind::ForInStatement ||
        syntax.kind == ast::SyntaxKind::DoWhileStatement ||
        syntax.kind == ast::SyntaxKind::BreakStmt ||
        syntax.kind == ast::SyntaxKind::ContinueStatement ||
        syntax.kind == ast::SyntaxKind::LabeledStatement ||
        (syntax.kind == ast::SyntaxKind::ReturnStmt &&
         !tree.contains(ast::NodeId(syntax.payload.words[ast::kReturnStmtValueWord]))) ||
        (syntax.kind == ast::SyntaxKind::ExpressionStatement &&
         !isAdmittedExpressionStatement(tree, syntax))) {
      found = true;
    }
  });
  return found;
}

bool requiresFunctionBodyFailure(const ast::Tree& tree, const ast::Node& function) {
  if (isAdmittedFunctionBody(tree, function)) return false;
  const ast::NodeId body(function.payload.words[ast::kFunctionDeclBodyWord]);
  return !tree.contains(body) || !hasSpecificSurfaceFailure(tree, body);
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
               syntax.kind == ast::SyntaxKind::ForInStatement ||
               syntax.kind == ast::SyntaxKind::DoWhileStatement) {
      kind = SurfaceSyntaxKind::Loop;
      rejected = true;
    } else if (syntax.kind == ast::SyntaxKind::BreakStmt ||
               syntax.kind == ast::SyntaxKind::ContinueStatement) {
      kind = SurfaceSyntaxKind::LoopControl;
      rejected = true;
    } else if (syntax.kind == ast::SyntaxKind::LabeledStatement) {
      kind = SurfaceSyntaxKind::Label;
      rejected = true;
    } else if (syntax.kind == ast::SyntaxKind::ReturnStmt &&
               !boundModule.tree().contains(
                   ast::NodeId(syntax.payload.words[ast::kReturnStmtValueWord]))) {
      kind = SurfaceSyntaxKind::VoidReturn;
      rejected = true;
    } else if (syntax.kind == ast::SyntaxKind::ExpressionStatement &&
               !isAdmittedExpressionStatement(boundModule.tree(), syntax)) {
      kind = SurfaceSyntaxKind::ExpressionStatement;
      rejected = true;
    } else if (syntax.kind == ast::SyntaxKind::FunctionDecl &&
               requiresFunctionBodyFailure(boundModule.tree(), syntax)) {
      kind = SurfaceSyntaxKind::FunctionBody;
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
    // An admitted while-loop body is validated by isAdmittedLoopStatement; skip
    // the body block so its trailing break/continue is not rejected as
    // LoopControl. The condition (a bare identifier) is still traversed.
    if (syntax.kind == ast::SyntaxKind::WhileStmt &&
        isAdmittedLoopStatement(boundModule.tree(), nodeId)) {
      const ast::NodeId condition(syntax.payload.words[ast::kWhileStmtCondWord]);
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
