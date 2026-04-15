#include "zomlang/compiler/ast/dumper.h"

#include "zc/core/exception.h"
#include "zc/core/io.h"
#include "zc/core/string.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/ast/expression.h"
#include "zomlang/compiler/ast/factory.h"
#include "zomlang/compiler/ast/kinds.h"
#include "zomlang/compiler/ast/module.h"
#include "zomlang/compiler/ast/serializer.h"
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

enum class TestSerializerType { kTEXT, kJSON, kXML };

zc::Own<Serializer> createTestSerializer(MockOutputStream& output, TestSerializerType type) {
  switch (type) {
    case TestSerializerType::kTEXT:
      return zc::heap<TextSerializer>(output);
    case TestSerializerType::kJSON:
      return zc::heap<JSONSerializer>(output);
    case TestSerializerType::kXML:
      return zc::heap<XMLSerializer>(output);
  }
  ZC_UNREACHABLE;
}

}  // namespace

ZC_TEST("ASTDumper.DumpSourceFileText") {
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));

  zc::Vector<zc::Own<zomlang::compiler::ast::Statement>> statements;
  zc::Vector<zc::Own<VariableDeclaration>> decls;
  // Create a binding element for the variable declaration list
  auto identifier = ast::factory::createIdentifier("testVar"_zc);
  auto variable = ast::factory::createVariableDeclaration(zc::mv(identifier));
  decls.add(zc::mv(variable));

  auto variableDecl = ast::factory::createVariableDeclarationList(zc::mv(decls));
  auto variableStmt = ast::factory::createVariableStatement(zc::mv(variableDecl));
  statements.add(zc::mv(variableStmt));

  auto sourceFile =
      ast::factory::createSourceFile(zc::str("test.zom"), zc::none, zc::mv(statements));

  dumper.dump(*sourceFile);

  zc::String outputStr = output.getBuffer();
  ZC_ASSERT(outputStr.contains("SourceFile {"));
  ZC_ASSERT(outputStr.contains("fileName: test.zom"));
  ZC_ASSERT(outputStr.contains("statements:"));
}

ZC_TEST("ASTDumper.DumpIntegerLiteral") {
  zc::Own<IntegerLiteral> expr = ast::factory::createIntegerLiteral(123);
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*expr);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result == "IntegerLiteral {\n  value: 123\n}\n");
}

ZC_TEST("ASTDumper.DumpBinaryExpression") {
  auto lhs = ast::factory::createIntegerLiteral(10);
  auto rhs = ast::factory::createIntegerLiteral(20);
  auto op = ast::factory::createTokenNode(SyntaxKind::Plus);
  auto binExpr = ast::factory::createBinaryExpression(zc::mv(lhs), zc::mv(op), zc::mv(rhs));
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*binExpr);
  zc::String result = output.getBuffer();
  const zc::StringPtr expectedOutput =
      "BinaryExpression {\n"
      "  left:\n"
      "    IntegerLiteral {\n"
      "      value: 10\n"
      "    }\n"
      "  operator:\n"
      "    TokenNode {\n"
      "      symbol: +\n"
      "    }\n"
      "  right:\n"
      "    IntegerLiteral {\n"
      "      value: 20\n"
      "    }\n"
      "}\n";
  ZC_ASSERT(result == expectedOutput);
}

ZC_TEST("ASTDumper.DumpSourceFileJson") {
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kJSON);
  ASTDumper dumper(zc::mv(serializer));

  zc::Vector<zc::Own<Statement>> statements;
  zc::Vector<zc::Own<VariableDeclaration>> bindings;
  // Create a proper BindingElement with an identifier
  auto identifier = ast::factory::createIdentifier("testVar"_zc);
  auto bindingElement = ast::factory::createVariableDeclaration(zc::mv(identifier));
  bindings.add(zc::mv(bindingElement));

  auto variableDecl = ast::factory::createVariableDeclarationList(zc::mv(bindings));
  auto variableStmt = ast::factory::createVariableStatement(zc::mv(variableDecl));
  statements.add(zc::mv(variableStmt));

  auto sourceFile =
      ast::factory::createSourceFile(zc::str("test.zom"), zc::none, zc::mv(statements));

  dumper.dump(*sourceFile);

  zc::String outputStr = output.getBuffer();
  ZC_ASSERT(outputStr.contains("\"node\": \"SourceFile\""));
  ZC_ASSERT(outputStr.contains("\"fileName\": \"test.zom\""));
  ZC_ASSERT(outputStr.contains("\"statements\": ["));
}

// Test XML format
ZC_TEST("ASTDumper.DumpSourceFileXML") {
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kXML);
  ASTDumper dumper(zc::mv(serializer));

  zc::Vector<zc::Own<Statement>> statements;
  zc::Vector<zc::Own<VariableDeclaration>> bindings;
  auto identifier = ast::factory::createIdentifier("testVar"_zc);
  auto bindingElement = ast::factory::createVariableDeclaration(zc::mv(identifier));
  bindings.add(zc::mv(bindingElement));

  auto variableDecl = ast::factory::createVariableDeclarationList(zc::mv(bindings));
  auto variableStmt = ast::factory::createVariableStatement(zc::mv(variableDecl));
  statements.add(zc::mv(variableStmt));

  auto sourceFile =
      ast::factory::createSourceFile(zc::str("test.zom"), zc::none, zc::mv(statements));

  dumper.dump(*sourceFile);

  zc::String outputStr = output.getBuffer();
  ZC_ASSERT(outputStr.contains("<SourceFile>"));
  ZC_ASSERT(outputStr.contains("<fileName>test.zom</fileName>"));
  ZC_ASSERT(outputStr.contains("<statements>"));
  ZC_ASSERT(outputStr.contains("</SourceFile>"));
}

// Test StringLiteral in all formats
ZC_TEST("ASTDumper.DumpStringLiteralText") {
  auto expr = ast::factory::createStringLiteral("hello world"_zc);
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*expr);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("StringLiteral {"));
  ZC_ASSERT(result.contains("value: hello world"));
}

ZC_TEST("ASTDumper.DumpStringLiteralJSON") {
  auto expr = ast::factory::createStringLiteral("hello world"_zc);
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kJSON);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*expr);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("\"node\": \"StringLiteral\""));
  ZC_ASSERT(result.contains("\"value\": \"hello world\""));
}

ZC_TEST("ASTDumper.DumpStringLiteralXML") {
  auto expr = ast::factory::createStringLiteral("hello world"_zc);
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kXML);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*expr);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("<StringLiteral>"));
  ZC_ASSERT(result.contains("<value>hello world</value>"));
  ZC_ASSERT(result.contains("</StringLiteral>"));
}

// Test BooleanLiteral in all formats
ZC_TEST("ASTDumper.DumpBooleanLiteralText") {
  auto expr = ast::factory::createBooleanLiteral(true);
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*expr);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("BooleanLiteral {"));
  ZC_ASSERT(result.contains("value: true"));
}

ZC_TEST("ASTDumper.DumpBooleanLiteralJSON") {
  auto expr = ast::factory::createBooleanLiteral(false);
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kJSON);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*expr);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("\"node\": \"BooleanLiteral\""));
  ZC_ASSERT(result.contains("\"value\": \"false\""));
}

ZC_TEST("ASTDumper.DumpBooleanLiteralXML") {
  auto expr = ast::factory::createBooleanLiteral(true);
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kXML);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*expr);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("<BooleanLiteral>"));
  ZC_ASSERT(result.contains("<value>true</value>"));
  ZC_ASSERT(result.contains("</BooleanLiteral>"));
}

// Test NullLiteral in all formats
ZC_TEST("ASTDumper.DumpNullLiteralText") {
  auto expr = ast::factory::createNullLiteral();
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*expr);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("NullLiteral {"));
}

ZC_TEST("ASTDumper.DumpNullLiteralJSON") {
  auto expr = ast::factory::createNullLiteral();
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kJSON);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*expr);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("\"node\": \"NullLiteral\""));
}

ZC_TEST("ASTDumper.DumpNullLiteralXML") {
  auto expr = ast::factory::createNullLiteral();
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kXML);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*expr);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("<NullLiteral>"));
  ZC_ASSERT(result.contains("</NullLiteral>"));
}

// Test Identifier in all formats
ZC_TEST("ASTDumper.DumpIdentifierText") {
  auto expr = ast::factory::createIdentifier("myVariable"_zc);
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*expr);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("Identifier {"));
  ZC_ASSERT(result.contains("name: myVariable"));
}

ZC_TEST("ASTDumper.DumpIdentifierJSON") {
  auto expr = ast::factory::createIdentifier("myVariable"_zc);
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kJSON);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*expr);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("\"node\": \"Identifier\""));
  ZC_ASSERT(result.contains("\"name\": \"myVariable\""));
}

ZC_TEST("ASTDumper.DumpIdentifierXML") {
  auto expr = ast::factory::createIdentifier("myVariable"_zc);
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kXML);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*expr);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("<Identifier>"));
  ZC_ASSERT(result.contains("<name>myVariable</name>"));
  ZC_ASSERT(result.contains("</Identifier>"));
}

// Test UnaryExpression in all formats
ZC_TEST("ASTDumper.DumpUnaryExpressionText") {
  auto operand = ast::factory::createFloatLiteral(42.0);
  auto expr = ast::factory::createPrefixUnaryExpression(ast::SyntaxKind::Minus, zc::mv(operand));
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*expr);
  zc::String result = output.getBuffer();
  const zc::StringPtr expected =
      "PrefixUnaryExpression {\n"
      "  operator:\n"
      "    TokenNode {\n"
      "      symbol: -\n"
      "    }\n"
      "  operand:\n"
      "    FloatLiteral {\n"
      "      value: 42\n"
      "    }\n"
      "}\n";
  ZC_ASSERT(result == expected);
}

ZC_TEST("ASTDumper.DumpUnaryExpressionJSON") {
  auto operand = ast::factory::createFloatLiteral(42.0);
  auto expr = ast::factory::createPrefixUnaryExpression(ast::SyntaxKind::Plus, zc::mv(operand));
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kJSON);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*expr);
  zc::String result = output.getBuffer();
  const zc::StringPtr expected =
      "{\n"
      "  \"node\": \"PrefixUnaryExpression\",\n"
      "  \"operator\": {\n"
      "    \"node\": \"TokenNode\",\n"
      "    \"symbol\": \"+\"\n"
      "  },\n"
      "  \"operand\": {\n"
      "    \"node\": \"FloatLiteral\",\n"
      "    \"value\": \"42\"\n"
      "  }\n"
      "}";
  ZC_ASSERT(result == expected);
}

ZC_TEST("ASTDumper.DumpUnaryExpressionXML") {
  auto operand = ast::factory::createFloatLiteral(42.0);
  auto expr =
      ast::factory::createPrefixUnaryExpression(ast::SyntaxKind::Exclamation, zc::mv(operand));
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kXML);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*expr);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("<PrefixUnaryExpression>"));
  ZC_ASSERT(result.contains("<symbol>!</symbol>"));
  ZC_ASSERT(result.contains("</PrefixUnaryExpression>"));
}

// Test CallExpression in all formats
ZC_TEST("ASTDumper.DumpCallExpressionText") {
  auto callee = ast::factory::createIdentifier("func"_zc);
  zc::Vector<zc::Own<Expression>> args;
  args.add(ast::factory::createFloatLiteral(1.0));
  args.add(ast::factory::createStringLiteral("test"_zc));
  zc::Vector<zc::Own<ast::TypeNode>> typeArgs;
  auto expr =
      ast::factory::createCallExpression(zc::mv(callee), zc::none, zc::mv(typeArgs), zc::mv(args));
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*expr);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("CallExpression {"));
}

ZC_TEST("ASTDumper.DumpCallExpressionJSON") {
  auto callee = ast::factory::createIdentifier("func"_zc);
  zc::Vector<zc::Own<Expression>> args;
  args.add(ast::factory::createFloatLiteral(1.0));
  zc::Vector<zc::Own<ast::TypeNode>> typeArgs;
  auto expr =
      ast::factory::createCallExpression(zc::mv(callee), zc::none, zc::mv(typeArgs), zc::mv(args));
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kJSON);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*expr);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("\"node\": \"CallExpression\""));
}

ZC_TEST("ASTDumper.DumpCallExpressionXML") {
  auto callee = ast::factory::createIdentifier("func"_zc);
  zc::Vector<zc::Own<Expression>> args;
  zc::Vector<zc::Own<TypeNode>> typeArgs;
  auto expr =
      ast::factory::createCallExpression(zc::mv(callee), zc::none, zc::mv(typeArgs), zc::mv(args));
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kXML);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*expr);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("<CallExpression>"));
  ZC_ASSERT(result.contains("</CallExpression>"));
}

// Test ConditionalExpression in all formats
ZC_TEST("ASTDumper.DumpConditionalExpressionText") {
  auto test = ast::factory::createBooleanLiteral(true);
  auto consequent = ast::factory::createStringLiteral("yes"_zc);
  auto alternate = ast::factory::createStringLiteral("no"_zc);
  auto expr = ast::factory::createConditionalExpression(zc::mv(test), zc::none, zc::mv(consequent),
                                                        zc::none, zc::mv(alternate));
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*expr);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("ConditionalExpression {"));
}

ZC_TEST("ASTDumper.DumpConditionalExpressionJSON") {
  auto test = ast::factory::createBooleanLiteral(true);
  auto consequent = ast::factory::createStringLiteral("yes"_zc);
  auto alternate = ast::factory::createStringLiteral("no"_zc);
  auto expr = ast::factory::createConditionalExpression(zc::mv(test), zc::none, zc::mv(consequent),
                                                        zc::none, zc::mv(alternate));
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kJSON);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*expr);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("\"node\": \"ConditionalExpression\""));
}

ZC_TEST("ASTDumper.DumpConditionalExpressionXML") {
  auto test = ast::factory::createBooleanLiteral(true);
  auto consequent = ast::factory::createStringLiteral("yes"_zc);
  auto alternate = ast::factory::createStringLiteral("no"_zc);
  auto expr = ast::factory::createConditionalExpression(zc::mv(test), zc::none, zc::mv(consequent),
                                                        zc::none, zc::mv(alternate));
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kXML);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*expr);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("<ConditionalExpression>"));
  ZC_ASSERT(result.contains("</ConditionalExpression>"));
}

// Test Type dumping in all formats
ZC_TEST("ASTDumper.DumpPredefinedTypeText") {
  auto type = ast::factory::createPredefinedType("str"_zc);
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*type);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("StrTypeNode {"));
  ZC_ASSERT(result.contains("name: str"));
}

