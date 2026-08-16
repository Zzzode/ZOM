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
// See the License for the specific language governing permissions and limitations under
// the License.

#include "zomlang/compiler/driver/module-discovery.h"

#include "zc/core/memory.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/basic/string-pool.h"
#include "zomlang/compiler/basic/zomlang-opts.h"
#include "zomlang/compiler/diagnostics/core/diagnostic-engine.h"
#include "zomlang/compiler/diagnostics/fact/source-diagnostic-draft-buffer.h"
#include "zomlang/compiler/parser/parser.h"
#include "zomlang/compiler/source/manager.h"

namespace zomlang::compiler::driver {
namespace {

void addFile(package::SourceTreeBuilder& builder, zc::StringPtr path) {
  ZC_REQUIRE(builder.beginFile(path, 0) == zc::none);
  ZC_REQUIRE(builder.endFile() == zc::none);
}

package::SourceTreeRecord sourceTree(zc::ArrayPtr<const zc::StringPtr> paths) {
  package::SourceTreeBuilder builder;
  for (const auto path : paths) { addFile(builder, path); }
  auto result = builder.finish();
  if (result.is<package::SourceTreeRecord>()) {
    return zc::mv(result.get<package::SourceTreeRecord>());
  }
  ZC_FAIL_REQUIRE("source-tree discovery fixture was rejected");
}

identity::CanonicalRelativePath searchRoot() {
  zc::Vector<identity::CanonicalPathSegment> segments;
  auto segment = identity::CanonicalPathSegment::fromCanonical("src"_zc);
  ZC_IF_SOME(value, segment) { segments.add(zc::mv(value)); }
  ZC_REQUIRE(segments.size() == 1);
  return identity::CanonicalRelativePath::from(zc::mv(segments));
}

zc::Vector<identity::ModulePathSegment> modulePath() {
  zc::Vector<identity::ModulePathSegment> result;
  for (const auto text : {"a"_zc, "b"_zc, "c"_zc}) {
    auto segment = identity::ModulePathSegment::fromCanonical(text);
    ZC_IF_SOME(value, segment) { result.add(zc::mv(value)); }
  }
  ZC_REQUIRE(result.size() == 3);
  return result;
}

void expectPath(const identity::CanonicalRelativePath& actual,
                zc::ArrayPtr<const zc::StringPtr> expected) {
  ZC_REQUIRE(actual.segments().size() == expected.size());
  for (size_t index = 0; index < expected.size(); ++index) {
    ZC_EXPECT(actual.segments()[index].text() == expected[index]);
  }
}

class ParsedTreeFixture final {
public:
  ParsedTreeFixture(zc::Own<source::SourceManager>&& sourceManager, ast::Tree&& tree)
      : sourceManagerValue(zc::mv(sourceManager)), treeValue(zc::mv(tree)) {}

