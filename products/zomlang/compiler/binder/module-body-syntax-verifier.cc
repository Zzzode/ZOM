// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zc/core/debug.h"
#include "zomlang/compiler/ast/generated/node-schema.h"
#include "zomlang/compiler/ast/generated/node-traverse.h"
#include "zomlang/compiler/binder/module-body-syntax.h"
#include "zomlang/compiler/identity/canonical-encoder.h"
#include "zomlang/compiler/identity/canonical-scalar.h"

namespace zomlang::compiler::binder {
namespace {

bool equalModule(const identity::ModuleKey& left, const identity::ModuleKey& right) {
  return left.encode().asPtr() == right.encode().asPtr();
}

zc::Maybe<zc::ArrayPtr<const ast::NodeId>> independentlySelectItems(const ast::Tree& tree,
                                                                    ast::NodeId selectedRoot) {
  if (!tree.contains(tree.root())) { return zc::none; }
  const auto& source = tree.node(tree.root());
  if (source.kind != ast::SyntaxKind::SourceFile) { return zc::none; }
  const ast::NodeId declaration(source.payload.words[ast::kSourceFileModuleWord]);
  if (!selectedRoot || selectedRoot == tree.root()) {
    if (declaration) { return zc::none; }
    const ast::NodeList items{source.payload.words[ast::kSourceFileStatementsFirstWord],
                              source.payload.words[ast::kSourceFileStatementsSizeWord]};
    return tree.contains(items) ? zc::Maybe<zc::ArrayPtr<const ast::NodeId>>(tree.list(items))
                                : zc::none;
  }
  if (!tree.contains(selectedRoot)) { return zc::none; }
  const auto& root = tree.node(selectedRoot);
  if (root.kind != ast::SyntaxKind::ModuleDeclaration) { return zc::none; }
  const auto form =
      static_cast<ast::ModuleDeclarationForm>(root.payload.words[ast::kModuleDeclarationFormWord]);
  ast::NodeList items;
  if (form == ast::ModuleDeclarationForm::RootDeclaration ||
      form == ast::ModuleDeclarationForm::Alias) {
    if (declaration != selectedRoot) { return zc::none; }
    items = ast::NodeList{source.payload.words[ast::kSourceFileStatementsFirstWord],
                          source.payload.words[ast::kSourceFileStatementsSizeWord]};
  } else if (form == ast::ModuleDeclarationForm::InlineRoot) {
    items = ast::NodeList{root.payload.words[ast::kModuleDeclarationInlineItemsFirstWord],
                          root.payload.words[ast::kModuleDeclarationInlineItemsSizeWord]};
  } else {
    return zc::none;
  }
  return tree.contains(items) ? zc::Maybe<zc::ArrayPtr<const ast::NodeId>>(tree.list(items))
                              : zc::none;
}

bool isStableDefinitionBoundary(const ast::Node& syntax) {
  switch (syntax.kind) {
    case ast::SyntaxKind::ExternDecl:
    case ast::SyntaxKind::ExternVarDecl:
    case ast::SyntaxKind::UnitVariant:
    case ast::SyntaxKind::TupleVariant:
    case ast::SyntaxKind::EnumDeclaration:
    case ast::SyntaxKind::FunctionDecl:
    case ast::SyntaxKind::ClassDecl:
    case ast::SyntaxKind::StructDecl:
    case ast::SyntaxKind::InterfaceDecl:
    case ast::SyntaxKind::ErrorDecl:
    case ast::SyntaxKind::AliasDecl:
    case ast::SyntaxKind::MethodDecl:
    case ast::SyntaxKind::FieldDecl:
    case ast::SyntaxKind::AssociatedTypeDecl:
    case ast::SyntaxKind::ConstructorDecl:
    case ast::SyntaxKind::DestructorDecl:
    case ast::SyntaxKind::ClassConstDecl:
      return true;
    case ast::SyntaxKind::ModuleDeclaration:
      return static_cast<ast::ModuleDeclarationForm>(
                 syntax.payload.words[ast::kModuleDeclarationFormWord]) ==
             ast::ModuleDeclarationForm::Alias;
    default:
      return false;
  }
}

struct IndependentBoundaryCensus final {
  zc::Vector<ast::NodeId> definitions;
};

void collectIndependentBoundaries(const ast::Tree& tree, ast::NodeId node,
                                  IndependentBoundaryCensus& census) {
  const auto& syntax = tree.node(node);
  if (isStableDefinitionBoundary(syntax)) { census.definitions.add(node); }
  ast::visitChildNodeIds(
      tree, syntax, [&](ast::NodeId child) { collectIndependentBoundaries(tree, child, census); });
}

bool appendCanonicalIdentifier(identity::CanonicalEncoder& encoder, zc::StringPtr text) {
  auto value = identity::SemanticIdentifier::fromSource(text);
  ZC_IF_SOME(identifier, value) {
    identifier.encode(encoder);
    return true;
  }
  auto declaredName = identity::DeclaredDefinitionName::fromSource(text);
  ZC_IF_SOME(identifier, declaredName) {
    identifier.encode(encoder);
    return true;
  }
  return false;
}

zc::StringPtr independentlyReadText(const ast::Tree& tree, const ast::NodeSchemaFieldEntry& field,
                                    uint32_t raw) {
  if (field.storage == ast::NodeSchemaFieldStorage::StringId) {
    return tree.string(ast::StringId(raw));
  }
  if (field.storage == ast::NodeSchemaFieldStorage::IdentId) {
    return tree.ident(ast::IdentId(raw));
  }
  if (field.storage == ast::NodeSchemaFieldStorage::BigIntId) {
    return tree.bigInt(ast::BigIntId(raw));
  }
  if (field.storage == ast::NodeSchemaFieldStorage::FloatId) {
    return tree.floatLiteral(ast::FloatId(raw));
  }
  ZC_UNREACHABLE
}

zc::Maybe<zc::Array<uint8_t>> independentlyEncodeFields(const ast::Tree& tree,
                                                        const ast::Node& syntax,
                                                        uint32_t& childCount) {
  zc::Maybe<const ast::NodeSchemaEntry&> schema = ast::lookupNodeSchema(syntax.kind);
  if (schema == zc::none) { return zc::none; }
  const auto& schemaValue = ZC_ASSERT_NONNULL(schema);
  identity::CanonicalEncoder encoder;
  encoder.encodeSequenceSize(schemaValue.fieldCount);
  uint64_t children = 0;
  for (uint32_t index = 0; index < schemaValue.fieldCount; ++index) {
    const auto& field = schemaValue.fields[index];
    const uint32_t raw = syntax.payload.words[field.firstWord];
    encoder.encodeUint8(static_cast<uint8_t>(field.storage) + 1);
    encoder.encodeBool(field.optional);
    switch (field.storage) {
      case ast::NodeSchemaFieldStorage::NodeId: {
        const bool present = tree.contains(ast::NodeId(raw));
        if (!field.optional && !present) { return zc::none; }
        encoder.encodeBool(present);
        children += present ? 1 : 0;
        break;
      }
      case ast::NodeSchemaFieldStorage::NodeList: {
        const ast::NodeList list{raw, syntax.payload.words[field.secondWord]};
        if (!tree.contains(list)) { return zc::none; }
        const auto values = tree.list(list);
        encoder.encodeSequenceSize(values.size());
        children += values.size();
        break;
      }
      case ast::NodeSchemaFieldStorage::IdentList: {
        const ast::IdentList list{raw, syntax.payload.words[field.secondWord]};
        if (!tree.contains(list)) { return zc::none; }
        const auto values = tree.identList(list);
        encoder.encodeSequenceSize(values.size());
        for (const auto value : values) {
          if (!appendCanonicalIdentifier(encoder, tree.ident(value))) { return zc::none; }
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
        if (present) {
          const auto text = independentlyReadText(tree, field, raw);
          if (field.storage == ast::NodeSchemaFieldStorage::IdentId) {
            if (!appendCanonicalIdentifier(encoder, text)) { return zc::none; }
          } else {
            encoder.encodeByteString(text.asBytes());
          }
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
                             (static_cast<uint64_t>(syntax.payload.words[field.secondWord]) << 32));
        break;
    }
    if (children > UINT32_MAX) { return zc::none; }
  }
  childCount = static_cast<uint32_t>(children);
  return encoder.finish();
}

bool independentlyValidateInventory(
    const CanonicalParsedModule& parsed, const identity::ModuleKey& module, ast::NodeId moduleNode,
    zc::ArrayPtr<const ModuleBodyDefinitionBoundaryInput> definitions) {
  const auto& tree = parsed.tree();
  auto items = independentlySelectItems(tree, moduleNode);
  if (items == zc::none) { return false; }
  IndependentBoundaryCensus census;
  ZC_IF_SOME(values, items) {
    for (const auto item : values) { collectIndependentBoundaries(tree, item, census); }
  }
  for (const auto expected : census.definitions) {
    size_t found = 0;
    for (const auto& supplied : definitions) {
      if (supplied.node == expected) { ++found; }
    }
    if (found != 1) { return false; }
  }
  if (census.definitions.size() != definitions.size()) { return false; }

  return true;
}

class IndependentWalker final {
public:
  IndependentWalker(const CanonicalParsedModule& parsed,
                    zc::ArrayPtr<const ModuleBodyDefinitionBoundaryInput> definitions,
                    ast::NodeId unprunedRoot)
      : parsed(parsed), definitions(definitions), unprunedRoot(unprunedRoot) {}

  bool walk(ast::NodeId node, zc::Vector<uint32_t>& path) {
    if (node != unprunedRoot) {
      for (const auto& definition : definitions) {
        if (definition.node == node) {
          nodes.add(DetachedModuleBodyNode::definitionBoundary(definition.key));
          return true;
        }
      }
    }

    const auto& syntax = parsed.tree().node(node);
    uint32_t expectedChildren = 0;
    auto fields = independentlyEncodeFields(parsed.tree(), syntax, expectedChildren);
    if (fields == zc::none) { return false; }
    ZC_IF_SOME(fieldValue, fields) {
      auto detached =
          DetachedModuleBodyNode::syntax(syntax.kind, zc::mv(fieldValue), expectedChildren);
      if (detached == zc::none) { return false; }
      ZC_IF_SOME(value, detached) { nodes.add(zc::mv(value)); }
    }
    auto span = parsed.spanFor(syntax.range);
    if (span == zc::none) { return false; }
    zc::Vector<uint32_t> retained(path.size());
    retained.addAll(path);
    auto localPath = LocalSyntaxPath::from(zc::mv(retained));
    if (localPath == zc::none) { return false; }
    ZC_IF_SOME(spanValue, span) {
      ZC_IF_SOME(pathValue, localPath) {
        provenance.add(ModuleBodyProvenanceEntry{zc::mv(pathValue), node, spanValue.byteStart(),
                                                 spanValue.byteEnd()});
      }
    }

    zc::Vector<ast::NodeId> children;
    ast::visitChildNodeIds(parsed.tree(), syntax, [&](ast::NodeId child) { children.add(child); });
    if (children.size() != expectedChildren) { return false; }
    for (size_t index = 0; index < children.size(); ++index) {
      path.add(static_cast<uint32_t>(index));
      if (!walk(children[index], path)) { return false; }
      path.removeLast();
    }
    return true;
  }

  zc::Vector<DetachedModuleBodyNode> nodes;
  zc::Vector<ModuleBodyProvenanceEntry> provenance;

private:
  const CanonicalParsedModule& parsed;
  zc::ArrayPtr<const ModuleBodyDefinitionBoundaryInput> definitions;
  ast::NodeId unprunedRoot;
};

zc::Maybe<zc::Vector<ModuleBodyDefinitionBoundaryInput>> definitionBoundaries(
    const CanonicalParsedModule& parsedModule, ast::NodeId moduleNode,
    const StableIdentityAdmission& admission) {
  auto items = independentlySelectItems(parsedModule.tree(), moduleNode);
  if (items == zc::none) { return zc::none; }
  IndependentBoundaryCensus census;
  ZC_IF_SOME(values, items) {
    for (const auto item : values) {
      collectIndependentBoundaries(parsedModule.tree(), item, census);
    }
  }
  zc::Vector<ModuleBodyDefinitionBoundaryInput> result(census.definitions.size());
  for (const auto node : census.definitions) {
    size_t matches = 0;
    for (const auto& definition : admission.definitions()) {
      if (definition.node != node) { continue; }
      ++matches;
      result.add(ModuleBodyDefinitionBoundaryInput{node, definition.authority.key().clone()});
    }
    if (matches != 1) { return zc::none; }
  }
  return result;
}

}  // namespace

ModuleBodySyntaxProjectionResult ModuleBodySyntaxVerifier::reconstruct(
    const CanonicalParsedModule& parsedModule, const identity::ModuleKey& module,
    ast::NodeId moduleNode, const StableIdentityAdmission& admission) {
  if (!equalModule(admission.module(), module) ||
      !admission.source().sameAs(parsedModule.source()) ||
      admission.sourceDigest() != parsedModule.contentDigest()) {
    return ModuleBodySyntaxFailure{ModuleBodySyntaxFailureKind::InvalidBoundaryInventory,
                                   moduleNode};
  }
  auto definitions = definitionBoundaries(parsedModule, moduleNode, admission);
  if (definitions == zc::none) {
    return ModuleBodySyntaxFailure{ModuleBodySyntaxFailureKind::InvalidBoundaryInventory,
                                   moduleNode};
  }
  return reconstruct(parsedModule, module, moduleNode, ZC_ASSERT_NONNULL(definitions).asPtr());
}

ModuleBodySyntaxProjectionResult ModuleBodySyntaxVerifier::reconstruct(
    const CanonicalParsedModule& parsedModule, const identity::ModuleKey& module,
    ast::NodeId moduleNode, zc::ArrayPtr<const ModuleBodyDefinitionBoundaryInput> definitions) {
  if (!parsedModule.source().belongsTo(module.crate())) {
    return ModuleBodySyntaxFailure{ModuleBodySyntaxFailureKind::InvalidSource, ast::NodeId()};
  }
  auto items = independentlySelectItems(parsedModule.tree(), moduleNode);
  if (items == zc::none) {
    return ModuleBodySyntaxFailure{ModuleBodySyntaxFailureKind::InvalidModuleRoot, moduleNode};
  }
  if (!independentlyValidateInventory(parsedModule, module, moduleNode, definitions)) {
    return ModuleBodySyntaxFailure{ModuleBodySyntaxFailureKind::InvalidBoundaryInventory,
                                   ast::NodeId()};
  }

  IndependentWalker walker(parsedModule, definitions, ast::NodeId());
  uint32_t rootCount = 0;
  ZC_IF_SOME(values, items) {
    for (const auto item : values) {
      zc::Vector<uint32_t> path;
      path.add(rootCount++);
      if (!walker.walk(item, path)) {
        return ModuleBodySyntaxFailure{ModuleBodySyntaxFailureKind::ProjectionMismatch, item};
      }
    }
  }
  auto expectedSyntax = ModuleBodySyntax::from(rootCount, zc::mv(walker.nodes));
  auto expectedProvenance =
      ModuleBodyProvenance::from(parsedModule.source().clone(), zc::mv(walker.provenance));
  if (expectedSyntax == zc::none || expectedProvenance == zc::none) {
    return ModuleBodySyntaxFailure{ModuleBodySyntaxFailureKind::ProjectionMismatch, ast::NodeId()};
  }
  return ModuleBodySyntaxProjection{zc::mv(ZC_ASSERT_NONNULL(expectedSyntax)),
                                    zc::mv(ZC_ASSERT_NONNULL(expectedProvenance))};
}

ModuleBodySyntaxProjectionResult ModuleBodySyntaxVerifier::reconstructNamedItem(
    const CanonicalParsedModule& parsedModule, const identity::ModuleKey& module,
    ast::NodeId moduleNode, ast::NodeId definitionNode, const identity::DefinitionKey& definition,
    zc::ArrayPtr<const ModuleBodyDefinitionBoundaryInput> definitions) {
  if (!parsedModule.source().belongsTo(module.crate())) {
    return ModuleBodySyntaxFailure{ModuleBodySyntaxFailureKind::InvalidSource, ast::NodeId()};
  }
  if (!parsedModule.tree().contains(definitionNode) ||
      !independentlyValidateInventory(parsedModule, module, moduleNode, definitions)) {
    return ModuleBodySyntaxFailure{ModuleBodySyntaxFailureKind::InvalidBoundaryInventory,
                                   definitionNode};
  }
  size_t rootOccurrences = 0;
  for (const auto& candidate : definitions) {
    if (candidate.node != definitionNode) { continue; }
    ++rootOccurrences;
    if (candidate.key != definition) {
      return ModuleBodySyntaxFailure{ModuleBodySyntaxFailureKind::InvalidBoundaryInventory,
                                     definitionNode};
    }
  }
  if (rootOccurrences > 1) {
    return ModuleBodySyntaxFailure{ModuleBodySyntaxFailureKind::InvalidBoundaryInventory,
                                   definitionNode};
  }

  IndependentWalker walker(parsedModule, definitions, definitionNode);
  zc::Vector<uint32_t> path;
  path.add(0);
  if (!walker.walk(definitionNode, path)) {
    return ModuleBodySyntaxFailure{ModuleBodySyntaxFailureKind::ProjectionMismatch, definitionNode};
  }
  auto expectedSyntax = ModuleBodySyntax::from(1, zc::mv(walker.nodes));
  auto expectedProvenance =
      ModuleBodyProvenance::from(parsedModule.source().clone(), zc::mv(walker.provenance));
  if (expectedSyntax == zc::none || expectedProvenance == zc::none) {
    return ModuleBodySyntaxFailure{ModuleBodySyntaxFailureKind::ProjectionMismatch, definitionNode};
  }
  return ModuleBodySyntaxProjection{zc::mv(ZC_ASSERT_NONNULL(expectedSyntax)),
                                    zc::mv(ZC_ASSERT_NONNULL(expectedProvenance))};
}

ModuleBodySyntaxFailureKind ModuleBodySyntaxVerifier::verify(
    const CanonicalParsedModule& parsedModule, const identity::ModuleKey& module,
    ast::NodeId moduleNode, zc::ArrayPtr<const ModuleBodyDefinitionBoundaryInput> definitions,
    const ModuleBodySyntaxProjection& projection) {
  auto expected = reconstruct(parsedModule, module, moduleNode, definitions);
  if (expected.is<ModuleBodySyntaxFailure>()) {
    return expected.get<ModuleBodySyntaxFailure>().kind;
  }
  const auto& reconstructed = expected.get<ModuleBodySyntaxProjection>();
  return reconstructed.syntax == projection.syntax &&
                 reconstructed.provenance == projection.provenance
             ? ModuleBodySyntaxFailureKind::None
             : ModuleBodySyntaxFailureKind::ProjectionMismatch;
}

}  // namespace zomlang::compiler::binder
