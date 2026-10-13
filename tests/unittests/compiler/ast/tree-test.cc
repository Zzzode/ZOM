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

#include "compiler/ast/generated/node-payload.h"
#include "zc/core/vector.h"
#include "zc/ztest/test.h"

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

// ---------------------------------------------------------------------------
// Node ID allocation
// ---------------------------------------------------------------------------

ZC_TEST("TreeBuilder.NodeIdsAreSequentialFromOne") {
  TreeBuilder builder;

  zc::Vector<NodeId> ids;
  for (uint32_t i = 0; i < 10; ++i) {
    ids.add(builder.makeNode(SyntaxKind::IdentExpr, source::SourceRange()));
  }
  builder.setRoot(ids[0]);
  Tree tree = builder.finish();

  for (uint32_t i = 0; i < 10; ++i) {
    ZC_EXPECT(ids[i].value == i + 1, zc::str("node ", i, " should have id ", i + 1));
  }
  ZC_EXPECT(tree.nodeCount() == 10);
}

ZC_TEST("TreeBuilder.SingleNodeTree") {
  TreeBuilder builder;

  const NodeId only = builder.makeNode(SyntaxKind::NullLiteral, source::SourceRange());
  builder.setRoot(only);
  Tree tree = builder.finish();

  ZC_EXPECT(tree.nodeCount() == 1);
  ZC_EXPECT(tree.root() == only);
  ZC_EXPECT(tree.node(only).kind == SyntaxKind::NullLiteral);
}

// ---------------------------------------------------------------------------
// Tree queries and containment
// ---------------------------------------------------------------------------

ZC_TEST("Tree.DefaultConstructedTreeIsEmpty") {
  Tree tree;

  ZC_EXPECT(tree.nodeCount() == 0);
  ZC_EXPECT(!tree.root());
  ZC_EXPECT(tree.nodes().size() == 0);
  ZC_EXPECT(!tree.contains(NodeId(1)));
  ZC_EXPECT(!tree.contains(NodeId()));
}

ZC_TEST("Tree.ContainsUsesInclusiveUpperBound") {
  TreeBuilder builder;

  const NodeId first = builder.makeNode(SyntaxKind::IdentExpr, source::SourceRange());
  const NodeId second = builder.makeNode(SyntaxKind::IntLiteral, source::SourceRange());
  builder.setRoot(first);
  Tree tree = builder.finish();

  ZC_EXPECT(tree.contains(first));
  ZC_EXPECT(tree.contains(second));
  // One past the last allocated id is not contained.
  ZC_EXPECT(!tree.contains(NodeId(3)));
  // The empty id is never contained.
  ZC_EXPECT(!tree.contains(NodeId()));
}

ZC_TEST("Tree.NodesReturnsAllocationOrder") {
  TreeBuilder builder;

  const NodeId sourceFile = builder.makeNode(SyntaxKind::SourceFile, source::SourceRange());
  const NodeId moduleDecl = builder.makeNode(SyntaxKind::ModuleDeclaration, source::SourceRange());
  const NodeId importDecl = builder.makeNode(SyntaxKind::ImportDeclaration, source::SourceRange());
  builder.setRoot(sourceFile);
  Tree tree = builder.finish();

  const zc::ArrayPtr<const Node> all = tree.nodes();
  ZC_EXPECT(all.size() == 3);
  ZC_EXPECT(all[0].kind == SyntaxKind::SourceFile);
  ZC_EXPECT(all[1].kind == SyntaxKind::ModuleDeclaration);
  ZC_EXPECT(all[2].kind == SyntaxKind::ImportDeclaration);
  ZC_EXPECT(all[0].kind == tree.node(sourceFile).kind);
  ZC_EXPECT(all[1].kind == tree.node(moduleDecl).kind);
  ZC_EXPECT(all[2].kind == tree.node(importDecl).kind);
}

// ---------------------------------------------------------------------------
// Node payload storage
// ---------------------------------------------------------------------------

ZC_TEST("TreeBuilder.DefaultPayloadIsZeroInitialized") {
  TreeBuilder builder;

  const NodeId id = builder.makeNode(SyntaxKind::NullLiteral, source::SourceRange());
  builder.setRoot(id);
  Tree tree = builder.finish();

  const Node& node = tree.node(id);
  for (uint32_t i = 0; i < kNodePayloadWordCount; ++i) {
    ZC_EXPECT(node.payload.words[i] == 0, zc::str("word ", i, " should be zero"));
  }
}

