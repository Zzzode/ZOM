// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/hir/hir-shape.h"

#include "compiler/hir/hir-internal.h"

namespace zomlang::compiler::hir {
namespace detail {

zc::Maybe<ast::NodeId> statementItem(const ast::Tree& tree, ast::NodeId statement) {
  if (!tree.contains(statement)) return zc::none;
  if (tree.node(statement).kind != ast::SyntaxKind::StatementListItem) { return statement; }
  const ast::NodeId item(tree.node(statement).payload.words[ast::kStatementListItemItemWord]);
  if (!tree.contains(item)) return zc::none;
  return item;
}

namespace {

// Returns true when the syntactic binary operator is one of the six relational
// comparisons supported as a conditional condition. Strict identity operators
// and every arithmetic, bitwise, or logical operator are excluded.
bool isRelationalBinaryOperator(ast::BinaryOperatorKind syntax) {
  ZC_IF_SOME(kind, checker::OperatorKind::fromBinary(syntax)) {
    const auto& variant = kind.variant();
    return variant.is<checker::PrimitiveOperation>() &&
           isScalarComparisonOperation(variant.get<checker::PrimitiveOperation>());
  }
  return false;
}

// Returns true when the syntactic binary operator is one of the two logical
// short-circuit operators (`&&` / `||`). These produce bool and are admitted
// as conditional conditions alongside the relational comparisons.
bool isLogicalBinaryOperator(ast::BinaryOperatorKind syntax) {
  ZC_IF_SOME(kind, checker::OperatorKind::fromBinary(syntax)) {
    const auto& variant = kind.variant();
    if (!variant.is<checker::PrimitiveOperation>()) return false;
    const auto operation = variant.get<checker::PrimitiveOperation>();
    return operation == checker::PrimitiveOperation::LogicalAnd ||
           operation == checker::PrimitiveOperation::LogicalOr;
  }
  return false;
}

// Returns true when the syntactic binary operator is a scalar arithmetic or
// bitwise operator (add, sub, mul, div, rem, bit-and/or/xor, shift). These
// are admitted as one-level nested arithmetic operands of a comparison.
bool isArithmeticBinaryOperator(ast::BinaryOperatorKind syntax) {
  ZC_IF_SOME(kind, checker::OperatorKind::fromBinary(syntax)) {
    const auto& variant = kind.variant();
    if (!variant.is<checker::PrimitiveOperation>()) return false;
    return isScalarArithmeticOperation(variant.get<checker::PrimitiveOperation>());
  }
  return false;
}

// Returns true when the syntactic binary operator is a relational comparison,
// an arithmetic/bitwise operator, or a logical short-circuit operator, i.e. a
// primitive binary operation lowerable in return position. Strict identity is
// excluded.
bool isPrimitiveBinaryOperator(ast::BinaryOperatorKind syntax) {
  ZC_IF_SOME(kind, checker::OperatorKind::fromBinary(syntax)) {
    const auto& variant = kind.variant();
    if (!variant.is<checker::PrimitiveOperation>()) return false;
    const auto operation = variant.get<checker::PrimitiveOperation>();
    return isScalarComparisonOperation(operation) || isScalarArithmeticOperation(operation);
  }
  return false;
}

// Structurally admits a `"a" + "b"` binary as a compile-time fold initializer.
// Both operands must be string literals and the operator must be `+`. The fold
// itself (concatenating the string bytes and emitting the constant) runs in the
// body checker, so the shape layer only verifies the structure. This must be
// checked before the PrimitiveBinary branch because `+` is a primitive binary
// operator, but the checker emits a Literal fact (not Call/dispatch facts) for
// a fold, so the primitive-binary lowering would reject it.
bool isStringConcatFoldInitializer(const ast::Tree& tree, ast::NodeId initializer) {
  if (!tree.contains(initializer) || tree.node(initializer).kind != ast::SyntaxKind::BinaryExpr) {
    return false;
  }
  const auto& binary = tree.node(initializer);
  if (static_cast<ast::BinaryOperatorKind>(binary.payload.words[ast::kBinaryExprOpWord]) !=
      ast::BinaryOperatorKind::Add) {
    return false;
  }
  const ast::NodeId left(binary.payload.words[ast::kBinaryExprLhsWord]);
  const ast::NodeId right(binary.payload.words[ast::kBinaryExprRhsWord]);
  return tree.contains(left) && tree.contains(right) &&
         tree.node(left).kind == ast::SyntaxKind::StringLiteralExpr &&
         tree.node(right).kind == ast::SyntaxKind::StringLiteralExpr;
}

// Structurally admits one for-loop accumulator body write: an
// `<ident> = <binary>;` assignment whose binary operands are leaves
// (identifier or scalar literal), with at least one identifier. The builder
// resolves which local each operand names; the shape only records the
// structure. This mirrors the surface-admission loop-body write contract for
// the binary-value case.
bool isAccumulatorBodyWrite(const ast::Tree& tree, ast::NodeId statement) {
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
      tree.node(target).kind != ast::SyntaxKind::IdentExpr ||
      tree.node(value).kind != ast::SyntaxKind::BinaryExpr) {
    return false;
  }
  const ast::NodeId left(tree.node(value).payload.words[ast::kBinaryExprLhsWord]);
  const ast::NodeId right(tree.node(value).payload.words[ast::kBinaryExprRhsWord]);
  if (!tree.contains(left) || !tree.contains(right)) return false;
  auto isLeaf = [&](ast::NodeId operand) {
    return tree.node(operand).kind == ast::SyntaxKind::IdentExpr ||
           isScalarLiteral(tree.node(operand).kind);
  };
  const bool leftIdent = tree.node(left).kind == ast::SyntaxKind::IdentExpr;
  const bool rightIdent = tree.node(right).kind == ast::SyntaxKind::IdentExpr;
  return isLeaf(left) && isLeaf(right) && (leftIdent || rightIdent);
}

// One if-guarded `break;` loop-body statement: `if (<cond>) { break; }` with no
// else branch. Returns the condition comparison AST node when the statement
// matches, so the caller can carry it for the builder.
zc::Maybe<ast::NodeId> accumulatorGuardedBreakCondition(const ast::Tree& tree,
                                                        ast::NodeId statement) {
  auto item = statementItem(tree, statement);
  if (item == zc::none) return zc::none;
  ast::NodeId stmt;
  ZC_IF_SOME(value, item) { stmt = value; }
  if (tree.node(stmt).kind != ast::SyntaxKind::IfStmt) return zc::none;
  const auto& ifNode = tree.node(stmt);
  if (ifNode.payload.words[ast::kIfStmtElseStmtWord] != 0) return zc::none;
  const ast::NodeId cond(ifNode.payload.words[ast::kIfStmtCondWord]);
  const ast::NodeId thenStmt(ifNode.payload.words[ast::kIfStmtThenStmtWord]);
  if (!tree.contains(cond) || tree.node(cond).kind != ast::SyntaxKind::BinaryExpr ||
      !tree.contains(thenStmt) || tree.node(thenStmt).kind != ast::SyntaxKind::BlockStmt) {
    return zc::none;
  }
  const ast::NodeId condLeft(tree.node(cond).payload.words[ast::kBinaryExprLhsWord]);
  const ast::NodeId condRight(tree.node(cond).payload.words[ast::kBinaryExprRhsWord]);
  if (!tree.contains(condLeft) || !tree.contains(condRight)) return zc::none;
  const bool condLeftIdent = tree.node(condLeft).kind == ast::SyntaxKind::IdentExpr;
  const bool condRightIdent = tree.node(condRight).kind == ast::SyntaxKind::IdentExpr;
  const bool condLeftOk = condLeftIdent || isScalarLiteral(tree.node(condLeft).kind);
  const bool condRightOk = condRightIdent || isScalarLiteral(tree.node(condRight).kind);
  if (!condLeftOk || !condRightOk || (!condLeftIdent && !condRightIdent)) return zc::none;
  const auto& thenBlock = tree.node(thenStmt);
  const ast::NodeList thenStmts{thenBlock.payload.words[ast::kBlockStmtStmtsFirstWord],
                                thenBlock.payload.words[ast::kBlockStmtStmtsSizeWord]};
  if (!tree.contains(thenStmts) || thenStmts.size != 1) return zc::none;
  auto breakItem = statementItem(tree, tree.list(thenStmts)[0]);
  if (breakItem == zc::none) return zc::none;
  ast::NodeId breakStmt;
  ZC_IF_SOME(value, breakItem) { breakStmt = value; }
  if (tree.node(breakStmt).kind != ast::SyntaxKind::BreakStmt) return zc::none;
  if (tree.node(breakStmt).payload.words[ast::kBreakStmtLabelWord] != 0) return zc::none;
  return cond;
}

// One method parameter-list classification: whether the leading declared
// parameter is the implicit `this` receiver and the count of ordinary declared
// parameters after it.
struct MethodParameterLayout final {
  bool hasReceiver;
  size_t ordinaryCount;
};

zc::Maybe<MethodParameterLayout> methodParameterLayout(const ast::Tree& tree,
                                                       const ast::Node& function) {
  const ast::NodeId listNode(function.payload.words[ast::kMethodDeclParamsIdWord]);
  if (!tree.contains(listNode) ||
      tree.node(listNode).kind != ast::SyntaxKind::FunctionParameterList) {
    return zc::none;
  }
  const ast::NodeList parameters{
      tree.node(listNode).payload.words[ast::kFunctionParameterListParamsFirstWord],
      tree.node(listNode).payload.words[ast::kFunctionParameterListParamsSizeWord]};
  if (!tree.contains(parameters)) return zc::none;
  bool hasReceiver = false;
  size_t ordinaryCount = 0;
  for (size_t index = 0; index < parameters.size; ++index) {
    const auto parameter = tree.list(parameters)[index];
    if (!tree.contains(parameter) ||
        tree.node(parameter).kind != ast::SyntaxKind::FunctionParameterDecl) {
      return zc::none;
    }
    const auto name = tree.ident(
        ast::IdentId(tree.node(parameter).payload.words[ast::kFunctionParameterDeclNameWord]));
    if (name == "this"_zc) {
      if (index != 0 || hasReceiver) return zc::none;
      hasReceiver = true;
    } else {
      ++ordinaryCount;
    }
  }
  return MethodParameterLayout{hasReceiver, ordinaryCount};
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

// Classifies one statement as the conditional-return shape: an `if` with block
// branches whose tails each return, either over a bare identifier condition or
// a relational comparison of identifier/scalar-literal operands. Returns none
// for every other statement.
zc::Maybe<FunctionReturnShape> conditionalReturnShape(const ast::Tree& tree, ast::NodeId body,
                                                      ast::NodeId statement) {
  if (!tree.contains(statement) || tree.node(statement).kind != ast::SyntaxKind::IfStmt) {
    return zc::none;
  }
  const auto& ifNode = tree.node(statement);
  const ast::NodeId thenStmt(ifNode.payload.words[ast::kIfStmtThenStmtWord]);
  const ast::NodeId elseStmt(ifNode.payload.words[ast::kIfStmtElseStmtWord]);
  if (!tree.contains(thenStmt) || !tree.contains(elseStmt) ||
      tree.node(thenStmt).kind != ast::SyntaxKind::BlockStmt ||
      tree.node(elseStmt).kind != ast::SyntaxKind::BlockStmt) {
    return zc::none;
  }
  auto branchReturnValue = [&](ast::NodeId branch) -> zc::Maybe<ast::NodeId> {
    const auto& branchNode = tree.node(branch);
    const ast::NodeList branchStmts{branchNode.payload.words[ast::kBlockStmtStmtsFirstWord],
                                    branchNode.payload.words[ast::kBlockStmtStmtsSizeWord]};
    if (!tree.contains(branchStmts) || branchStmts.empty()) return zc::none;
    auto tail = statementItem(tree, tree.list(branchStmts)[branchStmts.size - 1]);
    if (tail == zc::none) return zc::none;
    ast::NodeId tailStmt;
    ZC_IF_SOME(value, tail) { tailStmt = value; }
    if (tree.node(tailStmt).kind != ast::SyntaxKind::ReturnStmt) return zc::none;
    const ast::NodeId returnValue(tree.node(tailStmt).payload.words[ast::kReturnStmtValueWord]);
    if (!tree.contains(returnValue)) return zc::none;
    return returnValue;
  };
  auto thenValue = branchReturnValue(thenStmt);
  auto elseValue = branchReturnValue(elseStmt);
  if (thenValue == zc::none || elseValue == zc::none) return zc::none;
  ast::NodeId thenNode;
  ast::NodeId elseNode;
  ZC_IF_SOME(value, thenValue) { thenNode = value; }
  ZC_IF_SOME(value, elseValue) { elseNode = value; }
  const ast::NodeId condition(ifNode.payload.words[ast::kIfStmtCondWord]);
  FunctionReturnShape shape{};
  shape.body = body;
  shape.returnStatement = statement;
  shape.value = statement;
  shape.isConditional = true;
  shape.condition = condition;
  shape.thenReturnValue = thenNode;
  shape.elseReturnValue = elseNode;
  // Detect the comparison or logical condition: a BinaryExpr for one of the
  // six relational operators or the two logical short-circuit operators whose
  // operands are each an IdentExpr parameter/local reference or a scalar
  // literal, with at least one identifier operand. A bare identifier condition
  // keeps the parameter-reference lowering.
  if (tree.contains(condition) && tree.node(condition).kind == ast::SyntaxKind::BinaryExpr &&
      (isRelationalBinaryOperator(static_cast<ast::BinaryOperatorKind>(
           tree.node(condition).payload.words[ast::kBinaryExprOpWord])) ||
       isLogicalBinaryOperator(static_cast<ast::BinaryOperatorKind>(
           tree.node(condition).payload.words[ast::kBinaryExprOpWord])))) {
    const ast::NodeId left(tree.node(condition).payload.words[ast::kBinaryExprLhsWord]);
    const ast::NodeId right(tree.node(condition).payload.words[ast::kBinaryExprRhsWord]);
    if (!tree.contains(left) || !tree.contains(right)) return zc::none;
    const bool leftIdent = tree.node(left).kind == ast::SyntaxKind::IdentExpr;
    const bool rightIdent = tree.node(right).kind == ast::SyntaxKind::IdentExpr;
    const bool leftLiteral = isScalarLiteral(tree.node(left).kind);
    const bool rightLiteral = isScalarLiteral(tree.node(right).kind);
    // Classify a one-level nested arithmetic operand: a BinaryExpr with an
    // arithmetic/bitwise operator whose two operands are each an IdentExpr or
    // a scalar literal. The HIR builder synthesizes a leading local binding for
    // the arithmetic result, then compares that local.
    auto classifyNestedArithmetic = [&](ast::NodeId operand, ast::NodeId& leafLeft,
                                        ast::NodeId& leafRight, bool& leafLeftIsLiteral,
                                        bool& leafRightIsLiteral) -> bool {
      if (tree.node(operand).kind != ast::SyntaxKind::BinaryExpr) return false;
      const auto op = static_cast<ast::BinaryOperatorKind>(
          tree.node(operand).payload.words[ast::kBinaryExprOpWord]);
      if (!isArithmeticBinaryOperator(op)) return false;
      const ast::NodeId innerLeft(tree.node(operand).payload.words[ast::kBinaryExprLhsWord]);
      const ast::NodeId innerRight(tree.node(operand).payload.words[ast::kBinaryExprRhsWord]);
      if (!tree.contains(innerLeft) || !tree.contains(innerRight)) return false;
      const bool innerLeftIdent = tree.node(innerLeft).kind == ast::SyntaxKind::IdentExpr;
      const bool innerRightIdent = tree.node(innerRight).kind == ast::SyntaxKind::IdentExpr;
      const bool innerLeftLiteral = isScalarLiteral(tree.node(innerLeft).kind);
      const bool innerRightLiteral = isScalarLiteral(tree.node(innerRight).kind);
      if ((!innerLeftIdent && !innerLeftLiteral) || (!innerRightIdent && !innerRightLiteral)) {
        return false;
      }
      leafLeft = innerLeft;
      leafRight = innerRight;
      leafLeftIsLiteral = innerLeftLiteral;
      leafRightIsLiteral = innerRightLiteral;
      return true;
    };
    ast::NodeId nestedLeft;
    ast::NodeId nestedRight;
    bool nestedLeftIsLiteral = false;
    bool nestedRightIsLiteral = false;
    const bool leftIsNestedArithmetic =
        !leftIdent && !leftLiteral &&
        classifyNestedArithmetic(left, nestedLeft, nestedRight, nestedLeftIsLiteral,
                                 nestedRightIsLiteral);
    const bool rightIsNestedArithmetic =
        !rightIdent && !rightLiteral && !leftIsNestedArithmetic &&
        classifyNestedArithmetic(right, nestedLeft, nestedRight, nestedLeftIsLiteral,
                                 nestedRightIsLiteral);
    if ((!leftIdent && !leftLiteral && !leftIsNestedArithmetic) ||
        (!rightIdent && !rightLiteral && !rightIsNestedArithmetic) ||
        (!leftIdent && !rightIdent && !leftIsNestedArithmetic && !rightIsNestedArithmetic)) {
      return zc::none;
    }
    shape.conditionIsEquality = true;
    shape.conditionLeft = left;
    shape.conditionRight = right;
    shape.conditionLeftIsLiteral = !leftIdent && !leftIsNestedArithmetic;
    shape.conditionRightIsLiteral = !rightIdent && !rightIsNestedArithmetic;
    if (leftIsNestedArithmetic || rightIsNestedArithmetic) {
      shape.conditionLeftIsNestedArithmetic = leftIsNestedArithmetic;
      shape.conditionRightIsNestedArithmetic = rightIsNestedArithmetic;
      shape.nestedArithmeticLeft = nestedLeft;
      shape.nestedArithmeticRight = nestedRight;
      shape.nestedArithmeticLeftIsLiteral = nestedLeftIsLiteral;
      shape.nestedArithmeticRightIsLiteral = nestedRightIsLiteral;
    }
  } else if (tree.contains(condition) &&
             tree.node(condition).kind == ast::SyntaxKind::UnaryExpression) {
    // Detect the unary `!x` condition: a UnaryExpression with the LogicalNot
    // operator whose operand is an IdentExpr parameter/local reference or a
    // scalar literal. The HIR builder desugars `!x` to `x == false`, reusing
    // the comparison condition path.
    const auto unaryOp = static_cast<ast::UnaryOperatorKind>(
        tree.node(condition).payload.words[ast::kUnaryExpressionOpWord]);
    if (unaryOp == ast::UnaryOperatorKind::LogicalNot) {
      const ast::NodeId operand(
          tree.node(condition).payload.words[ast::kUnaryExpressionOperandWord]);
      if (tree.contains(operand)) {
        const bool operandIdent = tree.node(operand).kind == ast::SyntaxKind::IdentExpr;
        const bool operandLiteral = isScalarLiteral(tree.node(operand).kind);
        if (operandIdent || operandLiteral) {
          shape.conditionIsUnary = true;
          shape.conditionUnaryOperand = operand;
          shape.conditionUnaryOperandIsLiteral = operandLiteral;
        }
      }
    }
  }
  return shape;
}

// Classifies one statement as the match-return shape: a match whose
// scrutinee is a bare identifier (a bool, integer, or enum parameter
// reference) and whose arms each tail-return a scalar literal. A bool match
// has one arm for `true` and one for `false`, or one literal arm plus a
// `default` (wildcard) arm covering the remaining bool value; it reuses the
// conditional fields (`condition`, `thenReturnValue`, `elseReturnValue`) so
// the HIR builder lowers it through the bare-parameter conditional path. An
// integer match has one or more integer-literal arms plus a `default` arm
// covering the open integer domain; a single literal arm sets `isMatchEquality`
// and carries the pattern literal node so the HIR builder synthesizes
// `scrutinee == literal` through the equality conditional path, while two or
// more literal arms set `isMatchChainedEquality` and carry all literal nodes
// and return values for a chained conditional. An enum match has N (>= 2)
// unit-variant pattern arms on the closed enum domain; two arms route through
// the equality path and three or more through the chained path with the last
// arm as the else branch. Returns none for every other statement.
zc::Maybe<FunctionReturnShape> matchReturnShape(const ast::Tree& tree, ast::NodeId body,
                                                ast::NodeId statement) {
  if (!tree.contains(statement) || tree.node(statement).kind != ast::SyntaxKind::MatchStmt) {
    return zc::none;
  }
  const auto& matchNode = tree.node(statement);
  const ast::NodeId scrutinee(matchNode.payload.words[ast::kMatchStmtScrutineeWord]);
  if (!tree.contains(scrutinee) || tree.node(scrutinee).kind != ast::SyntaxKind::IdentExpr) {
    return zc::none;
  }
  const ast::NodeList arms{matchNode.payload.words[ast::kMatchStmtArmsFirstWord],
                           matchNode.payload.words[ast::kMatchStmtArmsSizeWord]};
  if (!tree.contains(arms) || arms.size < 2) return zc::none;
  zc::Maybe<ast::NodeId> trueValue;
  zc::Maybe<ast::NodeId> falseValue;
  zc::Maybe<ast::NodeId> defaultValue;
  bool sawDefault = false;
  zc::Vector<ast::NodeId> intLiteralNodes;
  zc::Vector<ast::NodeId> intLiteralValues;
  zc::Maybe<ast::NodeId> guardNode;
  bool sawEnumPattern = false;
  zc::Vector<ast::NodeId> enumPatternNodes;
  zc::Vector<ast::NodeId> enumReturnValues;
  for (size_t index = 0; index < arms.size; ++index) {
    const ast::NodeId armId = tree.list(arms)[index];
    if (!tree.contains(armId)) return zc::none;
    const auto& arm = tree.node(armId);
    if (arm.kind != ast::SyntaxKind::MatchArmStmt) return zc::none;
    const ast::NodeId guard(arm.payload.words[ast::kMatchArmStmtGuardWord]);
    const bool hasGuard = tree.contains(guard);
    const ast::NodeId pattern(arm.payload.words[ast::kMatchArmStmtPatternWord]);
    if (!tree.contains(pattern)) return zc::none;
    // A literal pattern carries its bool value explicitly or its integer
    // literal node; a wildcard (default) arm covers the remaining domain and
    // is assigned after both arms are read.
    bool isLiteral = false;
    bool literalValue = false;
    bool isIntLiteral = false;
    if (tree.node(pattern).kind == ast::SyntaxKind::LiteralPattern) {
      const ast::NodeId literal(tree.node(pattern).payload.words[ast::kLiteralPatternLiteralWord]);
      if (!tree.contains(literal)) return zc::none;
      if (tree.node(literal).kind == ast::SyntaxKind::BoolLiteral) {
        if (intLiteralNodes.size() > 0) return zc::none;
        isLiteral = true;
        literalValue = tree.node(literal).payload.words[ast::kBoolLiteralValueWord] != 0;
      } else if (tree.node(literal).kind == ast::SyntaxKind::IntLiteral) {
        if (sawDefault) return zc::none;
        isIntLiteral = true;
        intLiteralNodes.add(literal);
      } else {
        return zc::none;
      }
    } else if (tree.node(pattern).kind == ast::SyntaxKind::WildcardPattern) {
      if (sawDefault) return zc::none;
      sawDefault = true;
    } else if (tree.node(pattern).kind == ast::SyntaxKind::EnumPattern) {
      // A unit enum variant pattern (e.g., `Color.Red`). N enum arms on the
      // same scrutinee lower to the equality conditional path (N == 2) or the
      // chained conditional path (N >= 3); the last arm is the "else" branch.
      // The variant discriminant is read from the checker's literal fact by
      // the builder.
      if (intLiteralNodes.size() > 0 || sawDefault) return zc::none;
      sawEnumPattern = true;
      enumPatternNodes.add(pattern);
    } else {
      return zc::none;
    }
    // A guard is admitted only on the true arm of a bool match. The guard must
    // be a single binary expression whose one operand is a bare identifier and
    // whose other operand is a scalar literal. The HIR builder lowers the
    // match to a conjunctive condition (scrutinee AND guard) keeping the
    // four-block diamond CFG.
    if (hasGuard) {
      if (isIntLiteral || !isLiteral || !literalValue || guardNode != zc::none) { return zc::none; }
      if (tree.node(guard).kind != ast::SyntaxKind::BinaryExpr) return zc::none;
      const ast::NodeId guardLeft(tree.node(guard).payload.words[ast::kBinaryExprLhsWord]);
      const ast::NodeId guardRight(tree.node(guard).payload.words[ast::kBinaryExprRhsWord]);
      if (!tree.contains(guardLeft) || !tree.contains(guardRight)) return zc::none;
      const bool leftIdent = tree.node(guardLeft).kind == ast::SyntaxKind::IdentExpr;
      const bool rightIdent = tree.node(guardRight).kind == ast::SyntaxKind::IdentExpr;
      if (leftIdent == rightIdent) return zc::none;
      const ast::NodeId guardLiteral = leftIdent ? guardRight : guardLeft;
      if (!isScalarLiteral(tree.node(guardLiteral).kind)) return zc::none;
      guardNode = guard;
    }
    const ast::NodeId armBody(arm.payload.words[ast::kMatchArmStmtBodyWord]);
    if (!tree.contains(armBody)) return zc::none;
    ast::NodeId returnStmt;
    if (tree.node(armBody).kind == ast::SyntaxKind::ReturnStmt) {
      returnStmt = armBody;
    } else if (tree.node(armBody).kind == ast::SyntaxKind::BlockStmt) {
      const ast::NodeList stmts{tree.node(armBody).payload.words[ast::kBlockStmtStmtsFirstWord],
                                tree.node(armBody).payload.words[ast::kBlockStmtStmtsSizeWord]};
      if (!tree.contains(stmts) || stmts.size != 1) return zc::none;
      auto item = statementItem(tree, tree.list(stmts)[0]);
      if (item == zc::none) return zc::none;
      ZC_IF_SOME(itemValue, item) { returnStmt = itemValue; }
      if (tree.node(returnStmt).kind != ast::SyntaxKind::ReturnStmt) return zc::none;
    } else {
      return zc::none;
    }
    const ast::NodeId returnValue(tree.node(returnStmt).payload.words[ast::kReturnStmtValueWord]);
    if (!tree.contains(returnValue) || !isScalarLiteral(tree.node(returnValue).kind)) {
      return zc::none;
    }
    if (isIntLiteral) {
      intLiteralValues.add(returnValue);
    } else if (isLiteral) {
      if (literalValue) {
        if (trueValue != zc::none) return zc::none;
        trueValue = returnValue;
      } else {
        if (falseValue != zc::none) return zc::none;
        falseValue = returnValue;
      }
    } else if (sawEnumPattern) {
      enumReturnValues.add(returnValue);
    } else {
      defaultValue = returnValue;
    }
  }
  FunctionReturnShape shape{};
  shape.body = body;
  shape.returnStatement = statement;
  shape.value = statement;
  shape.isConditional = true;
  shape.isMatchReturn = true;
  shape.matchHasDefaultArm = sawDefault;
  shape.condition = scrutinee;
  shape.matchStatement = statement;
  if (sawEnumPattern) {
    // Enum match: N unit-variant pattern arms on a closed enum domain. Two
    // arms lower to the equality conditional path; three or more lower to the
    // chained conditional path with the last arm as the else branch. The HIR
    // builder reads each variant discriminant from the checker's literal fact
    // on the EnumPattern node and synthesizes `scrutinee == discriminant`.
    if (enumPatternNodes.size() < 2 || enumPatternNodes.size() != enumReturnValues.size()) {
      return zc::none;
    }
    if (enumPatternNodes.size() == 2) {
      shape.isMatchEnum = true;
      shape.isMatchEquality = true;
      shape.matchEqualityLiteral = enumPatternNodes[0];
      shape.conditionIsEquality = true;
      shape.conditionLeft = scrutinee;
      shape.conditionRight = enumPatternNodes[0];
      shape.conditionLeftIsLiteral = false;
      shape.conditionRightIsLiteral = true;
      shape.thenReturnValue = enumReturnValues[0];
      shape.elseReturnValue = enumReturnValues[1];
      return shape;
    }
    // N >= 3: chained conditional path. The first N-1 arms are the chained
    // equality comparisons; the Nth arm is the else branch.
    shape.isMatchEnum = true;
    shape.isMatchChainedEquality = true;
    const size_t chainedCount = enumPatternNodes.size() - 1;
    for (size_t i = 0; i < chainedCount; ++i) {
      shape.matchChainedLiterals.add(enumPatternNodes[i]);
      shape.matchChainedThenValues.add(enumReturnValues[i]);
    }
    shape.matchChainedElseValue = enumReturnValues[chainedCount];
    shape.thenReturnValue = shape.matchChainedThenValues[0];
    shape.elseReturnValue = shape.matchChainedElseValue;
    return shape;
  }
  if (intLiteralNodes.size() > 0) {
    if (!sawDefault || intLiteralValues.size() != intLiteralNodes.size() ||
        defaultValue == zc::none) {
      return zc::none;
    }
    ast::NodeId elseNode;
    ZC_IF_SOME(value, defaultValue) { elseNode = value; }
    if (intLiteralNodes.size() == 1) {
      // Single literal arm plus default: equality conditional path.
      shape.isMatchEquality = true;
      shape.matchEqualityLiteral = intLiteralNodes[0];
      shape.conditionIsEquality = true;
      shape.conditionLeft = scrutinee;
      shape.conditionRight = intLiteralNodes[0];
      shape.conditionLeftIsLiteral = false;
      shape.conditionRightIsLiteral = true;
      shape.thenReturnValue = intLiteralValues[0];
      shape.elseReturnValue = elseNode;
      return shape;
    }
    // Two or more literal arms plus default: chained conditional path.
    shape.isMatchChainedEquality = true;
    shape.matchChainedLiterals = zc::mv(intLiteralNodes);
    shape.matchChainedThenValues = zc::mv(intLiteralValues);
    shape.matchChainedElseValue = elseNode;
    // Populate the shared then/else slots so the conditional setup code can
    // resolve the return type; all arms share the function's return type.
    shape.thenReturnValue = shape.matchChainedThenValues[0];
    shape.elseReturnValue = elseNode;
    return shape;
  }
  // Assign the default arm to the missing bool value.
  if (sawDefault) {
    if (trueValue != zc::none && falseValue == zc::none) {
      falseValue = defaultValue;
    } else if (falseValue != zc::none && trueValue == zc::none) {
      trueValue = defaultValue;
    } else {
      return zc::none;
    }
  } else if (trueValue == zc::none || falseValue == zc::none) {
    return zc::none;
  }
  // A guard is admitted only when the default arm covers the remaining bool
  // domain, so the conjunctive condition (scrutinee AND guard) is semantically
  // exact: the false arm is the default, and the guard further constrains the
  // true arm. A guard without a default arm would leave the guard-false case
  // uncovered and is rejected here.
  if (guardNode != zc::none && !sawDefault) return zc::none;
  ast::NodeId trueNode;
  ast::NodeId falseNode;
  ZC_IF_SOME(value, trueValue) { trueNode = value; }
  ZC_IF_SOME(value, falseValue) { falseNode = value; }
  shape.thenReturnValue = trueNode;
  shape.elseReturnValue = falseNode;
  ZC_IF_SOME(guard, guardNode) {
    shape.hasMatchGuard = true;
    shape.matchGuard = guard;
  }
  return shape;
}

zc::Maybe<ast::NodeId> localBorrowReference(const ast::Tree& tree, ast::NodeId expression) {
  if (!tree.contains(expression) ||
      tree.node(expression).kind != ast::SyntaxKind::UnaryExpression) {
    return zc::none;
  }
  const auto operation = static_cast<ast::UnaryOperatorKind>(
      tree.node(expression).payload.words[ast::kUnaryExpressionOpWord]);
  if (operation != ast::UnaryOperatorKind::Ref && operation != ast::UnaryOperatorKind::RefMut) {
    return zc::none;
  }
  const ast::NodeId operand(tree.node(expression).payload.words[ast::kUnaryExpressionOperandWord]);
  if (!tree.contains(operand) || tree.node(operand).kind != ast::SyntaxKind::IdentExpr) {
    return zc::none;
  }
  return operand;
}

}  // namespace

zc::Maybe<ast::NodeId> reborrowReference(const ast::Tree& tree, ast::NodeId expression) {
  if (!tree.contains(expression) ||
      tree.node(expression).kind != ast::SyntaxKind::UnaryExpression) {
    return zc::none;
  }
  const auto operation = static_cast<ast::UnaryOperatorKind>(
      tree.node(expression).payload.words[ast::kUnaryExpressionOpWord]);
  if (operation != ast::UnaryOperatorKind::Ref && operation != ast::UnaryOperatorKind::RefMut) {
    return zc::none;
  }
  const ast::NodeId dereference(
      tree.node(expression).payload.words[ast::kUnaryExpressionOperandWord]);
  if (!tree.contains(dereference) ||
      tree.node(dereference).kind != ast::SyntaxKind::UnaryExpression ||
      static_cast<ast::UnaryOperatorKind>(
          tree.node(dereference).payload.words[ast::kUnaryExpressionOpWord]) !=
          ast::UnaryOperatorKind::Deref) {
    return zc::none;
  }
  const ast::NodeId reference(
      tree.node(dereference).payload.words[ast::kUnaryExpressionOperandWord]);
  if (!tree.contains(reference) || tree.node(reference).kind != ast::SyntaxKind::IdentExpr) {
    return zc::none;
  }
  return reference;
}

// Classifies a function body as the sequential N-local shape: N (>= 2) leading
// `let id: T = <literal | aggregate | identifier>;` statements followed by a
// single `return <identifier>;`. Each identifier initializer names an earlier
// local or a parameter; the return names one of the locals or a parameter.
// Returns none for any other body. The result is recomputed from the AST wher-
// ever the per-binding layout is needed, keeping FunctionReturnShape copyable
// and guaranteeing the producer and verifiers derive one identical layout.
zc::Maybe<SequentialLocalShape> sequentialLocalShape(const ast::Tree& tree, ast::NodeId body) {
  if (!tree.contains(body) || tree.node(body).kind != ast::SyntaxKind::BlockStmt) return zc::none;
  const auto& block = tree.node(body);
  const ast::NodeList statements{block.payload.words[ast::kBlockStmtStmtsFirstWord],
                                 block.payload.words[ast::kBlockStmtStmtsSizeWord]};
  if (!tree.contains(statements) || statements.size < 2) return zc::none;
  const size_t bindingCount = statements.size - 1;
  SequentialLocalShape shape{};
  shape.body = body;
  for (size_t index = 0; index < bindingCount; ++index) {
    auto declaratorNode = localDeclarator(tree, tree.list(statements)[index]);
    if (declaratorNode == zc::none) { return zc::none; }
    ast::NodeId declarator;
    ZC_IF_SOME(value, declaratorNode) { declarator = value; }
    const ast::NodeId pattern(
        tree.node(declarator).payload.words[ast::kVariableDeclaratorPatternWord]);
    const ast::NodeId initializer(
        tree.node(declarator).payload.words[ast::kVariableDeclaratorInitWord]);
    if (!tree.contains(initializer)) return zc::none;
    SequentialInitializerKind kind;
    size_t referencedLocal = 0;
    zc::Maybe<SequentialBinaryOperand> leftOperand;
    zc::Maybe<SequentialBinaryOperand> rightOperand;
    zc::Maybe<checker::PrimitiveOperation> unaryOperation;
    zc::Maybe<SequentialBinaryOperand> unaryOperand;
    ast::NodeId castInnerNode;
    ast::NodeId ternaryCondNode;
    ast::NodeId ternaryThenNode;
    ast::NodeId ternaryElseNode;
    bool ternaryConditionIsLocal = false;
    bool ternaryConditionIsLiteral = false;
    bool matchHasDefaultArm = false;
    if (isScalarLiteral(tree.node(initializer).kind)) {
      kind = SequentialInitializerKind::Literal;
    } else if (tree.node(initializer).kind == ast::SyntaxKind::StructLiteralExpr) {
      kind = SequentialInitializerKind::Aggregate;
    } else if (tree.node(initializer).kind == ast::SyntaxKind::IdentExpr) {
      kind = SequentialInitializerKind::ParameterReference;
      for (size_t earlier = 0; earlier < index; ++earlier) {
        if (matchesLocalReference(tree, shape.bindings[earlier].pattern, initializer)) {
          kind = SequentialInitializerKind::LocalReference;
          referencedLocal = earlier;
          break;
        }
      }
    } else if (isStringConcatFoldInitializer(tree, initializer)) {
      // A string-concat fold `"a" + "b"`. The checker folds the concatenation
      // to a string constant; the builder lowers it to a scalar literal. This
      // must precede the PrimitiveBinary branch because `+` is a primitive
      // binary operator, but the fold carries no Call/dispatch facts.
      kind = SequentialInitializerKind::FoldedStringConcat;
    } else if (tree.node(initializer).kind == ast::SyntaxKind::BinaryExpr &&
               isPrimitiveBinaryOperator(static_cast<ast::BinaryOperatorKind>(
                   tree.node(initializer).payload.words[ast::kBinaryExprOpWord]))) {
      // A primitive binary operation whose operands are each a scalar literal, a
      // parameter reference, a reference to an earlier local, or (for at most one
      // operand) a nested one-level primitive binary, with at least one reference
      // or nested operand. Each leaf operand is classified against the earlier
      // bindings the same way an identifier initializer is.
      const ast::NodeId binaryLeft(tree.node(initializer).payload.words[ast::kBinaryExprLhsWord]);
      const ast::NodeId binaryRight(tree.node(initializer).payload.words[ast::kBinaryExprRhsWord]);
      if (!tree.contains(binaryLeft) || !tree.contains(binaryRight)) return zc::none;
      // Classifies a leaf operand: a scalar literal, a parameter reference, or a
      // reference to an earlier local. A leaf operand is never a binary, so this
      // keeps two-level nesting unsupported.
      auto classifyLeaf = [&](ast::NodeId operandNode) -> zc::Maybe<SequentialBinaryLeafOperand> {
        if (isScalarLiteral(tree.node(operandNode).kind)) {
          return SequentialBinaryLeafOperand{operandNode, SequentialBinaryOperandKind::Literal, 0};
        }
        if (tree.node(operandNode).kind != ast::SyntaxKind::IdentExpr) return zc::none;
        for (size_t earlier = 0; earlier < index; ++earlier) {
          if (matchesLocalReference(tree, shape.bindings[earlier].pattern, operandNode)) {
            return SequentialBinaryLeafOperand{
                operandNode, SequentialBinaryOperandKind::LocalReference, earlier};
          }
        }
        return SequentialBinaryLeafOperand{operandNode,
                                           SequentialBinaryOperandKind::ParameterReference, 0};
      };
      auto classifyOperand = [&](ast::NodeId operandNode) -> zc::Maybe<SequentialBinaryOperand> {
        if (tree.node(operandNode).kind == ast::SyntaxKind::BinaryExpr &&
            isPrimitiveBinaryOperator(static_cast<ast::BinaryOperatorKind>(
                tree.node(operandNode).payload.words[ast::kBinaryExprOpWord]))) {
          // A nested one-level primitive binary. Its two leaf operands are
          // classified; at least one must be a reference and neither may be a
          // further binary.
          const ast::NodeId nestedLeft(
              tree.node(operandNode).payload.words[ast::kBinaryExprLhsWord]);
          const ast::NodeId nestedRight(
              tree.node(operandNode).payload.words[ast::kBinaryExprRhsWord]);
          if (!tree.contains(nestedLeft) || !tree.contains(nestedRight)) return zc::none;
          auto innerLeft = classifyLeaf(nestedLeft);
          auto innerRight = classifyLeaf(nestedRight);
          if (innerLeft == zc::none || innerRight == zc::none) return zc::none;
          bool innerLeftLiteral = false;
          bool innerRightLiteral = false;
          ZC_IF_SOME(value, innerLeft) {
            innerLeftLiteral = value.kind == SequentialBinaryOperandKind::Literal;
          }
          ZC_IF_SOME(value, innerRight) {
            innerRightLiteral = value.kind == SequentialBinaryOperandKind::Literal;
          }
          if (innerLeftLiteral && innerRightLiteral) return zc::none;
          zc::Maybe<checker::PrimitiveOperation> nestedOperation;
          ZC_IF_SOME(kind, checker::OperatorKind::fromBinary(static_cast<ast::BinaryOperatorKind>(
                               tree.node(operandNode).payload.words[ast::kBinaryExprOpWord]))) {
            if (kind.variant().is<checker::PrimitiveOperation>()) {
              nestedOperation = kind.variant().get<checker::PrimitiveOperation>();
            }
          }
          if (nestedOperation == zc::none) return zc::none;
          return SequentialBinaryOperand{operandNode,
                                         SequentialBinaryOperandKind::NestedBinary,
                                         0,
                                         zc::mv(nestedOperation),
                                         zc::mv(innerLeft),
                                         zc::mv(innerRight)};
        }
        auto leaf = classifyLeaf(operandNode);
        if (leaf == zc::none) return zc::none;
        SequentialBinaryOperand operand{};
        ZC_IF_SOME(value, leaf) {
          operand.node = value.node;
          operand.kind = value.kind;
          operand.referencedLocal = value.referencedLocal;
        }
        return operand;
      };
      // At most one operand may be a nested binary in this slice.
      const bool leftIsNested = tree.node(binaryLeft).kind == ast::SyntaxKind::BinaryExpr;
      const bool rightIsNested = tree.node(binaryRight).kind == ast::SyntaxKind::BinaryExpr;
      if (leftIsNested && rightIsNested) return zc::none;
      auto left = classifyOperand(binaryLeft);
      auto right = classifyOperand(binaryRight);
      if (left == zc::none || right == zc::none) return zc::none;
      // A literal-vs-literal binary is admitted structurally; the body checker
      // anchors the operand type from the enclosing annotation, and the
      // dead-erase filter removes a dead literal-vs-literal binding before
      // LIR lowering.
      kind = SequentialInitializerKind::PrimitiveBinary;
      leftOperand = zc::mv(left);
      rightOperand = zc::mv(right);
    } else if (tree.node(initializer).kind == ast::SyntaxKind::UnaryExpression) {
      const auto unaryOp = static_cast<ast::UnaryOperatorKind>(
          tree.node(initializer).payload.words[ast::kUnaryExpressionOpWord]);
      const bool isIncrement = unaryOp == ast::UnaryOperatorKind::PreIncrement ||
                               unaryOp == ast::UnaryOperatorKind::PreDecrement;
      if (isIncrement) {
        // A prefix increment/decrement (`++x` / `--x`) over one operand that is
        // an identifier naming an earlier local. The builder desugars this to
        // a binary write (`x = x +/- 1`) followed by a local-reference
        // initializer for the binding. The operand must be a local (never a
        // parameter) because the desugar writes to it.
        const ast::NodeId unaryOperandNode(
            tree.node(initializer).payload.words[ast::kUnaryExpressionOperandWord]);
        if (!tree.contains(unaryOperandNode) ||
            tree.node(unaryOperandNode).kind != ast::SyntaxKind::IdentExpr) {
          return zc::none;
        }
        SequentialBinaryOperand classified{};
        classified.node = unaryOperandNode;
        classified.kind = SequentialBinaryOperandKind::ParameterReference;
        for (size_t earlier = 0; earlier < index; ++earlier) {
          if (matchesLocalReference(tree, shape.bindings[earlier].pattern, unaryOperandNode)) {
            classified.kind = SequentialBinaryOperandKind::LocalReference;
            classified.referencedLocal = earlier;
            break;
          }
        }
        if (classified.kind != SequentialBinaryOperandKind::LocalReference) return zc::none;
        auto operation = checker::OperatorKind::fromUnary(unaryOp);
        if (operation == zc::none) return zc::none;
        if (!ZC_ASSERT_NONNULL(operation).variant().is<checker::PrimitiveOperation>())
          return zc::none;
        kind = SequentialInitializerKind::Increment;
        referencedLocal = classified.referencedLocal;
        unaryOperation = ZC_ASSERT_NONNULL(operation).variant().get<checker::PrimitiveOperation>();
        unaryOperand = zc::mv(classified);
      } else {
        // A primitive unary operation (`+` `-` `~` `!`) over one operand that is
        // an identifier naming an earlier local or a parameter. The builder
        // desugars the unary to an equivalent binary operation.
        const bool isPrimitiveUnary = unaryOp == ast::UnaryOperatorKind::Plus ||
                                      unaryOp == ast::UnaryOperatorKind::Minus ||
                                      unaryOp == ast::UnaryOperatorKind::LogicalNot ||
                                      unaryOp == ast::UnaryOperatorKind::BitNot;
        if (!isPrimitiveUnary) return zc::none;
        const ast::NodeId unaryOperandNode(
            tree.node(initializer).payload.words[ast::kUnaryExpressionOperandWord]);
        if (!tree.contains(unaryOperandNode) ||
            tree.node(unaryOperandNode).kind != ast::SyntaxKind::IdentExpr) {
          return zc::none;
        }
        SequentialBinaryOperand classified{};
        classified.node = unaryOperandNode;
        classified.kind = SequentialBinaryOperandKind::ParameterReference;
        for (size_t earlier = 0; earlier < index; ++earlier) {
          if (matchesLocalReference(tree, shape.bindings[earlier].pattern, unaryOperandNode)) {
            classified.kind = SequentialBinaryOperandKind::LocalReference;
            classified.referencedLocal = earlier;
            break;
          }
        }
        auto operation = checker::OperatorKind::fromUnary(unaryOp);
        if (operation == zc::none) return zc::none;
        if (!ZC_ASSERT_NONNULL(operation).variant().is<checker::PrimitiveOperation>())
          return zc::none;
        kind = SequentialInitializerKind::PrimitiveUnary;
        unaryOperation = ZC_ASSERT_NONNULL(operation).variant().get<checker::PrimitiveOperation>();
        unaryOperand = zc::mv(classified);
      }
    } else if (tree.node(initializer).kind == ast::SyntaxKind::PostfixExpression) {
      const auto postfixOp = static_cast<ast::PostfixOperatorKind>(
          tree.node(initializer).payload.words[ast::kPostfixExpressionOpWord]);
      if (postfixOp == ast::PostfixOperatorKind::Increment ||
          postfixOp == ast::PostfixOperatorKind::Decrement) {
        // A postfix increment/decrement (`x++` / `x--`) over one operand that
        // is an identifier naming an earlier local. The builder desugars this
        // to a local-reference initializer (reading the old value) followed
        // by a binary write (`x = x +/- 1`). The operand must be a local
        // (never a parameter) because the desugar writes to it. The write
        // follows the binding, the reverse of the prefix form.
        const ast::NodeId postfixOperandNode(
            tree.node(initializer).payload.words[ast::kPostfixExpressionOperandWord]);
        if (!tree.contains(postfixOperandNode) ||
            tree.node(postfixOperandNode).kind != ast::SyntaxKind::IdentExpr) {
          return zc::none;
        }
        SequentialBinaryOperand classified{};
        classified.node = postfixOperandNode;
        classified.kind = SequentialBinaryOperandKind::ParameterReference;
        for (size_t earlier = 0; earlier < index; ++earlier) {
          if (matchesLocalReference(tree, shape.bindings[earlier].pattern, postfixOperandNode)) {
            classified.kind = SequentialBinaryOperandKind::LocalReference;
            classified.referencedLocal = earlier;
            break;
          }
        }
        if (classified.kind != SequentialBinaryOperandKind::LocalReference) return zc::none;
        auto operation = checker::OperatorKind::fromPostfix(postfixOp);
        if (operation == zc::none) return zc::none;
        if (!ZC_ASSERT_NONNULL(operation).variant().is<checker::PrimitiveOperation>())
          return zc::none;
        kind = SequentialInitializerKind::PostfixIncrement;
        referencedLocal = classified.referencedLocal;
        unaryOperation = ZC_ASSERT_NONNULL(operation).variant().get<checker::PrimitiveOperation>();
        unaryOperand = zc::mv(classified);
      } else {
        return zc::none;
      }
    } else if (tree.node(initializer).kind == ast::SyntaxKind::CastExpression) {
      // An integer `as` cast whose inner expression is a scalar literal. The
      // builder lowers the inner literal with the cast result type; the cast
      // itself is a no-op for widening or identity conversions.
      const ast::NodeId castExpr(
          tree.node(initializer).payload.words[ast::kCastExpressionExprWord]);
      if (!tree.contains(castExpr) || !isScalarLiteral(tree.node(castExpr).kind)) {
        return zc::none;
      }
      kind = SequentialInitializerKind::Cast;
      castInnerNode = castExpr;
    } else if (tree.node(initializer).kind == ast::SyntaxKind::ConditionalExpr) {
      // A ternary conditional expression `cond ? then : else`. The condition
      // is a bool reference or a bool literal; both branches are scalar
      // literals of the same type. The builder lowers this to a conditional
      // select.
      const ast::NodeId cond(tree.node(initializer).payload.words[ast::kConditionalExprCondWord]);
      const ast::NodeId thenExpr(
          tree.node(initializer).payload.words[ast::kConditionalExprThenExprWord]);
      const ast::NodeId elseExpr(
          tree.node(initializer).payload.words[ast::kConditionalExprElseExprWord]);
      if (!tree.contains(cond) || !tree.contains(thenExpr) || !tree.contains(elseExpr)) {
        return zc::none;
      }
      const bool conditionIsLiteral = tree.node(cond).kind == ast::SyntaxKind::BoolLiteral;
      if ((tree.node(cond).kind != ast::SyntaxKind::IdentExpr && !conditionIsLiteral) ||
          !isScalarLiteral(tree.node(thenExpr).kind) ||
          !isScalarLiteral(tree.node(elseExpr).kind)) {
        return zc::none;
      }
      kind = SequentialInitializerKind::Ternary;
      ternaryCondNode = cond;
      ternaryThenNode = thenExpr;
      ternaryElseNode = elseExpr;
      if (conditionIsLiteral) {
        ternaryConditionIsLiteral = true;
      } else {
        for (size_t earlier = 0; earlier < index; ++earlier) {
          if (matchesLocalReference(tree, shape.bindings[earlier].pattern, cond)) {
            ternaryConditionIsLocal = true;
            break;
          }
        }
      }
    } else if (tree.node(initializer).kind == ast::SyntaxKind::MatchExpr) {
      // A boolean match expression `match (scrutinee) { when true => e1;
      // when false => e2; [default => e3;] }`. This normalizes to the ternary
      // conditional-select path: the scrutinee becomes the condition, the
      // true-arm body becomes the then-branch, and the false-arm body becomes
      // the else-branch. An optional third default (wildcard) arm is
      // semantically unreachable for bool (true+false cover all bool values)
      // and is dropped; the count equations credit its checker-produced facts.
      const ast::NodeId scrutinee(
          tree.node(initializer).payload.words[ast::kMatchExprScrutineeWord]);
      const ast::NodeList arms{tree.node(initializer).payload.words[ast::kMatchExprArmsFirstWord],
                               tree.node(initializer).payload.words[ast::kMatchExprArmsSizeWord]};
      if (!tree.contains(scrutinee) || !tree.contains(arms) || (arms.size != 2 && arms.size != 3)) {
        return zc::none;
      }
      const bool conditionIsLiteral = tree.node(scrutinee).kind == ast::SyntaxKind::BoolLiteral;
      if (tree.node(scrutinee).kind != ast::SyntaxKind::IdentExpr && !conditionIsLiteral) {
        return zc::none;
      }
      if (arms.size == 3) {
        const ast::NodeId defaultArmId = tree.list(arms)[2];
        if (!tree.contains(defaultArmId) ||
            tree.node(defaultArmId).kind != ast::SyntaxKind::MatchArmExpr) {
          return zc::none;
        }
        const auto& defaultArm = tree.node(defaultArmId);
        const ast::NodeId defaultGuard(defaultArm.payload.words[ast::kMatchArmExprGuardWord]);
        if (tree.contains(defaultGuard)) return zc::none;
        const ast::NodeId defaultPattern(defaultArm.payload.words[ast::kMatchArmExprPatternWord]);
        if (!tree.contains(defaultPattern) ||
            tree.node(defaultPattern).kind != ast::SyntaxKind::WildcardPattern) {
          return zc::none;
        }
        const ast::NodeId defaultBody(defaultArm.payload.words[ast::kMatchArmExprBodyWord]);
        if (!tree.contains(defaultBody) || !isScalarLiteral(tree.node(defaultBody).kind)) {
          return zc::none;
        }
        matchHasDefaultArm = true;
      }
      zc::Maybe<ast::NodeId> trueArmBody;
      zc::Maybe<ast::NodeId> falseArmBody;
      for (size_t armIndex = 0; armIndex < 2; ++armIndex) {
        const ast::NodeId armId = tree.list(arms)[armIndex];
        if (!tree.contains(armId) || tree.node(armId).kind != ast::SyntaxKind::MatchArmExpr) {
          return zc::none;
        }
        const auto& arm = tree.node(armId);
        const ast::NodeId guard(arm.payload.words[ast::kMatchArmExprGuardWord]);
        if (tree.contains(guard)) return zc::none;
        const ast::NodeId pattern(arm.payload.words[ast::kMatchArmExprPatternWord]);
        if (!tree.contains(pattern) || tree.node(pattern).kind != ast::SyntaxKind::LiteralPattern) {
          return zc::none;
        }
        const ast::NodeId literal(
            tree.node(pattern).payload.words[ast::kLiteralPatternLiteralWord]);
        if (!tree.contains(literal) || tree.node(literal).kind != ast::SyntaxKind::BoolLiteral) {
          return zc::none;
        }
        const ast::NodeId body(arm.payload.words[ast::kMatchArmExprBodyWord]);
        if (!tree.contains(body) || !isScalarLiteral(tree.node(body).kind)) { return zc::none; }
        if (tree.node(literal).payload.words[ast::kBoolLiteralValueWord] != 0) {
          if (trueArmBody != zc::none) return zc::none;
          trueArmBody = body;
        } else {
          if (falseArmBody != zc::none) return zc::none;
          falseArmBody = body;
        }
      }
      if (trueArmBody == zc::none || falseArmBody == zc::none) { return zc::none; }
      kind = SequentialInitializerKind::Ternary;
      ternaryCondNode = scrutinee;
      ZC_IF_SOME(value, trueArmBody) { ternaryThenNode = value; }
      ZC_IF_SOME(value, falseArmBody) { ternaryElseNode = value; }
      if (conditionIsLiteral) {
        ternaryConditionIsLiteral = true;
      } else {
        for (size_t earlier = 0; earlier < index; ++earlier) {
          if (matchesLocalReference(tree, shape.bindings[earlier].pattern, scrutinee)) {
            ternaryConditionIsLocal = true;
            break;
          }
        }
      }
    } else if (tree.node(initializer).kind == ast::SyntaxKind::MemberExpression &&
               static_cast<ast::MemberAccessKind>(
                   tree.node(initializer).payload.words[ast::kMemberExpressionAccessWord]) ==
                   ast::MemberAccessKind::Qualified) {
      // A qualified enum variant access `EnumName::Variant`. The builder
      // resolves the variant index from the binder and lowers it to an integer
      // constant.
      const ast::NodeId object(
          tree.node(initializer).payload.words[ast::kMemberExpressionObjectWord]);
      if (!tree.contains(object) || tree.node(object).kind != ast::SyntaxKind::IdentExpr) {
        return zc::none;
      }
      kind = SequentialInitializerKind::EnumVariant;
    } else if (tree.node(initializer).kind == ast::SyntaxKind::MemberExpression &&
               static_cast<ast::MemberAccessKind>(
                   tree.node(initializer).payload.words[ast::kMemberExpressionAccessWord]) ==
                   ast::MemberAccessKind::Dot) {
      // A string-length fold `s.length` where `s` is an earlier local
      // initialized with a string literal. The checker folded the byte length
      // to an integer constant; the builder lowers it to a scalar literal.
      const ast::NodeId object(
          tree.node(initializer).payload.words[ast::kMemberExpressionObjectWord]);
      const auto propertyName = tree.ident(
          ast::IdentId(tree.node(initializer).payload.words[ast::kMemberExpressionPropertyWord]));
      bool folded = false;
      if (propertyName == "length"_zc && tree.contains(object) &&
          tree.node(object).kind == ast::SyntaxKind::IdentExpr) {
        for (size_t earlier = 0; earlier < index; ++earlier) {
          if (matchesLocalReference(tree, shape.bindings[earlier].pattern, object) &&
              tree.contains(shape.bindings[earlier].initializer) &&
              tree.node(shape.bindings[earlier].initializer).kind ==
                  ast::SyntaxKind::StringLiteralExpr) {
            folded = true;
            break;
          }
        }
      }
      if (!folded) return zc::none;
      kind = SequentialInitializerKind::FoldedStringLength;
    } else if (tree.node(initializer).kind == ast::SyntaxKind::CallExpression) {
      // An enum tuple-variant construction `Enum::Variant(args)`. The builder
      // dead-erases the binding when it is never read, so no aggregate
      // representation is needed in HIR/MIR/LIR.
      const ast::NodeId callee(
          tree.node(initializer).payload.words[ast::kCallExpressionCalleeWord]);
      if (!tree.contains(callee) || tree.node(callee).kind != ast::SyntaxKind::MemberExpression) {
        return zc::none;
      }
      if (static_cast<ast::MemberAccessKind>(
              tree.node(callee).payload.words[ast::kMemberExpressionAccessWord]) !=
          ast::MemberAccessKind::Qualified) {
        return zc::none;
      }
      const ast::NodeId object(tree.node(callee).payload.words[ast::kMemberExpressionObjectWord]);
      if (!tree.contains(object) || tree.node(object).kind != ast::SyntaxKind::IdentExpr) {
        return zc::none;
      }
      kind = SequentialInitializerKind::EnumVariantConstruction;
    } else {
      return zc::none;
    }
    shape.bindings.add(SequentialLocalBinding{
        declarator, pattern, initializer, kind, referencedLocal, zc::mv(leftOperand),
        zc::mv(rightOperand), zc::mv(unaryOperation), zc::mv(unaryOperand), castInnerNode,
        ternaryCondNode, ternaryThenNode, ternaryElseNode, ternaryConditionIsLocal,
        ternaryConditionIsLiteral, matchHasDefaultArm});
  }
  auto returnItem = statementItem(tree, tree.list(statements)[statements.size - 1]);
  if (returnItem == zc::none) return zc::none;
  ast::NodeId returnNode;
  ZC_IF_SOME(value, returnItem) { returnNode = value; }
  if (tree.node(returnNode).kind != ast::SyntaxKind::ReturnStmt) return zc::none;
  ast::NodeId returnValue(tree.node(returnNode).payload.words[ast::kReturnStmtValueWord]);
  if (!tree.contains(returnValue)) return zc::none;
  // Unwrap an unsafe-block-wrapped return so `return unsafe { x }` classifies the
  // same as `return x`; the unsafe boundary itself is retained by the outer
  // FunctionReturnShape via its own unsafe-block detection.
  if (tree.node(returnValue).kind == ast::SyntaxKind::UnsafeBlockExpr) {
    const ast::NodeId unsafeBody(
        tree.node(returnValue).payload.words[ast::kUnsafeBlockExprBodyWord]);
    if (!tree.contains(unsafeBody) || tree.node(unsafeBody).kind != ast::SyntaxKind::BlockStmt) {
      return zc::none;
    }
    const auto& unsafeBodyNode = tree.node(unsafeBody);
    const ast::NodeList unsafeStatements{
        unsafeBodyNode.payload.words[ast::kBlockStmtStmtsFirstWord],
        unsafeBodyNode.payload.words[ast::kBlockStmtStmtsSizeWord]};
    if (!tree.contains(unsafeStatements) || unsafeStatements.empty()) return zc::none;
    auto innerItem = statementItem(tree, tree.list(unsafeStatements)[unsafeStatements.size - 1]);
    if (innerItem == zc::none) return zc::none;
    ast::NodeId innerStatement;
    ZC_IF_SOME(value, innerItem) { innerStatement = value; }
    if (tree.node(innerStatement).kind != ast::SyntaxKind::ExpressionStatement) return zc::none;
    returnValue = ast::NodeId(
        tree.node(innerStatement).payload.words[ast::kExpressionStatementExpressionWord]);
    if (!tree.contains(returnValue)) return zc::none;
  }
  if (tree.node(returnValue).kind != ast::SyntaxKind::IdentExpr) { return zc::none; }
  shape.returnStatement = returnNode;
  shape.returnValue = returnValue;
  for (size_t index = 0; index < shape.bindings.size(); ++index) {
    if (matchesLocalReference(tree, shape.bindings[index].pattern, returnValue)) {
      shape.returnsLocal = index;
      break;
    }
  }
  return shape;
}

namespace {
// Visits every earlier-local index referenced by `binding`'s initializer: the
// LocalReference target, the local operands of a primitive binary (including
// nested arithmetic), the unary operand, or a ternary condition resolved by
// identifier matching against earlier patterns. Aggregate element references
// are scalar and are not tracked: a removed aggregate binding is skipped
// wholesale, and its scalar element sources stay as ordinary dead locals the
// pipeline lowers.
template <typename Visit>
void forEachBindingLocalReference(const ast::Tree& tree, const SequentialLocalShape& shape,
                                  size_t bindingIndex, Visit&& visit) {
  const auto& binding = shape.bindings[bindingIndex];
  if (binding.initializerKind == SequentialInitializerKind::LocalReference) {
    visit(binding.referencedLocal);
  } else if (binding.initializerKind == SequentialInitializerKind::PrimitiveBinary) {
    for (const auto* operand : {&binding.leftOperand, &binding.rightOperand}) {
      ZC_IF_SOME(value, *operand) {
        if (value.kind == SequentialBinaryOperandKind::LocalReference) {
          visit(value.referencedLocal);
        }
        ZC_IF_SOME(nestedLeft, value.nestedLeft) {
          if (nestedLeft.kind == SequentialBinaryOperandKind::LocalReference) {
            visit(nestedLeft.referencedLocal);
          }
        }
        ZC_IF_SOME(nestedRight, value.nestedRight) {
          if (nestedRight.kind == SequentialBinaryOperandKind::LocalReference) {
            visit(nestedRight.referencedLocal);
          }
        }
      }
    }
  } else if (binding.initializerKind == SequentialInitializerKind::PrimitiveUnary) {
    ZC_IF_SOME(value, binding.unaryOperand) {
      if (value.kind == SequentialBinaryOperandKind::LocalReference) {
        visit(value.referencedLocal);
      }
    }
  } else if (binding.initializerKind == SequentialInitializerKind::Ternary) {
    if (binding.ternaryConditionIsLocal) {
      // The shape stores only the flag; the referenced binding is found by
      // matching the condition identifier against earlier patterns, the same
      // way the builder resolves it.
      for (size_t earlier = 0; earlier < bindingIndex; ++earlier) {
        if (matchesLocalReference(tree, shape.bindings[earlier].pattern, binding.ternaryCondNode)) {
          visit(earlier);
          break;
        }
      }
    }
  }
}
}  // namespace

zc::Vector<bool> deadSequentialBindings(const ast::Tree& tree, const SequentialLocalShape& shape) {
  const size_t count = shape.bindings.size();
  zc::Vector<bool> live;
  for (size_t i = 0; i < count; ++i) live.add(false);
  // The returned local is the seed of liveness.
  ZC_IF_SOME(returned, shape.returnsLocal) {
    if (returned < count) live[returned] = true;
  }
  // Propagate liveness backward through referenced locals until fixpoint.
  bool changed = true;
  while (changed) {
    changed = false;
    for (size_t i = 0; i < count; ++i) {
      if (!live[i]) continue;
      forEachBindingLocalReference(tree, shape, i, [&](size_t referenced) {
        if (referenced < count && !live[referenced]) {
          live[referenced] = true;
          changed = true;
        }
      });
    }
  }
  zc::Vector<bool> dead;
  for (size_t i = 0; i < count; ++i) dead.add(!live[i]);
  return dead;
}

zc::Vector<bool> erasureRelatedDeadBindings(const ast::Tree& tree,
                                            const SequentialLocalShape& shape,
                                            const zc::Vector<bool>& dead,
                                            const zc::Vector<ast::NodeId>& erasedInitializers) {
  const size_t count = shape.bindings.size();
  zc::Vector<bool> removed;
  for (size_t i = 0; i < count; ++i) removed.add(false);
  // Seed: dead bindings whose initializer is an admitted dead-erase coercion
  // site. Only these bindings (and their dead transitive references) may be
  // filtered; unrelated dead scalar locals stay in the shape so ordinary
  // sequential-local bodies lower unchanged.
  for (size_t i = 0; i < count; ++i) {
    if (!dead[i]) continue;
    for (const auto erased : erasedInitializers) {
      if (shape.bindings[i].initializer == erased) {
        removed[i] = true;
        break;
      }
    }
  }
  // Seed: dead string-literal bindings whose only reader is a string-length
  // fold (`s.length`). The checker folded the byte length to an integer
  // constant, so the string carrier is never read at runtime and can be
  // filtered without a string representation in HIR/MIR/LIR.
  for (size_t i = 0; i < count; ++i) {
    if (removed[i] || !dead[i]) continue;
    if (shape.bindings[i].initializerKind != SequentialInitializerKind::Literal) continue;
    if (!tree.contains(shape.bindings[i].initializer) ||
        tree.node(shape.bindings[i].initializer).kind != ast::SyntaxKind::StringLiteralExpr) {
      continue;
    }
    for (size_t later = i + 1; later < count; ++later) {
      if (shape.bindings[later].initializerKind != SequentialInitializerKind::FoldedStringLength) {
        continue;
      }
      const ast::NodeId memberObject(tree.node(shape.bindings[later].initializer)
                                         .payload.words[ast::kMemberExpressionObjectWord]);
      if (matchesLocalReference(tree, shape.bindings[i].pattern, memberObject)) {
        removed[i] = true;
        break;
      }
    }
  }
  // A removed binding drags its dead transitive references out of the lowered
  // shape: the HIR nodes that read them disappear with the removed binding, so
  // the references have no reader left. The LIR sequential slice admits scalar
  // locals only, so an aggregate source cannot stay behind.
  bool changed = true;
  while (changed) {
    changed = false;
    for (size_t i = 0; i < count; ++i) {
      if (!removed[i]) continue;
      forEachBindingLocalReference(tree, shape, i, [&](size_t referenced) {
        if (referenced < count && dead[referenced] && !removed[referenced]) {
          removed[referenced] = true;
          changed = true;
        }
      });
    }
  }
  // Constraint: a kept binding's references must be kept. If a removed binding
  // is still referenced by a kept binding, keep it (and its references,
  // transitively) so the lowered shape stays self-consistent. Such a shape
  // carries an aggregate or erased local the LIR slice cannot lower, so it is
  // rejected downstream; the filter never produces a broken remap.
  changed = true;
  while (changed) {
    changed = false;
    for (size_t i = 0; i < count; ++i) {
      if (removed[i]) continue;
      forEachBindingLocalReference(tree, shape, i, [&](size_t referenced) {
        if (referenced < count && removed[referenced]) {
          removed[referenced] = false;
          changed = true;
        }
      });
    }
  }
  return removed;
}

SequentialLocalShape filterDeadSequentialBindings(const SequentialLocalShape& shape,
                                                  const zc::Vector<bool>& removed) {
  const size_t count = shape.bindings.size();
  // Build the old-to-new index map; removed bindings map to -1.
  zc::Vector<int> remap;
  for (size_t i = 0; i < count; ++i) remap.add(-1);
  size_t newCount = 0;
  for (size_t i = 0; i < count; ++i) {
    if (!removed[i]) remap[i] = static_cast<int>(newCount++);
  }
  SequentialLocalShape filtered{};
  filtered.body = shape.body;
  filtered.returnStatement = shape.returnStatement;
  filtered.returnValue = shape.returnValue;
  for (size_t i = 0; i < count; ++i) {
    if (removed[i]) continue;
    SequentialLocalBinding binding = shape.bindings[i];
    if (binding.initializerKind == SequentialInitializerKind::LocalReference) {
      const int mapped = remap[binding.referencedLocal];
      ZC_IREQUIRE(mapped >= 0, "kept binding must not reference a removed binding");
      binding.referencedLocal = static_cast<size_t>(mapped);
    }
    auto remapOperand = [&](SequentialBinaryOperand& operand) {
      if (operand.kind == SequentialBinaryOperandKind::LocalReference) {
        const int mapped = remap[operand.referencedLocal];
        ZC_IREQUIRE(mapped >= 0, "kept binding must not reference a removed binding");
        operand.referencedLocal = static_cast<size_t>(mapped);
      }
      ZC_IF_SOME(nestedLeft, operand.nestedLeft) {
        if (nestedLeft.kind == SequentialBinaryOperandKind::LocalReference) {
          const int mapped = remap[nestedLeft.referencedLocal];
          ZC_IREQUIRE(mapped >= 0, "kept binding must not reference a removed binding");
          nestedLeft.referencedLocal = static_cast<size_t>(mapped);
        }
      }
      ZC_IF_SOME(nestedRight, operand.nestedRight) {
        if (nestedRight.kind == SequentialBinaryOperandKind::LocalReference) {
          const int mapped = remap[nestedRight.referencedLocal];
          ZC_IREQUIRE(mapped >= 0, "kept binding must not reference a removed binding");
          nestedRight.referencedLocal = static_cast<size_t>(mapped);
        }
      }
    };
    ZC_IF_SOME(left, binding.leftOperand) { remapOperand(left); }
    ZC_IF_SOME(right, binding.rightOperand) { remapOperand(right); }
    ZC_IF_SOME(unary, binding.unaryOperand) { remapOperand(unary); }
    // The ternary condition local index is not stored in the shape; the builder
    // resolves it on-the-fly by matching the condition identifier against the
    // (filtered) earlier bindings, so no remapping is needed here.
    filtered.bindings.add(zc::mv(binding));
  }
  ZC_IF_SOME(returned, shape.returnsLocal) {
    const int mapped = remap[returned];
    ZC_IREQUIRE(mapped >= 0, "returned local must not be removed");
    filtered.returnsLocal = static_cast<size_t>(mapped);
  }
  return filtered;
}

zc::Maybe<LeadingLocalConditionalShape> leadingLocalConditionalShape(const ast::Tree& tree,
                                                                     ast::NodeId body) {
  if (!tree.contains(body) || tree.node(body).kind != ast::SyntaxKind::BlockStmt) return zc::none;
  const auto& block = tree.node(body);
  const ast::NodeList statements{block.payload.words[ast::kBlockStmtStmtsFirstWord],
                                 block.payload.words[ast::kBlockStmtStmtsSizeWord]};
  if (!tree.contains(statements) || statements.size < 2) return zc::none;
  const size_t bindingCount = statements.size - 1;
  LeadingLocalConditionalShape shape{};
  shape.body = body;
  for (size_t index = 0; index < bindingCount; ++index) {
    auto declaratorNode = localDeclarator(tree, tree.list(statements)[index]);
    if (declaratorNode == zc::none) return zc::none;
    ast::NodeId declarator;
    ZC_IF_SOME(value, declaratorNode) { declarator = value; }
    const ast::NodeId pattern(
        tree.node(declarator).payload.words[ast::kVariableDeclaratorPatternWord]);
    const ast::NodeId initializer(
        tree.node(declarator).payload.words[ast::kVariableDeclaratorInitWord]);
    if (!tree.contains(initializer)) return zc::none;
    SequentialInitializerKind kind;
    size_t referencedLocal = 0;
    if (isScalarLiteral(tree.node(initializer).kind)) {
      kind = SequentialInitializerKind::Literal;
    } else if (tree.node(initializer).kind == ast::SyntaxKind::IdentExpr) {
      kind = SequentialInitializerKind::ParameterReference;
      for (size_t earlier = 0; earlier < index; ++earlier) {
        if (matchesLocalReference(tree, shape.bindings[earlier].pattern, initializer)) {
          kind = SequentialInitializerKind::LocalReference;
          referencedLocal = earlier;
          break;
        }
      }
    } else {
      return zc::none;
    }
    shape.bindings.add(SequentialLocalBinding{
        declarator, pattern, initializer, kind, referencedLocal, zc::none, zc::none, zc::none,
        zc::none, ast::NodeId(), ast::NodeId(), ast::NodeId(), ast::NodeId()});
  }
  auto tailItem = statementItem(tree, tree.list(statements)[statements.size - 1]);
  if (tailItem == zc::none) return zc::none;
  ast::NodeId tailStatement;
  ZC_IF_SOME(value, tailItem) { tailStatement = value; }
  if (tree.node(tailStatement).kind != ast::SyntaxKind::IfStmt) return zc::none;
  auto conditional = conditionalReturnShape(tree, body, tailStatement);
  if (conditional == zc::none) return zc::none;
  if (!ZC_ASSERT_NONNULL(conditional).conditionIsEquality &&
      !ZC_ASSERT_NONNULL(conditional).conditionIsUnary) {
    return zc::none;
  }
  // Arms must be one of: scalar literal, bare identifier (parameter or local
  // reference), or a binary expression with leaf operands (literal, parameter,
  // or local reference). The builder resolves the identifier and leaf kinds.
  auto isAdmittedArm = [&](ast::NodeId armValue) -> bool {
    if (isScalarLiteral(tree.node(armValue).kind)) return true;
    if (tree.node(armValue).kind == ast::SyntaxKind::IdentExpr) return true;
    if (tree.node(armValue).kind == ast::SyntaxKind::BinaryExpr) {
      const auto op = static_cast<ast::BinaryOperatorKind>(
          tree.node(armValue).payload.words[ast::kBinaryExprOpWord]);
      if (!isArithmeticBinaryOperator(op) && !isRelationalBinaryOperator(op)) return false;
      const ast::NodeId left(tree.node(armValue).payload.words[ast::kBinaryExprLhsWord]);
      const ast::NodeId right(tree.node(armValue).payload.words[ast::kBinaryExprRhsWord]);
      if (!tree.contains(left) || !tree.contains(right)) return false;
      auto isLeaf = [&](ast::NodeId leaf) -> bool {
        return tree.node(leaf).kind == ast::SyntaxKind::IdentExpr ||
               isScalarLiteral(tree.node(leaf).kind);
      };
      return isLeaf(left) && isLeaf(right);
    }
    return false;
  };
  if (!isAdmittedArm(ZC_ASSERT_NONNULL(conditional).thenReturnValue) ||
      !isAdmittedArm(ZC_ASSERT_NONNULL(conditional).elseReturnValue)) {
    return zc::none;
  }
  shape.ifStatement = tailStatement;
  return shape;
}

zc::Maybe<FunctionReturnShape> functionReturnShape(const ast::Tree& tree,
                                                   const ast::Node& function) {
  const bool isMethod = function.kind == ast::SyntaxKind::MethodDecl;
  if (function.kind != ast::SyntaxKind::FunctionDecl && !isMethod) return zc::none;
  const auto bodyWord = isMethod ? ast::kMethodDeclBodyWord : ast::kFunctionDeclBodyWord;
  const ast::NodeId body(function.payload.words[bodyWord]);
  if (!tree.contains(body) || tree.node(body).kind != ast::SyntaxKind::BlockStmt) return zc::none;
  const auto& block = tree.node(body);
  const ast::NodeList statements{block.payload.words[ast::kBlockStmtStmtsFirstWord],
                                 block.payload.words[ast::kBlockStmtStmtsSizeWord]};
  if (!tree.contains(statements) || statements.empty()) return zc::none;
  // An inherent method is admitted only for a single flat return statement,
  // with parameter arities matching the lowered shapes exactly so the
  // capability drain keeps every other body on ZOM4099:
  // `return <literal>;` (receiver optional, no ordinary parameters),
  // `return <ordinary-parameter>;` or `return <ordinary-parameter> OP
  // <ordinary-parameter|literal>;` (receiver plus exactly one parameter), or
  // `return this.<field>;` (receiver, no ordinary parameters). Every other body
  // shape (locals, calls, loops, unsafe blocks, field-operand binaries, extra
  // parameters) returns none until its lowering exists.
  if (isMethod) {
    auto layout = methodParameterLayout(tree, function);
    if (layout == zc::none) return zc::none;
    const bool hasReceiver = ZC_ASSERT_NONNULL(layout).hasReceiver;
    const size_t ordinaryCount = ZC_ASSERT_NONNULL(layout).ordinaryCount;
    // Two-statement mutating-method shape: `this.<field> = <scalar literal>;`
    // immediately followed by `return this.<field>;` on the same field. The
    // write value is a scalar literal in this slice; receiver mutability is a
    // checker decision, not a shape decision.
    if (statements.size == 2) {
      // One-let method shape: `let id = <scalar literal>;` immediately followed
      // by `return id;`, on a shared receiver with no ordinary parameters. The
      // binding must be a plain `let`; mut locals and non-literal initializers
      // keep their own future shapes and return none.
      if (hasReceiver && ordinaryCount == 0) {
        auto letItem = statementItem(tree, tree.list(statements)[0]);
        auto letReturnItem = statementItem(tree, tree.list(statements)[1]);
        if (letItem != zc::none && letReturnItem != zc::none) {
          ast::NodeId letNode;
          ZC_IF_SOME(value, letItem) { letNode = value; }
          ast::NodeId letStatement;
          ZC_IF_SOME(value, letReturnItem) { letStatement = value; }
          auto letDeclarator = localDeclarator(tree, letNode);
          if (letDeclarator != zc::none && tree.node(letNode).kind == ast::SyntaxKind::LetStmt &&
              static_cast<ast::BindingDeclarationKind>(
                  tree.node(letNode).payload.words[ast::kLetStmtKindWord]) ==
                  ast::BindingDeclarationKind::Let &&
              tree.node(letStatement).kind == ast::SyntaxKind::ReturnStmt) {
            ast::NodeId declarator;
            ZC_IF_SOME(value, letDeclarator) { declarator = value; }
            const ast::NodeId pattern(
                tree.node(declarator).payload.words[ast::kVariableDeclaratorPatternWord]);
            const ast::NodeId initializer(
                tree.node(declarator).payload.words[ast::kVariableDeclaratorInitWord]);
            if (tree.contains(pattern) && tree.contains(initializer) &&
                isScalarLiteral(tree.node(initializer).kind)) {
              const ast::NodeId letReturnValue(
                  tree.node(letStatement).payload.words[ast::kReturnStmtValueWord]);
              if (tree.contains(letReturnValue) &&
                  matchesLocalReference(tree, pattern, letReturnValue)) {
                FunctionReturnShape shape{};
                shape.body = body;
                shape.returnStatement = letStatement;
                shape.value = letReturnValue;
                shape.localPattern = pattern;
                shape.localInitializer = initializer;
                shape.returnsLocal = true;
                shape.localReference = letReturnValue;
                return shape;
              }
            }
          }
        }
      }
      auto writeItem = statementItem(tree, tree.list(statements)[0]);
      if (writeItem == zc::none) return zc::none;
      ast::NodeId writeStatement;
      ZC_IF_SOME(value, writeItem) { writeStatement = value; }
      if (!tree.contains(writeStatement) ||
          tree.node(writeStatement).kind != ast::SyntaxKind::ExpressionStatement) {
        return zc::none;
      }
      const ast::NodeId assignment(
          tree.node(writeStatement).payload.words[ast::kExpressionStatementExpressionWord]);
      if (!tree.contains(assignment) ||
          tree.node(assignment).kind != ast::SyntaxKind::AssignmentExpr ||
          static_cast<ast::AssignmentOperatorKind>(
              tree.node(assignment).payload.words[ast::kAssignmentExprOpWord]) !=
              ast::AssignmentOperatorKind::Assign) {
        return zc::none;
      }
      const ast::NodeId writeTarget(
          tree.node(assignment).payload.words[ast::kAssignmentExprLhsWord]);
      const ast::NodeId writeValue(
          tree.node(assignment).payload.words[ast::kAssignmentExprRhsWord]);
      if (!tree.contains(writeTarget) || !tree.contains(writeValue) ||
          tree.node(writeTarget).kind != ast::SyntaxKind::MemberExpression ||
          static_cast<ast::MemberAccessKind>(
              tree.node(writeTarget).payload.words[ast::kMemberExpressionAccessWord]) !=
              ast::MemberAccessKind::Dot ||
          !isScalarLiteral(tree.node(writeValue).kind)) {
        return zc::none;
      }
      const ast::NodeId writeObject(
          tree.node(writeTarget).payload.words[ast::kMemberExpressionObjectWord]);
      if (!tree.contains(writeObject) || tree.node(writeObject).kind != ast::SyntaxKind::ThisExpr) {
        return zc::none;
      }
      auto returnItem = statementItem(tree, tree.list(statements)[1]);
      if (returnItem == zc::none) return zc::none;
      ast::NodeId returnNode;
      ZC_IF_SOME(value, returnItem) { returnNode = value; }
      if (!tree.contains(returnNode) || tree.node(returnNode).kind != ast::SyntaxKind::ReturnStmt) {
        return zc::none;
      }
      const ast::NodeId returnValue(tree.node(returnNode).payload.words[ast::kReturnStmtValueWord]);
      if (!tree.contains(returnValue) ||
          tree.node(returnValue).kind != ast::SyntaxKind::MemberExpression ||
          static_cast<ast::MemberAccessKind>(
              tree.node(returnValue).payload.words[ast::kMemberExpressionAccessWord]) !=
              ast::MemberAccessKind::Dot) {
        return zc::none;
      }
      const ast::NodeId returnObject(
          tree.node(returnValue).payload.words[ast::kMemberExpressionObjectWord]);
      if (!tree.contains(returnObject) ||
          tree.node(returnObject).kind != ast::SyntaxKind::ThisExpr) {
        return zc::none;
      }
      // The return reads the SAME field as the write. The two member expressions
      // are distinct source nodes, so compare their projected properties.
      const ast::IdentId writeProperty(
          tree.node(writeTarget).payload.words[ast::kMemberExpressionPropertyWord]);
      const ast::IdentId returnProperty(
          tree.node(returnValue).payload.words[ast::kMemberExpressionPropertyWord]);
      if (tree.ident(writeProperty) != tree.ident(returnProperty)) { return zc::none; }
      if (!hasReceiver || ordinaryCount != 0) return zc::none;
      FunctionReturnShape shape{};
      shape.body = body;
      shape.returnStatement = returnNode;
      shape.value = returnValue;
      shape.returnsReceiverField = true;
      shape.writesReceiverField = true;
      shape.receiverWriteStatement = writeStatement;
      shape.receiverWriteAssignment = assignment;
      shape.receiverWriteValue = writeValue;
      return shape;
    }
    // Void mutating-method shape: the sole statement is
    // `this.<field> = <ordinary-parameter>;` and there is no return. Receiver
    // mutability and parameter resolution are checker decisions; shape only
    // records the structure. Tried before the return-only size-1 gate.
    if (statements.size == 1 && hasReceiver && ordinaryCount == 1) {
      auto voidItem = statementItem(tree, tree.list(statements)[0]);
      if (voidItem != zc::none) {
        ast::NodeId voidStatement;
        ZC_IF_SOME(value, voidItem) { voidStatement = value; }
        if (tree.contains(voidStatement) &&
            tree.node(voidStatement).kind == ast::SyntaxKind::ExpressionStatement) {
          const ast::NodeId voidAssignment(
              tree.node(voidStatement).payload.words[ast::kExpressionStatementExpressionWord]);
          if (tree.contains(voidAssignment) &&
              tree.node(voidAssignment).kind == ast::SyntaxKind::AssignmentExpr &&
              static_cast<ast::AssignmentOperatorKind>(
                  tree.node(voidAssignment).payload.words[ast::kAssignmentExprOpWord]) ==
                  ast::AssignmentOperatorKind::Assign) {
            const ast::NodeId voidTarget(
                tree.node(voidAssignment).payload.words[ast::kAssignmentExprLhsWord]);
            const ast::NodeId voidRhs(
                tree.node(voidAssignment).payload.words[ast::kAssignmentExprRhsWord]);
            if (tree.contains(voidTarget) && tree.contains(voidRhs) &&
                tree.node(voidTarget).kind == ast::SyntaxKind::MemberExpression &&
                static_cast<ast::MemberAccessKind>(
                    tree.node(voidTarget).payload.words[ast::kMemberExpressionAccessWord]) ==
                    ast::MemberAccessKind::Dot &&
                tree.node(voidRhs).kind == ast::SyntaxKind::IdentExpr) {
              const ast::NodeId voidObject(
                  tree.node(voidTarget).payload.words[ast::kMemberExpressionObjectWord]);
              if (tree.contains(voidObject) &&
                  tree.node(voidObject).kind == ast::SyntaxKind::ThisExpr) {
                FunctionReturnShape shape{};
                shape.body = body;
                shape.writesReceiverField = true;
                shape.receiverWriteStatement = voidStatement;
                shape.receiverWriteAssignment = voidAssignment;
                shape.receiverWriteValue = voidRhs;
                shape.isVoidBody = true;
                shape.voidWriteValueIsParameter = true;
                return shape;
              }
            }
          }
        }
      }
    }
    if (statements.size != 1) return zc::none;
    auto methodItem = statementItem(tree, tree.list(statements)[0]);
    if (methodItem == zc::none) return zc::none;
    ast::NodeId methodStatement;
    ZC_IF_SOME(value, methodItem) { methodStatement = value; }
    // The method conditional: one if/else whose branches return scalar literals
    // over a bare bool ordinary-parameter condition, on a shared receiver with
    // exactly one ordinary parameter. Equality conditions, field arms, and
    // parameter arms keep their own future shapes.
    if (tree.node(methodStatement).kind == ast::SyntaxKind::IfStmt && hasReceiver &&
        ordinaryCount == 1) {
      auto methodConditional = conditionalReturnShape(tree, body, methodStatement);
      if (methodConditional != zc::none) {
        const auto& conditional = ZC_ASSERT_NONNULL(methodConditional);
        if (!conditional.conditionIsEquality &&
            tree.node(conditional.condition).kind == ast::SyntaxKind::IdentExpr &&
            isScalarLiteral(tree.node(conditional.thenReturnValue).kind) &&
            isScalarLiteral(tree.node(conditional.elseReturnValue).kind)) {
          return zc::mv(methodConditional).orDefault(FunctionReturnShape{});
        }
      }
      return zc::none;
    }
    if (!tree.contains(methodStatement) ||
        tree.node(methodStatement).kind != ast::SyntaxKind::ReturnStmt) {
      return zc::none;
    }
    ast::NodeId value(tree.node(methodStatement).payload.words[ast::kReturnStmtValueWord]);
    if (!tree.contains(value)) return zc::none;
    FunctionReturnShape shape{};
    shape.body = body;
    shape.returnStatement = methodStatement;
    shape.value = value;
    if (isScalarLiteral(tree.node(value).kind)) {
      if (ordinaryCount != 0) return zc::none;
      return shape;
    }
    if (tree.node(value).kind == ast::SyntaxKind::IdentExpr) {
      if (hasReceiver && ordinaryCount == 1) return shape;
      return zc::none;
    }
    // The parameter-binary tail: `return <ident> OP <ident|literal>` over the
    // one ordinary parameter of a shared-receiver method. Each operand is an
    // ordinary-parameter reference or a scalar literal with at least one
    // parameter reference; the operator family (relational or arithmetic) is a
    // checker decision. Receiver-field operands and nested binaries keep their
    // own future shapes.
    if (tree.node(value).kind == ast::SyntaxKind::BinaryExpr && hasReceiver && ordinaryCount == 1 &&
        isPrimitiveBinaryOperator(static_cast<ast::BinaryOperatorKind>(
            tree.node(value).payload.words[ast::kBinaryExprOpWord]))) {
      const ast::NodeId binaryLeft(tree.node(value).payload.words[ast::kBinaryExprLhsWord]);
      const ast::NodeId binaryRight(tree.node(value).payload.words[ast::kBinaryExprRhsWord]);
      if (!tree.contains(binaryLeft) || !tree.contains(binaryRight)) return zc::none;
      const bool leftIdent = tree.node(binaryLeft).kind == ast::SyntaxKind::IdentExpr;
      const bool rightIdent = tree.node(binaryRight).kind == ast::SyntaxKind::IdentExpr;
      const bool leftLiteral = isScalarLiteral(tree.node(binaryLeft).kind);
      const bool rightLiteral = isScalarLiteral(tree.node(binaryRight).kind);
      if ((!leftIdent && !leftLiteral) || (!rightIdent && !rightLiteral) ||
          (!leftIdent && !rightIdent)) {
        return zc::none;
      }
      shape.returnsComparison = true;
      shape.comparisonLeft = binaryLeft;
      shape.comparisonRight = binaryRight;
      shape.comparisonLeftIsLiteral = !leftIdent;
      shape.comparisonRightIsLiteral = !rightIdent;
      return shape;
    }
    // The receiver-field arithmetic tail: `return this.<field> OP <literal>`
    // (or the mirrored operand order) on a shared or mutable receiver with no
    // ordinary parameters. Exactly one operand is a `this.<field>` dot
    // projection and the other is a scalar literal; parameter and nested
    // operands keep their own shapes. The operator family is a checker
    // decision.
    if (tree.node(value).kind == ast::SyntaxKind::BinaryExpr && hasReceiver && ordinaryCount == 0 &&
        isPrimitiveBinaryOperator(static_cast<ast::BinaryOperatorKind>(
            tree.node(value).payload.words[ast::kBinaryExprOpWord]))) {
      const ast::NodeId binaryLeft(tree.node(value).payload.words[ast::kBinaryExprLhsWord]);
      const ast::NodeId binaryRight(tree.node(value).payload.words[ast::kBinaryExprRhsWord]);
      auto isThisField = [&](ast::NodeId operand) -> bool {
        if (!tree.contains(operand) ||
            tree.node(operand).kind != ast::SyntaxKind::MemberExpression) {
          return false;
        }
        const auto& projected = tree.node(operand);
        if (static_cast<ast::MemberAccessKind>(
                projected.payload.words[ast::kMemberExpressionAccessWord]) !=
            ast::MemberAccessKind::Dot) {
          return false;
        }
        const ast::NodeId object(projected.payload.words[ast::kMemberExpressionObjectWord]);
        return tree.contains(object) && tree.node(object).kind == ast::SyntaxKind::ThisExpr;
      };
      const bool leftField = isThisField(binaryLeft);
      const bool rightField = isThisField(binaryRight);
      if (leftField != rightField) {
        const ast::NodeId literalSide = leftField ? binaryRight : binaryLeft;
        if (isScalarLiteral(tree.node(literalSide).kind)) {
          shape.returnsReceiverFieldArithmetic = true;
          // Preserve source operand order; the literal flags record which side
          // is the scalar literal (and therefore which side is the field).
          shape.comparisonLeft = binaryLeft;
          shape.comparisonRight = binaryRight;
          shape.comparisonLeftIsLiteral = !leftField;
          shape.comparisonRightIsLiteral = leftField;
          return shape;
        }
      }
    }
    if (tree.node(value).kind == ast::SyntaxKind::MemberExpression &&
        static_cast<ast::MemberAccessKind>(
            tree.node(value).payload.words[ast::kMemberExpressionAccessWord]) ==
            ast::MemberAccessKind::Dot) {
      const ast::NodeId object(tree.node(value).payload.words[ast::kMemberExpressionObjectWord]);
      if (tree.contains(object) && tree.node(object).kind == ast::SyntaxKind::ThisExpr &&
          hasReceiver && ordinaryCount == 0) {
        shape.returnsReceiverField = true;
        return shape;
      }
    }
    // The self-call tail: `return this.<method>();` with no explicit arguments,
    // forwarding the implicit receiver to a zero-parameter method of the same
    // owner. Ordinary parameters and explicit arguments keep future shapes.
    if (tree.node(value).kind == ast::SyntaxKind::CallExpression && hasReceiver &&
        ordinaryCount == 0) {
      const ast::NodeId selfCallee(tree.node(value).payload.words[ast::kCallExpressionCalleeWord]);
      const ast::NodeList selfTypeArguments{
          tree.node(value).payload.words[ast::kCallExpressionTypeArgsFirstWord],
          tree.node(value).payload.words[ast::kCallExpressionTypeArgsSizeWord]};
      const ast::NodeList selfArguments{
          tree.node(value).payload.words[ast::kCallExpressionArgsFirstWord],
          tree.node(value).payload.words[ast::kCallExpressionArgsSizeWord]};
      if (tree.contains(selfCallee) &&
          tree.node(selfCallee).kind == ast::SyntaxKind::MemberExpression &&
          static_cast<ast::MemberAccessKind>(
              tree.node(selfCallee).payload.words[ast::kMemberExpressionAccessWord]) ==
              ast::MemberAccessKind::Dot &&
          tree.contains(selfTypeArguments) && selfTypeArguments.empty() &&
          tree.contains(selfArguments) && selfArguments.empty()) {
        const ast::NodeId selfObject(
            tree.node(selfCallee).payload.words[ast::kMemberExpressionObjectWord]);
        if (tree.contains(selfObject) && tree.node(selfObject).kind == ast::SyntaxKind::ThisExpr) {
          shape.returnsReceiverSelfCall = true;
          return shape;
        }
      }
    }
    return zc::none;
  }
  if (statements.size == 1) {
    auto conditionalItem = statementItem(tree, tree.list(statements)[0]);
    if (conditionalItem != zc::none) {
      ast::NodeId conditionalStmt;
      ZC_IF_SOME(value, conditionalItem) { conditionalStmt = value; }
      if (tree.node(conditionalStmt).kind == ast::SyntaxKind::IfStmt) {
        auto conditional = conditionalReturnShape(tree, body, conditionalStmt);
        if (conditional != zc::none &&
            (ZC_ASSERT_NONNULL(conditional).conditionLeftIsNestedArithmetic ||
             ZC_ASSERT_NONNULL(conditional).conditionRightIsNestedArithmetic)) {
          // A sole-if with a nested arithmetic comparison operand is routed
          // through the leading-local conditional path: the builder
          // synthesizes one arithmetic binding, then compares that local.
          ZC_ASSERT_NONNULL(conditional).isLeadingLocalConditional = true;
        }
        return conditional;
      }
      if (tree.node(conditionalStmt).kind == ast::SyntaxKind::MatchStmt) {
        return matchReturnShape(tree, body, conditionalStmt);
      }
    }
  }
  // Leading scalar-local bindings followed by one comparison conditional with
  // literal arms. The conditional shape carries the condition/arm nodes; the
  // leading bindings are derived on demand via leadingLocalConditionalShape.
  {
    auto leading = leadingLocalConditionalShape(tree, body);
    if (leading != zc::none) {
      ast::NodeId ifStatement;
      ZC_IF_SOME(value, leading) { ifStatement = value.ifStatement; }
      auto conditional = conditionalReturnShape(tree, body, ifStatement);
      if (conditional != zc::none) {
        ZC_ASSERT_NONNULL(conditional).isLeadingLocalConditional = true;
        return conditional;
      }
    }
  }
  auto returnStatement = statementItem(tree, tree.list(statements)[statements.size - 1]);
  if (returnStatement == zc::none) return zc::none;
  ZC_IF_SOME(statement, returnStatement) {
    if (tree.node(statement).kind != ast::SyntaxKind::ReturnStmt) return zc::none;
  }
  ast::NodeId returnNode;
  ZC_IF_SOME(statement, returnStatement) { returnNode = statement; }
  ast::NodeId value(tree.node(returnNode).payload.words[ast::kReturnStmtValueWord]);
  if (!tree.contains(value)) return zc::none;
  if (statements.size == 2) {
    // Admitted loop shape: a leading `while` loop with a bare identifier
    // condition and an empty body, followed by a scalar return.
    auto leadingItem = statementItem(tree, tree.list(statements)[0]);
    if (leadingItem != zc::none) {
      ast::NodeId leadingStmt;
      ZC_IF_SOME(item, leadingItem) { leadingStmt = item; }
      if (tree.node(leadingStmt).kind == ast::SyntaxKind::WhileStmt) {
        const auto& loop = tree.node(leadingStmt);
        const ast::NodeId loopCondition(loop.payload.words[ast::kWhileStmtCondWord]);
        const ast::NodeId loopBody(loop.payload.words[ast::kWhileStmtBodyWord]);
        if (!tree.contains(loopCondition) ||
            tree.node(loopCondition).kind != ast::SyntaxKind::IdentExpr ||
            !tree.contains(loopBody) || tree.node(loopBody).kind != ast::SyntaxKind::BlockStmt ||
            !isScalarLiteral(tree.node(value).kind)) {
          return zc::none;
        }
        const auto& loopBlock = tree.node(loopBody);
        const ast::NodeList loopStatements{loopBlock.payload.words[ast::kBlockStmtStmtsFirstWord],
                                           loopBlock.payload.words[ast::kBlockStmtStmtsSizeWord]};
        if (!tree.contains(loopStatements) || !loopStatements.empty()) return zc::none;
        FunctionReturnShape shape{};
        shape.body = body;
        shape.returnStatement = returnNode;
        shape.value = value;
        shape.isLoop = true;
        shape.loopCondition = loopCondition;
        shape.loopStatement = leadingStmt;
        return shape;
      }
    }
    // For-loop shape: a C-style `for (let id = <lit>; <ident> <cmp> <lit>;
    // <ident> = <binary>) {}` followed by a scalar return. The for-loop
    // desugars to a leading local binding plus a loop whose condition is the
    // comparison and whose body is the update write.
    auto forItem = statementItem(tree, tree.list(statements)[0]);
    if (forItem != zc::none) {
      ast::NodeId forStmt;
      ZC_IF_SOME(item, forItem) { forStmt = item; }
      if (tree.node(forStmt).kind == ast::SyntaxKind::ForStmt) {
        const auto& loop = tree.node(forStmt);
        const ast::NodeId init(loop.payload.words[ast::kForStmtInitWord]);
        const ast::NodeId cond(loop.payload.words[ast::kForStmtCondWord]);
        const ast::NodeId update(loop.payload.words[ast::kForStmtUpdateWord]);
        const ast::NodeId forBody(loop.payload.words[ast::kForStmtBodyWord]);
        if (!tree.contains(init) || !tree.contains(cond) || !tree.contains(update) ||
            !tree.contains(forBody) || !isScalarLiteral(tree.node(value).kind)) {
          return zc::none;
        }
        FunctionReturnShape shape{};
        shape.body = body;
        shape.returnStatement = returnNode;
        shape.value = value;
        shape.isForLoop = true;
        shape.forLoopInit = init;
        shape.forLoopCond = cond;
        shape.forLoopUpdate = update;
        shape.forLoopBody = forBody;
        shape.forLoopStatement = forStmt;
        return shape;
      }
    }
  }
  // For-loop accumulator shape: N leading scalar `mut` accumulator locals, a
  // C-style `for` loop whose body writes each accumulator, and a trailing
  // `return <accumulator-local>;`. The for-loop is the second-to-last
  // statement; N = statements.size - 2 >= 1. The return names the first
  // accumulator. The for-loop init/cond/update reuse the for-loop fields; the
  // accumulator patterns, initializers, and body writes are carried for the
  // builder.
  if (statements.size >= 3) {
    auto forItem = statementItem(tree, tree.list(statements)[statements.size - 2]);
    if (forItem != zc::none) {
      ast::NodeId forStmt;
      ZC_IF_SOME(item, forItem) { forStmt = item; }
      if (tree.node(forStmt).kind == ast::SyntaxKind::ForStmt) {
        const size_t accumulatorCount = statements.size - 2;
        const auto& loop = tree.node(forStmt);
        const ast::NodeId init(loop.payload.words[ast::kForStmtInitWord]);
        const ast::NodeId cond(loop.payload.words[ast::kForStmtCondWord]);
        const ast::NodeId update(loop.payload.words[ast::kForStmtUpdateWord]);
        const ast::NodeId forBody(loop.payload.words[ast::kForStmtBodyWord]);
        if (tree.contains(init) && tree.contains(cond) && tree.contains(update) &&
            tree.contains(forBody) && tree.node(value).kind == ast::SyntaxKind::IdentExpr) {
          // Validate the N leading mut declarations.
          zc::Vector<ast::NodeId> patterns;
          zc::Vector<ast::NodeId> initializers;
          bool leadingOk = true;
          for (size_t i = 0; i < accumulatorCount; ++i) {
            auto letItem = statementItem(tree, tree.list(statements)[i]);
            if (letItem == zc::none) {
              leadingOk = false;
              break;
            }
            ast::NodeId letNode;
            ZC_IF_SOME(item, letItem) { letNode = item; }
            if (tree.node(letNode).kind != ast::SyntaxKind::LetStmt ||
                static_cast<ast::BindingDeclarationKind>(
                    tree.node(letNode).payload.words[ast::kLetStmtKindWord]) !=
                    ast::BindingDeclarationKind::Mut) {
              leadingOk = false;
              break;
            }
            const ast::NodeId declarations(
                tree.node(letNode).payload.words[ast::kLetStmtDeclarationsWord]);
            if (!tree.contains(declarations) ||
                tree.node(declarations).kind != ast::SyntaxKind::VariableDeclaratorList) {
              leadingOk = false;
              break;
            }
            const ast::NodeList declarators{
                tree.node(declarations).payload.words[ast::kVariableDeclaratorListDeclsFirstWord],
                tree.node(declarations).payload.words[ast::kVariableDeclaratorListDeclsSizeWord]};
            if (!tree.contains(declarators) || declarators.size != 1) {
              leadingOk = false;
              break;
            }
            const auto declarator = tree.list(declarators)[0];
            if (!tree.contains(declarator) ||
                tree.node(declarator).kind != ast::SyntaxKind::VariableDeclarator) {
              leadingOk = false;
              break;
            }
            const ast::NodeId pattern(
                tree.node(declarator).payload.words[ast::kVariableDeclaratorPatternWord]);
            const ast::NodeId initializer(
                tree.node(declarator).payload.words[ast::kVariableDeclaratorInitWord]);
            if (!tree.contains(pattern) ||
                tree.node(pattern).kind != ast::SyntaxKind::IdentifierPattern ||
                !tree.contains(initializer) || !isScalarLiteral(tree.node(initializer).kind)) {
              leadingOk = false;
              break;
            }
            patterns.add(pattern);
            initializers.add(initializer);
          }
          if (leadingOk && matchesLocalReference(tree, patterns[0], value)) {
            // The loop body is N admitted accumulator writes optionally
            // followed by one trailing unlabeled `break;` or `continue;`.
            const auto& loopBlock = tree.node(forBody);
            const ast::NodeList loopStatements{
                loopBlock.payload.words[ast::kBlockStmtStmtsFirstWord],
                loopBlock.payload.words[ast::kBlockStmtStmtsSizeWord]};
            if (tree.contains(loopStatements) && loopStatements.size >= accumulatorCount &&
                loopStatements.size <= accumulatorCount + 2) {
              zc::Vector<ast::NodeId> bodyWrites;
              bool bodyOk = true;
              // An if-guarded break leads the body: `if (<cond>) { break; }`
              // followed by the N accumulator writes. The guard evaluates
              // before the writes in source order, so the write suffix starts
              // at index 1 and the body size is N+1.
              auto guardedBreak =
                  accumulatorGuardedBreakCondition(tree, tree.list(loopStatements)[0]);
              const bool hasGuardedBreak = guardedBreak != zc::none;
              const size_t writeOffset = hasGuardedBreak ? 1 : 0;
              if (hasGuardedBreak && loopStatements.size != accumulatorCount + 1) {
                bodyOk = false;
              }
              for (size_t i = 0; bodyOk && i < accumulatorCount; ++i) {
                if (!isAccumulatorBodyWrite(tree, tree.list(loopStatements)[writeOffset + i])) {
                  bodyOk = false;
                  break;
                }
                bodyWrites.add(tree.list(loopStatements)[writeOffset + i]);
              }
              ast::NodeId trailingBreak{};
              ast::NodeId trailingContinue{};
              if (bodyOk && !hasGuardedBreak && loopStatements.size == accumulatorCount + 1) {
                auto trailingItem =
                    statementItem(tree, tree.list(loopStatements)[accumulatorCount]);
                if (trailingItem == zc::none) {
                  bodyOk = false;
                } else {
                  ast::NodeId trailingStmt;
                  ZC_IF_SOME(item, trailingItem) { trailingStmt = item; }
                  const auto trailingKind = tree.node(trailingStmt).kind;
                  if (trailingKind != ast::SyntaxKind::BreakStmt &&
                      trailingKind != ast::SyntaxKind::ContinueStatement) {
                    bodyOk = false;
                  } else {
                    const auto labelWord = trailingKind == ast::SyntaxKind::BreakStmt
                                               ? ast::kBreakStmtLabelWord
                                               : ast::kContinueStatementLabelWord;
                    if (tree.node(trailingStmt).payload.words[labelWord] != 0) {
                      bodyOk = false;
                    } else if (trailingKind == ast::SyntaxKind::BreakStmt) {
                      trailingBreak = trailingStmt;
                    } else {
                      trailingContinue = trailingStmt;
                    }
                  }
                }
              }
              if (bodyOk) {
                FunctionReturnShape shape{};
                shape.body = body;
                shape.returnStatement = returnNode;
                shape.value = value;
                shape.isForLoopAccumulator = true;
                shape.forLoopAccumulatorPatterns = zc::mv(patterns);
                shape.forLoopAccumulatorInitializers = zc::mv(initializers);
                shape.forLoopBodyWrites = zc::mv(bodyWrites);
                shape.forLoopInit = init;
                shape.forLoopCond = cond;
                shape.forLoopUpdate = update;
                shape.forLoopBody = forBody;
                shape.forLoopStatement = forStmt;
                shape.forLoopBodyBreak = trailingBreak;
                shape.forLoopBodyContinue = trailingContinue;
                if (hasGuardedBreak) {
                  ZC_IF_SOME(breakCond, guardedBreak) {
                    shape.forLoopBodyBreakCondition = breakCond;
                  }
                }
                return shape;
              }
            }
          }
        }
      }
    }
  }
  // Nested for-loop accumulator shape: N leading scalar `mut` accumulator
  // locals, an outer C-style `for` loop whose sole body statement is an inner
  // C-style `for` loop that writes each accumulator, and a trailing
  // `return <accumulator-local>;`. The outer loop reuses the for-loop fields;
  // the inner loop nodes are carried in the nested fields. The accumulator
  // patterns, initializers, and body writes reuse the forLoopAccumulator*
  // fields. The return names the first accumulator.
  if (statements.size >= 3) {
    auto forItem = statementItem(tree, tree.list(statements)[statements.size - 2]);
    if (forItem != zc::none) {
      ast::NodeId forStmt;
      ZC_IF_SOME(item, forItem) { forStmt = item; }
      if (tree.node(forStmt).kind == ast::SyntaxKind::ForStmt) {
        const size_t accumulatorCount = statements.size - 2;
        const auto& loop = tree.node(forStmt);
        const ast::NodeId init(loop.payload.words[ast::kForStmtInitWord]);
        const ast::NodeId cond(loop.payload.words[ast::kForStmtCondWord]);
        const ast::NodeId update(loop.payload.words[ast::kForStmtUpdateWord]);
        const ast::NodeId forBody(loop.payload.words[ast::kForStmtBodyWord]);
        if (tree.contains(init) && tree.contains(cond) && tree.contains(update) &&
            tree.contains(forBody) && tree.node(value).kind == ast::SyntaxKind::IdentExpr) {
          // Validate the N leading mut declarations.
          zc::Vector<ast::NodeId> patterns;
          zc::Vector<ast::NodeId> initializers;
          bool leadingOk = true;
          for (size_t i = 0; i < accumulatorCount; ++i) {
            auto letItem = statementItem(tree, tree.list(statements)[i]);
            if (letItem == zc::none) {
              leadingOk = false;
              break;
            }
            ast::NodeId letNode;
            ZC_IF_SOME(item, letItem) { letNode = item; }
            if (tree.node(letNode).kind != ast::SyntaxKind::LetStmt ||
                static_cast<ast::BindingDeclarationKind>(
                    tree.node(letNode).payload.words[ast::kLetStmtKindWord]) !=
                    ast::BindingDeclarationKind::Mut) {
              leadingOk = false;
              break;
            }
            const ast::NodeId declarations(
                tree.node(letNode).payload.words[ast::kLetStmtDeclarationsWord]);
            if (!tree.contains(declarations) ||
                tree.node(declarations).kind != ast::SyntaxKind::VariableDeclaratorList) {
              leadingOk = false;
              break;
            }
            const ast::NodeList declarators{
                tree.node(declarations).payload.words[ast::kVariableDeclaratorListDeclsFirstWord],
                tree.node(declarations).payload.words[ast::kVariableDeclaratorListDeclsSizeWord]};
            if (!tree.contains(declarators) || declarators.size != 1) {
              leadingOk = false;
              break;
            }
            const auto declarator = tree.list(declarators)[0];
            if (!tree.contains(declarator) ||
                tree.node(declarator).kind != ast::SyntaxKind::VariableDeclarator) {
              leadingOk = false;
              break;
            }
            const ast::NodeId pattern(
                tree.node(declarator).payload.words[ast::kVariableDeclaratorPatternWord]);
            const ast::NodeId initializer(
                tree.node(declarator).payload.words[ast::kVariableDeclaratorInitWord]);
            if (!tree.contains(pattern) ||
                tree.node(pattern).kind != ast::SyntaxKind::IdentifierPattern ||
                !tree.contains(initializer) || !isScalarLiteral(tree.node(initializer).kind)) {
              leadingOk = false;
              break;
            }
            patterns.add(pattern);
            initializers.add(initializer);
          }
          if (leadingOk && matchesLocalReference(tree, patterns[0], value)) {
            // The outer loop body has exactly one statement: the inner for-loop.
            const auto& outerLoopBlock = tree.node(forBody);
            const ast::NodeList outerLoopStatements{
                outerLoopBlock.payload.words[ast::kBlockStmtStmtsFirstWord],
                outerLoopBlock.payload.words[ast::kBlockStmtStmtsSizeWord]};
            if (tree.contains(outerLoopStatements) && outerLoopStatements.size == 1) {
              auto innerForItem = statementItem(tree, tree.list(outerLoopStatements)[0]);
              if (innerForItem != zc::none) {
                ast::NodeId innerForStmt;
                ZC_IF_SOME(item, innerForItem) { innerForStmt = item; }
                if (tree.node(innerForStmt).kind == ast::SyntaxKind::ForStmt) {
                  const auto& innerLoop = tree.node(innerForStmt);
                  const ast::NodeId innerInit(innerLoop.payload.words[ast::kForStmtInitWord]);
                  const ast::NodeId innerCond(innerLoop.payload.words[ast::kForStmtCondWord]);
                  const ast::NodeId innerUpdate(innerLoop.payload.words[ast::kForStmtUpdateWord]);
                  const ast::NodeId innerBody(innerLoop.payload.words[ast::kForStmtBodyWord]);
                  if (tree.contains(innerInit) && tree.contains(innerCond) &&
                      tree.contains(innerUpdate) && tree.contains(innerBody)) {
                    // The inner loop body has N statements: N accumulator
                    // writes. The update write is in the for-loop header, not
                    // the body.
                    const auto& innerLoopBlock = tree.node(innerBody);
                    const ast::NodeList innerLoopStatements{
                        innerLoopBlock.payload.words[ast::kBlockStmtStmtsFirstWord],
                        innerLoopBlock.payload.words[ast::kBlockStmtStmtsSizeWord]};
                    if (tree.contains(innerLoopStatements) &&
                        innerLoopStatements.size == accumulatorCount) {
                      zc::Vector<ast::NodeId> bodyWrites;
                      bool bodyOk = true;
                      for (size_t i = 0; i < accumulatorCount; ++i) {
                        if (!isAccumulatorBodyWrite(tree, tree.list(innerLoopStatements)[i])) {
                          bodyOk = false;
                          break;
                        }
                        bodyWrites.add(tree.list(innerLoopStatements)[i]);
                      }
                      if (bodyOk) {
                        FunctionReturnShape shape{};
                        shape.body = body;
                        shape.returnStatement = returnNode;
                        shape.value = value;
                        shape.isNestedForLoopAccumulator = true;
                        shape.forLoopAccumulatorPatterns = zc::mv(patterns);
                        shape.forLoopAccumulatorInitializers = zc::mv(initializers);
                        shape.forLoopBodyWrites = zc::mv(bodyWrites);
                        shape.forLoopInit = init;
                        shape.forLoopCond = cond;
                        shape.forLoopUpdate = update;
                        shape.forLoopBody = forBody;
                        shape.forLoopStatement = forStmt;
                        shape.nestedForLoopInnerInit = innerInit;
                        shape.nestedForLoopInnerCond = innerCond;
                        shape.nestedForLoopInnerUpdate = innerUpdate;
                        shape.nestedForLoopInnerBody = innerBody;
                        shape.nestedForLoopInnerStatement = innerForStmt;
                        return shape;
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
  if (statements.size == 3) {
    // Loop-body composite shape: a leading `mut` local declaration, an admitted
    // `while` whose bare-identifier condition guards a body that writes that
    // local, and a trailing `return <ident>;`. The loop body writes reuse the
    // mut-local write lowering; they become the loop statement's body node ids.
    auto middleItem = statementItem(tree, tree.list(statements)[1]);
    ast::NodeId middleStmt;
    ZC_IF_SOME(item, middleItem) { middleStmt = item; }
    if (middleItem != zc::none && tree.node(middleStmt).kind == ast::SyntaxKind::WhileStmt) {
      auto leadingItem = statementItem(tree, tree.list(statements)[0]);
      ast::NodeId letNode;
      ZC_IF_SOME(item, leadingItem) { letNode = item; }
      const auto& loop = tree.node(middleStmt);
      const ast::NodeId loopCondition(loop.payload.words[ast::kWhileStmtCondWord]);
      const ast::NodeId loopBody(loop.payload.words[ast::kWhileStmtBodyWord]);
      if (leadingItem == zc::none || tree.node(letNode).kind != ast::SyntaxKind::LetStmt ||
          static_cast<ast::BindingDeclarationKind>(
              tree.node(letNode).payload.words[ast::kLetStmtKindWord]) !=
              ast::BindingDeclarationKind::Mut ||
          !tree.contains(loopCondition) ||
          tree.node(loopCondition).kind != ast::SyntaxKind::IdentExpr || !tree.contains(loopBody) ||
          tree.node(loopBody).kind != ast::SyntaxKind::BlockStmt ||
          tree.node(value).kind != ast::SyntaxKind::IdentExpr) {
        return zc::none;
      }
      const ast::NodeId declarations(
          tree.node(letNode).payload.words[ast::kLetStmtDeclarationsWord]);
      if (!tree.contains(declarations) ||
          tree.node(declarations).kind != ast::SyntaxKind::VariableDeclaratorList) {
        return zc::none;
      }
      const ast::NodeList declarators{
          tree.node(declarations).payload.words[ast::kVariableDeclaratorListDeclsFirstWord],
          tree.node(declarations).payload.words[ast::kVariableDeclaratorListDeclsSizeWord]};
      if (!tree.contains(declarators) || declarators.size != 1) return zc::none;
      const auto declarator = tree.list(declarators)[0];
      if (!tree.contains(declarator) ||
          tree.node(declarator).kind != ast::SyntaxKind::VariableDeclarator) {
        return zc::none;
      }
      const ast::NodeId pattern(
          tree.node(declarator).payload.words[ast::kVariableDeclaratorPatternWord]);
      const ast::NodeId initializer(
          tree.node(declarator).payload.words[ast::kVariableDeclaratorInitWord]);
      if (!tree.contains(pattern) ||
          tree.node(pattern).kind != ast::SyntaxKind::IdentifierPattern ||
          !tree.contains(initializer) || !isScalarLiteral(tree.node(initializer).kind)) {
        return zc::none;
      }
      const auto& loopBlock = tree.node(loopBody);
      const ast::NodeList loopStatements{loopBlock.payload.words[ast::kBlockStmtStmtsFirstWord],
                                         loopBlock.payload.words[ast::kBlockStmtStmtsSizeWord]};
      if (!tree.contains(loopStatements) || loopStatements.empty()) return zc::none;
      // The body is a sequence of write statements optionally followed by one
      // trailing unlabeled `break;` or `continue;` (the bounded slice admits a
      // break/continue only as the final statement). The write prefix feeds
      // `localWrites`; the trailing break/continue node is recorded separately.
      ast::NodeId trailingBreak{};
      ast::NodeId trailingContinue{};
      size_t writeCount = loopStatements.size;
      const auto statementNodes = tree.list(loopStatements);
      for (size_t statementIndex = 0; statementIndex < statementNodes.size(); ++statementIndex) {
        auto item = statementItem(tree, statementNodes[statementIndex]);
        if (item == zc::none) return zc::none;
        ast::NodeId writeStmt;
        ZC_IF_SOME(value, item) { writeStmt = value; }
        const bool isLast = statementIndex + 1 == statementNodes.size();
        const auto kind = tree.node(writeStmt).kind;
        if (kind == ast::SyntaxKind::BreakStmt || kind == ast::SyntaxKind::ContinueStatement) {
          if (!isLast) return zc::none;
          const auto labelWord = kind == ast::SyntaxKind::BreakStmt
                                     ? ast::kBreakStmtLabelWord
                                     : ast::kContinueStatementLabelWord;
          if (tree.node(writeStmt).payload.words[labelWord] != 0) return zc::none;
          if (kind == ast::SyntaxKind::BreakStmt) {
            trailingBreak = writeStmt;
          } else {
            trailingContinue = writeStmt;
          }
          writeCount = statementIndex;
          break;
        }
        if (kind != ast::SyntaxKind::ExpressionStatement) return zc::none;
        const ast::NodeId assignment(
            tree.node(writeStmt).payload.words[ast::kExpressionStatementExpressionWord]);
        if (!tree.contains(assignment) ||
            tree.node(assignment).kind != ast::SyntaxKind::AssignmentExpr ||
            static_cast<ast::AssignmentOperatorKind>(
                tree.node(assignment).payload.words[ast::kAssignmentExprOpWord]) !=
                ast::AssignmentOperatorKind::Assign) {
          return zc::none;
        }
        const ast::NodeId target(tree.node(assignment).payload.words[ast::kAssignmentExprLhsWord]);
        if (!tree.contains(target) || tree.node(target).kind != ast::SyntaxKind::IdentExpr) {
          return zc::none;
        }
      }
      // A loop body with zero writes has nothing to lower and would ICE in
      // the HIR builder's per-write digest.  Reject it at the shape level.
      if (writeCount == 0) return zc::none;
      zc::Maybe<ast::NodeId> localInitializer;
      localInitializer = initializer;
      FunctionReturnShape shape{};
      shape.body = body;
      shape.returnStatement = returnNode;
      shape.value = value;
      shape.localPattern = pattern;
      shape.localInitializer = zc::mv(localInitializer);
      shape.localWrites = ast::NodeList{loopStatements.first, static_cast<uint32_t>(writeCount)};
      shape.returnsLocal = true;
      shape.localReference = value;
      shape.isLoopBody = true;
      shape.loopCondition = loopCondition;
      shape.loopStatement = middleStmt;
      shape.loopBodyBreak = trailingBreak;
      shape.loopBodyContinue = trailingContinue;
      return shape;
    } else {
      // Discarded receiver-call statement shape:
      // `<let|mut> id = T { .. }; id.set(<literal args>); return id.get();`. The
      // middle statement is a discarded owner-local receiver call and the
      // trailing return is a second receiver call on the same owner local.
      // Only scalar-literal arguments and empty type-argument lists are
      // admitted; mutability is a checker decision. A non-matching middle
      // statement falls through to the later shape arms unchanged.
      bool matchesDiscardedShape =
          tree.node(middleStmt).kind == ast::SyntaxKind::ExpressionStatement;
      ast::NodeId middleCall;
      ast::NodeId middleCallee;
      ast::NodeList middleTypeArguments{0, 0};
      ast::NodeList middleArguments{0, 0};
      ast::NodeId middleReceiver;
      if (matchesDiscardedShape) {
        middleCall = ast::NodeId(
            tree.node(middleStmt).payload.words[ast::kExpressionStatementExpressionWord]);
        matchesDiscardedShape = tree.contains(middleCall) &&
                                tree.node(middleCall).kind == ast::SyntaxKind::CallExpression;
      }
      if (matchesDiscardedShape) {
        middleCallee =
            ast::NodeId(tree.node(middleCall).payload.words[ast::kCallExpressionCalleeWord]);
        middleTypeArguments = {
            tree.node(middleCall).payload.words[ast::kCallExpressionTypeArgsFirstWord],
            tree.node(middleCall).payload.words[ast::kCallExpressionTypeArgsSizeWord]};
        middleArguments = {tree.node(middleCall).payload.words[ast::kCallExpressionArgsFirstWord],
                           tree.node(middleCall).payload.words[ast::kCallExpressionArgsSizeWord]};
        matchesDiscardedShape =
            tree.contains(middleCallee) &&
            tree.node(middleCallee).kind == ast::SyntaxKind::MemberExpression &&
            static_cast<ast::MemberAccessKind>(
                tree.node(middleCallee).payload.words[ast::kMemberExpressionAccessWord]) ==
                ast::MemberAccessKind::Dot &&
            tree.contains(middleTypeArguments) && middleTypeArguments.empty() &&
            tree.contains(middleArguments);
      }
      if (matchesDiscardedShape) {
        for (const auto argument : tree.list(middleArguments)) {
          if (!tree.contains(argument) || !isScalarLiteral(tree.node(argument).kind)) {
            matchesDiscardedShape = false;
          }
        }
      }
      if (matchesDiscardedShape) {
        middleReceiver =
            ast::NodeId(tree.node(middleCallee).payload.words[ast::kMemberExpressionObjectWord]);
        matchesDiscardedShape = tree.contains(middleReceiver) &&
                                tree.node(middleReceiver).kind == ast::SyntaxKind::IdentExpr &&
                                tree.node(value).kind == ast::SyntaxKind::CallExpression;
      }
      ast::NodeId trailingCallee;
      ast::NodeList trailingTypeArguments{0, 0};
      ast::NodeId trailingReceiver;
      if (matchesDiscardedShape) {
        trailingCallee =
            ast::NodeId(tree.node(value).payload.words[ast::kCallExpressionCalleeWord]);
        trailingTypeArguments = {
            tree.node(value).payload.words[ast::kCallExpressionTypeArgsFirstWord],
            tree.node(value).payload.words[ast::kCallExpressionTypeArgsSizeWord]};
        matchesDiscardedShape =
            tree.contains(trailingCallee) &&
            tree.node(trailingCallee).kind == ast::SyntaxKind::MemberExpression &&
            static_cast<ast::MemberAccessKind>(
                tree.node(trailingCallee).payload.words[ast::kMemberExpressionAccessWord]) ==
                ast::MemberAccessKind::Dot &&
            tree.contains(trailingTypeArguments) && trailingTypeArguments.empty();
      }
      if (matchesDiscardedShape) {
        trailingReceiver =
            ast::NodeId(tree.node(trailingCallee).payload.words[ast::kMemberExpressionObjectWord]);
        matchesDiscardedShape = tree.contains(trailingReceiver) &&
                                tree.node(trailingReceiver).kind == ast::SyntaxKind::IdentExpr;
      }
      auto leadingItem = statementItem(tree, tree.list(statements)[0]);
      ast::NodeId pattern;
      ast::NodeId initializer;
      if (matchesDiscardedShape && leadingItem != zc::none) {
        ast::NodeId letNode;
        ZC_IF_SOME(item, leadingItem) { letNode = item; }
        const ast::NodeId letDeclarations(
            tree.node(letNode).payload.words[ast::kLetStmtDeclarationsWord]);
        bool letMatches =
            tree.node(letNode).kind == ast::SyntaxKind::LetStmt && tree.contains(letDeclarations) &&
            tree.node(letDeclarations).kind == ast::SyntaxKind::VariableDeclaratorList;
        ast::NodeId letDeclarator;
        if (letMatches) {
          const ast::NodeList letDeclarators{
              tree.node(letDeclarations).payload.words[ast::kVariableDeclaratorListDeclsFirstWord],
              tree.node(letDeclarations).payload.words[ast::kVariableDeclaratorListDeclsSizeWord]};
          letMatches = tree.contains(letDeclarators) && letDeclarators.size == 1;
          if (letMatches) letDeclarator = tree.list(letDeclarators)[0];
        }
        if (letMatches && tree.contains(letDeclarator) &&
            tree.node(letDeclarator).kind == ast::SyntaxKind::VariableDeclarator) {
          pattern = ast::NodeId(
              tree.node(letDeclarator).payload.words[ast::kVariableDeclaratorPatternWord]);
          initializer =
              ast::NodeId(tree.node(letDeclarator).payload.words[ast::kVariableDeclaratorInitWord]);
          letMatches = tree.contains(pattern) &&
                       tree.node(pattern).kind == ast::SyntaxKind::IdentifierPattern &&
                       tree.contains(initializer) &&
                       tree.node(initializer).kind == ast::SyntaxKind::StructLiteralExpr &&
                       matchesLocalReference(tree, pattern, middleReceiver) &&
                       matchesLocalReference(tree, pattern, trailingReceiver);
        }
        if (letMatches) {
          FunctionReturnShape shape{};
          shape.body = body;
          shape.returnStatement = returnNode;
          shape.value = value;
          shape.localPattern = pattern;
          shape.localInitializer = initializer;
          shape.returnsLocal = true;
          shape.localReference = trailingReceiver;
          shape.returnsReceiverCall = true;
          shape.hasDiscardedReceiverCallStatement = true;
          shape.discardedCallStatement = middleStmt;
          return shape;
        }
      }
    }
  }
  zc::Maybe<ast::NodeId> unsafeBlock;
  if (tree.node(value).kind == ast::SyntaxKind::UnsafeBlockExpr) {
    const ast::NodeId unsafeBody(tree.node(value).payload.words[ast::kUnsafeBlockExprBodyWord]);
    if (!tree.contains(unsafeBody) || tree.node(unsafeBody).kind != ast::SyntaxKind::BlockStmt) {
      return zc::none;
    }
    const auto& unsafeBodyNode = tree.node(unsafeBody);
    const ast::NodeList unsafeStatements{
        unsafeBodyNode.payload.words[ast::kBlockStmtStmtsFirstWord],
        unsafeBodyNode.payload.words[ast::kBlockStmtStmtsSizeWord]};
    if (!tree.contains(unsafeStatements) || unsafeStatements.empty()) return zc::none;
    auto unsafeItem = statementItem(tree, tree.list(unsafeStatements)[unsafeStatements.size - 1]);
    if (unsafeItem == zc::none) return zc::none;
    ast::NodeId innerStatement;
    ZC_IF_SOME(item, unsafeItem) { innerStatement = item; }
    if (tree.node(innerStatement).kind != ast::SyntaxKind::ExpressionStatement) return zc::none;
    const ast::NodeId innerValue(
        tree.node(innerStatement).payload.words[ast::kExpressionStatementExpressionWord]);
    if (!tree.contains(innerValue)) return zc::none;
    // The scalar-return and parameter-reborrow paths lower unsafe blocks for
    // single-statement shapes; other single-statement inner expressions keep
    // the shape but drop the unsafe-block marker.
    if (statements.size != 1 || isScalarLiteral(tree.node(innerValue).kind) ||
        reborrowReference(tree, innerValue) != zc::none) {
      unsafeBlock = value;
    }
    value = innerValue;
  }
  if (statements.size == 1) {
    // A single `return "a" + "b"` folds the concatenation at compile time.
    // The body checker emits a string-literal fact for the binary node; the
    // builder lowers it to a scalar literal return, reusing the
    // string-literal-return path.
    if (tree.contains(value) && tree.node(value).kind == ast::SyntaxKind::BinaryExpr) {
      const auto op = static_cast<ast::BinaryOperatorKind>(
          tree.node(value).payload.words[ast::kBinaryExprOpWord]);
      if (op == ast::BinaryOperatorKind::Add) {
        const ast::NodeId left(tree.node(value).payload.words[ast::kBinaryExprLhsWord]);
        const ast::NodeId right(tree.node(value).payload.words[ast::kBinaryExprRhsWord]);
        if (tree.contains(left) && tree.contains(right) &&
            tree.node(left).kind == ast::SyntaxKind::StringLiteralExpr &&
            tree.node(right).kind == ast::SyntaxKind::StringLiteralExpr) {
          FunctionReturnShape shape{};
          shape.body = body;
          shape.returnStatement = returnNode;
          shape.value = value;
          shape.returnsFoldedStringConcat = true;
          shape.unsafeBlock = zc::mv(unsafeBlock);
          return shape;
        }
      }
    }
    // A single `return <float-literal> as <integer-primitive>;` folds the cast
    // to an integer constant at compile time. The body checker emits an
    // integer-literal fact for the cast node; the builder lowers it to a
    // scalar literal return, reusing the literal-return path. The shape only
    // requires the float-literal inner structure; the target type is a
    // checker decision.
    if (tree.contains(value) && tree.node(value).kind == ast::SyntaxKind::CastExpression) {
      const ast::NodeId castExpr(tree.node(value).payload.words[ast::kCastExpressionExprWord]);
      if (tree.contains(castExpr) &&
          tree.node(castExpr).kind == ast::SyntaxKind::FloatLiteralExpr) {
        FunctionReturnShape shape{};
        shape.body = body;
        shape.returnStatement = returnNode;
        shape.value = value;
        shape.returnsFoldedFloatCast = true;
        shape.unsafeBlock = zc::mv(unsafeBlock);
        return shape;
      }
    }
    // A single `return <BinaryExpr>` returns the operation result directly. The
    // BinaryExpr is one of the six relational comparisons (result bool) or one of
    // the twelve arithmetic/bitwise operators (result operand type) over two
    // operands, each an IdentExpr parameter reference or a scalar literal, with
    // at least one parameter. Which operators are supported is a checker
    // decision; the shape only requires the primitive-binary structure.
    if (tree.contains(value) && tree.node(value).kind == ast::SyntaxKind::BinaryExpr &&
        isPrimitiveBinaryOperator(static_cast<ast::BinaryOperatorKind>(
            tree.node(value).payload.words[ast::kBinaryExprOpWord]))) {
      const ast::NodeId left(tree.node(value).payload.words[ast::kBinaryExprLhsWord]);
      const ast::NodeId right(tree.node(value).payload.words[ast::kBinaryExprRhsWord]);
      if (!tree.contains(left) || !tree.contains(right)) return zc::none;
      const bool leftIdent = tree.node(left).kind == ast::SyntaxKind::IdentExpr;
      const bool rightIdent = tree.node(right).kind == ast::SyntaxKind::IdentExpr;
      const bool leftLiteral = isScalarLiteral(tree.node(left).kind);
      const bool rightLiteral = isScalarLiteral(tree.node(right).kind);
      if ((!leftIdent && !leftLiteral) || (!rightIdent && !rightLiteral) ||
          (!leftIdent && !rightIdent)) {
        return zc::none;
      }
      FunctionReturnShape shape{};
      shape.body = body;
      shape.returnStatement = returnNode;
      shape.value = value;
      shape.returnsComparison = true;
      shape.comparisonLeft = left;
      shape.comparisonRight = right;
      shape.comparisonLeftIsLiteral = !leftIdent;
      shape.comparisonRightIsLiteral = !rightIdent;
      shape.unsafeBlock = zc::mv(unsafeBlock);
      return shape;
    }
    // A single `return <UnaryExpression>` returns the unary result directly.
    // The UnaryExpression is one of the four primitive unary operators
    // (`+` `-` `~` `!`) over one operand, an IdentExpr parameter reference or a
    // scalar literal. The HIR builder desugars each to an equivalent binary
    // operation, reusing the comparison-return materialization path.
    if (tree.contains(value) && tree.node(value).kind == ast::SyntaxKind::UnaryExpression) {
      const auto unaryOp = static_cast<ast::UnaryOperatorKind>(
          tree.node(value).payload.words[ast::kUnaryExpressionOpWord]);
      const bool isPrimitiveUnary = unaryOp == ast::UnaryOperatorKind::Plus ||
                                    unaryOp == ast::UnaryOperatorKind::Minus ||
                                    unaryOp == ast::UnaryOperatorKind::LogicalNot ||
                                    unaryOp == ast::UnaryOperatorKind::BitNot;
      if (isPrimitiveUnary) {
        const ast::NodeId operand(tree.node(value).payload.words[ast::kUnaryExpressionOperandWord]);
        if (tree.contains(operand)) {
          const bool operandIdent = tree.node(operand).kind == ast::SyntaxKind::IdentExpr;
          const bool operandLiteral = isScalarLiteral(tree.node(operand).kind);
          if (operandIdent || operandLiteral) {
            FunctionReturnShape shape{};
            shape.body = body;
            shape.returnStatement = returnNode;
            shape.value = value;
            shape.returnsUnary = true;
            shape.unaryOperand = operand;
            shape.unaryOperandIsLiteral = operandLiteral;
            shape.unsafeBlock = zc::mv(unsafeBlock);
            return shape;
          }
        }
      }
    }
    // Single-statement free-function shape:
    // `return <ordinary-parameter>.<field>;` reads one field of a by-value
    // struct parameter. There are no locals in the body, so the member object is
    // necessarily a callable parameter; the builder resolves it against the
    // binder facts. Methods keep the receiver-only shapes.
    if (!isMethod && tree.contains(value) &&
        tree.node(value).kind == ast::SyntaxKind::MemberExpression &&
        static_cast<ast::MemberAccessKind>(
            tree.node(value).payload.words[ast::kMemberExpressionAccessWord]) ==
            ast::MemberAccessKind::Dot) {
      const ast::NodeId object(tree.node(value).payload.words[ast::kMemberExpressionObjectWord]);
      if (tree.contains(object) && tree.node(object).kind == ast::SyntaxKind::IdentExpr) {
        FunctionReturnShape shape{};
        shape.body = body;
        shape.returnStatement = returnNode;
        shape.value = value;
        shape.returnsParameterField = true;
        shape.unsafeBlock = zc::mv(unsafeBlock);
        return shape;
      }
    }
    FunctionReturnShape shape{};
    shape.body = body;
    shape.returnStatement = returnNode;
    shape.value = value;
    shape.unsafeBlock = zc::mv(unsafeBlock);
    return shape;
  }
  {
    auto sequential = sequentialLocalShape(tree, body);
    // The N>=2 sequential shape is claimed for every leading-`let` body. A body
    // with exactly one leading `let` is normally handled by the single-local
    // path; only when that single binding is a primitive binary does it route
    // here, reusing the complete sequential binary lowering instead of a
    // separate single-local binary rail.
    if (sequential != zc::none) {
      bool routeToSequential = false;
      ZC_IF_SOME(shape, sequential) {
        routeToSequential =
            shape.bindings.size() >= 2 ||
            (shape.bindings.size() == 1 &&
             (shape.bindings[0].initializerKind == SequentialInitializerKind::PrimitiveBinary ||
              shape.bindings[0].initializerKind == SequentialInitializerKind::PrimitiveUnary ||
              shape.bindings[0].initializerKind == SequentialInitializerKind::Cast ||
              shape.bindings[0].initializerKind == SequentialInitializerKind::Ternary ||
              shape.bindings[0].initializerKind == SequentialInitializerKind::FoldedStringConcat));
      }
      if (routeToSequential) {
        FunctionReturnShape shape{};
        shape.body = body;
        shape.returnStatement = returnNode;
        shape.value = value;
        shape.returnsLocal = true;
        shape.localReference = value;
        shape.isSequentialLocalReturn = true;
        shape.unsafeBlock = zc::mv(unsafeBlock);
        return shape;
      }
    }
  }
  auto localStatement = statementItem(tree, tree.list(statements)[0]);
  if (localStatement == zc::none) return zc::none;
  ast::NodeId letNode;
  ZC_IF_SOME(statement, localStatement) { letNode = statement; }
  if (tree.node(letNode).kind != ast::SyntaxKind::LetStmt) { return zc::none; }
  ast::NodeId localReference = value;
  bool returnsReceiverCall = false;
  bool returnsDirectAggregateCall = false;
  bool returnsDirectScalarLocalCall = false;
  bool returnsLocalIncrement = false;
  bool returnsLocalPostfixIncrement = false;
  const bool returnsLocalField = tree.node(value).kind == ast::SyntaxKind::MemberExpression;
  const auto reborrow = reborrowReference(tree, value);
  const auto localBorrow = localBorrowReference(tree, value);
  if (tree.node(value).kind == ast::SyntaxKind::CallExpression) {
    const ast::NodeId callee(tree.node(value).payload.words[ast::kCallExpressionCalleeWord]);
    const ast::NodeList callTypeArguments{
        tree.node(value).payload.words[ast::kCallExpressionTypeArgsFirstWord],
        tree.node(value).payload.words[ast::kCallExpressionTypeArgsSizeWord]};
    const ast::NodeList callArguments{
        tree.node(value).payload.words[ast::kCallExpressionArgsFirstWord],
        tree.node(value).payload.words[ast::kCallExpressionArgsSizeWord]};
    if (tree.contains(callee) && tree.node(callee).kind == ast::SyntaxKind::MemberExpression &&
        static_cast<ast::MemberAccessKind>(
            tree.node(callee).payload.words[ast::kMemberExpressionAccessWord]) ==
            ast::MemberAccessKind::Dot) {
      localReference =
          ast::NodeId(tree.node(callee).payload.words[ast::kMemberExpressionObjectWord]);
      returnsReceiverCall = true;
    } else if (statements.size == 2 && tree.contains(callee) &&
               tree.node(callee).kind == ast::SyntaxKind::IdentExpr &&
               tree.contains(callTypeArguments) && callTypeArguments.empty() &&
               tree.contains(callArguments) && callArguments.size == 1 &&
               tree.contains(tree.list(callArguments)[0]) &&
               tree.node(tree.list(callArguments)[0]).kind == ast::SyntaxKind::IdentExpr) {
      // Two-statement direct call: `let v = <initializer>; return f(v);`. The
      // sole call argument must name the single local; the carrier is split on
      // the initializer kind below once it is resolved: StructLiteralExpr routes
      // the by-value aggregate slice, a scalar literal routes the scalar-local
      // slice, and every other initializer keeps the generic capability drain.
      localReference = tree.list(callArguments)[0];
      returnsDirectAggregateCall = true;
    } else {
      return zc::none;
    }
  } else if (returnsLocalField) {
    localReference = ast::NodeId(tree.node(value).payload.words[ast::kMemberExpressionObjectWord]);
  } else if (reborrow != zc::none) {
    ZC_IF_SOME(reference, reborrow) { localReference = reference; }
  } else if (localBorrow != zc::none) {
    ZC_IF_SOME(reference, localBorrow) { localReference = reference; }
  } else if (tree.node(value).kind == ast::SyntaxKind::UnaryExpression) {
    // A prefix increment/decrement return (`return ++x;` / `return --x;`)
    // desugars to a binary write (`x = x +/- 1`) followed by a local
    // reference return. The operand must be an identifier naming the
    // declared local; the binding match is verified downstream by the
    // builder.
    const auto unaryOp = static_cast<ast::UnaryOperatorKind>(
        tree.node(value).payload.words[ast::kUnaryExpressionOpWord]);
    if (unaryOp == ast::UnaryOperatorKind::PreIncrement ||
        unaryOp == ast::UnaryOperatorKind::PreDecrement) {
      const ast::NodeId unaryOperand(
          tree.node(value).payload.words[ast::kUnaryExpressionOperandWord]);
      if (tree.contains(unaryOperand) &&
          tree.node(unaryOperand).kind == ast::SyntaxKind::IdentExpr) {
        localReference = unaryOperand;
        returnsLocalIncrement = true;
      }
    }
  } else if (tree.node(value).kind == ast::SyntaxKind::PostfixExpression) {
    // A postfix increment/decrement return (`return x++;` / `return x--;`)
    // returns the old value. The increment write is dead (the local is
    // destroyed after return) and elides to a local reference return. The
    // operand must be an identifier naming the declared local; the binding
    // match is verified downstream by the builder.
    const auto postfixOp = static_cast<ast::PostfixOperatorKind>(
        tree.node(value).payload.words[ast::kPostfixExpressionOpWord]);
    if (postfixOp == ast::PostfixOperatorKind::Increment ||
        postfixOp == ast::PostfixOperatorKind::Decrement) {
      const ast::NodeId postfixOperand(
          tree.node(value).payload.words[ast::kPostfixExpressionOperandWord]);
      if (tree.contains(postfixOperand) &&
          tree.node(postfixOperand).kind == ast::SyntaxKind::IdentExpr) {
        localReference = postfixOperand;
        returnsLocalPostfixIncrement = true;
      }
    }
  }
  if (!tree.contains(localReference) ||
      tree.node(localReference).kind != ast::SyntaxKind::IdentExpr) {
    return zc::none;
  }
  const ast::NodeId declarations(tree.node(letNode).payload.words[ast::kLetStmtDeclarationsWord]);
  if (!tree.contains(declarations) ||
      tree.node(declarations).kind != ast::SyntaxKind::VariableDeclaratorList) {
    return zc::none;
  }
  const auto& declarationList = tree.node(declarations);
  const ast::NodeList declarators{
      declarationList.payload.words[ast::kVariableDeclaratorListDeclsFirstWord],
      declarationList.payload.words[ast::kVariableDeclaratorListDeclsSizeWord]};
  if (!tree.contains(declarators) || declarators.size != 1) return zc::none;
  const auto declarator = tree.list(declarators)[0];
  if (!tree.contains(declarator) ||
      tree.node(declarator).kind != ast::SyntaxKind::VariableDeclarator) {
    return zc::none;
  }
  const ast::NodeId pattern(
      tree.node(declarator).payload.words[ast::kVariableDeclaratorPatternWord]);
  const ast::NodeId initializer(
      tree.node(declarator).payload.words[ast::kVariableDeclaratorInitWord]);
  if (!tree.contains(pattern) || tree.node(pattern).kind != ast::SyntaxKind::IdentifierPattern) {
    return zc::none;
  }
  if (!tree.contains(initializer) && statements.size == 2) {
    // A local borrow requires the referent to be initialized at the borrow point.
    if (localBorrow != zc::none) return zc::none;
    // An uninitialized local passed as a direct-call argument is outside both
    // local-argument carriers, which require a literal or aggregate initializer.
    if (returnsDirectAggregateCall) return zc::none;
    FunctionReturnShape shape{};
    shape.body = body;
    shape.returnStatement = returnNode;
    shape.value = value;
    shape.localPattern = pattern;
    shape.returnsLocal = true;
    shape.localReference = localReference;
    shape.returnsLocalField = returnsLocalField;
    shape.returnsLocalReborrow = reborrow != zc::none;
    shape.returnsReceiverCall = returnsReceiverCall;
    shape.returnsDirectAggregateCall = returnsDirectAggregateCall;
    shape.returnsLocalBorrow = localBorrow != zc::none;
    shape.unsafeBlock = zc::mv(unsafeBlock);
    return shape;
  }
  if (tree.contains(initializer) && !isScalarLiteral(tree.node(initializer).kind) &&
      tree.node(initializer).kind != ast::SyntaxKind::CallExpression &&
      tree.node(initializer).kind != ast::SyntaxKind::IdentExpr &&
      tree.node(initializer).kind != ast::SyntaxKind::StructLiteralExpr &&
      !(returnsReceiverCall && tree.node(initializer).kind == ast::SyntaxKind::MemberExpression &&
        static_cast<ast::MemberAccessKind>(
            tree.node(initializer).payload.words[ast::kMemberExpressionAccessWord]) ==
            ast::MemberAccessKind::Qualified)) {
    return zc::none;
  }
  // Split the sole-local direct-call carrier by initializer kind. A struct
  // literal initializer rides the by-value aggregate slice and a scalar literal
  // rides the scalar-local slice. An identifier or call initializer names a
  // local neither call-argument carrier admits, so the shape is unclaimed: the
  // builder drains the owning definition with the capability code rather than
  // assembling an unsupported call record.
  if (returnsDirectAggregateCall && tree.contains(initializer)) {
    if (isScalarLiteral(tree.node(initializer).kind)) {
      returnsDirectAggregateCall = false;
      returnsDirectScalarLocalCall = true;
    } else if (tree.node(initializer).kind != ast::SyntaxKind::StructLiteralExpr) {
      return zc::none;
    }
  }
  if (statements.size == 2) {
    FunctionReturnShape shape{};
    shape.body = body;
    shape.returnStatement = returnNode;
    shape.value = value;
    shape.localPattern = pattern;
    shape.localInitializer = initializer;
    shape.returnsLocal = true;
    shape.localReference = localReference;
    shape.returnsLocalIncrement = returnsLocalIncrement;
    shape.returnsLocalPostfixIncrement = returnsLocalPostfixIncrement;
    shape.returnsLocalField = returnsLocalField;
    shape.returnsLocalReborrow = reborrow != zc::none;
    shape.returnsReceiverCall = returnsReceiverCall;
    shape.returnsDirectAggregateCall = returnsDirectAggregateCall;
    shape.returnsDirectScalarLocalCall = returnsDirectScalarLocalCall;
    shape.returnsLocalBorrow = localBorrow != zc::none;
    shape.unsafeBlock = zc::mv(unsafeBlock);
    return shape;
  }
  if (static_cast<ast::BindingDeclarationKind>(
          tree.node(letNode).payload.words[ast::kLetStmtKindWord]) !=
      ast::BindingDeclarationKind::Mut) {
    return zc::none;
  }
  for (size_t index = 1; index + 1 < statements.size; ++index) {
    auto writeStatement = statementItem(tree, tree.list(statements)[index]);
    if (writeStatement == zc::none) return zc::none;
    ZC_IF_SOME(statement, writeStatement) {
      if (tree.node(statement).kind != ast::SyntaxKind::ExpressionStatement) return zc::none;
      const ast::NodeId assignment(
          tree.node(statement).payload.words[ast::kExpressionStatementExpressionWord]);
      if (!tree.contains(assignment)) return zc::none;
      // A postfix increment/decrement (`x++` / `x--`) desugars to a binary
      // write (`x = x + 1` / `x = x - 1`). The operand must be an identifier
      // reference to the same mutable local as the return value; the binding
      // match is verified downstream by the builder.
      if (tree.node(assignment).kind == ast::SyntaxKind::PostfixExpression) {
        const auto postfixOp = static_cast<ast::PostfixOperatorKind>(
            tree.node(assignment).payload.words[ast::kPostfixExpressionOpWord]);
        if (postfixOp != ast::PostfixOperatorKind::Increment &&
            postfixOp != ast::PostfixOperatorKind::Decrement) {
          return zc::none;
        }
        const ast::NodeId postfixOperand(
            tree.node(assignment).payload.words[ast::kPostfixExpressionOperandWord]);
        if (!tree.contains(postfixOperand) ||
            tree.node(postfixOperand).kind != ast::SyntaxKind::IdentExpr) {
          return zc::none;
        }
        continue;
      }
      // A prefix increment/decrement (`++x` / `--x`) desugars to a binary
      // write (`x = x + 1` / `x = x - 1`), the same as the postfix form.
      // The operand must be an identifier reference to the same mutable
      // local as the return value; the binding match is verified downstream
      // by the builder.
      if (tree.node(assignment).kind == ast::SyntaxKind::UnaryExpression) {
        const auto unaryOp = static_cast<ast::UnaryOperatorKind>(
            tree.node(assignment).payload.words[ast::kUnaryExpressionOpWord]);
        if (unaryOp != ast::UnaryOperatorKind::PreIncrement &&
            unaryOp != ast::UnaryOperatorKind::PreDecrement) {
          return zc::none;
        }
        const ast::NodeId unaryOperand(
            tree.node(assignment).payload.words[ast::kUnaryExpressionOperandWord]);
        if (!tree.contains(unaryOperand) ||
            tree.node(unaryOperand).kind != ast::SyntaxKind::IdentExpr) {
          return zc::none;
        }
        continue;
      }
      if (tree.node(assignment).kind != ast::SyntaxKind::AssignmentExpr) return zc::none;
      const auto writeOp = static_cast<ast::AssignmentOperatorKind>(
          tree.node(assignment).payload.words[ast::kAssignmentExprOpWord]);
      // A compound assignment (`x += 1`) desugars to a binary write
      // (`x = x + 1`); the target and value structural checks below are the
      // same as a plain write.
      const bool compoundWrite = isCompoundAssignment(writeOp);
      if (writeOp != ast::AssignmentOperatorKind::Assign && !compoundWrite) { return zc::none; }
      const ast::NodeId target(tree.node(assignment).payload.words[ast::kAssignmentExprLhsWord]);
      const ast::NodeId writeValue(
          tree.node(assignment).payload.words[ast::kAssignmentExprRhsWord]);
      if (!tree.contains(target) || !tree.contains(writeValue)) return zc::none;
      // A scalar-local write value is a scalar literal, an identifier reference
      // (a parameter, resolved downstream), or a primitive binary operation; a
      // field write value stays literal-only in this slice. A compound
      // assignment's RHS is the binary's second operand, not a nested binary,
      // so binary values stay plain-assignment-only.
      const bool identValue = tree.node(writeValue).kind == ast::SyntaxKind::IdentExpr;
      const bool binaryValue =
          !compoundWrite && tree.node(writeValue).kind == ast::SyntaxKind::BinaryExpr;
      if (!isScalarLiteral(tree.node(writeValue).kind) && !identValue && !binaryValue) {
        return zc::none;
      }
      if (tree.node(target).kind == ast::SyntaxKind::IdentExpr) continue;
      if (identValue || binaryValue) return zc::none;
      if (!returnsLocalField || tree.node(target).kind != ast::SyntaxKind::MemberExpression) {
        return zc::none;
      }
      const ast::NodeId object(tree.node(target).payload.words[ast::kMemberExpressionObjectWord]);
      if (!tree.contains(object) || tree.node(object).kind != ast::SyntaxKind::IdentExpr) {
        return zc::none;
      }
    }
  }
  zc::Maybe<ast::NodeId> localInitializer;
  if (tree.contains(initializer)) { localInitializer = initializer; }
  FunctionReturnShape shape{};
  shape.body = body;
  shape.returnStatement = returnNode;
  shape.value = value;
  shape.localPattern = pattern;
  shape.localInitializer = zc::mv(localInitializer);
  shape.localWrites = ast::NodeList{statements.first + 1, statements.size - 2};
  shape.returnsLocal = true;
  shape.localReference = localReference;
  shape.returnsLocalField = returnsLocalField;
  shape.returnsLocalReborrow = reborrow != zc::none;
  shape.returnsReceiverCall = returnsReceiverCall;
  shape.returnsLocalBorrow = localBorrow != zc::none;
  shape.unsafeBlock = zc::mv(unsafeBlock);
  return shape;
}

}  // namespace detail
}  // namespace zomlang::compiler::hir
