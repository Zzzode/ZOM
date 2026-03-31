// Copyright (c) 2025 Zode.Z. All rights reserved
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

#include "zomlang/compiler/ast/cast.h"

#include "zc/ztest/test.h"
#include "zomlang/compiler/ast/ast.h"
#include "zomlang/compiler/ast/classof.h"
#include "zomlang/compiler/ast/expression.h"
#include "zomlang/compiler/ast/kinds.h"
#include "zomlang/compiler/ast/statement.h"

namespace zomlang {
namespace compiler {
namespace ast {

namespace {

zc::Vector<SyntaxKind> getAllConcreteAstKinds() {
  zc::Vector<SyntaxKind> result;
#define AST_INTERFACE_NODE(Class, Parent)
#define AST_ELEMENT_NODE(Class, ...) result.add(SyntaxKind::Class);
#include "zomlang/compiler/ast/ast-nodes.def"
#undef AST_ELEMENT_NODE
#undef AST_INTERFACE_NODE
  return result;
}

bool exerciseAllIsInterfaceFunctions(SyntaxKind kind) {
  bool value = false;
#define AST_INTERFACE_NODE(Class, Parent) value = value ^ is##Class(kind);
#define AST_ELEMENT_NODE(Class, ...)
#include "zomlang/compiler/ast/ast-nodes.def"
#undef AST_ELEMENT_NODE
#undef AST_INTERFACE_NODE
  return value;
}

}  // namespace

ZC_TEST("CastTest.Isa") {
  // Identifier
  Identifier id("test"_zc);
  Node& node = id;

  ZC_EXPECT(isa<Identifier>(node));
  ZC_EXPECT(isa<PrimaryExpression>(node));  // Inheritance check
  ZC_EXPECT(isa<Expression>(node));

  ZC_EXPECT(!isa<Statement>(node));
}

ZC_TEST("CastTest.Cast") {
  Identifier id("test"_zc);
  Node& node = id;

  // Successful cast
  Identifier& castedId = cast<Identifier>(node);
  ZC_EXPECT(castedId.getText() == "test"_zc);

  // Base class cast
  Expression& expr = cast<Expression>(node);
  ZC_EXPECT(isa<Identifier>(expr));

  // Failed cast should throw
  ZC_EXPECT_THROW_RECOVERABLE(FAILED, (void)cast<StringLiteral&>(node));
}

ZC_TEST("CastTest.DynCast") {
  Identifier id("test"_zc);
  Node& node = id;

  // Successful dyn_cast
  auto maybeId = dyn_cast<Identifier>(node);
  ZC_EXPECT(maybeId != zc::none);
  ZC_IF_SOME(val, maybeId) { ZC_EXPECT(val.getText() == "test"_zc); }

  // Failed dyn_cast
  auto maybeLit = dyn_cast<StringLiteral>(node);
  ZC_EXPECT(maybeLit == zc::none);
}

ZC_TEST("CastTest.OwnPtr") {
  zc::Own<Node> node = zc::heap<Identifier>("test"_zc);

  // isa with Own
  ZC_EXPECT(isa<Identifier>(node));
  ZC_EXPECT(!isa<StringLiteral>(node));

  // dyn_cast with Own (transfer ownership)
  {
    zc::Own<Node> n = zc::heap<Identifier>("test"_zc);
    auto maybeId = dyn_cast<Identifier>(zc::mv(n));
    ZC_EXPECT(maybeId != zc::none);
    ZC_IF_SOME(id, maybeId) { ZC_EXPECT(id->getText() == "test"_zc); }
  }

  {
    zc::Own<Node> n = zc::heap<Identifier>("test"_zc);
    auto maybeLit = dyn_cast<StringLiteral>(zc::mv(n));
    ZC_EXPECT(maybeLit == zc::none);
  }
}

ZC_TEST("CastTest.ClassofAndKindStringCoverage") {
  auto kinds = getAllConcreteAstKinds();
  ZC_EXPECT(kinds.size() > 0);

  size_t trueCount = 0;
  size_t falseCount = 0;

  for (auto kind : kinds) {
    ZC_EXPECT(isNode(kind));
    (void)exerciseAllIsInterfaceFunctions(kind);

    auto asString = _::syntaxKindToString(kind);
    ZC_EXPECT(asString != "Unknown"_zc);

    if (isExpression(kind)) {
      ++trueCount;
    } else {
      ++falseCount;
    }
  }

  ZC_EXPECT(trueCount > 0);
  ZC_EXPECT(falseCount > 0);

  ZC_EXPECT(_::syntaxKindToString(SyntaxKind::Plus) == "Unknown"_zc);
}

ZC_TEST("CastTest.OwnTransferCast") {
  {
    zc::Own<Node> node = zc::heap<Identifier>("test"_zc);
    auto id = cast<Identifier>(zc::mv(node));
    ZC_EXPECT(id->getText() == "test"_zc);
  }

  {
    zc::Own<Node> node = zc::heap<Identifier>("test"_zc);
    auto expr = cast<Expression>(zc::mv(node));
    ZC_EXPECT(isa<Identifier>(*expr));
  }

  {
    zc::Own<Node> node = zc::heap<Identifier>("test"_zc);
    ZC_EXPECT_THROW_RECOVERABLE(FAILED, (void)cast<StringLiteral>(zc::mv(node)));
  }
}

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
