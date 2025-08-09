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

// Test XML format
ZC_TEST("ASTDumper.DumpSourceFileXML") {
  MockOutputStream output;
  ASTDumper dumper(output, DumpFormat::kXML);

  zc::Vector<zc::Own<Statement>> statements;
  zc::Vector<zc::Own<BindingElement>> bindings;
  auto identifier = ast::factory::createIdentifier(zc::str("testVar"));
  auto bindingElement = ast::factory::createBindingElement(zc::mv(identifier));
  bindings.add(zc::mv(bindingElement));

  auto variableDecl = ast::factory::createVariableDeclaration(zc::mv(bindings));
  statements.add(zc::mv(variableDecl));

  auto sourceFile = ast::factory::createSourceFile(zc::str("test.zom"), zc::mv(statements));

  dumper.dumpSourceFile(*sourceFile);

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
  ASTDumper dumper(output, DumpFormat::kTEXT);
  dumper.dump(*expr);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("StringLiteral {"));
  ZC_ASSERT(result.contains("value: hello world"));
}

ZC_TEST("ASTDumper.DumpStringLiteralJSON") {
  auto expr = ast::factory::createStringLiteral(zc::str("hello world"));
  MockOutputStream output;
  ASTDumper dumper(output, DumpFormat::kJSON);
  dumper.dump(*expr);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("\"node\": \"StringLiteral\""));
  ZC_ASSERT(result.contains("\"value\": \"hello world\""));
}

ZC_TEST("ASTDumper.DumpStringLiteralXML") {
  auto expr = ast::factory::createStringLiteral(zc::str("hello world"));
  MockOutputStream output;
  ASTDumper dumper(output, DumpFormat::kXML);
  dumper.dump(*expr);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("<StringLiteral>"));
  ZC_ASSERT(result.contains("value: hello world"));
  ZC_ASSERT(result.contains("</StringLiteral>"));
}

// Test BooleanLiteral in all formats
ZC_TEST("ASTDumper.DumpBooleanLiteralText") {
  auto expr = ast::factory::createBooleanLiteral(true);
  MockOutputStream output;
  ASTDumper dumper(output, DumpFormat::kTEXT);
  dumper.dump(*expr);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("BooleanLiteral {"));
  ZC_ASSERT(result.contains("value: true"));
}

ZC_TEST("ASTDumper.DumpBooleanLiteralJSON") {
  auto expr = ast::factory::createBooleanLiteral(false);
  MockOutputStream output;
  ASTDumper dumper(output, DumpFormat::kJSON);
  dumper.dump(*expr);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("\"node\": \"BooleanLiteral\""));
  ZC_ASSERT(result.contains("\"value\": \"false\""));
}

ZC_TEST("ASTDumper.DumpBooleanLiteralXML") {
  auto expr = ast::factory::createBooleanLiteral(true);
  MockOutputStream output;
  ASTDumper dumper(output, DumpFormat::kXML);
  dumper.dump(*expr);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("<BooleanLiteral>"));
  ZC_ASSERT(result.contains("value: true"));
  ZC_ASSERT(result.contains("</BooleanLiteral>"));
}

// Test NilLiteral in all formats
ZC_TEST("ASTDumper.DumpNilLiteralText") {
  auto expr = ast::factory::createNilLiteral();
  MockOutputStream output;
  ASTDumper dumper(output, DumpFormat::kTEXT);
  dumper.dump(*expr);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("NilLiteral {"));
}

ZC_TEST("ASTDumper.DumpNilLiteralJSON") {
  auto expr = ast::factory::createNilLiteral();
  MockOutputStream output;
  ASTDumper dumper(output, DumpFormat::kJSON);
  dumper.dump(*expr);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("\"node\": \"NilLiteral\""));
}

ZC_TEST("ASTDumper.DumpNilLiteralXML") {
  auto expr = ast::factory::createNilLiteral();
  MockOutputStream output;
  ASTDumper dumper(output, DumpFormat::kXML);
  dumper.dump(*expr);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("<NilLiteral>"));
  ZC_ASSERT(result.contains("</NilLiteral>"));
}

// Test Identifier in all formats
ZC_TEST("ASTDumper.DumpIdentifierText") {
  auto expr = ast::factory::createIdentifier(zc::str("myVariable"));
  MockOutputStream output;
  ASTDumper dumper(output, DumpFormat::kTEXT);
  dumper.dump(*expr);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("Identifier {"));
  ZC_ASSERT(result.contains("name: myVariable"));
}

ZC_TEST("ASTDumper.DumpIdentifierJSON") {
  auto expr = ast::factory::createIdentifier(zc::str("myVariable"));
  MockOutputStream output;
  ASTDumper dumper(output, DumpFormat::kJSON);
  dumper.dump(*expr);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("\"node\": \"Identifier\""));
  ZC_ASSERT(result.contains("\"name\": \"myVariable\""));
}

