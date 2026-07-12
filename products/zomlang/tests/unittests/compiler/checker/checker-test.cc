// Copyright (c) 2025 Zode.Z. All rights reserved
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
// See the License for the specific language governing permissions and limitations under
// the License.

#include "zomlang/compiler/checker/checker.h"

#include "zc/core/vector.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/binder/binder.h"
#include "zomlang/compiler/binder/definition-identity-map.h"
#include "zomlang/compiler/diagnostics/diagnostic-consumer.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/diagnostics/diagnostic.h"
#include "zomlang/compiler/type/function-type.h"
#include "zomlang/compiler/type/primitive-type.h"
#include "zomlang/compiler/type/type-env.h"
#include "zomlang/compiler/type/type.h"
#include "zomlang/tests/unittests/compiler/test-ast-builder.h"
#include "zomlang/tests/unittests/compiler/test-semantic-identities.h"
#include "zomlang/tests/unittests/compiler/test-semantic-type-context.h"

namespace zomlang {
namespace compiler {
namespace checker {

using tests::TestFixture;

namespace {

class CapturingDiagnosticConsumer final : public diagnostics::DiagnosticConsumer {
public:
  zc::Vector<diagnostics::DiagID> ids;

  void handleDiagnostic(const source::SourceManager&,
                        const diagnostics::Diagnostic& diagnostic) override {
    ids.add(diagnostic.getId());
    for (const auto& child : diagnostic.getChildDiagnostics()) { ids.add(child->getId()); }
  }
};

bool containsDiagnosticId(const CapturingDiagnosticConsumer& consumer, diagnostics::DiagID id) {
  for (auto emitted : consumer.ids) {
    if (emitted == id) return true;
  }
  return false;
}

struct CheckerRunResult {
  bool bindSuccess;
  bool checkSuccess;
  tests::TestTypeEnv typeEnv;
};

bool sameNode(const ast::Node& lhs, const ast::Node& rhs) {
  if (lhs.kind != rhs.kind) return false;
  if (lhs.range.getStart() != rhs.range.getStart()) return false;
  if (lhs.range.getEnd() != rhs.range.getEnd()) return false;
  for (size_t i = 0; i < ast::kNodePayloadWordCount; ++i) {
    if (lhs.payload.words[i] != rhs.payload.words[i]) return false;
  }
  return true;
}

struct MetadataSnapshotEntry {
  ast::NodeId parent;
  uint32_t scope = 0;
  identity::DefId definition;
  bool unresolved = false;
  bool deferredMember = false;
  ast::NodeId shadowOf;
  bool reexport = false;
  ast::NodeList captures;
  ast::NodeId labelTarget;
};

MetadataSnapshotEntry snapshotMetadata(const ast::BindingMetadata& metadata, ast::NodeId node) {
  return MetadataSnapshotEntry{metadata.parent(node),           metadata.scope(node),
                               metadata.definition(node),       metadata.isUnresolved(node),
                               metadata.isDeferredMember(node), metadata.shadowOf(node),
                               metadata.isReexport(node),       metadata.captures(node),
                               metadata.labelTarget(node)};
}

bool sameMetadata(const MetadataSnapshotEntry& lhs, const MetadataSnapshotEntry& rhs) {
  return lhs.parent == rhs.parent && lhs.scope == rhs.scope && lhs.definition == rhs.definition &&
         lhs.unresolved == rhs.unresolved && lhs.deferredMember == rhs.deferredMember &&
         lhs.shadowOf == rhs.shadowOf && lhs.reexport == rhs.reexport &&
         lhs.captures.first == rhs.captures.first && lhs.captures.size == rhs.captures.size &&
         lhs.labelTarget == rhs.labelTarget;
}

CheckerRunResult runChecker(TestFixture& fix, zc::ArrayPtr<const ast::NodeId> decls) {
  auto tree = fix.buildSourceFile("test"_zc, decls);

  auto identities = tests::makeTestDefinitionIdentityMap(tree);
  binder::Binder binder(fix.symbols(), fix.diagnostics(), tree, identities, fix.metadata());
  bool bindSuccess = binder.bind();

  tests::TestTypeEnv typeEnv;
  bool checkSuccess = false;
  if (bindSuccess) {
    Checker checker(fix.symbols(), fix.diagnostics(), tree, fix.metadata(), typeEnv);
    checkSuccess = checker.check();
  }

  return {bindSuccess, checkSuccess, zc::mv(typeEnv)};
}

}  // namespace

ZC_TEST("Checker.ChecksValidAnnotatedLocal") {
  TestFixture fix;
  auto pat = fix.makeBindingPattern("x"_zc);
  auto ty = fix.makeNamedTypeExpr("i32"_zc);
  auto init = fix.makeIntLiteral(42);
  auto decl = fix.makeVariableDeclarator(pat, ty, init);
  zc::Vector<ast::NodeId> decls;
  decls.add(decl);
  auto let = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr())));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(let);
  auto result = runChecker(fix, topDecls.asPtr());

