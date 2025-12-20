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
#include "zomlang/compiler/ast/expression.h"
#include "zomlang/compiler/ast/statement.h"

namespace zomlang {
namespace compiler {
namespace ast {

ZC_TEST("CastTest.Isa") {
  // Identifier
  Identifier id("test"_zc);
  Node& node = id;

  ZC_EXPECT(isa<Identifier>(node));
  ZC_EXPECT(isa<PrimaryExpression>(node));  // Inheritance check
  ZC_EXPECT(isa<Expression>(node));

  // Debugging output for failure analysis
  if (isa<Statement>(node)) {
    // We cannot use zc::print here easily without including debug.h or similar,
    // and zc::print might not be available. Using standard iostream for temporary debug
    // or just rely on the assertion failure.
    // For now, let's revert to just the assertion, but we know this is where it fails.
  }

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

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