ZC_TEST("ASTDumper.DumpPredefinedTypeJSON") {
  auto type = ast::factory::createPredefinedType("i32"_zc);
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kJSON);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*type);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("\"node\": \"I32TypeNode\""));
  ZC_ASSERT(result.contains("\"name\": \"i32\""));
}

ZC_TEST("ASTDumper.DumpPredefinedTypeXML") {
  auto type = ast::factory::createPredefinedType("bool"_zc);
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kXML);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*type);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("<BoolTypeNode>"));
  ZC_ASSERT(result.contains("<name>bool</name>"));
  ZC_ASSERT(result.contains("</BoolTypeNode>"));
}

// Test Statement dumping
ZC_TEST("ASTDumper.DumpExpressionStatementText") {
  auto expr = ast::factory::createFloatLiteral(42.0);
  auto stmt = ast::factory::createExpressionStatement(zc::mv(expr));
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*stmt);
  zc::String result = output.getBuffer();
  // Debug: print actual output
  ZC_LOG(INFO, "ExpressionStatement TEXT output:", result);
  ZC_ASSERT(result.contains("ExpressionStatement"));
}

ZC_TEST("ASTDumper.DumpExpressionStatementJSON") {
  auto expr = ast::factory::createStringLiteral("hello"_zc);
  auto stmt = ast::factory::createExpressionStatement(zc::mv(expr));
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kJSON);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*stmt);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("\"node\": \"ExpressionStatement\""));
}

ZC_TEST("ASTDumper.DumpExpressionStatementXML") {
  auto expr = ast::factory::createBooleanLiteral(false);
  auto stmt = ast::factory::createExpressionStatement(zc::mv(expr));
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kXML);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*stmt);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("<ExpressionStatement>"));
  ZC_ASSERT(result.contains("</ExpressionStatement>"));
}

// Test EmptyStatement
ZC_TEST("ASTDumper.DumpEmptyStatementText") {
  auto stmt = ast::factory::createEmptyStatement();
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*stmt);
  zc::String result = output.getBuffer();
  // Debug: print actual output
  ZC_LOG(INFO, "EmptyStatement TEXT output:", result);
  ZC_ASSERT(result.contains("EmptyStatement"));
}

ZC_TEST("ASTDumper.DumpEmptyStatementJSON") {
  auto stmt = ast::factory::createEmptyStatement();
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kJSON);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*stmt);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("\"node\": \"EmptyStatement\""));
}

ZC_TEST("ASTDumper.DumpEmptyStatementXML") {
  auto stmt = ast::factory::createEmptyStatement();
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kXML);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*stmt);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("<EmptyStatement>"));
  ZC_ASSERT(result.contains("</EmptyStatement>"));
}

// Test FunctionDeclaration dumping
ZC_TEST("ASTDumper.DumpFunctionDeclarationText") {
  auto funcName = ast::factory::createIdentifier("testFunc"_zc);
  zc::Vector<zc::Own<ast::ParameterDeclaration>> parameters;
  auto param = ast::factory::createParameterDeclaration(
      {}, zc::none, ast::factory::createIdentifier("param"_zc), zc::none, zc::none, zc::none);
  parameters.add(zc::mv(param));

  zc::Vector<zc::Own<ast::Statement>> bodyStatements;
  auto body = ast::factory::createBlockStatement(zc::mv(bodyStatements));
  zc::Vector<zc::Own<ast::TypeParameterDeclaration>> typeParams;
  auto funcDecl = ast::factory::createFunctionDeclaration(
      zc::mv(funcName), zc::mv(typeParams), zc::mv(parameters), zc::none, zc::mv(body));

  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*funcDecl);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("FunctionDeclaration {"));
  ZC_ASSERT(result.contains("name:"));
  ZC_ASSERT(result.contains("testFunc"));
}

// Test BlockStatement dumping
ZC_TEST("ASTDumper.DumpBlockStatementText") {
  zc::Vector<zc::Own<ast::Statement>> statements;
  auto expr = ast::factory::createIntegerLiteral(42);
  auto exprStmt = ast::factory::createExpressionStatement(zc::mv(expr));
  statements.add(zc::mv(exprStmt));
  auto block = ast::factory::createBlockStatement(zc::mv(statements));

  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*block);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("BlockStatement {"));
  ZC_ASSERT(result.contains("children:"));
}

// Test PostfixUnaryExpression dumping
ZC_TEST("ASTDumper.DumpPostfixUnaryExpressionText") {
  auto operand = ast::factory::createIdentifier("x"_zc);
  auto expr =
      ast::factory::createPostfixUnaryExpression(ast::SyntaxKind::PlusPlus, zc::mv(operand));

  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*expr);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("PostfixUnaryExpression {"));
  ZC_ASSERT(result.contains("operand:"));
  ZC_ASSERT(result.contains("operator:"));
}

// Test TypeReference dumping
ZC_TEST("ASTDumper.DumpTypeReferenceText") {
  auto typeName = ast::factory::createIdentifier("MyType"_zc);
  auto typeRef = ast::factory::createTypeReference(zc::mv(typeName), zc::none);

  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*typeRef);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("TypeReferenceNode {"));
  ZC_ASSERT(result.contains("name:"));
}