ZC_TEST("ASTDumper.DumpIdentifierXML") {
  auto expr = ast::factory::createIdentifier(zc::str("myVariable"));
  MockOutputStream output;
  ASTDumper dumper(output, DumpFormat::kXML);
  dumper.dump(*expr);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("<Identifier>"));
  ZC_ASSERT(result.contains("name: myVariable"));
  ZC_ASSERT(result.contains("</Identifier>"));
}

// Test UnaryExpression in all formats
ZC_TEST("ASTDumper.DumpUnaryExpressionText") {
  auto operand = ast::factory::createNumericLiteral(42.0);
  auto op = ast::factory::createUnaryMinusOperator();
  auto expr = ast::factory::createPrefixUnaryExpression(zc::mv(op), zc::mv(operand));
  MockOutputStream output;
  ASTDumper dumper(output, DumpFormat::kTEXT);
  dumper.dump(*expr);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("PrefixUnaryExpression {"));
  ZC_ASSERT(result.contains("operator: -"));
}

ZC_TEST("ASTDumper.DumpUnaryExpressionJSON") {
  auto operand = ast::factory::createNumericLiteral(42.0);
  auto op = ast::factory::createUnaryPlusOperator();
  auto expr = ast::factory::createPrefixUnaryExpression(zc::mv(op), zc::mv(operand));
  MockOutputStream output;
  ASTDumper dumper(output, DumpFormat::kJSON);
  dumper.dump(*expr);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("\"node\": \"PrefixUnaryExpression\""));
  ZC_ASSERT(result.contains("\"operator\": \"+\""));
}

ZC_TEST("ASTDumper.DumpUnaryExpressionXML") {
  auto operand = ast::factory::createNumericLiteral(42.0);
  auto op = ast::factory::createLogicalNotOperator();
  auto expr = ast::factory::createPrefixUnaryExpression(zc::mv(op), zc::mv(operand));
  MockOutputStream output;
  ASTDumper dumper(output, DumpFormat::kXML);
  dumper.dump(*expr);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("<PrefixUnaryExpression>"));
  ZC_ASSERT(result.contains("operator: !"));
  ZC_ASSERT(result.contains("</PrefixUnaryExpression>"));
}

// Test AssignmentExpression in all formats
ZC_TEST("ASTDumper.DumpAssignmentExpressionText") {
  auto left = ast::factory::createIdentifier(zc::str("x"));
  auto right = ast::factory::createNumericLiteral(10.0);
  auto op = ast::factory::createAssignOperator();
  auto expr = ast::factory::createAssignmentExpression(zc::mv(left), zc::mv(op), zc::mv(right));
  MockOutputStream output;
  ASTDumper dumper(output, DumpFormat::kTEXT);
  dumper.dump(*expr);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("AssignmentExpression {"));
}

ZC_TEST("ASTDumper.DumpAssignmentExpressionJSON") {
  auto left = ast::factory::createIdentifier(zc::str("x"));
  auto right = ast::factory::createNumericLiteral(10.0);
  auto op = ast::factory::createAssignOperator();
  auto expr = ast::factory::createAssignmentExpression(zc::mv(left), zc::mv(op), zc::mv(right));
  MockOutputStream output;
  ASTDumper dumper(output, DumpFormat::kJSON);
  dumper.dump(*expr);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("\"node\": \"AssignmentExpression\""));
}

ZC_TEST("ASTDumper.DumpAssignmentExpressionXML") {
  auto left = ast::factory::createIdentifier(zc::str("x"));
  auto right = ast::factory::createNumericLiteral(10.0);
  auto op = ast::factory::createAssignOperator();
  auto expr = ast::factory::createAssignmentExpression(zc::mv(left), zc::mv(op), zc::mv(right));
  MockOutputStream output;
  ASTDumper dumper(output, DumpFormat::kXML);
  dumper.dump(*expr);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("<AssignmentExpression>"));
  ZC_ASSERT(result.contains("</AssignmentExpression>"));
}

// Test CallExpression in all formats
ZC_TEST("ASTDumper.DumpCallExpressionText") {
  auto callee = ast::factory::createIdentifier(zc::str("func"));
  zc::Vector<zc::Own<Expression>> args;
  args.add(ast::factory::createNumericLiteral(1.0));
  args.add(ast::factory::createStringLiteral(zc::str("test")));
  auto expr = ast::factory::createCallExpression(zc::mv(callee), zc::mv(args));
  MockOutputStream output;
  ASTDumper dumper(output, DumpFormat::kTEXT);
  dumper.dump(*expr);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("CallExpression {"));
}

