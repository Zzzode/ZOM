// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/binder/surface/module-body-syntax.h"

#include "zc/core/debug.h"
#include "compiler/ast/generated/node-schema.h"
#include "compiler/identity/canonical/canonical-decoder.h"
#include "compiler/identity/canonical/canonical-encoder.h"
#include "compiler/identity/canonical/canonical-scalar.h"

namespace zomlang::compiler::binder {

namespace module_body_syntax_detail {

struct DetachedModuleBodyNodeData final {
  DetachedModuleBodyNodeKind kind;
  ast::SyntaxKind syntaxKind;
  zc::Array<uint8_t> canonicalPayload;
  uint32_t childCount;
};

struct ModuleBodySyntaxData final {
  uint32_t rootCount;
  zc::Vector<DetachedModuleBodyNode> nodes;
};

struct ModuleBodyProvenanceData final {
  identity::SourceFileKey source;
  zc::Vector<ModuleBodyProvenanceEntry> entries;
};

}  // namespace module_body_syntax_detail

namespace detail = module_body_syntax_detail;
namespace {

constexpr zc::StringPtr kModuleBodySyntaxDomain = "zom.module-body-syntax"_zc;
constexpr zc::StringPtr kModuleBodyProvenanceDomain = "zom.module-body-provenance"_zc;
constexpr zc::StringPtr kNamedItemSyntaxDomain = "zom.named-item-syntax"_zc;
constexpr zc::StringPtr kNamedItemProvenanceDomain = "zom.named-item-provenance"_zc;
constexpr uint64_t kMaximumDetachedNodes = 1024 * 1024;
constexpr uint64_t kMaximumDetachedPayloadBytes = 64 * 1024 * 1024;
constexpr uint64_t kMaximumTextBytes = 64 * 1024 * 1024;
constexpr uint64_t kMaximumIdentifierBytes = 4096;
constexpr uint64_t kMaximumIdentifierList = 65536;
constexpr uint64_t kMaximumSourceKeyBytes = 64 * 1024;
constexpr uint64_t kMaximumPathBytes = 16 * 1024;
constexpr uint64_t kMaximumNamedItemValueBytes = 128 * 1024 * 1024;

bool sameDomain(zc::ArrayPtr<const uint8_t> actual, zc::StringPtr expected) {
  return actual == expected.asBytes();
}

bool sameModule(const identity::ModuleKey& left, const identity::ModuleKey& right) {
  return left.encode().asPtr() == right.encode().asPtr();
}

int comparePaths(const LocalSyntaxPath& left, const LocalSyntaxPath& right) noexcept {
  const auto leftComponents = left.components();
  const auto rightComponents = right.components();
  const size_t common = leftComponents.size() < rightComponents.size() ? leftComponents.size()
                                                                       : rightComponents.size();
  for (size_t index = 0; index < common; ++index) {
    if (leftComponents[index] < rightComponents[index]) return -1;
    if (leftComponents[index] > rightComponents[index]) return 1;
  }
  if (leftComponents.size() < rightComponents.size()) return -1;
  if (leftComponents.size() > rightComponents.size()) return 1;
  return 0;
}

bool validEnumValue(const ast::NodeSchemaFieldEntry& field, uint32_t value) {
  for (uint32_t index = 0; index < field.enumValueCount; ++index) {
    if (field.enumValues[index].value == value) { return true; }
  }
  return false;
}

bool validCanonicalIdentifier(zc::StringPtr text) {
  return identity::SemanticIdentifier::fromCanonical(text) != zc::none ||
         identity::DeclaredDefinitionName::fromCanonical(text) != zc::none;
}

bool validateCanonicalFields(ast::SyntaxKind kind, zc::ArrayPtr<const uint8_t> encoded,
                             uint32_t expectedChildren) {
  zc::Maybe<const ast::NodeSchemaEntry&> schema = ast::lookupNodeSchema(kind);
  if (schema == zc::none) { return false; }
  const auto& schemaValue = ZC_ASSERT_NONNULL(schema);
  identity::CanonicalDecoder decoder(encoded);
  auto fieldCount = decoder.decodeSequenceSize(schemaValue.fieldCount);
  if (fieldCount == zc::none || ZC_ASSERT_NONNULL(fieldCount) != schemaValue.fieldCount) {
    return false;
  }
  uint64_t childCount = 0;
  for (uint32_t index = 0; index < schemaValue.fieldCount; ++index) {
    const auto& field = schemaValue.fields[index];
    auto storage = decoder.decodeUint8();
    auto optional = decoder.decodeBool();
    if (storage == zc::none || optional == zc::none ||
        ZC_ASSERT_NONNULL(storage) != static_cast<uint8_t>(field.storage) + 1 ||
        ZC_ASSERT_NONNULL(optional) != field.optional) {
      return false;
    }
    switch (field.storage) {
      case ast::NodeSchemaFieldStorage::NodeId: {
        auto present = decoder.decodeBool();
        if (present == zc::none || (!field.optional && !ZC_ASSERT_NONNULL(present))) {
          return false;
        }
        if (ZC_ASSERT_NONNULL(present)) { ++childCount; }
        break;
      }
      case ast::NodeSchemaFieldStorage::NodeList: {
        auto count = decoder.decodeSequenceSize(kMaximumDetachedNodes);
        if (count == zc::none) { return false; }
        childCount += ZC_ASSERT_NONNULL(count);
        break;
      }
      case ast::NodeSchemaFieldStorage::IdentList: {
        auto count = decoder.decodeSequenceSize(kMaximumIdentifierList);
        if (count == zc::none) { return false; }
        ZC_IF_SOME(value, count) {
          for (uint64_t item = 0; item < value; ++item) {
            auto text = decoder.decodeByteString(kMaximumIdentifierBytes);
            if (text == zc::none) { return false; }
            ZC_IF_SOME(bytes, text) {
              if (!validCanonicalIdentifier(zc::str(bytes.asChars()))) { return false; }
            }
          }
        }
        break;
      }
      case ast::NodeSchemaFieldStorage::StringId:
      case ast::NodeSchemaFieldStorage::IdentId:
      case ast::NodeSchemaFieldStorage::BigIntId:
      case ast::NodeSchemaFieldStorage::FloatId: {
        auto present = decoder.decodeBool();
        if (present == zc::none || (!field.optional && !ZC_ASSERT_NONNULL(present))) {
          return false;
        }
        if (ZC_ASSERT_NONNULL(present)) {
          const uint64_t maximum = field.storage == ast::NodeSchemaFieldStorage::IdentId
                                       ? kMaximumIdentifierBytes
                                       : kMaximumTextBytes;
          auto text = decoder.decodeByteString(maximum);
          if (text == zc::none) { return false; }
          if (field.storage == ast::NodeSchemaFieldStorage::IdentId) {
            ZC_IF_SOME(bytes, text) {
              if (!validCanonicalIdentifier(zc::str(bytes.asChars()))) { return false; }
            }
          }
        }
        break;
      }
      case ast::NodeSchemaFieldStorage::Bool:
        if (decoder.decodeBool() == zc::none) { return false; }
        break;
      case ast::NodeSchemaFieldStorage::UInt8: {
        if (decoder.decodeUint8() == zc::none) { return false; }
        break;
      }
      case ast::NodeSchemaFieldStorage::UInt16:
      case ast::NodeSchemaFieldStorage::UInt32: {
        auto value = decoder.decodeUint32();
        if (value == zc::none || (field.storage == ast::NodeSchemaFieldStorage::UInt16 &&
                                  ZC_ASSERT_NONNULL(value) > UINT16_MAX)) {
          return false;
        }
        break;
      }
      case ast::NodeSchemaFieldStorage::UInt64:
        if (decoder.decodeUint64() == zc::none) { return false; }
        break;
      case ast::NodeSchemaFieldStorage::Enum: {
        auto value = decoder.decodeUint32();
        if (value == zc::none || !validEnumValue(field, ZC_ASSERT_NONNULL(value))) { return false; }
        break;
      }
    }
    if (childCount > UINT32_MAX) { return false; }
  }
  return decoder.finished() && childCount == expectedChildren;
}

bool validPreorder(uint32_t rootCount, zc::ArrayPtr<const DetachedModuleBodyNode> nodes) {
  uint64_t pending = rootCount;
  for (const auto& node : nodes) {
    if (pending == 0) { return false; }
    --pending;
    pending += node.childCount();
    if (pending > kMaximumDetachedNodes) { return false; }
  }
  return pending == 0 && (rootCount != 0 || nodes.size() == 0);
}

zc::Maybe<identity::SourceFileKey> decodeSourceKey(zc::ArrayPtr<const uint8_t> encoded) {
  identity::CanonicalDecoder decoder(encoded);
  auto source = identity::SourceFileKey::decodeCanonical(decoder);
  if (source == zc::none || !decoder.finished()) { return zc::none; }
  return zc::mv(source);
}

}  // namespace

DetachedModuleBodyNode::DetachedModuleBodyNode(
    zc::Own<detail::DetachedModuleBodyNodeData>&& value) noexcept
    : impl(zc::mv(value)) {}
DetachedModuleBodyNode::~DetachedModuleBodyNode() noexcept(false) = default;
DetachedModuleBodyNode::DetachedModuleBodyNode(DetachedModuleBodyNode&&) noexcept = default;
DetachedModuleBodyNode& DetachedModuleBodyNode::operator=(DetachedModuleBodyNode&&) noexcept =
    default;

zc::Maybe<DetachedModuleBodyNode> DetachedModuleBodyNode::syntax(
    ast::SyntaxKind kind, zc::Array<uint8_t>&& canonicalFields, uint32_t childCount) {
  if (!validateCanonicalFields(kind, canonicalFields.asPtr(), childCount)) { return zc::none; }
  return DetachedModuleBodyNode(
      zc::heap<detail::DetachedModuleBodyNodeData>(detail::DetachedModuleBodyNodeData{
          DetachedModuleBodyNodeKind::Syntax, kind, zc::mv(canonicalFields), childCount}));
}

DetachedModuleBodyNode DetachedModuleBodyNode::definitionBoundary(
    const identity::DefinitionKey& key) {
  return DetachedModuleBodyNode(zc::heap<detail::DetachedModuleBodyNodeData>(
      detail::DetachedModuleBodyNodeData{DetachedModuleBodyNodeKind::DefinitionBoundary,
                                         ast::SyntaxKind::Unknown, key.encode(), 0}));
}

DetachedModuleBodyNode DetachedModuleBodyNode::clone() const {
  return DetachedModuleBodyNode(
      zc::heap<detail::DetachedModuleBodyNodeData>(detail::DetachedModuleBodyNodeData{
          impl->kind, impl->syntaxKind, zc::heapArray<uint8_t>(impl->canonicalPayload.asPtr()),
          impl->childCount}));
}

DetachedModuleBodyNodeKind DetachedModuleBodyNode::kind() const noexcept { return impl->kind; }

zc::Maybe<ast::SyntaxKind> DetachedModuleBodyNode::syntaxKind() const noexcept {
  if (impl->kind != DetachedModuleBodyNodeKind::Syntax) { return zc::none; }
  return impl->syntaxKind;
}

zc::ArrayPtr<const uint8_t> DetachedModuleBodyNode::canonicalPayload() const {
  return impl->canonicalPayload.asPtr();
}

uint32_t DetachedModuleBodyNode::childCount() const noexcept { return impl->childCount; }

zc::Maybe<DetachedModuleBodyChildField> DetachedModuleBodyNode::childField(
    uint32_t fieldIndex) const {
  if (impl->kind != DetachedModuleBodyNodeKind::Syntax) { return zc::none; }
  auto schema = ast::lookupNodeSchema(impl->syntaxKind);
  if (schema == nullptr || fieldIndex >= schema->fieldCount) { return zc::none; }
  const auto& schemaValue = *schema;
  identity::CanonicalDecoder decoder(impl->canonicalPayload.asPtr());
  auto fieldCount = decoder.decodeSequenceSize(schemaValue.fieldCount);
  if (fieldCount == zc::none || ZC_ASSERT_NONNULL(fieldCount) != schemaValue.fieldCount) {
    return zc::none;
  }

  uint32_t childOrdinal = 0;
  zc::Maybe<DetachedModuleBodyChildField> selected;
  for (uint32_t index = 0; index < schemaValue.fieldCount; ++index) {
    const auto& field = schemaValue.fields[index];
    auto storage = decoder.decodeUint8();
    auto optional = decoder.decodeBool();
    if (storage == zc::none || optional == zc::none ||
        ZC_ASSERT_NONNULL(storage) != static_cast<uint8_t>(field.storage) + 1 ||
        ZC_ASSERT_NONNULL(optional) != field.optional) {
      return zc::none;
    }
    switch (field.storage) {
      case ast::NodeSchemaFieldStorage::NodeId: {
        auto present = decoder.decodeBool();
        if (present == zc::none || (!field.optional && !ZC_ASSERT_NONNULL(present))) {
          return zc::none;
        }
        if (index == fieldIndex) {
          selected = DetachedModuleBodyChildField{ZC_ASSERT_NONNULL(present), childOrdinal,
                                                  ZC_ASSERT_NONNULL(present) ? 1U : 0U};
        }
        if (ZC_ASSERT_NONNULL(present)) { ++childOrdinal; }
        break;
      }
      case ast::NodeSchemaFieldStorage::NodeList: {
        auto count = decoder.decodeSequenceSize(kMaximumDetachedNodes);
        if (count == zc::none || ZC_ASSERT_NONNULL(count) > UINT32_MAX - childOrdinal) {
          return zc::none;
        }
        if (index == fieldIndex) {
          selected = DetachedModuleBodyChildField{true, childOrdinal,
                                                  static_cast<uint32_t>(ZC_ASSERT_NONNULL(count))};
        }
        childOrdinal += static_cast<uint32_t>(ZC_ASSERT_NONNULL(count));
        break;
      }
      case ast::NodeSchemaFieldStorage::IdentList: {
        auto count = decoder.decodeSequenceSize(kMaximumIdentifierList);
        if (count == zc::none) { return zc::none; }
        ZC_IF_SOME(value, count) {
          for (uint64_t item = 0; item < value; ++item) {
            if (decoder.decodeByteString(kMaximumIdentifierBytes) == zc::none) { return zc::none; }
          }
        }
        break;
      }
      case ast::NodeSchemaFieldStorage::StringId:
      case ast::NodeSchemaFieldStorage::IdentId:
      case ast::NodeSchemaFieldStorage::BigIntId:
      case ast::NodeSchemaFieldStorage::FloatId: {
        auto present = decoder.decodeBool();
        if (present == zc::none || (!field.optional && !ZC_ASSERT_NONNULL(present)) ||
            (ZC_ASSERT_NONNULL(present) &&
             decoder.decodeByteString(field.storage == ast::NodeSchemaFieldStorage::IdentId
                                          ? kMaximumIdentifierBytes
                                          : kMaximumTextBytes) == zc::none)) {
          return zc::none;
        }
        break;
      }
      case ast::NodeSchemaFieldStorage::Bool:
        if (decoder.decodeBool() == zc::none) { return zc::none; }
        break;
      case ast::NodeSchemaFieldStorage::UInt8:
        if (decoder.decodeUint8() == zc::none) { return zc::none; }
        break;
      case ast::NodeSchemaFieldStorage::UInt16:
      case ast::NodeSchemaFieldStorage::UInt32:
      case ast::NodeSchemaFieldStorage::Enum:
        if (decoder.decodeUint32() == zc::none) { return zc::none; }
        break;
      case ast::NodeSchemaFieldStorage::UInt64:
        if (decoder.decodeUint64() == zc::none) { return zc::none; }
        break;
    }
  }
  if (!decoder.finished() || childOrdinal != impl->childCount) { return zc::none; }
  return selected;
}

zc::Maybe<identity::DeclaredDefinitionName> DetachedModuleBodyNode::identifierField(
    uint32_t fieldIndex) const {
  if (impl->kind != DetachedModuleBodyNodeKind::Syntax) { return zc::none; }
  auto schema = ast::lookupNodeSchema(impl->syntaxKind);
  if (schema == nullptr || fieldIndex >= schema->fieldCount ||
      schema->fields[fieldIndex].storage != ast::NodeSchemaFieldStorage::IdentId) {
    return zc::none;
  }
  identity::CanonicalDecoder decoder(impl->canonicalPayload.asPtr());
  auto fieldCount = decoder.decodeSequenceSize(schema->fieldCount);
  if (fieldCount == zc::none || ZC_ASSERT_NONNULL(fieldCount) != schema->fieldCount) {
    return zc::none;
  }

  zc::Maybe<identity::DeclaredDefinitionName> selected;
  for (uint32_t index = 0; index < schema->fieldCount; ++index) {
    const auto& field = schema->fields[index];
    auto storage = decoder.decodeUint8();
    auto optional = decoder.decodeBool();
    if (storage == zc::none || optional == zc::none ||
        ZC_ASSERT_NONNULL(storage) != static_cast<uint8_t>(field.storage) + 1 ||
        ZC_ASSERT_NONNULL(optional) != field.optional) {
      return zc::none;
    }
    switch (field.storage) {
      case ast::NodeSchemaFieldStorage::NodeId: {
        auto present = decoder.decodeBool();
        if (present == zc::none || (!field.optional && !ZC_ASSERT_NONNULL(present))) {
          return zc::none;
        }
        break;
      }
      case ast::NodeSchemaFieldStorage::NodeList: {
        auto count = decoder.decodeSequenceSize(kMaximumDetachedNodes);
        if (count == zc::none) { return zc::none; }
        break;
      }
      case ast::NodeSchemaFieldStorage::IdentList: {
        auto count = decoder.decodeSequenceSize(kMaximumIdentifierList);
        if (count == zc::none) { return zc::none; }
        ZC_IF_SOME(value, count) {
          for (uint64_t item = 0; item < value; ++item) {
            if (decoder.decodeByteString(kMaximumIdentifierBytes) == zc::none) { return zc::none; }
          }
        }
        break;
      }
      case ast::NodeSchemaFieldStorage::StringId:
      case ast::NodeSchemaFieldStorage::IdentId:
      case ast::NodeSchemaFieldStorage::BigIntId:
      case ast::NodeSchemaFieldStorage::FloatId: {
        auto present = decoder.decodeBool();
        if (present == zc::none || (!field.optional && !ZC_ASSERT_NONNULL(present))) {
          return zc::none;
        }
        if (!ZC_ASSERT_NONNULL(present)) { break; }
        auto text = decoder.decodeByteString(field.storage == ast::NodeSchemaFieldStorage::IdentId
                                                 ? kMaximumIdentifierBytes
                                                 : kMaximumTextBytes);
        if (text == zc::none) { return zc::none; }
        if (index == fieldIndex) {
          auto name = identity::DeclaredDefinitionName::fromCanonical(
              zc::str(ZC_ASSERT_NONNULL(text).asChars()));
          if (name == zc::none) { return zc::none; }
          selected = zc::mv(ZC_ASSERT_NONNULL(name));
        }
        break;
      }
      case ast::NodeSchemaFieldStorage::Bool:
        if (decoder.decodeBool() == zc::none) { return zc::none; }
        break;
      case ast::NodeSchemaFieldStorage::UInt8:
        if (decoder.decodeUint8() == zc::none) { return zc::none; }
        break;
      case ast::NodeSchemaFieldStorage::UInt16:
      case ast::NodeSchemaFieldStorage::UInt32:
      case ast::NodeSchemaFieldStorage::Enum:
        if (decoder.decodeUint32() == zc::none) { return zc::none; }
        break;
      case ast::NodeSchemaFieldStorage::UInt64:
        if (decoder.decodeUint64() == zc::none) { return zc::none; }
        break;
    }
  }
  return decoder.finished() ? zc::mv(selected) : zc::Maybe<identity::DeclaredDefinitionName>();
}

zc::Maybe<zc::Vector<identity::DeclaredDefinitionName>> DetachedModuleBodyNode::identifierListField(
    uint32_t fieldIndex) const {
  if (impl->kind != DetachedModuleBodyNodeKind::Syntax) { return zc::none; }
  auto schema = ast::lookupNodeSchema(impl->syntaxKind);
  if (schema == nullptr || fieldIndex >= schema->fieldCount ||
      schema->fields[fieldIndex].storage != ast::NodeSchemaFieldStorage::IdentList) {
    return zc::none;
  }
  identity::CanonicalDecoder decoder(impl->canonicalPayload.asPtr());
  auto fieldCount = decoder.decodeSequenceSize(schema->fieldCount);
  if (fieldCount == zc::none || ZC_ASSERT_NONNULL(fieldCount) != schema->fieldCount) {
    return zc::none;
  }

  zc::Vector<identity::DeclaredDefinitionName> selected;
  for (uint32_t index = 0; index < schema->fieldCount; ++index) {
    const auto& field = schema->fields[index];
    auto storage = decoder.decodeUint8();
    auto optional = decoder.decodeBool();
    if (storage == zc::none || optional == zc::none ||
        ZC_ASSERT_NONNULL(storage) != static_cast<uint8_t>(field.storage) + 1 ||
        ZC_ASSERT_NONNULL(optional) != field.optional) {
      return zc::none;
    }
    switch (field.storage) {
      case ast::NodeSchemaFieldStorage::NodeId: {
        auto present = decoder.decodeBool();
        if (present == zc::none || (!field.optional && !ZC_ASSERT_NONNULL(present))) {
          return zc::none;
        }
        break;
      }
      case ast::NodeSchemaFieldStorage::NodeList: {
        if (decoder.decodeSequenceSize(kMaximumDetachedNodes) == zc::none) { return zc::none; }
        break;
      }
      case ast::NodeSchemaFieldStorage::IdentList: {
        auto count = decoder.decodeSequenceSize(kMaximumIdentifierList);
        if (count == zc::none) { return zc::none; }
        ZC_IF_SOME(value, count) {
          for (uint64_t item = 0; item < value; ++item) {
            auto text = decoder.decodeByteString(kMaximumIdentifierBytes);
            if (text == zc::none) { return zc::none; }
            if (index != fieldIndex) { continue; }
            auto name = identity::DeclaredDefinitionName::fromCanonical(
                zc::str(ZC_ASSERT_NONNULL(text).asChars()));
            if (name == zc::none) { return zc::none; }
            selected.add(zc::mv(ZC_ASSERT_NONNULL(name)));
          }
        }
        break;
      }
      case ast::NodeSchemaFieldStorage::StringId:
      case ast::NodeSchemaFieldStorage::IdentId:
      case ast::NodeSchemaFieldStorage::BigIntId:
      case ast::NodeSchemaFieldStorage::FloatId: {
        auto present = decoder.decodeBool();
        if (present == zc::none || (!field.optional && !ZC_ASSERT_NONNULL(present)) ||
            (ZC_ASSERT_NONNULL(present) &&
             decoder.decodeByteString(field.storage == ast::NodeSchemaFieldStorage::IdentId
                                          ? kMaximumIdentifierBytes
                                          : kMaximumTextBytes) == zc::none)) {
          return zc::none;
        }
        break;
      }
      case ast::NodeSchemaFieldStorage::Bool:
        if (decoder.decodeBool() == zc::none) { return zc::none; }
        break;
      case ast::NodeSchemaFieldStorage::UInt8:
        if (decoder.decodeUint8() == zc::none) { return zc::none; }
        break;
      case ast::NodeSchemaFieldStorage::UInt16:
      case ast::NodeSchemaFieldStorage::UInt32:
      case ast::NodeSchemaFieldStorage::Enum:
        if (decoder.decodeUint32() == zc::none) { return zc::none; }
        break;
      case ast::NodeSchemaFieldStorage::UInt64:
        if (decoder.decodeUint64() == zc::none) { return zc::none; }
        break;
    }
  }
  return decoder.finished() ? zc::mv(selected)
                            : zc::Maybe<zc::Vector<identity::DeclaredDefinitionName>>();
}

bool DetachedModuleBodyNode::operator==(const DetachedModuleBodyNode& other) const noexcept {
  return impl->kind == other.impl->kind && impl->syntaxKind == other.impl->syntaxKind &&
         impl->canonicalPayload.asPtr() == other.impl->canonicalPayload.asPtr() &&
         impl->childCount == other.impl->childCount;
}

ModuleBodySyntax::ModuleBodySyntax(zc::Own<detail::ModuleBodySyntaxData>&& value) noexcept
    : impl(zc::mv(value)) {}
ModuleBodySyntax::~ModuleBodySyntax() noexcept(false) = default;
ModuleBodySyntax::ModuleBodySyntax(ModuleBodySyntax&&) noexcept = default;
ModuleBodySyntax& ModuleBodySyntax::operator=(ModuleBodySyntax&&) noexcept = default;

zc::Maybe<ModuleBodySyntax> ModuleBodySyntax::from(uint32_t rootCount,
                                                   zc::Vector<DetachedModuleBodyNode>&& nodes) {
  if (nodes.size() > kMaximumDetachedNodes || !validPreorder(rootCount, nodes.asPtr())) {
    return zc::none;
  }
  return ModuleBodySyntax(zc::heap<detail::ModuleBodySyntaxData>(
      detail::ModuleBodySyntaxData{rootCount, zc::mv(nodes)}));
}

zc::Maybe<ModuleBodySyntax> ModuleBodySyntax::decodeCanonical(zc::ArrayPtr<const uint8_t> encoded) {
  identity::CanonicalDecoder decoder(encoded);
  auto domain = decoder.decodeByteString(kModuleBodySyntaxDomain.size());
  auto schema = decoder.decodeByteString(zc::StringPtr(ast::kAstSchemaFingerprint).size());
  auto rootCount = decoder.decodeUint32();
  auto nodeCount = decoder.decodeSequenceSize(kMaximumDetachedNodes);
  if (domain == zc::none || schema == zc::none || rootCount == zc::none || nodeCount == zc::none) {
    return zc::none;
  }
  ZC_IF_SOME(value, domain) {
    if (!sameDomain(value.asPtr(), kModuleBodySyntaxDomain)) { return zc::none; }
  }
  ZC_IF_SOME(value, schema) {
    if (!sameDomain(value.asPtr(), ast::kAstSchemaFingerprint)) { return zc::none; }
  }
  zc::Vector<DetachedModuleBodyNode> nodes(static_cast<size_t>(ZC_ASSERT_NONNULL(nodeCount)));
  ZC_IF_SOME(count, nodeCount) {
    for (uint64_t index = 0; index < count; ++index) {
      auto kind = decoder.decodeUint8();
      if (kind == zc::none) { return zc::none; }
      switch (static_cast<DetachedModuleBodyNodeKind>(ZC_ASSERT_NONNULL(kind))) {
        case DetachedModuleBodyNodeKind::Syntax: {
          auto syntaxKind = decoder.decodeUint32();
          auto childCount = decoder.decodeUint32();
          auto payload = decoder.decodeByteString(kMaximumDetachedPayloadBytes);
          if (syntaxKind == zc::none || childCount == zc::none || payload == zc::none) {
            return zc::none;
          }
          const auto decodedKind = static_cast<ast::SyntaxKind>(ZC_ASSERT_NONNULL(syntaxKind));
          ZC_IF_SOME(payloadValue, payload) {
            auto node = DetachedModuleBodyNode::syntax(decodedKind, zc::mv(payloadValue),
                                                       ZC_ASSERT_NONNULL(childCount));
            if (node == zc::none) { return zc::none; }
            ZC_IF_SOME(nodeValue, node) { nodes.add(zc::mv(nodeValue)); }
          }
          break;
        }
        case DetachedModuleBodyNodeKind::DefinitionBoundary: {
          auto payload = decoder.decodeByteString(32);
          if (payload == zc::none) { return zc::none; }
          ZC_IF_SOME(payloadValue, payload) {
            auto key = identity::DefinitionKey::fromBytes(payloadValue.asPtr());
            if (key == zc::none) { return zc::none; }
            ZC_IF_SOME(keyValue, key) {
              nodes.add(DetachedModuleBodyNode::definitionBoundary(keyValue));
            }
          }
          break;
        }
        default:
          return zc::none;
      }
    }
  }
  if (!decoder.finished()) { return zc::none; }
  return from(ZC_ASSERT_NONNULL(rootCount), zc::mv(nodes));
}

ModuleBodySyntax ModuleBodySyntax::clone() const {
  zc::Vector<DetachedModuleBodyNode> nodes(impl->nodes.size());
  for (const auto& node : impl->nodes) { nodes.add(node.clone()); }
  auto result = from(impl->rootCount, zc::mv(nodes));
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_UNREACHABLE
}

uint32_t ModuleBodySyntax::rootCount() const noexcept { return impl->rootCount; }

zc::ArrayPtr<const DetachedModuleBodyNode> ModuleBodySyntax::nodes() const {
  return impl->nodes.asPtr();
}

zc::Array<uint8_t> ModuleBodySyntax::encodeCanonical() const {
  identity::CanonicalEncoder encoder;
  encoder.encodeByteString(kModuleBodySyntaxDomain.asBytes());
  encoder.encodeByteString(zc::StringPtr(ast::kAstSchemaFingerprint).asBytes());
  encoder.encodeUint32(impl->rootCount);
  encoder.encodeSequenceSize(impl->nodes.size());
  for (const auto& node : impl->nodes) {
    encoder.encodeUint8(static_cast<uint8_t>(node.kind()));
    if (node.kind() == DetachedModuleBodyNodeKind::Syntax) {
      encoder.encodeUint32(static_cast<uint32_t>(ZC_ASSERT_NONNULL(node.syntaxKind())));
      encoder.encodeUint32(node.childCount());
    }
    encoder.encodeByteString(node.canonicalPayload());
  }
  return encoder.finish();
}

bool ModuleBodySyntax::operator==(const ModuleBodySyntax& other) const noexcept {
  if (impl->rootCount != other.impl->rootCount || impl->nodes.size() != other.impl->nodes.size()) {
    return false;
  }
  for (size_t index = 0; index < impl->nodes.size(); ++index) {
    if (impl->nodes[index] != other.impl->nodes[index]) { return false; }
  }
  return true;
}

ModuleBodyProvenanceEntry ModuleBodyProvenanceEntry::clone() const {
  return ModuleBodyProvenanceEntry{path.clone(), node, byteStart, byteEnd};
}

ModuleBodyProvenance::ModuleBodyProvenance(
    zc::Own<detail::ModuleBodyProvenanceData>&& value) noexcept
    : impl(zc::mv(value)) {}
ModuleBodyProvenance::~ModuleBodyProvenance() noexcept(false) = default;
ModuleBodyProvenance::ModuleBodyProvenance(ModuleBodyProvenance&&) noexcept = default;
ModuleBodyProvenance& ModuleBodyProvenance::operator=(ModuleBodyProvenance&&) noexcept = default;

zc::Maybe<ModuleBodyProvenance> ModuleBodyProvenance::from(
    identity::SourceFileKey&& source, zc::Vector<ModuleBodyProvenanceEntry>&& entries) {
  if (entries.size() > kMaximumDetachedNodes) { return zc::none; }
  for (size_t index = 0; index < entries.size(); ++index) {
    const auto& entry = entries[index];
    if (!entry.node || entry.byteStart > entry.byteEnd ||
        (index != 0 && comparePaths(entries[index - 1].path, entry.path) >= 0)) {
      return zc::none;
    }
  }
  return ModuleBodyProvenance(zc::heap<detail::ModuleBodyProvenanceData>(
      detail::ModuleBodyProvenanceData{zc::mv(source), zc::mv(entries)}));
}

zc::Maybe<ModuleBodyProvenance> ModuleBodyProvenance::decodeCanonical(
    zc::ArrayPtr<const uint8_t> encoded) {
  identity::CanonicalDecoder decoder(encoded);
  auto domain = decoder.decodeByteString(kModuleBodyProvenanceDomain.size());
  auto sourceBytes = decoder.decodeByteString(kMaximumSourceKeyBytes);
  auto count = decoder.decodeSequenceSize(kMaximumDetachedNodes);
  if (domain == zc::none || sourceBytes == zc::none || count == zc::none) { return zc::none; }
  ZC_IF_SOME(value, domain) {
    if (!sameDomain(value.asPtr(), kModuleBodyProvenanceDomain)) { return zc::none; }
  }
  zc::Maybe<identity::SourceFileKey> source;
  ZC_IF_SOME(value, sourceBytes) { source = decodeSourceKey(value.asPtr()); }
  if (source == zc::none) { return zc::none; }
  zc::Vector<ModuleBodyProvenanceEntry> entries(static_cast<size_t>(ZC_ASSERT_NONNULL(count)));
  ZC_IF_SOME(value, count) {
    for (uint64_t index = 0; index < value; ++index) {
      auto pathBytes = decoder.decodeByteString(kMaximumPathBytes);
      auto node = decoder.decodeUint32();
      auto start = decoder.decodeUint64();
      auto end = decoder.decodeUint64();
      if (pathBytes == zc::none || node == zc::none || start == zc::none || end == zc::none) {
        return zc::none;
      }
      ZC_IF_SOME(pathValue, pathBytes) {
        auto path = LocalSyntaxPath::decodeCanonical(pathValue.asPtr());
        if (path == zc::none) { return zc::none; }
        ZC_IF_SOME(decodedPath, path) {
          entries.add(ModuleBodyProvenanceEntry{zc::mv(decodedPath),
                                                ast::NodeId(ZC_ASSERT_NONNULL(node)),
                                                ZC_ASSERT_NONNULL(start), ZC_ASSERT_NONNULL(end)});
        }
      }
    }
  }
  if (!decoder.finished()) { return zc::none; }
  ZC_IF_SOME(sourceValue, source) { return from(zc::mv(sourceValue), zc::mv(entries)); }
  return zc::none;
}

ModuleBodyProvenance ModuleBodyProvenance::clone() const {
  zc::Vector<ModuleBodyProvenanceEntry> entries(impl->entries.size());
  for (const auto& entry : impl->entries) { entries.add(entry.clone()); }
  auto result = from(impl->source.clone(), zc::mv(entries));
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_UNREACHABLE
}

const identity::SourceFileKey& ModuleBodyProvenance::source() const noexcept {
  return impl->source;
}

zc::ArrayPtr<const ModuleBodyProvenanceEntry> ModuleBodyProvenance::entries() const {
  return impl->entries.asPtr();
}

zc::Array<uint8_t> ModuleBodyProvenance::encodeCanonical() const {
  identity::CanonicalEncoder encoder;
  encoder.encodeByteString(kModuleBodyProvenanceDomain.asBytes());
  const auto source = impl->source.encode();
  encoder.encodeByteString(source.asPtr());
  encoder.encodeSequenceSize(impl->entries.size());
  for (const auto& entry : impl->entries) {
    const auto path = entry.path.encode();
    encoder.encodeByteString(path.asPtr());
    encoder.encodeUint32(entry.node.value);
    encoder.encodeUint64(entry.byteStart);
    encoder.encodeUint64(entry.byteEnd);
  }
  return encoder.finish();
}

bool ModuleBodyProvenance::operator==(const ModuleBodyProvenance& other) const {
  if (!impl->source.sameAs(other.impl->source) ||
      impl->entries.size() != other.impl->entries.size()) {
    return false;
  }
  for (size_t index = 0; index < impl->entries.size(); ++index) {
    const auto& left = impl->entries[index];
    const auto& right = other.impl->entries[index];
    if (left.path != right.path || left.node != right.node || left.byteStart != right.byteStart ||
        left.byteEnd != right.byteEnd) {
      return false;
    }
  }
  return true;
}

NamedItemSyntax::NamedItemSyntax(identity::ModuleKey&& owningModule,
                                 ModuleBodySyntax&& syntax) noexcept
    : owningModuleField(zc::mv(owningModule)), syntaxField(zc::mv(syntax)) {}

zc::Maybe<NamedItemSyntax> NamedItemSyntax::from(identity::ModuleKey&& owningModule,
                                                 ModuleBodySyntax&& syntax) {
  if (syntax.rootCount() != 1 || syntax.nodes().size() == 0) { return zc::none; }
  return NamedItemSyntax(zc::mv(owningModule), zc::mv(syntax));
}

zc::Maybe<NamedItemSyntax> NamedItemSyntax::decodeCanonical(zc::ArrayPtr<const uint8_t> encoded) {
  if (encoded.size() == 0 || encoded.size() > kMaximumNamedItemValueBytes) { return zc::none; }
  identity::CanonicalDecoder decoder(encoded);
  auto domain = decoder.decodeByteString(kNamedItemSyntaxDomain.size());
  auto owningModule = identity::ModuleKey::decodeCanonical(decoder);
  auto syntaxBytes = decoder.decodeByteString(kMaximumNamedItemValueBytes);
  if (domain == zc::none || owningModule == zc::none || syntaxBytes == zc::none ||
      !decoder.finished() ||
      ZC_ASSERT_NONNULL(domain).asPtr() != kNamedItemSyntaxDomain.asBytes()) {
    return zc::none;
  }
  auto syntax = ModuleBodySyntax::decodeCanonical(ZC_ASSERT_NONNULL(syntaxBytes).asPtr());
  if (syntax == zc::none) { return zc::none; }
  return from(zc::mv(ZC_ASSERT_NONNULL(owningModule)), zc::mv(ZC_ASSERT_NONNULL(syntax)));
}

NamedItemSyntax NamedItemSyntax::clone() const {
  return NamedItemSyntax(owningModuleField.clone(), syntaxField.clone());
}

const identity::ModuleKey& NamedItemSyntax::owningModule() const noexcept {
  return owningModuleField;
}

const ModuleBodySyntax& NamedItemSyntax::detachedSyntax() const noexcept { return syntaxField; }

zc::Array<uint8_t> NamedItemSyntax::encodeCanonical() const {
  identity::CanonicalEncoder encoder;
  encoder.encodeByteString(kNamedItemSyntaxDomain.asBytes());
  owningModuleField.encode(encoder);
  auto syntax = syntaxField.encodeCanonical();
  encoder.encodeByteString(syntax.asPtr());
  return encoder.finish();
}

bool NamedItemSyntax::operator==(const NamedItemSyntax& other) const noexcept {
  return sameModule(owningModuleField, other.owningModuleField) && syntaxField == other.syntaxField;
}

NamedItemProvenance::NamedItemProvenance(ModuleBodyProvenance&& provenance) noexcept
    : provenanceField(zc::mv(provenance)) {}

zc::Maybe<NamedItemProvenance> NamedItemProvenance::from(ModuleBodyProvenance&& provenance) {
  if (provenance.entries().size() == 0) { return zc::none; }
  return NamedItemProvenance(zc::mv(provenance));
}

zc::Maybe<NamedItemProvenance> NamedItemProvenance::decodeCanonical(
    zc::ArrayPtr<const uint8_t> encoded) {
  if (encoded.size() == 0 || encoded.size() > kMaximumNamedItemValueBytes) { return zc::none; }
  identity::CanonicalDecoder decoder(encoded);
  auto domain = decoder.decodeByteString(kNamedItemProvenanceDomain.size());
  auto provenanceBytes = decoder.decodeByteString(kMaximumNamedItemValueBytes);
  if (domain == zc::none || provenanceBytes == zc::none || !decoder.finished() ||
      ZC_ASSERT_NONNULL(domain).asPtr() != kNamedItemProvenanceDomain.asBytes()) {
    return zc::none;
  }
  auto provenance =
      ModuleBodyProvenance::decodeCanonical(ZC_ASSERT_NONNULL(provenanceBytes).asPtr());
  if (provenance == zc::none) { return zc::none; }
  return from(zc::mv(ZC_ASSERT_NONNULL(provenance)));
}

NamedItemProvenance NamedItemProvenance::clone() const {
  return NamedItemProvenance(provenanceField.clone());
}

const ModuleBodyProvenance& NamedItemProvenance::detachedProvenance() const noexcept {
  return provenanceField;
}

zc::Array<uint8_t> NamedItemProvenance::encodeCanonical() const {
  identity::CanonicalEncoder encoder;
  encoder.encodeByteString(kNamedItemProvenanceDomain.asBytes());
  auto provenance = provenanceField.encodeCanonical();
  encoder.encodeByteString(provenance.asPtr());
  return encoder.finish();
}

bool NamedItemProvenance::operator==(const NamedItemProvenance& other) const {
  return provenanceField == other.provenanceField;
}

}  // namespace zomlang::compiler::binder
