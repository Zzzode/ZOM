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

void ASTDumper::dump(const Node& node) { dumpNode(node); }

void ASTDumper::dumpSourceFile(const SourceFile& sourceFile) {
  switch (impl->format) {
    case DumpFormat::kTEXT:
      writeNodeHeader("SourceFile");
      writeProperty("fileName", sourceFile.getFileName(), 1);
      writeLine("statements:", 1);
      for (const auto& stmt : sourceFile.getStatements()) { dumpStatement(stmt, 2); }
      writeNodeFooter("SourceFile");
      break;
    case DumpFormat::kJSON:
      impl->output.write("{\n"_zcb);
      writeProperty("type", "SourceFile", 1);
      impl->output.write(",\n"_zcb);
      writeProperty("fileName", sourceFile.getFileName(), 1);
      impl->output.write(",\n"_zcb);
      writeIndent(1);
      impl->output.write("\"children\": [\n"_zcb);
      {
        bool first = true;
        for (const auto& stmt : sourceFile.getStatements()) {
          if (!first) { impl->output.write(",\n"_zcb); }
          first = false;
          dumpStatement(stmt, 2);
        }
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
      for (const auto& stmt : sourceFile.getStatements()) { dumpStatement(stmt, 2); }
      writeIndent(1);
      impl->output.write("</statements>\n"_zcb);
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
    case SyntaxKind::kBlockStatement:
      dumpBlockStatement(static_cast<const BlockStatement&>(stmt), indent);
      break;
    case SyntaxKind::kExpressionStatement:
      dumpExpressionStatement(static_cast<const ExpressionStatement&>(stmt), indent);
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
    case SyntaxKind::kFunctionExpression:
      dumpFunctionExpression(static_cast<const FunctionExpression&>(expr), indent);
      break;
    case SyntaxKind::kStringLiteral:
      dumpStringLiteral(static_cast<const StringLiteral&>(expr), indent);
      break;
    case SyntaxKind::kNumericLiteral:
      dumpNumericLiteral(static_cast<const NumericLiteral&>(expr), indent);
      break;
    case SyntaxKind::kBooleanLiteral:
      dumpBooleanLiteral(static_cast<const BooleanLiteral&>(expr), indent);
      break;
    case SyntaxKind::kNilLiteral:
      dumpNilLiteral(static_cast<const NilLiteral&>(expr), indent);
      break;
    case SyntaxKind::kCallExpression:
      dumpCallExpression(static_cast<const CallExpression&>(expr), indent);
      break;
    case SyntaxKind::kNewExpression:
      dumpNewExpression(static_cast<const NewExpression&>(expr), indent);
      break;
    case SyntaxKind::kArrayLiteralExpression:
      dumpArrayLiteralExpression(static_cast<const ArrayLiteralExpression&>(expr), indent);
      break;
    case SyntaxKind::kObjectLiteralExpression:
      dumpObjectLiteralExpression(static_cast<const ObjectLiteralExpression&>(expr), indent);
      break;
    case SyntaxKind::kParenthesizedExpression:
      dumpParenthesizedExpression(static_cast<const ParenthesizedExpression&>(expr), indent);
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
    case DumpFormat::kTEXT:
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
    case DumpFormat::kTEXT:
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
    case DumpFormat::kTEXT:
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

void ASTDumper::dumpBindingElement(const BindingElement& bindingElement, int indent) {
  switch (impl->format) {
    case DumpFormat::kTEXT:
      writeNodeHeader("BindingElement", indent);
      writeProperty("name", bindingElement.getName()->getName(), indent + 1);
      if (auto* type = bindingElement.getType()) {
        (void)type;  // Suppress unused variable warning
        // TODO: Implement proper type string representation
        writeProperty("type", "Type", indent + 1);
      }
      if (bindingElement.getInitializer()) {
        writeLine("initializer:", indent + 1);
        dumpExpression(*bindingElement.getInitializer(), indent + 2);
      }
      writeNodeFooter("BindingElement", indent);
      break;
    case DumpFormat::kJSON:
      writeIndent(indent);
      impl->output.write("{\n"_zcb);
      writeProperty("type", "BindingElement", indent + 1);
      impl->output.write(",\n"_zcb);
      writeProperty("name", bindingElement.getName()->getName(), indent + 1);
      if (auto* type = bindingElement.getType()) {
        (void)type;  // Suppress unused variable warning
        impl->output.write(",\n"_zcb);
        writeProperty("varType", "Type", indent + 1);
      }
      if (bindingElement.getInitializer()) {
        impl->output.write(",\n"_zcb);
        writeIndent(indent + 1);
        impl->output.write("\"initializer\": \n"_zcb);
        dumpExpression(*bindingElement.getInitializer(), indent + 1);
      }
      impl->output.write("\n"_zcb);
      writeIndent(indent);
      impl->output.write("}"_zcb);
      break;
    case DumpFormat::kXML:
      writeIndent(indent);
      impl->output.write("<BindingElement>\n"_zcb);
      writeIndent(indent + 1);
      impl->output.write("<name>"_zcb);
      impl->output.write(bindingElement.getName()->getName().asBytes());
      impl->output.write("</name>\n"_zcb);
      if (auto* type = bindingElement.getType()) {
        (void)type;  // Suppress unused variable warning
        writeIndent(indent + 1);
        impl->output.write("<varType>Type</varType>\n"_zcb);
      }
      if (bindingElement.getInitializer()) {
        writeIndent(indent + 1);
        impl->output.write("<initializer>\n"_zcb);
        dumpExpression(*bindingElement.getInitializer(), indent + 2);
        writeIndent(indent + 1);
        impl->output.write("</initializer>\n"_zcb);
      }
      writeIndent(indent);
      impl->output.write("</BindingElement>\n"_zcb);
      break;
  }
}

void ASTDumper::dumpVariableDeclaration(const VariableDeclaration& varDecl, int indent) {
  switch (impl->format) {
    case DumpFormat::kTEXT:
      writeNodeHeader("VariableDeclaration", indent);
      writeLine("bindings:", indent + 1);
      for (const auto& binding : varDecl.getBindings()) { dumpBindingElement(binding, indent + 2); }
      writeNodeFooter("VariableDeclaration", indent);
      break;
    case DumpFormat::kJSON: {
      writeIndent(indent);
      impl->output.write("{\n"_zcb);
      writeProperty("type", "VariableDeclaration", indent + 1);
      impl->output.write(",\n"_zcb);
      writeIndent(indent + 1);
      impl->output.write("\"bindings\": [\n"_zcb);
      bool first = true;
      for (const auto& binding : varDecl.getBindings()) {
        if (!first) { impl->output.write(",\n"_zcb); }
        first = false;
        dumpBindingElement(binding, indent + 2);
      }
      impl->output.write("\n"_zcb);
      writeIndent(indent + 1);
      impl->output.write("]\n"_zcb);
      writeIndent(indent);
      impl->output.write("}"_zcb);
      break;
    }
    case DumpFormat::kXML:
      writeIndent(indent);
      impl->output.write("<VariableDeclaration>\n"_zcb);
      writeIndent(indent + 1);
      impl->output.write("<bindings>\n"_zcb);
      for (const auto& binding : varDecl.getBindings()) { dumpBindingElement(binding, indent + 2); }
      writeIndent(indent + 1);
      impl->output.write("</bindings>\n"_zcb);
      writeIndent(indent);
      impl->output.write("</VariableDeclaration>\n"_zcb);
      break;
  }
}

void ASTDumper::dumpBinaryExpression(const BinaryExpression& binExpr, int indent) {
  switch (impl->format) {
    case DumpFormat::kTEXT:
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
      impl->output.write("\"left\": \n"_zcb);
      dumpExpression(*binExpr.getLeft(), indent + 1);
      impl->output.write(",\n"_zcb);
      writeIndent(indent + 1);
      impl->output.write("\"right\": \n"_zcb);
      dumpExpression(*binExpr.getRight(), indent + 1);
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
    case DumpFormat::kTEXT:
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
    case DumpFormat::kTEXT:
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

zc::String escapeJsonString(const zc::StringPtr str) {
  zc::Vector<char> result;
  for (char c : str) {
    switch (c) {
      case '"':
        result.addAll("\\\""_zc);
        break;
      case '\\':
        result.addAll("\\\\"_zc);
        break;
      case '\b':
        result.addAll("\\b"_zc);
        break;
      case '\f':
        result.addAll("\\f"_zc);
        break;
      case '\n':
        result.addAll("\\n"_zc);
        break;
      case '\r':
        result.addAll("\\r"_zc);
        break;
      case '\t':
        result.addAll("\\t"_zc);
        break;
      default:
        if (c >= 0 && c < 32) {
          // Control characters - use zc::str and zc::hex for formatting
          auto hexStr = zc::hex((unsigned char)c);
          // Pad to 4 digits with leading zeros
          if (hexStr.size() == 1) {
            result.addAll(zc::str("\\u000", hexStr));
          } else if (hexStr.size() == 2) {
            result.addAll(zc::str("\\u00", hexStr));
          } else if (hexStr.size() == 3) {
            result.addAll(zc::str("\\u0", hexStr));
          } else {
            result.addAll(zc::str("\\u", hexStr));
          }
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
  switch (impl->format) {
    case DumpFormat::kTEXT:
      writeLine(zc::str(name, ": ", value), indent);
      break;
    case DumpFormat::kJSON: {
      writeIndent(indent);
      auto escapedValue = escapeJsonString(value);
      impl->output.write(zc::str("\"", name, "\": \"", escapedValue, "\"").asBytes());
      break;
    }
    case DumpFormat::kXML:
      writeIndent(indent);
      impl->output.write(zc::str("<", name, ">", value, "</", name, ">\n").asBytes());
      break;
  }
}

void ASTDumper::dumpFunctionExpression(const FunctionExpression& funcExpr, int indent) {
  switch (impl->format) {
    case DumpFormat::kTEXT:
      writeNodeHeader("FunctionExpression", indent);
      if (!funcExpr.getTypeParameters().empty()) {
        writeLine("typeParameters:", indent + 1);
        for (const auto& typeParam : funcExpr.getTypeParameters()) {
          (void)typeParam;  // Suppress unused variable warning
          // TODO: Implement TypeParameter dump
          writeLine("TypeParameter", indent + 2);
        }
      }
      if (!funcExpr.getParameters().empty()) {
        writeLine("parameters:", indent + 1);
        for (const auto& param : funcExpr.getParameters()) {
          dumpBindingElement(param, indent + 2);
        }
      }
      if (funcExpr.getReturnType()) {
        writeLine("returnType:", indent + 1);
        // TODO: Implement Type dump
        writeLine("Type", indent + 2);
      }
      if (funcExpr.getBody()) {
        writeLine("body:", indent + 1);
        dumpStatement(*funcExpr.getBody(), indent + 2);
      }
      writeNodeFooter("FunctionExpression", indent);
      break;
    case DumpFormat::kJSON:
      writeIndent(indent);
      impl->output.write("{\n"_zcb);
      writeProperty("type", "FunctionExpression", indent + 1);

      if (!funcExpr.getTypeParameters().empty()) {
        impl->output.write(",\n"_zcb);
        writeIndent(indent + 1);
        impl->output.write("\"typeParameters\": [\n"_zcb);
        bool first = true;
        for (const auto& typeParam : funcExpr.getTypeParameters()) {
          (void)typeParam;  // Suppress unused variable warning
          if (!first) { impl->output.write(",\n"_zcb); }
          first = false;
          writeIndent(indent + 2);
          impl->output.write("{\"type\": \"TypeParameter\"}"_zcb);
        }
        impl->output.write("\n"_zcb);
        writeIndent(indent + 1);
        impl->output.write("]"_zcb);
      }

      if (!funcExpr.getParameters().empty()) {
        impl->output.write(",\n"_zcb);
        writeIndent(indent + 1);
        impl->output.write("\"parameters\": [\n"_zcb);
        bool first = true;
        for (const auto& param : funcExpr.getParameters()) {
          if (!first) { impl->output.write(",\n"_zcb); }
          first = false;
          dumpBindingElement(param, indent + 2);
        }
        impl->output.write("\n"_zcb);
        writeIndent(indent + 1);
        impl->output.write("]"_zcb);
      }

      if (funcExpr.getReturnType()) {
        impl->output.write(",\n"_zcb);
        writeIndent(indent + 1);
        impl->output.write("\"returnType\": {\"type\": \"Type\"}"_zcb);
      }

      if (funcExpr.getBody()) {
        impl->output.write(",\n"_zcb);
        writeIndent(indent + 1);
        impl->output.write("\"body\": \n"_zcb);
        dumpStatement(*funcExpr.getBody(), indent + 1);
      }

      impl->output.write("\n"_zcb);
      writeIndent(indent);
      impl->output.write("}"_zcb);
      break;
    case DumpFormat::kXML:
      writeIndent(indent);
      impl->output.write("<FunctionExpression>\n"_zcb);
      if (!funcExpr.getTypeParameters().empty()) {
        writeIndent(indent + 1);
        impl->output.write("<typeParameters>\n"_zcb);
        for (const auto& typeParam : funcExpr.getTypeParameters()) {
          (void)typeParam;  // Suppress unused variable warning
          writeIndent(indent + 2);
          impl->output.write("<TypeParameter></TypeParameter>\n"_zcb);
        }
        writeIndent(indent + 1);
        impl->output.write("</typeParameters>\n"_zcb);
      }
      if (!funcExpr.getParameters().empty()) {
        writeIndent(indent + 1);
        impl->output.write("<parameters>\n"_zcb);
        for (const auto& param : funcExpr.getParameters()) {
          dumpBindingElement(param, indent + 2);
        }
        writeIndent(indent + 1);
        impl->output.write("</parameters>\n"_zcb);
      }
      if (funcExpr.getReturnType()) {
        writeIndent(indent + 1);
        impl->output.write("<returnType><Type></Type></returnType>\n"_zcb);
      }
      if (funcExpr.getBody()) {
        writeIndent(indent + 1);
        impl->output.write("<body>\n"_zcb);
        dumpStatement(*funcExpr.getBody(), indent + 2);
        writeIndent(indent + 1);
        impl->output.write("</body>\n"_zcb);
      }
      writeIndent(indent);
      impl->output.write("</FunctionExpression>\n"_zcb);
      break;
  }
}

void ASTDumper::dumpStringLiteral(const StringLiteral& strLit, int indent) {
  switch (impl->format) {
    case DumpFormat::kTEXT:
      writeNodeHeader("StringLiteral", indent);
      writeProperty("value", strLit.getValue(), indent + 1);
      writeNodeFooter("StringLiteral", indent);
      break;
    case DumpFormat::kJSON:
      writeIndent(indent);
      impl->output.write("{\n"_zcb);
      writeProperty("type", "StringLiteral", indent + 1);
      impl->output.write(",\n"_zcb);
      writeProperty("value", strLit.getValue(), indent + 1);
      impl->output.write("\n"_zcb);
      writeIndent(indent);
      impl->output.write("}"_zcb);
      break;
    case DumpFormat::kXML:
      writeIndent(indent);
      impl->output.write("<StringLiteral>\n"_zcb);
      writeProperty("value", strLit.getValue(), indent + 1);
      writeIndent(indent);
      impl->output.write("</StringLiteral>\n"_zcb);
      break;
  }
}

void ASTDumper::dumpNumericLiteral(const NumericLiteral& numLit, int indent) {
  switch (impl->format) {
    case DumpFormat::kTEXT:
      writeNodeHeader("NumericLiteral", indent);
      writeProperty("value", zc::str(numLit.getValue()), indent + 1);
      writeNodeFooter("NumericLiteral", indent);
      break;
    case DumpFormat::kJSON:
      writeIndent(indent);
      impl->output.write("{\n"_zcb);
      writeProperty("type", "NumericLiteral", indent + 1);
      impl->output.write(",\n"_zcb);
      writeProperty("value", zc::str(numLit.getValue()), indent + 1);
      impl->output.write("\n"_zcb);
      writeIndent(indent);
      impl->output.write("}"_zcb);
      break;
    case DumpFormat::kXML:
      writeIndent(indent);
      impl->output.write("<NumericLiteral>\n"_zcb);
      writeProperty("value", zc::str(numLit.getValue()), indent + 1);
      writeIndent(indent);
      impl->output.write("</NumericLiteral>\n"_zcb);
      break;
  }
}

void ASTDumper::dumpBooleanLiteral(const BooleanLiteral& boolLit, int indent) {
  switch (impl->format) {
    case DumpFormat::kTEXT:
      writeNodeHeader("BooleanLiteral", indent);
      writeProperty("value", boolLit.getValue() ? "true" : "false", indent + 1);
      writeNodeFooter("BooleanLiteral", indent);
      break;
    case DumpFormat::kJSON:
      writeIndent(indent);
      impl->output.write("{\n"_zcb);
      writeProperty("type", "BooleanLiteral", indent + 1);
      impl->output.write(",\n"_zcb);
      writeProperty("value", boolLit.getValue() ? "true" : "false", indent + 1);
      impl->output.write("\n"_zcb);
      writeIndent(indent);
      impl->output.write("}"_zcb);
      break;
    case DumpFormat::kXML:
      writeIndent(indent);
      impl->output.write("<BooleanLiteral>\n"_zcb);
      writeProperty("value", boolLit.getValue() ? "true" : "false", indent + 1);
      writeIndent(indent);
      impl->output.write("</BooleanLiteral>\n"_zcb);
      break;
  }
}

void ASTDumper::dumpNilLiteral(const NilLiteral& nilLit, int indent) {
  (void)nilLit;  // Suppress unused parameter warning
  switch (impl->format) {
    case DumpFormat::kTEXT:
      writeNodeHeader("NilLiteral", indent);
      writeNodeFooter("NilLiteral", indent);
      break;
    case DumpFormat::kJSON:
      writeIndent(indent);
      impl->output.write("{\n"_zcb);
      writeProperty("type", "NilLiteral", indent + 1);
      impl->output.write("\n"_zcb);
      writeIndent(indent);
      impl->output.write("}"_zcb);
      break;
    case DumpFormat::kXML:
      writeIndent(indent);
      impl->output.write("<NilLiteral>\n"_zcb);
      writeIndent(indent);
      impl->output.write("</NilLiteral>\n"_zcb);
      break;
  }
}

void ASTDumper::dumpCallExpression(const CallExpression& callExpr, int indent) {
  switch (impl->format) {
    case DumpFormat::kTEXT:
      writeNodeHeader("CallExpression", indent);
      writeNodeFooter("CallExpression", indent);
      break;
    case DumpFormat::kJSON:
      writeIndent(indent);
      impl->output.write("{\n"_zcb);
      writeProperty("type", "CallExpression", indent + 1);
      impl->output.write("\n"_zcb);
      writeIndent(indent);
      impl->output.write("}"_zcb);
      break;
    case DumpFormat::kXML:
      writeIndent(indent);
      impl->output.write("<CallExpression>\n"_zcb);
      writeIndent(indent);
      impl->output.write("</CallExpression>\n"_zcb);
      break;
  }
}

void ASTDumper::dumpNewExpression(const NewExpression& newExpr, int indent) {
  switch (impl->format) {
    case DumpFormat::kTEXT:
      writeNodeHeader("NewExpression", indent);
      writeNodeFooter("NewExpression", indent);
      break;
    case DumpFormat::kJSON:
      writeIndent(indent);
      impl->output.write("{\n"_zcb);
      writeProperty("type", "NewExpression", indent + 1);
      impl->output.write("\n"_zcb);
      writeIndent(indent);
      impl->output.write("}"_zcb);
      break;
    case DumpFormat::kXML:
      writeIndent(indent);
      impl->output.write("<NewExpression>\n"_zcb);
      writeIndent(indent);
      impl->output.write("</NewExpression>\n"_zcb);
      break;
  }
}

void ASTDumper::dumpArrayLiteralExpression(const ArrayLiteralExpression& arrLit, int indent) {
  switch (impl->format) {
    case DumpFormat::kTEXT:
      writeNodeHeader("ArrayLiteralExpression", indent);
      writeNodeFooter("ArrayLiteralExpression", indent);
      break;
    case DumpFormat::kJSON:
      writeIndent(indent);
      impl->output.write("{\n"_zcb);
      writeProperty("type", "ArrayLiteralExpression", indent + 1);
      impl->output.write("\n"_zcb);
      writeIndent(indent);
      impl->output.write("}"_zcb);
      break;
    case DumpFormat::kXML:
      writeIndent(indent);
      impl->output.write("<ArrayLiteralExpression>\n"_zcb);
      writeIndent(indent);
      impl->output.write("</ArrayLiteralExpression>\n"_zcb);
      break;
  }
}

void ASTDumper::dumpObjectLiteralExpression(const ObjectLiteralExpression& objLit, int indent) {
  switch (impl->format) {
    case DumpFormat::kTEXT:
      writeNodeHeader("ObjectLiteralExpression", indent);
      writeNodeFooter("ObjectLiteralExpression", indent);
      break;
    case DumpFormat::kJSON:
      writeIndent(indent);
      impl->output.write("{\n"_zcb);
      writeProperty("type", "ObjectLiteralExpression", indent + 1);
      impl->output.write("\n"_zcb);
      writeIndent(indent);
      impl->output.write("}"_zcb);
      break;
    case DumpFormat::kXML:
      writeIndent(indent);
      impl->output.write("<ObjectLiteralExpression>\n"_zcb);
      writeIndent(indent);
      impl->output.write("</ObjectLiteralExpression>\n"_zcb);
      break;
  }
}

void ASTDumper::dumpParenthesizedExpression(const ParenthesizedExpression& parenExpr, int indent) {
  switch (impl->format) {
    case DumpFormat::kTEXT:
      writeNodeHeader("ParenthesizedExpression", indent);
      writeNodeFooter("ParenthesizedExpression", indent);
      break;
    case DumpFormat::kJSON:
      writeIndent(indent);
      impl->output.write("{\n"_zcb);
      writeProperty("type", "ParenthesizedExpression", indent + 1);
      impl->output.write("\n"_zcb);
      writeIndent(indent);
      impl->output.write("}"_zcb);
      break;
    case DumpFormat::kXML:
      writeIndent(indent);
      impl->output.write("<ParenthesizedExpression>\n"_zcb);
      writeIndent(indent);
      impl->output.write("</ParenthesizedExpression>\n"_zcb);
      break;
  }
}

void ASTDumper::dumpBlockStatement(const BlockStatement& blockStmt, int indent) {
  switch (impl->format) {
    case DumpFormat::kTEXT:
      writeNodeHeader("BlockStatement", indent);
      for (const auto& stmt : blockStmt.getStatements()) { dumpStatement(stmt, indent + 1); }
      writeNodeFooter("BlockStatement", indent);
      break;
    case DumpFormat::kJSON: {
      writeIndent(indent);
      impl->output.write("{\n"_zcb);
      writeProperty("type", "BlockStatement", indent + 1);
      impl->output.write(",\n"_zcb);
      writeIndent(indent + 1);
      impl->output.write("\"statements\": [\n"_zcb);
      bool first = true;
      for (const auto& stmt : blockStmt.getStatements()) {
        if (!first) { impl->output.write(",\n"_zcb); }
        first = false;
        dumpStatement(stmt, indent + 2);
      }
      impl->output.write("\n"_zcb);
      writeIndent(indent + 1);
      impl->output.write("]\n"_zcb);
      writeIndent(indent);
      impl->output.write("}"_zcb);
      break;
    }
    case DumpFormat::kXML:
      writeIndent(indent);
      impl->output.write("<BlockStatement>\n"_zcb);
      for (const auto& stmt : blockStmt.getStatements()) { dumpStatement(stmt, indent + 1); }
      writeIndent(indent);
      impl->output.write("</BlockStatement>\n"_zcb);
      break;
  }
}

void ASTDumper::dumpExpressionStatement(const ExpressionStatement& exprStmt, int indent) {
  switch (impl->format) {
    case DumpFormat::kTEXT:
      writeNodeHeader("ExpressionStatement", indent);
      dumpExpression(*exprStmt.getExpression(), indent + 1);
      writeNodeFooter("ExpressionStatement", indent);
      break;
    case DumpFormat::kJSON:
      writeIndent(indent);
      impl->output.write("{\n"_zcb);
      writeProperty("type", "ExpressionStatement", indent + 1);
      impl->output.write(",\n"_zcb);
      writeIndent(indent + 1);
      impl->output.write("\"expression\": \n"_zcb);
      dumpExpression(*exprStmt.getExpression(), indent + 1);
      impl->output.write("\n"_zcb);
      writeIndent(indent);
      impl->output.write("}"_zcb);
      break;
    case DumpFormat::kXML:
      writeIndent(indent);
      impl->output.write("<ExpressionStatement>\n"_zcb);
      dumpExpression(*exprStmt.getExpression(), indent + 1);
      writeIndent(indent);
      impl->output.write("</ExpressionStatement>\n"_zcb);
      break;
  }
}

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