// Test ArrayType dumping
ZC_TEST("ASTDumper.DumpArrayTypeText") {
  auto elementType = ast::factory::createPredefinedType("i32"_zc);
  auto arrayType = ast::factory::createArrayType(zc::mv(elementType));

  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*arrayType);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("ArrayTypeNode {"));
  ZC_ASSERT(result.contains("elementType:"));
}

// Test UnionType dumping
ZC_TEST("ASTDumper.DumpUnionTypeText") {
  zc::Vector<zc::Own<ast::TypeNode>> types;
  types.add(ast::factory::createPredefinedType("str"_zc));
  types.add(ast::factory::createPredefinedType("i32"_zc));
  auto unionType = ast::factory::createUnionType(zc::mv(types));

  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*unionType);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("UnionTypeNode {"));
  ZC_ASSERT(result.contains("types:"));
}

// Test ParenthesizedExpression dumping
ZC_TEST("ASTDumper.DumpParenthesizedExpressionText") {
  auto inner = ast::factory::createIntegerLiteral(123);
  auto parenExpr = ast::factory::createParenthesizedExpression(zc::mv(inner));

  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*parenExpr);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("ParenthesizedExpression {"));
  ZC_ASSERT(result.contains("expression:"));
}

// Test JSON serialization for complex types
ZC_TEST("ASTDumper.DumpArrayTypeJSON") {
  auto elementType = ast::factory::createPredefinedType("str"_zc);
  auto arrayType = ast::factory::createArrayType(zc::mv(elementType));

  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kJSON);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*arrayType);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("\"node\": \"ArrayTypeNode\""));
  ZC_ASSERT(result.contains("\"elementType\":"));
}

// Test XML serialization for complex types
ZC_TEST("ASTDumper.DumpUnionTypeXML") {
  zc::Vector<zc::Own<ast::TypeNode>> types;
  types.add(ast::factory::createPredefinedType("bool"_zc));
  types.add(ast::factory::createPredefinedType("null"_zc));
  auto unionType = ast::factory::createUnionType(zc::mv(types));

  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kXML);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*unionType);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("<UnionTypeNode>"));
  ZC_ASSERT(result.contains("<types>"));
  ZC_ASSERT(result.contains("</UnionTypeNode>"));
}

// ============================================================================
// New dump tests for uncovered node types (TEXT format)
// ============================================================================

// Test IfStatement
ZC_TEST("ASTDumper.DumpIfStatementText") {
  auto cond = ast::factory::createBooleanLiteral(true);
  auto thenStmt = ast::factory::createEmptyStatement();
  auto elseStmt = ast::factory::createEmptyStatement();
  auto node = ast::factory::createIfStatement(zc::mv(cond), zc::mv(thenStmt), zc::mv(elseStmt));
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*node);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("IfStatement {"));
  ZC_ASSERT(result.contains("condition:"));
  ZC_ASSERT(result.contains("thenStatement:"));
  ZC_ASSERT(result.contains("elseStatement:"));
}

// Test WhileStatement
ZC_TEST("ASTDumper.DumpWhileStatementText") {
  auto cond = ast::factory::createBooleanLiteral(true);
  auto body = ast::factory::createEmptyStatement();
  auto node = ast::factory::createWhileStatement(zc::mv(cond), zc::mv(body));
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*node);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("WhileStatement {"));
  ZC_ASSERT(result.contains("condition:"));
  ZC_ASSERT(result.contains("body:"));
}

// Test ForStatement
ZC_TEST("ASTDumper.DumpForStatementText") {
  auto init = ast::factory::createEmptyStatement();
  auto cond = ast::factory::createBooleanLiteral(true);
  auto update = ast::factory::createIntegerLiteral(1);
  auto body = ast::factory::createEmptyStatement();
  auto node =
      ast::factory::createForStatement(zc::mv(init), zc::mv(cond), zc::mv(update), zc::mv(body));
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*node);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("ForStatement {"));
}

// Test LabeledStatement
ZC_TEST("ASTDumper.DumpLabeledStatementText") {
  auto label = ast::factory::createIdentifier("myLabel"_zc);
  auto stmt = ast::factory::createEmptyStatement();
  auto node = ast::factory::createLabeledStatement(zc::mv(label), zc::mv(stmt));
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*node);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("LabeledStatement {"));
  ZC_ASSERT(result.contains("label:"));
  ZC_ASSERT(result.contains("myLabel"));
}

// Test BreakStatement
ZC_TEST("ASTDumper.DumpBreakStatementText") {
  auto node = ast::factory::createBreakStatement(zc::none);
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*node);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("BreakStatement {"));
}

// Test ContinueStatement
ZC_TEST("ASTDumper.DumpContinueStatementText") {
  auto node = ast::factory::createContinueStatement(zc::none);
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*node);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("ContinueStatement {"));
}

