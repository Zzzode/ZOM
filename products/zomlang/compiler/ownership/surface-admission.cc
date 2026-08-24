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

#include "zomlang/compiler/ownership/surface-admission.h"

#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/ast/generated/node-traverse.h"

namespace zomlang::compiler::ownership {
namespace {

bool occursAfter(const OwnershipSurfaceFailure& left,
                 const OwnershipSurfaceFailure& right) noexcept {
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

void insertFailure(zc::Vector<OwnershipSurfaceFailure>& failures,
                   OwnershipSurfaceFailure&& failure) {
  failures.add(zc::mv(failure));
  for (size_t index = failures.size() - 1;
       index != 0 && occursAfter(failures[index - 1], failures[index]); --index) {
    auto prior = zc::mv(failures[index - 1]);
    failures[index - 1] = zc::mv(failures[index]);
    failures[index] = zc::mv(prior);
  }
}

bool isAdmittedExpressionStatement(const ast::Tree& tree, const ast::Node& statement) {
  const ast::NodeId expression(statement.payload.words[ast::kExpressionStatementExpressionWord]);
  if (!tree.contains(expression)) { return false; }
  if (tree.node(expression).kind == ast::SyntaxKind::SpawnExpression) return true;
  if (tree.node(expression).kind != ast::SyntaxKind::AssignmentExpr) return false;
  const auto& assignment = tree.node(expression);
  if (static_cast<ast::AssignmentOperatorKind>(
          assignment.payload.words[ast::kAssignmentExprOpWord]) !=
      ast::AssignmentOperatorKind::Assign) {
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
      break;
    default:
      return false;
  }
  if (tree.node(target).kind == ast::SyntaxKind::IdentExpr) return true;
  if (tree.node(target).kind != ast::SyntaxKind::MemberExpression) return false;
  const ast::NodeId object(tree.node(target).payload.words[ast::kMemberExpressionObjectWord]);
  return tree.contains(object) && tree.node(object).kind == ast::SyntaxKind::IdentExpr;
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

bool hasAdmittedArguments(const ast::Tree& tree, const ast::Node& call) {
  const ast::NodeList typeArguments{call.payload.words[ast::kCallExpressionTypeArgsFirstWord],
                                    call.payload.words[ast::kCallExpressionTypeArgsSizeWord]};
  const ast::NodeList arguments{call.payload.words[ast::kCallExpressionArgsFirstWord],
                                call.payload.words[ast::kCallExpressionArgsSizeWord]};
  if (!tree.contains(typeArguments) || !tree.contains(arguments) || !typeArguments.empty()) {
    return false;
  }
  for (const auto argument : tree.list(arguments)) {
    if (!tree.contains(argument) || !isScalarLiteral(tree.node(argument).kind)) return false;
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
         hasAdmittedArguments(tree, call);
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

bool isAdmittedReturnValue(const ast::Tree& tree, ast::NodeId value) {
  if (!tree.contains(value)) return false;
  return isScalarLiteral(tree.node(value).kind) ||
         tree.node(value).kind == ast::SyntaxKind::IdentExpr || isAdmittedDirectCall(tree, value) ||
         isAdmittedReceiverCall(tree, value) || isAdmittedReferenceReborrow(tree, value) ||
         isAdmittedLocalBorrow(tree, value) || isAdmittedErrorPostfix(tree, value) ||
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

bool isAdmittedLocalInitializer(const ast::Tree& tree, ast::NodeId initializer) {
  if (!tree.contains(initializer)) return true;
  return isScalarLiteral(tree.node(initializer).kind) ||
         tree.node(initializer).kind == ast::SyntaxKind::IdentExpr ||
         isAdmittedDirectCall(tree, initializer) ||
         isAdmittedAggregateInitializer(tree, initializer);
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

/// \brief Shape-matches a `while` loop that has an admitted semantic contract.
///
/// The narrowly admitted loop form is a `while` whose condition is a bare
/// identifier (resolved to a bool parameter downstream) and whose body is an
/// empty block. This is a genuine reducible loop that Built MIR can lower to a
/// four-block CFG with a reducible back-edge.
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
  return tree.contains(statements) && statements.empty();
}

bool isAdmittedConditionalBody(const ast::Tree& tree, ast::NodeId ifStmt) {
  const auto& ifNode = tree.node(ifStmt);
  // Admit two structural condition shapes: a bare identifier (a bool parameter
  // reference) or a binary comparison of two identifiers. Which comparison
  // operators are actually supported is a checker decision (only `Eq` produces a
  // primitive-callable fact today); a non-`Eq` comparison is admitted here and
  // rejected downstream by the checker, keeping the operator-support contract in
  // a single place. Every other condition shape fails closed.
  const ast::NodeId condition(ifNode.payload.words[ast::kIfStmtCondWord]);
  if (!tree.contains(condition)) return false;
  if (tree.node(condition).kind == ast::SyntaxKind::BinaryExpr) {
    const ast::NodeId left(tree.node(condition).payload.words[ast::kBinaryExprLhsWord]);
    const ast::NodeId right(tree.node(condition).payload.words[ast::kBinaryExprRhsWord]);
    if (!tree.contains(left) || !tree.contains(right) ||
        tree.node(left).kind != ast::SyntaxKind::IdentExpr ||
        tree.node(right).kind != ast::SyntaxKind::IdentExpr) {
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
  if (statements.size == 1) return isAdmittedReturnValue(tree, returnValue);

  if (statements.size == 3) {
    auto sourceDeclarator = localDeclarator(tree, tree.list(statements)[0]);
    auto destinationDeclarator = localDeclarator(tree, tree.list(statements)[1]);
    if (sourceDeclarator != zc::none && destinationDeclarator != zc::none) {
      if (tree.node(returnValue).kind != ast::SyntaxKind::IdentExpr) return false;
      ast::NodeId source;
      ast::NodeId destination;
      ZC_IF_SOME(value, sourceDeclarator) { source = value; }
      ZC_IF_SOME(value, destinationDeclarator) { destination = value; }
      const ast::NodeId sourcePattern(
          tree.node(source).payload.words[ast::kVariableDeclaratorPatternWord]);
      const ast::NodeId sourceInitializer(
          tree.node(source).payload.words[ast::kVariableDeclaratorInitWord]);
      const ast::NodeId destinationPattern(
          tree.node(destination).payload.words[ast::kVariableDeclaratorPatternWord]);
      const ast::NodeId destinationInitializer(
          tree.node(destination).payload.words[ast::kVariableDeclaratorInitWord]);
      return (isScalarLiteral(tree.node(sourceInitializer).kind) ||
              isAdmittedAggregateInitializer(tree, sourceInitializer)) &&
             matchesLocalReference(tree, sourcePattern, destinationInitializer) &&
             (matchesLocalReference(tree, sourcePattern, returnValue) ||
              matchesLocalReference(tree, destinationPattern, returnValue));
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
      !isAdmittedLocalInitializer(tree, initializer)) {
    return false;
  }
  ast::NodeId returnReference = returnValue;
  const bool returnsField = tree.node(returnValue).kind == ast::SyntaxKind::MemberExpression;
  const bool returnsReceiverCall = isAdmittedReceiverCall(tree, returnValue);
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
  if (!tree.contains(initializer) && statements.size == 2) {
    // A local borrow requires the referent to be initialized at the borrow point.
    if (isAdmittedLocalBorrow(tree, returnValue)) return false;
    return true;
  }
  if (statements.size == 2) return true;
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
    if (!tree.contains(assignment) ||
        tree.node(assignment).kind != ast::SyntaxKind::AssignmentExpr ||
        static_cast<ast::AssignmentOperatorKind>(
            tree.node(assignment).payload.words[ast::kAssignmentExprOpWord]) !=
            ast::AssignmentOperatorKind::Assign) {
      return false;
    }
    const ast::NodeId target(tree.node(assignment).payload.words[ast::kAssignmentExprLhsWord]);
    const ast::NodeId value(tree.node(assignment).payload.words[ast::kAssignmentExprRhsWord]);
    if (!tree.contains(target) || !tree.contains(value) ||
        !isScalarLiteral(tree.node(value).kind)) {
      return false;
    }
    if (tree.node(target).kind == ast::SyntaxKind::IdentExpr) {
      if (!matchesLocalReference(tree, pattern, target)) return false;
      continue;
    }
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
  ast::visitTreePreOrder(tree, body, [&](ast::NodeId, const ast::Node& syntax) {
    if (found) return;
    if (syntax.kind == ast::SyntaxKind::SpawnExpression ||
        syntax.kind == ast::SyntaxKind::SuspendStatement ||
        syntax.kind == ast::SyntaxKind::MatchStmt || syntax.kind == ast::SyntaxKind::WhileStmt ||
        syntax.kind == ast::SyntaxKind::ForStmt || syntax.kind == ast::SyntaxKind::ForInStatement ||
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

struct OwnershipSurfaceSourceRejected::Impl final {
  explicit Impl(zc::Vector<OwnershipSurfaceFailure>&& failures) noexcept
      : failureValues(zc::mv(failures)) {}

  zc::Vector<OwnershipSurfaceFailure> failureValues;
};

OwnershipSurfaceSourceRejected::OwnershipSurfaceSourceRejected(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}
OwnershipSurfaceSourceRejected::~OwnershipSurfaceSourceRejected() noexcept(false) = default;
OwnershipSurfaceSourceRejected::OwnershipSurfaceSourceRejected(
    OwnershipSurfaceSourceRejected&&) noexcept = default;
OwnershipSurfaceSourceRejected& OwnershipSurfaceSourceRejected::operator=(
    OwnershipSurfaceSourceRejected&&) noexcept = default;
zc::ArrayPtr<const OwnershipSurfaceFailure> OwnershipSurfaceSourceRejected::failures()
    const noexcept {
  return impl->failureValues.asPtr();
}

struct OwnershipAdmittedBoundModule::Impl final {
  explicit Impl(driver::module_graph_query::CheckerBoundModuleView&& boundModule) noexcept
      : boundModuleValue(zc::mv(boundModule)) {}

  driver::module_graph_query::CheckerBoundModuleView boundModuleValue;
};

OwnershipAdmittedBoundModule::OwnershipAdmittedBoundModule(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}
OwnershipAdmittedBoundModule::~OwnershipAdmittedBoundModule() noexcept(false) = default;
OwnershipAdmittedBoundModule::OwnershipAdmittedBoundModule(
    OwnershipAdmittedBoundModule&&) noexcept = default;
OwnershipAdmittedBoundModule& OwnershipAdmittedBoundModule::operator=(
    OwnershipAdmittedBoundModule&&) noexcept = default;
const driver::module_graph_query::CheckerBoundModuleView&
OwnershipAdmittedBoundModule::boundModule() const noexcept {
  return impl->boundModuleValue;
}
OwnershipAdmittedBoundModule OwnershipAdmittedBoundModule::retain() const {
  return OwnershipAdmittedBoundModule(zc::heap<Impl>(impl->boundModuleValue.retain()));
}
OwnershipAdmittedBoundModule::operator const driver::module_graph_query::CheckerBoundModuleView&()
    const noexcept {
  return boundModule();
}
identity::SemanticContextBrand OwnershipAdmittedBoundModule::semanticContext() const noexcept {
  return boundModule().semanticContext();
}
identity::CompilationUnitId OwnershipAdmittedBoundModule::compilationUnit() const noexcept {
  return boundModule().compilationUnit();
}
identity::CrateId OwnershipAdmittedBoundModule::crate() const noexcept {
  return boundModule().crate();
}
identity::ModuleId OwnershipAdmittedBoundModule::module() const noexcept {
  return boundModule().module();
}
identity::SourceFileId OwnershipAdmittedBoundModule::sourceFile() const noexcept {
  return boundModule().sourceFile();
}
const identity::ContextFingerprint& OwnershipAdmittedBoundModule::semanticFingerprint()
    const noexcept {
  return boundModule().semanticFingerprint();
}
const ast::Tree& OwnershipAdmittedBoundModule::tree() const noexcept {
  return boundModule().tree();
}
const binder::CanonicalParsedModule& OwnershipAdmittedBoundModule::parsedModule() const noexcept {
  return boundModule().parsedModule();
}
const binder::ImmutableDefinitionInventory& OwnershipAdmittedBoundModule::definitions()
    const noexcept {
  return boundModule().definitions();
}
zc::ArrayPtr<const binder::MaterializedDependencyExportSurface>
OwnershipAdmittedBoundModule::dependencySurfaces() const noexcept {
  return boundModule().dependencySurfaces();
}
zc::Maybe<const binder::MaterializedDependencyExportSurface&>
OwnershipAdmittedBoundModule::preludeSurface() const noexcept {
  return boundModule().preludeSurface();
}
zc::ArrayPtr<const binder::ImportBindingFact> OwnershipAdmittedBoundModule::resolvedImports()
    const noexcept {
  return boundModule().resolvedImports();
}
zc::ArrayPtr<const binder::ModuleAliasBindingFact>
OwnershipAdmittedBoundModule::resolvedModuleAliases() const noexcept {
  return boundModule().resolvedModuleAliases();
}
const binder::ImmutableBindingMetadata& OwnershipAdmittedBoundModule::bindings() const noexcept {
  return boundModule().bindings();
}
const binder::VerifiedExportSurface& OwnershipAdmittedBoundModule::bindingSurface() const noexcept {
  return boundModule().bindingSurface();
}

OwnershipSurfaceAdmissionResult OwnershipSurfaceAdmissionBuilder::admit(
    driver::module_graph_query::CheckerBoundModuleView&& boundModule) {
  zc::Vector<OwnershipSurfaceFailure> failures;
  uint32_t traversalOrdinal = 0;
  // Custom traversal that skips the interior of unsafe blocks: their tail
  // expression is admitted as the block's value, not as a standalone
  // expression statement.
  auto walk = [&](ast::NodeId nodeId, auto&& self) -> void {
    const auto& syntax = boundModule.tree().node(nodeId);
    ZC_IREQUIRE(traversalOrdinal != UINT32_MAX, "ownership surface traversal overflow");
    const uint32_t ordinal = traversalOrdinal++;
    OwnershipSurfaceSyntaxKind kind;
    bool rejected = false;
    if (syntax.kind == ast::SyntaxKind::SpawnExpression) {
      kind = OwnershipSurfaceSyntaxKind::Spawn;
      rejected = true;
    } else if (syntax.kind == ast::SyntaxKind::SuspendStatement) {
      kind = OwnershipSurfaceSyntaxKind::Suspend;
      rejected = true;
    } else if (syntax.kind == ast::SyntaxKind::MatchStmt) {
      kind = OwnershipSurfaceSyntaxKind::Match;
      rejected = true;
    } else if ((syntax.kind == ast::SyntaxKind::WhileStmt &&
                !isAdmittedLoopStatement(boundModule.tree(), nodeId)) ||
               syntax.kind == ast::SyntaxKind::ForStmt ||
               syntax.kind == ast::SyntaxKind::ForInStatement ||
               syntax.kind == ast::SyntaxKind::DoWhileStatement) {
      kind = OwnershipSurfaceSyntaxKind::Loop;
      rejected = true;
    } else if (syntax.kind == ast::SyntaxKind::BreakStmt ||
               syntax.kind == ast::SyntaxKind::ContinueStatement) {
      kind = OwnershipSurfaceSyntaxKind::LoopControl;
      rejected = true;
    } else if (syntax.kind == ast::SyntaxKind::LabeledStatement) {
      kind = OwnershipSurfaceSyntaxKind::Label;
      rejected = true;
    } else if (syntax.kind == ast::SyntaxKind::ReturnStmt &&
               !boundModule.tree().contains(
                   ast::NodeId(syntax.payload.words[ast::kReturnStmtValueWord]))) {
      kind = OwnershipSurfaceSyntaxKind::VoidReturn;
      rejected = true;
    } else if (syntax.kind == ast::SyntaxKind::ExpressionStatement &&
               !isAdmittedExpressionStatement(boundModule.tree(), syntax)) {
      kind = OwnershipSurfaceSyntaxKind::ExpressionStatement;
      rejected = true;
    } else if (syntax.kind == ast::SyntaxKind::FunctionDecl &&
               requiresFunctionBodyFailure(boundModule.tree(), syntax)) {
      kind = OwnershipSurfaceSyntaxKind::FunctionBody;
      rejected = true;
    }
    if (rejected) {
      auto span = boundModule.parsedModule().spanFor(syntax.range);
      ZC_IREQUIRE(span != zc::none, "ownership surface syntax must retain a source span");
      ZC_IF_SOME(value, span) {
        insertFailure(failures, OwnershipSurfaceFailure{kind, value.clone(), ordinal});
      }
    }
    if (syntax.kind == ast::SyntaxKind::UnsafeBlockExpr) return;
    ast::visitChildNodeIds(boundModule.tree(), syntax,
                           [&](ast::NodeId child) { self(child, self); });
  };
  walk(boundModule.tree().root(), walk);
  if (failures.size() != 0) {
    return OwnershipSurfaceSourceRejected(
        zc::heap<OwnershipSurfaceSourceRejected::Impl>(zc::mv(failures)));
  }
  return OwnershipAdmittedBoundModule(
      zc::heap<OwnershipAdmittedBoundModule::Impl>(zc::mv(boundModule)));
}

}  // namespace zomlang::compiler::ownership
