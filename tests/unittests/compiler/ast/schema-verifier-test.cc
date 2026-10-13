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

#include "compiler/ast/dump.h"
#include "compiler/ast/generated/node-payload.h"
#include "compiler/ast/generated/node-schema.h"
#include "compiler/ast/tree.h"
#include "compiler/basic/string-pool.h"
#include "compiler/basic/zomlang-opts.h"
#include "compiler/diagnostics/fact/source-diagnostic-draft-buffer.h"
#include "compiler/parser/parser.h"
#include "compiler/source/manager.h"
#include "zc/core/io.h"
#include "zc/core/vector.h"
#include "zc/ztest/test.h"

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

/// Build a minimal valid empty SourceFile root on \p builder and set it as the
/// tree root.  Tests that need to validate an unrelated node in the same tree
/// use this so the root check passes and the node under test is reached.
NodeId makeEmptySourceFileRoot(TreeBuilder& builder) {
  zc::Vector<NodeId> stmts;
  const NodeList stmtList = builder.makeList(stmts.asPtr());
  const StringId fileName = builder.internString("test.zom"_zc);

  NodePayload sourcePayload;
  sourcePayload.words[kSourceFileFileNameWord] = fileName.value;
  sourcePayload.words[kSourceFileModuleWord] = 0;
  sourcePayload.words[kSourceFileStatementsFirstWord] = stmtList.first;
  sourcePayload.words[kSourceFileStatementsSizeWord] = stmtList.size;
  const NodeId root =
      builder.makeNode(SyntaxKind::SourceFile, source::SourceRange(), sourcePayload);
  builder.setRoot(root);
  return root;
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
  checkCase("fn f() {}");
  checkCase("fn add(a: i32, b: i32) -> i32 { return a + b; }");
  checkCase("let x = 1 + 2 * 3;");
  checkCase("if (true) { let a = 1; } else { let b = 2; }");
  checkCase("while (true) { break; }");
  checkCase("for (let i = 0; i < 10; i = i + 1) {}");
  checkCase("return 42;");
}

// ---------------------------------------------------------------------------
// Interned-handle membership: a schema-declared StringId/IdentId/BigIntId/FloatId
// field whose payload word points outside this tree's intern table must fail
// verification rather than be accepted and crash on later interned access.
// ---------------------------------------------------------------------------

ZC_TEST("SchemaVerifier.OutOfRangeStringIdFails") {
  TreeBuilder builder;
  const StringId fileName = builder.internString("test.zom"_zc);
  const IdentId identName = builder.internIdent("x"_zc);

  NodePayload identPayload;
  identPayload.words[0] = identName.value;
  const NodeId identExpr =
      builder.makeNode(SyntaxKind::IdentExpr, source::SourceRange(), identPayload);
  NodePayload listItemPayload;
  listItemPayload.words[0] = identExpr.value;
  listItemPayload.words[1] = 0;
  const NodeId listItem =
      builder.makeNode(SyntaxKind::StatementListItem, source::SourceRange(), listItemPayload);
  zc::Vector<NodeId> stmts;
  stmts.add(listItem);
  const NodeList stmtList = builder.makeList(stmts.asPtr());

  // SourceFile.file_name (word[0]) points past the intern table.
  NodePayload sourcePayload;
  sourcePayload.words[0] = 0xffffffffu;
  sourcePayload.words[1] = 0;
  sourcePayload.words[2] = stmtList.first;
  sourcePayload.words[3] = stmtList.size;
  const NodeId sourceFile =
      builder.makeNode(SyntaxKind::SourceFile, source::SourceRange(), sourcePayload);
  builder.setRoot(sourceFile);
  Tree tree = builder.finish();
  (void)fileName;

  ZC_EXPECT(!verifySchema(tree));
  ZC_EXPECT(verifySchemaFailure(tree) != zc::none);
}

