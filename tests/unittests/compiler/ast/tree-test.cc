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

#include "compiler/ast/tree.h"

#include "zc/core/vector.h"
#include "zc/ztest/test.h"
#include "compiler/ast/generated/node-payload.h"

namespace zomlang {
namespace compiler {
namespace ast {

ZC_TEST("TreeBuilder.PayloadCapacityMatchesSchemaLayout") {
  ZC_EXPECT(kNodePayloadWordCount == 8);
  ZC_EXPECT(sizeof(NodePayload) == kNodePayloadByteCount);
  ZC_EXPECT(kMethodDeclPayloadWordCount == kNodePayloadWordCount);
}

ZC_TEST("TreeBuilder.AllocatesStableOneBasedNodeIds") {
  TreeBuilder builder;

  const NodeId first = builder.makeNode(SyntaxKind::SourceFile, source::SourceRange());
  const NodeId second = builder.makeNode(SyntaxKind::ModuleDeclaration, source::SourceRange());
  builder.setRoot(first);

  Tree tree = builder.finish();

  ZC_EXPECT(first.value == 1);
  ZC_EXPECT(second.value == 2);
  ZC_EXPECT(tree.root() == first);
  ZC_EXPECT(tree.nodeCount() == 2);
  ZC_EXPECT(tree.contains(first));
  ZC_EXPECT(tree.contains(second));
  ZC_EXPECT(!tree.contains(NodeId()));
  ZC_EXPECT(tree.node(first).kind == SyntaxKind::SourceFile);
  ZC_EXPECT(tree.node(second).kind == SyntaxKind::ModuleDeclaration);
}

ZC_TEST("TreeBuilder.StoresNodeListsOutsideNodeStorage") {
  TreeBuilder builder;

  const NodeId sourceFile = builder.makeNode(SyntaxKind::SourceFile, source::SourceRange());
  const NodeId firstStatement =
      builder.makeNode(SyntaxKind::ModuleDeclaration, source::SourceRange());
  const NodeId secondStatement =
      builder.makeNode(SyntaxKind::ImportDeclaration, source::SourceRange());

  zc::Vector<NodeId> statements;
  statements.add(firstStatement);
  statements.add(secondStatement);
  const NodeList list = builder.makeList(statements.asPtr());

  builder.setRoot(sourceFile);
  Tree tree = builder.finish();

  ZC_EXPECT(tree.nodeCount() == 3);
  ZC_EXPECT(list.size == 2);

  const zc::ArrayPtr<const NodeId> resolved = tree.list(list);
  ZC_EXPECT(resolved.size() == 2);
  ZC_EXPECT(resolved[0] == firstStatement);
  ZC_EXPECT(resolved[1] == secondStatement);
}

ZC_TEST("TreeBuilder.StoresInternedSyntaxText") {
  TreeBuilder builder;

  const StringId fileName = builder.internString("main.zom"_zc);
  const IdentId ident = builder.internIdent("value"_zc);
  const BigIntId integer = builder.internBigInt("42"_zc);
  const FloatId floating = builder.internFloat("3.14"_zc);

  zc::Vector<IdentId> segments;
  segments.add(ident);
  const IdentList list = builder.makeIdentList(segments.asPtr());

  const NodeId sourceFile = builder.makeNode(SyntaxKind::SourceFile, source::SourceRange());
  builder.setRoot(sourceFile);
  Tree tree = builder.finish();

  ZC_EXPECT(tree.string(fileName) == "main.zom");
  ZC_EXPECT(tree.ident(ident) == "value");
  ZC_EXPECT(tree.bigInt(integer) == "42");
  ZC_EXPECT(tree.floatLiteral(floating) == "3.14");

  const zc::ArrayPtr<const IdentId> resolved = tree.identList(list);
  ZC_EXPECT(resolved.size() == 1);
  ZC_EXPECT(resolved[0] == ident);
}

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
