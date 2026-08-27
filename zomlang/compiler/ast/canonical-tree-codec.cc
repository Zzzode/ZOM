// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/ast/canonical-tree-codec.h"

#include <climits>

#include "zc/core/debug.h"
#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/ast/generated/node-schema.h"
#include "zomlang/compiler/ast/schema-verifier.h"
#include "zomlang/compiler/identity/canonical/canonical-decoder.h"
#include "zomlang/compiler/identity/canonical/canonical-encoder.h"
#include "zomlang/compiler/source/manager.h"

namespace zomlang::compiler::ast {
namespace {

constexpr uint64_t kMaximumNodes = 1024 * 1024;
constexpr uint64_t kMaximumListItems = 16 * 1024 * 1024;
constexpr uint64_t kMaximumTextBytes = 64 * 1024 * 1024;

bool validEnumValue(const NodeSchemaFieldEntry& field, uint32_t value) {
  for (uint32_t index = 0; index < field.enumValueCount; ++index) {
    if (field.enumValues[index].value == value) { return true; }
  }
  return false;
}

zc::StringPtr textField(const Tree& tree, const NodeSchemaFieldEntry& field, uint32_t raw) {
  switch (field.storage) {
    case NodeSchemaFieldStorage::StringId:
      return tree.string(StringId(raw));
    case NodeSchemaFieldStorage::IdentId:
      return tree.ident(IdentId(raw));
    case NodeSchemaFieldStorage::BigIntId:
      return tree.bigInt(BigIntId(raw));
    case NodeSchemaFieldStorage::FloatId:
      return tree.floatLiteral(FloatId(raw));
    default:
      ZC_UNREACHABLE
  }
}

zc::Maybe<uint64_t> offsetFor(const source::SourceManager& sources,
                              const source::BufferId& buffer, source::SourceLoc location) {
  if (location.isInvalid()) { return zc::none; }
  const auto sourceRange = sources.getRangeForBuffer(buffer);
  if (location < sourceRange.getStart() || location > sourceRange.getEnd()) { return zc::none; }
  return static_cast<uint64_t>(sources.getLocOffsetInBuffer(location, buffer));
}

}  // namespace

zc::Maybe<zc::Array<uint8_t>> encodeCanonicalTree(const Tree& tree,
                                                   const source::SourceManager& sources,
                                                   const source::BufferId& buffer) {
  if (!tree.contains(tree.root()) || verifySchemaFailure(tree) != zc::none ||
      tree.nodeCount() > kMaximumNodes) {
    return zc::none;
  }
  identity::CanonicalEncoder encoder;
  encoder.encodeUint32(tree.root().value);
  encoder.encodeSequenceSize(tree.nodeCount());
  uint32_t nodeId = 0;
  for (const auto& node : tree.nodes()) {
    ++nodeId;
    const auto* schema = lookupNodeSchema(node.kind);
    auto start = offsetFor(sources, buffer, node.range.getStart());
    auto end = offsetFor(sources, buffer, node.range.getEnd());
    if (schema == nullptr || start == zc::none || end == zc::none ||
        ZC_ASSERT_NONNULL(start) > ZC_ASSERT_NONNULL(end)) {
      return zc::none;
    }
    encoder.encodeUint32(static_cast<uint32_t>(node.kind));
    encoder.encodeUint64(ZC_ASSERT_NONNULL(start));
    encoder.encodeUint64(ZC_ASSERT_NONNULL(end));
    encoder.encodeSequenceSize(schema->fieldCount);
    for (uint32_t fieldIndex = 0; fieldIndex < schema->fieldCount; ++fieldIndex) {
      const auto& field = schema->fields[fieldIndex];
      encoder.encodeUint8(static_cast<uint8_t>(field.storage) + 1);
      encoder.encodeBool(field.optional);
      const uint32_t raw = node.payload.words[field.firstWord];
      switch (field.storage) {
        case NodeSchemaFieldStorage::NodeId: {
          const bool present = raw != 0;
          if ((!field.optional && !present) || raw >= nodeId ||
              (present && !tree.contains(NodeId(raw)))) {
            return zc::none;
          }
          encoder.encodeBool(present);
          if (present) { encoder.encodeUint32(raw); }
          break;
        }
        case NodeSchemaFieldStorage::NodeList: {
          const NodeList list{raw, node.payload.words[field.secondWord]};
          if (!tree.contains(list)) { return zc::none; }
          const auto values = tree.list(list);
          encoder.encodeSequenceSize(values.size());
          for (const auto value : values) {
            if (!value || value.value >= nodeId || !tree.contains(value)) { return zc::none; }
            encoder.encodeUint32(value.value);
          }
          break;
        }
        case NodeSchemaFieldStorage::IdentList: {
          const IdentList list{raw, node.payload.words[field.secondWord]};
          if (!tree.contains(list)) { return zc::none; }
          const auto values = tree.identList(list);
          encoder.encodeSequenceSize(values.size());
          for (const auto value : values) {
            if (!value) { return zc::none; }
            encoder.encodeByteString(tree.ident(value).asBytes());
          }
          break;
        }
        case NodeSchemaFieldStorage::StringId:
        case NodeSchemaFieldStorage::IdentId:
        case NodeSchemaFieldStorage::BigIntId:
        case NodeSchemaFieldStorage::FloatId: {
          const bool present = raw != 0;
          if (!field.optional && !present) { return zc::none; }
          encoder.encodeBool(present);
          if (present) { encoder.encodeByteString(textField(tree, field, raw).asBytes()); }
          break;
        }
        case NodeSchemaFieldStorage::Bool:
          encoder.encodeBool(raw != 0);
          break;
        case NodeSchemaFieldStorage::UInt8:
          if (raw > UINT8_MAX) { return zc::none; }
          encoder.encodeUint8(static_cast<uint8_t>(raw));
          break;
        case NodeSchemaFieldStorage::UInt16:
          if (raw > UINT16_MAX) { return zc::none; }
          encoder.encodeUint32(raw);
          break;
        case NodeSchemaFieldStorage::UInt32:
          encoder.encodeUint32(raw);
          break;
        case NodeSchemaFieldStorage::UInt64:
          encoder.encodeUint64(static_cast<uint64_t>(raw) |
                               (static_cast<uint64_t>(node.payload.words[field.secondWord]) << 32));
          break;
        case NodeSchemaFieldStorage::Enum:
          if (!validEnumValue(field, raw)) { return zc::none; }
          encoder.encodeUint32(raw);
          break;
      }
    }
  }
  return encoder.finish();
}

zc::Maybe<Tree> decodeCanonicalTree(zc::ArrayPtr<const uint8_t> encoded,
                                    source::SourceManager& sources,
                                    const source::BufferId& buffer,
                                    uint64_t sourceByteLength) {
  identity::CanonicalDecoder decoder(encoded);
  auto root = decoder.decodeUint32();
  auto nodeCount = decoder.decodeSequenceSize(kMaximumNodes);
  if (root == zc::none || nodeCount == zc::none || ZC_ASSERT_NONNULL(nodeCount) == 0 ||
      ZC_ASSERT_NONNULL(root) == 0 || ZC_ASSERT_NONNULL(root) > ZC_ASSERT_NONNULL(nodeCount)) {
    return zc::none;
  }
  TreeBuilder builder;
  for (uint32_t nodeIndex = 0; nodeIndex < ZC_ASSERT_NONNULL(nodeCount); ++nodeIndex) {
    auto kindValue = decoder.decodeUint32();
    auto start = decoder.decodeUint64();
    auto end = decoder.decodeUint64();
    if (kindValue == zc::none || start == zc::none || end == zc::none ||
        ZC_ASSERT_NONNULL(start) > ZC_ASSERT_NONNULL(end) ||
        ZC_ASSERT_NONNULL(end) > sourceByteLength) {
      return zc::none;
    }
    const auto kind = static_cast<SyntaxKind>(ZC_ASSERT_NONNULL(kindValue));
    const auto* schema = lookupNodeSchema(kind);
    if (schema == nullptr) { return zc::none; }
    auto fieldCount = decoder.decodeSequenceSize(schema->fieldCount);
    if (fieldCount == zc::none || ZC_ASSERT_NONNULL(fieldCount) != schema->fieldCount) {
      return zc::none;
    }
    NodePayload payload;
    for (uint32_t fieldIndex = 0; fieldIndex < schema->fieldCount; ++fieldIndex) {
      const auto& field = schema->fields[fieldIndex];
      auto storage = decoder.decodeUint8();
      auto optional = decoder.decodeBool();
      if (storage == zc::none || optional == zc::none ||
          ZC_ASSERT_NONNULL(storage) != static_cast<uint8_t>(field.storage) + 1 ||
          ZC_ASSERT_NONNULL(optional) != field.optional) {
        return zc::none;
      }
      switch (field.storage) {
        case NodeSchemaFieldStorage::NodeId: {
          auto present = decoder.decodeBool();
          if (present == zc::none || (!field.optional && !ZC_ASSERT_NONNULL(present))) {
            return zc::none;
          }
          if (ZC_ASSERT_NONNULL(present)) {
            auto value = decoder.decodeUint32();
            if (value == zc::none || ZC_ASSERT_NONNULL(value) == 0 ||
                ZC_ASSERT_NONNULL(value) > nodeIndex) {
              return zc::none;
            }
            payload.words[field.firstWord] = ZC_ASSERT_NONNULL(value);
          }
          break;
        }
        case NodeSchemaFieldStorage::NodeList: {
          auto count = decoder.decodeSequenceSize(kMaximumListItems);
          if (count == zc::none) { return zc::none; }
          zc::Vector<NodeId> values(static_cast<size_t>(ZC_ASSERT_NONNULL(count)));
          for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(count); ++index) {
            auto value = decoder.decodeUint32();
            if (value == zc::none || ZC_ASSERT_NONNULL(value) == 0 ||
                ZC_ASSERT_NONNULL(value) > nodeIndex) {
              return zc::none;
            }
            values.add(NodeId(ZC_ASSERT_NONNULL(value)));
          }
          const auto list = builder.makeList(values.asPtr());
          payload.words[field.firstWord] = list.first;
          payload.words[field.secondWord] = list.size;
          break;
        }
        case NodeSchemaFieldStorage::IdentList: {
          auto count = decoder.decodeSequenceSize(kMaximumListItems);
          if (count == zc::none) { return zc::none; }
          zc::Vector<IdentId> values(static_cast<size_t>(ZC_ASSERT_NONNULL(count)));
          for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(count); ++index) {
            auto text = decoder.decodeByteString(kMaximumTextBytes);
            if (text == zc::none || ZC_ASSERT_NONNULL(text).size() == 0) { return zc::none; }
            values.add(builder.internIdent(zc::str(ZC_ASSERT_NONNULL(text).asChars())));
          }
          const auto list = builder.makeIdentList(values.asPtr());
          payload.words[field.firstWord] = list.first;
          payload.words[field.secondWord] = list.size;
          break;
        }
        case NodeSchemaFieldStorage::StringId:
        case NodeSchemaFieldStorage::IdentId:
        case NodeSchemaFieldStorage::BigIntId:
        case NodeSchemaFieldStorage::FloatId: {
          auto present = decoder.decodeBool();
          if (present == zc::none || (!field.optional && !ZC_ASSERT_NONNULL(present))) {
            return zc::none;
          }
          if (!ZC_ASSERT_NONNULL(present)) { break; }
          auto text = decoder.decodeByteString(kMaximumTextBytes);
          if (text == zc::none || ZC_ASSERT_NONNULL(text).size() == 0) { return zc::none; }
          const auto retained = zc::str(ZC_ASSERT_NONNULL(text).asChars());
          switch (field.storage) {
            case NodeSchemaFieldStorage::StringId:
              payload.words[field.firstWord] = builder.internString(retained).value;
              break;
            case NodeSchemaFieldStorage::IdentId:
              payload.words[field.firstWord] = builder.internIdent(retained).value;
              break;
            case NodeSchemaFieldStorage::BigIntId:
              payload.words[field.firstWord] = builder.internBigInt(retained).value;
              break;
            case NodeSchemaFieldStorage::FloatId:
              payload.words[field.firstWord] = builder.internFloat(retained).value;
              break;
            default:
              ZC_UNREACHABLE
          }
          break;
        }
        case NodeSchemaFieldStorage::Bool: {
          auto value = decoder.decodeBool();
          if (value == zc::none) { return zc::none; }
          payload.words[field.firstWord] = ZC_ASSERT_NONNULL(value) ? 1 : 0;
          break;
        }
        case NodeSchemaFieldStorage::UInt8: {
          auto value = decoder.decodeUint8();
          if (value == zc::none) { return zc::none; }
          payload.words[field.firstWord] = ZC_ASSERT_NONNULL(value);
          break;
        }
        case NodeSchemaFieldStorage::UInt16:
        case NodeSchemaFieldStorage::UInt32:
        case NodeSchemaFieldStorage::Enum: {
          auto value = decoder.decodeUint32();
          if (value == zc::none ||
              (field.storage == NodeSchemaFieldStorage::UInt16 &&
               ZC_ASSERT_NONNULL(value) > UINT16_MAX) ||
              (field.storage == NodeSchemaFieldStorage::Enum &&
               !validEnumValue(field, ZC_ASSERT_NONNULL(value)))) {
            return zc::none;
          }
          payload.words[field.firstWord] = ZC_ASSERT_NONNULL(value);
          break;
        }
        case NodeSchemaFieldStorage::UInt64: {
          auto value = decoder.decodeUint64();
          if (value == zc::none) { return zc::none; }
          payload.words[field.firstWord] = static_cast<uint32_t>(ZC_ASSERT_NONNULL(value));
          payload.words[field.secondWord] =
              static_cast<uint32_t>(ZC_ASSERT_NONNULL(value) >> 32);
          break;
        }
      }
    }
    const auto node = builder.makeNode(
        kind,
        source::SourceRange(
            sources.getLocForOffset(buffer, static_cast<unsigned>(ZC_ASSERT_NONNULL(start))),
            sources.getLocForOffset(buffer, static_cast<unsigned>(ZC_ASSERT_NONNULL(end)))),
        payload);
    if (node.value != nodeIndex + 1) { return zc::none; }
  }
  if (!decoder.finished()) { return zc::none; }
  builder.setRoot(NodeId(ZC_ASSERT_NONNULL(root)));
  auto tree = builder.finish();
  if (verifySchemaFailure(tree) != zc::none ||
      tree.node(tree.root()).kind != SyntaxKind::SourceFile) {
    return zc::none;
  }
  return zc::mv(tree);
}

zc::Maybe<zc::StringPtr> canonicalSourceFileName(const Tree& tree) {
  if (!tree.contains(tree.root())) { return zc::none; }
  const auto& root = tree.node(tree.root());
  if (root.kind != SyntaxKind::SourceFile) { return zc::none; }
  const StringId name(root.payload.words[kSourceFileFileNameWord]);
  if (!name) { return zc::none; }
  return tree.string(name);
}

}  // namespace zomlang::compiler::ast