ZC_TEST("TreeBuilder.NodePayloadWordsRoundTrip") {
  TreeBuilder builder;

  NodePayload payload;
  for (uint32_t i = 0; i < kNodePayloadWordCount; ++i) { payload.words[i] = (i + 1) * 111u; }
  const NodeId id = builder.makeNode(SyntaxKind::IdentExpr, source::SourceRange(), payload);
  builder.setRoot(id);
  Tree tree = builder.finish();

  const Node& node = tree.node(id);
  for (uint32_t i = 0; i < kNodePayloadWordCount; ++i) {
    ZC_EXPECT(node.payload.words[i] == (i + 1) * 111u, zc::str("word ", i));
  }
}

ZC_TEST("NodePayload.PerKindWordCountsFitFixedCapacity") {
  // Every generated per-kind payload layout must fit inside the fixed
  // 8-word NodePayload capacity.  The largest layout (MethodDecl) fills it.
  ZC_EXPECT(kMethodDeclPayloadWordCount == kNodePayloadWordCount);
  ZC_EXPECT(kAttributeListPayloadWordCount <= kNodePayloadWordCount);
  ZC_EXPECT(kFunctionDeclPayloadWordCount <= kNodePayloadWordCount);
  ZC_EXPECT(kStandaloneImplDeclPayloadWordCount <= kNodePayloadWordCount);
  ZC_EXPECT(kModuleDeclarationPayloadWordCount <= kNodePayloadWordCount);
  ZC_EXPECT(kFieldDeclPayloadWordCount <= kNodePayloadWordCount);
  ZC_EXPECT(kExternDeclPayloadWordCount <= kNodePayloadWordCount);
  ZC_EXPECT(kTupleVariantPayloadWordCount <= kNodePayloadWordCount);
  ZC_EXPECT(kSourceFilePayloadWordCount <= kNodePayloadWordCount);
  ZC_EXPECT(kForStmtPayloadWordCount <= kNodePayloadWordCount);

  // Zero-word kinds carry no payload at all.
  ZC_EXPECT(kNullLiteralPayloadWordCount == 0);
  ZC_EXPECT(kUnitLiteralPayloadWordCount == 0);
  ZC_EXPECT(kThisExprPayloadWordCount == 0);
  ZC_EXPECT(kSuperExprPayloadWordCount == 0);
  ZC_EXPECT(kEmptyStatementPayloadWordCount == 0);

  // Representative small layouts.
  ZC_EXPECT(kIdentExprPayloadWordCount == 1);
  ZC_EXPECT(kBlockStmtPayloadWordCount == 2);
  ZC_EXPECT(kBinaryExprPayloadWordCount == 3);
}

// ---------------------------------------------------------------------------
// Source range storage
// ---------------------------------------------------------------------------

ZC_TEST("TreeBuilder.SourceRangeRoundTrip") {
  static const char text[] = "let x = 42;";
  const zc::byte* start = reinterpret_cast<const zc::byte*>(text);
  const source::SourceRange range(start, start + 5);

  TreeBuilder builder;
  const NodeId id = builder.makeNode(SyntaxKind::LetStmt, range);
  builder.setRoot(id);
  Tree tree = builder.finish();

  const Node& node = tree.node(id);
  ZC_EXPECT(node.range.isValid());
  ZC_EXPECT(node.range.getStart() == source::SourceLoc(start));
  ZC_EXPECT(node.range.getEnd() == source::SourceLoc(start + 5));
  ZC_EXPECT(node.range.getLength() == 5);
}

ZC_TEST("TreeBuilder.DefaultSourceRangeIsInvalid") {
  TreeBuilder builder;

  const NodeId id = builder.makeNode(SyntaxKind::EmptyStatement, source::SourceRange());
  builder.setRoot(id);
  Tree tree = builder.finish();

  ZC_EXPECT(tree.node(id).range.isInvalid());
}

// ---------------------------------------------------------------------------
// Node lists
// ---------------------------------------------------------------------------

ZC_TEST("TreeBuilder.EmptyNodeListResolvesToEmptySlice") {
  TreeBuilder builder;

  zc::Vector<NodeId> empty;
  const NodeList list = builder.makeList(empty.asPtr());
  const NodeId root = builder.makeNode(SyntaxKind::SourceFile, source::SourceRange());
  builder.setRoot(root);
  Tree tree = builder.finish();

  ZC_EXPECT(list.size == 0);
  ZC_EXPECT(list.empty());
  ZC_EXPECT(tree.contains(list));
  const zc::ArrayPtr<const NodeId> resolved = tree.list(list);
  ZC_EXPECT(resolved.size() == 0);
}

