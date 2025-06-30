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
  if (impl->format == DumpFormat::kJSON && impl->isFirstNode) {
    impl->output.write("[\n"_zcb);
    impl->isFirstNode = false;
  }
  dumpNode(node);
  if (impl->format == DumpFormat::kJSON) { impl->output.write("\n]"_zcb); }
}

void ASTDumper::dumpSourceFile(const SourceFile& sourceFile) {
  switch (impl->format) {
    case DumpFormat::kText:
      writeNodeHeader("SourceFile");
      break;
    case DumpFormat::kJSON:
      impl->output.write("{\n"_zcb);
      writeProperty("type", "SourceFile", 1);
      impl->output.write(",\n"_zcb);
      writeIndent(1);
      impl->output.write("\"children\": [\n"_zcb);
      break;
    case DumpFormat::kXML:
      impl->output.write("<SourceFile>\n"_zcb);
      break;
  }

  // Note: We would need to add methods to SourceFile to access its statements
  // For now, we'll just indicate that children would be dumped here
  switch (impl->format) {
    case DumpFormat::kText:
      writeLine("// Children: statements (not accessible in current API)", 1);
      break;
    case DumpFormat::kJSON:
      writeIndent(2);
      impl->output.write(
          "{\"type\": \"placeholder\", \"note\": \"statements not accessible\"}\n"_zcb);
      break;
    case DumpFormat::kXML:
      writeIndent(1);
      impl->output.write("<!-- statements not accessible in current API -->\n"_zcb);
      break;
  }

  switch (impl->format) {
    case DumpFormat::kText:
      writeNodeFooter("SourceFile");
      break;
    case DumpFormat::kJSON:
      writeIndent(1);
      impl->output.write("]\n}"_zcb);
      break;
    case DumpFormat::kXML:
      impl->output.write("</SourceFile>\n"_zcb);
      break;
  }
}

void ASTDumper::dumpNode(const Node& node, int indent) {
  switch (node.getKind()) {
    case SyntaxKind::kSourceFile:
      dumpSourceFile(static_cast<const SourceFile&>(node));
      break;
    case SyntaxKind::kImportDeclaration:
    case SyntaxKind::kExportDeclaration:
    case SyntaxKind::kVariableDeclaration:
      dumpStatement(static_cast<const Statement&>(node), indent);
      break;
    case SyntaxKind::kBinaryExpression:
      dumpExpression(static_cast<const Expression&>(node), indent);
      break;
    case SyntaxKind::kStatement:
      dumpStatement(static_cast<const Statement&>(node), indent);
      break;
    case SyntaxKind::kExpression:
      dumpExpression(static_cast<const Expression&>(node), indent);
      break;
    default:
      // Generic node dump
      writeNodeHeader("Node", indent);
      writeNodeFooter("Node", indent);
      break;
  }
}

void ASTDumper::dumpStatement(const Statement& stmt, int indent) {
  switch (stmt.getKind()) {
    case SyntaxKind::kImportDeclaration:
      dumpImportDeclaration(static_cast<const ImportDeclaration&>(stmt), indent);
      break;
    case SyntaxKind::kExportDeclaration:
      dumpExportDeclaration(static_cast<const ExportDeclaration&>(stmt), indent);
      break;
    case SyntaxKind::kVariableDeclaration:
      dumpVariableDeclaration(static_cast<const VariableDeclaration&>(stmt), indent);
      break;
    default:
      // Generic statement dump
      writeNodeHeader("Statement", indent);
      writeNodeFooter("Statement", indent);
      break;
  }
}

void ASTDumper::dumpExpression(const Expression& expr, int indent) {
  switch (expr.getKind()) {
    case SyntaxKind::kBinaryExpression:
      dumpBinaryExpression(static_cast<const BinaryExpression&>(expr), indent);
      break;
    default:
      // Generic expression dump
      writeNodeHeader("Expression", indent);
      writeNodeFooter("Expression", indent);
      break;
  }
}

void ASTDumper::dumpImportDeclaration(const ImportDeclaration& importDecl, int indent) {
  switch (impl->format) {
    case DumpFormat::kText:
      writeNodeHeader("ImportDeclaration", indent);
      dumpModulePath(importDecl.getModulePath(), indent + 1);
      ZC_IF_SOME(alias, importDecl.getAlias()) { writeProperty("alias", alias, indent + 1); }
      writeNodeFooter("ImportDeclaration", indent);
      break;
    case DumpFormat::kJSON:
      writeIndent(indent);
      impl->output.write("{\n"_zcb);
      writeProperty("type", "ImportDeclaration", indent + 1);
      impl->output.write(",\n"_zcb);
      writeIndent(indent + 1);
      impl->output.write("\"modulePath\": \""_zcb);
      impl->output.write(importDecl.getModulePath().toString().asBytes());
      impl->output.write("\""_zcb);
      ZC_IF_SOME(alias, importDecl.getAlias()) {
        impl->output.write(",\n"_zcb);
        writeProperty("alias", alias, indent + 1);
      }
      impl->output.write("\n"_zcb);
      writeIndent(indent);
      impl->output.write("}"_zcb);
      break;
    case DumpFormat::kXML:
      writeIndent(indent);
      impl->output.write("<ImportDeclaration>\n"_zcb);
      writeIndent(indent + 1);
      impl->output.write("<modulePath>"_zcb);
      impl->output.write(importDecl.getModulePath().toString().asBytes());
      impl->output.write("</modulePath>\n"_zcb);
      ZC_IF_SOME(alias, importDecl.getAlias()) {
        writeIndent(indent + 1);
        impl->output.write("<alias>"_zcb);
        impl->output.write(alias.asBytes());
        impl->output.write("</alias>\n"_zcb);
      }
      writeIndent(indent);
      impl->output.write("</ImportDeclaration>\n"_zcb);
      break;
  }
}

void ASTDumper::dumpExportDeclaration(const ExportDeclaration& exportDecl, int indent) {
  switch (impl->format) {
    case DumpFormat::kText:
      writeNodeHeader("ExportDeclaration", indent);
      writeProperty("identifier", exportDecl.getIdentifier(), indent + 1);
      if (exportDecl.isRename()) {
        ZC_IF_SOME(alias, exportDecl.getAlias()) { writeProperty("alias", alias, indent + 1); }
        ZC_IF_SOME(modulePath, exportDecl.getModulePath()) {
          dumpModulePath(modulePath, indent + 1);
        }
      }
      writeNodeFooter("ExportDeclaration", indent);
      break;
    case DumpFormat::kJSON:
      writeIndent(indent);
      impl->output.write("{\n"_zcb);
      writeProperty("type", "ExportDeclaration", indent + 1);
      impl->output.write(",\n"_zcb);
      writeProperty("identifier", exportDecl.getIdentifier(), indent + 1);
      if (exportDecl.isRename()) {
        ZC_IF_SOME(alias, exportDecl.getAlias()) {
          impl->output.write(",\n"_zcb);
          writeProperty("alias", alias, indent + 1);
        }
        ZC_IF_SOME(modulePath, exportDecl.getModulePath()) {
          impl->output.write(",\n"_zcb);
          writeIndent(indent + 1);
          impl->output.write("\"modulePath\": \""_zcb);
          impl->output.write(modulePath.toString().asBytes());
          impl->output.write("\""_zcb);
        }
      }
      impl->output.write("\n"_zcb);
      writeIndent(indent);
      impl->output.write("}"_zcb);
      break;
    case DumpFormat::kXML:
      writeIndent(indent);
      impl->output.write("<ExportDeclaration>\n"_zcb);
      writeIndent(indent + 1);
      impl->output.write("<identifier>"_zcb);
      impl->output.write(exportDecl.getIdentifier().asBytes());
      impl->output.write("</identifier>\n"_zcb);
      if (exportDecl.isRename()) {
        ZC_IF_SOME(alias, exportDecl.getAlias()) {
          writeIndent(indent + 1);
          impl->output.write("<alias>"_zcb);
          impl->output.write(alias.asBytes());
          impl->output.write("</alias>\n"_zcb);
        }
        ZC_IF_SOME(modulePath, exportDecl.getModulePath()) {
          writeIndent(indent + 1);
          impl->output.write("<modulePath>"_zcb);
          impl->output.write(modulePath.toString().asBytes());
          impl->output.write("</modulePath>\n"_zcb);
        }
      }
      writeIndent(indent);
      impl->output.write("</ExportDeclaration>\n"_zcb);
      break;
  }
}