// Test ReturnStatement
ZC_TEST("ASTDumper.DumpReturnStatementText") {
  auto node = ast::factory::createReturnStatement(zc::none);
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*node);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("ReturnStatement {"));
}

// Test ClassDeclaration
ZC_TEST("ASTDumper.DumpClassDeclarationText") {
  auto name = ast::factory::createIdentifier("MyClass"_zc);
  zc::Vector<zc::Own<ast::ClassElement>> members;
  auto node =
      ast::factory::createClassDeclaration(zc::mv(name), zc::none, zc::none, zc::mv(members));
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*node);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("ClassDeclaration {"));
  ZC_ASSERT(result.contains("MyClass"));
}

// Test InterfaceDeclaration
ZC_TEST("ASTDumper.DumpInterfaceDeclarationText") {
  auto name = ast::factory::createIdentifier("MyInterface"_zc);
  zc::Vector<zc::Own<ast::InterfaceElement>> members;
  auto node =
      ast::factory::createInterfaceDeclaration(zc::mv(name), zc::none, zc::none, zc::mv(members));
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*node);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("InterfaceDeclaration {"));
  ZC_ASSERT(result.contains("MyInterface"));
}

// Test StructDeclaration
ZC_TEST("ASTDumper.DumpStructDeclarationText") {
  auto name = ast::factory::createIdentifier("MyStruct"_zc);
  zc::Vector<zc::Own<ast::ClassElement>> members;
  auto node =
      ast::factory::createStructDeclaration(zc::mv(name), zc::none, zc::none, zc::mv(members));
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*node);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("StructDeclaration {"));
  ZC_ASSERT(result.contains("MyStruct"));
}

// Test EnumDeclaration with members
ZC_TEST("ASTDumper.DumpEnumDeclarationText") {
  auto name = ast::factory::createIdentifier("Color"_zc);
  zc::Vector<zc::Own<ast::EnumMember>> members;
  members.add(ast::factory::createEnumMember(ast::factory::createIdentifier("Red"_zc)));
  members.add(ast::factory::createEnumMember(ast::factory::createIdentifier("Green"_zc)));
  auto node = ast::factory::createEnumDeclaration(zc::mv(name), zc::mv(members));
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*node);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("EnumDeclaration {"));
  ZC_ASSERT(result.contains("Color"));
  ZC_ASSERT(result.contains("Red"));
  ZC_ASSERT(result.contains("Green"));
}

// Test ErrorDeclaration
ZC_TEST("ASTDumper.DumpErrorDeclarationText") {
  auto name = ast::factory::createIdentifier("MyError"_zc);
  zc::Vector<zc::Own<ast::Statement>> body;
  auto node = ast::factory::createErrorDeclaration(zc::mv(name), zc::mv(body));
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*node);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("ErrorDeclaration {"));
  ZC_ASSERT(result.contains("MyError"));
}

// Test AliasDeclaration
ZC_TEST("ASTDumper.DumpAliasDeclarationText") {
  auto name = ast::factory::createIdentifier("MyAlias"_zc);
  auto type = ast::factory::createPredefinedType("i32"_zc);
  auto node = ast::factory::createAliasDeclaration(zc::mv(name), zc::none, zc::mv(type));
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*node);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("AliasDeclaration {"));
  ZC_ASSERT(result.contains("MyAlias"));
}

// Test MatchStatement
ZC_TEST("ASTDumper.DumpMatchStatementText") {
  auto discriminant = ast::factory::createIdentifier("value"_zc);
  zc::Vector<zc::Own<ast::Statement>> clauses;
  auto node = ast::factory::createMatchStatement(zc::mv(discriminant), zc::mv(clauses));
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*node);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("MatchStatement {"));
}

// Test MatchClause
ZC_TEST("ASTDumper.DumpMatchClauseText") {
  auto pattern = ast::factory::createWildcardPattern();
  auto body = ast::factory::createEmptyStatement();
  auto node = ast::factory::createMatchClause(zc::mv(pattern), zc::none, zc::mv(body));
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*node);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("MatchClause {"));
}

// Test DefaultClause
ZC_TEST("ASTDumper.DumpDefaultClauseText") {
  zc::Vector<zc::Own<ast::Statement>> statements;
  auto node = ast::factory::createDefaultClause(zc::mv(statements));
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*node);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("DefaultClause {"));
}

// Test ImportDeclaration
ZC_TEST("ASTDumper.DumpImportDeclarationText") {
  zc::Vector<zc::Own<ast::Identifier>> segments;
  segments.add(ast::factory::createIdentifier("std"_zc));
  segments.add(ast::factory::createIdentifier("io"_zc));
  auto modulePath = ast::factory::createModulePath(zc::mv(segments));
  zc::Vector<zc::Own<ast::ImportSpecifier>> specifiers;
  auto node =
      ast::factory::createImportDeclaration(zc::mv(modulePath), zc::none, zc::mv(specifiers));
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*node);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("ImportDeclaration {"));
}

