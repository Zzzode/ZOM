#include "zomlang/compiler/ast/module.h"

#include "zc/ztest/test.h"
#include "zomlang/compiler/ast/factory.h"

namespace zomlang {
namespace compiler {
namespace ast {

// ================================================================================
// SourceFile Tests
ZC_TEST("ModuleTest: CreateSourceFile") {
  using namespace zomlang::compiler::ast::factory;
  zc::Vector<zc::Own<ast::Statement>> statements;
  auto sourceFile = createSourceFile(zc::str("test_module.zom"), zc::mv(statements));
  ZC_EXPECT(sourceFile->getFileName() == "test_module.zom",
            "SourceFile should have correct filename");
  ZC_EXPECT(sourceFile->getStatements().size() == 0, "SourceFile should have no statements");
}

ZC_TEST("ModuleTest: SourceFileWithStatements") {
  using namespace zomlang::compiler::ast::factory;
  zc::Vector<zc::Own<ast::Statement>> statements;
  auto emptyStmt = createEmptyStatement();
  statements.add(zc::mv(emptyStmt));
  auto sourceFile = createSourceFile(zc::str("test_module.zom"), zc::mv(statements));
  ZC_EXPECT(sourceFile->getStatements().size() == 1, "SourceFile should have 1 statement");
}

ZC_TEST("ModuleTest: SourceFileWithMultipleStatements") {
  using namespace zomlang::compiler::ast::factory;
  zc::Vector<zc::Own<ast::Statement>> statements;
  statements.add(createEmptyStatement());
  statements.add(createEmptyStatement());
  statements.add(createEmptyStatement());
  auto sourceFile = createSourceFile(zc::str("multi_stmt.zom"), zc::mv(statements));
  ZC_EXPECT(sourceFile->getStatements().size() == 3, "SourceFile should have 3 statements");
  ZC_EXPECT(sourceFile->getFileName() == "multi_stmt.zom",
            "SourceFile should have correct filename");
}

// ================================================================================
// ModulePath Tests
ZC_TEST("ModuleTest: CreateModulePath") {
  using namespace zomlang::compiler::ast::factory;
  zc::Vector<zc::String> identifiers;
  identifiers.add(zc::str("std"));
  identifiers.add(zc::str("io"));
  auto modulePath = createModulePath(zc::mv(identifiers));

  auto pathIds = modulePath->getIdentifiers();
  ZC_EXPECT(pathIds.size() == 2, "ModulePath should have 2 identifiers");
  ZC_EXPECT(pathIds[0] == "std", "First identifier should be 'std'");
  ZC_EXPECT(pathIds[1] == "io", "Second identifier should be 'io'");
}

ZC_TEST("ModuleTest: ModulePathToString") {
  using namespace zomlang::compiler::ast::factory;
  zc::Vector<zc::String> identifiers;
  identifiers.add(zc::str("std"));
  identifiers.add(zc::str("collections"));
  identifiers.add(zc::str("vector"));
  auto modulePath = createModulePath(zc::mv(identifiers));

  auto pathString = modulePath->toString();
  ZC_EXPECT(pathString == "std.collections.vector", "ModulePath toString should join with dots");
}

ZC_TEST("ModuleTest: SingleIdentifierModulePath") {
  using namespace zomlang::compiler::ast::factory;
  zc::Vector<zc::String> identifiers;
  identifiers.add(zc::str("math"));
  auto modulePath = createModulePath(zc::mv(identifiers));

  auto pathIds = modulePath->getIdentifiers();
  ZC_EXPECT(pathIds.size() == 1, "ModulePath should have 1 identifier");
  ZC_EXPECT(pathIds[0] == "math", "Identifier should be 'math'");
  ZC_EXPECT(modulePath->toString() == "math",
            "Single identifier path should be just the identifier");
}

// ================================================================================
// ImportDeclaration Tests
ZC_TEST("ModuleTest: CreateImportDeclaration") {
  using namespace zomlang::compiler::ast::factory;
  zc::Vector<zc::String> identifiers;
  identifiers.add(zc::str("std"));
  identifiers.add(zc::str("io"));
  auto modulePath = createModulePath(zc::mv(identifiers));
  auto importDecl = createImportDeclaration(zc::mv(modulePath));

  ZC_EXPECT(importDecl->getModulePath().toString() == "std.io",
            "Import should have correct module path");
  ZC_EXPECT(importDecl->getAlias() == zc::none, "Import without alias should return none");
}

ZC_TEST("ModuleTest: CreateImportDeclarationWithAlias") {
  using namespace zomlang::compiler::ast::factory;
  zc::Vector<zc::String> identifiers;
  identifiers.add(zc::str("std"));
  identifiers.add(zc::str("collections"));
  auto modulePath = createModulePath(zc::mv(identifiers));
  auto importDecl = createImportDeclaration(zc::mv(modulePath), zc::str("collections"));

  ZC_EXPECT(importDecl->getModulePath().toString() == "std.collections",
            "Import should have correct module path");
  ZC_EXPECT(importDecl->getAlias() != zc::none, "Import with alias should not return none");
  ZC_IF_SOME(alias, importDecl->getAlias()) {
    ZC_EXPECT(alias == "collections", "Import should have correct alias");
  }
}

// ================================================================================
// ExportDeclaration Tests
ZC_TEST("ModuleTest: CreateSimpleExportDeclaration") {
  using namespace zomlang::compiler::ast::factory;
  auto exportDecl = createExportDeclaration(zc::str("myFunction"));

  ZC_EXPECT(exportDecl->getIdentifier() == "myFunction", "Export should have correct identifier");
  ZC_EXPECT(!exportDecl->isRename(), "Simple export should not be a rename");
  ZC_EXPECT(exportDecl->getAlias() == zc::none, "Simple export should have no alias");
  ZC_EXPECT(exportDecl->getModulePath() == zc::none, "Simple export should have no module path");
}

ZC_TEST("ModuleTest: CreateRenameExportDeclaration") {
  using namespace zomlang::compiler::ast::factory;
  zc::Vector<zc::String> identifiers;
  identifiers.add(zc::str("utils"));
  identifiers.add(zc::str("helpers"));
  auto modulePath = createModulePath(zc::mv(identifiers));
  auto exportDecl =
      createExportDeclaration(zc::str("originalName"), zc::str("newName"), zc::mv(modulePath));

  ZC_EXPECT(exportDecl->getIdentifier() == "originalName",
            "Rename export should have correct identifier");
  ZC_EXPECT(exportDecl->isRename(), "Rename export should be marked as rename");
  ZC_EXPECT(exportDecl->getAlias() != zc::none, "Rename export should have alias");
  ZC_IF_SOME(alias, exportDecl->getAlias()) {
    ZC_EXPECT(alias == "newName", "Rename export should have correct alias");
  }
  ZC_EXPECT(exportDecl->getModulePath() != zc::none, "Rename export should have module path");
  ZC_IF_SOME(modulePath, exportDecl->getModulePath()) {
    ZC_EXPECT(modulePath.toString() == "utils.helpers",
              "Rename export should have correct module path");
  }
}

ZC_TEST("ModuleTest: ExportDeclarationEdgeCases") {
  using namespace zomlang::compiler::ast::factory;

  // Test with empty identifier
  auto exportDecl1 = createExportDeclaration(zc::str(""));
  ZC_EXPECT(exportDecl1->getIdentifier() == "", "Export should handle empty identifier");

  // Test with special characters in identifier
  auto exportDecl2 = createExportDeclaration(zc::str("_private$var"));
  ZC_EXPECT(exportDecl2->getIdentifier() == "_private$var",
            "Export should handle special characters");
}

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang