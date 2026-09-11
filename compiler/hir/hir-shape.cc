// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/hir/hir-shape.h"

#include "compiler/hir/hir-internal.h"

namespace zomlang::compiler::hir {
namespace detail {
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

// Returns true when the syntactic binary operator is a relational comparison or
// an arithmetic/bitwise operator, i.e. a primitive binary operation lowerable in
// return position. Strict identity and the logical short-circuit operators are
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

bool matchesLocalReference(const ast::Tree& tree, ast::NodeId pattern, ast::NodeId reference) {
  if (!tree.contains(pattern) || !tree.contains(reference) ||
      tree.node(pattern).kind != ast::SyntaxKind::IdentifierPattern ||
      tree.node(reference).kind != ast::SyntaxKind::IdentExpr) {
    return false;
  }
  return tree.node(pattern).payload.words[ast::kIdentifierPatternNameWord] ==
         tree.node(reference).payload.words[ast::kIdentExprNameWord];
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

zc::Maybe<ast::NodeId> statementItem(const ast::Tree& tree, ast::NodeId statement) {
  if (!tree.contains(statement)) return zc::none;
  if (tree.node(statement).kind != ast::SyntaxKind::StatementListItem) { return statement; }
  const ast::NodeId item(tree.node(statement).payload.words[ast::kStatementListItemItemWord]);
  if (!tree.contains(item)) return zc::none;
  return item;
}

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
    zc::Maybe<SequentialBinaryOperand> leftOperand;
    zc::Maybe<SequentialBinaryOperand> rightOperand;
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
      bool leftIsLiteral = false;
      bool rightIsLiteral = false;
      ZC_IF_SOME(value, left) {
        leftIsLiteral = value.kind == SequentialBinaryOperandKind::Literal;
      }
      ZC_IF_SOME(value, right) {
        rightIsLiteral = value.kind == SequentialBinaryOperandKind::Literal;
      }
      // At least one operand must be a reference or a nested binary; a
      // literal-vs-literal operation has no place to lower.
      if (leftIsLiteral && rightIsLiteral) return zc::none;
      kind = SequentialInitializerKind::PrimitiveBinary;
      leftOperand = zc::mv(left);
      rightOperand = zc::mv(right);
    } else {
      return zc::none;
    }
    shape.bindings.add(SequentialLocalBinding{declarator, pattern, initializer, kind,
                                              referencedLocal, zc::mv(leftOperand),
                                              zc::mv(rightOperand)});
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
  if (tree.node(returnValue).kind != ast::SyntaxKind::IdentExpr) return zc::none;
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

zc::Maybe<FunctionReturnShape> functionReturnShape(const ast::Tree& tree,
                                                   const ast::Node& function) {
  if (function.kind != ast::SyntaxKind::FunctionDecl) return zc::none;
  const ast::NodeId body(function.payload.words[ast::kFunctionDeclBodyWord]);
  if (!tree.contains(body) || tree.node(body).kind != ast::SyntaxKind::BlockStmt) return zc::none;
  const auto& block = tree.node(body);
  const ast::NodeList statements{block.payload.words[ast::kBlockStmtStmtsFirstWord],
                                 block.payload.words[ast::kBlockStmtStmtsSizeWord]};
  if (!tree.contains(statements) || statements.empty()) return zc::none;
  if (statements.size == 1) {
    auto conditionalItem = statementItem(tree, tree.list(statements)[0]);
    if (conditionalItem != zc::none) {
      ast::NodeId conditionalStmt;
      ZC_IF_SOME(value, conditionalItem) { conditionalStmt = value; }
      if (tree.node(conditionalStmt).kind == ast::SyntaxKind::IfStmt) {
        const auto& ifNode = tree.node(conditionalStmt);
        const ast::NodeId thenStmt(ifNode.payload.words[ast::kIfStmtThenStmtWord]);
        const ast::NodeId elseStmt(ifNode.payload.words[ast::kIfStmtElseStmtWord]);
        if (tree.contains(thenStmt) && tree.contains(elseStmt) &&
            tree.node(thenStmt).kind == ast::SyntaxKind::BlockStmt &&
            tree.node(elseStmt).kind == ast::SyntaxKind::BlockStmt) {
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
            const ast::NodeId returnValue(
                tree.node(tailStmt).payload.words[ast::kReturnStmtValueWord]);
            if (!tree.contains(returnValue)) return zc::none;
            return returnValue;
          };
          auto thenValue = branchReturnValue(thenStmt);
          auto elseValue = branchReturnValue(elseStmt);
          if (thenValue != zc::none && elseValue != zc::none) {
            ast::NodeId thenNode;
            ast::NodeId elseNode;
            ZC_IF_SOME(value, thenValue) { thenNode = value; }
            ZC_IF_SOME(value, elseValue) { elseNode = value; }
            const ast::NodeId condition(ifNode.payload.words[ast::kIfStmtCondWord]);
            FunctionReturnShape shape{};
            shape.body = body;
            shape.returnStatement = conditionalStmt;
            shape.value = conditionalStmt;
            shape.isConditional = true;
            shape.condition = condition;
            shape.thenReturnValue = thenNode;
            shape.elseReturnValue = elseNode;
            // Detect the relational-comparison condition: a comparison
            // BinaryExpr for one of the six relational operators whose operands
            // are each an IdentExpr parameter reference or a scalar literal, with
            // at least one parameter operand. A bare identifier condition keeps
            // the parameter-reference lowering.
            if (tree.contains(condition) &&
                tree.node(condition).kind == ast::SyntaxKind::BinaryExpr &&
                isRelationalBinaryOperator(static_cast<ast::BinaryOperatorKind>(
                    tree.node(condition).payload.words[ast::kBinaryExprOpWord]))) {
              const ast::NodeId left(tree.node(condition).payload.words[ast::kBinaryExprLhsWord]);
              const ast::NodeId right(tree.node(condition).payload.words[ast::kBinaryExprRhsWord]);
              if (!tree.contains(left) || !tree.contains(right)) return zc::none;
              const bool leftIdent = tree.node(left).kind == ast::SyntaxKind::IdentExpr;
              const bool rightIdent = tree.node(right).kind == ast::SyntaxKind::IdentExpr;
              const bool leftLiteral = isScalarLiteral(tree.node(left).kind);
              const bool rightLiteral = isScalarLiteral(tree.node(right).kind);
              if ((!leftIdent && !leftLiteral) || (!rightIdent && !rightLiteral) ||
                  (!leftIdent && !rightIdent)) {
                return zc::none;
              }
              shape.conditionIsEquality = true;
              shape.conditionLeft = left;
              shape.conditionRight = right;
              shape.conditionLeftIsLiteral = !leftIdent;
              shape.conditionRightIsLiteral = !rightIdent;
            }
            return shape;
          }
        }
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
      for (const auto statement : tree.list(loopStatements)) {
        auto item = statementItem(tree, statement);
        if (item == zc::none) return zc::none;
        ast::NodeId writeStmt;
        ZC_IF_SOME(value, item) { writeStmt = value; }
        if (tree.node(writeStmt).kind != ast::SyntaxKind::ExpressionStatement) return zc::none;
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
      zc::Maybe<ast::NodeId> localInitializer;
      localInitializer = initializer;
      FunctionReturnShape shape{};
      shape.body = body;
      shape.returnStatement = returnNode;
      shape.value = value;
      shape.localPattern = pattern;
      shape.localInitializer = zc::mv(localInitializer);
      shape.localWrites = ast::NodeList{loopStatements.first, loopStatements.size};
      shape.returnsLocal = true;
      shape.localReference = value;
      shape.isLoopBody = true;
      shape.loopCondition = loopCondition;
      shape.loopStatement = middleStmt;
      return shape;
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
             shape.bindings[0].initializerKind == SequentialInitializerKind::PrimitiveBinary);
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
  const bool returnsLocalField = tree.node(value).kind == ast::SyntaxKind::MemberExpression;
  const auto reborrow = reborrowReference(tree, value);
  const auto localBorrow = localBorrowReference(tree, value);
  if (tree.node(value).kind == ast::SyntaxKind::CallExpression) {
    const ast::NodeId callee(tree.node(value).payload.words[ast::kCallExpressionCalleeWord]);
    if (!tree.contains(callee) || tree.node(callee).kind != ast::SyntaxKind::MemberExpression ||
        static_cast<ast::MemberAccessKind>(
            tree.node(callee).payload.words[ast::kMemberExpressionAccessWord]) !=
            ast::MemberAccessKind::Dot) {
      return zc::none;
    }
    localReference = ast::NodeId(tree.node(callee).payload.words[ast::kMemberExpressionObjectWord]);
    returnsReceiverCall = true;
  } else if (returnsLocalField) {
    localReference = ast::NodeId(tree.node(value).payload.words[ast::kMemberExpressionObjectWord]);
  } else if (reborrow != zc::none) {
    ZC_IF_SOME(reference, reborrow) { localReference = reference; }
  } else if (localBorrow != zc::none) {
    ZC_IF_SOME(reference, localBorrow) { localReference = reference; }
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
    shape.returnsLocalBorrow = localBorrow != zc::none;
    shape.unsafeBlock = zc::mv(unsafeBlock);
    return shape;
  }
  if (tree.contains(initializer) && !isScalarLiteral(tree.node(initializer).kind) &&
      tree.node(initializer).kind != ast::SyntaxKind::CallExpression &&
      tree.node(initializer).kind != ast::SyntaxKind::IdentExpr &&
      tree.node(initializer).kind != ast::SyntaxKind::StructLiteralExpr) {
    return zc::none;
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
    shape.returnsLocalField = returnsLocalField;
    shape.returnsLocalReborrow = reborrow != zc::none;
    shape.returnsReceiverCall = returnsReceiverCall;
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
      if (!tree.contains(assignment) ||
          tree.node(assignment).kind != ast::SyntaxKind::AssignmentExpr ||
          static_cast<ast::AssignmentOperatorKind>(
              tree.node(assignment).payload.words[ast::kAssignmentExprOpWord]) !=
              ast::AssignmentOperatorKind::Assign) {
        return zc::none;
      }
      const ast::NodeId target(tree.node(assignment).payload.words[ast::kAssignmentExprLhsWord]);
      const ast::NodeId writeValue(
          tree.node(assignment).payload.words[ast::kAssignmentExprRhsWord]);
      if (!tree.contains(target) || !tree.contains(writeValue)) return zc::none;
      // A scalar-local write value is a scalar literal, an identifier reference
      // (a parameter, resolved downstream), or a primitive binary operation; a
      // field write value stays literal-only in this slice.
      const bool identValue = tree.node(writeValue).kind == ast::SyntaxKind::IdentExpr;
      const bool binaryValue = tree.node(writeValue).kind == ast::SyntaxKind::BinaryExpr;
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
