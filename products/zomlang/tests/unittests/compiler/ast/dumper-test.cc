#include "zomlang/compiler/ast/dumper.h"

#include "zc/core/io.h"
#include "zc/core/string.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/ast/expression.h"
#include "zomlang/compiler/ast/factory.h"
#include "zomlang/compiler/ast/module.h"
#include "zomlang/compiler/ast/type.h"

namespace zomlang {
namespace compiler {
namespace ast {

namespace {

class MockOutputStream : public zc::OutputStream {
public:
  void write(zc::ArrayPtr<const zc::byte> data) override {
    buffer.addAll(data.begin(), data.end());
  }

  zc::String getBuffer() const {
    if (buffer.size() == 0) { return zc::str(""); }
    // Create a null-terminated string from the buffer
    zc::Vector<char> charBuffer;
    charBuffer.addAll(reinterpret_cast<const char*>(buffer.begin()),
                      reinterpret_cast<const char*>(buffer.end()));
    charBuffer.add('\0');
    return zc::str(charBuffer.begin());
  }

private:
  zc::Vector<zc::byte> buffer;
};

}  // namespace

ZC_TEST("ASTDumper.DumpSourceFileText") {
  MockOutputStream output;
  ASTDumper dumper(output, DumpFormat::kTEXT);

  zc::Vector<zc::Own<zomlang::compiler::ast::Statement>> statements;
  zc::Vector<zc::Own<BindingElement>> bindings;
  // Create a proper BindingElement with an identifier
  auto identifier = ast::factory::createIdentifier(zc::str("testVar"));
  auto bindingElement = ast::factory::createBindingElement(zc::mv(identifier));
  bindings.add(zc::mv(bindingElement));

  auto variableDecl = ast::factory::createVariableDeclaration(zc::mv(bindings));
  statements.add(zc::mv(variableDecl));

  auto sourceFile = ast::factory::createSourceFile(zc::str("test.zom"), zc::mv(statements));

  dumper.dumpSourceFile(*sourceFile);

  zc::String outputStr = output.getBuffer();
  ZC_ASSERT(outputStr.contains("SourceFile {"));
  ZC_ASSERT(outputStr.contains("fileName: test.zom"));
  ZC_ASSERT(outputStr.contains("statements:"));
}

ZC_TEST("ASTDumper.DumpNumericLiteral") {
  zc::Own<NumericLiteral> expr = ast::factory::createNumericLiteral(123);
  MockOutputStream output;
  ASTDumper dumper(output, DumpFormat::kTEXT);
  dumper.dump(*expr);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result == "NumericLiteral(123)");
}

ZC_TEST("ASTDumper.DumpBinaryExpression") {
  auto lhs = ast::factory::createNumericLiteral(10);
  auto rhs = ast::factory::createNumericLiteral(20);
  auto op = ast::factory::createAddOperator();
  auto binExpr = ast::factory::createBinaryExpression(zc::mv(lhs), zc::mv(op), zc::mv(rhs));
  MockOutputStream output;
  ASTDumper dumper(output, DumpFormat::kTEXT);
  dumper.dump(*binExpr);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result == "BinaryExpression(Add, NumericLiteral(10), NumericLiteral(20))");
}

ZC_TEST("ASTDumper.DumpSourceFileJson") {
  MockOutputStream output;
  ASTDumper dumper(output, DumpFormat::kJSON);

  zc::Vector<zc::Own<Statement>> statements;
  zc::Vector<zc::Own<BindingElement>> bindings;
  // Create a proper BindingElement with an identifier
  auto identifier = ast::factory::createIdentifier(zc::str("testVar"));
  auto bindingElement = ast::factory::createBindingElement(zc::mv(identifier));
  bindings.add(zc::mv(bindingElement));

  auto variableDecl = ast::factory::createVariableDeclaration(zc::mv(bindings));
  statements.add(zc::mv(variableDecl));

  auto sourceFile = ast::factory::createSourceFile(zc::str("test.zom"), zc::mv(statements));

  dumper.dumpSourceFile(*sourceFile);

  zc::String outputStr = output.getBuffer();
  ZC_ASSERT(outputStr.contains("\"node\": \"SourceFile\""));
  ZC_ASSERT(outputStr.contains("\"fileName\": \"test.zom\""));
  ZC_ASSERT(outputStr.contains("\"children\": ["));
}

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang