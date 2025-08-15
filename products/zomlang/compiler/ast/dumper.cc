// Copyright (c) 2024-2025 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations under
// the License.

#include "zomlang/compiler/ast/dumper.h"

#include "zc/core/io.h"
#include "zc/core/string.h"
#include "zomlang/compiler/ast/ast.h"
#include "zomlang/compiler/ast/expression.h"
#include "zomlang/compiler/ast/module.h"
#include "zomlang/compiler/ast/operator.h"
#include "zomlang/compiler/ast/statement.h"
#include "zomlang/compiler/ast/type.h"

namespace zomlang {
namespace compiler {
namespace ast {

// ================================================================================
// ASTDumper::Impl

struct ASTDumper::Impl {
  Impl(zc::OutputStream& output, DumpFormat format) noexcept
      : output(output), format(format), isFirstNode(true) {}
  ~Impl() noexcept(false) = default;

  ZC_DISALLOW_COPY_AND_MOVE(Impl);

  zc::OutputStream& output;
  DumpFormat format;
  bool isFirstNode;
};

// ================================================================================
// ASTDumper

ASTDumper::ASTDumper(zc::OutputStream& output, DumpFormat format) noexcept
    : impl(zc::heap<Impl>(output, format)) {}

ASTDumper::~ASTDumper() noexcept(false) = default;

void ASTDumper::dump(const Node& node) {
  // Use visitor pattern instead of switch-case
  node.accept(*this);
}

// ================================================================================
// Visitor pattern implementations

void ASTDumper::visit(const SourceFile& sourceFile) {
  switch (impl->format) {
    case DumpFormat::kTEXT:
      writeNodeHeader("SourceFile");
      writeProperty("fileName", sourceFile.getFileName(), currentIndent + 1);
      writeLine("statements:", currentIndent + 1);
      setIndent(currentIndent + 2);
      for (const auto& stmt : sourceFile.getStatements()) {
        stmt.accept(*this);
      }
      setIndent(currentIndent - 2);
      writeNodeFooter("SourceFile");
      break;
    case DumpFormat::kJSON:
      impl->output.write("{\n"_zcb);
      writeProperty("node", "SourceFile", 1);
      impl->output.write(",\n"_zcb);
      writeProperty("fileName", sourceFile.getFileName(), 1);
      impl->output.write(",\n"_zcb);
      writeIndent(1);
      impl->output.write("\"children\": [\n"_zcb);
      {
        bool first = true;
        setIndent(2);
        for (const auto& stmt : sourceFile.getStatements()) {
          if (!first) { impl->output.write(",\n"_zcb); }
          first = false;
          stmt.accept(*this);
        }
        setIndent(0);
      }
      impl->output.write("\n"_zcb);
      writeIndent(1);
      impl->output.write("]\n}\n"_zcb);
      break;
    case DumpFormat::kXML:
      impl->output.write("<SourceFile>\n"_zcb);
      writeIndent(1);
      impl->output.write("<fileName>"_zcb);
      impl->output.write(sourceFile.getFileName().asBytes());
      impl->output.write("</fileName>\n"_zcb);
      writeIndent(1);
      impl->output.write("<statements>\n"_zcb);
      setIndent(2);
      for (const auto& stmt : sourceFile.getStatements()) {
        stmt.accept(*this);
      }
      setIndent(0);
      writeIndent(1);
      impl->output.write("</statements>\n"_zcb);
      impl->output.write("</SourceFile>\n"_zcb);
      break;
  }
}

void ASTDumper::visit(const ImportDeclaration& importDecl) {
  switch (impl->format) {
    case DumpFormat::kTEXT:
      writeNodeHeader("ImportDeclaration");
      dumpModulePath(importDecl.getModulePath());
      ZC_IF_SOME(alias, importDecl.getAlias()) {
        writeProperty("alias", alias, currentIndent + 1);
      }
      writeNodeFooter("ImportDeclaration");
      break;
    case DumpFormat::kJSON:
      writeIndent(currentIndent);
      impl->output.write("{\n"_zcb);
      writeProperty("node", "ImportDeclaration", currentIndent + 1);
      impl->output.write(",\n"_zcb);
      writeIndent(currentIndent + 1);
      impl->output.write("\"modulePath\": \""_zcb);
      impl->output.write(importDecl.getModulePath().toString().asBytes());
      impl->output.write("\""_zcb);
      ZC_IF_SOME(alias, importDecl.getAlias()) {
        impl->output.write(",\n"_zcb);
        writeProperty("alias", alias, currentIndent + 1);
      }
      impl->output.write("\n"_zcb);
      writeIndent(currentIndent);
      impl->output.write("}"_zcb);
      break;
    case DumpFormat::kXML:
      writeIndent(currentIndent);
      impl->output.write("<ImportDeclaration>\n"_zcb);
      writeIndent(currentIndent + 1);
      impl->output.write("<modulePath>"_zcb);
      impl->output.write(importDecl.getModulePath().toString().asBytes());
      impl->output.write("</modulePath>\n"_zcb);
      ZC_IF_SOME(alias, importDecl.getAlias()) {
        writeIndent(currentIndent + 1);
        impl->output.write("<alias>"_zcb);
        impl->output.write(alias.asBytes());
        impl->output.write("</alias>\n"_zcb);
      }
      writeIndent(currentIndent);
      impl->output.write("</ImportDeclaration>\n"_zcb);
      break;
  }
}

void ASTDumper::visit(const ExportDeclaration& exportDecl) {
  switch (impl->format) {
    case DumpFormat::kTEXT:
      writeNodeHeader("ExportDeclaration");
      writeProperty("identifier", exportDecl.getIdentifier(), currentIndent + 1);
      if (exportDecl.isRename()) {
        ZC_IF_SOME(alias, exportDecl.getAlias()) {
          writeProperty("alias", alias, currentIndent + 1);
        }
        ZC_IF_SOME(modulePath, exportDecl.getModulePath()) {
          dumpModulePath(modulePath);
        }
      }
      writeNodeFooter("ExportDeclaration");
      break;
    case DumpFormat::kJSON:
      writeIndent(currentIndent);
      impl->output.write("{\n"_zcb);
      writeProperty("node", "ExportDeclaration", currentIndent + 1);
      impl->output.write(",\n"_zcb);
      writeProperty("identifier", exportDecl.getIdentifier(), currentIndent + 1);
      if (exportDecl.isRename()) {
        ZC_IF_SOME(alias, exportDecl.getAlias()) {
          impl->output.write(",\n"_zcb);
          writeProperty("alias", alias, currentIndent + 1);
        }
        ZC_IF_SOME(modulePath, exportDecl.getModulePath()) {
          impl->output.write(",\n"_zcb);
          writeIndent(currentIndent + 1);
          impl->output.write("\"modulePath\": \""_zcb);
          impl->output.write(modulePath.toString().asBytes());
          impl->output.write("\""_zcb);
        }
      }
      impl->output.write("\n"_zcb);
      writeIndent(currentIndent);
      impl->output.write("}"_zcb);
      break;
    case DumpFormat::kXML:
      writeIndent(currentIndent);
      impl->output.write("<ExportDeclaration>\n"_zcb);
      writeIndent(currentIndent + 1);
      impl->output.write("<identifier>"_zcb);
      impl->output.write(exportDecl.getIdentifier().asBytes());
      impl->output.write("</identifier>\n"_zcb);
      if (exportDecl.isRename()) {
        ZC_IF_SOME(alias, exportDecl.getAlias()) {
          writeIndent(currentIndent + 1);
          impl->output.write("<alias>"_zcb);
          impl->output.write(alias.asBytes());
          impl->output.write("</alias>\n"_zcb);
        }
        ZC_IF_SOME(modulePath, exportDecl.getModulePath()) {
          writeIndent(currentIndent + 1);
          impl->output.write("<modulePath>"_zcb);
          impl->output.write(modulePath.toString().asBytes());
          impl->output.write("</modulePath>\n"_zcb);
        }
      }
      writeIndent(currentIndent);
      impl->output.write("</ExportDeclaration>\n"_zcb);
      break;
  }
}

void ASTDumper::visit(const VariableDeclaration& varDecl) {
  switch (impl->format) {
    case DumpFormat::kTEXT:
      writeNodeHeader("VariableDeclaration");
      writeLine("bindings:", currentIndent + 1);
      setIndent(currentIndent + 2);
      for (const auto& binding : varDecl.getBindings()) {
        dumpBindingElement(binding);
      }
      setIndent(currentIndent - 2);
      writeNodeFooter("VariableDeclaration");
      break;
    case DumpFormat::kJSON: {
      writeIndent(currentIndent);
      impl->output.write("{\n"_zcb);
      writeProperty("node", "VariableDeclaration", currentIndent + 1);
      impl->output.write(",\n"_zcb);
      writeIndent(currentIndent + 1);
      impl->output.write("\"bindings\": [\n"_zcb);
      bool first = true;
      setIndent(currentIndent + 2);
      for (const auto& binding : varDecl.getBindings()) {
        if (!first) { impl->output.write(",\n"_zcb); }
        first = false;
        dumpBindingElement(binding);
      }
      setIndent(currentIndent);
      impl->output.write("\n"_zcb);
      writeIndent(currentIndent + 1);
      impl->output.write("]\n"_zcb);
      writeIndent(currentIndent);
      impl->output.write("}"_zcb);
      break;
    }
    case DumpFormat::kXML:
      writeIndent(currentIndent);
      impl->output.write("<VariableDeclaration>\n"_zcb);
      writeIndent(currentIndent + 1);
      impl->output.write("<bindings>\n"_zcb);
      setIndent(currentIndent + 2);
      for (const auto& binding : varDecl.getBindings()) {
        dumpBindingElement(binding);
      }
      setIndent(currentIndent);
      writeIndent(currentIndent + 1);
      impl->output.write("</bindings>\n"_zcb);
      writeIndent(currentIndent);
      impl->output.write("</VariableDeclaration>\n"_zcb);
      break;
  }
}

void ASTDumper::visit(const FunctionDeclaration& funcDecl) {
  switch (impl->format) {
    case DumpFormat::kTEXT:
      writeNodeHeader("FunctionDeclaration");
      writeProperty("name", funcDecl.getName().getName(), currentIndent + 1);
      if (!funcDecl.getParameters().empty()) {
        writeLine("parameters:", currentIndent + 1);
        setIndent(currentIndent + 2);
        for (const auto& param : funcDecl.getParameters()) {
          dumpBindingElement(param);
        }
        setIndent(currentIndent - 2);
      }
      writeLine("body:", currentIndent + 1);
      setIndent(currentIndent + 2);
      funcDecl.getBody().accept(*this);
      setIndent(currentIndent - 2);
      writeNodeFooter("FunctionDeclaration");
      break;
    case DumpFormat::kJSON:
      writeIndent(currentIndent);
      impl->output.write("{\n"_zcb);
      writeProperty("node", "FunctionDeclaration", currentIndent + 1);
      impl->output.write(",\n"_zcb);
      writeProperty("name", funcDecl.getName().getName(), currentIndent + 1);
      if (!funcDecl.getParameters().empty()) {
        impl->output.write(",\n"_zcb);
        writeIndent(currentIndent + 1);
        impl->output.write("\"parameters\": [\n"_zcb);
        bool first = true;
        setIndent(currentIndent + 2);
        for (const auto& param : funcDecl.getParameters()) {
          if (!first) { impl->output.write(",\n"_zcb); }
          first = false;
          dumpBindingElement(param);
        }
        setIndent(currentIndent);
        impl->output.write("\n"_zcb);
        writeIndent(currentIndent + 1);
        impl->output.write("]"_zcb);
      }
      impl->output.write(",\n"_zcb);
      writeIndent(currentIndent + 1);
      impl->output.write("\"body\": "_zcb);
      setIndent(currentIndent + 1);
      funcDecl.getBody().accept(*this);
      setIndent(currentIndent);
      impl->output.write("\n"_zcb);
      writeIndent(currentIndent);
      impl->output.write("}"_zcb);
      break;
    case DumpFormat::kXML:
      writeIndent(currentIndent);
      impl->output.write("<FunctionDeclaration>\n"_zcb);
      writeIndent(currentIndent + 1);
      impl->output.write("<name>"_zcb);
      impl->output.write(funcDecl.getName().getName().asBytes());
      impl->output.write("</name>\n"_zcb);
      if (!funcDecl.getParameters().empty()) {
        writeIndent(currentIndent + 1);
        impl->output.write("<parameters>\n"_zcb);
        setIndent(currentIndent + 2);
        for (const auto& param : funcDecl.getParameters()) {
          dumpBindingElement(param);
        }
        setIndent(currentIndent);
        writeIndent(currentIndent + 1);
        impl->output.write("</parameters>\n"_zcb);
      }
      writeIndent(currentIndent + 1);
      impl->output.write("<body>\n"_zcb);
      setIndent(currentIndent + 2);
      funcDecl.getBody().accept(*this);
      setIndent(currentIndent);
      writeIndent(currentIndent + 1);
      impl->output.write("</body>\n"_zcb);
      writeIndent(currentIndent);
      impl->output.write("</FunctionDeclaration>\n"_zcb);
      break;
  }
}

void ASTDumper::visit(const BlockStatement& blockStmt) {
  switch (impl->format) {
    case DumpFormat::kTEXT:
      writeNodeHeader("BlockStatement");
      writeLine("statements:", currentIndent + 1);
      setIndent(currentIndent + 2);
      for (const auto& stmt : blockStmt.getStatements()) {
        stmt.accept(*this);
      }
      setIndent(currentIndent - 2);
      writeNodeFooter("BlockStatement");
      break;
    case DumpFormat::kJSON:
      writeIndent(currentIndent);
      impl->output.write("{\n"_zcb);
      writeProperty("node", "BlockStatement", currentIndent + 1);
      impl->output.write(",\n"_zcb);
      writeIndent(currentIndent + 1);
      impl->output.write("\"statements\": [\n"_zcb);
      {
        bool first = true;
        setIndent(currentIndent + 2);
        for (const auto& stmt : blockStmt.getStatements()) {
          if (!first) { impl->output.write(",\n"_zcb); }
          first = false;
          stmt.accept(*this);
        }
        setIndent(currentIndent);
      }
      impl->output.write("\n"_zcb);
      writeIndent(currentIndent + 1);
      impl->output.write("]\n"_zcb);
      writeIndent(currentIndent);
      impl->output.write("}"_zcb);
      break;
    case DumpFormat::kXML:
      writeIndent(currentIndent);
      impl->output.write("<BlockStatement>\n"_zcb);
      writeIndent(currentIndent + 1);
      impl->output.write("<statements>\n"_zcb);
      setIndent(currentIndent + 2);
      for (const auto& stmt : blockStmt.getStatements()) {
        stmt.accept(*this);
      }
      setIndent(currentIndent);
      writeIndent(currentIndent + 1);
      impl->output.write("</statements>\n"_zcb);
      writeIndent(currentIndent);
      impl->output.write("</BlockStatement>\n"_zcb);
      break;
  }
}

void ASTDumper::visit(const ExpressionStatement& exprStmt) {
  switch (impl->format) {
    case DumpFormat::kTEXT:
      writeNodeHeader("ExpressionStatement");
      writeLine("expression:", currentIndent + 1);
      setIndent(currentIndent + 2);
      exprStmt.getExpression().accept(*this);
      setIndent(currentIndent - 2);
      writeNodeFooter("ExpressionStatement");
      break;
    case DumpFormat::kJSON:
      writeIndent(currentIndent);
      impl->output.write("{\n"_zcb);
      writeProperty("node", "ExpressionStatement", currentIndent + 1);
      impl->output.write(",\n"_zcb);
      writeIndent(currentIndent + 1);
      impl->output.write("\"expression\": "_zcb);
      setIndent(currentIndent + 1);
      exprStmt.getExpression().accept(*this);
      setIndent(currentIndent);
      impl->output.write("\n"_zcb);
      writeIndent(currentIndent);
      impl->output.write("}"_zcb);
      break;
    case DumpFormat::kXML:
      writeIndent(currentIndent);
      impl->output.write("<ExpressionStatement>\n"_zcb);
      writeIndent(currentIndent + 1);
      impl->output.write("<expression>\n"_zcb);
      setIndent(currentIndent + 2);
      exprStmt.getExpression().accept(*this);
      setIndent(currentIndent);
      writeIndent(currentIndent + 1);
      impl->output.write("</expression>\n"_zcb);
      writeIndent(currentIndent);
      impl->output.write("</ExpressionStatement>\n"_zcb);
      break;
  }
}

void ASTDumper::visit(const EmptyStatement& emptyStmt) {
  switch (impl->format) {
    case DumpFormat::kTEXT:
      writeNodeHeader("EmptyStatement");
      writeNodeFooter("EmptyStatement");
      break;
    case DumpFormat::kJSON:
      writeIndent(currentIndent);
      impl->output.write("{\n"_zcb);
      writeProperty("node", "EmptyStatement", currentIndent + 1);
      impl->output.write("\n"_zcb);
      writeIndent(currentIndent);
      impl->output.write("}"_zcb);
      break;
    case DumpFormat::kXML:
      writeIndent(currentIndent);
      impl->output.write("<EmptyStatement />\n"_zcb);
      break;
  }
}

// Expression visitors
void ASTDumper::visit(const BinaryExpression& binExpr) {
  switch (impl->format) {
    case DumpFormat::kTEXT:
      writeNodeHeader("BinaryExpression");
      writeLine("left:", currentIndent + 1);
      setIndent(currentIndent + 2);
      binExpr.getLeft().accept(*this);
       setIndent(currentIndent - 2);
       writeLine("operator:", currentIndent + 1);
       setIndent(currentIndent + 2);
       binExpr.getOperator().accept(*this);
       setIndent(currentIndent - 2);
       writeLine("right:", currentIndent + 1);
       setIndent(currentIndent + 2);
       binExpr.getRight().accept(*this);
      setIndent(currentIndent - 2);
      writeNodeFooter("BinaryExpression");
      break;
    case DumpFormat::kJSON:
      writeIndent(currentIndent);
      impl->output.write("{\n"_zcb);
      writeProperty("node", "BinaryExpression", currentIndent + 1);
      impl->output.write(",\n"_zcb);
      writeIndent(currentIndent + 1);
      impl->output.write("\"left\": "_zcb);
      setIndent(currentIndent + 1);
      binExpr.getLeft().accept(*this);
       setIndent(currentIndent);
       impl->output.write(",\n"_zcb);
       writeIndent(currentIndent + 1);
       impl->output.write("\"operator\": "_zcb);
       setIndent(currentIndent + 1);
       binExpr.getOperator().accept(*this);
       setIndent(currentIndent);
       impl->output.write(",\n"_zcb);
       writeIndent(currentIndent + 1);
       impl->output.write("\"right\": "_zcb);
       setIndent(currentIndent + 1);
       binExpr.getRight().accept(*this);
      setIndent(currentIndent);
      impl->output.write("\n"_zcb);
      writeIndent(currentIndent);
      impl->output.write("}"_zcb);
      break;
    case DumpFormat::kXML:
      writeIndent(currentIndent);
      impl->output.write("<BinaryExpression>\n"_zcb);
      writeIndent(currentIndent + 1);
      impl->output.write("<left>\n"_zcb);
      setIndent(currentIndent + 2);
      binExpr.getLeft().accept(*this);
       setIndent(currentIndent);
       writeIndent(currentIndent + 1);
       impl->output.write("</left>\n"_zcb);
       writeIndent(currentIndent + 1);
       impl->output.write("<operator>\n"_zcb);
       setIndent(currentIndent + 2);
       binExpr.getOperator().accept(*this);
       setIndent(currentIndent);
       writeIndent(currentIndent + 1);
       impl->output.write("</operator>\n"_zcb);
       writeIndent(currentIndent + 1);
       impl->output.write("<right>\n"_zcb);
       setIndent(currentIndent + 2);
       binExpr.getRight().accept(*this);
      setIndent(currentIndent);
      writeIndent(currentIndent + 1);
      impl->output.write("</right>\n"_zcb);
      writeIndent(currentIndent);
      impl->output.write("</BinaryExpression>\n"_zcb);
      break;
  }
}

void ASTDumper::visit(const StringLiteral& strLit) {
  switch (impl->format) {
    case DumpFormat::kTEXT:
      writeNodeHeader("StringLiteral");
      writeProperty("value", strLit.getValue(), currentIndent + 1);
      writeNodeFooter("StringLiteral");
      break;
    case DumpFormat::kJSON:
      writeIndent(currentIndent);
      impl->output.write("{\n"_zcb);
      writeProperty("node", "StringLiteral", currentIndent + 1);
      impl->output.write(",\n"_zcb);
      writeProperty("value", strLit.getValue(), currentIndent + 1);
      impl->output.write("\n"_zcb);
      writeIndent(currentIndent);
      impl->output.write("}"_zcb);
      break;
    case DumpFormat::kXML:
      writeIndent(currentIndent);
      impl->output.write("<StringLiteral>"_zcb);
      impl->output.write(strLit.getValue().asBytes());
      impl->output.write("</StringLiteral>\n"_zcb);
      break;
  }
}

void ASTDumper::visit(const IntegerLiteral& intLit) {
  switch (impl->format) {
    case DumpFormat::kTEXT:
      writeNodeHeader("IntegerLiteral");
      writeProperty("value", zc::str(intLit.getValue()), currentIndent + 1);
      writeNodeFooter("IntegerLiteral");
      break;
    case DumpFormat::kJSON:
      writeIndent(currentIndent);
      impl->output.write("{\n"_zcb);
      writeProperty("node", "IntegerLiteral", currentIndent + 1);
      impl->output.write(",\n"_zcb);
      writeProperty("value", zc::str(intLit.getValue()), currentIndent + 1);
      impl->output.write("\n"_zcb);
      writeIndent(currentIndent);
      impl->output.write("}"_zcb);
      break;
    case DumpFormat::kXML:
      writeIndent(currentIndent);
      impl->output.write("<IntegerLiteral>"_zcb);
      impl->output.write(zc::str(intLit.getValue()).asBytes());
      impl->output.write("</IntegerLiteral>\n"_zcb);
      break;
  }
}

void ASTDumper::visit(const Identifier& identifier) {
  switch (impl->format) {
    case DumpFormat::kTEXT:
      writeNodeHeader("Identifier");
      writeProperty("name", identifier.getName(), currentIndent + 1);
      writeNodeFooter("Identifier");
      break;
    case DumpFormat::kJSON:
      writeIndent(currentIndent);
      impl->output.write("{\n"_zcb);
      writeProperty("node", "Identifier", currentIndent + 1);
      impl->output.write(",\n"_zcb);
      writeProperty("name", identifier.getName(), currentIndent + 1);
      impl->output.write("\n"_zcb);
      writeIndent(currentIndent);
      impl->output.write("}"_zcb);
      break;
    case DumpFormat::kXML:
      writeIndent(currentIndent);
      impl->output.write("<Identifier>"_zcb);
      impl->output.write(identifier.getName().asBytes());
      impl->output.write("</Identifier>\n"_zcb);
      break;
  }
}

// Placeholder implementations for other visit methods
void ASTDumper::visit(const FunctionExpression& funcExpr) {
  writeNodeHeader("FunctionExpression");
  writeNodeFooter("FunctionExpression");
}

void ASTDumper::visit(const FloatLiteral& floatLit) {
  writeNodeHeader("FloatLiteral");
  writeProperty("value", zc::str(floatLit.getValue()), currentIndent + 1);
  writeNodeFooter("FloatLiteral");
}

void ASTDumper::visit(const BooleanLiteral& boolLit) {
  writeNodeHeader("BooleanLiteral");
  writeProperty("value", boolLit.getValue() ? "true" : "false", currentIndent + 1);
  writeNodeFooter("BooleanLiteral");
}

void ASTDumper::visit(const NullLiteral& nullLit) {
  writeNodeHeader("NullLiteral");
  writeNodeFooter("NullLiteral");
}

void ASTDumper::visit(const CallExpression& callExpr) {
  writeNodeHeader("CallExpression");
  writeNodeFooter("CallExpression");
}

void ASTDumper::visit(const NewExpression& newExpr) {
  writeNodeHeader("NewExpression");
  writeNodeFooter("NewExpression");
}

void ASTDumper::visit(const ArrayLiteralExpression& arrLit) {
  writeNodeHeader("ArrayLiteralExpression");
  writeNodeFooter("ArrayLiteralExpression");
}

void ASTDumper::visit(const ObjectLiteralExpression& objLit) {
  writeNodeHeader("ObjectLiteralExpression");
  writeNodeFooter("ObjectLiteralExpression");
}

void ASTDumper::visit(const ParenthesizedExpression& parenExpr) {
  writeNodeHeader("ParenthesizedExpression");
  writeLine("expression:", currentIndent + 1);
  setIndent(currentIndent + 2);
  parenExpr.getExpression().accept(*this);
  setIndent(currentIndent - 2);
  writeNodeFooter("ParenthesizedExpression");
}

void ASTDumper::visit(const PrefixUnaryExpression& prefixUnaryExpr) {
  writeNodeHeader("PrefixUnaryExpression");
  writeNodeFooter("PrefixUnaryExpression");
}

void ASTDumper::visit(const AssignmentExpression& assignmentExpr) {
  writeNodeHeader("AssignmentExpression");
  writeNodeFooter("AssignmentExpression");
}

void ASTDumper::visit(const ConditionalExpression& conditionalExpr) {
  writeNodeHeader("ConditionalExpression");
  writeNodeFooter("ConditionalExpression");
}

void ASTDumper::visit(const CastExpression& castExpr) {
  writeNodeHeader("CastExpression");
  writeNodeFooter("CastExpression");
}

void ASTDumper::visit(const AsExpression& asExpr) {
  writeNodeHeader("AsExpression");
  writeNodeFooter("AsExpression");
}

void ASTDumper::visit(const ForcedAsExpression& forcedAsExpr) {
  writeNodeHeader("ForcedAsExpression");
  writeNodeFooter("ForcedAsExpression");
}

void ASTDumper::visit(const ConditionalAsExpression& conditionalAsExpr) {
  writeNodeHeader("ConditionalAsExpression");
  writeNodeFooter("ConditionalAsExpression");
}

// ================================================================================
// Helper methods

void ASTDumper::dumpModulePath(const ModulePath& modulePath) {
  switch (impl->format) {
    case DumpFormat::kTEXT:
      writeProperty("modulePath", modulePath.toString(), currentIndent + 1);
      break;
    case DumpFormat::kJSON:
      writeProperty("modulePath", modulePath.toString(), currentIndent + 1);
      break;
    case DumpFormat::kXML:
      writeIndent(currentIndent + 1);
      impl->output.write("<modulePath>"_zcb);
      impl->output.write(modulePath.toString().asBytes());
      impl->output.write("</modulePath>\n"_zcb);
      break;
  }
}

void ASTDumper::dumpBindingElement(const BindingElement& bindingElement) {
  switch (impl->format) {
    case DumpFormat::kTEXT:
      writeNodeHeader("BindingElement");
      writeProperty("name", bindingElement.getName().getName(), currentIndent + 1);
      ZC_IF_SOME(type, bindingElement.getType()) {
        writeLine("varType:", currentIndent + 1);
        setIndent(currentIndent + 2);
        dumpType(type);
        setIndent(currentIndent - 2);
      }
      if (bindingElement.getInitializer()) {
        writeLine("initializer:", currentIndent + 1);
        setIndent(currentIndent + 2);
        bindingElement.getInitializer()->accept(*this);
        setIndent(currentIndent - 2);
      }
      writeNodeFooter("BindingElement");
      break;
    case DumpFormat::kJSON:
      writeIndent(currentIndent);
      impl->output.write("{\n"_zcb);
      writeProperty("node", "BindingElement", currentIndent + 1);
      impl->output.write(",\n"_zcb);
      writeProperty("name", bindingElement.getName().getName(), currentIndent + 1);
      ZC_IF_SOME(type, bindingElement.getType()) {
        impl->output.write(",\n"_zcb);
        writeIndent(currentIndent + 1);
        impl->output.write("\"varType\": "_zcb);
        setIndent(currentIndent + 1);
        dumpType(type);
        setIndent(currentIndent);
      }
      if (bindingElement.getInitializer()) {
        impl->output.write(",\n"_zcb);
        writeIndent(currentIndent + 1);
        impl->output.write("\"initializer\": "_zcb);
        setIndent(currentIndent + 1);
        bindingElement.getInitializer()->accept(*this);
        setIndent(currentIndent);
      }
      impl->output.write("\n"_zcb);
      writeIndent(currentIndent);
      impl->output.write("}"_zcb);
      break;
    case DumpFormat::kXML:
      writeIndent(currentIndent);
      impl->output.write("<BindingElement>\n"_zcb);
      writeIndent(currentIndent + 1);
      impl->output.write("<name>"_zcb);
      impl->output.write(bindingElement.getName().getName().asBytes());
      impl->output.write("</name>\n"_zcb);
      ZC_IF_SOME(type, bindingElement.getType()) {
        writeIndent(currentIndent + 1);
        impl->output.write("<varType>\n"_zcb);
        setIndent(currentIndent + 2);
        dumpType(type);
        setIndent(currentIndent);
        writeIndent(currentIndent + 1);
        impl->output.write("</varType>\n"_zcb);
      }
      if (bindingElement.getInitializer()) {
        writeIndent(currentIndent + 1);
        impl->output.write("<initializer>\n"_zcb);
        setIndent(currentIndent + 2);
        bindingElement.getInitializer()->accept(*this);
        setIndent(currentIndent);
        writeIndent(currentIndent + 1);
        impl->output.write("</initializer>\n"_zcb);
      }
      writeIndent(currentIndent);
      impl->output.write("</BindingElement>\n"_zcb);
      break;
  }
}

// Placeholder implementations for type dumping methods
void ASTDumper::dumpType(const Type& type) {
  writeNodeHeader("Type");
  writeNodeFooter("Type");
}

void ASTDumper::dumpTypeReference(const TypeReference& typeRef) {
  writeNodeHeader("TypeReference");
  writeNodeFooter("TypeReference");
}

void ASTDumper::dumpArrayType(const ArrayType& arrayType) {
  writeNodeHeader("ArrayType");
  writeNodeFooter("ArrayType");
}

void ASTDumper::dumpUnionType(const UnionType& unionType) {
  writeNodeHeader("UnionType");
  writeNodeFooter("UnionType");
}

void ASTDumper::dumpIntersectionType(const IntersectionType& intersectionType) {
  writeNodeHeader("IntersectionType");
  writeNodeFooter("IntersectionType");
}

void ASTDumper::dumpParenthesizedType(const ParenthesizedType& parenType) {
  writeNodeHeader("ParenthesizedType");
  writeNodeFooter("ParenthesizedType");
}

void ASTDumper::dumpPredefinedType(const PredefinedType& predefinedType) {
  writeNodeHeader("PredefinedType");
  writeNodeFooter("PredefinedType");
}

void ASTDumper::dumpObjectType(const ObjectType& objectType) {
  writeNodeHeader("ObjectType");
  writeNodeFooter("ObjectType");
}

void ASTDumper::dumpTupleType(const TupleType& tupleType) {
  writeNodeHeader("TupleType");
  writeNodeFooter("TupleType");
}

void ASTDumper::dumpReturnType(const ReturnType& returnType) {
  writeNodeHeader("ReturnType");
  writeNodeFooter("ReturnType");
}

void ASTDumper::dumpFunctionType(const FunctionType& functionType) {
  writeNodeHeader("FunctionType");
  writeNodeFooter("FunctionType");
}

void ASTDumper::dumpOptionalType(const OptionalType& optionalType) {
  writeNodeHeader("OptionalType");
  writeNodeFooter("OptionalType");
}

void ASTDumper::dumpTypeQuery(const TypeQuery& typeQuery) {
  writeNodeHeader("TypeQuery");
  writeNodeFooter("TypeQuery");
}

// ================================================================================
// Output formatting helpers

void ASTDumper::writeIndent(int indent) {
  for (int i = 0; i < indent; ++i) { impl->output.write("  "_zcb); }
}

void ASTDumper::writeLine(const zc::StringPtr text, int indent) {
  int actualIndent = (indent == -1) ? currentIndent : indent;
  writeIndent(actualIndent);
  impl->output.write(text.asBytes());
  impl->output.write("\n"_zcb);
}

void ASTDumper::writeNodeHeader(const zc::StringPtr nodeType, int indent) {
  int actualIndent = (indent == -1) ? currentIndent : indent;
  switch (impl->format) {
    case DumpFormat::kTEXT:
      writeIndent(actualIndent);
      impl->output.write(nodeType.asBytes());
      impl->output.write(" {\n"_zcb);
      break;
    case DumpFormat::kJSON:
    case DumpFormat::kXML:
      // Handled in specific visit methods
      break;
  }
}

void ASTDumper::writeNodeFooter(const zc::StringPtr nodeType, int indent) {
  int actualIndent = (indent == -1) ? currentIndent : indent;
  switch (impl->format) {
    case DumpFormat::kTEXT:
      writeIndent(actualIndent);
      impl->output.write("}\n"_zcb);
      break;
    case DumpFormat::kJSON:
    case DumpFormat::kXML:
      // Handled in specific visit methods
      break;
  }
}

zc::String escapeJsonString(const zc::StringPtr str) {
  zc::Vector<char> result;
  for (char c : str) {
    switch (c) {
      case '"':
        result.addAll(zc::StringPtr("\\\""));
        break;
      case '\\':
        result.addAll(zc::StringPtr("\\\\"));
        break;
      case '\b':
        result.addAll(zc::StringPtr("\\b"));
        break;
      case '\f':
        result.addAll(zc::StringPtr("\\f"));
        break;
      case '\n':
        result.addAll(zc::StringPtr("\\n"));
        break;
      case '\r':
        result.addAll(zc::StringPtr("\\r"));
        break;
      case '\t':
        result.addAll(zc::StringPtr("\\t"));
        break;
      default:
        if (c >= 0 && c < 32) {
          result.addAll(zc::str("\\u", zc::hex(static_cast<uint32_t>(c))));
        } else {
          result.add(c);
        }
        break;
    }
  }
  result.add('\0');
  return zc::String(result.releaseAsArray());
}

void ASTDumper::writeProperty(const zc::StringPtr name, const zc::StringPtr value, int indent) {
  int actualIndent = (indent == -1) ? currentIndent : indent;
  switch (impl->format) {
    case DumpFormat::kTEXT:
      writeIndent(actualIndent);
      impl->output.write(name.asBytes());
      impl->output.write(": "_zcb);
      impl->output.write(value.asBytes());
      impl->output.write("\n"_zcb);
      break;
    case DumpFormat::kJSON:
      writeIndent(actualIndent);
      impl->output.write("\""_zcb);
      impl->output.write(name.asBytes());
      impl->output.write("\": \""_zcb);
      impl->output.write(escapeJsonString(value).asBytes());
      impl->output.write("\""_zcb);
      break;
    case DumpFormat::kXML:
      // XML properties are handled differently in specific visit methods
      break;
  }
}

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