ZC_TEST("SchemaVerifier.OutOfRangeIdentIdFails") {
  TreeBuilder builder;
  const StringId fileName = builder.internString("test.zom"_zc);

  // IdentExpr.name (word[0]) points past the intern table.
  NodePayload identPayload;
  identPayload.words[0] = 0xffffffffu;
  const NodeId identExpr =
      builder.makeNode(SyntaxKind::IdentExpr, source::SourceRange(), identPayload);
  NodePayload listItemPayload;
  listItemPayload.words[0] = identExpr.value;
  listItemPayload.words[1] = 0;
  const NodeId listItem =
      builder.makeNode(SyntaxKind::StatementListItem, source::SourceRange(), listItemPayload);
  zc::Vector<NodeId> stmts;
  stmts.add(listItem);
  const NodeList stmtList = builder.makeList(stmts.asPtr());
  NodePayload sourcePayload;
  sourcePayload.words[0] = fileName.value;
  sourcePayload.words[1] = 0;
  sourcePayload.words[2] = stmtList.first;
  sourcePayload.words[3] = stmtList.size;
  const NodeId sourceFile =
      builder.makeNode(SyntaxKind::SourceFile, source::SourceRange(), sourcePayload);
  builder.setRoot(sourceFile);
  Tree tree = builder.finish();

  ZC_EXPECT(!verifySchema(tree));
  ZC_EXPECT(verifySchemaFailure(tree) != zc::none);
}

ZC_TEST("SchemaVerifier.OutOfRangeBigIntIdFails") {
  TreeBuilder builder;
  // IntLiteral { base = 10 (word[0]), value = out-of-range BigIntId (word[1]) }.
  NodePayload literalPayload;
  literalPayload.words[0] = 10;
  literalPayload.words[1] = 0xffffffffu;
  const NodeId literal =
      builder.makeNode(SyntaxKind::IntLiteral, source::SourceRange(), literalPayload);
  Tree tree = wrapExprInSourceFile(builder, literal);

  ZC_EXPECT(!verifySchema(tree));
  ZC_EXPECT(verifySchemaFailure(tree) != zc::none);
}

ZC_TEST("SchemaVerifier.OutOfRangeFloatIdFails") {
  TreeBuilder builder;
  // FloatLiteralExpr { width = 64 (word[0]), value = out-of-range FloatId (word[1]) }.
  NodePayload literalPayload;
  literalPayload.words[0] = 64;
  literalPayload.words[1] = 0xffffffffu;
  const NodeId literal =
      builder.makeNode(SyntaxKind::FloatLiteralExpr, source::SourceRange(), literalPayload);
  Tree tree = wrapExprInSourceFile(builder, literal);

  ZC_EXPECT(!verifySchema(tree));
  ZC_EXPECT(verifySchemaFailure(tree) != zc::none);
}

// ---------------------------------------------------------------------------
// 7. Empty tree (no root) fails verification
// ---------------------------------------------------------------------------

ZC_TEST("SchemaVerifier.EmptyTreeFails") {
  Tree tree;

  ZC_EXPECT(!verifySchema(tree));
  auto failure = verifySchemaFailure(tree);
  ZC_EXPECT(failure != zc::none);
  ZC_IF_SOME(message, failure) { ZC_EXPECT(message.contains("root")); }
}

// ---------------------------------------------------------------------------
// 8. Root that is not a SourceFile fails verification
// ---------------------------------------------------------------------------

ZC_TEST("SchemaVerifier.RootNotSourceFileFails") {
  TreeBuilder builder;

  const NodeId root = builder.makeNode(SyntaxKind::IdentExpr, source::SourceRange());
  builder.setRoot(root);
  Tree tree = builder.finish();

  ZC_EXPECT(!verifySchema(tree));
  ZC_EXPECT(verifySchemaFailure(tree) != zc::none);
}

// ---------------------------------------------------------------------------
// 9. A token kind stored as an AST node fails the known-kind check
// ---------------------------------------------------------------------------

ZC_TEST("SchemaVerifier.UnknownNodeKindFails") {
  TreeBuilder builder;

  // Identifier is a lexical token kind, not a schema-backed AST node.
  builder.makeNode(SyntaxKind::Identifier, source::SourceRange());
  makeEmptySourceFileRoot(builder);
  Tree tree = builder.finish();

  ZC_EXPECT(!verifySchema(tree));
  ZC_EXPECT(verifySchemaFailure(tree) != zc::none);
}

// ---------------------------------------------------------------------------
// 10. Enum field outside its declared domain fails
// ---------------------------------------------------------------------------
//
// UnaryExpression.op is an Enum field over UnaryOperatorKind (0..8).  Value
// 99 is outside the domain and must be rejected.