ZC_TEST("TreeBuilder.MultipleListsAreStoredContiguously") {
  TreeBuilder builder;

  const NodeId a = builder.makeNode(SyntaxKind::IdentExpr, source::SourceRange());
  const NodeId b = builder.makeNode(SyntaxKind::IdentExpr, source::SourceRange());
  const NodeId c = builder.makeNode(SyntaxKind::IdentExpr, source::SourceRange());

  zc::Vector<NodeId> firstIds;
  firstIds.add(a);
  firstIds.add(b);
  const NodeList first = builder.makeList(firstIds.asPtr());

  zc::Vector<NodeId> secondIds;
  secondIds.add(c);
  const NodeList second = builder.makeList(secondIds.asPtr());

  const NodeId root = builder.makeNode(SyntaxKind::SourceFile, source::SourceRange());
  builder.setRoot(root);
  Tree tree = builder.finish();

  ZC_EXPECT(first.size == 2);
  ZC_EXPECT(second.size == 1);
  // List storage is contiguous: the second list starts right after the first.
  ZC_EXPECT(second.first == first.first + first.size);

  const zc::ArrayPtr<const NodeId> firstResolved = tree.list(first);
  ZC_EXPECT(firstResolved.size() == 2);
  ZC_EXPECT(firstResolved[0] == a);
  ZC_EXPECT(firstResolved[1] == b);

  const zc::ArrayPtr<const NodeId> secondResolved = tree.list(second);
  ZC_EXPECT(secondResolved.size() == 1);
  ZC_EXPECT(secondResolved[0] == c);
}

ZC_TEST("TreeBuilder.NodeListPreservesOrderAndDuplicates") {
  TreeBuilder builder;

  const NodeId child = builder.makeNode(SyntaxKind::IdentExpr, source::SourceRange());
  zc::Vector<NodeId> ids;
  ids.add(child);
  ids.add(child);
  ids.add(child);
  const NodeList list = builder.makeList(ids.asPtr());
  const NodeId root = builder.makeNode(SyntaxKind::SourceFile, source::SourceRange());
  builder.setRoot(root);
  Tree tree = builder.finish();

  const zc::ArrayPtr<const NodeId> resolved = tree.list(list);
  ZC_EXPECT(resolved.size() == 3);
  ZC_EXPECT(resolved[0] == child);
  ZC_EXPECT(resolved[1] == child);
  ZC_EXPECT(resolved[2] == child);
}

ZC_TEST("TreeBuilder.IdentListRoundTrip") {
  TreeBuilder builder;

  const IdentId x = builder.internIdent("x"_zc);
  const IdentId y = builder.internIdent("y"_zc);
  const IdentId z = builder.internIdent("z"_zc);
  zc::Vector<IdentId> ids;
  ids.add(x);
  ids.add(y);
  ids.add(z);
  const IdentList list = builder.makeIdentList(ids.asPtr());
  const NodeId root = builder.makeNode(SyntaxKind::SourceFile, source::SourceRange());
  builder.setRoot(root);
  Tree tree = builder.finish();

  const zc::ArrayPtr<const IdentId> resolved = tree.identList(list);
  ZC_EXPECT(resolved.size() == 3);
  ZC_EXPECT(resolved[0] == x);
  ZC_EXPECT(resolved[1] == y);
  ZC_EXPECT(resolved[2] == z);
}

ZC_TEST("TreeBuilder.EmptyIdentListResolvesToEmptySlice") {
  TreeBuilder builder;

  zc::Vector<IdentId> empty;
  const IdentList list = builder.makeIdentList(empty.asPtr());
  const NodeId root = builder.makeNode(SyntaxKind::SourceFile, source::SourceRange());
  builder.setRoot(root);
  Tree tree = builder.finish();

  ZC_EXPECT(list.empty());
  ZC_EXPECT(tree.identList(list).size() == 0);
}

// ---------------------------------------------------------------------------
// Interning
// ---------------------------------------------------------------------------

ZC_TEST("TreeBuilder.StringInterningDeduplicates") {
  TreeBuilder builder;

  const StringId a = builder.internString("main.zom"_zc);
  const StringId b = builder.internString("main.zom"_zc);
  const StringId c = builder.internString("other.zom"_zc);

  ZC_EXPECT(a == b);
  ZC_EXPECT(a != c);

  const NodeId root = builder.makeNode(SyntaxKind::SourceFile, source::SourceRange());
  builder.setRoot(root);
  Tree tree = builder.finish();

  ZC_EXPECT(tree.string(a) == "main.zom");
  ZC_EXPECT(tree.string(c) == "other.zom");
}

