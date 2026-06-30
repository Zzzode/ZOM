// Copyright (c) 2024-2025 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "zomlang/compiler/ast/dump.h"

#include <cstdint>

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/string.h"
#include "zomlang/compiler/ast/generated/node-accessors.h"
#include "zomlang/compiler/ast/generated/node-schema.h"

namespace zomlang {
namespace compiler {
namespace ast {
namespace {

void writeText(zc::OutputStream& output, zc::StringPtr text) { output.write(text.asBytes()); }

void writeByte(zc::OutputStream& output, char value) {
  const zc::byte byte = static_cast<zc::byte>(value);
  output.write(zc::arrayPtr(byte));
}

void writeIndent(zc::OutputStream& output, uint32_t indent) {
  for (uint32_t i = 0; i < indent; ++i) { writeByte(output, ' '); }
}

void writeJsonString(zc::OutputStream& output, zc::StringPtr text) {
  writeByte(output, '"');
  static constexpr char kHex[] = "0123456789abcdef";
  for (char ch : text) {
    const auto byte = static_cast<uint8_t>(ch);
    switch (ch) {
      case '"':
        output.write("\\\""_zcb);
        break;
      case '\\':
        output.write("\\\\"_zcb);
        break;
      case '\b':
        output.write("\\b"_zcb);
        break;
      case '\f':
        output.write("\\f"_zcb);
        break;
      case '\n':
        output.write("\\n"_zcb);
        break;
      case '\r':
        output.write("\\r"_zcb);
        break;
      case '\t':
        output.write("\\t"_zcb);
        break;
      default:
        if (byte < 0x20) {
          const char escape[] = {'\\', 'u', '0', '0', kHex[byte >> 4], kHex[byte & 0x0f]};
          output.write(zc::arrayPtr(escape, 6).asBytes());
        } else {
          writeByte(output, ch);
        }
        break;
    }
  }
  writeByte(output, '"');
}

zc::StringPtr basename(zc::StringPtr text) {
  size_t start = 0;
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '/' || text[i] == '\\') { start = i + 1; }
  }
  return text.slice(start);
}

zc::StringPtr normalizeStringField(const NodeSchemaFieldEntry& field, zc::StringPtr value) {
  return zc::StringPtr(field.name) == "file_name"_zc ? basename(value) : value;
}

uint32_t readWord(const Node& node, const NodeSchemaFieldEntry& field) {
  return node.payload.words[field.firstWord];
}

uint64_t readUInt64(const Node& node, const NodeSchemaFieldEntry& field) {
  return static_cast<uint64_t>(node.payload.words[field.firstWord]) |
         (static_cast<uint64_t>(node.payload.words[field.secondWord]) << 32);
}

NodeId readNodeId(const Node& node, const NodeSchemaFieldEntry& field) {
  return NodeId(readWord(node, field));
}

NodeList readNodeList(const Node& node, const NodeSchemaFieldEntry& field) {
  NodeList list;
  list.first = node.payload.words[field.firstWord];
  list.size = node.payload.words[field.secondWord];
  return list;
}

IdentList readIdentList(const Node& node, const NodeSchemaFieldEntry& field) {
  IdentList list;
  list.first = node.payload.words[field.firstWord];
  list.size = node.payload.words[field.secondWord];
  return list;
}

zc::StringPtr enumValueName(const NodeSchemaFieldEntry& field, uint32_t value) {
  for (uint32_t index = 0; index < field.enumValueCount; ++index) {
    if (field.enumValues[index].value == value) { return field.enumValues[index].name; }
  }
  return nullptr;
}

bool isChildField(NodeSchemaFieldStorage storage) {
  return storage == NodeSchemaFieldStorage::NodeId || storage == NodeSchemaFieldStorage::NodeList;
}

void writeSpan(zc::OutputStream& output, const source::SourceManager& sourceManager,
               source::SourceRange range) {
  if (range.isInvalid()) {
    output.write("@0:0..0:0"_zcb);
    return;
  }

  const source::LineAndColumn start = sourceManager.getLineAndColumn(range.getStart());
  const source::LineAndColumn end = sourceManager.getLineAndColumn(range.getEnd());
  output.write(
      zc::str("@", start.line, ":", start.column, "..", end.line, ":", end.column).asBytes());
}

void writeJsonSpan(zc::OutputStream& output, const source::SourceManager& sourceManager,
                   source::SourceRange range) {
  if (range.isInvalid()) {
    output.write("null"_zcb);
    return;
  }

  const source::LineAndColumn start = sourceManager.getLineAndColumn(range.getStart());
  const source::LineAndColumn end = sourceManager.getLineAndColumn(range.getEnd());
  output.write(zc::str("{\"start\":{\"line\":", start.line, ",\"column\":", start.column,
                       "},\"end\":{\"line\":", end.line, ",\"column\":", end.column, "}}")
                   .asBytes());
}

void writeDecodedText(zc::OutputStream& output, const Tree& tree, const NodeSchemaFieldEntry& field,
                      const Node& node, bool json) {
  const uint32_t raw = readWord(node, field);
  if (raw == 0) {
    if (field.optional) {
      output.write("null"_zcb);
      return;
    }
    writeJsonString(output, "");
    return;
  }

  zc::StringPtr value;
  switch (field.storage) {
    case NodeSchemaFieldStorage::StringId:
      value = normalizeStringField(field, tree.string(StringId(raw)));
      break;
    case NodeSchemaFieldStorage::IdentId:
      value = tree.ident(IdentId(raw));
      break;
    case NodeSchemaFieldStorage::BigIntId:
      value = tree.bigInt(BigIntId(raw));
      break;
    case NodeSchemaFieldStorage::FloatId:
      value = tree.floatLiteral(FloatId(raw));
      break;
    default:
      value = "";
      break;
  }

  if (json) {
    writeJsonString(output, value);
  } else {
    writeJsonString(output, value);
  }
}

void writeIdentListValue(zc::OutputStream& output, const Tree& tree,
                         const NodeSchemaFieldEntry& field, const Node& node) {
  output.write("["_zcb);
  bool first = true;
  for (IdentId id : tree.identList(readIdentList(node, field))) {
    if (!first) { output.write(", "_zcb); }
    first = false;
    writeJsonString(output, tree.ident(id));
  }
  output.write("]"_zcb);
}

void writeScalarValue(zc::OutputStream& output, const Tree& tree, const NodeSchemaFieldEntry& field,
                      const Node& node, bool json) {
  const uint32_t raw = readWord(node, field);
  if (field.optional && raw == 0 && field.storage != NodeSchemaFieldStorage::Bool) {
    output.write("null"_zcb);
    return;
  }

  switch (field.storage) {
    case NodeSchemaFieldStorage::IdentList:
      writeIdentListValue(output, tree, field, node);
      return;
    case NodeSchemaFieldStorage::StringId:
    case NodeSchemaFieldStorage::IdentId:
    case NodeSchemaFieldStorage::BigIntId:
    case NodeSchemaFieldStorage::FloatId:
      writeDecodedText(output, tree, field, node, json);
      return;
    case NodeSchemaFieldStorage::Bool:
      output.write(raw != 0 ? "true"_zcb : "false"_zcb);
      return;
    case NodeSchemaFieldStorage::UInt8:
    case NodeSchemaFieldStorage::UInt16:
    case NodeSchemaFieldStorage::UInt32:
      output.write(zc::str(static_cast<uint64_t>(raw)).asBytes());
      return;
    case NodeSchemaFieldStorage::UInt64:
      output.write(zc::str(readUInt64(node, field)).asBytes());
      return;
    case NodeSchemaFieldStorage::Enum: {
      zc::StringPtr name = enumValueName(field, raw);
      if (!(name == nullptr)) {
        if (json) {
          writeJsonString(output, name);
        } else {
          writeText(output, name);
        }
        return;
      }
      output.write(zc::str(static_cast<uint64_t>(raw)).asBytes());
      return;
    }
    case NodeSchemaFieldStorage::NodeId:
    case NodeSchemaFieldStorage::NodeList:
      ZC_UNREACHABLE;
  }
  ZC_UNREACHABLE;
}

zc::Maybe<zc::String> validateTree(const Tree& tree) {
  if (!tree.contains(tree.root())) {
    return zc::str("AST dump failed: root node id #", static_cast<uint64_t>(tree.root().value),
                   " is outside this tree");
  }

  uint32_t id = 1;
  for (const Node& node : tree.nodes()) {
    const NodeSchemaEntry* schema = lookupNodeSchema(node.kind);
    if (schema == nullptr) {
      return zc::str("AST dump failed: node #", static_cast<uint64_t>(id),
                     " has no schema metadata for kind ",
                     static_cast<uint64_t>(static_cast<uint32_t>(node.kind)));
    }

    for (uint32_t fieldIndex = 0; fieldIndex < schema->fieldCount; ++fieldIndex) {
      const NodeSchemaFieldEntry& field = schema->fields[fieldIndex];
      if (field.storage == NodeSchemaFieldStorage::NodeId) {
        const NodeId child = readNodeId(node, field);
        if (child && !tree.contains(child)) {
          return zc::str("AST dump failed: node #", static_cast<uint64_t>(id), " field ",
                         field.name, " references invalid node #",
                         static_cast<uint64_t>(child.value));
        }
      } else if (field.storage == NodeSchemaFieldStorage::NodeList) {
        for (NodeId child : tree.list(readNodeList(node, field))) {
          if (!tree.contains(child)) {
            return zc::str("AST dump failed: node #", static_cast<uint64_t>(id), " field ",
                           field.name, " contains invalid node #",
                           static_cast<uint64_t>(child.value));
          }
        }
      }
    }
    ++id;
  }

  return zc::none;
}

zc::Maybe<zc::String> dumpTreeNode(zc::OutputStream& output, const Tree& tree,
                                   const source::SourceManager& sourceManager, NodeId id,
                                   uint32_t indent) {
  const Node& node = tree.node(id);
  const NodeSchemaEntry* schema = lookupNodeSchema(node.kind);
  if (schema == nullptr) {
    return zc::str("AST dump failed: node #", static_cast<uint64_t>(id.value),
                   " has no schema metadata");
  }

  writeIndent(output, indent);
  writeText(output, schema->name);
  output.write(zc::str(" #", static_cast<uint64_t>(id.value), " ").asBytes());
  writeSpan(output, sourceManager, node.range);

  for (uint32_t fieldIndex = 0; fieldIndex < schema->fieldCount; ++fieldIndex) {
    const NodeSchemaFieldEntry& field = schema->fields[fieldIndex];
    if (isChildField(field.storage)) { continue; }
    output.write(zc::str(" ", field.name, "=").asBytes());
    writeScalarValue(output, tree, field, node, false);
  }
  output.write("\n"_zcb);

  for (uint32_t fieldIndex = 0; fieldIndex < schema->fieldCount; ++fieldIndex) {
    const NodeSchemaFieldEntry& field = schema->fields[fieldIndex];
    if (field.storage == NodeSchemaFieldStorage::NodeId) {
      writeIndent(output, indent + 2);
      output.write(zc::str(field.name, ":").asBytes());
      const NodeId child = readNodeId(node, field);
      if (!child) {
        output.write(" null\n"_zcb);
        continue;
      }
      output.write("\n"_zcb);
      ZC_IF_SOME(error, dumpTreeNode(output, tree, sourceManager, child, indent + 4)) {
        return zc::mv(error);
      }
    } else if (field.storage == NodeSchemaFieldStorage::NodeList) {
      writeIndent(output, indent + 2);
      output.write(zc::str(field.name, ":").asBytes());
      const zc::ArrayPtr<const NodeId> children = tree.list(readNodeList(node, field));
      if (children.size() == 0) {
        output.write(" []\n"_zcb);
        continue;
      }
      output.write("\n"_zcb);
      for (NodeId child : children) {
        ZC_IF_SOME(error, dumpTreeNode(output, tree, sourceManager, child, indent + 4)) {
          return zc::mv(error);
        }
      }
    }
  }

  return zc::none;
}

void dumpJsonFieldValue(zc::OutputStream& output, const Tree& tree,
                        const NodeSchemaFieldEntry& field, const Node& node) {
  if (field.storage == NodeSchemaFieldStorage::NodeId) {
    const NodeId child = readNodeId(node, field);
    if (child) {
      output.write(zc::str(static_cast<uint64_t>(child.value)).asBytes());
    } else {
      output.write("null"_zcb);
    }
    return;
  }

  if (field.storage == NodeSchemaFieldStorage::NodeList) {
    output.write("["_zcb);
    bool first = true;
    for (NodeId child : tree.list(readNodeList(node, field))) {
      if (!first) { output.write(", "_zcb); }
      first = false;
      output.write(zc::str(static_cast<uint64_t>(child.value)).asBytes());
    }
    output.write("]"_zcb);
    return;
  }

  writeScalarValue(output, tree, field, node, true);
}

zc::Maybe<zc::String> dumpJson(zc::OutputStream& output, const Tree& tree,
                               const source::SourceManager& sourceManager) {
  output.write("{\n"_zcb);
  output.write("  \"format\": \"zom.ast.json\",\n"_zcb);
  output.write(zc::str("  \"schema\": \"", kAstSchemaVersion, "\",\n").asBytes());
  output.write(zc::str("  \"fingerprint\": \"", kAstSchemaFingerprint, "\",\n").asBytes());
  output.write(zc::str("  \"root\": ", static_cast<uint64_t>(tree.root().value), ",\n").asBytes());
  output.write("  \"nodes\": [\n"_zcb);

  uint32_t id = 1;
  for (const Node& node : tree.nodes()) {
    const NodeSchemaEntry* schema = lookupNodeSchema(node.kind);
    if (schema == nullptr) {
      return zc::str("AST dump failed: node #", static_cast<uint64_t>(id),
                     " has no schema metadata");
    }

    if (id > 1) { output.write(",\n"_zcb); }
    output.write("    {\n"_zcb);
    output.write(zc::str("      \"id\": ", static_cast<uint64_t>(id), ",\n").asBytes());
    output.write("      \"kind\": "_zcb);
    writeJsonString(output, schema->name);
    output.write(",\n"_zcb);
    output.write("      \"span\": "_zcb);
    writeJsonSpan(output, sourceManager, node.range);
    output.write(",\n"_zcb);
    output.write("      \"fields\": {\n"_zcb);

    for (uint32_t fieldIndex = 0; fieldIndex < schema->fieldCount; ++fieldIndex) {
      const NodeSchemaFieldEntry& field = schema->fields[fieldIndex];
      output.write("        "_zcb);
      writeJsonString(output, field.name);
      output.write(": "_zcb);
      dumpJsonFieldValue(output, tree, field, node);
      output.write(fieldIndex + 1 < schema->fieldCount ? ",\n"_zcb : "\n"_zcb);
    }

    output.write("      }\n"_zcb);
    output.write("    }"_zcb);
    ++id;
  }

  output.write("\n  ]\n"_zcb);
  output.write("}\n"_zcb);
  return zc::none;
}

zc::Maybe<zc::String> dumpRaw(zc::OutputStream& output, const Tree& tree) {
  output.write(zc::str("root: ", static_cast<uint64_t>(tree.root().value), "\n").asBytes());
  uint32_t id = 1;
  for (const Node& node : tree.nodes()) {
    const NodeSchemaEntry* schema = lookupNodeSchema(node.kind);
    zc::StringPtr name = schema == nullptr ? "Unknown"_zc : zc::StringPtr(schema->name);
    output.write(zc::str("node ", static_cast<uint64_t>(id), ": kind=", name, " payload=[",
                         static_cast<uint64_t>(node.payload.words[0]), ",",
                         static_cast<uint64_t>(node.payload.words[1]), ",",
                         static_cast<uint64_t>(node.payload.words[2]), ",",
                         static_cast<uint64_t>(node.payload.words[3]), ",",
                         static_cast<uint64_t>(node.payload.words[4]), ",",
                         static_cast<uint64_t>(node.payload.words[5]), "]\n")
                     .asBytes());
    ++id;
  }
  return zc::none;
}

}  // namespace

zc::StringPtr astDumpFormatName(AstDumpFormat format) {
  switch (format) {
    case AstDumpFormat::Tree:
      return "tree"_zc;
    case AstDumpFormat::Json:
      return "json"_zc;
    case AstDumpFormat::Raw:
      return "raw"_zc;
  }
  ZC_UNREACHABLE;
}

zc::StringPtr astDumpFileExtension(AstDumpFormat format) {
  switch (format) {
    case AstDumpFormat::Tree:
      return ".ast"_zc;
    case AstDumpFormat::Json:
      return ".json"_zc;
    case AstDumpFormat::Raw:
      return ".raw"_zc;
  }
  ZC_UNREACHABLE;
}

zc::Maybe<zc::String> dumpTree(zc::OutputStream& output, const Tree& tree,
                               const source::SourceManager& sourceManager, AstDumpFormat format) {
  ZC_IF_SOME(error, validateTree(tree)) { return zc::mv(error); }

  switch (format) {
    case AstDumpFormat::Tree:
      return dumpTreeNode(output, tree, sourceManager, tree.root(), 0);
    case AstDumpFormat::Json:
      return dumpJson(output, tree, sourceManager);
    case AstDumpFormat::Raw:
      return dumpRaw(output, tree);
  }
  ZC_UNREACHABLE;
}

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