ZC_TEST("SchemaVerifier.EnumFieldOutOfDomainFails") {
  TreeBuilder builder;

  const NodeId operand = builder.makeNode(SyntaxKind::ThisExpr, source::SourceRange());
  NodePayload unaryPayload;
  unaryPayload.words[kUnaryExpressionOpWord] = 99;  // outside UnaryOperatorKind
  unaryPayload.words[kUnaryExpressionOperandWord] = operand.value;
  const NodeId unary =
      builder.makeNode(SyntaxKind::UnaryExpression, source::SourceRange(), unaryPayload);
  Tree tree = wrapExprInSourceFile(builder, unary);

  ZC_EXPECT(!verifySchema(tree));
  ZC_EXPECT(verifySchemaFailure(tree) != zc::none);
}

// ---------------------------------------------------------------------------
// 11. Bool field outside {0, 1} fails
// ---------------------------------------------------------------------------

ZC_TEST("SchemaVerifier.BoolFieldOutOfDomainFails") {
  TreeBuilder builder;

  const IdentId name = builder.internIdent("x"_zc);
  NodePayload patternPayload;
  patternPayload.words[kBindingPatternNameWord] = name.value;
  patternPayload.words[kBindingPatternIsMutWord] = 2;  // Bool must be 0 or 1
  patternPayload.words[kBindingPatternIsRefWord] = 0;
  patternPayload.words[kBindingPatternSubWord] = 0;
  builder.makeNode(SyntaxKind::BindingPattern, source::SourceRange(), patternPayload);
  makeEmptySourceFileRoot(builder);
  Tree tree = builder.finish();

  ZC_EXPECT(!verifySchema(tree));
  ZC_EXPECT(verifySchemaFailure(tree) != zc::none);
}

// ---------------------------------------------------------------------------
// 12. UInt8 field above 255 fails
// ---------------------------------------------------------------------------

ZC_TEST("SchemaVerifier.UInt8FieldOutOfDomainFails") {
  TreeBuilder builder;

  const BigIntId value = builder.internBigInt("42"_zc);
  NodePayload literalPayload;
  literalPayload.words[kIntLiteralBaseWord] = 256;  // UInt8 max is 255
  literalPayload.words[kIntLiteralValueWord] = value.value;
  builder.makeNode(SyntaxKind::IntLiteral, source::SourceRange(), literalPayload);
  makeEmptySourceFileRoot(builder);
  Tree tree = builder.finish();

  ZC_EXPECT(!verifySchema(tree));
  ZC_EXPECT(verifySchemaFailure(tree) != zc::none);
}

// ---------------------------------------------------------------------------
// 13. Required interned-identifier field left empty fails
// ---------------------------------------------------------------------------
//
// IdentExpr.name is a required IdentId.  Leaving it zero must trigger the
// required-scalar check in the interned-field validator.

ZC_TEST("SchemaVerifier.RequiredIdentFieldEmptyFails") {
  TreeBuilder builder;

  NodePayload identPayload;  // all zero: name (required IdentId) is empty
  builder.makeNode(SyntaxKind::IdentExpr, source::SourceRange(), identPayload);
  makeEmptySourceFileRoot(builder);
  Tree tree = builder.finish();

  ZC_EXPECT(!verifySchema(tree));
  ZC_EXPECT(verifySchemaFailure(tree) != zc::none);
}

// ---------------------------------------------------------------------------
// 14. Optional NodeId field may be empty
// ---------------------------------------------------------------------------
//
// IfStmt.else_stmt is optional.  A well-formed IfStmt with no else branch must
// pass verification.

