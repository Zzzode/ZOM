// ZOM - Modern C++ Compiler Toolchain
// Copyright (c) 2024 Contributors
// SPDX-License-Identifier: MIT

/// \file
/// \brief Unit tests for core AST infrastructure.
///
/// This file contains ztest-based unit tests for NodeFlags bitwise operators,
/// NodeList template methods, and TokenNode creation.

#include "zomlang/compiler/ast/ast.h"

#include "zc/core/memory.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/ast/expression.h"
#include "zomlang/compiler/ast/factory.h"
#include "zomlang/compiler/ast/kinds.h"

namespace zomlang {
namespace compiler {
namespace ast {

// Helper: build a NodeList<Expression> from factory-created expressions via Vector
// (NodeList::add uses ZC_REQUIRE with nullptr comparison which is not accessible)
namespace {

template <typename T>
zc::Vector<zc::Own<T>> emptyVector() {
  return {};
}

template <typename T, typename... Args>
zc::Vector<zc::Own<T>> makeVector(Args&&... args) {
  zc::Vector<zc::Own<T>> vec;
  (vec.add(zc::fwd<Args>(args)), ...);
  return vec;
}

}  // namespace

// ================================================================================
// NodeFlags Bitwise Operator Tests
// ================================================================================

ZC_TEST("NodeFlags.BitwiseOr") {
  NodeFlags a = NodeFlags::None;
  NodeFlags b = NodeFlags::OptionalChain;
  NodeFlags result = a | b;
  ZC_EXPECT(hasFlag(result, NodeFlags::OptionalChain));
}

ZC_TEST("NodeFlags.BitwiseAnd") {
  NodeFlags flags = NodeFlags::OptionalChain;
  NodeFlags result = flags & NodeFlags::OptionalChain;
  ZC_EXPECT(hasFlag(result, NodeFlags::OptionalChain));
}

ZC_TEST("NodeFlags.BitwiseAndNone") {
  NodeFlags flags = NodeFlags::OptionalChain;
  NodeFlags result = flags & NodeFlags::None;
  ZC_EXPECT(!hasFlag(result, NodeFlags::OptionalChain));
  ZC_EXPECT(result == NodeFlags::None);
}

ZC_TEST("NodeFlags.BitwiseXor") {
  NodeFlags flags = NodeFlags::OptionalChain;
  NodeFlags result = flags ^ NodeFlags::OptionalChain;
  ZC_EXPECT(!hasFlag(result, NodeFlags::OptionalChain));
  ZC_EXPECT(result == NodeFlags::None);
}

ZC_TEST("NodeFlags.BitwiseNot") {
  NodeFlags flags = NodeFlags::None;
  NodeFlags result = ~flags;
  // Just verify it compiles and runs; the result is all bits set
  ZC_EXPECT(result != NodeFlags::None);
}

ZC_TEST("NodeFlags.OrAssignment") {
  NodeFlags flags = NodeFlags::None;
  flags |= NodeFlags::OptionalChain;
  ZC_EXPECT(hasFlag(flags, NodeFlags::OptionalChain));
}

ZC_TEST("NodeFlags.AndAssignment") {
  NodeFlags flags = NodeFlags::OptionalChain;
  flags &= NodeFlags::OptionalChain;
  ZC_EXPECT(hasFlag(flags, NodeFlags::OptionalChain));

  NodeFlags flags2 = NodeFlags::OptionalChain;
  flags2 &= NodeFlags::None;
  ZC_EXPECT(!hasFlag(flags2, NodeFlags::OptionalChain));
}

ZC_TEST("NodeFlags.XorAssignment") {
  NodeFlags flags = NodeFlags::OptionalChain;
  flags ^= NodeFlags::OptionalChain;
  ZC_EXPECT(!hasFlag(flags, NodeFlags::OptionalChain));
  ZC_EXPECT(flags == NodeFlags::None);
}

ZC_TEST("NodeFlags.HasFlag") {
  NodeFlags flags = NodeFlags::OptionalChain;
  ZC_EXPECT(hasFlag(flags, NodeFlags::OptionalChain));
}

ZC_TEST("NodeFlags.NoneHasNoFlags") {
  NodeFlags flags = NodeFlags::None;
  ZC_EXPECT(!hasFlag(flags, NodeFlags::OptionalChain));
}

ZC_TEST("NodeFlags.DoubleOrCombinesFlags") {
  NodeFlags flags = NodeFlags::None;
  flags |= NodeFlags::OptionalChain;
  flags |= NodeFlags::OptionalChain;
  ZC_EXPECT(hasFlag(flags, NodeFlags::OptionalChain));
  // ORing the same flag again should still have just that flag
  ZC_EXPECT(flags == NodeFlags::OptionalChain);
}

// ================================================================================
// NodeList Tests
// ================================================================================

ZC_TEST("NodeList.DefaultConstructionIsEmpty") {
  NodeList<Node> list;
  ZC_EXPECT(list.empty());
  ZC_EXPECT(list.size() == 0);
}

ZC_TEST("NodeList.MoveConstructionFromVector") {
  using namespace zomlang::compiler::ast::factory;

  zc::Vector<zc::Own<Expression>> vec;
  vec.add(createIntegerLiteral(1));
  vec.add(createIntegerLiteral(2));
  vec.add(createIntegerLiteral(3));

  NodeList<Expression> list(zc::mv(vec));
  ZC_EXPECT(list.size() == 3);
  ZC_EXPECT(!list.empty());
}

ZC_TEST("NodeList.AtAccess") {
  using namespace zomlang::compiler::ast::factory;

  zc::Vector<zc::Own<Expression>> vec;
  vec.add(createStringLiteral("first"_zc));
  vec.add(createStringLiteral("second"_zc));

  NodeList<Expression> list(zc::mv(vec));
  const auto& elem0 = list.at(0);
  ZC_EXPECT(elem0.getKind() == SyntaxKind::StringLiteral);

  const auto& elem1 = list.at(1);
  ZC_EXPECT(elem1.getKind() == SyntaxKind::StringLiteral);
}

ZC_TEST("NodeList.BracketAccess") {
  using namespace zomlang::compiler::ast::factory;

  zc::Vector<zc::Own<Expression>> vec;
  vec.add(createIntegerLiteral(10));
  vec.add(createIntegerLiteral(20));

  NodeList<Expression> list(zc::mv(vec));
  ZC_EXPECT(list[0].getKind() == SyntaxKind::IntegerLiteral);
  ZC_EXPECT(list[1].getKind() == SyntaxKind::IntegerLiteral);
}

ZC_TEST("NodeList.ConstBracketAccess") {
  using namespace zomlang::compiler::ast::factory;

  zc::Vector<zc::Own<Expression>> vec;
  vec.add(createIntegerLiteral(42));

  NodeList<Expression> list(zc::mv(vec));
  const NodeList<Expression>& constList = list;
  ZC_EXPECT(constList[0].getKind() == SyntaxKind::IntegerLiteral);
}

ZC_TEST("NodeList.ConstAtAccess") {
  using namespace zomlang::compiler::ast::factory;

  zc::Vector<zc::Own<Expression>> vec;
  vec.add(createStringLiteral("hello"_zc));

  NodeList<Expression> list(zc::mv(vec));
  const NodeList<Expression>& constList = list;
  const auto& elem = constList.at(0);
  ZC_EXPECT(elem.getKind() == SyntaxKind::StringLiteral);
}

ZC_TEST("NodeList.Remove") {
  using namespace zomlang::compiler::ast::factory;

  zc::Vector<zc::Own<Expression>> vec;
  vec.add(createIntegerLiteral(10));
  vec.add(createIntegerLiteral(20));
  vec.add(createIntegerLiteral(30));

  NodeList<Expression> list(zc::mv(vec));
  auto removed = list.remove(1);
  ZC_EXPECT(list.size() == 2);
  ZC_EXPECT(removed->getKind() == SyntaxKind::IntegerLiteral);
}

ZC_TEST("NodeList.RemoveFirst") {
  using namespace zomlang::compiler::ast::factory;

  zc::Vector<zc::Own<Expression>> vec;
  vec.add(createStringLiteral("a"_zc));
  vec.add(createStringLiteral("b"_zc));

  NodeList<Expression> list(zc::mv(vec));
  auto removed = list.remove(0);
  ZC_EXPECT(list.size() == 1);
  ZC_EXPECT(removed->getKind() == SyntaxKind::StringLiteral);
}

ZC_TEST("NodeList.RemoveLast") {
  using namespace zomlang::compiler::ast::factory;

  zc::Vector<zc::Own<Expression>> vec;
  vec.add(createBooleanLiteral(true));
  vec.add(createBooleanLiteral(false));

  NodeList<Expression> list(zc::mv(vec));
  auto removed = list.remove(list.size() - 1);
  ZC_EXPECT(list.size() == 1);
  ZC_EXPECT(removed->getKind() == SyntaxKind::BooleanLiteral);
}

ZC_TEST("NodeList.Clear") {
  using namespace zomlang::compiler::ast::factory;

  zc::Vector<zc::Own<Expression>> vec;
  vec.add(createBooleanLiteral(true));
  vec.add(createBooleanLiteral(false));

  NodeList<Expression> list(zc::mv(vec));
  ZC_EXPECT(list.size() == 2);

  list.clear();
  ZC_EXPECT(list.empty());
  ZC_EXPECT(list.size() == 0);
}

ZC_TEST("NodeList.IteratorTraversal") {
  using namespace zomlang::compiler::ast::factory;

  zc::Vector<zc::Own<Expression>> vec;
  vec.add(createIntegerLiteral(1));
  vec.add(createIntegerLiteral(2));
  vec.add(createIntegerLiteral(3));

  NodeList<Expression> list(zc::mv(vec));
  size_t count = 0;
  for (auto& node : list) {
    ZC_EXPECT(node.getKind() == SyntaxKind::IntegerLiteral);
    ++count;
  }
  ZC_EXPECT(count == 3);
}

ZC_TEST("NodeList.ConstIteratorTraversal") {
  using namespace zomlang::compiler::ast::factory;

  zc::Vector<zc::Own<Expression>> vec;
  vec.add(createStringLiteral("a"_zc));
  vec.add(createStringLiteral("b"_zc));

  NodeList<Expression> list(zc::mv(vec));
  const NodeList<Expression>& constList = list;
  size_t count = 0;
  for (const auto& node : constList) {
    ZC_EXPECT(node.getKind() == SyntaxKind::StringLiteral);
    ++count;
  }
  ZC_EXPECT(count == 2);
}

ZC_TEST("NodeList.BackIterator") {
  using namespace zomlang::compiler::ast::factory;

  zc::Vector<zc::Own<Expression>> vec;
  vec.add(createIntegerLiteral(1));
  vec.add(createIntegerLiteral(2));

  NodeList<Expression> list(zc::mv(vec));
  auto it = list.back();
  ZC_EXPECT(it != list.end());
}

ZC_TEST("NodeList.ConstBackIterator") {
  using namespace zomlang::compiler::ast::factory;

  zc::Vector<zc::Own<Expression>> vec;
  vec.add(createIntegerLiteral(1));
  vec.add(createIntegerLiteral(2));

  NodeList<Expression> list(zc::mv(vec));
  const NodeList<Expression>& constList = list;
  auto it = constList.back();
  ZC_EXPECT(it != constList.end());
}

ZC_TEST("NodeList.CBeginCend") {
  using namespace zomlang::compiler::ast::factory;

  zc::Vector<zc::Own<Expression>> vec;
  vec.add(createBooleanLiteral(true));

  NodeList<Expression> list(zc::mv(vec));
  auto it = list.cbegin();
  ZC_EXPECT(it != list.cend());
  ++it;
  ZC_EXPECT(it == list.cend());
}

ZC_TEST("NodeList.CreateNodeListFactory") {
  using namespace zomlang::compiler::ast::factory;

  zc::Vector<zc::Own<Expression>> vec;
  vec.add(createBooleanLiteral(true));
  vec.add(createBooleanLiteral(false));

  auto list = createNodeList(zc::mv(vec));
  ZC_EXPECT(list.size() == 2);
}

ZC_TEST("NodeList.EmptyVectorConstruction") {
  using namespace zomlang::compiler::ast::factory;

  zc::Vector<zc::Own<Expression>> vec;
  NodeList<Expression> list(zc::mv(vec));
  ZC_EXPECT(list.empty());
  ZC_EXPECT(list.size() == 0);
}

ZC_TEST("NodeList.SingleElement") {
  using namespace zomlang::compiler::ast::factory;

  zc::Vector<zc::Own<Expression>> vec;
  vec.add(createIntegerLiteral(99));

  NodeList<Expression> list(zc::mv(vec));
  ZC_EXPECT(list.size() == 1);
  ZC_EXPECT(!list.empty());
  ZC_EXPECT(list[0].getKind() == SyntaxKind::IntegerLiteral);
}

// ================================================================================
// TokenNode Tests
// ================================================================================

ZC_TEST("TokenNode.BasicCreation") {
  using namespace zomlang::compiler::ast::factory;

  auto token = createTokenNode(SyntaxKind::Plus);
  ZC_EXPECT(token->getKind() == SyntaxKind::Plus);
}

ZC_TEST("TokenNode.ComparisonOperator") {
  using namespace zomlang::compiler::ast::factory;

  auto token = createTokenNode(SyntaxKind::LessThan);
  ZC_EXPECT(token->getKind() == SyntaxKind::LessThan);
}

ZC_TEST("TokenNode.ArithmeticOperator") {
  using namespace zomlang::compiler::ast::factory;

  auto token = createTokenNode(SyntaxKind::Asterisk);
  ZC_EXPECT(token->getKind() == SyntaxKind::Asterisk);
}

ZC_TEST("TokenNode.LogicalOperator") {
  using namespace zomlang::compiler::ast::factory;

  auto token = createTokenNode(SyntaxKind::AmpersandAmpersand);
  ZC_EXPECT(token->getKind() == SyntaxKind::AmpersandAmpersand);
}

ZC_TEST("TokenNode.Punctuation") {
  using namespace zomlang::compiler::ast::factory;

  auto token = createTokenNode(SyntaxKind::Comma);
  ZC_EXPECT(token->getKind() == SyntaxKind::Comma);
}

ZC_TEST("TokenNode.AssignmentOperator") {
  using namespace zomlang::compiler::ast::factory;

  auto token = createTokenNode(SyntaxKind::Equals);
  ZC_EXPECT(token->getKind() == SyntaxKind::Equals);
}

ZC_TEST("TokenNode.GetSetFlags") {
  using namespace zomlang::compiler::ast::factory;

  auto token = createTokenNode(SyntaxKind::Minus);
  ZC_EXPECT(token->getFlags() == NodeFlags::None);

  token->setFlags(NodeFlags::OptionalChain);
  ZC_EXPECT(hasFlag(token->getFlags(), NodeFlags::OptionalChain));
}

ZC_TEST("TokenNode.ArrowOperator") {
  using namespace zomlang::compiler::ast::factory;

  auto token = createTokenNode(SyntaxKind::Arrow);
  ZC_EXPECT(token->getKind() == SyntaxKind::Arrow);
}

ZC_TEST("TokenNode.DotDotDotToken") {
  using namespace zomlang::compiler::ast::factory;

  auto token = createTokenNode(SyntaxKind::DotDotDot);
  ZC_EXPECT(token->getKind() == SyntaxKind::DotDotDot);
}

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