void ASTDumper::dumpModulePath(const ModulePath& modulePath, int indent) {
  switch (impl->format) {
    case DumpFormat::kText:
      writeProperty("modulePath", modulePath.toString(), indent);
      break;
    case DumpFormat::kJSON:
      writeProperty("modulePath", modulePath.toString(), indent);
      break;
    case DumpFormat::kXML:
      writeIndent(indent);
      impl->output.write("<modulePath>"_zcb);
      impl->output.write(modulePath.toString().asBytes());
      impl->output.write("</modulePath>\n"_zcb);
      break;
  }
}

void ASTDumper::dumpVariableDeclaration(const VariableDeclaration& varDecl, int indent) {
  switch (impl->format) {
    case DumpFormat::kText:
      writeNodeHeader("VariableDeclaration", indent);
      if (auto* type = varDecl.getType()) {
        // TODO: Implement proper type string representation
        writeProperty("type", "Type", indent + 1);
      } else {
        writeProperty("type", "auto", indent + 1);
      }
      writeProperty("name", varDecl.getName()->getName(), indent + 1);
      if (varDecl.getInitializer()) {
        writeLine("initializer:", indent + 1);
        dumpExpression(*varDecl.getInitializer(), indent + 2);
      }
      writeNodeFooter("VariableDeclaration", indent);
      break;
    case DumpFormat::kJSON:
      writeIndent(indent);
      impl->output.write("{\n"_zcb);
      writeProperty("type", "VariableDeclaration", indent + 1);
      impl->output.write(",\n"_zcb);
      if (auto* type = varDecl.getType()) {
        // TODO: Implement proper type string representation
        writeProperty("varType", "Type", indent + 1);
      } else {
        writeProperty("varType", "auto", indent + 1);
      }
      impl->output.write(",\n"_zcb);
      writeProperty("name", varDecl.getName()->getName(), indent + 1);
      if (varDecl.getInitializer()) {
        impl->output.write(",\n"_zcb);
        writeIndent(indent + 1);
        impl->output.write("\"initializer\": "_zcb);
        dumpExpression(*varDecl.getInitializer(), 0);
      }
      impl->output.write("\n"_zcb);
      writeIndent(indent);
      impl->output.write("}"_zcb);
      break;
    case DumpFormat::kXML:
      writeIndent(indent);
      impl->output.write("<VariableDeclaration>\n"_zcb);
      writeIndent(indent + 1);
      impl->output.write("<varType>"_zcb);
      if (auto* type = varDecl.getType()) {
        // TODO: Implement proper type string representation
        impl->output.write("Type"_zcb);
      } else {
        impl->output.write("auto"_zcb);
      }
      impl->output.write("</varType>\n"_zcb);
      writeIndent(indent + 1);
      impl->output.write("<name>"_zcb);
      impl->output.write(varDecl.getName()->getName().asBytes());
      impl->output.write("</name>\n"_zcb);
      if (varDecl.getInitializer()) {
        writeIndent(indent + 1);
        impl->output.write("<initializer>\n"_zcb);
        dumpExpression(*varDecl.getInitializer(), indent + 2);
        writeIndent(indent + 1);
        impl->output.write("</initializer>\n"_zcb);
      }
      writeIndent(indent);
      impl->output.write("</VariableDeclaration>\n"_zcb);
      break;
  }
}

