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
  zc::Vector<zc::Own<ast::Identifier>> identifiers;
  identifiers.add(createIdentifier(zc::str("std")));
  identifiers.add(createIdentifier(zc::str("io")));
  auto modulePath = createModulePath(zc::mv(identifiers));

  const auto& pathIds = modulePath->getIdentifiers();
  ZC_EXPECT(pathIds.size() == 2, "ModulePath should have 2 identifiers");
  ZC_EXPECT(pathIds[0].getName() == "std", "First identifier should be 'std'");
  ZC_EXPECT(pathIds[1].getName() == "io", "Second identifier should be 'io'");
}

ZC_TEST("ModuleTest: ModulePathToString") {
  using namespace zomlang::compiler::ast::factory;
  zc::Vector<zc::Own<ast::Identifier>> identifiers;
  identifiers.add(createIdentifier(zc::str("std")));
  identifiers.add(createIdentifier(zc::str("collections")));
  identifiers.add(createIdentifier(zc::str("vector")));
  auto modulePath = createModulePath(zc::mv(identifiers));

  // Note: toString() method needs to be implemented in ModulePath class
  // For now, we'll test the identifiers directly
  const auto& pathIds = modulePath->getIdentifiers();
  ZC_EXPECT(pathIds.size() == 3, "ModulePath should have 3 identifiers");
  ZC_EXPECT(pathIds[0].getName() == "std", "First identifier should be 'std'");
  ZC_EXPECT(pathIds[1].getName() == "collections", "Second identifier should be 'collections'");
  ZC_EXPECT(pathIds[2].getName() == "vector", "Third identifier should be 'vector'");
}

ZC_TEST("ModuleTest: SingleIdentifierModulePath") {
  using namespace zomlang::compiler::ast::factory;
  zc::Vector<zc::Own<ast::Identifier>> identifiers;
  identifiers.add(createIdentifier(zc::str("math")));
  auto modulePath = createModulePath(zc::mv(identifiers));

  const auto& pathIds = modulePath->getIdentifiers();
  ZC_EXPECT(pathIds.size() == 1, "ModulePath should have 1 identifier");
  ZC_EXPECT(pathIds[0].getName() == "math", "Identifier should be 'math'");
}

// ================================================================================
// ImportDeclaration Tests
ZC_TEST("ModuleTest: CreateImportDeclaration") {
  using namespace zomlang::compiler::ast::factory;
  zc::Vector<zc::Own<ast::Identifier>> identifiers;
  identifiers.add(createIdentifier(zc::str("std")));
  identifiers.add(createIdentifier(zc::str("io")));
  auto modulePath = createModulePath(zc::mv(identifiers));
  auto importDecl = createImportDeclaration(zc::mv(modulePath));

  const auto& modulePathRef = importDecl->getModulePath();
  const auto& pathIds = modulePathRef.getIdentifiers();
  ZC_EXPECT(pathIds.size() == 2, "Import should have correct module path size");
  ZC_EXPECT(pathIds[0].getName() == "std", "First identifier should be 'std'");
  ZC_EXPECT(pathIds[1].getName() == "io", "Second identifier should be 'io'");
  ZC_EXPECT(importDecl->getAlias() == zc::none, "Import without alias should return none");
}

ZC_TEST("ModuleTest: CreateImportDeclarationWithAlias") {
  using namespace zomlang::compiler::ast::factory;
  zc::Vector<zc::Own<ast::Identifier>> identifiers;
  identifiers.add(createIdentifier(zc::str("std")));
  identifiers.add(createIdentifier(zc::str("collections")));
  auto modulePath = createModulePath(zc::mv(identifiers));
  auto importDecl =
      createImportDeclaration(zc::mv(modulePath), createIdentifier(zc::str("collections")));

  const auto& modulePathRef = importDecl->getModulePath();
  const auto& pathIds = modulePathRef.getIdentifiers();
  ZC_EXPECT(pathIds.size() == 2, "Import should have correct module path size");
  ZC_EXPECT(pathIds[0].getName() == "std", "First identifier should be 'std'");
  ZC_EXPECT(pathIds[1].getName() == "collections", "Second identifier should be 'collections'");
  ZC_EXPECT(importDecl->getAlias() != zc::none, "Import with alias should not return none");
  ZC_IF_SOME(alias, importDecl->getAlias()) {
    ZC_EXPECT(alias.getName() == "collections", "Import should have correct alias");
  }
}

// ================================================================================
// ExportDeclaration Tests
ZC_TEST("ModuleTest: CreateSimpleExportDeclaration") {
  using namespace zomlang::compiler::ast::factory;
  auto exportDecl = createExportDeclaration(createIdentifier(zc::str("myFunction")));

  ZC_EXPECT(exportDecl->getIdentifier().getName() == "myFunction",
            "Export should have correct identifier");
  ZC_EXPECT(!exportDecl->isRename(), "Simple export should not be a rename");
  ZC_EXPECT(exportDecl->getAlias() == zc::none, "Simple export should have no alias");
  ZC_EXPECT(exportDecl->getModulePath() == zc::none, "Simple export should have no module path");
}

ZC_TEST("ModuleTest: CreateRenameExportDeclaration") {
  using namespace zomlang::compiler::ast::factory;
  zc::Vector<zc::Own<ast::Identifier>> identifiers;
  identifiers.add(createIdentifier(zc::str("utils")));
  identifiers.add(createIdentifier(zc::str("helpers")));
  auto modulePath = createModulePath(zc::mv(identifiers));
  auto exportDecl =
      createExportDeclaration(createIdentifier(zc::str("originalName")),
                              createIdentifier(zc::str("newName")), zc::mv(modulePath));

  ZC_EXPECT(exportDecl->getIdentifier().getName() == "originalName",
            "Rename export should have correct identifier");
  ZC_EXPECT(exportDecl->isRename(), "Rename export should be marked as rename");
  ZC_EXPECT(exportDecl->getAlias() != zc::none, "Rename export should have alias");
  ZC_IF_SOME(alias, exportDecl->getAlias()) {
    ZC_EXPECT(alias.getName() == "newName", "Rename export should have correct alias");
  }
  ZC_EXPECT(exportDecl->getModulePath() != zc::none, "Rename export should have module path");
  ZC_IF_SOME(modulePathRef, exportDecl->getModulePath()) {
    const auto& pathIds = modulePathRef.getIdentifiers();
    ZC_EXPECT(pathIds.size() == 2, "Module path should have 2 identifiers");
    ZC_EXPECT(pathIds[0].getName() == "utils", "First identifier should be 'utils'");
    ZC_EXPECT(pathIds[1].getName() == "helpers", "Second identifier should be 'helpers'");
  }
}

ZC_TEST("ModuleTest: ExportDeclarationEdgeCases") {
  using namespace zomlang::compiler::ast::factory;

  // Test empty identifier
  auto exportDecl1 = createExportDeclaration(createIdentifier(zc::str("")));
  ZC_EXPECT(exportDecl1->getIdentifier().getName() == "", "Empty identifier should be preserved");

  // Test identifier with special characters
  auto exportDecl2 = createExportDeclaration(createIdentifier(zc::str("_private$var")));
  ZC_EXPECT(exportDecl2->getIdentifier().getName() == "_private$var",
            "Special characters in identifier should be preserved");
}

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