// Test ExportDeclaration
ZC_TEST("ASTDumper.DumpExportDeclarationText") {
  zc::Vector<zc::Own<ast::ExportSpecifier>> specifiers;
  specifiers.add(ast::factory::createExportSpecifier(ast::factory::createIdentifier("myFunc"_zc)));
  auto node = ast::factory::createExportDeclaration(zc::none, zc::mv(specifiers), zc::none);
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*node);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("ExportDeclaration {"));
}

// Test SpreadElement
ZC_TEST("ASTDumper.DumpSpreadElementText") {
  auto expr = ast::factory::createIdentifier("items"_zc);
  auto node = ast::factory::createSpreadElement(zc::mv(expr));
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*node);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("SpreadElement {"));
}

// Test ArrayLiteralExpression
ZC_TEST("ASTDumper.DumpArrayLiteralExpressionText") {
  zc::Vector<zc::Own<ast::Expression>> elements;
  elements.add(ast::factory::createIntegerLiteral(1));
  elements.add(ast::factory::createIntegerLiteral(2));
  auto node = ast::factory::createArrayLiteralExpression(zc::mv(elements), false);
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*node);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("ArrayLiteralExpression {"));
}

// Test ObjectLiteralExpression with PropertyAssignment
ZC_TEST("ASTDumper.DumpObjectLiteralExpressionText") {
  zc::Vector<zc::Own<ast::ObjectLiteralElement>> properties;
  auto propName = ast::factory::createIdentifier("key"_zc);
  auto propVal = ast::factory::createStringLiteral("value"_zc);
  properties.add(
      ast::factory::createPropertyAssignment(zc::mv(propName), zc::mv(propVal), zc::none));
  auto node = ast::factory::createObjectLiteralExpression(zc::mv(properties), false);
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*node);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("ObjectLiteralExpression {"));
}

// Test PropertyAssignment
ZC_TEST("ASTDumper.DumpPropertyAssignmentText") {
  auto name = ast::factory::createIdentifier("propName"_zc);
  auto init = ast::factory::createIntegerLiteral(42);
  auto node = ast::factory::createPropertyAssignment(zc::mv(name), zc::mv(init), zc::none);
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*node);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("PropertyAssignment {"));
  ZC_ASSERT(result.contains("propName"));
}

// Test AsExpression
ZC_TEST("ASTDumper.DumpAsExpressionText") {
  auto expr = ast::factory::createIdentifier("x"_zc);
  auto type = ast::factory::createPredefinedType("i32"_zc);
  auto node = ast::factory::createAsExpression(zc::mv(expr), zc::mv(type));
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*node);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("AsExpression {"));
}

// Test VoidExpression
ZC_TEST("ASTDumper.DumpVoidExpressionText") {
  auto expr = ast::factory::createIdentifier("x"_zc);
  auto node = ast::factory::createVoidExpression(zc::mv(expr));
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*node);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("VoidExpression {"));
}

// Test TypeOfExpression
ZC_TEST("ASTDumper.DumpTypeOfExpressionText") {
  auto expr = ast::factory::createIdentifier("x"_zc);
  auto node = ast::factory::createTypeOfExpression(zc::mv(expr));
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*node);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("TypeOfExpression {"));
}

// Test AwaitExpression
ZC_TEST("ASTDumper.DumpAwaitExpressionText") {
  auto expr = ast::factory::createIdentifier("promise"_zc);
  auto node = ast::factory::createAwaitExpression(zc::mv(expr));
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*node);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("AwaitExpression {"));
}

// Test NonNullExpression
ZC_TEST("ASTDumper.DumpNonNullExpressionText") {
  auto expr = ast::factory::createIdentifier("value"_zc);
  auto node = ast::factory::createNonNullExpression(zc::mv(expr));
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*node);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("NonNullExpression {"));
}

// Test NewExpression
ZC_TEST("ASTDumper.DumpNewExpressionText") {
  auto callee = ast::factory::createIdentifier("MyClass"_zc);
  auto node = ast::factory::createNewExpression(zc::mv(callee), zc::none, zc::none);
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*node);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("NewExpression {"));
}

// Test ThisExpression
ZC_TEST("ASTDumper.DumpThisExpressionText") {
  auto node = ast::factory::createThisExpression();
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*node);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("ThisExpression {"));
}

// Test CaptureElement
ZC_TEST("ASTDumper.DumpCaptureElementText") {
  auto id = ast::factory::createIdentifier("captured"_zc);
  auto node = ast::factory::createCaptureElement(false, zc::mv(id), false);
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*node);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("CaptureElement {"));
}

// Test FunctionExpression
ZC_TEST("ASTDumper.DumpFunctionExpressionText") {
  zc::Vector<zc::Own<ast::TypeParameterDeclaration>> typeParams;
  zc::Vector<zc::Own<ast::ParameterDeclaration>> params;
  zc::Vector<zc::Own<ast::CaptureElement>> captures;
  zc::Vector<zc::Own<ast::Statement>> bodyStmts;
  auto body = ast::factory::createBlockStatement(zc::mv(bodyStmts));
  auto node = ast::factory::createFunctionExpression(zc::mv(typeParams), zc::mv(params),
                                                     zc::mv(captures), zc::none, zc::mv(body));
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*node);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("FunctionExpression {"));
}

