// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zc/core/debug.h"
#include "zomlang/compiler/ast/generated/node-schema.h"
#include "zomlang/compiler/ast/generated/node-traverse.h"
#include "zomlang/compiler/binder/definition-inventory.h"
#include "zomlang/compiler/binder/module-body-syntax.h"
#include "zomlang/compiler/identity/canonical-encoder.h"
#include "zomlang/compiler/identity/canonical-scalar.h"

namespace zomlang::compiler::binder {
namespace {

ModuleBodySyntaxProjectionResult failure(ModuleBodySyntaxFailureKind kind,
                                         ast::NodeId node = ast::NodeId()) {
  return ModuleBodySyntaxFailure{kind, node};
}

bool sameModule(const identity::ModuleKey& left, const identity::ModuleKey& right) {
  const auto leftBytes = left.encode();
  const auto rightBytes = right.encode();
  return leftBytes.asPtr() == rightBytes.asPtr();
}

bool findTreePath(const ast::Tree& tree, ast::NodeId current, ast::NodeId target,
                  zc::Vector<uint32_t>& path) {
  if (current == target) { return true; }
  uint32_t childIndex = 0;
  bool found = false;
  ast::visitChildNodeIds(tree, tree.node(current), [&](ast::NodeId child) {
    const uint32_t currentIndex = childIndex++;
    if (found) { return; }
    path.add(currentIndex);
    if (findTreePath(tree, child, target, path)) {
      found = true;
    } else {
      path.removeLast();
    }
  });
  return found;
}

zc::Maybe<zc::ArrayPtr<const ast::NodeId>> moduleItems(const ast::Tree& tree,
                                                       ast::NodeId moduleNode) {
  if (!tree.contains(tree.root()) || tree.node(tree.root()).kind != ast::SyntaxKind::SourceFile) {
    return zc::none;
  }
  const auto& source = tree.node(tree.root());
  const ast::NodeId declaredModule(source.payload.words[ast::kSourceFileModuleWord]);
  ast::NodeList items;
  if (!moduleNode || moduleNode == tree.root()) {
    if (declaredModule) { return zc::none; }
    items = ast::NodeList{source.payload.words[ast::kSourceFileStatementsFirstWord],
                          source.payload.words[ast::kSourceFileStatementsSizeWord]};
  } else {
    if (!tree.contains(moduleNode) ||
        tree.node(moduleNode).kind != ast::SyntaxKind::ModuleDeclaration) {
      return zc::none;
    }
    const auto& module = tree.node(moduleNode);
    const auto form = static_cast<ast::ModuleDeclarationForm>(
        module.payload.words[ast::kModuleDeclarationFormWord]);
    if (form == ast::ModuleDeclarationForm::RootDeclaration) {
      if (declaredModule != moduleNode) { return zc::none; }
      items = ast::NodeList{source.payload.words[ast::kSourceFileStatementsFirstWord],
                            source.payload.words[ast::kSourceFileStatementsSizeWord]};
    } else if (form == ast::ModuleDeclarationForm::InlineRoot) {
      items = ast::NodeList{module.payload.words[ast::kModuleDeclarationInlineItemsFirstWord],
                            module.payload.words[ast::kModuleDeclarationInlineItemsSizeWord]};
    } else {
      return zc::none;
    }
  }
  if (!tree.contains(items)) { return zc::none; }
  return tree.list(items);
}

zc::StringPtr textField(const ast::Tree& tree, const ast::NodeSchemaFieldEntry& field,
                        uint32_t raw) {
  switch (field.storage) {
    case ast::NodeSchemaFieldStorage::StringId:
      return tree.string(ast::StringId(raw));
    case ast::NodeSchemaFieldStorage::IdentId:
      return tree.ident(ast::IdentId(raw));
    case ast::NodeSchemaFieldStorage::BigIntId:
      return tree.bigInt(ast::BigIntId(raw));
    case ast::NodeSchemaFieldStorage::FloatId:
      return tree.floatLiteral(ast::FloatId(raw));
    default:
      ZC_UNREACHABLE
  }
}

bool encodeIdentifier(identity::CanonicalEncoder& encoder, zc::StringPtr source) {
  auto identifier = identity::SemanticIdentifier::fromSource(source);
  ZC_IF_SOME(value, identifier) {
    value.encode(encoder);
    return true;
  }
  auto declaredName = identity::DeclaredDefinitionName::fromSource(source);
  ZC_IF_SOME(value, declaredName) {
    value.encode(encoder);
    return true;
  }
  return false;
}

zc::Maybe<zc::Array<uint8_t>> encodeCanonicalFields(const ast::Tree& tree, const ast::Node& node,
                                                    uint32_t& childCount) {
  zc::Maybe<const ast::NodeSchemaEntry&> schema = ast::lookupNodeSchema(node.kind);
  if (schema == zc::none) { return zc::none; }
  const auto& schemaValue = ZC_ASSERT_NONNULL(schema);
  identity::CanonicalEncoder encoder;
  encoder.encodeSequenceSize(schemaValue.fieldCount);
  uint64_t children = 0;
  for (uint32_t index = 0; index < schemaValue.fieldCount; ++index) {
    const auto& field = schemaValue.fields[index];
    encoder.encodeUint8(static_cast<uint8_t>(field.storage) + 1);
    encoder.encodeBool(field.optional);
    const uint32_t raw = node.payload.words[field.firstWord];
    switch (field.storage) {
      case ast::NodeSchemaFieldStorage::NodeId: {
        const bool present = tree.contains(ast::NodeId(raw));
        if (!field.optional && !present) { return zc::none; }
        encoder.encodeBool(present);
        if (present) { ++children; }
        break;
      }
      case ast::NodeSchemaFieldStorage::NodeList: {
        const ast::NodeList list{raw, node.payload.words[field.secondWord]};
        if (!tree.contains(list)) { return zc::none; }
        encoder.encodeSequenceSize(list.size);
        children += list.size;
        break;
      }
      case ast::NodeSchemaFieldStorage::IdentList: {
        const ast::IdentList list{raw, node.payload.words[field.secondWord]};
        if (!tree.contains(list)) { return zc::none; }
        const auto identifiers = tree.identList(list);
        encoder.encodeSequenceSize(identifiers.size());
        for (const auto identifier : identifiers) {
          if (!encodeIdentifier(encoder, tree.ident(identifier))) { return zc::none; }
        }
        break;
      }
      case ast::NodeSchemaFieldStorage::StringId:
      case ast::NodeSchemaFieldStorage::IdentId:
      case ast::NodeSchemaFieldStorage::BigIntId:
      case ast::NodeSchemaFieldStorage::FloatId: {
        const bool present = raw != 0;
        if (!field.optional && !present) { return zc::none; }
        encoder.encodeBool(present);
        if (!present) { break; }
        const auto text = textField(tree, field, raw);
        if (field.storage == ast::NodeSchemaFieldStorage::IdentId) {
          if (!encodeIdentifier(encoder, text)) { return zc::none; }
        } else {
          encoder.encodeByteString(text.asBytes());
        }
        break;
      }
      case ast::NodeSchemaFieldStorage::Bool:
        encoder.encodeBool(raw != 0);
        break;
      case ast::NodeSchemaFieldStorage::UInt8:
        if (raw > UINT8_MAX) { return zc::none; }
        encoder.encodeUint8(static_cast<uint8_t>(raw));
        break;
      case ast::NodeSchemaFieldStorage::UInt16:
        if (raw > UINT16_MAX) { return zc::none; }
        encoder.encodeUint32(raw);
        break;
      case ast::NodeSchemaFieldStorage::UInt32:
      case ast::NodeSchemaFieldStorage::Enum:
        encoder.encodeUint32(raw);
        break;
      case ast::NodeSchemaFieldStorage::UInt64:
        encoder.encodeUint64(static_cast<uint64_t>(raw) |
                             (static_cast<uint64_t>(node.payload.words[field.secondWord]) << 32));
        break;
    }
    if (children > UINT32_MAX) { return zc::none; }
  }
  childCount = static_cast<uint32_t>(children);
  return encoder.finish();
}

class ProjectionBuilder final {
public:
  ProjectionBuilder(const CanonicalParsedModule& parsedModule,
                    zc::ArrayPtr<const ModuleBodyDefinitionBoundaryInput> definitions,
                    zc::ArrayPtr<const ModuleBodyImplementationBoundaryInput> implementations,
                    ast::NodeId unprunedRoot)
      : parsedModule(parsedModule),
        tree(parsedModule.tree()),
        definitions(definitions),
        implementations(implementations),
        unprunedRoot(unprunedRoot) {}