ZC_TEST("TreeBuilder.IdentInterningDeduplicates") {
  TreeBuilder builder;

  const IdentId a = builder.internIdent("value"_zc);
  const IdentId b = builder.internIdent("value"_zc);
  const IdentId c = builder.internIdent("other"_zc);

  ZC_EXPECT(a == b);
  ZC_EXPECT(a != c);

  const NodeId root = builder.makeNode(SyntaxKind::SourceFile, source::SourceRange());
  builder.setRoot(root);
  Tree tree = builder.finish();

  ZC_EXPECT(tree.ident(a) == "value");
  ZC_EXPECT(tree.ident(c) == "other");
}

ZC_TEST("TreeBuilder.LiteralInterningDeduplicates") {
  TreeBuilder builder;

  const BigIntId i1 = builder.internBigInt("42"_zc);
  const BigIntId i2 = builder.internBigInt("42"_zc);
  const BigIntId i3 = builder.internBigInt("7"_zc);
  const FloatId f1 = builder.internFloat("3.14"_zc);
  const FloatId f2 = builder.internFloat("3.14"_zc);
  const FloatId f3 = builder.internFloat("2.71"_zc);

  ZC_EXPECT(i1 == i2);
  ZC_EXPECT(i1 != i3);
  ZC_EXPECT(f1 == f2);
  ZC_EXPECT(f1 != f3);

  const NodeId root = builder.makeNode(SyntaxKind::SourceFile, source::SourceRange());
  builder.setRoot(root);
  Tree tree = builder.finish();

  ZC_EXPECT(tree.bigInt(i1) == "42");
  ZC_EXPECT(tree.bigInt(i3) == "7");
  ZC_EXPECT(tree.floatLiteral(f1) == "3.14");
  ZC_EXPECT(tree.floatLiteral(f3) == "2.71");
}

ZC_TEST("TreeBuilder.EmptyIdentInternsToEmptyId") {
  TreeBuilder builder;

  const IdentId emptyIdent = builder.internIdent(zc::StringPtr());
  const BigIntId emptyInt = builder.internBigInt(zc::StringPtr());
  const FloatId emptyFloat = builder.internFloat(zc::StringPtr());

  ZC_EXPECT(!emptyIdent);
  ZC_EXPECT(!emptyInt);
  ZC_EXPECT(!emptyFloat);

  const NodeId root = builder.makeNode(SyntaxKind::SourceFile, source::SourceRange());
  builder.setRoot(root);
  Tree tree = builder.finish();

  ZC_EXPECT(!tree.contains(emptyIdent));
  ZC_EXPECT(!tree.contains(emptyInt));
  ZC_EXPECT(!tree.contains(emptyFloat));
}

ZC_TEST("Tree.ContainsInternedIds") {
  TreeBuilder builder;

  const StringId s = builder.internString("a.zom"_zc);
  const IdentId i = builder.internIdent("a"_zc);
  const BigIntId bi = builder.internBigInt("1"_zc);
  const FloatId f = builder.internFloat("1.0"_zc);
  const NodeId root = builder.makeNode(SyntaxKind::SourceFile, source::SourceRange());
  builder.setRoot(root);
  Tree tree = builder.finish();

  ZC_EXPECT(tree.contains(s));
  ZC_EXPECT(tree.contains(i));
  ZC_EXPECT(tree.contains(bi));
  ZC_EXPECT(tree.contains(f));

  // Out-of-range ids are rejected.
  ZC_EXPECT(!tree.contains(StringId(999)));
  ZC_EXPECT(!tree.contains(IdentId(999)));
  ZC_EXPECT(!tree.contains(BigIntId(999)));
  ZC_EXPECT(!tree.contains(FloatId(999)));

  // Empty ids are never contained.
  ZC_EXPECT(!tree.contains(StringId()));
  ZC_EXPECT(!tree.contains(IdentId()));
}

// ---------------------------------------------------------------------------
// Move semantics
// ---------------------------------------------------------------------------

