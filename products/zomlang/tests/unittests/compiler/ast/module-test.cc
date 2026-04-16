#include "zomlang/compiler/ast/module.h"

#include "zc/ztest/test.h"
#include "zomlang/compiler/ast/ast.h"
#include "zomlang/compiler/ast/expression.h"
#include "zomlang/compiler/ast/factory.h"
#include "zomlang/compiler/ast/type.h"
#include "zomlang/compiler/source/location.h"

namespace zomlang {
namespace compiler {
namespace ast {

namespace {

source::SourceRange makeTestSourceRange(unsigned start, unsigned end) {
  static const char kText[] = "module-range";
  auto bytes = reinterpret_cast<const zc::byte*>(kText);
  return source::SourceRange(source::SourceLoc(bytes + start), source::SourceLoc(bytes + end));
}

void expectMutableFlagsRoundTrip(Node& node) {
  ZC_EXPECT(node.getFlags() == NodeFlags::None);
  node.setFlags(NodeFlags::OptionalChain);
  ZC_EXPECT(hasFlag(node.getFlags(), NodeFlags::OptionalChain));
}

void expectNoOpFlags(Node& node) {
  ZC_EXPECT(node.getFlags() == NodeFlags::None);
  node.setFlags(NodeFlags::OptionalChain);
  ZC_EXPECT(node.getFlags() == NodeFlags::None);
}

void expectSourceRangeRoundTrip(Node& node, const source::SourceRange& range) {
  node.setSourceRange(source::SourceRange(range.getStart(), range.getEnd()));
  const auto& actual = node.getSourceRange();
  ZC_EXPECT(actual.getStart() == range.getStart());
  ZC_EXPECT(actual.getEnd() == range.getEnd());
}

}  // namespace

// ================================================================================
// SourceFile Tests
ZC_TEST("ModuleTest: CreateSourceFile") {
  using namespace zomlang::compiler::ast::factory;
  zc::Vector<zc::Own<ast::Statement>> statements;
  auto sourceFile = createSourceFile(zc::str("test_module.zom"), zc::none, zc::mv(statements));
  ZC_EXPECT(sourceFile->getFileName() == "test_module.zom",
            "SourceFile should have correct filename");
  ZC_EXPECT(sourceFile->getStatements().size() == 0, "SourceFile should have no statements");
}

ZC_TEST("ModuleTest: SourceFileWithStatements") {
  using namespace zomlang::compiler::ast::factory;
  zc::Vector<zc::Own<ast::Statement>> statements;
  auto emptyStmt = createEmptyStatement();
  statements.add(zc::mv(emptyStmt));
  auto sourceFile = createSourceFile(zc::str("test_module.zom"), zc::none, zc::mv(statements));
  ZC_EXPECT(sourceFile->getStatements().size() == 1, "SourceFile should have 1 statement");
}

ZC_TEST("ModuleTest: SourceFileWithMultipleStatements") {
  using namespace zomlang::compiler::ast::factory;
  zc::Vector<zc::Own<ast::Statement>> statements;
  statements.add(createEmptyStatement());
  statements.add(createEmptyStatement());
  statements.add(createEmptyStatement());
  auto sourceFile = createSourceFile(zc::str("multi_stmt.zom"), zc::none, zc::mv(statements));
  ZC_EXPECT(sourceFile->getStatements().size() == 3, "SourceFile should have 3 statements");
  ZC_EXPECT(sourceFile->getFileName() == "multi_stmt.zom",
            "SourceFile should have correct filename");
}

// ================================================================================
// ModulePath Tests
ZC_TEST("ModuleTest: CreateModulePath") {
  using namespace zomlang::compiler::ast::factory;
  zc::Vector<zc::Own<Identifier>> segments;
  segments.add(createIdentifier("std"_zc));
  segments.add(createIdentifier("io"_zc));
  auto modulePath = createModulePath(zc::mv(segments));

  const auto& pathSegments = modulePath->getSegments();
  ZC_EXPECT(pathSegments.size() == 2, "ModulePath should contain 2 segments");
  ZC_EXPECT(pathSegments[0].getText() == "std", "First segment should be std");
  ZC_EXPECT(pathSegments[1].getText() == "io", "Second segment should be io");
}

ZC_TEST("ModuleTest: ModulePathWithMultipleSegments") {
  using namespace zomlang::compiler::ast::factory;
  zc::Vector<zc::Own<Identifier>> segments;
  segments.add(createIdentifier("std"_zc));
  segments.add(createIdentifier("collections"_zc));
  segments.add(createIdentifier("vector"_zc));
  auto modulePath = createModulePath(zc::mv(segments));

  const auto& pathSegments = modulePath->getSegments();
  ZC_EXPECT(pathSegments.size() == 3, "ModulePath should contain 3 segments");
  ZC_EXPECT(pathSegments[2].getText() == "vector", "Last segment should be vector");
}

ZC_TEST("ModuleTest: SingleIdentifierModulePath") {
  using namespace zomlang::compiler::ast::factory;
  zc::Vector<zc::Own<Identifier>> segments;
  segments.add(createIdentifier("math"_zc));
  auto modulePath = createModulePath(zc::mv(segments));

  const auto& pathSegments = modulePath->getSegments();
  ZC_EXPECT(pathSegments.size() == 1, "ModulePath should contain 1 segment");
  ZC_EXPECT(pathSegments[0].getText() == "math", "ModulePath should have correct segment");
}

// ================================================================================
// ImportDeclaration Tests
ZC_TEST("ModuleTest: CreateModuleImportDeclaration") {
  using namespace zomlang::compiler::ast::factory;
  zc::Vector<zc::Own<Identifier>> segments;
  segments.add(createIdentifier("std"_zc));
  segments.add(createIdentifier("io"_zc));
  auto modulePath = createModulePath(zc::mv(segments));
  auto importDecl = createImportDeclaration(zc::mv(modulePath), createIdentifier("io2"_zc),
                                            zc::Vector<zc::Own<ImportSpecifier>>());

  const auto& modulePathRef = importDecl->getModulePath();
  ZC_EXPECT(modulePathRef.getSegments().size() == 2, "Import should have correct module path");
  ZC_EXPECT(importDecl->isModuleImport(), "Import should be a module import");
  ZC_IF_SOME(alias, importDecl->getAlias()) {
    ZC_EXPECT(alias.getText() == "io2", "Import should have correct alias");
  }
}

ZC_TEST("ModuleTest: CreateNamedImportDeclaration") {
  using namespace zomlang::compiler::ast::factory;
  zc::Vector<zc::Own<Identifier>> segments;
  segments.add(createIdentifier("std"_zc));
  segments.add(createIdentifier("collections"_zc));
  auto modulePath = createModulePath(zc::mv(segments));

  zc::Vector<zc::Own<ImportSpecifier>> specifiers;
  specifiers.add(createImportSpecifier(createIdentifier("Vector"_zc), zc::none));
  specifiers.add(createImportSpecifier(createIdentifier("Map"_zc), createIdentifier("HashMap"_zc)));

  auto importDecl = createImportDeclaration(zc::mv(modulePath), zc::none, zc::mv(specifiers));

  ZC_EXPECT(importDecl->isNamedImport(), "Import should be a named import");
  ZC_EXPECT(importDecl->getSpecifiers().size() == 2, "Import should have 2 specifiers");
  ZC_EXPECT(importDecl->getAlias() == zc::none, "Named import should not have module alias");
  ZC_EXPECT(importDecl->getSpecifiers()[0].getImportedName().getText() == "Vector");
  ZC_IF_SOME(alias, importDecl->getSpecifiers()[1].getAlias()) {
    ZC_EXPECT(alias.getText() == "HashMap");
  }
}

// ================================================================================
// ExportDeclaration Tests
ZC_TEST("ModuleTest: CreateLocalExportDeclaration") {
  using namespace zomlang::compiler::ast::factory;
  zc::Vector<zc::Own<ExportSpecifier>> specifiers;
  specifiers.add(createExportSpecifier(createIdentifier("Point"_zc), zc::none));
  specifiers.add(
      createExportSpecifier(createIdentifier("distance"_zc), createIdentifier("calcDistance"_zc)));

  auto exportDecl = createExportDeclaration(zc::none, zc::mv(specifiers), zc::none);

  ZC_EXPECT(exportDecl->isLocalExport(), "Export should be a local export");
  ZC_EXPECT(exportDecl->getSpecifiers().size() == 2, "Export should have 2 specifiers");
  ZC_IF_SOME(alias, exportDecl->getSpecifiers()[1].getAlias()) {
    ZC_EXPECT(alias.getText() == "calcDistance");
  }
}

ZC_TEST("ModuleTest: CreateReExportDeclaration") {
  using namespace zomlang::compiler::ast::factory;
  zc::Vector<zc::Own<Identifier>> segments;
  segments.add(createIdentifier("math"_zc));
  segments.add(createIdentifier("geometry"_zc));
  auto modulePath = createModulePath(zc::mv(segments));

  zc::Vector<zc::Own<ExportSpecifier>> specifiers;
  specifiers.add(createExportSpecifier(createIdentifier("Point"_zc), zc::none));

  auto exportDecl = createExportDeclaration(zc::mv(modulePath), zc::mv(specifiers), zc::none);

  ZC_EXPECT(exportDecl->isReExport(), "Export should be a re-export");
  ZC_EXPECT(exportDecl->getSpecifiers().size() == 1, "Re-export should have 1 specifier");
  ZC_IF_SOME(reexportPath, exportDecl->getModulePath()) {
    ZC_EXPECT(reexportPath.getSegments().size() == 2, "Re-export path should have 2 segments");
  }
}

// ================================================================================
// ModulePath Kind Test
ZC_TEST("ModuleTest: ModulePathKind") {
  using namespace zomlang::compiler::ast::factory;
  zc::Vector<zc::Own<Identifier>> segments;
  segments.add(createIdentifier("foo"_zc));
  segments.add(createIdentifier("bar"_zc));
  segments.add(createIdentifier("baz"_zc));
  auto modulePath = createModulePath(zc::mv(segments));

  ZC_EXPECT(modulePath->getKind() == SyntaxKind::ModulePath,
            "ModulePath should have ModulePath kind");
  ZC_EXPECT(modulePath->getSegments().size() == 3, "ModulePath should have 3 segments");
  ZC_EXPECT(modulePath->getSegments()[0].getText() == "foo", "First segment should be 'foo'");
  ZC_EXPECT(modulePath->getSegments()[1].getText() == "bar", "Second segment should be 'bar'");
  ZC_EXPECT(modulePath->getSegments()[2].getText() == "baz", "Third segment should be 'baz'");
}

// ================================================================================
// ModuleDeclaration Tests
ZC_TEST("ModuleTest: CreateModuleDeclaration") {
  using namespace zomlang::compiler::ast::factory;
  zc::Vector<zc::Own<Identifier>> segments;
  segments.add(createIdentifier("myapp"_zc));
  segments.add(createIdentifier("core"_zc));
  auto modulePath = createModulePath(zc::mv(segments));
  auto moduleDecl = createModuleDeclaration(zc::mv(modulePath));

  ZC_EXPECT(moduleDecl->getKind() == SyntaxKind::ModuleDeclaration,
            "ModuleDeclaration should have correct kind");
  const auto& path = moduleDecl->getModulePath();
  ZC_EXPECT(path.getSegments().size() == 2,
            "ModuleDeclaration should have module path with 2 segments");
  ZC_EXPECT(path.getSegments()[0].getText() == "myapp", "First path segment should be 'myapp'");
  ZC_EXPECT(path.getSegments()[1].getText() == "core", "Second path segment should be 'core'");
}

// ================================================================================
// SourceFile Kind Test
ZC_TEST("ModuleTest: SourceFileKind") {
  using namespace zomlang::compiler::ast::factory;
  zc::Vector<zc::Own<ast::Statement>> statements;
  auto sourceFile = createSourceFile(zc::str("test.zom"), zc::none, zc::mv(statements));

  ZC_EXPECT(sourceFile->getKind() == SyntaxKind::SourceFile,
            "SourceFile should have SourceFile kind");
}

// ================================================================================
// SourceFile With ModuleDeclaration
ZC_TEST("ModuleTest: SourceFileWithModuleDeclaration") {
  using namespace zomlang::compiler::ast::factory;
  zc::Vector<zc::Own<Identifier>> pathSegments;
  pathSegments.add(createIdentifier("app"_zc));
  pathSegments.add(createIdentifier("utils"_zc));
  auto modulePath = createModulePath(zc::mv(pathSegments));
  auto moduleDecl = createModuleDeclaration(zc::mv(modulePath));

  zc::Vector<zc::Own<ast::Statement>> statements;
  auto sourceFile = createSourceFile(zc::str("utils.zom"), zc::mv(moduleDecl), zc::mv(statements));

  ZC_EXPECT(sourceFile->getFileName() == "utils.zom", "SourceFile should have correct filename");
  ZC_EXPECT(sourceFile->getModuleDeclaration() != zc::none,
            "SourceFile should have a module declaration");
  ZC_IF_SOME(mod, sourceFile->getModuleDeclaration()) {
    ZC_EXPECT(mod.getModulePath().getSegments().size() == 2, "Module path should have 2 segments");
  }
}

// ================================================================================
// SourceFile Without ModuleDeclaration
ZC_TEST("ModuleTest: SourceFileWithoutModuleDeclaration") {
  using namespace zomlang::compiler::ast::factory;
  zc::Vector<zc::Own<ast::Statement>> statements;
  statements.add(createEmptyStatement());
  auto sourceFile = createSourceFile(zc::str("script.zom"), zc::none, zc::mv(statements));

  ZC_EXPECT(sourceFile->getModuleDeclaration() == zc::none,
            "SourceFile should not have a module declaration");
  ZC_EXPECT(sourceFile->getFileName() == "script.zom", "SourceFile should have correct filename");
  ZC_EXPECT(sourceFile->getStatements().size() == 1, "SourceFile should have 1 statement");
  ZC_EXPECT(sourceFile->getKind() == SyntaxKind::SourceFile,
            "SourceFile should have SourceFile kind");
}

// ================================================================================
// ImportSpecifier Tests
ZC_TEST("ModuleTest: ImportSpecifierWithoutAlias") {
  using namespace zomlang::compiler::ast::factory;
  auto spec = createImportSpecifier(createIdentifier("MyClass"_zc), zc::none);

  ZC_EXPECT(spec->getImportedName().getText() == "MyClass",
            "ImportSpecifier should have correct imported name");
  ZC_EXPECT(spec->getAlias() == zc::none, "ImportSpecifier should not have an alias");
  ZC_EXPECT(spec->getKind() == SyntaxKind::ImportSpecifier,
            "ImportSpecifier should have correct kind");
}

ZC_TEST("ModuleTest: ImportSpecifierWithAlias") {
  using namespace zomlang::compiler::ast::factory;
  auto spec = createImportSpecifier(createIdentifier("MyClass"_zc), createIdentifier("Cls"_zc));

  ZC_EXPECT(spec->getImportedName().getText() == "MyClass",
            "ImportSpecifier should have correct imported name");
  ZC_EXPECT(spec->getAlias() != zc::none, "ImportSpecifier should have an alias");
  ZC_IF_SOME(alias, spec->getAlias()) {
    ZC_EXPECT(alias.getText() == "Cls", "Alias should have correct text");
  }
}

// ================================================================================
// ImportDeclaration — Module Import
ZC_TEST("ModuleTest: ImportDeclarationModuleImport") {
  using namespace zomlang::compiler::ast::factory;
  zc::Vector<zc::Own<Identifier>> segments;
  segments.add(createIdentifier("std"_zc));
  auto modulePath = createModulePath(zc::mv(segments));

  zc::Vector<zc::Own<ImportSpecifier>> noSpecifiers;
  auto importDecl = createImportDeclaration(zc::mv(modulePath), zc::none, zc::mv(noSpecifiers));

  ZC_EXPECT(importDecl->isModuleImport() == true,
            "Import with no specifiers should be a module import");
  ZC_EXPECT(importDecl->isNamedImport() == false,
            "Import with no specifiers should not be a named import");
  ZC_EXPECT(importDecl->getKind() == SyntaxKind::ImportDeclaration,
            "ImportDeclaration should have correct kind");
}

// ================================================================================
// ImportDeclaration — Named Import
ZC_TEST("ModuleTest: ImportDeclarationNamedImport") {
  using namespace zomlang::compiler::ast::factory;
  zc::Vector<zc::Own<Identifier>> segments;
  segments.add(createIdentifier("std"_zc));
  segments.add(createIdentifier("collections"_zc));
  auto modulePath = createModulePath(zc::mv(segments));

  zc::Vector<zc::Own<ImportSpecifier>> specifiers;
  specifiers.add(createImportSpecifier(createIdentifier("List"_zc), zc::none));
  specifiers.add(createImportSpecifier(createIdentifier("Map"_zc), createIdentifier("Dict"_zc)));
  specifiers.add(createImportSpecifier(createIdentifier("Set"_zc), zc::none));

  auto importDecl = createImportDeclaration(zc::mv(modulePath), zc::none, zc::mv(specifiers));

  ZC_EXPECT(importDecl->isModuleImport() == false,
            "Import with specifiers should not be a module import");
  ZC_EXPECT(importDecl->isNamedImport() == true, "Import with specifiers should be a named import");
  ZC_EXPECT(importDecl->getSpecifiers().size() == 3, "Named import should have 3 specifiers");
}

// ================================================================================
// ImportDeclaration With Alias
ZC_TEST("ModuleTest: ImportDeclarationWithAlias") {
  using namespace zomlang::compiler::ast::factory;
  zc::Vector<zc::Own<Identifier>> segments;
  segments.add(createIdentifier("network"_zc));
  auto modulePath = createModulePath(zc::mv(segments));

  zc::Vector<zc::Own<ImportSpecifier>> noSpecifiers;
  auto importDecl =
      createImportDeclaration(zc::mv(modulePath), createIdentifier("net"_zc), zc::mv(noSpecifiers));

  ZC_EXPECT(importDecl->getAlias() != zc::none, "Import should have an alias");
  ZC_IF_SOME(alias, importDecl->getAlias()) {
    ZC_EXPECT(alias.getText() == "net", "Alias should have correct text");
  }
  ZC_EXPECT(importDecl->isModuleImport() == true,
            "Module import with alias is still a module import");
}

// ================================================================================
// ExportSpecifier Tests
ZC_TEST("ModuleTest: ExportSpecifierWithoutAlias") {
  using namespace zomlang::compiler::ast::factory;
  auto spec = createExportSpecifier(createIdentifier("MyType"_zc), zc::none);

  ZC_EXPECT(spec->getExportedName().getText() == "MyType",
            "ExportSpecifier should have correct exported name");
  ZC_EXPECT(spec->getAlias() == zc::none, "ExportSpecifier should not have an alias");
  ZC_EXPECT(spec->getKind() == SyntaxKind::ExportSpecifier,
            "ExportSpecifier should have correct kind");
}

ZC_TEST("ModuleTest: ExportSpecifierWithAlias") {
  using namespace zomlang::compiler::ast::factory;
  auto spec =
      createExportSpecifier(createIdentifier("computeResult"_zc), createIdentifier("compute"_zc));

  ZC_EXPECT(spec->getExportedName().getText() == "computeResult",
            "ExportSpecifier should have correct exported name");
  ZC_EXPECT(spec->getAlias() != zc::none, "ExportSpecifier should have an alias");
  ZC_IF_SOME(alias, spec->getAlias()) {
    ZC_EXPECT(alias.getText() == "compute", "Alias should have correct text");
  }
}

// ================================================================================
// ExportDeclaration — Local Export
ZC_TEST("ModuleTest: ExportDeclarationLocalExport") {
  using namespace zomlang::compiler::ast::factory;
  zc::Vector<zc::Own<ExportSpecifier>> specifiers;
  specifiers.add(createExportSpecifier(createIdentifier("foo"_zc), zc::none));
  specifiers.add(createExportSpecifier(createIdentifier("bar"_zc), zc::none));

  auto exportDecl = createExportDeclaration(zc::none, zc::mv(specifiers), zc::none);

  ZC_EXPECT(exportDecl->isLocalExport() == true,
            "Export with no modulePath and no declaration should be a local export");
  ZC_EXPECT(exportDecl->isReExport() == false, "Local export should not be a re-export");
  ZC_EXPECT(exportDecl->isDeclarationExport() == false,
            "Local export should not be a declaration export");
  ZC_EXPECT(exportDecl->getSpecifiers().size() == 2, "Local export should have 2 specifiers");
}

// ================================================================================
// ExportDeclaration — Re-export
ZC_TEST("ModuleTest: ExportDeclarationReExport") {
  using namespace zomlang::compiler::ast::factory;
  zc::Vector<zc::Own<Identifier>> segments;
  segments.add(createIdentifier("external"_zc));
  segments.add(createIdentifier("lib"_zc));
  auto modulePath = createModulePath(zc::mv(segments));

  zc::Vector<zc::Own<ExportSpecifier>> specifiers;
  specifiers.add(createExportSpecifier(createIdentifier("Service"_zc), zc::none));

  auto exportDecl = createExportDeclaration(zc::mv(modulePath), zc::mv(specifiers), zc::none);

  ZC_EXPECT(exportDecl->isReExport() == true, "Export with modulePath should be a re-export");
  ZC_EXPECT(exportDecl->isLocalExport() == false, "Re-export should not be a local export");
  ZC_IF_SOME(path, exportDecl->getModulePath()) {
    ZC_EXPECT(path.getSegments().size() == 2, "Re-export path should have 2 segments");
    ZC_EXPECT(path.getSegments()[0].getText() == "external", "First segment should be 'external'");
  }
}

// ================================================================================
// ExportDeclaration — Declaration Export
ZC_TEST("ModuleTest: ExportDeclarationWithDeclaration") {
  using namespace zomlang::compiler::ast::factory;
  auto funcBody = createBlockStatement(zc::Vector<zc::Own<ast::Statement>>());
  auto funcDecl = createFunctionDeclaration(createIdentifier("greet"_zc), zc::none,
                                            zc::Vector<zc::Own<ParameterDeclaration>>(), zc::none,
                                            zc::mv(funcBody));

  zc::Vector<zc::Own<ExportSpecifier>> noSpecifiers;
  auto exportDecl = createExportDeclaration(zc::none, zc::mv(noSpecifiers), zc::mv(funcDecl));

  ZC_EXPECT(exportDecl->isDeclarationExport() == true,
            "Export with inline declaration should be a declaration export");
  ZC_EXPECT(exportDecl->isLocalExport() == false,
            "Declaration export should not be a local export");
  ZC_EXPECT(exportDecl->isReExport() == false, "Declaration export should not be a re-export");
  ZC_EXPECT(exportDecl->getDeclaration() != zc::none,
            "Declaration export should have a declaration");
}

// ================================================================================
// ExportDeclaration — Empty Local Export (no specifiers, no path, no declaration)
ZC_TEST("ModuleTest: ExportDeclarationEmpty") {
  using namespace zomlang::compiler::ast::factory;
  zc::Vector<zc::Own<ExportSpecifier>> noSpecifiers;
  auto exportDecl = createExportDeclaration(zc::none, zc::mv(noSpecifiers), zc::none);

  ZC_EXPECT(exportDecl->isLocalExport() == true, "Empty export should be a local export");
  ZC_EXPECT(exportDecl->getSpecifiers().size() == 0, "Empty export should have no specifiers");
  ZC_EXPECT(exportDecl->getModulePath() == zc::none, "Empty export should have no module path");
  ZC_EXPECT(exportDecl->getDeclaration() == zc::none, "Empty export should have no declaration");
}

// ================================================================================
// ExportDeclaration — With Multiple Specifiers
ZC_TEST("ModuleTest: ExportDeclarationWithSpecifiers") {
  using namespace zomlang::compiler::ast::factory;
  zc::Vector<zc::Own<ExportSpecifier>> specifiers;
  specifiers.add(createExportSpecifier(createIdentifier("alpha"_zc), zc::none));
  specifiers.add(createExportSpecifier(createIdentifier("beta"_zc), createIdentifier("b"_zc)));
  specifiers.add(createExportSpecifier(createIdentifier("gamma"_zc), zc::none));

  auto exportDecl = createExportDeclaration(zc::none, zc::mv(specifiers), zc::none);

  ZC_EXPECT(exportDecl->getSpecifiers().size() == 3, "Export should have 3 specifiers");
  ZC_EXPECT(exportDecl->getSpecifiers()[0].getExportedName().getText() == "alpha",
            "First specifier name should be 'alpha'");
  ZC_IF_SOME(alias, exportDecl->getSpecifiers()[1].getAlias()) {
    ZC_EXPECT(alias.getText() == "b", "Second specifier alias should be 'b'");
  }
  ZC_EXPECT(exportDecl->getSpecifiers()[2].getExportedName().getText() == "gamma",
            "Third specifier name should be 'gamma'");
}

// ================================================================================
// ExportDeclaration Kind Test
ZC_TEST("ModuleTest: ExportDeclarationKind") {
  using namespace zomlang::compiler::ast::factory;
  zc::Vector<zc::Own<ExportSpecifier>> specifiers;
  auto exportDecl = createExportDeclaration(zc::none, zc::mv(specifiers), zc::none);

  ZC_EXPECT(exportDecl->getKind() == SyntaxKind::ExportDeclaration,
            "ExportDeclaration should have correct kind");
}

ZC_TEST("ModuleTest: NodeFlagsAndSourceRanges") {
  using namespace zomlang::compiler::ast::factory;

  auto sourceFile =
      createSourceFile(zc::str("flags.zom"), zc::none, zc::Vector<zc::Own<Statement>>());
  expectNoOpFlags(*sourceFile);
  expectSourceRangeRoundTrip(*sourceFile, makeTestSourceRange(0, 3));

  zc::Vector<zc::Own<Identifier>> pathSegments;
  pathSegments.add(createIdentifier("pkg"_zc));
  pathSegments.add(createIdentifier("core"_zc));
  auto modulePath = createModulePath(zc::mv(pathSegments));
  expectNoOpFlags(*modulePath);
  expectSourceRangeRoundTrip(*modulePath, makeTestSourceRange(1, 4));

  zc::Vector<zc::Own<Identifier>> moduleDeclarationSegments;
  moduleDeclarationSegments.add(createIdentifier("app"_zc));
  moduleDeclarationSegments.add(createIdentifier("ui"_zc));
  auto moduleDeclaration =
      createModuleDeclaration(createModulePath(zc::mv(moduleDeclarationSegments)));
  expectMutableFlagsRoundTrip(*moduleDeclaration);
  expectSourceRangeRoundTrip(*moduleDeclaration, makeTestSourceRange(2, 5));

  auto importSpecifier =
      createImportSpecifier(createIdentifier("Button"_zc), createIdentifier("UiButton"_zc));
  expectMutableFlagsRoundTrip(*importSpecifier);
  expectSourceRangeRoundTrip(*importSpecifier, makeTestSourceRange(3, 6));

  zc::Vector<zc::Own<ImportSpecifier>> importSpecifiers;
  importSpecifiers.add(createImportSpecifier(createIdentifier("Dialog"_zc), zc::none));
  zc::Vector<zc::Own<Identifier>> importDeclarationSegments;
  importDeclarationSegments.add(createIdentifier("ui"_zc));
  auto importDeclaration =
      createImportDeclaration(createModulePath(zc::mv(importDeclarationSegments)),
                              createIdentifier("widgets"_zc), zc::mv(importSpecifiers));
  expectMutableFlagsRoundTrip(*importDeclaration);
  expectSourceRangeRoundTrip(*importDeclaration, makeTestSourceRange(4, 7));

  auto exportSpecifier =
      createExportSpecifier(createIdentifier("Dialog"_zc), createIdentifier("Modal"_zc));
  expectMutableFlagsRoundTrip(*exportSpecifier);
  expectSourceRangeRoundTrip(*exportSpecifier, makeTestSourceRange(5, 8));

  zc::Vector<zc::Own<ExportSpecifier>> exportSpecifiers;
  exportSpecifiers.add(createExportSpecifier(createIdentifier("Dialog"_zc), zc::none));
  zc::Vector<zc::Own<Identifier>> exportDeclarationSegments;
  exportDeclarationSegments.add(createIdentifier("ui"_zc));
  auto exportDeclaration = createExportDeclaration(
      createModulePath(zc::mv(exportDeclarationSegments)), zc::mv(exportSpecifiers), zc::none);
  expectMutableFlagsRoundTrip(*exportDeclaration);
  expectSourceRangeRoundTrip(*exportDeclaration, makeTestSourceRange(6, 9));
}

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