  bool visit(ast::NodeId node, zc::Vector<uint32_t>& path) {
    zc::Maybe<const ModuleBodyDefinitionBoundaryInput&> definition;
    for (const auto& candidate : definitions) {
      if (candidate.node != node) { continue; }
      if (definition != zc::none) { return false; }
      definition = candidate;
    }
    zc::Maybe<const ModuleBodyImplementationBoundaryInput&> implementation;
    for (const auto& candidate : implementations) {
      if (candidate.node != node) { continue; }
      if (implementation != zc::none) { return false; }
      implementation = candidate;
    }
    if (definition != zc::none && implementation != zc::none) { return false; }
    if (node != unprunedRoot) {
      ZC_IF_SOME(value, definition) {
        nodes.add(DetachedModuleBodyNode::definitionBoundary(value.key));
        return true;
      }
      ZC_IF_SOME(value, implementation) {
        nodes.add(DetachedModuleBodyNode::implementationBoundary(value.key));
        return true;
      }
    }

    const auto& syntax = tree.node(node);
    uint32_t childCount = 0;
    auto fields = encodeCanonicalFields(tree, syntax, childCount);
    if (fields == zc::none) { return false; }
    ZC_IF_SOME(fieldValue, fields) {
      auto detached = DetachedModuleBodyNode::syntax(syntax.kind, zc::mv(fieldValue), childCount);
      if (detached == zc::none) { return false; }
      ZC_IF_SOME(value, detached) { nodes.add(zc::mv(value)); }
    }
    auto span = parsedModule.spanFor(syntax.range);
    if (span == zc::none) { return false; }
    zc::Vector<uint32_t> pathComponents(path.size());
    pathComponents.addAll(path);
    auto retainedPath = LocalSyntaxPath::from(zc::mv(pathComponents));
    if (retainedPath == zc::none) { return false; }
    ZC_IF_SOME(spanValue, span) {
      ZC_IF_SOME(pathValue, retainedPath) {
        provenance.add(ModuleBodyProvenanceEntry{zc::mv(pathValue), node, spanValue.byteStart(),
                                                 spanValue.byteEnd()});
      }
    }

    uint32_t childIndex = 0;
    bool valid = true;
    ast::visitChildNodeIds(tree, syntax, [&](ast::NodeId child) {
      const uint32_t currentIndex = childIndex++;
      if (!valid) { return; }
      path.add(currentIndex);
      valid = visit(child, path);
      path.removeLast();
    });
    return valid && childIndex == childCount;
  }

