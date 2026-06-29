#include "zomlang/compiler/basic/frontend.h"

#include "zc/ztest/test.h"
#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/ast/tree.h"
#include "zomlang/compiler/basic/string-pool.h"
#include "zomlang/compiler/basic/zomlang-opts.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/source/manager.h"
#include "zomlang/compiler/symbol/symbol-table.h"

namespace zomlang {
namespace compiler {
namespace basic {

static zc::ArrayPtr<const ast::NodeId> topLevelStatements(const ast::Tree& tree) {
  const ast::Node& rootNode = tree.node(tree.root());
  ZC_EXPECT(rootNode.kind == ast::SyntaxKind::SourceFile);

  ast::NodeList list;
  list.first = rootNode.payload.words[ast::kSourceFileStatementsFirstWord];
  list.size = rootNode.payload.words[ast::kSourceFileStatementsSizeWord];
  return tree.list(list);
}

static ast::NodeId statementItem(const ast::Tree& tree, ast::NodeId wrapper) {
  const ast::Node& wrapperNode = tree.node(wrapper);
  if (wrapperNode.kind != ast::SyntaxKind::StatementListItem) { return wrapper; }
  return ast::NodeId(wrapperNode.payload.words[ast::kStatementListItemItemWord]);
}

ZC_TEST("FrontendTest: PerformParseEmptySource") {
  source::SourceManager sourceMgr;
  diagnostics::DiagnosticEngine diagnosticEngine(sourceMgr);
  LangOptions langOpts;
  StringPool stringPool;

  zc::String code = zc::str("");
  auto bufferId = sourceMgr.addMemBufferCopy(code.asBytes(), "test.zom");

  auto result = performParse(sourceMgr, diagnosticEngine, langOpts, stringPool, bufferId);
  ZC_EXPECT(result != zc::none, "Parse result should have value");
}

ZC_TEST("FrontendTest: PerformParseSimpleExpression") {
  source::SourceManager sourceMgr;
  diagnostics::DiagnosticEngine diagnosticEngine(sourceMgr);
  LangOptions langOpts;
  StringPool stringPool;

  zc::String code = zc::str("42");
  auto bufferId = sourceMgr.addMemBufferCopy(code.asBytes(), "test.zom");

  auto result = performParse(sourceMgr, diagnosticEngine, langOpts, stringPool, bufferId);
  ZC_EXPECT(result != zc::none, "Parse result should not be null");
  ZC_IF_SOME(tree, result) {
    const auto statements = topLevelStatements(tree);
    ZC_EXPECT(statements.size() == 1, "Parse result should expose top-level NodeId list");
    ZC_EXPECT(tree.node(statementItem(tree, statements[0])).kind ==
              ast::SyntaxKind::ExpressionStatement);
  }
}

ZC_TEST("FrontendTest: PerformParseVariableDeclaration") {
  source::SourceManager sourceMgr;
  diagnostics::DiagnosticEngine diagnosticEngine(sourceMgr);
  LangOptions langOpts;
  StringPool stringPool;

  zc::String code = zc::str("let x = 42;");
  auto bufferId = sourceMgr.addMemBufferCopy(code.asBytes(), "test.zom");

  auto result = performParse(sourceMgr, diagnosticEngine, langOpts, stringPool, bufferId);
  ZC_EXPECT(result != zc::none, "Parse result should not be null");
}

ZC_TEST("FrontendTest: PerformBindInitializesBindingMetadata") {
  source::SourceManager sourceMgr;
  diagnostics::DiagnosticEngine diagnosticEngine(sourceMgr);
  LangOptions langOpts;
  StringPool stringPool;
  symbol::SymbolTable symbolTable;

  zc::String code = zc::str("let x = 42;");
  auto bufferId = sourceMgr.addMemBufferCopy(code.asBytes(), "test.zom");

  auto result = performParse(sourceMgr, diagnosticEngine, langOpts, stringPool, bufferId);
  ZC_EXPECT(result != zc::none, "Parse result should not be null");

  ZC_IF_SOME(tree, result) {
    ast::BindingMetadata metadata;
    ZC_EXPECT(performBind(symbolTable, diagnosticEngine, tree, metadata),
              "Bind should initialize metadata side tables");
    ZC_EXPECT(metadata.parent(tree.root()) == ast::NodeId());
  }
}

ZC_TEST("FrontendTest: PerformParseFunctionDeclaration") {
  source::SourceManager sourceMgr;
  diagnostics::DiagnosticEngine diagnosticEngine(sourceMgr);
  LangOptions langOpts;
  StringPool stringPool;

  zc::String code = zc::str("fun add(a: i32, b: i32) -> i32 { return a + b; }");
  auto bufferId = sourceMgr.addMemBufferCopy(code.asBytes(), "test.zom");

  auto result = performParse(sourceMgr, diagnosticEngine, langOpts, stringPool, bufferId);
  ZC_EXPECT(result != zc::none, "Parse result should not be null");
}

}  // namespace basic
}  // namespace compiler
}  // namespace zomlang
