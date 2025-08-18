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

#include "zomlang/compiler/ast/serializer.h"

#include <cstdio>

#include "zc/core/debug.h"

namespace zomlang {
namespace compiler {
namespace ast {

// JSON string escaping helper function
zc::String escapeJsonString(zc::StringPtr str) {
  zc::Vector<char> result;
  for (char c : str) {
    switch (c) {
      case '"':
        result.addAll("\\\""_zc.asArray());
        break;
      case '\\':
        result.addAll("\\\\"_zc.asArray());
        break;
      case '\b':
        result.addAll("\\b"_zc.asArray());
        break;
      case '\f':
        result.addAll("\\f"_zc.asArray());
        break;
      case '\n':
        result.addAll("\\n"_zc.asArray());
        break;
      case '\r':
        result.addAll("\\r"_zc.asArray());
        break;
      case '\t':
        result.addAll("\\t"_zc.asArray());
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buffer[7];
          std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned char>(c));
          result.addAll(zc::ArrayPtr<const char>(buffer, 6));
        } else {
          result.add(c);
        }
        break;
    }
  }
  return zc::str(result.asPtr());
}

// XML string escaping helper function
zc::String escapeXmlString(zc::StringPtr str) {
  zc::Vector<char> result;
  for (char c : str) {
    switch (c) {
      case '<':
        result.addAll("&lt;"_zc.asArray());
        break;
      case '>':
        result.addAll("&gt;"_zc.asArray());
        break;
      case '&':
        result.addAll("&amp;"_zc.asArray());
        break;
      case '"':
        result.addAll("&quot;"_zc.asArray());
        break;
      case '\'':
        result.addAll("&apos;"_zc.asArray());
        break;
      default:
        result.add(c);
        break;
    }
  }
  return zc::str(result.asPtr());
}

// JSONSerializer implementation
struct JSONSerializer::Impl {
  zc::OutputStream& output;
  int indentLevel = 0;
  zc::Vector<bool> firstPropertyStack;  // Stack to track firstProperty state for nested objects

  explicit Impl(zc::OutputStream& out) : output(out) {
    firstPropertyStack.add(true);  // Root level starts as first property
  }

  void write(zc::StringPtr str) { output.write(str.asBytes()); }

  void writeIndent() {
    for (int i = 0; i < indentLevel; ++i) { write("  "); }
  }

  bool& currentFirstProperty() {
    ZC_ASSERT(!firstPropertyStack.empty(), "firstPropertyStack should never be empty");
    return firstPropertyStack.back();
  }

  void pushScope() { firstPropertyStack.add(true); }

  void popScope() {
    ZC_ASSERT(firstPropertyStack.size() > 1, "Cannot pop root scope");
    firstPropertyStack.removeLast();
  }
};

JSONSerializer::JSONSerializer(zc::OutputStream& output) noexcept : impl(zc::heap<Impl>(output)) {
  // Don't write root object here - let the first node handle it
}

JSONSerializer::~JSONSerializer() noexcept(false) {
  // Don't close root object here - let the last node handle it
}

void JSONSerializer::writeNodeStart(const zc::StringPtr nodeType) {
  // Note: In array context, writeArrayElement() already handles comma and indent
  // Only add comma if we're not in array context (firstProperty is managed by parent scope)
  impl->write("{\n");
  impl->indentLevel++;
  impl->pushScope();  // Enter new object scope
  writeProperty("node"_zc, nodeType);
}

void JSONSerializer::writeNodeEnd(const zc::StringPtr nodeType) {
  impl->popScope();  // Exit object scope
  impl->indentLevel--;
  impl->write("\n");
  impl->writeIndent();
  impl->write("}");
}

void JSONSerializer::writeProperty(const zc::StringPtr name, const zc::StringPtr value) {
  if (!impl->currentFirstProperty()) { impl->write(",\n"); }
  impl->currentFirstProperty() = false;
  impl->writeIndent();
  zc::String escapedValue = escapeJsonString(value);
  impl->write(zc::str("\"", name, "\": \"", escapedValue, "\""));
}

void JSONSerializer::writeChildStart(const zc::StringPtr name) {
  if (!impl->currentFirstProperty()) { impl->write(",\n"); }
  impl->currentFirstProperty() = false;
  impl->writeIndent();
  impl->write(zc::str("\"", name, "\": "));
  // Child value will be written immediately after this
}

void JSONSerializer::writeChildEnd(const zc::StringPtr name) {
  // Don't pop scope here - child objects manage their own scope
}

void JSONSerializer::writeArrayStart(const zc::StringPtr name, size_t count) {
  if (!impl->currentFirstProperty()) { impl->write(",\n"); }
  impl->currentFirstProperty() = false;
  impl->writeIndent();
  impl->write(zc::str("\"", name, "\": [\n"));
  impl->indentLevel++;
  impl->pushScope();  // Enter array scope - reset firstProperty for array elements
}

void JSONSerializer::writeArrayEnd(const zc::StringPtr name) {
  impl->popScope();  // Exit array scope
  impl->indentLevel--;
  impl->write("\n");
  impl->writeIndent();
  impl->write("]");
}

void JSONSerializer::writeArrayElement() {
  // For arrays: first element has no comma, subsequent elements have comma
  bool isFirst = impl->currentFirstProperty();
  if (!isFirst) { impl->write(",\n"); }
  impl->currentFirstProperty() = false;
  impl->writeIndent();
  // Array element content will be written after this
}

void JSONSerializer::writeNull() { impl->write("null"); }

void JSONSerializer::increaseIndent() { impl->indentLevel++; }

void JSONSerializer::decreaseIndent() { impl->indentLevel--; }

// XMLSerializer implementation
struct XMLSerializer::Impl {
  zc::OutputStream& output;
  int indentLevel = 0;