ZC_TEST("Tree.MoveConstructionTransfersOwnership") {
  TreeBuilder builder;

  const NodeId root = builder.makeNode(SyntaxKind::NullLiteral, source::SourceRange());
  builder.setRoot(root);
  Tree original = builder.finish();
  Tree moved(zc::mv(original));

  ZC_EXPECT(moved.nodeCount() == 1);
  ZC_EXPECT(moved.root() == root);
  ZC_EXPECT(moved.node(root).kind == SyntaxKind::NullLiteral);
}

ZC_TEST("TreeBuilder.MoveConstructionTransfersTree") {
  TreeBuilder builder;

  const NodeId root = builder.makeNode(SyntaxKind::BoolLiteral, source::SourceRange());
  builder.setRoot(root);
  TreeBuilder moved(zc::mv(builder));
  Tree tree = moved.finish();

  ZC_EXPECT(tree.nodeCount() == 1);
  ZC_EXPECT(tree.root() == root);
  ZC_EXPECT(tree.node(root).kind == SyntaxKind::BoolLiteral);
}

// ---------------------------------------------------------------------------
// Traversal
// ---------------------------------------------------------------------------

ZC_TEST("TreeTraversal.FindNodesByKind") {
  TreeBuilder builder;

  const NodeId a = builder.makeNode(SyntaxKind::IdentExpr, source::SourceRange());
  const NodeId b = builder.makeNode(SyntaxKind::IntLiteral, source::SourceRange());
  const NodeId c = builder.makeNode(SyntaxKind::IdentExpr, source::SourceRange());
  const NodeId d = builder.makeNode(SyntaxKind::BoolLiteral, source::SourceRange());
  builder.setRoot(a);
  Tree tree = builder.finish();

  uint32_t identCount = 0;
  uint32_t literalCount = 0;
  for (const Node& node : tree.nodes()) {
    if (node.kind == SyntaxKind::IdentExpr) { ++identCount; }
    if (node.kind == SyntaxKind::IntLiteral || node.kind == SyntaxKind::BoolLiteral) {
      ++literalCount;
    }
  }
  ZC_EXPECT(identCount == 2);
  ZC_EXPECT(literalCount == 2);

  // Every allocated id resolves to a node reachable through nodes().
  ZC_EXPECT(tree.contains(a));
  ZC_EXPECT(tree.contains(b));
  ZC_EXPECT(tree.contains(c));
  ZC_EXPECT(tree.contains(d));
}

ZC_TEST("TreeTraversal.ParentChildChainViaPayload") {
  // Build a leaf-first chain of 5 nodes; each parent stores its child link in
  // payload word 0.  This is a storage-level test, not a schema-valid tree.
  TreeBuilder builder;

  zc::Vector<NodeId> chain;
  chain.add(builder.makeNode(SyntaxKind::ThisExpr, source::SourceRange()));
  for (uint32_t depth = 1; depth < 5; ++depth) {
    NodePayload payload;
    payload.words[0] = chain[depth - 1].value;
    chain.add(builder.makeNode(SyntaxKind::UnaryExpression, source::SourceRange(), payload));
  }
  builder.setRoot(chain[4]);
  Tree tree = builder.finish();

  // Walk from the root following the child link until the empty id.
  uint32_t steps = 0;
  NodeId current = tree.root();
  while (current) {
    ++steps;
    current = NodeId(tree.node(current).payload.words[0]);
  }
  ZC_EXPECT(steps == 5);
}

ZC_TEST("TreeTraversal.SiblingNodesInList") {
  TreeBuilder builder;

  const NodeId s1 = builder.makeNode(SyntaxKind::IdentExpr, source::SourceRange());
  const NodeId s2 = builder.makeNode(SyntaxKind::IntLiteral, source::SourceRange());
  const NodeId s3 = builder.makeNode(SyntaxKind::BoolLiteral, source::SourceRange());
  zc::Vector<NodeId> siblings;
  siblings.add(s1);
  siblings.add(s2);
  siblings.add(s3);
  const NodeList list = builder.makeList(siblings.asPtr());
  const NodeId root = builder.makeNode(SyntaxKind::SourceFile, source::SourceRange());
  builder.setRoot(root);
  Tree tree = builder.finish();

  const zc::ArrayPtr<const NodeId> resolved = tree.list(list);
  ZC_EXPECT(resolved.size() == 3);
  ZC_EXPECT(tree.node(resolved[0]).kind == SyntaxKind::IdentExpr);
  ZC_EXPECT(tree.node(resolved[1]).kind == SyntaxKind::IntLiteral);
  ZC_EXPECT(tree.node(resolved[2]).kind == SyntaxKind::BoolLiteral);
}

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