ZC_TEST("ASTDumper.DumpCallExpressionJSON") {
  auto callee = ast::factory::createIdentifier(zc::str("func"));
  zc::Vector<zc::Own<Expression>> args;
  args.add(ast::factory::createNumericLiteral(1.0));
  auto expr = ast::factory::createCallExpression(zc::mv(callee), zc::mv(args));
  MockOutputStream output;
  ASTDumper dumper(output, DumpFormat::kJSON);
  dumper.dump(*expr);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("\"node\": \"CallExpression\""));
}

ZC_TEST("ASTDumper.DumpCallExpressionXML") {
  auto callee = ast::factory::createIdentifier(zc::str("func"));
  zc::Vector<zc::Own<Expression>> args;
  auto expr = ast::factory::createCallExpression(zc::mv(callee), zc::mv(args));
  MockOutputStream output;
  ASTDumper dumper(output, DumpFormat::kXML);
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
  ASTDumper dumper(output, DumpFormat::kTEXT);
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
  ASTDumper dumper(output, DumpFormat::kJSON);
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
  ASTDumper dumper(output, DumpFormat::kXML);
  dumper.dump(*expr);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("<ConditionalExpression>"));
  ZC_ASSERT(result.contains("</ConditionalExpression>"));
}

// Test Type dumping in all formats
ZC_TEST("ASTDumper.DumpPredefinedTypeText") {
  auto type = ast::factory::createPredefinedType(zc::str("string"));
  MockOutputStream output;
  ASTDumper dumper(output, DumpFormat::kTEXT);
  dumper.dump(*type);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("PredefinedType {"));
  ZC_ASSERT(result.contains("name: string"));
}

ZC_TEST("ASTDumper.DumpPredefinedTypeJSON") {
  auto type = ast::factory::createPredefinedType(zc::str("number"));
  MockOutputStream output;
  ASTDumper dumper(output, DumpFormat::kJSON);
  dumper.dump(*type);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("\"node\": \"PredefinedType\""));
  ZC_ASSERT(result.contains("\"name\": \"number\""));
}

ZC_TEST("ASTDumper.DumpPredefinedTypeXML") {
  auto type = ast::factory::createPredefinedType(zc::str("boolean"));
  MockOutputStream output;
  ASTDumper dumper(output, DumpFormat::kXML);
  dumper.dump(*type);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("<PredefinedType>"));
  ZC_ASSERT(result.contains("name: boolean"));
  ZC_ASSERT(result.contains("</PredefinedType>"));
}

// Test Statement dumping
ZC_TEST("ASTDumper.DumpExpressionStatementText") {
  auto expr = ast::factory::createNumericLiteral(42.0);
  auto stmt = ast::factory::createExpressionStatement(zc::mv(expr));
  MockOutputStream output;
  ASTDumper dumper(output, DumpFormat::kTEXT);
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
  ASTDumper dumper(output, DumpFormat::kJSON);
  dumper.dump(*stmt);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("\"node\": \"ExpressionStatement\""));
}

ZC_TEST("ASTDumper.DumpExpressionStatementXML") {
  auto expr = ast::factory::createBooleanLiteral(false);
  auto stmt = ast::factory::createExpressionStatement(zc::mv(expr));
  MockOutputStream output;
  ASTDumper dumper(output, DumpFormat::kXML);
  dumper.dump(*stmt);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("<ExpressionStatement>"));
  ZC_ASSERT(result.contains("</ExpressionStatement>"));
}

// Test EmptyStatement
ZC_TEST("ASTDumper.DumpEmptyStatementText") {
  auto stmt = ast::factory::createEmptyStatement();
  MockOutputStream output;
  ASTDumper dumper(output, DumpFormat::kTEXT);
  dumper.dump(*stmt);
  zc::String result = output.getBuffer();
  // Debug: print actual output
  ZC_LOG(INFO, "EmptyStatement TEXT output:", result);
  ZC_ASSERT(result.contains("EmptyStatement"));
}

ZC_TEST("ASTDumper.DumpEmptyStatementJSON") {
  auto stmt = ast::factory::createEmptyStatement();
  MockOutputStream output;
  ASTDumper dumper(output, DumpFormat::kJSON);
  dumper.dump(*stmt);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("\"node\": \"EmptyStatement\""));
}

ZC_TEST("ASTDumper.DumpEmptyStatementXML") {
  auto stmt = ast::factory::createEmptyStatement();
  MockOutputStream output;
  ASTDumper dumper(output, DumpFormat::kXML);
  dumper.dump(*stmt);
  zc::String result = output.getBuffer();
  ZC_ASSERT(result.contains("<EmptyStatement>"));
  ZC_ASSERT(result.contains("</EmptyStatement>"));
}

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