  explicit Impl(zc::OutputStream& out) : output(out) {}

  void write(zc::StringPtr str) { output.write(str.asBytes()); }

  void writeIndent() {
    for (int i = 0; i < indentLevel; ++i) { write("  "); }
  }
};

XMLSerializer::XMLSerializer(zc::OutputStream& output) noexcept : impl(zc::heap<Impl>(output)) {
  impl->write("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
}

XMLSerializer::~XMLSerializer() noexcept(false) = default;

void XMLSerializer::writeNodeStart(const zc::StringPtr nodeType) {
  impl->writeIndent();
  impl->write(zc::str("<", nodeType, ">\n"));
  impl->indentLevel++;
}

void XMLSerializer::writeNodeEnd(const zc::StringPtr nodeType) {
  impl->indentLevel--;
  impl->writeIndent();
  impl->write(zc::str("</", nodeType, ">\n"));
}

void XMLSerializer::writeProperty(const zc::StringPtr name, const zc::StringPtr value) {
  impl->writeIndent();
  auto escapedValue = escapeXmlString(value);
  impl->write(zc::str("<", name, ">", escapedValue, "</", name, ">\n"));
}

void XMLSerializer::writeChildStart(const zc::StringPtr name) {
  impl->writeIndent();
  impl->write(zc::str("<", name, ">\n"));
  impl->indentLevel++;
}

void XMLSerializer::writeChildEnd(const zc::StringPtr name) {
  impl->indentLevel--;
  impl->writeIndent();
  impl->write(zc::str("</", name, ">\n"));
}

void XMLSerializer::writeArrayStart(const zc::StringPtr name, size_t count) {
  impl->writeIndent();
  impl->write(zc::str("<", name, ">\n"));
  impl->indentLevel++;
}

void XMLSerializer::writeArrayEnd(const zc::StringPtr name) {
  impl->indentLevel--;
  impl->writeIndent();
  impl->write(zc::str("</", name, ">\n"));
}

void XMLSerializer::writeArrayElement() {
  // XML array element handling
}

void XMLSerializer::writeNull() { impl->write("<null/>"); }

void XMLSerializer::increaseIndent() { impl->indentLevel++; }

void XMLSerializer::decreaseIndent() { impl->indentLevel--; }

// TextSerializer implementation
struct TextSerializer::Impl {
  zc::OutputStream& output;
  int indentLevel = 0;

  explicit Impl(zc::OutputStream& out) : output(out) {}

  void write(zc::StringPtr str) { output.write(str.asBytes()); }

  void writeIndent() {
    for (int i = 0; i < indentLevel; ++i) { write("  "); }
  }
};

TextSerializer::TextSerializer(zc::OutputStream& output) noexcept : impl(zc::heap<Impl>(output)) {}

TextSerializer::~TextSerializer() noexcept(false) = default;

void TextSerializer::writeNodeStart(const zc::StringPtr nodeType) {
  impl->writeIndent();
  impl->write(zc::str(nodeType, " {\n"));
  impl->indentLevel++;
}

void TextSerializer::writeNodeEnd(const zc::StringPtr nodeType) {
  impl->indentLevel--;
  impl->writeIndent();
  impl->write("}\n");
}

void TextSerializer::writeProperty(const zc::StringPtr name, const zc::StringPtr value) {
  impl->writeIndent();
  impl->write(zc::str(name, ": ", value, "\n"));
}

void TextSerializer::writeChildStart(const zc::StringPtr name) {
  impl->writeIndent();
  impl->write(zc::str(name, ":\n"));
  impl->indentLevel++;
}

void TextSerializer::writeChildEnd(const zc::StringPtr name) { impl->indentLevel--; }

void TextSerializer::writeArrayStart(const zc::StringPtr name, size_t count) {
  impl->writeIndent();
  impl->write(zc::str(name, ": [\n"));
  impl->indentLevel++;
}

void TextSerializer::writeArrayEnd(const zc::StringPtr name) {
  impl->indentLevel--;
  impl->writeIndent();
  impl->write("]\n");
}

void TextSerializer::writeArrayElement() {
  // Text format array element handling
}

void TextSerializer::writeNull() {
  impl->writeIndent();
  impl->write("null\n");
}

void TextSerializer::increaseIndent() { impl->indentLevel++; }

void TextSerializer::decreaseIndent() { impl->indentLevel--; }

// Factory function implementation
zc::Own<Serializer> createSerializer(const zc::StringPtr format, zc::OutputStream& output) {
  if (format == "json") {
    return zc::heap<JSONSerializer>(output);
  } else if (format == "xml") {
    return zc::heap<XMLSerializer>(output);
  } else {
    return zc::heap<TextSerializer>(output);
  }
}

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
