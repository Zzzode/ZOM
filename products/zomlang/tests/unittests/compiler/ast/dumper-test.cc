#include "zomlang/compiler/ast/dumper.h"

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
  zc::Vector<zc::Own<BindingElement>> bindings;
  // Create a binding element for the variable declaration list
  auto identifier = ast::factory::createIdentifier(zc::str("testVar"));
  auto bindingElement = ast::factory::createBindingElement(zc::mv(identifier));
  bindings.add(zc::mv(bindingElement));

  auto variableDecl = ast::factory::createVariableDeclarationList(zc::mv(bindings));
  auto variableStmt = ast::factory::createVariableStatement(zc::mv(variableDecl));
  statements.add(zc::mv(variableStmt));

  auto sourceFile = ast::factory::createSourceFile(zc::str("test.zom"), zc::mv(statements));

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
      "    BinaryOperator {\n"
      "      symbol: +\n"
      "      precedence: 11\n"
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
  zc::Vector<zc::Own<BindingElement>> bindings;
  // Create a proper BindingElement with an identifier
  auto identifier = ast::factory::createIdentifier(zc::str("testVar"));
  auto bindingElement = ast::factory::createBindingElement(zc::mv(identifier));
  bindings.add(zc::mv(bindingElement));

  auto variableDecl = ast::factory::createVariableDeclarationList(zc::mv(bindings));
  auto variableStmt = ast::factory::createVariableStatement(zc::mv(variableDecl));
  statements.add(zc::mv(variableStmt));

  auto sourceFile = ast::factory::createSourceFile(zc::str("test.zom"), zc::mv(statements));

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
  zc::Vector<zc::Own<BindingElement>> bindings;
  auto identifier = ast::factory::createIdentifier(zc::str("testVar"));
  auto bindingElement = ast::factory::createBindingElement(zc::mv(identifier));
  bindings.add(zc::mv(bindingElement));

  auto variableDecl = ast::factory::createVariableDeclarationList(zc::mv(bindings));
  auto variableStmt = ast::factory::createVariableStatement(zc::mv(variableDecl));
  statements.add(zc::mv(variableStmt));

  auto sourceFile = ast::factory::createSourceFile(zc::str("test.zom"), zc::mv(statements));

  dumper.dump(*sourceFile);

  zc::String outputStr = output.getBuffer();
  ZC_ASSERT(outputStr.contains("<SourceFile>"));
  ZC_ASSERT(outputStr.contains("<fileName>test.zom</fileName>"));
  ZC_ASSERT(outputStr.contains("<statements>"));
  ZC_ASSERT(outputStr.contains("</SourceFile>"));
}

// Test StringLiteral in all formats
ZC_TEST("ASTDumper.DumpStringLiteralText") {
  auto expr = ast::factory::createStringLiteral(zc::str("hello world"));
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*expr);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("StringLiteral {"));
  ZC_ASSERT(result.contains("value: hello world"));
}

ZC_TEST("ASTDumper.DumpStringLiteralJSON") {
  auto expr = ast::factory::createStringLiteral(zc::str("hello world"));
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kJSON);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*expr);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("\"node\": \"StringLiteral\""));
  ZC_ASSERT(result.contains("\"value\": \"hello world\""));
}

ZC_TEST("ASTDumper.DumpStringLiteralXML") {
  auto expr = ast::factory::createStringLiteral(zc::str("hello world"));
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
  auto expr = ast::factory::createIdentifier(zc::str("myVariable"));
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*expr);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("Identifier {"));
  ZC_ASSERT(result.contains("name: myVariable"));
}

ZC_TEST("ASTDumper.DumpIdentifierJSON") {
  auto expr = ast::factory::createIdentifier(zc::str("myVariable"));
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kJSON);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*expr);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("\"node\": \"Identifier\""));
  ZC_ASSERT(result.contains("\"name\": \"myVariable\""));
}

ZC_TEST("ASTDumper.DumpIdentifierXML") {
  auto expr = ast::factory::createIdentifier(zc::str("myVariable"));
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
      "    UnaryOperator {\n"
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
      "    \"node\": \"UnaryOperator\",\n"
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
  auto callee = ast::factory::createIdentifier(zc::str("func"));
  zc::Vector<zc::Own<Expression>> args;
  args.add(ast::factory::createFloatLiteral(1.0));
  args.add(ast::factory::createStringLiteral(zc::str("test")));
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
  auto callee = ast::factory::createIdentifier(zc::str("func"));
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
  auto callee = ast::factory::createIdentifier(zc::str("func"));
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
  auto consequent = ast::factory::createStringLiteral(zc::str("yes"));
  auto alternate = ast::factory::createStringLiteral(zc::str("no"));
  auto expr = ast::factory::createConditionalExpression(zc::mv(test), zc::mv(consequent),
                                                        zc::mv(alternate));
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*expr);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("ConditionalExpression {"));
}