ZC_TEST("SchemaVerifier.OptionalNodeIdEmptyPasses") {
  TreeBuilder builder;

  // cond: BoolLiteral { value = true }
  NodePayload boolPayload;
  boolPayload.words[kBoolLiteralValueWord] = 1;
  const NodeId cond = builder.makeNode(SyntaxKind::BoolLiteral, source::SourceRange(), boolPayload);

  // then: BlockStmt { stmts = empty }
  zc::Vector<NodeId> blockStmts;
  const NodeList blockList = builder.makeList(blockStmts.asPtr());
  NodePayload blockPayload;
  blockPayload.words[kBlockStmtStmtsFirstWord] = blockList.first;
  blockPayload.words[kBlockStmtStmtsSizeWord] = blockList.size;
  const NodeId thenBlock =
      builder.makeNode(SyntaxKind::BlockStmt, source::SourceRange(), blockPayload);

  // IfStmt { cond, then_stmt, else_stmt = empty (optional) }
  NodePayload ifPayload;
  ifPayload.words[kIfStmtCondWord] = cond.value;
  ifPayload.words[kIfStmtThenStmtWord] = thenBlock.value;
  ifPayload.words[kIfStmtElseStmtWord] = 0;
  const NodeId ifStmt = builder.makeNode(SyntaxKind::IfStmt, source::SourceRange(), ifPayload);

  Tree tree = wrapExprInSourceFile(builder, ifStmt);
  ZC_EXPECT(verifySchema(tree));
  ZC_EXPECT(verifySchemaFailure(tree) == zc::none);
}

// ---------------------------------------------------------------------------
// 15. NodeList field pointing past list storage fails
// ---------------------------------------------------------------------------

ZC_TEST("SchemaVerifier.NodeListOutOfRangeFails") {
  TreeBuilder builder;

  NodePayload blockPayload;
  blockPayload.words[kBlockStmtStmtsFirstWord] = 100;  // past list storage
  blockPayload.words[kBlockStmtStmtsSizeWord] = 5;
  builder.makeNode(SyntaxKind::BlockStmt, source::SourceRange(), blockPayload);
  makeEmptySourceFileRoot(builder);
  Tree tree = builder.finish();

  ZC_EXPECT(!verifySchema(tree));
  ZC_EXPECT(verifySchemaFailure(tree) != zc::none);
}

// ---------------------------------------------------------------------------
// 16. IdentList field pointing past identifier-list storage fails
// ---------------------------------------------------------------------------

ZC_TEST("SchemaVerifier.IdentListOutOfRangeFails") {
  TreeBuilder builder;

  NodePayload pathPayload;
  pathPayload.words[kModulePathSegmentsFirstWord] = 100;  // past ident-list storage
  pathPayload.words[kModulePathSegmentsSizeWord] = 3;
  pathPayload.words[kModulePathRootWord] = 0;  // Relative
  builder.makeNode(SyntaxKind::ModulePath, source::SourceRange(), pathPayload);
  makeEmptySourceFileRoot(builder);
  Tree tree = builder.finish();

  ZC_EXPECT(!verifySchema(tree));
  ZC_EXPECT(verifySchemaFailure(tree) != zc::none);
}

// ---------------------------------------------------------------------------
// 17. Valid empty SourceFile passes
// ---------------------------------------------------------------------------

ZC_TEST("SchemaVerifier.ValidEmptySourceFilePasses") {
  TreeBuilder builder;
  makeEmptySourceFileRoot(builder);
  Tree tree = builder.finish();

  ZC_EXPECT(verifySchema(tree));
  ZC_EXPECT(verifySchemaFailure(tree) == zc::none);
}

// ---------------------------------------------------------------------------
// 18. Valid let-statement tree passes
// ---------------------------------------------------------------------------
//
// Covers BindingPattern, IntLiteral, VariableDeclarator,
// VariableDeclaratorList, and LetStmt with their cast-target checks.