void ASTDumper::dumpBinaryExpression(const BinaryExpression& binExpr, int indent) {
  switch (impl->format) {
    case DumpFormat::kText:
      writeNodeHeader("BinaryExpression", indent);
      writeProperty("operator", binExpr.getOperator()->getSymbol(), indent + 1);
      writeLine("left:", indent + 1);
      dumpExpression(*binExpr.getLeft(), indent + 2);
      writeLine("right:", indent + 1);
      dumpExpression(*binExpr.getRight(), indent + 2);
      writeNodeFooter("BinaryExpression", indent);
      break;
    case DumpFormat::kJSON:
      writeIndent(indent);
      impl->output.write("{\n"_zcb);
      writeProperty("type", "BinaryExpression", indent + 1);
      impl->output.write(",\n"_zcb);
      writeProperty("operator", binExpr.getOperator()->getSymbol(), indent + 1);
      impl->output.write(",\n"_zcb);
      writeIndent(indent + 1);
      impl->output.write("\"left\": "_zcb);
      dumpExpression(*binExpr.getLeft(), 0);
      impl->output.write(",\n"_zcb);
      writeIndent(indent + 1);
      impl->output.write("\"right\": "_zcb);
      dumpExpression(*binExpr.getRight(), 0);
      impl->output.write("\n"_zcb);
      writeIndent(indent);
      impl->output.write("}"_zcb);
      break;
    case DumpFormat::kXML:
      writeIndent(indent);
      impl->output.write("<BinaryExpression>\n"_zcb);
      writeIndent(indent + 1);
      impl->output.write("<operator>"_zcb);
      impl->output.write(binExpr.getOperator()->getSymbol().asBytes());
      impl->output.write("</operator>\n"_zcb);
      writeIndent(indent + 1);
      impl->output.write("<left>\n"_zcb);
      dumpExpression(*binExpr.getLeft(), indent + 2);
      writeIndent(indent + 1);
      impl->output.write("</left>\n"_zcb);
      writeIndent(indent + 1);
      impl->output.write("<right>\n"_zcb);
      dumpExpression(*binExpr.getRight(), indent + 2);
      writeIndent(indent + 1);
      impl->output.write("</right>\n"_zcb);
      writeIndent(indent);
      impl->output.write("</BinaryExpression>\n"_zcb);
      break;
  }
}

void ASTDumper::writeIndent(int indent) {
  for (int i = 0; i < indent; ++i) { impl->output.write("  "_zcb); }
}

void ASTDumper::writeLine(const zc::StringPtr text, int indent) {
  writeIndent(indent);
  impl->output.write(text.asBytes());
  impl->output.write("\n"_zcb);
}

void ASTDumper::writeNodeHeader(const zc::StringPtr nodeType, int indent) {
  switch (impl->format) {
    case DumpFormat::kText:
      writeLine(zc::str(nodeType, " {"), indent);
      break;
    case DumpFormat::kJSON:
      writeIndent(indent);
      impl->output.write("{\n"_zcb);
      writeProperty("type", nodeType, indent + 1);
      break;
    case DumpFormat::kXML:
      writeIndent(indent);
      impl->output.write(zc::str("<", nodeType, ">\n").asBytes());
      break;
  }
}

void ASTDumper::writeNodeFooter(const zc::StringPtr nodeType, int indent) {
  switch (impl->format) {
    case DumpFormat::kText:
      writeLine("}", indent);
      break;
    case DumpFormat::kJSON:
      impl->output.write("\n"_zcb);
      writeIndent(indent);
      impl->output.write("}"_zcb);
      break;
    case DumpFormat::kXML:
      writeIndent(indent);
      impl->output.write(zc::str("</", nodeType, ">\n").asBytes());
      break;
  }
}

void ASTDumper::writeProperty(const zc::StringPtr name, const zc::StringPtr value, int indent) {
  switch (impl->format) {
    case DumpFormat::kText:
      writeLine(zc::str(name, ": ", value), indent);
      break;
    case DumpFormat::kJSON:
      writeIndent(indent);
      impl->output.write(zc::str("\"", name, "\": \"", value, "\"").asBytes());
      break;
    case DumpFormat::kXML:
      writeIndent(indent);
      impl->output.write(zc::str("<", name, ">", value, "</", name, ">\n").asBytes());
      break;
  }
}

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