ZC_TEST("ASTDumper.DumpConditionalExpressionJSON") {
  auto test = ast::factory::createBooleanLiteral(true);
  auto consequent = ast::factory::createStringLiteral(zc::str("yes"));
  auto alternate = ast::factory::createStringLiteral(zc::str("no"));
  auto expr = ast::factory::createConditionalExpression(zc::mv(test), zc::mv(consequent),
                                                        zc::mv(alternate));
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kJSON);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*expr);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("\"node\": \"ConditionalExpression\""));
}

ZC_TEST("ASTDumper.DumpConditionalExpressionXML") {
  auto test = ast::factory::createBooleanLiteral(true);
  auto consequent = ast::factory::createStringLiteral(zc::str("yes"));
  auto alternate = ast::factory::createStringLiteral(zc::str("no"));
  auto expr = ast::factory::createConditionalExpression(zc::mv(test), zc::mv(consequent),
                                                        zc::mv(alternate));
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
  auto type = ast::factory::createPredefinedType(zc::str("str"));
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*type);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("PredefinedType {"));
  ZC_ASSERT(result.contains("name: str"));
}

ZC_TEST("ASTDumper.DumpPredefinedTypeJSON") {
  auto type = ast::factory::createPredefinedType(zc::str("i32"));
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kJSON);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*type);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("\"node\": \"PredefinedType\""));
  ZC_ASSERT(result.contains("\"name\": \"i32\""));
}

ZC_TEST("ASTDumper.DumpPredefinedTypeXML") {
  auto type = ast::factory::createPredefinedType(zc::str("bool"));
  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kXML);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*type);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("<PredefinedType>"));
  ZC_ASSERT(result.contains("<name>bool</name>"));
  ZC_ASSERT(result.contains("</PredefinedType>"));
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
  auto expr = ast::factory::createStringLiteral(zc::str("hello"));
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
  auto funcName = ast::factory::createIdentifier(zc::str("testFunc"));
  zc::Vector<zc::Own<ast::BindingElement>> parameters;
  auto param = ast::factory::createBindingElement(ast::factory::createIdentifier(zc::str("param")),
                                                  zc::none, zc::none);
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
  auto operand = ast::factory::createIdentifier(zc::str("x"));
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
  auto typeName = ast::factory::createIdentifier(zc::str("MyType"));
  auto typeRef = ast::factory::createTypeReference(zc::mv(typeName), zc::none);

  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*typeRef);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("TypeReference {"));
  ZC_ASSERT(result.contains("name:"));
}

// Test ArrayType dumping
ZC_TEST("ASTDumper.DumpArrayTypeText") {
  auto elementType = ast::factory::createPredefinedType(zc::str("i32"));
  auto arrayType = ast::factory::createArrayType(zc::mv(elementType));

  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*arrayType);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("ArrayType {"));
  ZC_ASSERT(result.contains("elementType:"));
}

// Test UnionType dumping
ZC_TEST("ASTDumper.DumpUnionTypeText") {
  zc::Vector<zc::Own<ast::TypeNode>> types;
  types.add(ast::factory::createPredefinedType(zc::str("str")));
  types.add(ast::factory::createPredefinedType(zc::str("i32")));
  auto unionType = ast::factory::createUnionType(zc::mv(types));

  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kTEXT);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*unionType);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("UnionType {"));
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
  auto elementType = ast::factory::createPredefinedType(zc::str("str"));
  auto arrayType = ast::factory::createArrayType(zc::mv(elementType));

  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kJSON);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*arrayType);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("\"node\": \"ArrayType\""));
  ZC_ASSERT(result.contains("\"elementType\":"));
}

// Test XML serialization for complex types
ZC_TEST("ASTDumper.DumpUnionTypeXML") {
  zc::Vector<zc::Own<ast::TypeNode>> types;
  types.add(ast::factory::createPredefinedType(zc::str("bool")));
  types.add(ast::factory::createPredefinedType(zc::str("null")));
  auto unionType = ast::factory::createUnionType(zc::mv(types));

  MockOutputStream output;
  auto serializer = createTestSerializer(output, TestSerializerType::kXML);
  ASTDumper dumper(zc::mv(serializer));
  dumper.dump(*unionType);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("<UnionType>"));
  ZC_ASSERT(result.contains("<types>"));
  ZC_ASSERT(result.contains("</UnionType>"));
}

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