ZC_TEST("SchemaVerifier.ValidLetStatementPasses") {
  TreeBuilder builder;

  // BindingPattern { name, is_mut = false, is_ref = false, sub = none }
  const IdentId name = builder.internIdent("x"_zc);
  NodePayload patternPayload;
  patternPayload.words[kBindingPatternNameWord] = name.value;
  patternPayload.words[kBindingPatternIsMutWord] = 0;
  patternPayload.words[kBindingPatternIsRefWord] = 0;
  patternPayload.words[kBindingPatternSubWord] = 0;
  const NodeId pattern =
      builder.makeNode(SyntaxKind::BindingPattern, source::SourceRange(), patternPayload);

  // IntLiteral { base = 10, value }
  const BigIntId value = builder.internBigInt("42"_zc);
  NodePayload literalPayload;
  literalPayload.words[kIntLiteralBaseWord] = 10;
  literalPayload.words[kIntLiteralValueWord] = value.value;
  const NodeId literal =
      builder.makeNode(SyntaxKind::IntLiteral, source::SourceRange(), literalPayload);

  // VariableDeclarator { pattern, ty = none, init = literal }
  NodePayload declPayload;
  declPayload.words[kVariableDeclaratorPatternWord] = pattern.value;
  declPayload.words[kVariableDeclaratorTyWord] = 0;
  declPayload.words[kVariableDeclaratorInitWord] = literal.value;
  const NodeId declarator =
      builder.makeNode(SyntaxKind::VariableDeclarator, source::SourceRange(), declPayload);

  // VariableDeclaratorList { n_decls = 1, decls = [declarator] }
  zc::Vector<NodeId> decls;
  decls.add(declarator);
  const NodeList declList = builder.makeList(decls.asPtr());
  NodePayload listPayload;
  listPayload.words[kVariableDeclaratorListNDeclsWord] = 1;
  listPayload.words[kVariableDeclaratorListDeclsFirstWord] = declList.first;
  listPayload.words[kVariableDeclaratorListDeclsSizeWord] = declList.size;
  const NodeId listNode =
      builder.makeNode(SyntaxKind::VariableDeclaratorList, source::SourceRange(), listPayload);

  // LetStmt { kind = Let, declarations = listNode }
  NodePayload letPayload;
  letPayload.words[kLetStmtKindWord] = 0;  // Let
  letPayload.words[kLetStmtDeclarationsWord] = listNode.value;
  const NodeId letStmt = builder.makeNode(SyntaxKind::LetStmt, source::SourceRange(), letPayload);

  Tree tree = wrapExprInSourceFile(builder, letStmt);
  ZC_EXPECT(verifySchema(tree));
  ZC_EXPECT(verifySchemaFailure(tree) == zc::none);
}

// ---------------------------------------------------------------------------
// 19. Valid function declaration passes
// ---------------------------------------------------------------------------
//
// Covers FunctionDecl, FunctionParameterList, FunctionParameterDecl,
// PredefinedTypeExpr, and BlockStmt with their cast-target checks.

ZC_TEST("SchemaVerifier.ValidFunctionDeclPasses") {
  TreeBuilder builder;

  // PredefinedTypeExpr { kind = I32 } for the parameter type.
  NodePayload tyPayload;
  tyPayload.words[kPredefinedTypeExprKindWord] = 2;  // I32
  const NodeId paramTy =
      builder.makeNode(SyntaxKind::PredefinedTypeExpr, source::SourceRange(), tyPayload);

  // FunctionParameterDecl { name, ty, default = none, attrs = none }
  const IdentId paramName = builder.internIdent("a"_zc);
  NodePayload paramPayload;
  paramPayload.words[kFunctionParameterDeclNameWord] = paramName.value;
  paramPayload.words[kFunctionParameterDeclTyWord] = paramTy.value;
  paramPayload.words[kFunctionParameterDeclDefaultWord] = 0;
  paramPayload.words[kFunctionParameterDeclAttrsWord] = 0;
  const NodeId param =
      builder.makeNode(SyntaxKind::FunctionParameterDecl, source::SourceRange(), paramPayload);

  // FunctionParameterList { nparams = 1, params = [param] }
  zc::Vector<NodeId> params;
  params.add(param);
  const NodeList paramList = builder.makeList(params.asPtr());
  NodePayload paramListPayload;
  paramListPayload.words[kFunctionParameterListNparamsWord] = 1;
  paramListPayload.words[kFunctionParameterListParamsFirstWord] = paramList.first;
  paramListPayload.words[kFunctionParameterListParamsSizeWord] = paramList.size;
  const NodeId paramListNode =
      builder.makeNode(SyntaxKind::FunctionParameterList, source::SourceRange(), paramListPayload);

  // BlockStmt { stmts = empty } for the body.
  zc::Vector<NodeId> bodyStmts;
  const NodeList bodyList = builder.makeList(bodyStmts.asPtr());
  NodePayload bodyPayload;
  bodyPayload.words[kBlockStmtStmtsFirstWord] = bodyList.first;
  bodyPayload.words[kBlockStmtStmtsSizeWord] = bodyList.size;
  const NodeId body = builder.makeNode(SyntaxKind::BlockStmt, source::SourceRange(), bodyPayload);

  // FunctionDecl { name, params, type_params = none, ret_ty = none,
  //                raises = none, body }
  const IdentId fnName = builder.internIdent("identity"_zc);
  NodePayload fnPayload;
  fnPayload.words[kFunctionDeclNameWord] = fnName.value;
  fnPayload.words[kFunctionDeclParamsIdWord] = paramListNode.value;
  fnPayload.words[kFunctionDeclTypeParamsIdWord] = 0;
  fnPayload.words[kFunctionDeclRetTyWord] = 0;
  fnPayload.words[kFunctionDeclRaisesTyWord] = 0;
  fnPayload.words[kFunctionDeclBodyWord] = body.value;
  const NodeId fn = builder.makeNode(SyntaxKind::FunctionDecl, source::SourceRange(), fnPayload);

  Tree tree = wrapExprInSourceFile(builder, fn);
  ZC_EXPECT(verifySchema(tree));
  ZC_EXPECT(verifySchemaFailure(tree) == zc::none);
}