// Test WildcardPattern
ZC_TEST("ASTDumper.DumpWildcardPatternText") {
  auto node = ast::factory::createWildcardPattern();
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*node);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("WildcardPattern {"));
}

// Test IdentifierPattern
ZC_TEST("ASTDumper.DumpIdentifierPatternText") {
  auto id = ast::factory::createIdentifier("x"_zc);
  auto node = ast::factory::createIdentifierPattern(zc::mv(id));
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*node);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("IdentifierPattern {"));
}

// Test TuplePattern
ZC_TEST("ASTDumper.DumpTuplePatternText") {
  zc::Vector<zc::Own<ast::Pattern>> elements;
  elements.add(ast::factory::createWildcardPattern());
  auto node = ast::factory::createTuplePattern(zc::mv(elements));
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*node);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("TuplePattern {"));
}

// Test StructurePattern
ZC_TEST("ASTDumper.DumpStructurePatternText") {
  zc::Vector<zc::Own<ast::Pattern>> fields;
  fields.add(ast::factory::createWildcardPattern());
  auto node = ast::factory::createStructurePattern(zc::mv(fields));
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*node);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("StructurePattern {"));
}

// Test ArrayPattern
ZC_TEST("ASTDumper.DumpArrayPatternText") {
  zc::Vector<zc::Own<ast::Pattern>> elements;
  elements.add(ast::factory::createWildcardPattern());
  auto node = ast::factory::createArrayPattern(zc::mv(elements));
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*node);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("ArrayPattern {"));
}

// Test IsPattern
ZC_TEST("ASTDumper.DumpIsPatternText") {
  auto type = ast::factory::createPredefinedType("i32"_zc);
  auto node = ast::factory::createIsPattern(zc::mv(type));
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*node);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("IsPattern {"));
}

// Test PropertyDeclaration
ZC_TEST("ASTDumper.DumpPropertyDeclarationText") {
  auto name = ast::factory::createIdentifier("myProp"_zc);
  auto node = ast::factory::createPropertyDeclaration({}, zc::mv(name), zc::none, zc::none);
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*node);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("PropertyDeclaration {"));
  ZC_ASSERT(result.contains("myProp"));
}

// Test MethodDeclaration
ZC_TEST("ASTDumper.DumpMethodDeclarationText") {
  auto name = ast::factory::createIdentifier("myMethod"_zc);
  zc::Vector<zc::Own<ast::ParameterDeclaration>> params;
  zc::Vector<zc::Own<ast::Statement>> bodyStmts;
  auto body = ast::factory::createBlockStatement(zc::mv(bodyStmts));
  auto node = ast::factory::createMethodDeclaration({}, zc::mv(name), zc::none, zc::none,
                                                    zc::mv(params), zc::none, zc::mv(body));
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*node);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("MethodDeclaration {"));
  ZC_ASSERT(result.contains("myMethod"));
}

// Test InitDeclaration
ZC_TEST("ASTDumper.DumpInitDeclarationText") {
  zc::Vector<zc::Own<ast::ParameterDeclaration>> params;
  zc::Vector<zc::Own<ast::Statement>> bodyStmts;
  auto body = ast::factory::createBlockStatement(zc::mv(bodyStmts));
  auto node =
      ast::factory::createInitDeclaration({}, zc::none, zc::mv(params), zc::none, zc::mv(body));
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*node);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("InitDeclaration {"));
}

// Test GetAccessor
ZC_TEST("ASTDumper.DumpGetAccessorText") {
  auto name = ast::factory::createIdentifier("myGetter"_zc);
  zc::Vector<zc::Own<ast::ParameterDeclaration>> params;
  zc::Vector<zc::Own<ast::Statement>> bodyStmts;
  auto body = ast::factory::createBlockStatement(zc::mv(bodyStmts));
  auto node = ast::factory::createGetAccessorDeclaration({}, zc::mv(name), zc::none, zc::mv(params),
                                                         zc::none, zc::mv(body));
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*node);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("GetAccessor {"));
  ZC_ASSERT(result.contains("myGetter"));
}

// Test SetAccessor
ZC_TEST("ASTDumper.DumpSetAccessorText") {
  auto name = ast::factory::createIdentifier("mySetter"_zc);
  zc::Vector<zc::Own<ast::ParameterDeclaration>> params;
  zc::Vector<zc::Own<ast::Statement>> bodyStmts;
  auto body = ast::factory::createBlockStatement(zc::mv(bodyStmts));
  auto node = ast::factory::createSetAccessorDeclaration({}, zc::mv(name), zc::none, zc::mv(params),
                                                         zc::none, zc::mv(body));
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*node);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("SetAccessor {"));
  ZC_ASSERT(result.contains("mySetter"));
}

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