  zc::Vector<DetachedModuleBodyNode> nodes;
  zc::Vector<ModuleBodyProvenanceEntry> provenance;

private:
  const CanonicalParsedModule& parsedModule;
  const ast::Tree& tree;
  zc::ArrayPtr<const ModuleBodyDefinitionBoundaryInput> definitions;
  zc::ArrayPtr<const ModuleBodyImplementationBoundaryInput> implementations;
  ast::NodeId unprunedRoot;
};

bool validateBoundaryInventory(
    const CanonicalParsedModule& parsedModule, const identity::ModuleKey& module,
    ast::NodeId moduleNode, zc::ArrayPtr<const ModuleBodyDefinitionBoundaryInput> definitions,
    zc::ArrayPtr<const ModuleBodyImplementationBoundaryInput> implementations) {
  const auto& tree = parsedModule.tree();
  const auto inventory = DefinitionInventory::collect(tree);
  const ast::NodeId inventoryModuleNode = moduleNode == tree.root() ? ast::NodeId() : moduleNode;

  for (size_t index = 0; index < definitions.size(); ++index) {
    if (!tree.contains(definitions[index].node)) { return false; }
    for (size_t prior = 0; prior < index; ++prior) {
      if (definitions[prior].node == definitions[index].node) { return false; }
    }
    bool matched = false;
    for (const auto& expected : inventory.definitions()) {
      if (expected.node == definitions[index].node && expected.moduleNode == inventoryModuleNode &&
          expected.site.value().is<DeclarationDefinitionSite>()) {
        if (matched) { return false; }
        matched = true;
      }
    }
    if (!matched) { return false; }
  }
  for (const auto& expected : inventory.definitions()) {
    if (expected.moduleNode != inventoryModuleNode ||
        !expected.site.value().is<DeclarationDefinitionSite>()) {
      continue;
    }
    size_t matches = 0;
    for (const auto& candidate : definitions) {
      if (candidate.node == expected.node) { ++matches; }
    }
    if (matches != 1) { return false; }
  }

  for (size_t index = 0; index < implementations.size(); ++index) {
    const auto& candidate = implementations[index];
    if (!tree.contains(candidate.node) || !sameModule(candidate.key.site().module(), module) ||
        !candidate.key.site().source().sameAs(parsedModule.source())) {
      return false;
    }
    for (size_t prior = 0; prior < index; ++prior) {
      if (implementations[prior].node == candidate.node) { return false; }
    }
    bool matched = false;
    for (const auto& expected : inventory.impls()) {
      if (expected.node == candidate.node && expected.moduleNode == inventoryModuleNode) {
        if (matched) { return false; }
        matched = true;
      }
    }
    if (!matched) { return false; }
    zc::Vector<uint32_t> path;
    if (!findTreePath(tree, tree.root(), candidate.node, path) ||
        candidate.key.site().moduleSyntaxPath() != path.asPtr()) {
      return false;
    }
    for (const auto& definition : definitions) {
      if (definition.node == candidate.node) { return false; }
    }
  }
  for (const auto& expected : inventory.impls()) {
    if (expected.moduleNode != inventoryModuleNode) { continue; }
    size_t matches = 0;
    for (const auto& candidate : implementations) {
      if (candidate.node == expected.node) { ++matches; }
    }
    if (matches != 1) { return false; }
  }
  return true;
}

zc::Vector<ModuleBodyDefinitionBoundaryInput> definitionBoundaries(
    const CanonicalParsedModule& parsedModule, ast::NodeId moduleNode,
    const StableIdentityAdmission& admission) {
  const auto inventory = DefinitionInventory::collect(parsedModule.tree());
  const ast::NodeId inventoryModuleNode =
      moduleNode == parsedModule.tree().root() ? ast::NodeId() : moduleNode;
  zc::Vector<ModuleBodyDefinitionBoundaryInput> result(admission.definitions().size());
  for (const auto& definition : admission.definitions()) {
    for (const auto& expected : inventory.definitions()) {
      if (expected.node == definition.node && expected.moduleNode == inventoryModuleNode &&
          expected.site.value().is<DeclarationDefinitionSite>()) {
        result.add(
            ModuleBodyDefinitionBoundaryInput{definition.node, definition.authority.key().clone()});
        break;
      }
    }
  }
  return result;
}

zc::Vector<ModuleBodyImplementationBoundaryInput> implementationBoundaries(
    const StableIdentityAdmission& admission) {
  zc::Vector<ModuleBodyImplementationBoundaryInput> result(admission.implementations().size());
  for (const auto& implementation : admission.implementations()) {
    result.add(ModuleBodyImplementationBoundaryInput{
        implementation.node, ImplSourceOccurrenceKey::from(implementation.authority.key().clone(),
                                                           implementation.site.key().clone())});
  }
  return result;
}

}  // namespace

ModuleBodySyntaxProjectionResult ModuleBodySyntaxProducer::produce(
    const CanonicalParsedModule& parsedModule, const identity::ModuleKey& module,
    ast::NodeId moduleNode, const StableIdentityAdmission& admission) {
  if (!sameModule(admission.module(), module) ||
      !admission.source().sameAs(parsedModule.source()) ||
      admission.sourceDigest() != parsedModule.contentDigest()) {
    return failure(ModuleBodySyntaxFailureKind::InvalidBoundaryInventory);
  }
  auto definitions = definitionBoundaries(parsedModule, moduleNode, admission);
  auto implementations = implementationBoundaries(admission);
  return produce(parsedModule, module, moduleNode, definitions.asPtr(), implementations.asPtr());
}

ModuleBodySyntaxProjectionResult ModuleBodySyntaxProducer::produce(
    const CanonicalParsedModule& parsedModule, const identity::ModuleKey& module,
    ast::NodeId moduleNode, zc::ArrayPtr<const ModuleBodyDefinitionBoundaryInput> definitions,
    zc::ArrayPtr<const ModuleBodyImplementationBoundaryInput> implementations) {
  if (!parsedModule.source().belongsTo(module.crate())) {
    return failure(ModuleBodySyntaxFailureKind::InvalidSource);
  }
  auto items = moduleItems(parsedModule.tree(), moduleNode);
  if (items == zc::none) {
    return failure(ModuleBodySyntaxFailureKind::InvalidModuleRoot, moduleNode);
  }
  if (!validateBoundaryInventory(parsedModule, module, moduleNode, definitions, implementations)) {
    return failure(ModuleBodySyntaxFailureKind::InvalidBoundaryInventory);
  }

  ProjectionBuilder builder(parsedModule, definitions, implementations, ast::NodeId());
  ZC_IF_SOME(moduleItemsValue, items) {
    uint32_t rootIndex = 0;
    for (const auto item : moduleItemsValue) {
      zc::Vector<uint32_t> path;
      path.add(rootIndex++);
      if (!builder.visit(item, path)) {
        return failure(ModuleBodySyntaxFailureKind::InvalidDetachedSyntax, item);
      }
    }
    auto syntax = ModuleBodySyntax::from(rootIndex, zc::mv(builder.nodes));
    if (syntax == zc::none) { return failure(ModuleBodySyntaxFailureKind::InvalidDetachedSyntax); }
    auto provenance =
        ModuleBodyProvenance::from(parsedModule.source().clone(), zc::mv(builder.provenance));
    if (provenance == zc::none) { return failure(ModuleBodySyntaxFailureKind::InvalidProvenance); }
    ZC_IF_SOME(syntaxValue, syntax) {
      ZC_IF_SOME(provenanceValue, provenance) {
        return ModuleBodySyntaxProjection{zc::mv(syntaxValue), zc::mv(provenanceValue)};
      }
    }
  }
  return failure(ModuleBodySyntaxFailureKind::InvalidDetachedSyntax);
}

ModuleBodySyntaxProjectionResult ModuleBodySyntaxProducer::produceNamedItem(
    const CanonicalParsedModule& parsedModule, const identity::ModuleKey& module,
    ast::NodeId moduleNode, ast::NodeId definitionNode, const identity::DefinitionKey& definition,
    zc::ArrayPtr<const ModuleBodyDefinitionBoundaryInput> definitions,
    zc::ArrayPtr<const ModuleBodyImplementationBoundaryInput> implementations) {
  if (!parsedModule.source().belongsTo(module.crate())) {
    return failure(ModuleBodySyntaxFailureKind::InvalidSource);
  }
  if (!parsedModule.tree().contains(definitionNode)) {
    return failure(ModuleBodySyntaxFailureKind::InvalidDetachedSyntax, definitionNode);
  }
  if (!validateBoundaryInventory(parsedModule, module, moduleNode, definitions, implementations)) {
    return failure(ModuleBodySyntaxFailureKind::InvalidBoundaryInventory);
  }
  size_t rootOccurrences = 0;
  for (const auto& candidate : definitions) {
    if (candidate.node != definitionNode) { continue; }
    ++rootOccurrences;
    if (candidate.key != definition) {
      return failure(ModuleBodySyntaxFailureKind::InvalidBoundaryInventory, definitionNode);
    }
  }
  if (rootOccurrences > 1) {
    return failure(ModuleBodySyntaxFailureKind::InvalidBoundaryInventory, definitionNode);
  }

  ProjectionBuilder builder(parsedModule, definitions, implementations, definitionNode);
  zc::Vector<uint32_t> path;
  path.add(0);
  if (!builder.visit(definitionNode, path)) {
    return failure(ModuleBodySyntaxFailureKind::InvalidDetachedSyntax, definitionNode);
  }
  auto syntax = ModuleBodySyntax::from(1, zc::mv(builder.nodes));
  auto provenance =
      ModuleBodyProvenance::from(parsedModule.source().clone(), zc::mv(builder.provenance));
  if (syntax == zc::none) {
    return failure(ModuleBodySyntaxFailureKind::InvalidDetachedSyntax, definitionNode);
  }
  if (provenance == zc::none) {
    return failure(ModuleBodySyntaxFailureKind::InvalidProvenance, definitionNode);
  }
  return ModuleBodySyntaxProjection{zc::mv(ZC_ASSERT_NONNULL(syntax)),
                                    zc::mv(ZC_ASSERT_NONNULL(provenance))};
}

}  // namespace zomlang::compiler::binder