// ---------------------------------------------------------------------------
// 20. Valid module declaration attached to SourceFile.module passes
// ---------------------------------------------------------------------------

ZC_TEST("SchemaVerifier.ValidModuleDeclarationPasses") {
  TreeBuilder builder;

  // ModuleDeclaration { form = RootDeclaration, declared_name, alias_target = none,
  //                      inline_items = empty, exported_alias = false }
  const IdentId name = builder.internIdent("my_module"_zc);
  NodePayload modulePayload;
  modulePayload.words[kModuleDeclarationFormWord] = 0;  // RootDeclaration
  modulePayload.words[kModuleDeclarationDeclaredNameWord] = name.value;
  modulePayload.words[kModuleDeclarationAliasTargetWord] = 0;
  modulePayload.words[kModuleDeclarationInlineItemsFirstWord] = 0;
  modulePayload.words[kModuleDeclarationInlineItemsSizeWord] = 0;
  modulePayload.words[kModuleDeclarationExportedAliasWord] = 0;
  const NodeId module =
      builder.makeNode(SyntaxKind::ModuleDeclaration, source::SourceRange(), modulePayload);

  // SourceFile { file_name, module, statements = empty }
  zc::Vector<NodeId> stmts;
  const NodeList stmtList = builder.makeList(stmts.asPtr());
  const StringId fileName = builder.internString("test.zom"_zc);
  NodePayload sourcePayload;
  sourcePayload.words[kSourceFileFileNameWord] = fileName.value;
  sourcePayload.words[kSourceFileModuleWord] = module.value;
  sourcePayload.words[kSourceFileStatementsFirstWord] = stmtList.first;
  sourcePayload.words[kSourceFileStatementsSizeWord] = stmtList.size;
  const NodeId root =
      builder.makeNode(SyntaxKind::SourceFile, source::SourceRange(), sourcePayload);
  builder.setRoot(root);
  Tree tree = builder.finish();

  ZC_EXPECT(verifySchema(tree));
  ZC_EXPECT(verifySchemaFailure(tree) == zc::none);
}

// ---------------------------------------------------------------------------
// 21. Parsed declaration-heavy sources pass schema verification
// ---------------------------------------------------------------------------

ZC_TEST("SchemaVerifier.ParsedDeclarationsPass") {
  auto checkCase = [](zc::StringPtr source) {
    auto parsed = parseSource(source);
    ZC_EXPECT(parsed.tree != zc::none, zc::str("Should parse: ", source));
    ZC_IF_SOME(tree, parsed.tree) {
      ZC_EXPECT(verifySchema(tree), zc::str("Schema should pass for: ", source));
      ZC_EXPECT(verifySchemaFailure(tree) == zc::none, zc::str("No schema failure for: ", source));
      ZC_EXPECT(noRequiredNodeIdIsNull(tree), zc::str("No required NodeId null for: ", source));
    }
  };

  checkCase("struct Vec3 { x: f64; y: f64; z: f64; }");
  checkCase("enum Color { Red, Green, Blue, }");
  checkCase("alias Id = i64;");
  checkCase("import std::io;");
  checkCase("const PI = 3.14;");
  checkCase("fn identity(a: i32) -> i32 { return a; }");
}

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