  ZC_NODISCARD const ast::Tree& tree() const noexcept { return treeValue; }

private:
  zc::Own<source::SourceManager> sourceManagerValue;
  ast::Tree treeValue;
};

ParsedTreeFixture parseTree(zc::StringPtr text) {
  auto sourceManager = zc::heap<source::SourceManager>();
  basic::LangOptions langOptions;
  basic::StringPool strings;
  const auto buffer = sourceManager->addMemBufferCopy(text.asBytes(), "module-discovery.zom");
  diagnostics::SourceDiagnosticDraftBuffer diagnosticFacts(*sourceManager, buffer);
  parser::Parser parser(*sourceManager, diagnosticFacts, langOptions, strings, buffer);
  auto result = parser.parse();
  ZC_IF_SOME(tree, result) {
    ZC_REQUIRE(!diagnosticFacts.hasErrors());
    return ParsedTreeFixture(zc::mv(sourceManager), zc::mv(tree));
  }
  ZC_FAIL_REQUIRE("module dependency fixture did not parse");
}

void expectModulePath(zc::ArrayPtr<const identity::ModulePathSegment> actual,
                      zc::ArrayPtr<const zc::StringPtr> expected) {
  ZC_REQUIRE(actual.size() == expected.size());
  for (size_t index = 0; index < expected.size(); ++index) {
    ZC_EXPECT(actual[index].text() == expected[index]);
  }
}

ast::Tree malformedImportTree(bool repeatedSite) {
  ast::TreeBuilder builder;
  ast::NodePayload pathPayload;
  if (!repeatedSite) {
    const auto emptySegments = builder.makeIdentList({});
    pathPayload.words[ast::kModulePathSegmentsFirstWord] = emptySegments.first;
    pathPayload.words[ast::kModulePathSegmentsSizeWord] = emptySegments.size;
  } else {
    const ast::IdentId identifiers[] = {builder.internIdent("dependency"_zc)};
    const auto segments = builder.makeIdentList(identifiers);
    pathPayload.words[ast::kModulePathSegmentsFirstWord] = segments.first;
    pathPayload.words[ast::kModulePathSegmentsSizeWord] = segments.size;
  }
  const auto path =
      builder.makeNode(ast::SyntaxKind::ModulePath, source::SourceRange(), pathPayload);

  ast::NodePayload importPayload;
  importPayload.words[ast::kImportDeclarationPathWord] = path.value;
  const auto import =
      builder.makeNode(ast::SyntaxKind::ImportDeclaration, source::SourceRange(), importPayload);

  ast::NodePayload firstWrapperPayload;
  firstWrapperPayload.words[ast::kStatementListItemItemWord] = import.value;
  const auto firstWrapper = builder.makeNode(ast::SyntaxKind::StatementListItem,
                                             source::SourceRange(), firstWrapperPayload);
  zc::Vector<ast::NodeId> statementValues;
  statementValues.add(firstWrapper);
  if (repeatedSite) {
    ast::NodePayload secondWrapperPayload;
    secondWrapperPayload.words[ast::kStatementListItemItemWord] = import.value;
    statementValues.add(builder.makeNode(ast::SyntaxKind::StatementListItem, source::SourceRange(),
                                         secondWrapperPayload));
  }
  const auto statements = builder.makeList(statementValues.asPtr());

  ast::NodePayload sourcePayload;
  sourcePayload.words[ast::kSourceFileFileNameWord] = builder.internString("invalid.zom"_zc).value;
  sourcePayload.words[ast::kSourceFileStatementsFirstWord] = statements.first;
  sourcePayload.words[ast::kSourceFileStatementsSizeWord] = statements.size;
  const auto root =
      builder.makeNode(ast::SyntaxKind::SourceFile, source::SourceRange(), sourcePayload);
  builder.setRoot(root);
  return builder.finish();
}

}  // namespace

ZC_TEST("ModuleDiscoveryTest.RejectsEmptyModulePathAsInvariant") {
  const zc::StringPtr files[] = {"src/a.zom"_zc};
  auto tree = sourceTree(files);
  auto root = searchRoot();

  auto result = discoverModuleSource(tree, root, zc::ArrayPtr<const identity::ModulePathSegment>());

  ZC_EXPECT(result.is<InvalidModuleSourceRequest>());
}

ZC_TEST("ModuleDiscoveryTest.ReturnsMissingWhenNeitherCandidateExists") {
  const zc::StringPtr files[] = {"src/a/b/other.zom"_zc, "src/a/b/c/not-mod.zom"_zc};
  auto tree = sourceTree(files);
  auto root = searchRoot();
  auto path = modulePath();

  auto result = discoverModuleSource(tree, root, path.asPtr());

  ZC_EXPECT(result.is<MissingModuleSource>());
}

ZC_TEST("ModuleDiscoveryTest.ResolvesDirectModuleFile") {
  const zc::StringPtr files[] = {"src/a/b/c.zom"_zc};
  auto tree = sourceTree(files);
  auto root = searchRoot();
  auto path = modulePath();

  auto result = discoverModuleSource(tree, root, path.asPtr());

  ZC_REQUIRE(result.is<ResolvedModuleSource>());
  const zc::StringPtr expected[] = {"src"_zc, "a"_zc, "b"_zc, "c.zom"_zc};
  expectPath(result.get<ResolvedModuleSource>().path(), expected);
}

ZC_TEST("ModuleDiscoveryTest.ResolvesNestedModuleFile") {
  const zc::StringPtr files[] = {"src/a/b/c/mod.zom"_zc};
  auto tree = sourceTree(files);
  auto root = searchRoot();
  auto path = modulePath();

  auto result = discoverModuleSource(tree, root, path.asPtr());

  ZC_REQUIRE(result.is<ResolvedModuleSource>());
  const zc::StringPtr expected[] = {"src"_zc, "a"_zc, "b"_zc, "c"_zc, "mod.zom"_zc};
  expectPath(result.get<ResolvedModuleSource>().path(), expected);
}

ZC_TEST("ModuleDiscoveryTest.ReturnsBothAmbiguousCandidatesInCanonicalOrder") {
  const zc::StringPtr files[] = {"src/a/b/c/mod.zom"_zc, "src/a/b/c.zom"_zc};
  auto tree = sourceTree(files);
  auto root = searchRoot();
  auto path = modulePath();

  auto result = discoverModuleSource(tree, root, path.asPtr());

  ZC_REQUIRE(result.is<AmbiguousModuleSource>());
  const auto candidates = result.get<AmbiguousModuleSource>().paths();
  ZC_REQUIRE(candidates.size() == 2);
  const zc::StringPtr direct[] = {"src"_zc, "a"_zc, "b"_zc, "c.zom"_zc};
  const zc::StringPtr nested[] = {"src"_zc, "a"_zc, "b"_zc, "c"_zc, "mod.zom"_zc};
  expectPath(candidates[0], direct);
  expectPath(candidates[1], nested);
}

ZC_TEST("ModuleDiscoveryTest.ExtractsNormalizedImportsInCanonicalOrder") {
  auto fixture = parseTree("module root;\nimport zeta::leaf;\nimport beta::leaf;\n"_zc);

  auto result = extractStructuralModuleDependencyRequests(fixture.tree());

  ZC_REQUIRE(result.is<zc::Vector<StructuralModuleDependencyRequest>>());
  const auto& requests = result.get<zc::Vector<StructuralModuleDependencyRequest>>();
  ZC_REQUIRE(requests.size() == 2);
  ZC_EXPECT(requests[0].kind() == StructuralModuleDependencyKind::Import);
  ZC_EXPECT(requests[1].kind() == StructuralModuleDependencyKind::Import);
  const zc::StringPtr first[] = {"beta"_zc, "leaf"_zc};
  const zc::StringPtr second[] = {"zeta"_zc, "leaf"_zc};
  expectModulePath(requests[0].normalizedPath(), first);
  expectModulePath(requests[1].normalizedPath(), second);
  ZC_EXPECT(requests[0].schemaPreorderOrdinal() > requests[1].schemaPreorderOrdinal());
  ZC_EXPECT(requests[0].syntaxNode() != requests[1].syntaxNode());
}

ZC_TEST("ModuleDiscoveryTest.ExtractsForeignReexport") {
  auto fixture = parseTree("module root;\nexport math::geometry::{Point};\n"_zc);

  auto result = extractStructuralModuleDependencyRequests(fixture.tree());

  ZC_REQUIRE(result.is<zc::Vector<StructuralModuleDependencyRequest>>());
  const auto& requests = result.get<zc::Vector<StructuralModuleDependencyRequest>>();
  ZC_REQUIRE(requests.size() == 1);
  ZC_EXPECT(requests[0].kind() == StructuralModuleDependencyKind::ForeignReexport);
  const zc::StringPtr expected[] = {"math"_zc, "geometry"_zc};
  expectModulePath(requests[0].normalizedPath(), expected);
}

ZC_TEST("ModuleDiscoveryTest.ExtractsModuleAlias") {
  auto fixture = parseTree("export module geometry = math::geometry;\n"_zc);

  auto result = extractStructuralModuleDependencyRequests(fixture.tree());

  ZC_REQUIRE(result.is<zc::Vector<StructuralModuleDependencyRequest>>());
  const auto& requests = result.get<zc::Vector<StructuralModuleDependencyRequest>>();
  ZC_REQUIRE(requests.size() == 1);
  ZC_EXPECT(requests[0].kind() == StructuralModuleDependencyKind::ModuleAlias);
  const zc::StringPtr expected[] = {"math"_zc, "geometry"_zc};
  expectModulePath(requests[0].normalizedPath(), expected);
}

ZC_TEST("ModuleDiscoveryTest.IgnoresLocalExportLists") {
  auto fixture = parseTree("module root;\nexport {value};\n"_zc);

  auto result = extractStructuralModuleDependencyRequests(fixture.tree());

  ZC_REQUIRE(result.is<zc::Vector<StructuralModuleDependencyRequest>>());
  ZC_EXPECT(result.get<zc::Vector<StructuralModuleDependencyRequest>>().empty());
}

ZC_TEST("ModuleDiscoveryTest.IgnoresUnreachableParserArenaNodes") {
  auto fixture = parseTree(R"(
struct Box { item: &i32 }

fun leak() -> &i32 {
  let value: i32;
  return Box { item: (&value) }.item;
}
)"_zc);

  auto result = extractStructuralModuleDependencyRequests(fixture.tree());

  ZC_REQUIRE(result.is<zc::Vector<StructuralModuleDependencyRequest>>());
  ZC_EXPECT(result.get<zc::Vector<StructuralModuleDependencyRequest>>().empty());
}

ZC_TEST("ModuleDiscoveryTest.TraversesSharedNonDeclarationSubtreesOnce") {
  auto fixture = parseTree(R"(
interface Eq { }
class Box<T> { value: T; }
impl<T> Eq for Box<T> where T: Eq { }
)"_zc);

  auto result = extractStructuralModuleDependencyRequests(fixture.tree());

  ZC_REQUIRE(result.is<zc::Vector<StructuralModuleDependencyRequest>>());
  ZC_EXPECT(result.get<zc::Vector<StructuralModuleDependencyRequest>>().empty());
}

ZC_TEST("ModuleDiscoveryTest.RejectsInvalidTreesPathsAndRepeatedSites") {
  ast::Tree missingRoot;
  auto missingRootResult = extractStructuralModuleDependencyRequests(missingRoot);
  ZC_EXPECT(missingRootResult.is<InvalidStructuralModuleDependencyRequests>());

  auto emptyPath = malformedImportTree(false);
  auto emptyPathResult = extractStructuralModuleDependencyRequests(emptyPath);
  ZC_EXPECT(emptyPathResult.is<InvalidStructuralModuleDependencyRequests>());

  auto repeatedSite = malformedImportTree(true);
  auto repeatedSiteResult = extractStructuralModuleDependencyRequests(repeatedSite);
  ZC_EXPECT(repeatedSiteResult.is<InvalidStructuralModuleDependencyRequests>());
}

}  // namespace zomlang::compiler::driver
