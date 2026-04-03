#include "zomlang/compiler/ast/module.h"

#include "zc/ztest/test.h"
#include "zomlang/compiler/ast/expression.h"
#include "zomlang/compiler/ast/factory.h"
#include "zomlang/compiler/ast/type.h"

namespace zomlang {
namespace compiler {
namespace ast {

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

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
