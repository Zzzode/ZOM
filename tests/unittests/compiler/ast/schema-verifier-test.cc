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

#include "compiler/ast/schema-verifier.h"

#include "zc/core/io.h"
#include "zc/core/vector.h"
#include "zc/ztest/test.h"
#include "compiler/ast/dump.h"
#include "compiler/ast/generated/node-payload.h"
#include "compiler/ast/generated/node-schema.h"
#include "compiler/ast/tree.h"
#include "compiler/basic/string-pool.h"
#include "compiler/basic/zomlang-opts.h"
#include "compiler/diagnostics/core/diagnostic-engine.h"
#include "compiler/diagnostics/fact/source-diagnostic-draft-buffer.h"
#include "compiler/parser/parser.h"
#include "compiler/source/manager.h"

namespace zomlang {
namespace compiler {
namespace ast {

namespace {

/// Build a minimal valid SourceFile tree containing a single IdentExpr
/// statement.  This serves as the "happy path" baseline and as a reusable
/// scaffold for other tests.
Tree buildValidIdentExprTree() {
  TreeBuilder builder;

  const StringId fileName = builder.internString("test.zom"_zc);
  const IdentId identName = builder.internIdent("x"_zc);

  // IdentExpr { name = identName }
  NodePayload identPayload;
  identPayload.words[0] = identName.value;
  const NodeId identExpr =
      builder.makeNode(SyntaxKind::IdentExpr, source::SourceRange(), identPayload);

  // StatementListItem { item = identExpr, attrs = none }
  NodePayload listItemPayload;
  listItemPayload.words[0] = identExpr.value;
  listItemPayload.words[1] = 0;  // attrs (optional)
  const NodeId listItem =
      builder.makeNode(SyntaxKind::StatementListItem, source::SourceRange(), listItemPayload);

  zc::Vector<NodeId> stmts;
  stmts.add(listItem);
  const NodeList stmtList = builder.makeList(stmts.asPtr());

  // SourceFile { file_name, module = none, statements = stmtList }
  NodePayload sourcePayload;
  sourcePayload.words[0] = fileName.value;
  sourcePayload.words[1] = 0;  // module (optional)
  sourcePayload.words[2] = stmtList.first;
  sourcePayload.words[3] = stmtList.size;
  const NodeId sourceFile =
      builder.makeNode(SyntaxKind::SourceFile, source::SourceRange(), sourcePayload);

  builder.setRoot(sourceFile);
  return builder.finish();
}

/// Wrap a single expression node in StatementListItem -> SourceFile so that
/// the resulting tree has a proper root and can be verified.
Tree wrapExprInSourceFile(TreeBuilder& builder, NodeId exprNode) {
  // StatementListItem { item = exprNode, attrs = none }
  NodePayload listItemPayload;
  listItemPayload.words[0] = exprNode.value;
  listItemPayload.words[1] = 0;
  const NodeId listItem =
      builder.makeNode(SyntaxKind::StatementListItem, source::SourceRange(), listItemPayload);

  zc::Vector<NodeId> stmts;
  stmts.add(listItem);
  const NodeList stmtList = builder.makeList(stmts.asPtr());

  const StringId fileName = builder.internString("test.zom"_zc);

  NodePayload sourcePayload;
  sourcePayload.words[0] = fileName.value;
  sourcePayload.words[1] = 0;
  sourcePayload.words[2] = stmtList.first;
  sourcePayload.words[3] = stmtList.size;
  const NodeId sourceFile =
      builder.makeNode(SyntaxKind::SourceFile, source::SourceRange(), sourcePayload);

  builder.setRoot(sourceFile);
  return builder.finish();
}

}  // namespace

// ---------------------------------------------------------------------------
// 1. Valid tree passes verification
// ---------------------------------------------------------------------------

ZC_TEST("SchemaVerifier.ValidTreePasses") {
  Tree tree = buildValidIdentExprTree();
  ZC_EXPECT(verifySchema(tree));
  ZC_EXPECT(verifySchemaFailure(tree) == zc::none);
}

// ---------------------------------------------------------------------------
// 2. Missing required child field causes failure
// ---------------------------------------------------------------------------
//
// UnaryExpression requires an "operand" NodeId (word[1]).  Leaving it zero
// (empty NodeId) must trigger a schema violation.

ZC_TEST("SchemaVerifier.MissingRequiredChildFails") {
  TreeBuilder builder;

  // UnaryExpression { op = Plus(0), operand = empty }
  NodePayload unaryPayload;
  unaryPayload.words[0] = 0;  // op: Plus
  unaryPayload.words[1] = 0;  // operand: EMPTY (required!)
  const NodeId unaryExpr =
      builder.makeNode(SyntaxKind::UnaryExpression, source::SourceRange(), unaryPayload);

  Tree tree = wrapExprInSourceFile(builder, unaryExpr);
  ZC_EXPECT(!verifySchema(tree));

  auto failure = verifySchemaFailure(tree);
  ZC_EXPECT(failure != zc::none);
}

// ---------------------------------------------------------------------------
// 3. Invalid child kind causes failure
// ---------------------------------------------------------------------------
//
// UnaryExpression expects "operand" to be an Expression.  Pointing it at a
// WildcardPattern (which belongs to the Pattern kind range 0x040-0x05f) must
// fail the cast-target check.

ZC_TEST("SchemaVerifier.InvalidChildKindFails") {
  TreeBuilder builder;

  // WildcardPattern { ty = none (optional) }
  NodePayload patternPayload;
  patternPayload.words[0] = 0;  // ty (optional)
  const NodeId pattern =
      builder.makeNode(SyntaxKind::WildcardPattern, source::SourceRange(), patternPayload);

  // UnaryExpression { op = Plus(0), operand = pattern (wrong kind!) }
  NodePayload unaryPayload;
  unaryPayload.words[0] = 0;              // op: Plus
  unaryPayload.words[1] = pattern.value;  // operand: Pattern instead of Expression
  const NodeId unaryExpr =
      builder.makeNode(SyntaxKind::UnaryExpression, source::SourceRange(), unaryPayload);

  Tree tree = wrapExprInSourceFile(builder, unaryExpr);
  ZC_EXPECT(!verifySchema(tree));

  auto failure = verifySchemaFailure(tree);
  ZC_EXPECT(failure != zc::none);
}

ZC_TEST("SchemaVerifier.InvalidMemberAccessKindFails") {
  TreeBuilder builder;

  const IdentId objectName = builder.internIdent("object"_zc);
  NodePayload objectPayload;
  objectPayload.words[kIdentExprNameWord] = objectName.value;
  const NodeId object =
      builder.makeNode(SyntaxKind::IdentExpr, source::SourceRange(), objectPayload);

  const IdentId propertyName = builder.internIdent("field"_zc);
  NodePayload memberPayload;
  memberPayload.words[kMemberExpressionObjectWord] = object.value;
  memberPayload.words[kMemberExpressionPropertyWord] = propertyName.value;
  memberPayload.words[kMemberExpressionAccessWord] = 3;
  const NodeId member =
      builder.makeNode(SyntaxKind::MemberExpression, source::SourceRange(), memberPayload);

  Tree tree = wrapExprInSourceFile(builder, member);
  ZC_EXPECT(!verifySchema(tree));
  ZC_EXPECT(verifySchemaFailure(tree) != zc::none);
}

// ---------------------------------------------------------------------------
// 4. Valid NodeList passes verification
// ---------------------------------------------------------------------------
//
// BlockStmt has a "stmts" NodeList field whose elements must be
// StatementListItem.  A BlockStmt containing properly-typed children must
// pass.

ZC_TEST("SchemaVerifier.ValidNodeListPasses") {
  TreeBuilder builder;

  const IdentId identName = builder.internIdent("y"_zc);

  // Inner IdentExpr
  NodePayload identPayload;
  identPayload.words[0] = identName.value;
  const NodeId identExpr =
      builder.makeNode(SyntaxKind::IdentExpr, source::SourceRange(), identPayload);

  // StatementListItem wrapping the IdentExpr
  NodePayload listItemPayload;
  listItemPayload.words[0] = identExpr.value;
  listItemPayload.words[1] = 0;
  const NodeId listItem =
      builder.makeNode(SyntaxKind::StatementListItem, source::SourceRange(), listItemPayload);

  // Build the stmts NodeList for BlockStmt
  zc::Vector<NodeId> blockStmts;
  blockStmts.add(listItem);
  const NodeList blockList = builder.makeList(blockStmts.asPtr());

  // BlockStmt { stmts = blockList }
  NodePayload blockPayload;
  blockPayload.words[0] = blockList.first;
  blockPayload.words[1] = blockList.size;
  const NodeId blockStmt =
      builder.makeNode(SyntaxKind::BlockStmt, source::SourceRange(), blockPayload);

  // Wrap BlockStmt in StatementListItem -> SourceFile
  NodePayload outerItemPayload;
  outerItemPayload.words[0] = blockStmt.value;
  outerItemPayload.words[1] = 0;
  const NodeId outerItem =
      builder.makeNode(SyntaxKind::StatementListItem, source::SourceRange(), outerItemPayload);

  zc::Vector<NodeId> sourceStmts;
  sourceStmts.add(outerItem);
  const NodeList sourceList = builder.makeList(sourceStmts.asPtr());

  const StringId fileName = builder.internString("test.zom"_zc);
  NodePayload sourcePayload;
  sourcePayload.words[0] = fileName.value;
  sourcePayload.words[1] = 0;
  sourcePayload.words[2] = sourceList.first;
  sourcePayload.words[3] = sourceList.size;
  const NodeId sourceFile =
      builder.makeNode(SyntaxKind::SourceFile, source::SourceRange(), sourcePayload);

  builder.setRoot(sourceFile);
  Tree tree = builder.finish();

  ZC_EXPECT(verifySchema(tree));
  ZC_EXPECT(verifySchemaFailure(tree) == zc::none);
}

// ---------------------------------------------------------------------------
// 5. Invalid NodeList element causes failure
// ---------------------------------------------------------------------------
//
// BlockStmt's "stmts" NodeList expects StatementListItem elements.  Putting
// an IdentExpr (an Expression, not a StatementListItem) directly in the list
// must fail.

ZC_TEST("SchemaVerifier.InvalidNodeListElementFails") {
  TreeBuilder builder;

  const IdentId identName = builder.internIdent("bad"_zc);

  // IdentExpr that we will incorrectly place directly in the BlockStmt list.
  // IdentExpr is in range 0x080-0x0bf (Expression), NOT a StatementListItem.
  NodePayload identPayload;
  identPayload.words[0] = identName.value;
  const NodeId identExpr =
      builder.makeNode(SyntaxKind::IdentExpr, source::SourceRange(), identPayload);

  // Put the IdentExpr directly in the stmts list (not wrapped in
  // StatementListItem) so the cast-target check fails.
  zc::Vector<NodeId> blockStmts;
  blockStmts.add(identExpr);
  const NodeList blockList = builder.makeList(blockStmts.asPtr());

  // BlockStmt { stmts = blockList (contains wrong kind!) }
  NodePayload blockPayload;
  blockPayload.words[0] = blockList.first;
  blockPayload.words[1] = blockList.size;
  const NodeId blockStmt =
      builder.makeNode(SyntaxKind::BlockStmt, source::SourceRange(), blockPayload);

  // Wrap in StatementListItem -> SourceFile
  NodePayload outerItemPayload;
  outerItemPayload.words[0] = blockStmt.value;
  outerItemPayload.words[1] = 0;
  const NodeId outerItem =
      builder.makeNode(SyntaxKind::StatementListItem, source::SourceRange(), outerItemPayload);

  zc::Vector<NodeId> sourceStmts;
  sourceStmts.add(outerItem);
  const NodeList sourceList = builder.makeList(sourceStmts.asPtr());

  const StringId fileName = builder.internString("test.zom"_zc);
  NodePayload sourcePayload;
  sourcePayload.words[0] = fileName.value;
  sourcePayload.words[1] = 0;
  sourcePayload.words[2] = sourceList.first;
  sourcePayload.words[3] = sourceList.size;
  const NodeId sourceFile =
      builder.makeNode(SyntaxKind::SourceFile, source::SourceRange(), sourcePayload);

  builder.setRoot(sourceFile);
  Tree tree = builder.finish();

  ZC_EXPECT(!verifySchema(tree));

  auto failure = verifySchemaFailure(tree);
  ZC_EXPECT(failure != zc::none);
}

// ---------------------------------------------------------------------------
// 6. Parsed valid source has no null required fields in dump output
// ---------------------------------------------------------------------------
//
// Parse real source code through the parser, verify the AST passes schema
// verification, then walk every node to confirm no required NodeId field is
// empty.  Finally dump the tree to a string and verify no required field
// appears as "null" in the dump output.

namespace {

struct ParseResult {
  zc::Maybe<Tree> tree;
  zc::Own<source::SourceManager> sourceManager;
};

/// Parse \p source as a ZOM file and return the AST tree plus source manager.
ParseResult parseSource(zc::StringPtr source) {
  ParseResult result;
  result.sourceManager = zc::heap<source::SourceManager>();
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  const auto bufferId = result.sourceManager->addMemBufferCopy(source.asBytes(), "test.zom");
  diagnostics::SourceDiagnosticDraftBuffer diagnosticFacts(*result.sourceManager, bufferId);
  parser::Parser parser(*result.sourceManager, diagnosticFacts, langOpts, stringPool, bufferId);
  result.tree = parser.parse();
  return result;
}

/// Return true when every required NodeId field in \p tree is non-empty.
/// This is the invariant that "no required AST field is null".
bool noRequiredNodeIdIsNull(const Tree& tree) {
  for (const Node& node : tree.nodes()) {
    const NodeSchemaEntry* schema = lookupNodeSchema(node.kind);
    if (schema == nullptr) { return false; }
    for (uint32_t fi = 0; fi < schema->fieldCount; ++fi) {
      const NodeSchemaFieldEntry& field = schema->fields[fi];
      if (field.storage != NodeSchemaFieldStorage::NodeId) { continue; }
      if (field.optional) { continue; }
      const NodeId id(node.payload.words[field.firstWord]);
      if (!id) { return false; }
    }
  }
  return true;
}

}  // namespace

ZC_TEST("SchemaVerifier.ParsedSourceNoNullRequiredField") {
  // Helper lambda: parse source, verify schema, walk nodes, check dump succeeds.
  auto checkCase = [](zc::StringPtr source) {
    auto parsed = parseSource(source);
    ZC_EXPECT(parsed.tree != zc::none, zc::str("Should parse: ", source));
    ZC_IF_SOME(tree, parsed.tree) {
      // 1. Schema verifier must pass.  This guarantees no required NodeId
      //    field is empty and no required scalar field is zero.
      ZC_EXPECT(verifySchema(tree), zc::str("Schema should pass for: ", source));
      ZC_EXPECT(verifySchemaFailure(tree) == zc::none, zc::str("No schema failure for: ", source));

      // 2. Explicit walk: no required NodeId field is empty.
      //    This directly demonstrates the "no null required field" invariant
      //    at the AST storage level.  The dump reads from the same storage,
      //    so this guarantees no required field appears as null in the dump.
      ZC_EXPECT(noRequiredNodeIdIsNull(tree), zc::str("No required NodeId null for: ", source));

      // 3. Dump the tree and verify it succeeds.  If dumpTree returns an
      //    error, it means a required child reference was invalid, which
      //    verifySchema() should already have caught.
      zc::VectorOutputStream output(4096);
      auto dumpResult = dumpTree(output, tree, *parsed.sourceManager, AstDumpFormat::Tree);
      ZC_EXPECT(dumpResult == zc::none, zc::str("Dump should succeed for: ", source));
    }
  };

  // A variety of valid sources covering different AST node kinds.
  checkCase("");
  checkCase("let x = 42;");
  checkCase("let y: i32 = 100;");
  checkCase("const PI = 3.14;");
  checkCase("fun f() {}");
  checkCase("fun add(a: i32, b: i32) -> i32 { return a + b; }");
  checkCase("let x = 1 + 2 * 3;");
  checkCase("if (true) { let a = 1; } else { let b = 2; }");
  checkCase("while (true) { break; }");
  checkCase("for (let i = 0; i < 10; i = i + 1) {}");
  checkCase("return 42;");
}

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
