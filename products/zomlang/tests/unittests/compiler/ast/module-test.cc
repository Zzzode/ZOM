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
  auto stringLiteral = createStringLiteral("std.io"_zc);
  auto modulePath = createModulePath(zc::mv(stringLiteral));

  const auto& pathString = modulePath->getStringLiteral();
  ZC_EXPECT(pathString.getValue() == "std.io", "ModulePath should have correct string literal");
}

ZC_TEST("ModuleTest: ModulePathToString") {
  using namespace zomlang::compiler::ast::factory;
  auto stringLiteral = createStringLiteral("std.collections.vector"_zc);
  auto modulePath = createModulePath(zc::mv(stringLiteral));

  const auto& pathString = modulePath->getStringLiteral();
  ZC_EXPECT(pathString.getValue() == "std.collections.vector",
            "ModulePath should have correct string literal");
}

ZC_TEST("ModuleTest: SingleIdentifierModulePath") {
  using namespace zomlang::compiler::ast::factory;
  auto stringLiteral = createStringLiteral("math"_zc);
  auto modulePath = createModulePath(zc::mv(stringLiteral));

  const auto& pathString = modulePath->getStringLiteral();
  ZC_EXPECT(pathString.getValue() == "math", "ModulePath should have correct string literal");
}

// ================================================================================
// ImportDeclaration Tests
ZC_TEST("ModuleTest: CreateImportDeclaration") {
  using namespace zomlang::compiler::ast::factory;
  auto stringLiteral = createStringLiteral("std.io"_zc);
  auto modulePath = createModulePath(zc::mv(stringLiteral));
  auto importDecl = createImportDeclaration(zc::mv(modulePath));

  const auto& modulePathRef = importDecl->getModulePath();
  const auto& pathString = modulePathRef.getStringLiteral();
  ZC_EXPECT(pathString.getValue() == "std.io", "Import should have correct module path");
  ZC_EXPECT(importDecl->getAlias() == zc::none, "Import without alias should return none");
}

ZC_TEST("ModuleTest: CreateImportDeclarationWithAlias") {
  using namespace zomlang::compiler::ast::factory;
  auto stringLiteral = createStringLiteral("std.collections"_zc);
  auto modulePath = createModulePath(zc::mv(stringLiteral));
  auto importDecl = createImportDeclaration(zc::mv(modulePath), createIdentifier("collections"_zc));

  const auto& modulePathRef = importDecl->getModulePath();
  const auto& pathString = modulePathRef.getStringLiteral();
  ZC_EXPECT(pathString.getValue() == "std.collections", "Import should have correct module path");
  ZC_EXPECT(importDecl->getAlias() != zc::none, "Import with alias should not return none");
  ZC_IF_SOME(alias, importDecl->getAlias()) {
    ZC_EXPECT(alias.getText() == "collections", "Import should have correct alias");
  }
}

// ================================================================================
// ExportDeclaration Tests
ZC_TEST("ModuleTest: CreateSimpleExportDeclaration") {
  using namespace zomlang::compiler::ast::factory;
  auto exportDecl = createExportDeclaration(createIdentifier("myFunction"_zc), zc::none);

  // The export path should be a simple identifier
  auto& exportPath = exportDecl->getExportPath();
  ZC_EXPECT(exportPath.getKind() == SyntaxKind::Identifier, "Export path should be an Identifier");
  ZC_EXPECT(exportDecl->getAlias() == zc::none, "Simple export should have no alias");
}

ZC_TEST("ModuleTest: CreateExportDeclarationWithAlias") {
  using namespace zomlang::compiler::ast::factory;
  auto exportDecl =
      createExportDeclaration(createIdentifier("originalName"_zc), createIdentifier("newName"_zc));

  // The export path should be a simple identifier
  auto& exportPath = exportDecl->getExportPath();
  ZC_EXPECT(exportPath.getKind() == SyntaxKind::Identifier, "Export path should be an Identifier");

  ZC_EXPECT(exportDecl->getAlias() != zc::none, "Export with alias should have alias");
  ZC_IF_SOME(alias, exportDecl->getAlias()) {
    ZC_EXPECT(alias.getText() == "newName", "Export should have correct alias");
  }
}

ZC_TEST("ModuleTest: CreateDotSeparatedExportDeclaration") {
  using namespace zomlang::compiler::ast::factory;

  // Create a.b.c export path using PropertyAccessExpression
  auto baseId = createIdentifier("a"_zc);
  auto bId = createIdentifier("b"_zc);
  auto cId = createIdentifier("c"_zc);

  auto ab = createPropertyAccessExpression(zc::mv(baseId), zc::mv(bId), false);
  auto abc = createPropertyAccessExpression(zc::mv(ab), zc::mv(cId), false);

  auto exportDecl = createExportDeclaration(zc::mv(abc), zc::none);

  // The export path should be a PropertyAccessExpression
  auto& exportPath = exportDecl->getExportPath();
  ZC_EXPECT(exportPath.getKind() == SyntaxKind::PropertyAccessExpression,
            "Export path should be a PropertyAccessExpression");
  ZC_EXPECT(exportDecl->getAlias() == zc::none, "Dot-separated export should have no alias");
}

ZC_TEST("ModuleTest: ExportDeclarationEdgeCases") {
  using namespace zomlang::compiler::ast::factory;

  // Test empty identifier name
  auto exportDecl1 = createExportDeclaration(createIdentifier(""_zc), zc::none);
  auto& exportPath1 = exportDecl1->getExportPath();
  ZC_EXPECT(exportPath1.getKind() == SyntaxKind::Identifier, "Export path should be an Identifier");

  // Test special characters in identifier
  auto exportDecl2 = createExportDeclaration(createIdentifier("_private$var"_zc), zc::none);
  auto& exportPath2 = exportDecl2->getExportPath();
  ZC_EXPECT(exportPath2.getKind() == SyntaxKind::Identifier, "Export path should be an Identifier");
}

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