  ZC_EXPECT(result.bindSuccess);
  ZC_EXPECT(result.checkSuccess);
  ZC_EXPECT(!fix.diagnostics().hasErrors());
  ZC_EXPECT(result.typeEnv.hasType(decl));
  ZC_EXPECT(isPrimitive(result.typeEnv.getType(decl)));
}

ZC_TEST("Checker.RejectsAnnotatedLocalTypeMismatch") {
  TestFixture fix;
  auto pat = fix.makeBindingPattern("x"_zc);
  auto ty = fix.makeNamedTypeExpr("i32"_zc);
  auto init = fix.makeStrLiteral("string"_zc);
  auto decl = fix.makeVariableDeclarator(pat, ty, init);
  zc::Vector<ast::NodeId> decls;
  decls.add(decl);
  auto let = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr())));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(let);
  auto result = runChecker(fix, topDecls.asPtr());

  ZC_EXPECT(result.bindSuccess);
  ZC_EXPECT(!result.checkSuccess);
  ZC_EXPECT(fix.diagnostics().hasErrors());
  ZC_EXPECT(result.typeEnv.hasType(decl));
  ZC_EXPECT(isError(result.typeEnv.getType(decl)));
}

ZC_TEST("Checker.RejectsUndefinedIdentifier") {
  TestFixture fix;
  auto expr = fix.makeIdentExpr("missing"_zc);

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(expr);
  auto result = runChecker(fix, topDecls.asPtr());

  ZC_EXPECT(!result.bindSuccess);
  ZC_EXPECT(!result.checkSuccess);
  ZC_EXPECT(fix.diagnostics().hasErrors());
}

ZC_TEST("Checker.RejectsFunctionArgumentTypeMismatch") {
  TestFixture fix;
  auto param = fix.makeFunctionParamDecl("value"_zc, fix.makeNamedTypeExpr("i32"_zc));
  zc::Vector<ast::NodeId> paramNodes;
  paramNodes.add(param);
  auto params = fix.makeFunctionParamList(fix.makeNodeList(paramNodes.asPtr()));
  auto body = fix.makeBlockStmt(ast::NodeList());
  auto fn = fix.makeFunctionDecl("takes_i32"_zc, body, params, fix.makeNamedTypeExpr("unit"_zc));

  zc::Vector<ast::NodeId> args;
  args.add(fix.makeStrLiteral("bad"_zc));
  auto call = fix.makeCallExpr(fix.makeIdentExpr("takes_i32"_zc), fix.makeNodeList(args.asPtr()));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(fn);
  topDecls.add(call);
  auto result = runChecker(fix, topDecls.asPtr());

  ZC_EXPECT(result.bindSuccess);
  ZC_EXPECT(!result.checkSuccess);
  ZC_EXPECT(fix.diagnostics().hasErrors());
  ZC_EXPECT(result.typeEnv.hasType(call));
  ZC_EXPECT(isError(result.typeEnv.getType(call)));
}

ZC_TEST("Checker.EmitsUseAfterMoveFromBorrowPhase") {
  TestFixture fix;
  auto consumer = zc::heap<CapturingDiagnosticConsumer>();
  auto consumerPtr = consumer.get();
  fix.diagnostics().addConsumer(zc::mv(consumer));

  auto ownedPattern = fix.makeBindingPattern("owned"_zc);
  auto ownedDecl = fix.makeVariableDeclarator(ownedPattern, fix.makeNamedTypeExpr("Owner"_zc));
  auto sinkPattern = fix.makeBindingPattern("sink"_zc);
  auto sinkInit = fix.makeIdentExpr("owned"_zc);
  auto sinkDecl =
      fix.makeVariableDeclarator(sinkPattern, fix.makeNamedTypeExpr("Owner"_zc), sinkInit);
  zc::Vector<ast::NodeId> decls;
  decls.add(ownedDecl);
  decls.add(sinkDecl);
  auto letStmt = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr())));
  auto useStmt = fix.makeExpressionStatement(fix.makeIdentExpr("owned"_zc));
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  stmts.add(useStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body, ast::NodeId(), fix.makeNamedTypeExpr("unit"_zc));

  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(fix.makeClassDecl("Owner"_zc));
  topDecls.add(fn);
  auto result = runChecker(fix, topDecls.asPtr());

  ZC_EXPECT(result.bindSuccess);
  ZC_EXPECT(!result.checkSuccess);
  ZC_EXPECT(containsDiagnosticId(*consumerPtr, diagnostics::DiagID::UseAfterMove));
  ZC_EXPECT(containsDiagnosticId(*consumerPtr, diagnostics::DiagID::ValueMovedHere));
}

ZC_TEST("Checker.DoesNotMutateAstNodes") {
  TestFixture fix;
  auto pat = fix.makeBindingPattern("x"_zc);
  auto ty = fix.makeNamedTypeExpr("i32"_zc);
  auto init = fix.makeIntLiteral(42);
  auto decl = fix.makeVariableDeclarator(pat, ty, init);
  zc::Vector<ast::NodeId> decls;
  decls.add(decl);
  auto let = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr())));
  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(let);

  auto tree = fix.buildSourceFile("test"_zc, topDecls.asPtr());
  zc::Vector<ast::Node> before;
  for (const auto& node : tree.nodes()) { before.add(node); }

  auto identities = tests::makeTestDefinitionIdentityMap(tree);
  binder::Binder binder(fix.symbols(), fix.diagnostics(), tree, identities, fix.metadata());
  ZC_EXPECT(binder.bind());

  tests::TestTypeEnv typeEnv;
  Checker checker(fix.symbols(), fix.diagnostics(), tree, fix.metadata(), typeEnv);
  ZC_EXPECT(checker.check());

  auto after = tree.nodes();
  ZC_EXPECT(after.size() == before.size());
  for (size_t i = 0; i < after.size(); ++i) { ZC_EXPECT(sameNode(before[i], after[i])); }
}

ZC_TEST("Checker.DoesNotMutateBindingMetadata") {
  TestFixture fix;
  auto pat = fix.makeBindingPattern("x"_zc);
  auto ty = fix.makeNamedTypeExpr("i32"_zc);
  auto init = fix.makeIntLiteral(42);
  auto decl = fix.makeVariableDeclarator(pat, ty, init);
  auto ref = fix.makeIdentExpr("x"_zc);
  zc::Vector<ast::NodeId> decls;
  decls.add(decl);
  auto let = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr())));
  zc::Vector<ast::NodeId> topDecls;
  topDecls.add(let);
  topDecls.add(ref);

  auto tree = fix.buildSourceFile("test"_zc, topDecls.asPtr());
  auto identities = tests::makeTestDefinitionIdentityMap(tree);
  binder::Binder binder(fix.symbols(), fix.diagnostics(), tree, identities, fix.metadata());
  ZC_EXPECT(binder.bind());

  zc::Vector<MetadataSnapshotEntry> before;
  auto beforeNodes = tree.nodes();
  for (size_t i = 0; i < beforeNodes.size(); ++i) {
    auto id = ast::NodeId(static_cast<uint32_t>(i + 1));
    before.add(snapshotMetadata(fix.metadata(), id));
  }

  tests::TestTypeEnv typeEnv;
  Checker checker(fix.symbols(), fix.diagnostics(), tree, fix.metadata(), typeEnv);
  ZC_EXPECT(checker.check());

  auto nodes = tree.nodes();
  ZC_EXPECT(nodes.size() == before.size());
  for (size_t i = 0; i < nodes.size(); ++i) {
    auto id = ast::NodeId(static_cast<uint32_t>(i + 1));
    ZC_EXPECT(sameMetadata(before[i], snapshotMetadata(fix.metadata(), id)));
  }
}

}  // namespace checker
}  // namespace compiler
}  // namespace zomlang
