// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/checker/body/body-checker.h"

#include <cstdint>

#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/ast/generated/node-traverse.h"
#include "zomlang/compiler/binder/metadata/definition-inventory.h"
#include "zomlang/compiler/checker/body/marker-proof.h"
#include "zomlang/compiler/checker/facts/scalar-literal-facts.h"
#include "zomlang/compiler/checker/inference/inference-recovery-context.h"
#include "zomlang/compiler/checker/operator-kind.h"
#include "zomlang/compiler/driver/core/marker-authority.h"
#include "zomlang/compiler/type/semantic-type-data.h"

namespace zomlang::compiler::checker::body {
namespace {

using checked::CheckedFactGroup;

enum class BodyProductionKind : uint8_t {
  NullLiteral = 0x01,
  BoolLiteral = 0x02,
  IntLiteral = 0x03,
  FloatLiteral = 0x04,
  BigIntLiteral = 0x05,
  StringLiteral = 0x06,
  UnitLiteral = 0x07,
  CharacterLiteral = 0x08,
  NoSubstitutionTemplateLiteral = 0x09,
  DirectCall = 0x0a,
  IdentifierReference = 0x0b,
  LocalWrite = 0x0c,
  OwnerLocalFieldReference = 0x0d,
  StructLiteral = 0x0e,
  OwnerLocalFieldWrite = 0x0f,
  ReferenceDereference = 0x10,
  ReferenceReborrow = 0x11,
  LocalBorrow = 0x12,
  ErrorOperator = 0x13,
  ConcreteMethodCall = 0x14,
  OwnerLocalMethodReference = 0x15,
  ReadIndex = 0x16,
  UnsafeBlock = 0x18,
  PrimitiveBinaryOperation = 0x19,
  Unsupported = 0x17
};

struct BodyProductionSite final {
  ast::NodeId node;
  checked::CheckedNodeKey key;
  CheckedFactGroup primaryGroup;
  BodyProductionKind production;
};

checked::CheckedNodeKey cloneNodeKey(const checked::CheckedNodeKey& key) {
  return checked::CheckedNodeKey{key.syntaxKind, key.schemaPreorder, key.sourceSpan.clone()};
}

checked::CheckedFactsInvariantRejected rejectInvariant(
    signature::CheckerInvariantKind kind, identity::ModuleId module, uint32_t ordinal,
    zc::Maybe<identity::DefId>&& owner = zc::none, zc::Maybe<ast::NodeId>&& node = zc::none,
    zc::Maybe<identity::SourceSpan>&& span = zc::none,
    zc::Vector<uint32_t>&& structuralFieldPath = zc::Vector<uint32_t>()) {
  zc::Maybe<identity::Sha256Digest> noExpected;
  zc::Maybe<identity::Sha256Digest> noActual;
  zc::Vector<signature::CheckerVerificationFailure> failures;
  failures.add(signature::CheckerVerificationFailure(signature::CheckerInvariantFact{
      kind, signature::CheckerInvariantStage::Body, module, zc::mv(owner), zc::mv(node),
      zc::mv(span), zc::mv(structuralFieldPath), zc::mv(noExpected), zc::mv(noActual), ordinal}));
  return checked::CheckedFactsInvariantRejected{zc::mv(failures)};
}

checked::CheckedFactsInvariantRejected rejectRecoveryInvariant(
    inference::InferenceRecoveryRejected&& rejection) {
  zc::Vector<signature::CheckerVerificationFailure> failures;
  failures.add(zc::mv(rejection.failure));
  return checked::CheckedFactsInvariantRejected{zc::mv(failures)};
}

zc::Maybe<const binder::PatternBindingSite&> patternBindingSite(
    const binder::DefinitionInventory& inventory,
    const binder::MaterializedDefinitionInventoryEntry& definition) {
  const auto& materialized = definition.site.value();
  if (materialized.is<binder::PatternBindingSite>()) {
    return materialized.get<binder::PatternBindingSite>();
  }
  zc::Maybe<const binder::PatternBindingSite&> result;
  for (const auto& candidate : inventory.definitions()) {
    if (candidate.node != definition.node || candidate.kind != definition.record.kind()) {
      continue;
    }
    const auto& site = candidate.site.value();
    if (!site.is<binder::PatternBindingSite>() || result != zc::none) { return zc::none; }
    result = site.get<binder::PatternBindingSite>();
  }
  return result;
}

zc::Maybe<identity::DefId> initializerOwner(
    const driver::module_graph_query::CheckerBoundModuleView& boundModule,
    ast::NodeId initializer) {
  const auto& tree = boundModule.tree();
  const auto inventory = binder::DefinitionInventory::collect(tree);
  for (const auto& definition : boundModule.definitions().definitions()) {
    ZC_IF_SOME(binding, patternBindingSite(inventory, definition)) {
      if (!tree.contains(binding.introducer) ||
          tree.node(binding.introducer).kind != ast::SyntaxKind::VariableDeclarator) {
        continue;
      }
      const auto& declarator = tree.node(binding.introducer);
      if (ast::NodeId(declarator.payload.words[ast::kVariableDeclaratorInitWord]) == initializer) {
        return definition.definition;
      }
    }
  }
  return zc::none;
}

bool subtreeContains(const ast::Tree& tree, ast::NodeId root, ast::NodeId target) {
  bool contains = false;
  ast::visitTreePreOrder(tree, root, [&](ast::NodeId node, const ast::Node&) {
    if (node == target) contains = true;
  });
  return contains;
}

bool isOwnerLocalPattern(const driver::module_graph_query::CheckerBoundModuleView& boundModule,
                         ast::NodeId node) {
  const auto& tree = boundModule.tree();
  for (const auto& local : boundModule.definitions().ownerLocalBindings()) {
    const auto& site = local.site.value();
    if (!site.is<binder::PatternBindingSite>()) continue;
    const auto& binding = site.get<binder::PatternBindingSite>();
    if (!tree.contains(binding.introducer) ||
        tree.node(binding.introducer).kind != ast::SyntaxKind::VariableDeclarator) {
      continue;
    }
    const auto& declarator = tree.node(binding.introducer);
    if (ast::NodeId(declarator.payload.words[ast::kVariableDeclaratorPatternWord]) != node) {
      continue;
    }
    for (const auto& definition : boundModule.definitions().definitions()) {
      if (definition.record.kind() != identity::DefinitionKind::Function ||
          !tree.contains(definition.node) ||
          tree.node(definition.node).kind != ast::SyntaxKind::FunctionDecl) {
        continue;
      }
      const ast::NodeId body(tree.node(definition.node).payload.words[ast::kFunctionDeclBodyWord]);
      if (tree.contains(body) && subtreeContains(tree, body, node)) { return true; }
    }
  }
  return false;
}

zc::Maybe<identity::DefId> returnValueOwner(
    const driver::module_graph_query::CheckerBoundModuleView& boundModule, ast::NodeId value) {
  const auto& tree = boundModule.tree();
  bool isReturnValue = false;
  ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId, const ast::Node& syntax) {
    if (syntax.kind == ast::SyntaxKind::ReturnStmt &&
        ast::NodeId(syntax.payload.words[ast::kReturnStmtValueWord]) == value) {
      isReturnValue = true;
    }
  });
  if (!isReturnValue) return zc::none;

  zc::Maybe<identity::DefId> result;
  size_t bestDepth = 0;
  for (const auto& definition : boundModule.definitions().definitions()) {
    if (definition.record.kind() != identity::DefinitionKind::Function ||
        !subtreeContains(tree, definition.node, value)) {
      continue;
    }
    const size_t depth = definition.record.owners().size();
    if (result == zc::none || depth > bestDepth) {
      result = definition.definition;
      bestDepth = depth;
    } else if (depth == bestDepth) {
      return zc::none;
    }
  }
  return result;
}

zc::Maybe<identity::SemanticTypeId> callableSuccess(const signature::VerifiedSignatureFacts& facts,
                                                    identity::DefId callable) {
  zc::Maybe<identity::SemanticTypeId> result;
  for (const auto& semanticSignature : facts.signatures()) {
    if (semanticSignature.definition != callable) continue;
    if (result != zc::none ||
        !semanticSignature.payload.variant().is<signature::CallableSignature>()) {
      return zc::none;
    }
    result = semanticSignature.payload.variant().get<signature::CallableSignature>().success;
  }
  return result;
}

zc::Maybe<identity::SemanticTypeId> valueType(const signature::VerifiedSignatureFacts& facts,
                                              identity::DefId definition) {
  zc::Maybe<identity::SemanticTypeId> result;
  for (const auto& semanticSignature : facts.signatures()) {
    if (semanticSignature.definition != definition) continue;
    if (result != zc::none ||
        !semanticSignature.payload.variant().is<signature::ValueSignature>()) {
      return zc::none;
    }
    result = semanticSignature.payload.variant().get<signature::ValueSignature>().type;
  }
  return result;
}

zc::Maybe<identity::DefId> resolvedDefinition(const binder::ImmutableBindingMetadata& bindings,
                                              ast::NodeId node) {
  zc::Maybe<identity::DefId> result;
  for (const auto& resolution : bindings.nodeBindings()) {
    if (resolution.node != node || !resolution.value.is<binder::BoundNameResolution>()) {
      continue;
    }
    const auto& target =
        resolution.value.get<binder::BoundNameResolution>().canonicalTarget.value();
    if (!target.is<binder::DefinitionBindingTarget>() || result != zc::none) { return zc::none; }
    result = target.get<binder::DefinitionBindingTarget>().definition;
  }
  return result;
}

zc::Maybe<binder::OwnerLocalBindingId> resolvedOwnerLocal(
    const binder::ImmutableBindingMetadata& bindings, ast::NodeId node) {
  zc::Maybe<binder::OwnerLocalBindingId> result;
  for (const auto& resolution : bindings.nodeBindings()) {
    if (resolution.node != node || !resolution.value.is<binder::BoundNameResolution>()) {
      continue;
    }
    const auto& target =
        resolution.value.get<binder::BoundNameResolution>().canonicalTarget.value();
    if (!target.is<binder::OwnerLocalBindingTarget>() || result != zc::none) { return zc::none; }
    result = target.get<binder::OwnerLocalBindingTarget>().binding;
  }
  return result;
}

zc::Maybe<identity::CallableParameterId> resolvedCallableParameter(
    const binder::ImmutableBindingMetadata& bindings, ast::NodeId node) {
  zc::Maybe<identity::CallableParameterId> result;
  for (const auto& resolution : bindings.nodeBindings()) {
    if (resolution.node != node || !resolution.value.is<binder::BoundNameResolution>()) {
      continue;
    }
    const auto& target =
        resolution.value.get<binder::BoundNameResolution>().canonicalTarget.value();
    if (!target.is<binder::CallableParameterBindingTarget>() || result != zc::none) {
      return zc::none;
    }
    result = target.get<binder::CallableParameterBindingTarget>().parameter;
  }
  return result;
}

zc::Maybe<ast::NodeId> ownerLocalInitializer(
    const driver::module_graph_query::CheckerBoundModuleView& boundModule,
    binder::OwnerLocalBindingId binding) {
  const auto& tree = boundModule.tree();
  for (const auto& local : boundModule.definitions().ownerLocalBindings()) {
    if (local.binding != binding) continue;
    const auto& site = local.site.value();
    if (!site.is<binder::PatternBindingSite>()) { return zc::none; }
    const auto& pattern = site.get<binder::PatternBindingSite>();
    if (!tree.contains(pattern.introducer) ||
        tree.node(pattern.introducer).kind != ast::SyntaxKind::VariableDeclarator) {
      return zc::none;
    }
    const ast::NodeId initializer(
        tree.node(pattern.introducer).payload.words[ast::kVariableDeclaratorInitWord]);
    if (!tree.contains(initializer)) return zc::none;
    return initializer;
  }
  return zc::none;
}

zc::Maybe<identity::SemanticTypeId> ownerLocalReferenceType(
    const BodyCheckingInput& input, ast::NodeId node,
    zc::ArrayPtr<const checked::NodeTypeMap::Entry> nodeTypes) {
  const auto binding = resolvedOwnerLocal(input.boundModule.bindings(), node);
  if (binding == zc::none) return zc::none;
  ZC_IF_SOME(value, binding) {
    const auto initializer = ownerLocalInitializer(input.boundModule, value);
    const auto& tree = input.boundModule.tree();
    ZC_IF_SOME(initializerNode, initializer) {
      for (const auto& entry : nodeTypes) {
        if (entry.key == initializerNode) return entry.value;
      }
      // The initializer's node type is not yet produced. A primitive-binary
      // initializer is typed one stage after a bare identifier reference, so a
      // reference to such a local resolves from the declarator's closed
      // annotation instead; the binary production separately verifies its result
      // type matches this annotation, so the two agree. A non-binary initializer
      // is always typed before its uses in schema preorder, so this fallback is
      // reached only for the binary case.
      if (tree.contains(initializerNode) &&
          tree.node(initializerNode).kind == ast::SyntaxKind::BinaryExpr) {
        for (const auto& local : input.boundModule.definitions().ownerLocalBindings()) {
          if (local.binding != value || !local.site.value().is<binder::PatternBindingSite>()) {
            continue;
          }
          const auto& site = local.site.value().get<binder::PatternBindingSite>();
          if (!tree.contains(site.introducer) ||
              tree.node(site.introducer).kind != ast::SyntaxKind::VariableDeclarator) {
            return zc::none;
          }
          const ast::NodeId annotation(
              tree.node(site.introducer).payload.words[ast::kVariableDeclaratorTyWord]);
          if (!tree.contains(annotation)) return zc::none;
          return signature::resolveClosedSourceType(input.boundModule, input.identities,
                                                    input.semanticTypes, annotation);
        }
      }
      return zc::none;
    }
    for (const auto& local : input.boundModule.definitions().ownerLocalBindings()) {
      if (local.binding != value || !local.site.value().is<binder::PatternBindingSite>()) {
        continue;
      }
      const auto& site = local.site.value().get<binder::PatternBindingSite>();
      if (!tree.contains(site.introducer) ||
          tree.node(site.introducer).kind != ast::SyntaxKind::VariableDeclarator) {
        return zc::none;
      }
      const ast::NodeId annotation(
          tree.node(site.introducer).payload.words[ast::kVariableDeclaratorTyWord]);
      if (!tree.contains(annotation)) return zc::none;
      return signature::resolveClosedSourceType(input.boundModule, input.identities,
                                                input.semanticTypes, annotation);
    }
  }
  return zc::none;
}

bool dependsOnStructuredLocalInitializer(const BodyCheckingInput& input, ast::NodeId node) {
  const auto& tree = input.boundModule.tree();
  zc::Vector<ast::NodeId> visited;
  ast::NodeId current = node;
  while (true) {
    const auto binding = resolvedOwnerLocal(input.boundModule.bindings(), current);
    if (binding == zc::none) return false;
    binder::OwnerLocalBindingId bindingValue;
    ZC_IF_SOME(value, binding) { bindingValue = value; }
    const auto initializer = ownerLocalInitializer(input.boundModule, bindingValue);
    if (initializer == zc::none) return false;
    ast::NodeId initializerNode;
    ZC_IF_SOME(value, initializer) { initializerNode = value; }
    if (!tree.contains(initializerNode)) return false;
    if (tree.node(initializerNode).kind == ast::SyntaxKind::StructLiteralExpr) return true;
    if (tree.node(initializerNode).kind != ast::SyntaxKind::IdentExpr) return false;
    for (const auto seen : visited) {
      if (seen == initializerNode) return false;
    }
    visited.add(initializerNode);
    current = initializerNode;
  }
}

bool dependsOnDirectCallLocalInitializer(const BodyCheckingInput& input, ast::NodeId node) {
  const auto binding = resolvedOwnerLocal(input.boundModule.bindings(), node);
  if (binding == zc::none) return false;
  ZC_IF_SOME(value, binding) {
    auto initializer = ownerLocalInitializer(input.boundModule, value);
    ZC_IF_SOME(initializerNode, initializer) {
      const auto& tree = input.boundModule.tree();
      return tree.contains(initializerNode) &&
             tree.node(initializerNode).kind == ast::SyntaxKind::CallExpression;
    }
  }
  return false;
}

// Returns the declared (annotation) type of the owner local whose declarator
// initializer is `initializer`, or none when the node is not an owner-local
// initializer or the declarator carries no closed type annotation. Used to
// enforce that a primitive-binary initializer's result type matches the local's
// annotation.
zc::Maybe<identity::SemanticTypeId> ownerLocalInitializerDeclaredType(
    const BodyCheckingInput& input, ast::NodeId initializer) {
  const auto& tree = input.boundModule.tree();
  for (const auto& local : input.boundModule.definitions().ownerLocalBindings()) {
    if (!local.site.value().is<binder::PatternBindingSite>()) continue;
    const auto& site = local.site.value().get<binder::PatternBindingSite>();
    if (!tree.contains(site.introducer) ||
        tree.node(site.introducer).kind != ast::SyntaxKind::VariableDeclarator) {
      continue;
    }
    const auto& declarator = tree.node(site.introducer);
    if (ast::NodeId(declarator.payload.words[ast::kVariableDeclaratorInitWord]) != initializer) {
      continue;
    }
    const ast::NodeId annotation(declarator.payload.words[ast::kVariableDeclaratorTyWord]);
    if (!tree.contains(annotation)) return zc::none;
    return signature::resolveClosedSourceType(input.boundModule, input.identities,
                                              input.semanticTypes, annotation);
  }
  return zc::none;
}

bool isMutableOwnerLocal(const driver::module_graph_query::CheckerBoundModuleView& boundModule,
                         ast::NodeId reference) {
  const auto binding = resolvedOwnerLocal(boundModule.bindings(), reference);
  if (binding == zc::none) return false;
  const auto& tree = boundModule.tree();
  for (const auto& local : boundModule.definitions().ownerLocalBindings()) {
    if (local.binding != binding || !local.site.value().is<binder::PatternBindingSite>()) {
      continue;
    }
    const auto& site = local.site.value().get<binder::PatternBindingSite>();
    if (!tree.contains(site.introducer) ||
        tree.node(site.introducer).kind != ast::SyntaxKind::VariableDeclarator) {
      return false;
    }
    bool mutableDeclaration = false;
    ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId node, const ast::Node& syntax) {
      if (syntax.kind != ast::SyntaxKind::LetStmt) return;
      const ast::NodeId declarations(syntax.payload.words[ast::kLetStmtDeclarationsWord]);
      if (!tree.contains(declarations) || !subtreeContains(tree, declarations, site.introducer)) {
        return;
      }
      mutableDeclaration =
          static_cast<ast::BindingDeclarationKind>(syntax.payload.words[ast::kLetStmtKindWord]) ==
          ast::BindingDeclarationKind::Mut;
    });
    return mutableDeclaration;
  }
  return false;
}

bool isSimpleLocalWrite(const driver::module_graph_query::CheckerBoundModuleView& boundModule,
                        ast::NodeId assignment) {
  const auto& tree = boundModule.tree();
  if (!tree.contains(assignment)) return false;
  const auto& syntax = tree.node(assignment);
  if (syntax.kind != ast::SyntaxKind::AssignmentExpr ||
      static_cast<ast::AssignmentOperatorKind>(syntax.payload.words[ast::kAssignmentExprOpWord]) !=
          ast::AssignmentOperatorKind::Assign) {
    return false;
  }
  const ast::NodeId target(syntax.payload.words[ast::kAssignmentExprLhsWord]);
  const ast::NodeId value(syntax.payload.words[ast::kAssignmentExprRhsWord]);
  if (!tree.contains(target) || !tree.contains(value) ||
      tree.node(target).kind != ast::SyntaxKind::IdentExpr) {
    return false;
  }
  const auto valueKind = tree.node(value).kind;
  const bool scalar =
      valueKind == ast::SyntaxKind::NullLiteral || valueKind == ast::SyntaxKind::BoolLiteral ||
      valueKind == ast::SyntaxKind::IntLiteral || valueKind == ast::SyntaxKind::FloatLiteralExpr ||
      valueKind == ast::SyntaxKind::BigIntLiteral ||
      valueKind == ast::SyntaxKind::StringLiteralExpr ||
      valueKind == ast::SyntaxKind::UnitLiteral ||
      valueKind == ast::SyntaxKind::CharacterLiteralExpr ||
      valueKind == ast::SyntaxKind::NoSubstitutionTemplateLiteralExpr;
  return scalar && isMutableOwnerLocal(boundModule, target);
}

bool isSimpleOwnerLocalFieldWrite(
    const driver::module_graph_query::CheckerBoundModuleView& boundModule, ast::NodeId assignment) {
  const auto& tree = boundModule.tree();
  if (!tree.contains(assignment)) return false;
  const auto& syntax = tree.node(assignment);
  if (syntax.kind != ast::SyntaxKind::AssignmentExpr ||
      static_cast<ast::AssignmentOperatorKind>(syntax.payload.words[ast::kAssignmentExprOpWord]) !=
          ast::AssignmentOperatorKind::Assign) {
    return false;
  }
  const ast::NodeId target(syntax.payload.words[ast::kAssignmentExprLhsWord]);
  const ast::NodeId value(syntax.payload.words[ast::kAssignmentExprRhsWord]);
  if (!tree.contains(target) || !tree.contains(value) ||
      tree.node(target).kind != ast::SyntaxKind::MemberExpression) {
    return false;
  }
  const auto& member = tree.node(target);
  const ast::NodeId object(member.payload.words[ast::kMemberExpressionObjectWord]);
  if (!tree.contains(object) || tree.node(object).kind != ast::SyntaxKind::IdentExpr) {
    return false;
  }
  const auto valueKind = tree.node(value).kind;
  const bool scalar =
      valueKind == ast::SyntaxKind::NullLiteral || valueKind == ast::SyntaxKind::BoolLiteral ||
      valueKind == ast::SyntaxKind::IntLiteral || valueKind == ast::SyntaxKind::FloatLiteralExpr ||
      valueKind == ast::SyntaxKind::BigIntLiteral ||
      valueKind == ast::SyntaxKind::StringLiteralExpr ||
      valueKind == ast::SyntaxKind::UnitLiteral ||
      valueKind == ast::SyntaxKind::CharacterLiteralExpr ||
      valueKind == ast::SyntaxKind::NoSubstitutionTemplateLiteralExpr;
  return scalar && isMutableOwnerLocal(boundModule, object);
}

struct OwnerLocalFieldShape final {
  binder::OwnerLocalBindingId binding;
  identity::SemanticTypeId receiverType;
  identity::DefId field;
  identity::SemanticTypeId fieldType;
  bool mutablePlace;
};

struct NominalFieldShape final {
  identity::DefId definition;
  identity::SemanticTypeId type;
  bool mutableField;
};

zc::Maybe<NominalFieldShape> nominalFieldShape(const BodyCheckingInput& input,
                                               identity::DefId nominalDefinition,
                                               zc::StringPtr propertyName) {
  zc::Maybe<identity::DefId> field;
  for (const auto& signature : input.signatureFacts.signatures()) {
    if (signature.definition != nominalDefinition ||
        !signature.payload.variant().is<signature::NominalSignature>()) {
      continue;
    }
    const auto& nominal = signature.payload.variant().get<signature::NominalSignature>();
    if (nominal.genericParameters.size() != 0) return zc::none;
    for (const auto candidate : nominal.fields) {
      for (const auto& definition : input.boundModule.definitions().definitions()) {
        if (definition.definition != candidate || definition.record.name() != propertyName) {
          continue;
        }
        if (field != zc::none) return zc::none;
        field = candidate;
      }
    }
  }
  if (field == zc::none) return zc::none;

  zc::Maybe<identity::SemanticTypeId> fieldType;
  bool fieldMutable = false;
  for (const auto& signature : input.signatureFacts.signatures()) {
    if (signature.definition != ZC_ASSERT_NONNULL(field) ||
        !signature.payload.variant().is<signature::ValueSignature>() ||
        !signature.scope.variant().is<signature::MemberSignatureScope>()) {
      continue;
    }
    const auto& scope = signature.scope.variant().get<signature::MemberSignatureScope>();
    if (scope.owner != nominalDefinition || fieldType != zc::none) return zc::none;
    const auto& value = signature.payload.variant().get<signature::ValueSignature>();
    fieldType = value.type;
    fieldMutable = value.mutability == signature::Mutability::Mutable;
  }
  ZC_IF_SOME(definition, field) {
    ZC_IF_SOME(type, fieldType) { return NominalFieldShape{definition, type, fieldMutable}; }
  }
  return zc::none;
}

zc::Maybe<OwnerLocalFieldShape> ownerLocalFieldShape(
    const BodyCheckingInput& input, ast::NodeId node,
    zc::ArrayPtr<const checked::NodeTypeMap::Entry> nodeTypes) {
  const auto& tree = input.boundModule.tree();
  if (!tree.contains(node) || tree.node(node).kind != ast::SyntaxKind::MemberExpression) {
    return zc::none;
  }
  const auto& member = tree.node(node);
  if (static_cast<ast::MemberAccessKind>(member.payload.words[ast::kMemberExpressionAccessWord]) !=
      ast::MemberAccessKind::Dot) {
    return zc::none;
  }
  const ast::NodeId object(member.payload.words[ast::kMemberExpressionObjectWord]);
  if (!tree.contains(object) || tree.node(object).kind != ast::SyntaxKind::IdentExpr) {
    return zc::none;
  }
  const auto binding = resolvedOwnerLocal(input.boundModule.bindings(), object);
  const auto receiverType = ownerLocalReferenceType(input, object, nodeTypes);
  if (binding == zc::none || receiverType == zc::none) return zc::none;

  auto lookup = input.semanticTypes.get(ZC_ASSERT_NONNULL(receiverType));
  if (!lookup.is<type::SemanticTypeLookup>()) return zc::none;
  const auto& typeData = lookup.get<type::SemanticTypeLookup>().data();
  if (!typeData.is<type::semantic::NominalTypeData>()) return zc::none;
  const auto& nominalType = typeData.get<type::semantic::NominalTypeData>();
  if (nominalType.arguments.size() != 0) return zc::none;

  auto field = nominalFieldShape(
      input, nominalType.definition,
      tree.ident(ast::IdentId(member.payload.words[ast::kMemberExpressionPropertyWord])));
  if (field == zc::none) return zc::none;
  ZC_IF_SOME(local, binding) {
    ZC_IF_SOME(type, receiverType) {
      ZC_IF_SOME(member, field) {
        return OwnerLocalFieldShape{
            local, type, member.definition, member.type,
            member.mutableField && isMutableOwnerLocal(input.boundModule, object)};
      }
    }
  }
  ZC_UNREACHABLE
}

struct StructLiteralShape final {
  identity::DefId definition;
  identity::SemanticTypeId type;
  zc::Vector<checked::AggregateElementFact> elements;
};

zc::Maybe<StructLiteralShape> structLiteralShape(
    const BodyCheckingInput& input, ast::NodeId node,
    zc::ArrayPtr<const checked::NodeTypeMap::Entry> nodeTypes) {
  const auto& tree = input.boundModule.tree();
  if (!tree.contains(node) || tree.node(node).kind != ast::SyntaxKind::StructLiteralExpr) {
    return zc::none;
  }
  const auto& literal = tree.node(node);
  const ast::NodeId typeSyntax(literal.payload.words[ast::kStructLiteralExprTyWord]);
  const ast::NodeList properties{literal.payload.words[ast::kStructLiteralExprPropertiesFirstWord],
                                 literal.payload.words[ast::kStructLiteralExprPropertiesSizeWord]};
  if (!tree.contains(typeSyntax) || !tree.contains(properties)) return zc::none;
  const auto type = signature::resolveClosedSourceType(input.boundModule, input.identities,
                                                       input.semanticTypes, typeSyntax);
  if (type == zc::none) return zc::none;
  auto lookup = input.semanticTypes.get(ZC_ASSERT_NONNULL(type));
  if (!lookup.is<type::SemanticTypeLookup>()) return zc::none;
  const auto& typeData = lookup.get<type::SemanticTypeLookup>().data();
  if (!typeData.is<type::semantic::NominalTypeData>()) return zc::none;
  const auto& nominal = typeData.get<type::semantic::NominalTypeData>();
  if (nominal.arguments.size() != 0) return zc::none;

  zc::Vector<checked::AggregateElementFact> elements(properties.size);
  for (uint32_t index = 0; index < properties.size; ++index) {
    const auto property = tree.list(properties)[index];
    if (!tree.contains(property) || tree.node(property).kind != ast::SyntaxKind::ObjectProperty) {
      return zc::none;
    }
    const auto& syntax = tree.node(property);
    if (syntax.payload.words[ast::kObjectPropertyShortFormWord] != 0) return zc::none;
    const ast::NodeId value(syntax.payload.words[ast::kObjectPropertyValueWord]);
    if (!tree.contains(value)) return zc::none;
    const auto field = nominalFieldShape(
        input, nominal.definition,
        tree.ident(ast::IdentId(syntax.payload.words[ast::kObjectPropertyNameWord])));
    zc::Maybe<const checked::NodeTypeMap::Entry&> valueType;
    for (const auto& entry : nodeTypes) {
      if (entry.key != value) continue;
      if (valueType != zc::none) return zc::none;
      valueType = entry;
    }
    if (field == zc::none || valueType == zc::none) return zc::none;
    ZC_IF_SOME(member, field) {
      ZC_IF_SOME(sourceType, valueType) {
        if (sourceType.value != member.type) return zc::none;
        zc::Maybe<identity::DefId> fieldDefinition = member.definition;
        zc::Maybe<checked::CoercionAdjustment> noAdjustment;
        elements.add(checked::AggregateElementFact{value, zc::mv(fieldDefinition), index,
                                                   sourceType.value, member.type,
                                                   zc::mv(noAdjustment)});
      }
    }
  }
  if (elements.size() != properties.size) return zc::none;
  ZC_IF_SOME(value, type) {
    return StructLiteralShape{nominal.definition, value, zc::mv(elements)};
  }
  ZC_UNREACHABLE
}

// Forward declaration: scalar-literal classification is defined below but is
// consulted by the primitive-comparison shape helper above it.
bool isScalarLiteral(ast::SyntaxKind kind) noexcept;

zc::Maybe<identity::SemanticTypeId> callableParameterReferenceType(const BodyCheckingInput& input,
                                                                   ast::NodeId node) {
  auto parameter = resolvedCallableParameter(input.boundModule.bindings(), node);
  if (parameter == zc::none) return zc::none;
  ZC_IF_SOME(handle, parameter) {
    auto authority = input.identities.callableParameter(handle);
    if (authority == zc::none) return zc::none;
    ZC_IF_SOME(entry, authority) {
      zc::Maybe<identity::SemanticTypeId> result;
      for (const auto& signature : input.signatureFacts.signatures()) {
        if (!signature.payload.variant().is<signature::CallableSignature>()) continue;
        const auto& callable = signature.payload.variant().get<signature::CallableSignature>();
        for (const auto& candidate : callable.parameters) {
          if (candidate.parameter != entry.key()) continue;
          if (result != zc::none) return zc::none;
          result = candidate.type;
        }
      }
      return result;
    }
  }
  return zc::none;
}

bool isIntegerIndexType(const type::SemanticTypeStore& semanticTypes,
                        identity::SemanticTypeId type) {
  auto lookup = semanticTypes.get(type);
  if (!lookup.is<type::SemanticTypeLookup>()) return false;
  const auto& data = lookup.get<type::SemanticTypeLookup>().data();
  if (!data.is<type::semantic::PrimitiveTypeData>()) return false;
  const auto kind = data.get<type::semantic::PrimitiveTypeData>().kind;
  return kind >= type::semantic::PrimitiveKind::I8 && kind <= type::semantic::PrimitiveKind::Usize;
}

struct ReadIndexShape final {
  identity::CallableParameterId base;
  ast::NodeId baseNode;
  identity::SemanticTypeId collectionType;
  ast::NodeId indexNode;
  identity::SemanticTypeId indexType;
  identity::SemanticTypeId elementType;
};

zc::Maybe<ReadIndexShape> readIndexShape(
    const BodyCheckingInput& input, ast::NodeId node,
    zc::ArrayPtr<const checked::NodeTypeMap::Entry> nodeTypes) {
  const auto& tree = input.boundModule.tree();
  if (!tree.contains(node) || tree.node(node).kind != ast::SyntaxKind::IndexExpression) {
    return zc::none;
  }
  const auto& syntax = tree.node(node);
  const ast::NodeId base(syntax.payload.words[ast::kIndexExpressionObjectWord]);
  const ast::NodeId index(syntax.payload.words[ast::kIndexExpressionIndexWord]);
  if (!tree.contains(base) || !tree.contains(index) ||
      tree.node(base).kind != ast::SyntaxKind::IdentExpr ||
      tree.node(index).kind != ast::SyntaxKind::IntLiteral) {
    return zc::none;
  }
  auto parameter = resolvedCallableParameter(input.boundModule.bindings(), base);
  auto collection = callableParameterReferenceType(input, base);
  zc::Maybe<const checked::NodeTypeMap::Entry&> baseType;
  zc::Maybe<const checked::NodeTypeMap::Entry&> indexType;
  for (const auto& entry : nodeTypes) {
    if (entry.key == base) baseType = entry;
    if (entry.key == index) indexType = entry;
  }
  if (parameter == zc::none || collection == zc::none || baseType == zc::none ||
      indexType == zc::none) {
    return zc::none;
  }
  ZC_IF_SOME(baseValue, baseType) {
    ZC_IF_SOME(collectionValue, collection) {
      if (baseValue.value != collectionValue) return zc::none;
      ZC_IF_SOME(indexValue, indexType) {
        if (!isIntegerIndexType(input.semanticTypes, indexValue.value)) return zc::none;
        auto lookup = input.semanticTypes.get(collectionValue);
        if (!lookup.is<type::SemanticTypeLookup>()) return zc::none;
        const auto& data = lookup.get<type::SemanticTypeLookup>().data();
        zc::Maybe<identity::SemanticTypeId> element;
        if (data.is<type::semantic::DynamicArrayTypeData>()) {
          element = data.get<type::semantic::DynamicArrayTypeData>().element;
        } else if (data.is<type::semantic::SliceTypeData>()) {
          element = data.get<type::semantic::SliceTypeData>().element;
        } else if (data.is<type::semantic::FixedArrayTypeData>()) {
          element = data.get<type::semantic::FixedArrayTypeData>().element;
        }
        ZC_IF_SOME(elementValue, element) {
          ZC_IF_SOME(parameterValue, parameter) {
            return ReadIndexShape{parameterValue,   base,        collectionValue, index,
                                  indexValue.value, elementValue};
          }
        }
      }
    }
  }
  return zc::none;
}

zc::Maybe<checked::MarkerEvidence> copyEvidence(const BodyCheckingInput& input,
                                                identity::SemanticTypeId subject) {
  auto proofInput = marker::MarkerProofInput::from(input);
  if (proofInput == zc::none) return zc::none;
  ZC_IF_SOME(value, proofInput) {
    marker::MarkerProofEngine engine(zc::mv(value));
    auto proof = engine.prove(input.standardMarkers.copy(), subject);
    if (!proof.is<marker::MarkerProofPositive>()) return zc::none;
    return zc::mv(proof).get<marker::MarkerProofPositive>().proof.evidence;
  }
  ZC_UNREACHABLE
}

/// \brief Shape of a primitive binary operation between two same-typed scalar
/// operands.
///
/// Each operand is a callable-parameter reference or a scalar literal of the
/// same primitive scalar type, with at least one parameter. `operation` is one
/// of the six relational comparisons (result type bool) or one of the twelve
/// arithmetic/bitwise operators (result type equal to `operandType`). Anything
/// else leaves the BinaryExpr production unsupported so the existing rejection
/// stands.
struct PrimitiveBinaryOperationShape final {
  ast::NodeId leftNode;
  ast::NodeId rightNode;
  identity::SemanticTypeId operandType;
  identity::SemanticTypeId resultType;
  PrimitiveOperation operation;
};

/// \brief Projects a binary operator to its primitive operation when it is one
/// of the six relational comparisons of same-typed scalars.
///
/// Reuses `OperatorKind::fromBinary` so the operator mapping lives in exactly
/// one place. Strict identity (`===` / `!==`) and every arithmetic, bitwise, or
/// logical operator return none so their existing rejection stands.
zc::Maybe<PrimitiveOperation> scalarComparisonOperation(ast::BinaryOperatorKind syntax) {
  ZC_IF_SOME(kind, OperatorKind::fromBinary(syntax)) {
    const auto& variant = kind.variant();
    if (!variant.is<PrimitiveOperation>()) return zc::none;
    switch (variant.get<PrimitiveOperation>()) {
      case PrimitiveOperation::Eq:
      case PrimitiveOperation::Ne:
      case PrimitiveOperation::Lt:
      case PrimitiveOperation::Le:
      case PrimitiveOperation::Gt:
      case PrimitiveOperation::Ge:
        return variant.get<PrimitiveOperation>();
      default:
        return zc::none;
    }
  }
  return zc::none;
}

/// \brief Projects a binary operator to its primitive operation when it is one
/// of the twelve arithmetic or bitwise operators of same-typed scalars.
///
/// Reuses `OperatorKind::fromBinary` so the operator mapping lives in exactly
/// one place. The six relational comparisons (handled by
/// `scalarComparisonOperation`), strict identity, and the logical short-circuit
/// operators (`&&` / `||`) return none so their existing handling stands. Unlike
/// a comparison, the result type of these operators is the operand type, not
/// bool.
zc::Maybe<PrimitiveOperation> scalarArithmeticOperation(ast::BinaryOperatorKind syntax) {
  ZC_IF_SOME(kind, OperatorKind::fromBinary(syntax)) {
    const auto& variant = kind.variant();
    if (!variant.is<PrimitiveOperation>()) return zc::none;
    switch (variant.get<PrimitiveOperation>()) {
      case PrimitiveOperation::Add:
      case PrimitiveOperation::Sub:
      case PrimitiveOperation::Mul:
      case PrimitiveOperation::Div:
      case PrimitiveOperation::Rem:
      case PrimitiveOperation::Pow:
      case PrimitiveOperation::Shl:
      case PrimitiveOperation::Shr:
      case PrimitiveOperation::UShr:
      case PrimitiveOperation::BitAnd:
      case PrimitiveOperation::BitOr:
      case PrimitiveOperation::BitXor:
        return variant.get<PrimitiveOperation>();
      default:
        return zc::none;
    }
  }
  return zc::none;
}

/// \brief True when the node is the condition of an enclosing `if` or `while`
/// statement. An arithmetic result is not bool, so it is not lowerable as a
/// condition and stays unsupported there; a comparison result is bool and is
/// lowerable in both positions.
bool isConditionPosition(const driver::module_graph_query::CheckerBoundModuleView& boundModule,
                         ast::NodeId node) {
  const auto& tree = boundModule.tree();
  bool isCondition = false;
  ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId, const ast::Node& syntax) {
    if (syntax.kind == ast::SyntaxKind::IfStmt &&
        ast::NodeId(syntax.payload.words[ast::kIfStmtCondWord]) == node) {
      isCondition = true;
    }
    if (syntax.kind == ast::SyntaxKind::WhileStmt &&
        ast::NodeId(syntax.payload.words[ast::kWhileStmtCondWord]) == node) {
      isCondition = true;
    }
  });
  return isCondition;
}

/// \brief Returns true only for primitive scalar types eligible for `Eq`.
bool isPrimitiveScalarType(const type::SemanticTypeStore& semanticTypes,
                           identity::SemanticTypeId type) {
  auto lookup = semanticTypes.get(type);
  if (!lookup.is<type::SemanticTypeLookup>()) return false;
  const auto& data = lookup.get<type::SemanticTypeLookup>().data();
  if (!data.is<type::semantic::PrimitiveTypeData>()) return false;
  const auto kind = data.get<type::semantic::PrimitiveTypeData>().kind;
  switch (kind) {
    case type::semantic::PrimitiveKind::I8:
    case type::semantic::PrimitiveKind::I16:
    case type::semantic::PrimitiveKind::I32:
    case type::semantic::PrimitiveKind::I64:
    case type::semantic::PrimitiveKind::U8:
    case type::semantic::PrimitiveKind::U16:
    case type::semantic::PrimitiveKind::U32:
    case type::semantic::PrimitiveKind::U64:
    case type::semantic::PrimitiveKind::Isize:
    case type::semantic::PrimitiveKind::Usize:
    case type::semantic::PrimitiveKind::F32:
    case type::semantic::PrimitiveKind::F64:
    case type::semantic::PrimitiveKind::Bool:
    case type::semantic::PrimitiveKind::Char:
      return true;
    default:
      return false;
  }
}

zc::Maybe<PrimitiveBinaryOperationShape> primitiveBinaryOperationShape(
    const BodyCheckingInput& input, ast::NodeId node,
    zc::ArrayPtr<const checked::NodeTypeMap::Entry> nodeTypes,
    zc::ArrayPtr<const checked::LiteralFactMap::Entry> literals) {
  const auto& tree = input.boundModule.tree();
  if (!tree.contains(node) || tree.node(node).kind != ast::SyntaxKind::BinaryExpr) return zc::none;
  const auto& syntax = tree.node(node);
  // A comparison result is bool; an arithmetic or bitwise result is the operand
  // type. A comparison is lowerable in any position; an arithmetic operation is
  // lowerable only outside a condition, since a non-bool value cannot drive an
  // `if` / `while` discriminant. Strict identity and the logical short-circuit
  // operators stay unsupported so their existing rejection stands.
  const auto binaryOperator =
      static_cast<ast::BinaryOperatorKind>(syntax.payload.words[ast::kBinaryExprOpWord]);
  auto comparison = scalarComparisonOperation(binaryOperator);
  auto arithmetic = scalarArithmeticOperation(binaryOperator);
  const bool isArithmetic = comparison == zc::none && arithmetic != zc::none;
  auto operation = comparison != zc::none ? comparison : arithmetic;
  if (operation == zc::none) return zc::none;
  if (isArithmetic && isConditionPosition(input.boundModule, node)) return zc::none;
  const ast::NodeId left(syntax.payload.words[ast::kBinaryExprLhsWord]);
  const ast::NodeId right(syntax.payload.words[ast::kBinaryExprRhsWord]);
  if (!tree.contains(left) || !tree.contains(right)) return zc::none;
  // Each operand is either a value reference (an IdentExpr resolving to a
  // callable parameter or an owner local) or a scalar literal. At least one must
  // be a reference; a literal-vs-literal operation has no place to lower. The
  // owner-local operand form supports a primitive binary as a local initializer
  // whose operand is an earlier local (e.g. `let y: T = x * b`).
  auto referenceType = [&](ast::NodeId operandNode) -> zc::Maybe<identity::SemanticTypeId> {
    if (!tree.contains(operandNode) || tree.node(operandNode).kind != ast::SyntaxKind::IdentExpr) {
      return zc::none;
    }
    auto parameter = callableParameterReferenceType(input, operandNode);
    if (parameter != zc::none) return parameter;
    return ownerLocalReferenceType(input, operandNode, nodeTypes);
  };
  // A nested operand is itself a one-level primitive binary (`a + b * c`). Its
  // result type is derived structurally from a reference operand (which resolves
  // independent of node-type facts) so the outer shape can agree types even
  // though the inner binary's own node-type fact is produced later in schema
  // preorder. The inner binary's own production site validates it fully and
  // fails closed on its own; two-level nesting is rejected because a nested
  // operand's operands must each be a reference or a scalar literal, never
  // another binary.
  auto nestedBinaryResultType =
      [&](ast::NodeId operandNode) -> zc::Maybe<identity::SemanticTypeId> {
    if (!tree.contains(operandNode) || tree.node(operandNode).kind != ast::SyntaxKind::BinaryExpr) {
      return zc::none;
    }
    const auto& inner = tree.node(operandNode);
    const auto innerOperator =
        static_cast<ast::BinaryOperatorKind>(inner.payload.words[ast::kBinaryExprOpWord]);
    const auto innerComparison = scalarComparisonOperation(innerOperator);
    const auto innerArithmetic = scalarArithmeticOperation(innerOperator);
    const bool innerIsArithmetic = innerComparison == zc::none && innerArithmetic != zc::none;
    if (innerComparison == zc::none && innerArithmetic == zc::none) return zc::none;
    const ast::NodeId innerLeft(inner.payload.words[ast::kBinaryExprLhsWord]);
    const ast::NodeId innerRight(inner.payload.words[ast::kBinaryExprRhsWord]);
    if (!tree.contains(innerLeft) || !tree.contains(innerRight)) return zc::none;
    const bool innerLeftIsReference = tree.node(innerLeft).kind == ast::SyntaxKind::IdentExpr &&
                                      referenceType(innerLeft) != zc::none;
    const bool innerRightIsReference = tree.node(innerRight).kind == ast::SyntaxKind::IdentExpr &&
                                       referenceType(innerRight) != zc::none;
    const bool innerLeftIsLiteral = isScalarLiteral(tree.node(innerLeft).kind);
    const bool innerRightIsLiteral = isScalarLiteral(tree.node(innerRight).kind);
    // One-level bound: each inner operand is a reference or a scalar literal, with
    // at least one reference; a nested inner operand keeps two-level nesting
    // unsupported.
    if ((!innerLeftIsReference && !innerLeftIsLiteral) ||
        (!innerRightIsReference && !innerRightIsLiteral) ||
        (!innerLeftIsReference && !innerRightIsReference)) {
      return zc::none;
    }
    zc::Maybe<identity::SemanticTypeId> innerOperandType;
    if (innerLeftIsReference) {
      innerOperandType = referenceType(innerLeft);
    } else if (innerRightIsReference) {
      innerOperandType = referenceType(innerRight);
    }
    if (innerOperandType == zc::none) return zc::none;
    identity::SemanticTypeId innerOperand;
    ZC_IF_SOME(value, innerOperandType) { innerOperand = value; }
    if (!isPrimitiveScalarType(input.semanticTypes, innerOperand)) return zc::none;
    if (!innerIsArithmetic) {
      auto canonical = input.semanticTypes.canonicalizeClosed(type::semantic::TypeData(
          type::semantic::PrimitiveTypeData{type::semantic::PrimitiveKind::Bool}));
      if (!canonical.is<type::semantic::CanonicalTypeData>()) return zc::none;
      auto interned =
          input.semanticTypes.intern(zc::mv(canonical).get<type::semantic::CanonicalTypeData>());
      if (!interned.is<type::SemanticTypeInterned>()) return zc::none;
      return interned.get<type::SemanticTypeInterned>().id;
    }
    return innerOperand;
  };
  const bool leftIsNested = tree.node(left).kind == ast::SyntaxKind::BinaryExpr;
  const bool rightIsNested = tree.node(right).kind == ast::SyntaxKind::BinaryExpr;
  // Exactly one operand may be nested in this slice; both operands nested stays
  // unsupported so its existing rejection stands.
  if (leftIsNested && rightIsNested) return zc::none;
  const bool leftIsReference =
      tree.node(left).kind == ast::SyntaxKind::IdentExpr && referenceType(left) != zc::none;
  const bool rightIsReference =
      tree.node(right).kind == ast::SyntaxKind::IdentExpr && referenceType(right) != zc::none;
  const bool leftIsLiteral = isScalarLiteral(tree.node(left).kind);
  const bool rightIsLiteral = isScalarLiteral(tree.node(right).kind);
  auto leftNestedType = leftIsNested ? nestedBinaryResultType(left) : zc::none;
  auto rightNestedType = rightIsNested ? nestedBinaryResultType(right) : zc::none;
  if (leftIsNested && leftNestedType == zc::none) return zc::none;
  if (rightIsNested && rightNestedType == zc::none) return zc::none;
  if ((!leftIsReference && !leftIsLiteral && !leftIsNested) ||
      (!rightIsReference && !rightIsLiteral && !rightIsNested) ||
      (!leftIsReference && !rightIsReference && !leftIsNested && !rightIsNested)) {
    return zc::none;
  }
  // Derive the shared operand type from a reference operand, or from a nested
  // operand's result type when no operand is a plain reference; both operands
  // must agree on this primitive scalar type.
  zc::Maybe<identity::SemanticTypeId> operandType;
  if (leftIsReference) {
    operandType = referenceType(left);
  } else if (rightIsReference) {
    operandType = referenceType(right);
  } else if (leftIsNested) {
    operandType = leftNestedType;
  } else if (rightIsNested) {
    operandType = rightNestedType;
  }
  if (operandType == zc::none) return zc::none;
  identity::SemanticTypeId operand;
  ZC_IF_SOME(value, operandType) { operand = value; }
  if (!isPrimitiveScalarType(input.semanticTypes, operand)) return zc::none;
  // A nested operand's result type must equal the shared operand type; a
  // comparison-under-arithmetic form (a bool inner feeding a non-bool parent) is
  // rejected here.
  if (leftIsNested) {
    bool nestedOk = false;
    ZC_IF_SOME(value, leftNestedType) { nestedOk = value == operand; }
    if (!nestedOk) return zc::none;
  }
  if (rightIsNested) {
    bool nestedOk = false;
    ZC_IF_SOME(value, rightNestedType) { nestedOk = value == operand; }
    if (!nestedOk) return zc::none;
  }
  // Cross-check one operand's node-type fact (and, for a literal operand, its
  // literal fact) against the shared operand type. The literal template mirrors
  // the call-argument literal validation. A nested operand is validated by its
  // own production site and only needs its result type to agree (checked above),
  // since its node-type fact is produced later in schema preorder.
  auto operandMatches = [&](ast::NodeId operandNode, bool isReference, bool isLiteral,
                            bool isNested) -> bool {
    if (isNested) return true;
    zc::Maybe<const checked::NodeTypeMap::Entry&> typeFact;
    for (const auto& entry : nodeTypes) {
      if (entry.key == operandNode) typeFact = entry;
    }
    if (typeFact == zc::none) return false;
    bool typeOk = false;
    ZC_IF_SOME(entry, typeFact) { typeOk = entry.value == operand; }
    if (!typeOk) return false;
    if (isReference) {
      auto resolved = referenceType(operandNode);
      bool referenceOk = false;
      ZC_IF_SOME(value, resolved) { referenceOk = value == operand; }
      return referenceOk;
    }
    if (!isLiteral) return false;
    zc::Maybe<const checked::LiteralFactMap::Entry&> literalFact;
    for (const auto& entry : literals) {
      if (entry.key == operandNode) literalFact = entry;
    }
    bool literalOk = false;
    ZC_IF_SOME(entry, literalFact) {
      literalOk = entry.value.node == operandNode && entry.value.type == operand;
    }
    return literalOk;
  };
  if (!operandMatches(left, leftIsReference, leftIsLiteral, leftIsNested) ||
      !operandMatches(right, rightIsReference, rightIsLiteral, rightIsNested)) {
    return zc::none;
  }
  // A comparison produces bool; an arithmetic or bitwise operation produces the
  // shared operand type.
  identity::SemanticTypeId resultType = operand;
  if (!isArithmetic) {
    auto canonical = input.semanticTypes.canonicalizeClosed(type::semantic::TypeData(
        type::semantic::PrimitiveTypeData{type::semantic::PrimitiveKind::Bool}));
    if (!canonical.is<type::semantic::CanonicalTypeData>()) return zc::none;
    auto interned =
        input.semanticTypes.intern(zc::mv(canonical).get<type::semantic::CanonicalTypeData>());
    if (!interned.is<type::SemanticTypeInterned>()) return zc::none;
    resultType = interned.get<type::SemanticTypeInterned>().id;
  }
  // When the binary is an owner-local initializer, its result type must match the
  // local's declared annotation type; a mismatch (e.g. `let x: bool = a + b`)
  // fails closed here.
  auto declaredType = ownerLocalInitializerDeclaredType(input, node);
  ZC_IF_SOME(declared, declaredType) {
    if (declared != resultType) return zc::none;
  }
  return PrimitiveBinaryOperationShape{left, right, operand, resultType,
                                       ZC_ASSERT_NONNULL(operation)};
}

struct ReferenceReborrowShape final {
  ast::NodeId dereference;
  ast::NodeId source;
  identity::SemanticTypeId sourceType;
  identity::SemanticTypeId referentType;
};

zc::Maybe<identity::SemanticTypeId> referenceSourceType(
    const BodyCheckingInput& input, ast::NodeId source,
    zc::ArrayPtr<const checked::NodeTypeMap::Entry> nodeTypes) {
  auto local = ownerLocalReferenceType(input, source, nodeTypes);
  if (local != zc::none) return local;
  return callableParameterReferenceType(input, source);
}

zc::Maybe<ReferenceReborrowShape> referenceReborrowShape(
    const BodyCheckingInput& input, ast::NodeId node,
    zc::ArrayPtr<const checked::NodeTypeMap::Entry> nodeTypes) {
  const auto& tree = input.boundModule.tree();
  if (!tree.contains(node) || tree.node(node).kind != ast::SyntaxKind::UnaryExpression) {
    return zc::none;
  }
  const auto operation = static_cast<ast::UnaryOperatorKind>(
      tree.node(node).payload.words[ast::kUnaryExpressionOpWord]);
  if (operation != ast::UnaryOperatorKind::Ref && operation != ast::UnaryOperatorKind::RefMut) {
    return zc::none;
  }
  const ast::NodeId dereference(tree.node(node).payload.words[ast::kUnaryExpressionOperandWord]);
  if (!tree.contains(dereference) ||
      tree.node(dereference).kind != ast::SyntaxKind::UnaryExpression ||
      static_cast<ast::UnaryOperatorKind>(
          tree.node(dereference).payload.words[ast::kUnaryExpressionOpWord]) !=
          ast::UnaryOperatorKind::Deref) {
    return zc::none;
  }
  const ast::NodeId source(tree.node(dereference).payload.words[ast::kUnaryExpressionOperandWord]);
  auto sourceType = referenceSourceType(input, source, nodeTypes);
  if (sourceType == zc::none) return zc::none;
  ZC_IF_SOME(type, sourceType) {
    auto lookup = input.semanticTypes.get(type);
    if (!lookup.is<type::SemanticTypeLookup>() ||
        !lookup.get<type::SemanticTypeLookup>().data().is<type::semantic::ReferenceTypeData>()) {
      return zc::none;
    }
    const auto& reference =
        lookup.get<type::SemanticTypeLookup>().data().get<type::semantic::ReferenceTypeData>();
    const auto expectedMutability = operation == ast::UnaryOperatorKind::Ref
                                        ? type::semantic::Mutability::Const
                                        : type::semantic::Mutability::Mutable;
    if (reference.mutability != expectedMutability) return zc::none;
    return ReferenceReborrowShape{dereference, source, type, reference.referent};
  }
  ZC_UNREACHABLE
}

struct LocalBorrowShape final {
  ast::NodeId source;
  identity::SemanticTypeId sourceType;
  identity::SemanticTypeId type;
  type::semantic::Mutability mutability;
};

zc::Maybe<LocalBorrowShape> localBorrowShape(
    const BodyCheckingInput& input, ast::NodeId node,
    zc::ArrayPtr<const checked::NodeTypeMap::Entry> nodeTypes) {
  const auto& tree = input.boundModule.tree();
  if (!tree.contains(node) || tree.node(node).kind != ast::SyntaxKind::UnaryExpression) {
    return zc::none;
  }
  const auto operation = static_cast<ast::UnaryOperatorKind>(
      tree.node(node).payload.words[ast::kUnaryExpressionOpWord]);
  if (operation != ast::UnaryOperatorKind::Ref && operation != ast::UnaryOperatorKind::RefMut) {
    return zc::none;
  }
  const ast::NodeId operand(tree.node(node).payload.words[ast::kUnaryExpressionOperandWord]);
  if (!tree.contains(operand) || tree.node(operand).kind != ast::SyntaxKind::IdentExpr ||
      resolvedOwnerLocal(input.boundModule.bindings(), operand) == zc::none) {
    return zc::none;
  }
  auto sourceType = ownerLocalReferenceType(input, operand, nodeTypes);
  if (sourceType == zc::none) return zc::none;
  const auto expectedMutability = operation == ast::UnaryOperatorKind::Ref
                                      ? type::semantic::Mutability::Const
                                      : type::semantic::Mutability::Mutable;
  auto canonical = input.semanticTypes.canonicalizeClosed(type::semantic::TypeData(
      type::semantic::ReferenceTypeData{expectedMutability, ZC_ASSERT_NONNULL(sourceType)}));
  if (!canonical.is<type::semantic::CanonicalTypeData>()) return zc::none;
  auto interned =
      input.semanticTypes.intern(zc::mv(canonical).get<type::semantic::CanonicalTypeData>());
  if (!interned.is<type::SemanticTypeInterned>()) return zc::none;
  return LocalBorrowShape{operand, ZC_ASSERT_NONNULL(sourceType),
                          interned.get<type::SemanticTypeInterned>().id, expectedMutability};
}

struct DirectCallableShape final {
  identity::DefId callee;
  identity::SemanticTypeId calleeType;
  identity::SemanticTypeId success;
  zc::Vector<identity::SemanticTypeId> parameters;
};

zc::Maybe<DirectCallableShape> directCallableShape(const BodyCheckingInput& input,
                                                   ast::NodeId calleeNode) {
  const auto callee = resolvedDefinition(input.boundModule.bindings(), calleeNode);
  if (callee == zc::none) return zc::none;

  zc::Maybe<identity::SemanticTypeId> success;
  zc::Vector<identity::SemanticTypeId> parameters;
  for (const auto& semanticSignature : input.signatureFacts.signatures()) {
    ZC_IF_SOME(definition, callee) {
      if (semanticSignature.definition != definition) continue;
      if (success != zc::none ||
          !semanticSignature.payload.variant().is<signature::CallableSignature>()) {
        return zc::none;
      }
      const auto& callable =
          semanticSignature.payload.variant().get<signature::CallableSignature>();
      if (callable.genericParameters.size() != 0 || callable.receiver != zc::none ||
          callable.raises != zc::none || callable.abi != zc::none) {
        return zc::none;
      }
      for (const auto& parameter : callable.parameters) {
        if (parameter.hasDefault) { return zc::none; }
        parameters.add(parameter.type);
      }
      success = callable.success;
    }
  }
  if (success == zc::none) return zc::none;

  zc::Vector<identity::SemanticTypeId> canonicalParameters;
  for (const auto parameter : parameters) canonicalParameters.add(parameter);
  zc::Maybe<identity::SemanticTypeId> noRaises;
  auto canonical = input.semanticTypes.canonicalizeClosed(
      type::semantic::TypeData(type::semantic::FunctionTypeData{
          zc::mv(canonicalParameters), ZC_ASSERT_NONNULL(success), zc::mv(noRaises)}));
  if (!canonical.is<type::semantic::CanonicalTypeData>()) return zc::none;
  auto interned =
      input.semanticTypes.intern(zc::mv(canonical).get<type::semantic::CanonicalTypeData>());
  if (!interned.is<type::SemanticTypeInterned>()) return zc::none;
  ZC_IF_SOME(definition, callee) {
    return DirectCallableShape{definition, interned.get<type::SemanticTypeInterned>().id,
                               ZC_ASSERT_NONNULL(success), zc::mv(parameters)};
  }
  ZC_UNREACHABLE
}

zc::Maybe<DirectCallableShape> directCallShape(const BodyCheckingInput& input,
                                               ast::NodeId callNode) {
  const auto& tree = input.boundModule.tree();
  if (!tree.contains(callNode)) return zc::none;
  const auto& call = tree.node(callNode);
  if (call.kind != ast::SyntaxKind::CallExpression) return zc::none;
  const ast::NodeId callee(call.payload.words[ast::kCallExpressionCalleeWord]);
  const ast::NodeList typeArguments{call.payload.words[ast::kCallExpressionTypeArgsFirstWord],
                                    call.payload.words[ast::kCallExpressionTypeArgsSizeWord]};
  const ast::NodeList arguments{call.payload.words[ast::kCallExpressionArgsFirstWord],
                                call.payload.words[ast::kCallExpressionArgsSizeWord]};
  if (!tree.contains(callee) || tree.node(callee).kind != ast::SyntaxKind::IdentExpr ||
      !tree.contains(typeArguments) || !tree.contains(arguments) || !typeArguments.empty()) {
    return zc::none;
  }
  auto shape = directCallableShape(input, callee);
  if (shape == zc::none) return zc::none;
  ZC_IF_SOME(value, shape) {
    if (arguments.size != value.parameters.size()) return zc::none;
  }
  return shape;
}

bool isMethodCallCallee(const ast::Tree& tree, ast::NodeId member) {
  bool found = false;
  ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId node, const ast::Node& syntax) {
    if (syntax.kind != ast::SyntaxKind::CallExpression) return;
    if (ast::NodeId(syntax.payload.words[ast::kCallExpressionCalleeWord]) == member) {
      found = true;
    }
  });
  return found;
}

struct ConcreteMethodCallShape final {
  ast::NodeId receiverNode;
  identity::DefId method;
  identity::SemanticTypeId receiverSourceType;
  identity::SemanticTypeId receiverParameterType;
  identity::SemanticTypeId calleeType;
  identity::SemanticTypeId success;
  zc::Vector<identity::SemanticTypeId> parameters;
};

zc::Maybe<ConcreteMethodCallShape> concreteMethodCallShape(
    const BodyCheckingInput& input, ast::NodeId callNode,
    zc::ArrayPtr<const checked::NodeTypeMap::Entry> nodeTypes) {
  const auto& tree = input.boundModule.tree();
  if (!tree.contains(callNode) || tree.node(callNode).kind != ast::SyntaxKind::CallExpression) {
    return zc::none;
  }
  const auto& call = tree.node(callNode);
  const ast::NodeId callee(call.payload.words[ast::kCallExpressionCalleeWord]);
  const ast::NodeList typeArguments{call.payload.words[ast::kCallExpressionTypeArgsFirstWord],
                                    call.payload.words[ast::kCallExpressionTypeArgsSizeWord]};
  const ast::NodeList arguments{call.payload.words[ast::kCallExpressionArgsFirstWord],
                                call.payload.words[ast::kCallExpressionArgsSizeWord]};
  if (!tree.contains(callee) || tree.node(callee).kind != ast::SyntaxKind::MemberExpression ||
      !tree.contains(typeArguments) || !typeArguments.empty() || !tree.contains(arguments)) {
    return zc::none;
  }
  const auto& member = tree.node(callee);
  if (static_cast<ast::MemberAccessKind>(member.payload.words[ast::kMemberExpressionAccessWord]) !=
      ast::MemberAccessKind::Dot) {
    return zc::none;
  }
  const ast::NodeId receiverNode(member.payload.words[ast::kMemberExpressionObjectWord]);
  if (!tree.contains(receiverNode) || tree.node(receiverNode).kind != ast::SyntaxKind::IdentExpr ||
      !isMutableOwnerLocal(input.boundModule, receiverNode)) {
    return zc::none;
  }
  const auto receiverSourceType = ownerLocalReferenceType(input, receiverNode, nodeTypes);
  if (receiverSourceType == zc::none) return zc::none;
  auto receiverLookup = input.semanticTypes.get(ZC_ASSERT_NONNULL(receiverSourceType));
  if (!receiverLookup.is<type::SemanticTypeLookup>() ||
      !receiverLookup.get<type::SemanticTypeLookup>()
           .data()
           .is<type::semantic::NominalTypeData>()) {
    return zc::none;
  }
  const auto& nominal =
      receiverLookup.get<type::SemanticTypeLookup>().data().get<type::semantic::NominalTypeData>();
  if (nominal.arguments.size() != 0) return zc::none;
  auto canonicalReceiver = input.semanticTypes.canonicalizeClosed(
      type::semantic::TypeData(type::semantic::ReferenceTypeData{
          type::semantic::Mutability::Mutable, ZC_ASSERT_NONNULL(receiverSourceType)}));
  if (!canonicalReceiver.is<type::semantic::CanonicalTypeData>()) return zc::none;
  auto receiverParameter = input.semanticTypes.intern(
      zc::mv(canonicalReceiver).get<type::semantic::CanonicalTypeData>());
  if (!receiverParameter.is<type::SemanticTypeInterned>()) return zc::none;

  const auto memberName =
      tree.ident(ast::IdentId(member.payload.words[ast::kMemberExpressionPropertyWord]));
  zc::Maybe<identity::DefId> selected;
  zc::Maybe<identity::SemanticTypeId> success;
  zc::Vector<identity::SemanticTypeId> parameters;
  for (const auto& nominalSignature : input.signatureFacts.signatures()) {
    if (nominalSignature.definition != nominal.definition ||
        !nominalSignature.payload.variant().is<signature::NominalSignature>()) {
      continue;
    }
    const auto& nominalFacts =
        nominalSignature.payload.variant().get<signature::NominalSignature>();
    if (nominalFacts.genericParameters.size() != 0) return zc::none;
    for (const auto candidate : nominalFacts.members) {
      bool namedMethod = false;
      for (const auto& definition : input.boundModule.definitions().definitions()) {
        if (definition.definition == candidate &&
            definition.record.kind() == identity::DefinitionKind::Method &&
            definition.record.name() == memberName) {
          namedMethod = true;
        }
      }
      if (!namedMethod) continue;
      if (selected != zc::none) return zc::none;
      for (const auto& methodSignature : input.signatureFacts.signatures()) {
        if (methodSignature.definition != candidate ||
            !methodSignature.scope.variant().is<signature::MemberSignatureScope>() ||
            !methodSignature.payload.variant().is<signature::CallableSignature>()) {
          continue;
        }
        const auto& scope = methodSignature.scope.variant().get<signature::MemberSignatureScope>();
        const auto& callable =
            methodSignature.payload.variant().get<signature::CallableSignature>();
        if (scope.owner != nominal.definition || callable.genericParameters.size() != 0 ||
            callable.receiver == zc::none || callable.raises != zc::none ||
            callable.abi != zc::none) {
          return zc::none;
        }
        ZC_IF_SOME(receiver, callable.receiver) {
          if (receiver.mode != signature::ReceiverMode::Mutable) return zc::none;
        }
        for (const auto& parameter : callable.parameters) {
          if (parameter.hasDefault || parameter.mode != signature::ParameterMode::Value)
            return zc::none;
          parameters.add(parameter.type);
        }
        selected = candidate;
        success = callable.success;
      }
    }
  }
  if (selected == zc::none || success == zc::none || arguments.size != parameters.size()) {
    return zc::none;
  }
  bool deferredMember = false;
  for (const auto& fact : input.boundModule.bindings().deferredMembers()) {
    if (fact.node != callee || fact.base != receiverNode || fact.member.text() != memberName ||
        fact.expectedNamespaces.size() != 1 ||
        fact.expectedNamespaces[0] != binder::Namespace::Value ||
        fact.genericArguments.size() != 0 || deferredMember) {
      continue;
    }
    deferredMember = true;
  }
  if (!deferredMember) return zc::none;

  zc::Vector<identity::SemanticTypeId> canonicalParameters;
  for (const auto parameter : parameters) canonicalParameters.add(parameter);
  zc::Maybe<identity::SemanticTypeId> noRaises;
  auto canonical = input.semanticTypes.canonicalizeClosed(
      type::semantic::TypeData(type::semantic::FunctionTypeData{
          zc::mv(canonicalParameters), ZC_ASSERT_NONNULL(success), zc::mv(noRaises)}));
  if (!canonical.is<type::semantic::CanonicalTypeData>()) return zc::none;
  auto interned =
      input.semanticTypes.intern(zc::mv(canonical).get<type::semantic::CanonicalTypeData>());
  if (!interned.is<type::SemanticTypeInterned>()) return zc::none;
  ZC_IF_SOME(method, selected) {
    return ConcreteMethodCallShape{receiverNode,
                                   method,
                                   ZC_ASSERT_NONNULL(receiverSourceType),
                                   receiverParameter.get<type::SemanticTypeInterned>().id,
                                   interned.get<type::SemanticTypeInterned>().id,
                                   ZC_ASSERT_NONNULL(success),
                                   zc::mv(parameters)};
  }
  ZC_UNREACHABLE
}

zc::Maybe<uint32_t> definitionPreorder(
    const driver::module_graph_query::CheckerBoundModuleView& boundModule,
    identity::DefId definition) {
  ast::NodeId declaration;
  bool foundDefinition = false;
  for (const auto& candidate : boundModule.definitions().definitions()) {
    if (candidate.definition != definition) continue;
    if (foundDefinition) return zc::none;
    declaration = candidate.node;
    foundDefinition = true;
  }
  if (!foundDefinition) return zc::none;
  uint32_t preorder = 0;
  zc::Maybe<uint32_t> result;
  ast::visitTreePreOrder(boundModule.tree(), boundModule.tree().root(),
                         [&](ast::NodeId node, const ast::Node&) {
                           if (node == declaration) result = preorder;
                           ++preorder;
                         });
  return result;
}

checked::CheckedFactsSourceRejected rejectTypeMismatch(const BodyProductionSite& site,
                                                       uint32_t ownerPreorder,
                                                       identity::SemanticTypeId expected,
                                                       identity::SemanticTypeId actual) {
  zc::Maybe<identity::SemanticIdentifier> noExpectedAlias;
  zc::Maybe<identity::SemanticIdentifier> noActualAlias;
  zc::Vector<checked::CheckerDisplayArgument> arguments;
  arguments.add(
      checked::CheckerDisplayArgument(checked::TypeDisplayArg{expected, zc::mv(noExpectedAlias)}));
  arguments.add(
      checked::CheckerDisplayArgument(checked::TypeDisplayArg{actual, zc::mv(noActualAlias)}));
  zc::Vector<checked::CheckerNoteRef> notes;
  zc::Maybe<checked::TypeErrorId> noRecovery;
  zc::Vector<checked::CheckerFailureRef> failures;
  failures.add(checked::CheckerFailureRef{
      checked::CheckerErrorId::TypeCheckerTypeMismatch(), checked::CheckerDiagnosticStage::Body,
      site.node, site.key.sourceSpan.clone(), zc::mv(arguments), zc::mv(notes),
      checked::CheckerDiagnosticProducer::Inference,
      checked::CheckerRecoveryPolicy(
          checked::CreateRootRecoveryPolicy{checked::CheckerRecoveryClass::TypeMismatch, true}),
      checked::CheckerEmitterOrdinal{static_cast<uint8_t>(checked::CheckerDiagnosticStage::Body),
                                     ownerPreorder, site.key.schemaPreorder, 0},
      zc::mv(noRecovery)});
  return checked::CheckedFactsSourceRejected{zc::mv(failures),
                                             zc::Vector<checked::CheckerAdvisoryRef>(),
                                             zc::Vector<checked::FrozenRecoveryLedger>()};
}

checked::CheckedFactsSourceRejected rejectNonUnionErrorOperator(
    const BodyProductionSite& site, uint32_t ownerPreorder, identity::SemanticTypeId operandType,
    ast::PostfixOperatorKind operation) {
  zc::Maybe<identity::SemanticIdentifier> noAlias;
  zc::Vector<checked::CheckerDisplayArgument> arguments;
  arguments.add(
      checked::CheckerDisplayArgument(checked::TypeDisplayArg{operandType, zc::mv(noAlias)}));
  zc::Vector<checked::CheckerNoteRef> notes;
  zc::Maybe<checked::TypeErrorId> noRecovery;
  zc::Vector<checked::CheckerFailureRef> failures;
  const auto diagnostic = operation == ast::PostfixOperatorKind::ErrorPropagate
                              ? checked::CheckerErrorId::ErrorPropagateNonUnion()
                              : checked::CheckerErrorId::ErrorUnwrapNonUnion();
  failures.add(checked::CheckerFailureRef{
      diagnostic, checked::CheckerDiagnosticStage::Body, site.node, site.key.sourceSpan.clone(),
      zc::mv(arguments), zc::mv(notes), checked::CheckerDiagnosticProducer::ErrorOperator,
      checked::CheckerRecoveryPolicy(
          checked::CreateRootRecoveryPolicy{checked::CheckerRecoveryClass::InvalidOperation, true}),
      checked::CheckerEmitterOrdinal{static_cast<uint8_t>(checked::CheckerDiagnosticStage::Body),
                                     ownerPreorder, site.key.schemaPreorder, 0},
      zc::mv(noRecovery)});
  return checked::CheckedFactsSourceRejected{zc::mv(failures),
                                             zc::Vector<checked::CheckerAdvisoryRef>(),
                                             zc::Vector<checked::FrozenRecoveryLedger>()};
}

inference::RecoveryClass inferenceRecoveryClass(checked::CheckerRecoveryClass recovery) noexcept {
  switch (recovery) {
    case checked::CheckerRecoveryClass::TypeMismatch:
      return inference::RecoveryClass::TypeMismatch;
    case checked::CheckerRecoveryClass::InvalidOperation:
      return inference::RecoveryClass::InvalidOperation;
    case checked::CheckerRecoveryClass::InvalidTypeExpression:
      return inference::RecoveryClass::InvalidTypeExpression;
    case checked::CheckerRecoveryClass::FailedObligation:
      return inference::RecoveryClass::FailedObligation;
    case checked::CheckerRecoveryClass::FailedProjection:
      return inference::RecoveryClass::FailedProjection;
    case checked::CheckerRecoveryClass::FailedInference:
      return inference::RecoveryClass::FailedInference;
  }
  ZC_UNREACHABLE;
}

zc::OneOf<checked::CheckedFactsSourceRejected, checked::CheckedFactsInvariantRejected>
attachRecoveryLedger(checked::CheckedFactsSourceRejected&& rejection,
                     const BodyCheckingInput& input,
                     const identity::RegistryBrandIssuer& factStoreBrands) {
  if (rejection.failures.size() != 1 || rejection.recoveryLedgers.size() != 0) {
    return rejectInvariant(signature::CheckerInvariantKind::InferenceLifecycle,
                           input.boundModule.module(), 0);
  }
  auto& failure = rejection.failures[0];
  auto initializer = initializerOwner(input.boundModule, failure.primaryNode);
  auto callable = returnValueOwner(input.boundModule, failure.primaryNode);
  if ((initializer == zc::none) == (callable == zc::none) ||
      !failure.recoveryPolicy.variant().is<checked::CreateRootRecoveryPolicy>()) {
    return rejectInvariant(signature::CheckerInvariantKind::InferenceLifecycle,
                           input.boundModule.module(), failure.emitterOrdinal.siteSchemaPreorder,
                           zc::none, failure.primaryNode, failure.primarySpan.clone());
  }
  const auto& recoveryPolicy =
      failure.recoveryPolicy.variant().get<checked::CreateRootRecoveryPolicy>();

  identity::DefId ownerDefinition;
  bool initializerOwnerKind = false;
  ZC_IF_SOME(definition, initializer) {
    ownerDefinition = definition;
    initializerOwnerKind = true;
  }
  ZC_IF_SOME(definition, callable) { ownerDefinition = definition; }
  auto created = inference::InferenceRecoveryContext::create(
      input.identities, factStoreBrands, input.boundModule.parsedModule().source(),
      initializerOwnerKind ? inference::InferenceOwner::initializer(ownerDefinition)
                           : inference::InferenceOwner::callableBody(ownerDefinition));
  if (created.is<inference::InferenceRecoveryRejected>()) {
    return rejectRecoveryInvariant(zc::mv(created).get<inference::InferenceRecoveryRejected>());
  }
  auto context = zc::mv(created).get<zc::Own<inference::InferenceRecoveryContext>>();
  auto issued = context->issueRoot(failure.emitterOrdinal, failure.primaryNode, failure.primarySpan,
                                   inferenceRecoveryClass(recoveryPolicy.recoveryClass));
  if (issued.is<inference::InferenceRecoveryRejected>()) {
    return rejectRecoveryInvariant(zc::mv(issued).get<inference::InferenceRecoveryRejected>());
  }
  failure.recovery = zc::mv(issued).get<checked::TypeErrorId>();
  auto finished = context->finish();
  if (finished.is<inference::InferenceRecoveryRejected>()) {
    return rejectRecoveryInvariant(zc::mv(finished).get<inference::InferenceRecoveryRejected>());
  }
  if (!finished.is<inference::InferenceRecoveryRecovered>()) {
    return rejectInvariant(signature::CheckerInvariantKind::InferenceLifecycle,
                           input.boundModule.module(), failure.emitterOrdinal.siteSchemaPreorder);
  }
  rejection.recoveryLedgers.add(
      zc::mv(finished).get<inference::InferenceRecoveryRecovered>().ledger);
  return zc::mv(rejection);
}

zc::Vector<uint32_t> factPath(CheckedFactGroup group) {
  zc::Vector<uint32_t> path;
  path.add(static_cast<uint32_t>(group));
  return path;
}

bool isPattern(ast::SyntaxKind kind) noexcept {
  switch (kind) {
    case ast::SyntaxKind::RestPattern:
    case ast::SyntaxKind::LiteralPattern:
    case ast::SyntaxKind::IsPattern:
    case ast::SyntaxKind::WildcardPattern:
    case ast::SyntaxKind::BindingPattern:
    case ast::SyntaxKind::IdentifierPattern:
    case ast::SyntaxKind::TuplePattern:
    case ast::SyntaxKind::StructPattern:
    case ast::SyntaxKind::PatternProperty:
    case ast::SyntaxKind::ArrayPattern:
    case ast::SyntaxKind::ExpressionPattern:
    case ast::SyntaxKind::EnumPattern:
      return true;
    default:
      return false;
  }
}

bool isExpression(ast::SyntaxKind kind) noexcept {
  switch (kind) {
    case ast::SyntaxKind::UnsafeBlockExpr:
    case ast::SyntaxKind::ErrorDefaultExpr:
    case ast::SyntaxKind::NullCoalesceExpr:
    case ast::SyntaxKind::IsExpression:
    case ast::SyntaxKind::NullLiteral:
    case ast::SyntaxKind::BoolLiteral:
    case ast::SyntaxKind::IntLiteral:
    case ast::SyntaxKind::FloatLiteralExpr:
    case ast::SyntaxKind::BigIntLiteral:
    case ast::SyntaxKind::StringLiteralExpr:
    case ast::SyntaxKind::ArrayLiteral:
    case ast::SyntaxKind::TupleLiteral:
    case ast::SyntaxKind::UnitLiteral:
    case ast::SyntaxKind::CharacterLiteralExpr:
    case ast::SyntaxKind::NoSubstitutionTemplateLiteralExpr:
    case ast::SyntaxKind::ThisExpr:
    case ast::SyntaxKind::IdentExpr:
    case ast::SyntaxKind::CallExpression:
    case ast::SyntaxKind::BinaryExpr:
    case ast::SyntaxKind::ConditionalExpr:
    case ast::SyntaxKind::AssignmentExpr:
    case ast::SyntaxKind::CommaExpr:
    case ast::SyntaxKind::MemberExpression:
    case ast::SyntaxKind::IndexExpression:
    case ast::SyntaxKind::NewExpression:
    case ast::SyntaxKind::FunctionExpression:
    case ast::SyntaxKind::ImportCallExpression:
    case ast::SyntaxKind::ObjectLiteralExpr:
    case ast::SyntaxKind::TemplateLiteralExpr:
    case ast::SyntaxKind::TypeOfExpression:
    case ast::SyntaxKind::UnaryExpression:
    case ast::SyntaxKind::PostfixExpression:
    case ast::SyntaxKind::CastExpression:
    case ast::SyntaxKind::LambdaExpression:
    case ast::SyntaxKind::SpawnExpression:
    case ast::SyntaxKind::StructLiteralExpr:
    case ast::SyntaxKind::SuperExpr:
      return true;
    default:
      return false;
  }
}

bool isScalarLiteral(ast::SyntaxKind kind) noexcept {
  switch (kind) {
    case ast::SyntaxKind::NullLiteral:
    case ast::SyntaxKind::BoolLiteral:
    case ast::SyntaxKind::IntLiteral:
    case ast::SyntaxKind::FloatLiteralExpr:
    case ast::SyntaxKind::BigIntLiteral:
    case ast::SyntaxKind::StringLiteralExpr:
    case ast::SyntaxKind::UnitLiteral:
    case ast::SyntaxKind::CharacterLiteralExpr:
    case ast::SyntaxKind::NoSubstitutionTemplateLiteralExpr:
      return true;
    default:
      return false;
  }
}

void addNodeRequirement(zc::Vector<checked::NodeFactRequirement>& requirements,
                        CheckedFactGroup group, ast::NodeId node,
                        const checked::CheckedNodeKey& key) {
  requirements.add(checked::NodeFactRequirement{group, node, cloneNodeKey(key)});
}

bool hasCapture(zc::ArrayPtr<const checked::CaptureFactRequirement> requirements,
                const checked::CaptureKey& key) {
  for (const auto& requirement : requirements) {
    if (requirement.key == key) return true;
  }
  return false;
}

bool addOperatorRequirements(const ast::Node& syntax, ast::NodeId node,
                             const checked::CheckedNodeKey& key,
                             zc::Vector<checked::NodeFactRequirement>& requirements) {
  if (syntax.kind == ast::SyntaxKind::UnaryExpression) {
    auto operation = OperatorKind::fromUnary(
        static_cast<ast::UnaryOperatorKind>(syntax.payload.words[ast::kUnaryExpressionOpWord]));
    if (operation == zc::none) return false;
    if (static_cast<ast::UnaryOperatorKind>(syntax.payload.words[ast::kUnaryExpressionOpWord]) ==
            ast::UnaryOperatorKind::Ref ||
        static_cast<ast::UnaryOperatorKind>(syntax.payload.words[ast::kUnaryExpressionOpWord]) ==
            ast::UnaryOperatorKind::RefMut ||
        static_cast<ast::UnaryOperatorKind>(syntax.payload.words[ast::kUnaryExpressionOpWord]) ==
            ast::UnaryOperatorKind::Deref) {
      return true;
    }
    addNodeRequirement(requirements, CheckedFactGroup::Call, node, key);
    return true;
  }
  if (syntax.kind == ast::SyntaxKind::BinaryExpr) {
    auto operation = OperatorKind::fromBinary(
        static_cast<ast::BinaryOperatorKind>(syntax.payload.words[ast::kBinaryExprOpWord]));
    if (operation == zc::none) return false;
    addNodeRequirement(requirements, CheckedFactGroup::Call, node, key);
    return true;
  }
  if (syntax.kind == ast::SyntaxKind::PostfixExpression) {
    auto operation = OperatorKind::fromPostfix(
        static_cast<ast::PostfixOperatorKind>(syntax.payload.words[ast::kPostfixExpressionOpWord]));
    if (operation == zc::none) return false;
    ZC_IF_SOME(value, operation) {
      if (value.variant().is<checker::ErrorOperatorKind>()) {
        addNodeRequirement(requirements, CheckedFactGroup::ErrorUnionShape, node, key);
        addNodeRequirement(requirements, CheckedFactGroup::ErrorOperator, node, key);
      } else {
        addNodeRequirement(requirements, CheckedFactGroup::Call, node, key);
      }
    }
    return true;
  }
  if (syntax.kind == ast::SyntaxKind::AssignmentExpr) {
    auto operation = OperatorKind::fromAssignment(
        static_cast<ast::AssignmentOperatorKind>(syntax.payload.words[ast::kAssignmentExprOpWord]));
    if (operation == zc::none) return false;
    ZC_IF_SOME(value, operation) {
      if (value.variant().is<CompoundAssignmentOperation>()) {
        addNodeRequirement(requirements, CheckedFactGroup::CompoundAssignment, node, key);
      }
    }
    return true;
  }
  return false;
}

template <typename Map>
Map emptyFactMap() {
  zc::Vector<typename Map::Entry> entries;
  return Map::fromEntries(zc::mv(entries));
}

template <typename Entry, typename Key>
zc::Maybe<const Entry&> factEntry(zc::ArrayPtr<Entry> entries, const Key& key) {
  for (const auto& entry : entries) {
    if (entry.key == key) return entry;
  }
  return zc::none;
}

zc::Maybe<const BodyProductionSite&> productionSite(zc::ArrayPtr<const BodyProductionSite> sites,
                                                    ast::NodeId node) {
  for (const auto& site : sites) {
    if (site.node == node) return site;
  }
  return zc::none;
}

size_t requirementCount(zc::ArrayPtr<const checked::NodeFactRequirement> requirements,
                        CheckedFactGroup group) noexcept {
  size_t count = 0;
  for (const auto& requirement : requirements) {
    if (requirement.group == group) ++count;
  }
  return count;
}

bool hasDefinitionRequirement(zc::ArrayPtr<const checked::DefinitionFactRequirement> requirements,
                              identity::DefId definition) noexcept {
  for (const auto& requirement : requirements) {
    if (requirement.definition == definition) return true;
  }
  return false;
}

size_t definitionRequirementCount(
    zc::ArrayPtr<const checked::DefinitionFactRequirement> requirements,
    CheckedFactGroup group) noexcept {
  size_t count = 0;
  for (const auto& requirement : requirements) {
    if (requirement.group == group) ++count;
  }
  return count;
}

zc::Maybe<checked::PatternFactMap::Entry> identifierPatternFact(
    const BodyProductionSite& site, identity::DefId definition,
    identity::SemanticTypeId semanticType) {
  zc::Vector<checked::PatternBindingFact> bindings;
  bindings.add(checked::PatternBindingFact{definition, semanticType});
  zc::Vector<checked::PatternRefinementFact> refinements;
  zc::Maybe<identity::SemanticTypeId> noGuard;

  return checked::PatternFactMap::Entry{
      site.node,
      checked::CheckedPatternFact{site.node, semanticType,
                                  checked::PatternConstructor(checked::WildcardPattern{}),
                                  zc::mv(bindings), zc::mv(refinements), true, zc::mv(noGuard)},
      zc::Array<uint8_t>()};
}

checked::ConstantFactMap::Entry scalarConstantFact(identity::DefId definition,
                                                   ast::NodeId expression,
                                                   const checked::LiteralFactMap::Entry& literal) {
  using DependencyMap = checked::ImmutableFactMap<identity::DefId, identity::Sha256Digest>;
  zc::Vector<DependencyMap::Entry> dependencyEntries;
  auto dependencies = DependencyMap::fromEntries(zc::mv(dependencyEntries));
  return checked::ConstantFactMap::Entry{
      definition,
      checked::ConstantEvaluationFact{definition, expression, literal.value.literal.clone(),
                                      literal.value.type, zc::mv(dependencies),
                                      identity::Sha256Digest()},
      zc::Array<uint8_t>()};
}

bool hasCompleteFamilyCoverage(zc::ArrayPtr<const BodyFactFamilyCoverage> coverage) {
  if (coverage.size() != 22) return false;
  for (size_t index = 0; index < coverage.size(); ++index) {
    if (static_cast<uint8_t>(coverage[index].group) != index + 1) return false;
  }
  return true;
}

}  // namespace

struct VerifiedBodyFactRequirementInventory::Impl final {
  Impl(identity::SemanticContextBrand semanticContext, identity::ModuleId module,
       const identity::Sha256Digest& sourceContentDigest,
       const binder::ParsedModuleReceipt& parsedModuleReceipt,
       zc::Vector<BodyFactFamilyCoverage>&& familyCoverage,
       zc::Vector<checked::NodeFactRequirement>&& nodeRequirements,
       zc::Vector<checked::DefinitionFactRequirement>&& definitionRequirements,
       zc::Vector<checked::CaptureFactRequirement>&& captureRequirements,
       zc::Vector<BodyProductionSite>&& productionSites)
      : semanticContextValue(semanticContext),
        moduleValue(module),
        sourceContentDigestValue(sourceContentDigest),
        parsedModuleReceiptValue(parsedModuleReceipt),
        familyCoverageValues(zc::mv(familyCoverage)),
        nodeRequirementValues(zc::mv(nodeRequirements)),
        definitionRequirementValues(zc::mv(definitionRequirements)),
        captureRequirementValues(zc::mv(captureRequirements)),
        productionSiteValues(zc::mv(productionSites)) {}

  identity::SemanticContextBrand semanticContextValue;
  identity::ModuleId moduleValue;
  identity::Sha256Digest sourceContentDigestValue;
  binder::ParsedModuleReceipt parsedModuleReceiptValue;
  zc::Vector<BodyFactFamilyCoverage> familyCoverageValues;
  zc::Vector<checked::NodeFactRequirement> nodeRequirementValues;
  zc::Vector<checked::DefinitionFactRequirement> definitionRequirementValues;
  zc::Vector<checked::CaptureFactRequirement> captureRequirementValues;
  zc::Vector<BodyProductionSite> productionSiteValues;
};

VerifiedBodyFactRequirementInventory::VerifiedBodyFactRequirementInventory(
    zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}
VerifiedBodyFactRequirementInventory::~VerifiedBodyFactRequirementInventory() noexcept(false) =
    default;
VerifiedBodyFactRequirementInventory::VerifiedBodyFactRequirementInventory(
    VerifiedBodyFactRequirementInventory&&) noexcept = default;
VerifiedBodyFactRequirementInventory& VerifiedBodyFactRequirementInventory::operator=(
    VerifiedBodyFactRequirementInventory&&) noexcept = default;
identity::SemanticContextBrand VerifiedBodyFactRequirementInventory::semanticContext()
    const noexcept {
  return impl->semanticContextValue;
}
identity::ModuleId VerifiedBodyFactRequirementInventory::module() const noexcept {
  return impl->moduleValue;
}
const identity::Sha256Digest& VerifiedBodyFactRequirementInventory::sourceContentDigest()
    const noexcept {
  return impl->sourceContentDigestValue;
}
const binder::ParsedModuleReceipt& VerifiedBodyFactRequirementInventory::parsedModuleReceipt()
    const noexcept {
  return impl->parsedModuleReceiptValue;
}
zc::ArrayPtr<const BodyFactFamilyCoverage> VerifiedBodyFactRequirementInventory::familyCoverage()
    const noexcept {
  return impl->familyCoverageValues.asPtr();
}
zc::ArrayPtr<const checked::NodeFactRequirement>
VerifiedBodyFactRequirementInventory::nodeRequirements() const noexcept {
  return impl->nodeRequirementValues.asPtr();
}
zc::ArrayPtr<const checked::DefinitionFactRequirement>
VerifiedBodyFactRequirementInventory::definitionRequirements() const noexcept {
  return impl->definitionRequirementValues.asPtr();
}
zc::ArrayPtr<const checked::CaptureFactRequirement>
VerifiedBodyFactRequirementInventory::captureRequirements() const noexcept {
  return impl->captureRequirementValues.asPtr();
}

BodyFactRequirementInventoryBuildResult BodyFactRequirementInventoryBuilder::build(
    const driver::module_graph_query::CheckerBoundModuleView& boundModule) {
  const auto& tree = boundModule.tree();
  const auto& parsedModule = boundModule.parsedModule();
  if (!boundModule.semanticContext().isValid() || !tree.contains(tree.root()) ||
      &tree != &parsedModule.tree()) {
    return rejectInvariant(signature::CheckerInvariantKind::InputReceiptMismatch,
                           boundModule.module(), 0);
  }

  zc::Vector<BodyFactFamilyCoverage> coverage;
  coverage.add(
      BodyFactFamilyCoverage{CheckedFactGroup::NodeType, BodyFactRequirementOrigin::Syntax});
  coverage.add(
      BodyFactFamilyCoverage{CheckedFactGroup::DefinitionType, BodyFactRequirementOrigin::Binding});
  coverage.add(
      BodyFactFamilyCoverage{CheckedFactGroup::Literal, BodyFactRequirementOrigin::Syntax});
  coverage.add(
      BodyFactFamilyCoverage{CheckedFactGroup::Constant, BodyFactRequirementOrigin::Binding});
  coverage.add(
      BodyFactFamilyCoverage{CheckedFactGroup::Aggregate, BodyFactRequirementOrigin::Syntax});
  coverage.add(
      BodyFactFamilyCoverage{CheckedFactGroup::Place, BodyFactRequirementOrigin::SemanticAnalysis});
  coverage.add(BodyFactFamilyCoverage{CheckedFactGroup::Coercion,
                                      BodyFactRequirementOrigin::SemanticAnalysis});
  coverage.add(BodyFactFamilyCoverage{CheckedFactGroup::Cast, BodyFactRequirementOrigin::Syntax});
  coverage.add(BodyFactFamilyCoverage{CheckedFactGroup::Call, BodyFactRequirementOrigin::Syntax});
  coverage.add(BodyFactFamilyCoverage{CheckedFactGroup::CompoundAssignment,
                                      BodyFactRequirementOrigin::Syntax});
  coverage.add(BodyFactFamilyCoverage{CheckedFactGroup::Member, BodyFactRequirementOrigin::Syntax});
  coverage.add(BodyFactFamilyCoverage{CheckedFactGroup::Index, BodyFactRequirementOrigin::Syntax});
  coverage.add(
      BodyFactFamilyCoverage{CheckedFactGroup::Pattern, BodyFactRequirementOrigin::Syntax});
  coverage.add(BodyFactFamilyCoverage{CheckedFactGroup::ObservedOperation,
                                      BodyFactRequirementOrigin::SemanticAnalysis});
  coverage.add(
      BodyFactFamilyCoverage{CheckedFactGroup::Capture, BodyFactRequirementOrigin::Binding});
  coverage.add(BodyFactFamilyCoverage{CheckedFactGroup::MarkerObligation,
                                      BodyFactRequirementOrigin::SemanticAnalysis});
  coverage.add(
      BodyFactFamilyCoverage{CheckedFactGroup::Exhaustiveness, BodyFactRequirementOrigin::Syntax});
  coverage.add(BodyFactFamilyCoverage{CheckedFactGroup::UnsafeOperation,
                                      BodyFactRequirementOrigin::SemanticAnalysis});
  coverage.add(BodyFactFamilyCoverage{CheckedFactGroup::Projection,
                                      BodyFactRequirementOrigin::SemanticAnalysis});
  coverage.add(BodyFactFamilyCoverage{CheckedFactGroup::Obligation,
                                      BodyFactRequirementOrigin::SemanticAnalysis});
  coverage.add(BodyFactFamilyCoverage{CheckedFactGroup::ErrorUnionShape,
                                      BodyFactRequirementOrigin::SemanticAnalysis});
  coverage.add(
      BodyFactFamilyCoverage{CheckedFactGroup::ErrorOperator, BodyFactRequirementOrigin::Syntax});

  zc::Vector<checked::NodeFactRequirement> nodeRequirements;
  zc::Vector<checked::DefinitionFactRequirement> definitionRequirements;
  zc::Vector<checked::CaptureFactRequirement> captureRequirements;
  zc::Vector<BodyProductionSite> productionSites;
  zc::Maybe<checked::CheckedFactsInvariantRejected> failure;
  uint32_t schemaPreorder = 0;
  ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId node, const ast::Node& syntax) {
    if (failure != zc::none) return;
    const uint32_t ordinal = schemaPreorder++;
    if (syntax.kind == ast::SyntaxKind::Unknown) {
      failure = rejectInvariant(signature::CheckerInvariantKind::InvalidFact, boundModule.module(),
                                ordinal, zc::none, node);
      return;
    }
    const bool bodyNode = isExpression(syntax.kind) || isPattern(syntax.kind) ||
                          syntax.kind == ast::SyntaxKind::MatchStmt ||
                          syntax.kind == ast::SyntaxKind::SuspendStatement;
    if (!bodyNode) return;
    auto span = parsedModule.spanFor(syntax.range);
    if (span == zc::none) {
      failure = rejectInvariant(signature::CheckerInvariantKind::InputReceiptMismatch,
                                boundModule.module(), ordinal, zc::none, node);
      return;
    }
    ZC_IF_SOME(sourceSpan, span) {
      checked::CheckedNodeKey key{static_cast<uint32_t>(syntax.kind), ordinal, sourceSpan.clone()};
      if (isExpression(syntax.kind)) {
        addNodeRequirement(nodeRequirements, CheckedFactGroup::NodeType, node, key);
      }
      if (isScalarLiteral(syntax.kind)) {
        addNodeRequirement(nodeRequirements, CheckedFactGroup::Literal, node, key);
      } else if (syntax.kind == ast::SyntaxKind::ArrayLiteral ||
                 syntax.kind == ast::SyntaxKind::TupleLiteral ||
                 syntax.kind == ast::SyntaxKind::ObjectLiteralExpr ||
                 syntax.kind == ast::SyntaxKind::StructLiteralExpr ||
                 syntax.kind == ast::SyntaxKind::NewExpression) {
        addNodeRequirement(nodeRequirements, CheckedFactGroup::Aggregate, node, key);
      } else if (syntax.kind == ast::SyntaxKind::CastExpression) {
        addNodeRequirement(nodeRequirements, CheckedFactGroup::Cast, node, key);
      } else if (syntax.kind == ast::SyntaxKind::CallExpression ||
                 syntax.kind == ast::SyntaxKind::ImportCallExpression) {
        addNodeRequirement(nodeRequirements, CheckedFactGroup::Call, node, key);
      } else if (syntax.kind == ast::SyntaxKind::MemberExpression) {
        addNodeRequirement(nodeRequirements, CheckedFactGroup::Member, node, key);
        if (!isMethodCallCallee(tree, node)) {
          addNodeRequirement(nodeRequirements, CheckedFactGroup::Place, node, key);
        }
      } else if (syntax.kind == ast::SyntaxKind::IndexExpression) {
        addNodeRequirement(nodeRequirements, CheckedFactGroup::Call, node, key);
        addNodeRequirement(nodeRequirements, CheckedFactGroup::Place, node, key);
        addNodeRequirement(nodeRequirements, CheckedFactGroup::Index, node, key);
        addNodeRequirement(nodeRequirements, CheckedFactGroup::MarkerObligation, node, key);
      } else if (isPattern(syntax.kind) && !(syntax.kind == ast::SyntaxKind::IdentifierPattern &&
                                             isOwnerLocalPattern(boundModule, node))) {
        addNodeRequirement(nodeRequirements, CheckedFactGroup::Pattern, node, key);
      } else if (syntax.kind == ast::SyntaxKind::MatchStmt) {
        addNodeRequirement(nodeRequirements, CheckedFactGroup::Exhaustiveness, node, key);
      } else if (syntax.kind == ast::SyntaxKind::SuspendStatement) {
        addNodeRequirement(nodeRequirements, CheckedFactGroup::ObservedOperation, node, key);
      } else if (syntax.kind == ast::SyntaxKind::NullCoalesceExpr) {
        const OperatorKind operation(PrimitiveOperation::NullCoalesce);
        if (operation.variant().is<PrimitiveOperation>()) {
          addNodeRequirement(nodeRequirements, CheckedFactGroup::Call, node, key);
        }
      } else if (syntax.kind == ast::SyntaxKind::UnaryExpression ||
                 syntax.kind == ast::SyntaxKind::BinaryExpr ||
                 syntax.kind == ast::SyntaxKind::PostfixExpression ||
                 syntax.kind == ast::SyntaxKind::AssignmentExpr) {
        if (!addOperatorRequirements(syntax, node, key, nodeRequirements)) {
          failure =
              rejectInvariant(signature::CheckerInvariantKind::InvalidFact, boundModule.module(),
                              ordinal, zc::none, node, sourceSpan.clone());
          return;
        }
      }
      BodyProductionKind production = BodyProductionKind::Unsupported;
      switch (syntax.kind) {
        case ast::SyntaxKind::NullLiteral:
          production = BodyProductionKind::NullLiteral;
          break;
        case ast::SyntaxKind::BoolLiteral:
          production = BodyProductionKind::BoolLiteral;
          break;
        case ast::SyntaxKind::IntLiteral:
          production = BodyProductionKind::IntLiteral;
          break;
        case ast::SyntaxKind::FloatLiteralExpr:
          production = BodyProductionKind::FloatLiteral;
          break;
        case ast::SyntaxKind::BigIntLiteral:
          production = BodyProductionKind::BigIntLiteral;
          break;
        case ast::SyntaxKind::StringLiteralExpr:
          production = BodyProductionKind::StringLiteral;
          break;
        case ast::SyntaxKind::UnitLiteral:
          production = BodyProductionKind::UnitLiteral;
          break;
        case ast::SyntaxKind::CharacterLiteralExpr:
          production = BodyProductionKind::CharacterLiteral;
          break;
        case ast::SyntaxKind::NoSubstitutionTemplateLiteralExpr:
          production = BodyProductionKind::NoSubstitutionTemplateLiteral;
          break;
        case ast::SyntaxKind::CallExpression:
          production =
              tree.node(ast::NodeId(syntax.payload.words[ast::kCallExpressionCalleeWord])).kind ==
                      ast::SyntaxKind::MemberExpression
                  ? BodyProductionKind::ConcreteMethodCall
                  : BodyProductionKind::DirectCall;
          break;
        case ast::SyntaxKind::IdentExpr:
          production = BodyProductionKind::IdentifierReference;
          break;
        case ast::SyntaxKind::AssignmentExpr:
          if (isSimpleLocalWrite(boundModule, node)) {
            production = BodyProductionKind::LocalWrite;
          } else if (isSimpleOwnerLocalFieldWrite(boundModule, node)) {
            production = BodyProductionKind::OwnerLocalFieldWrite;
          }
          break;
        case ast::SyntaxKind::MemberExpression:
          production = isMethodCallCallee(tree, node)
                           ? BodyProductionKind::OwnerLocalMethodReference
                           : BodyProductionKind::OwnerLocalFieldReference;
          break;
        case ast::SyntaxKind::IndexExpression:
          production = BodyProductionKind::ReadIndex;
          break;
        case ast::SyntaxKind::StructLiteralExpr:
          production = BodyProductionKind::StructLiteral;
          break;
        case ast::SyntaxKind::BinaryExpr: {
          // Admit the six relational comparisons (result bool, any position) and
          // the twelve arithmetic/bitwise operators (result operand type, only
          // outside a condition) where each operand is a scalar value reference
          // (a parameter or an owner local) or a scalar literal and at least one
          // operand is a reference; every other binary shape stays unsupported so
          // its existing rejection stands. A literal-vs-literal operation has no
          // place to lower and is left unsupported.
          const auto operation =
              static_cast<ast::BinaryOperatorKind>(syntax.payload.words[ast::kBinaryExprOpWord]);
          const bool isComparison = scalarComparisonOperation(operation) != zc::none;
          const bool isArithmetic =
              !isComparison && scalarArithmeticOperation(operation) != zc::none;
          if (!isComparison && !isArithmetic) break;
          // An arithmetic result is not bool, so it cannot drive an `if` / `while`
          // condition; it stays unsupported there and the existing rejection
          // stands.
          if (isArithmetic && isConditionPosition(boundModule, node)) break;
          const ast::NodeId left(syntax.payload.words[ast::kBinaryExprLhsWord]);
          const ast::NodeId right(syntax.payload.words[ast::kBinaryExprRhsWord]);
          if (tree.contains(left) && tree.contains(right)) {
            const bool leftIsReference =
                tree.node(left).kind == ast::SyntaxKind::IdentExpr &&
                (resolvedCallableParameter(boundModule.bindings(), left) != zc::none ||
                 resolvedOwnerLocal(boundModule.bindings(), left) != zc::none);
            const bool rightIsReference =
                tree.node(right).kind == ast::SyntaxKind::IdentExpr &&
                (resolvedCallableParameter(boundModule.bindings(), right) != zc::none ||
                 resolvedOwnerLocal(boundModule.bindings(), right) != zc::none);
            // An operand may also be a nested primitive binary (`a + b * c`). This
            // is structural; the shape validator enforces the one-level bound and
            // the shared operand type. Both operands nested is left unsupported.
            const bool leftIsNested = tree.node(left).kind == ast::SyntaxKind::BinaryExpr;
            const bool rightIsNested = tree.node(right).kind == ast::SyntaxKind::BinaryExpr;
            const bool leftOk =
                leftIsReference || isScalarLiteral(tree.node(left).kind) || leftIsNested;
            const bool rightOk =
                rightIsReference || isScalarLiteral(tree.node(right).kind) || rightIsNested;
            if (leftOk && rightOk &&
                (leftIsReference || rightIsReference || leftIsNested || rightIsNested)) {
              production = BodyProductionKind::PrimitiveBinaryOperation;
            }
          }
          break;
        }
        case ast::SyntaxKind::UnaryExpression: {
          const auto operation = static_cast<ast::UnaryOperatorKind>(
              syntax.payload.words[ast::kUnaryExpressionOpWord]);
          if (operation == ast::UnaryOperatorKind::Ref ||
              operation == ast::UnaryOperatorKind::RefMut) {
            const ast::NodeId operand(syntax.payload.words[ast::kUnaryExpressionOperandWord]);
            if (tree.contains(operand) && tree.node(operand).kind == ast::SyntaxKind::IdentExpr &&
                resolvedOwnerLocal(boundModule.bindings(), operand) != zc::none) {
              production = BodyProductionKind::LocalBorrow;
            } else {
              production = BodyProductionKind::ReferenceReborrow;
            }
          } else if (operation == ast::UnaryOperatorKind::Deref) {
            production = BodyProductionKind::ReferenceDereference;
          }
          break;
        }
        case ast::SyntaxKind::PostfixExpression: {
          const auto operation = static_cast<ast::PostfixOperatorKind>(
              syntax.payload.words[ast::kPostfixExpressionOpWord]);
          if (operation == ast::PostfixOperatorKind::ErrorPropagate ||
              operation == ast::PostfixOperatorKind::ErrorUnwrap) {
            production = BodyProductionKind::ErrorOperator;
          }
          break;
        }
        case ast::SyntaxKind::UnsafeBlockExpr:
          production = BodyProductionKind::UnsafeBlock;
          break;
        default:
          break;
      }
      CheckedFactGroup primary = CheckedFactGroup::NodeType;
      if (isPattern(syntax.kind)) primary = CheckedFactGroup::Pattern;
      if (syntax.kind == ast::SyntaxKind::MatchStmt) primary = CheckedFactGroup::Exhaustiveness;
      if (syntax.kind == ast::SyntaxKind::SuspendStatement) {
        primary = CheckedFactGroup::ObservedOperation;
      }
      productionSites.add(BodyProductionSite{node, cloneNodeKey(key), primary, production});
    }
  });
  ZC_IF_SOME(rejection, failure) { return zc::mv(rejection); }

  for (const auto& definition : boundModule.bindings().definitions()) {
    switch (definition.kind) {
      case identity::DefinitionKind::Parameter:
      case identity::DefinitionKind::Constant:
      case identity::DefinitionKind::Static:
      case identity::DefinitionKind::Local:
      case identity::DefinitionKind::PatternBinding:
      case identity::DefinitionKind::Closure:
        definitionRequirements.add(checked::DefinitionFactRequirement{
            CheckedFactGroup::DefinitionType, definition.identity});
        if (definition.kind == identity::DefinitionKind::Constant) {
          definitionRequirements.add(
              checked::DefinitionFactRequirement{CheckedFactGroup::Constant, definition.identity});
        }
        break;
      default:
        break;
    }
  }
  for (const auto& closure : boundModule.bindings().closureFreeVariables()) {
    for (const auto& variable : closure.variables) {
      checked::CaptureKey key{closure.closure.clone(), variable.target.clone()};
      if (!hasCapture(captureRequirements.asPtr(), key)) {
        captureRequirements.add(checked::CaptureFactRequirement{zc::mv(key)});
      }
    }
  }
  for (const auto& closure : boundModule.bindings().explicitClosureCaptures()) {
    for (const auto& capture : closure.captures) {
      checked::CaptureKey key{closure.closure.clone(), capture.target.clone()};
      if (!hasCapture(captureRequirements.asPtr(), key)) {
        captureRequirements.add(checked::CaptureFactRequirement{zc::mv(key)});
      }
    }
  }

  return VerifiedBodyFactRequirementInventory(zc::heap<VerifiedBodyFactRequirementInventory::Impl>(
      boundModule.semanticContext(), boundModule.module(), parsedModule.contentDigest(),
      parsedModule.receipt(), zc::mv(coverage), zc::mv(nodeRequirements),
      zc::mv(definitionRequirements), zc::mv(captureRequirements), zc::mv(productionSites)));
}

struct BodyChecker::Impl final {};

BodyChecker::BodyChecker() : impl(zc::heap<Impl>()) {}
BodyChecker::~BodyChecker() noexcept(false) = default;

BodyCheckingResult BodyChecker::check(const BodyCheckingInput& input,
                                      const identity::RegistryBrandIssuer& factStoreBrands) {
  const auto context = input.boundModule.semanticContext();
  const auto module = input.boundModule.module();
  const auto& parsedModule = input.boundModule.parsedModule();
  if (!context.isValid() || input.signatureFacts.semanticContext() != context ||
      input.importedSignatures.semanticContext() != context ||
      input.coherence.semanticContext() != context ||
      input.identities.semanticContext() != context || input.semanticTypes.context() != context ||
      input.requirements.semanticContext() != context || input.signatureFacts.module() != module ||
      input.importedSignatures.requester() != module || input.requirements.module() != module ||
      input.boundModule.bindingSurface().revision().digest() !=
          input.signatureFacts.bindingSurfaceRevision().digest() ||
      input.boundModule.semanticFingerprint().digest() !=
          input.signatureFacts.contextFingerprint().digest() ||
      input.boundModule.semanticFingerprint().digest() !=
          input.importedSignatures.contextFingerprint().digest() ||
      input.boundModule.semanticFingerprint().digest() !=
          input.coherence.contextFingerprint().digest() ||
      parsedModule.contentDigest() != input.signatureFacts.sourceContentDigest() ||
      parsedModule.contentDigest() != input.requirements.sourceContentDigest() ||
      parsedModule.receipt().digest() != input.signatureFacts.parsedModuleReceipt().digest() ||
      parsedModule.receipt().digest() != input.requirements.parsedModuleReceipt().digest() ||
      !hasCompleteFamilyCoverage(input.requirements.familyCoverage())) {
    return rejectInvariant(signature::CheckerInvariantKind::InputReceiptMismatch, module, 0);
  }

  if (input.requirements.captureRequirements().size() != 0) {
    zc::Maybe<identity::DefId> closureOwner;
    ZC_IF_SOME(ownerKey,
               input.requirements.captureRequirements()[0].key.closure.owner().definitionKey()) {
      ZC_IF_SOME(owner, input.identities.definition(ownerKey)) { closureOwner = owner.handle(); }
    }
    return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, module, 0,
                           zc::mv(closureOwner), zc::none, zc::none,
                           factPath(CheckedFactGroup::Capture));
  }

  zc::Vector<checked::NodeTypeMap::Entry> nodeTypes;
  zc::Vector<checked::DefinitionTypeMap::Entry> definitionTypes;
  zc::Vector<checked::LiteralFactMap::Entry> literals;
  zc::Vector<checked::ConstantFactMap::Entry> constants;
  zc::Vector<checked::PatternFactMap::Entry> patterns;
  zc::Vector<checked::CallFactMap::Entry> calls;
  zc::Vector<checked::AggregateFactMap::Entry> aggregates;
  zc::Vector<checked::PlaceFactMap::Entry> places;
  zc::Vector<checked::MemberFactMap::Entry> members;
  zc::Vector<checked::IndexFactMap::Entry> indexes;
  zc::Vector<checked::MarkerObligationFactMap::Entry> markerObligations;
  for (uint8_t stage = 0; stage != 5; ++stage) {
    for (const auto& site : input.requirements.impl->productionSiteValues) {
      bool deferredLocalReference = false;
      if (site.production == BodyProductionKind::IdentifierReference) {
        deferredLocalReference = dependsOnStructuredLocalInitializer(input, site.node) ||
                                 dependsOnDirectCallLocalInitializer(input, site.node);
      }
      const bool structured = site.production == BodyProductionKind::StructLiteral;
      const bool projected = site.production == BodyProductionKind::OwnerLocalFieldReference ||
                             site.production == BodyProductionKind::OwnerLocalMethodReference ||
                             deferredLocalReference;
      const bool methodReference = site.production == BodyProductionKind::OwnerLocalMethodReference;
      const bool fieldWrite = site.production == BodyProductionKind::OwnerLocalFieldWrite;
      const bool directCall = site.production == BodyProductionKind::DirectCall;
      const bool concreteMethodCall = site.production == BodyProductionKind::ConcreteMethodCall;
      const bool errorOperator = site.production == BodyProductionKind::ErrorOperator;
      const bool indexed = site.production == BodyProductionKind::ReadIndex;
      const bool unsafeBlock = site.production == BodyProductionKind::UnsafeBlock;
      const bool primitiveBinary = site.production == BodyProductionKind::PrimitiveBinaryOperation;
      if ((stage == 0 &&
           (structured || projected || fieldWrite || directCall || concreteMethodCall ||
            errorOperator || indexed || unsafeBlock || primitiveBinary)) ||
          (stage == 1 && (((!structured && !directCall) || errorOperator || indexed) &&
                          !unsafeBlock && !primitiveBinary)) ||
          (stage == 2 && ((!projected && !indexed) || methodReference)) ||
          (stage == 3 && (!fieldWrite && !concreteMethodCall && !methodReference)) ||
          (stage == 4 && !errorOperator)) {
        continue;
      }
      if (site.production == BodyProductionKind::Unsupported) {
        if (input.boundModule.tree().node(site.node).kind == ast::SyntaxKind::IdentifierPattern) {
          continue;
        }
        return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, module,
                               site.key.schemaPreorder, zc::none, site.node,
                               site.key.sourceSpan.clone(), factPath(site.primaryGroup));
      }
      zc::Maybe<identity::SemanticTypeId> producedType;
      if (site.production == BodyProductionKind::UnsafeBlock) {
        const auto& unsafeBlock = input.boundModule.tree().node(site.node);
        const ast::NodeId body(unsafeBlock.payload.words[ast::kUnsafeBlockExprBodyWord]);
        if (!input.boundModule.tree().contains(body) ||
            input.boundModule.tree().node(body).kind != ast::SyntaxKind::BlockStmt) {
          return rejectInvariant(signature::CheckerInvariantKind::InvalidFact, module,
                                 site.key.schemaPreorder, zc::none, site.node,
                                 site.key.sourceSpan.clone(), factPath(site.primaryGroup));
        }
        const auto& blockNode = input.boundModule.tree().node(body);
        const ast::NodeList statements{blockNode.payload.words[ast::kBlockStmtStmtsFirstWord],
                                       blockNode.payload.words[ast::kBlockStmtStmtsSizeWord]};
        if (!input.boundModule.tree().contains(statements) || statements.empty()) {
          return rejectInvariant(signature::CheckerInvariantKind::InvalidFact, module,
                                 site.key.schemaPreorder, zc::none, site.node,
                                 site.key.sourceSpan.clone(), factPath(site.primaryGroup));
        }
        ast::NodeId tailStatement = input.boundModule.tree().list(statements)[statements.size - 1];
        if (input.boundModule.tree().node(tailStatement).kind ==
            ast::SyntaxKind::StatementListItem) {
          tailStatement = ast::NodeId(input.boundModule.tree()
                                          .node(tailStatement)
                                          .payload.words[ast::kStatementListItemItemWord]);
        }
        if (!input.boundModule.tree().contains(tailStatement) ||
            input.boundModule.tree().node(tailStatement).kind !=
                ast::SyntaxKind::ExpressionStatement) {
          return rejectInvariant(signature::CheckerInvariantKind::InvalidFact, module,
                                 site.key.schemaPreorder, zc::none, site.node,
                                 site.key.sourceSpan.clone(), factPath(site.primaryGroup));
        }
        const ast::NodeId tailExpression(
            input.boundModule.tree()
                .node(tailStatement)
                .payload.words[ast::kExpressionStatementExpressionWord]);
        auto tailType = factEntry(nodeTypes.asPtr(), tailExpression);
        if (tailType == zc::none) {
          return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, module,
                                 site.key.schemaPreorder, zc::none, site.node,
                                 site.key.sourceSpan.clone(), factPath(site.primaryGroup));
        }
        ZC_IF_SOME(value, tailType) { producedType = value.value; }
      } else if (site.production == BodyProductionKind::IdentifierReference) {
        auto shape = directCallableShape(input, site.node);
        ZC_IF_SOME(value, shape) { producedType = value.calleeType; }
        if (producedType == zc::none) {
          producedType = ownerLocalReferenceType(input, site.node, nodeTypes.asPtr());
        }
        if (producedType == zc::none) {
          producedType = callableParameterReferenceType(input, site.node);
        }
        if (producedType == zc::none) {
          return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, module,
                                 site.key.schemaPreorder, zc::none, site.node,
                                 site.key.sourceSpan.clone(), factPath(site.primaryGroup));
        }
      } else if (site.production == BodyProductionKind::ConcreteMethodCall) {
        auto shape = concreteMethodCallShape(input, site.node, nodeTypes.asPtr());
        if (shape == zc::none) {
          return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, module,
                                 site.key.schemaPreorder, zc::none, site.node,
                                 site.key.sourceSpan.clone(), factPath(site.primaryGroup));
        }
        ZC_IF_SOME(value, shape) {
          const auto& call = input.boundModule.tree().node(site.node);
          const ast::NodeList arguments{call.payload.words[ast::kCallExpressionArgsFirstWord],
                                        call.payload.words[ast::kCallExpressionArgsSizeWord]};
          if (!input.boundModule.tree().contains(arguments) ||
              arguments.size != value.parameters.size()) {
            return rejectInvariant(signature::CheckerInvariantKind::InvalidFact, module,
                                   site.key.schemaPreorder, zc::none, site.node,
                                   site.key.sourceSpan.clone(), factPath(site.primaryGroup));
          }
          zc::Vector<checked::CheckedArgumentFact> checkedArguments;
          for (size_t index = 0; index < input.boundModule.tree().list(arguments).size(); ++index) {
            const auto argument = input.boundModule.tree().list(arguments)[index];
            auto argumentType = factEntry(nodeTypes.asPtr(), argument);
            auto literal = factEntry(literals.asPtr(), argument);
            if (!input.boundModule.tree().contains(argument) ||
                !isScalarLiteral(input.boundModule.tree().node(argument).kind) ||
                argumentType == zc::none || literal == zc::none) {
              return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, module,
                                     site.key.schemaPreorder, zc::none, site.node,
                                     site.key.sourceSpan.clone(), factPath(site.primaryGroup));
            }
            ZC_IF_SOME(type, argumentType) {
              ZC_IF_SOME(literalFact, literal) {
                if (type.value != value.parameters[index] || literalFact.value.node != argument ||
                    literalFact.value.type != type.value) {
                  return rejectInvariant(signature::CheckerInvariantKind::InvalidFact, module,
                                         site.key.schemaPreorder, zc::none, site.node,
                                         site.key.sourceSpan.clone(), factPath(site.primaryGroup));
                }
                zc::Maybe<checked::CoercionAdjustment> noAdjustment;
                checkedArguments.add(checked::CheckedArgumentFact{
                    argument, type.value, value.parameters[index], zc::mv(noAdjustment)});
              }
            }
          }
          zc::Maybe<checked::CoercionAdjustment> noReceiverCoercion;
          zc::Maybe<checked::CheckedArgumentFact> receiver;
          receiver =
              checked::CheckedArgumentFact{value.receiverNode, value.receiverSourceType,
                                           value.receiverParameterType, zc::mv(noReceiverCoercion)};
          zc::Maybe<signature::ReceiverMode> receiverMode = signature::ReceiverMode::Mutable;
          zc::Vector<checked::ReceiverAdjustmentStep> adjustmentSteps;
          adjustmentSteps.add(checked::ReceiverAdjustmentStep::BorrowMutable);
          zc::Maybe<checked::ReceiverAdjustment> receiverAdjustment;
          receiverAdjustment =
              checked::ReceiverAdjustment{value.receiverSourceType, value.receiverParameterType,
                                          zc::mv(adjustmentSteps), site.key.sourceSpan.clone()};
          zc::Maybe<checked::CanonicalSubstitutionId> noSubstitutions;
          zc::Maybe<checked::WitnessArgumentsId> noWitnesses;
          zc::Maybe<identity::SemanticTypeId> noRaises;
          producedType = value.success;
          calls.add(checked::CallFactMap::Entry{
              site.node,
              checked::TypedCallFact{
                  site.node,
                  checked::CheckedCallEnvelope{
                      checked::SelectedCallable(checked::ConcreteMethodCallable{value.method}),
                      value.calleeType, zc::mv(receiver), zc::mv(receiverMode),
                      zc::mv(receiverAdjustment), zc::mv(checkedArguments), value.success,
                      value.success, zc::mv(noSubstitutions), zc::mv(noWitnesses),
                      zc::mv(noRaises)},
                  site.key.sourceSpan.clone()},
              zc::Array<uint8_t>()});
        }
      } else if (site.production == BodyProductionKind::DirectCall) {
        auto shape = directCallShape(input, site.node);
        if (shape == zc::none) {
          return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, module,
                                 site.key.schemaPreorder, zc::none, site.node,
                                 site.key.sourceSpan.clone(), factPath(site.primaryGroup));
        }
        ZC_IF_SOME(value, shape) {
          const auto& call = input.boundModule.tree().node(site.node);
          const ast::NodeList arguments{call.payload.words[ast::kCallExpressionArgsFirstWord],
                                        call.payload.words[ast::kCallExpressionArgsSizeWord]};
          if (!input.boundModule.tree().contains(arguments) ||
              arguments.size != value.parameters.size()) {
            return rejectInvariant(signature::CheckerInvariantKind::InvalidFact, module,
                                   site.key.schemaPreorder, zc::none, site.node,
                                   site.key.sourceSpan.clone(), factPath(site.primaryGroup));
          }
          producedType = value.success;
          zc::Maybe<checked::CheckedArgumentFact> noReceiver;
          zc::Maybe<signature::ReceiverMode> noReceiverMode;
          zc::Maybe<checked::ReceiverAdjustment> noReceiverAdjustment;
          zc::Vector<checked::CheckedArgumentFact> checkedArguments;
          const auto argumentNodes = input.boundModule.tree().list(arguments);
          for (size_t index = 0; index < argumentNodes.size(); ++index) {
            const auto argument = argumentNodes[index];
            auto argumentType = factEntry(nodeTypes.asPtr(), argument);
            auto literal = factEntry(literals.asPtr(), argument);
            const bool isLiteralArgument =
                input.boundModule.tree().contains(argument) &&
                isScalarLiteral(input.boundModule.tree().node(argument).kind);
            const bool isParameterArgument =
                input.boundModule.tree().contains(argument) &&
                input.boundModule.tree().node(argument).kind == ast::SyntaxKind::IdentExpr &&
                resolvedCallableParameter(input.boundModule.bindings(), argument) != zc::none;
            if ((!isLiteralArgument && !isParameterArgument) || argumentType == zc::none ||
                (isLiteralArgument && literal == zc::none)) {
              return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, module,
                                     site.key.schemaPreorder, zc::none, site.node,
                                     site.key.sourceSpan.clone(), factPath(site.primaryGroup));
            }
            ZC_IF_SOME(type, argumentType) {
              if (type.value != value.parameters[index]) {
                return rejectInvariant(signature::CheckerInvariantKind::InvalidFact, module,
                                       site.key.schemaPreorder, zc::none, site.node,
                                       site.key.sourceSpan.clone(), factPath(site.primaryGroup));
              }
              if (isLiteralArgument) {
                ZC_IF_SOME(literalFact, literal) {
                  if (literalFact.value.node != argument || literalFact.value.type != type.value) {
                    return rejectInvariant(signature::CheckerInvariantKind::InvalidFact, module,
                                           site.key.schemaPreorder, zc::none, site.node,
                                           site.key.sourceSpan.clone(),
                                           factPath(site.primaryGroup));
                  }
                }
              }
              zc::Maybe<checked::CoercionAdjustment> noAdjustment;
              checkedArguments.add(checked::CheckedArgumentFact{
                  argument, type.value, value.parameters[index], zc::mv(noAdjustment)});
            }
          }
          zc::Maybe<checked::CanonicalSubstitutionId> noSubstitutions;
          zc::Maybe<checked::WitnessArgumentsId> noWitnesses;
          zc::Maybe<identity::SemanticTypeId> noRaises;
          calls.add(checked::CallFactMap::Entry{
              site.node,
              checked::TypedCallFact{
                  site.node,
                  checked::CheckedCallEnvelope{
                      checked::SelectedCallable(checked::DirectCallable{value.callee}),
                      value.calleeType, zc::mv(noReceiver), zc::mv(noReceiverMode),
                      zc::mv(noReceiverAdjustment), zc::mv(checkedArguments), value.success,
                      value.success, zc::mv(noSubstitutions), zc::mv(noWitnesses),
                      zc::mv(noRaises)},
                  site.key.sourceSpan.clone()},
              zc::Array<uint8_t>()});
        }
      } else if (site.production == BodyProductionKind::ReadIndex) {
        auto shape = readIndexShape(input, site.node, nodeTypes.asPtr());
        if (shape == zc::none) {
          return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, module,
                                 site.key.schemaPreorder, zc::none, site.node,
                                 site.key.sourceSpan.clone(), factPath(site.primaryGroup));
        }
        ZC_IF_SOME(value, shape) {
          auto evidence = copyEvidence(input, value.elementType);
          if (evidence == zc::none) {
            return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, module,
                                   site.key.schemaPreorder, zc::none, site.node,
                                   site.key.sourceSpan.clone(),
                                   factPath(CheckedFactGroup::MarkerObligation));
          }
          ZC_IF_SOME(copy, evidence) {
            producedType = value.elementType;
            zc::Maybe<checked::CoercionAdjustment> noBaseAdjustment;
            zc::Maybe<checked::CheckedArgumentFact> receiver;
            receiver = checked::CheckedArgumentFact{value.baseNode, value.collectionType,
                                                    value.collectionType, zc::mv(noBaseAdjustment)};
            zc::Maybe<signature::ReceiverMode> receiverMode = signature::ReceiverMode::Shared;
            zc::Vector<checked::ReceiverAdjustmentStep> adjustmentSteps;
            adjustmentSteps.add(checked::ReceiverAdjustmentStep::BorrowShared);
            zc::Maybe<checked::ReceiverAdjustment> receiverAdjustment;
            receiverAdjustment =
                checked::ReceiverAdjustment{value.collectionType, value.collectionType,
                                            zc::mv(adjustmentSteps), site.key.sourceSpan.clone()};
            zc::Vector<checked::CheckedArgumentFact> arguments;
            zc::Maybe<checked::CoercionAdjustment> noIndexAdjustment;
            arguments.add(checked::CheckedArgumentFact{value.indexNode, value.indexType,
                                                       value.indexType, zc::mv(noIndexAdjustment)});
            zc::Maybe<checked::CanonicalSubstitutionId> noSubstitutions;
            zc::Maybe<checked::WitnessArgumentsId> noWitnesses;
            zc::Maybe<identity::SemanticTypeId> noRaises;
            calls.add(checked::CallFactMap::Entry{
                site.node,
                checked::TypedCallFact{
                    site.node,
                    checked::CheckedCallEnvelope{
                        checked::SelectedCallable(
                            checked::PrimitiveCallable{PrimitiveOperation::Index}),
                        value.collectionType, zc::mv(receiver), zc::mv(receiverMode),
                        zc::mv(receiverAdjustment), zc::mv(arguments), value.elementType,
                        value.elementType, zc::mv(noSubstitutions), zc::mv(noWitnesses),
                        zc::mv(noRaises)},
                    site.key.sourceSpan.clone()},
                zc::Array<uint8_t>()});
            zc::Vector<checked::PlaceProjection> projections;
            projections.add(checked::PlaceProjection(checked::IndexProjection{value.indexNode}));
            places.add(checked::PlaceFactMap::Entry{
                site.node,
                checked::CheckedPlaceFact{
                    site.node, checked::PlaceRoot(checked::CallableParameterPlaceRoot{value.base}),
                    zc::mv(projections), value.elementType, false, false},
                zc::Array<uint8_t>()});
            indexes.add(checked::IndexFactMap::Entry{
                site.node,
                checked::CheckedIndexFact{site.node, value.collectionType, value.indexType,
                                          value.elementType, checked::IndexAccessMode::Read,
                                          value.elementType},
                zc::Array<uint8_t>()});
            markerObligations.add(checked::MarkerObligationFactMap::Entry{
                site.node,
                checked::MarkerObligationFact{site.node, value.elementType,
                                              input.standardMarkers.copy(),
                                              checked::Polarity::Positive, zc::mv(copy)},
                zc::Array<uint8_t>()});
          }
        }
      } else if (site.production == BodyProductionKind::PrimitiveBinaryOperation) {
        auto shape =
            primitiveBinaryOperationShape(input, site.node, nodeTypes.asPtr(), literals.asPtr());
        if (shape == zc::none) {
          return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, module,
                                 site.key.schemaPreorder, zc::none, site.node,
                                 site.key.sourceSpan.clone(), factPath(site.primaryGroup));
        }
        ZC_IF_SOME(value, shape) {
          producedType = value.resultType;
          zc::Maybe<checked::CheckedArgumentFact> noReceiver;
          zc::Maybe<signature::ReceiverMode> noReceiverMode;
          zc::Maybe<checked::ReceiverAdjustment> noReceiverAdjustment;
          zc::Vector<checked::CheckedArgumentFact> arguments;
          zc::Maybe<checked::CoercionAdjustment> noLeftAdjustment;
          zc::Maybe<checked::CoercionAdjustment> noRightAdjustment;
          arguments.add(checked::CheckedArgumentFact{value.leftNode, value.operandType,
                                                     value.operandType, zc::mv(noLeftAdjustment)});
          arguments.add(checked::CheckedArgumentFact{value.rightNode, value.operandType,
                                                     value.operandType, zc::mv(noRightAdjustment)});
          zc::Maybe<checked::CanonicalSubstitutionId> noSubstitutions;
          zc::Maybe<checked::WitnessArgumentsId> noWitnesses;
          zc::Maybe<identity::SemanticTypeId> noRaises;
          calls.add(checked::CallFactMap::Entry{
              site.node,
              checked::TypedCallFact{
                  site.node,
                  checked::CheckedCallEnvelope{
                      checked::SelectedCallable(checked::PrimitiveCallable{value.operation}),
                      value.operandType, zc::mv(noReceiver), zc::mv(noReceiverMode),
                      zc::mv(noReceiverAdjustment), zc::mv(arguments), value.resultType,
                      value.resultType, zc::mv(noSubstitutions), zc::mv(noWitnesses),
                      zc::mv(noRaises)},
                  site.key.sourceSpan.clone()},
              zc::Array<uint8_t>()});
        }
      } else if (site.production == BodyProductionKind::LocalWrite) {
        const auto& assignment = input.boundModule.tree().node(site.node);
        const ast::NodeId target(assignment.payload.words[ast::kAssignmentExprLhsWord]);
        producedType = ownerLocalReferenceType(input, target, nodeTypes.asPtr());
        if (producedType == zc::none) {
          return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, module,
                                 site.key.schemaPreorder, zc::none, site.node,
                                 site.key.sourceSpan.clone(), factPath(site.primaryGroup));
        }
      } else if (site.production == BodyProductionKind::OwnerLocalFieldWrite) {
        const auto& assignment = input.boundModule.tree().node(site.node);
        const ast::NodeId target(assignment.payload.words[ast::kAssignmentExprLhsWord]);
        auto targetType = factEntry(nodeTypes.asPtr(), target);
        auto targetPlace = factEntry(places.asPtr(), target);
        if (targetType == zc::none || targetPlace == zc::none) {
          return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, module,
                                 site.key.schemaPreorder, zc::none, site.node,
                                 site.key.sourceSpan.clone(), factPath(site.primaryGroup));
        }
        ZC_IF_SOME(type, targetType) {
          ZC_IF_SOME(place, targetPlace) {
            if (place.value.type != type.value || !place.value.mutablePlace) {
              return rejectInvariant(signature::CheckerInvariantKind::InvalidFact, module,
                                     site.key.schemaPreorder, zc::none, site.node,
                                     site.key.sourceSpan.clone(), factPath(site.primaryGroup));
            }
            producedType = type.value;
          }
        }
      } else if (site.production == BodyProductionKind::OwnerLocalFieldReference) {
        auto shape = ownerLocalFieldShape(input, site.node, nodeTypes.asPtr());
        if (shape == zc::none) {
          return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, module,
                                 site.key.schemaPreorder, zc::none, site.node,
                                 site.key.sourceSpan.clone(), factPath(site.primaryGroup));
        }
        ZC_IF_SOME(value, shape) {
          producedType = value.fieldType;
          zc::Maybe<checked::CoercionAdjustment> noAdjustment;
          members.add(checked::MemberFactMap::Entry{
              site.node,
              checked::CheckedMemberFact{site.node, value.receiverType, value.field,
                                         value.fieldType, zc::mv(noAdjustment)},
              zc::Array<uint8_t>()});
          zc::Vector<checked::PlaceProjection> projections;
          projections.add(checked::PlaceProjection(checked::FieldProjection{value.field}));
          places.add(checked::PlaceFactMap::Entry{
              site.node,
              checked::CheckedPlaceFact{
                  site.node, checked::PlaceRoot(checked::OwnerLocalPlaceRoot{value.binding}),
                  zc::mv(projections), value.fieldType, value.mutablePlace, true},
              zc::Array<uint8_t>()});
        }
      } else if (site.production == BodyProductionKind::OwnerLocalMethodReference) {
        ast::NodeId callNode;
        ast::visitTreePreOrder(
            input.boundModule.tree(), input.boundModule.tree().root(),
            [&](ast::NodeId node, const ast::Node& syntax) {
              if (syntax.kind == ast::SyntaxKind::CallExpression &&
                  ast::NodeId(syntax.payload.words[ast::kCallExpressionCalleeWord]) == site.node) {
                callNode = node;
              }
            });
        auto shape = concreteMethodCallShape(input, callNode, nodeTypes.asPtr());
        if (shape == zc::none) {
          return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, module,
                                 site.key.schemaPreorder, zc::none, site.node,
                                 site.key.sourceSpan.clone(), factPath(site.primaryGroup));
        }
        ZC_IF_SOME(value, shape) {
          producedType = value.calleeType;
          zc::Maybe<checked::CoercionAdjustment> noAdjustment;
          members.add(checked::MemberFactMap::Entry{
              site.node,
              checked::CheckedMemberFact{site.node, value.receiverSourceType, value.method,
                                         value.calleeType, zc::mv(noAdjustment)},
              zc::Array<uint8_t>()});
        }
      } else if (site.production == BodyProductionKind::ReferenceDereference) {
        const auto& syntax = input.boundModule.tree().node(site.node);
        const ast::NodeId source(syntax.payload.words[ast::kUnaryExpressionOperandWord]);
        auto sourceType = referenceSourceType(input, source, nodeTypes.asPtr());
        if (sourceType == zc::none) {
          return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, module,
                                 site.key.schemaPreorder, zc::none, site.node,
                                 site.key.sourceSpan.clone(), factPath(site.primaryGroup));
        }
        ZC_IF_SOME(type, sourceType) {
          auto lookup = input.semanticTypes.get(type);
          if (!lookup.is<type::SemanticTypeLookup>() ||
              !lookup.get<type::SemanticTypeLookup>()
                   .data()
                   .is<type::semantic::ReferenceTypeData>()) {
            return rejectInvariant(signature::CheckerInvariantKind::InvalidFact, module,
                                   site.key.schemaPreorder, zc::none, site.node,
                                   site.key.sourceSpan.clone(), factPath(site.primaryGroup));
          }
          producedType = lookup.get<type::SemanticTypeLookup>()
                             .data()
                             .get<type::semantic::ReferenceTypeData>()
                             .referent;
        }
      } else if (site.production == BodyProductionKind::ReferenceReborrow) {
        auto shape = referenceReborrowShape(input, site.node, nodeTypes.asPtr());
        if (shape == zc::none) {
          return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, module,
                                 site.key.schemaPreorder, zc::none, site.node,
                                 site.key.sourceSpan.clone(), factPath(site.primaryGroup));
        }
        ZC_IF_SOME(value, shape) { producedType = value.sourceType; }
      } else if (site.production == BodyProductionKind::LocalBorrow) {
        auto shape = localBorrowShape(input, site.node, nodeTypes.asPtr());
        if (shape == zc::none) {
          return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, module,
                                 site.key.schemaPreorder, zc::none, site.node,
                                 site.key.sourceSpan.clone(), factPath(site.primaryGroup));
        }
        ZC_IF_SOME(value, shape) { producedType = value.type; }
      } else if (site.production == BodyProductionKind::StructLiteral) {
        auto shape = structLiteralShape(input, site.node, nodeTypes.asPtr());
        if (shape == zc::none) {
          return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, module,
                                 site.key.schemaPreorder, zc::none, site.node,
                                 site.key.sourceSpan.clone(),
                                 factPath(CheckedFactGroup::Aggregate));
        }
        ZC_IF_SOME(value, shape) {
          producedType = value.type;
          aggregates.add(checked::AggregateFactMap::Entry{
              site.node,
              checked::CheckedAggregateFact{
                  site.node, checked::AggregateKind(checked::NominalAggregate{value.definition}),
                  value.type, zc::mv(value.elements), site.key.sourceSpan.clone()},
              zc::Array<uint8_t>()});
        }
      } else if (site.production == BodyProductionKind::ErrorOperator) {
        const auto& postfix = input.boundModule.tree().node(site.node);
        const auto operation = static_cast<ast::PostfixOperatorKind>(
            postfix.payload.words[ast::kPostfixExpressionOpWord]);
        const ast::NodeId operand(postfix.payload.words[ast::kPostfixExpressionOperandWord]);
        auto operandFact = factEntry(nodeTypes.asPtr(), operand);
        auto callable = returnValueOwner(input.boundModule, site.node);
        if (operandFact == zc::none || callable == zc::none) {
          return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, module,
                                 site.key.schemaPreorder, zc::none, site.node,
                                 site.key.sourceSpan.clone(), factPath(site.primaryGroup));
        }
        ZC_IF_SOME(operand, operandFact) {
          ZC_IF_SOME(owner, callable) {
            auto ownerPreorder = definitionPreorder(input.boundModule, owner);
            if (ownerPreorder == zc::none) {
              return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, module,
                                     site.key.schemaPreorder, owner, site.node,
                                     site.key.sourceSpan.clone(), factPath(site.primaryGroup));
            }
            ZC_IF_SOME(ownerOrdinal, ownerPreorder) {
              return attachRecoveryLedger(
                  rejectNonUnionErrorOperator(site, ownerOrdinal, operand.value, operation), input,
                  factStoreBrands);
            }
          }
        }
        return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, module,
                               site.key.schemaPreorder, zc::none, site.node,
                               site.key.sourceSpan.clone(), factPath(site.primaryGroup));
      } else {
        auto emitted = scalar_literal::FactEmitter::emit(scalar_literal::FactEmissionInput{
            context, module, input.boundModule.tree(), site.node, site.key,
            input.boundModule.parsedModule().source(), input.identities, input.semanticTypes});
        if (emitted.is<checked::CheckedFactsInvariantRejected>()) {
          return zc::mv(emitted).get<checked::CheckedFactsInvariantRejected>();
        }
        if (emitted.is<checked::CheckedFactsSourceRejected>()) {
          return attachRecoveryLedger(zc::mv(emitted).get<checked::CheckedFactsSourceRejected>(),
                                      input, factStoreBrands);
        }
        auto facts = zc::mv(emitted).get<scalar_literal::EmittedFacts>();
        producedType = facts.nodeType.value;
        literals.add(zc::mv(facts.literal));
      }
      auto callable = returnValueOwner(input.boundModule, site.node);
      ZC_IF_SOME(owner, callable) {
        auto expected = callableSuccess(input.signatureFacts, owner);
        auto ownerPreorder = definitionPreorder(input.boundModule, owner);
        if (expected == zc::none || ownerPreorder == zc::none) {
          return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, module,
                                 site.key.schemaPreorder, owner, site.node,
                                 site.key.sourceSpan.clone(), factPath(CheckedFactGroup::NodeType));
        }
        ZC_IF_SOME(expectedType, expected) {
          ZC_IF_SOME(ownerOrdinal, ownerPreorder) {
            if (producedType == zc::none || ZC_ASSERT_NONNULL(producedType) != expectedType) {
              return attachRecoveryLedger(rejectTypeMismatch(site, ownerOrdinal, expectedType,
                                                             ZC_ASSERT_NONNULL(producedType)),
                                          input, factStoreBrands);
            }
          }
        }
      }
      ZC_IF_SOME(value, producedType) {
        nodeTypes.add(checked::NodeTypeMap::Entry{site.node, value, zc::Array<uint8_t>()});
      }
    }
  }

  for (const auto& site : input.requirements.impl->productionSiteValues) {
    if (site.production != BodyProductionKind::LocalWrite &&
        site.production != BodyProductionKind::OwnerLocalFieldWrite) {
      continue;
    }
    const auto& assignment = input.boundModule.tree().node(site.node);
    const ast::NodeId target(assignment.payload.words[ast::kAssignmentExprLhsWord]);
    const ast::NodeId value(assignment.payload.words[ast::kAssignmentExprRhsWord]);
    auto targetType = factEntry(nodeTypes.asPtr(), target);
    auto valueType = factEntry(nodeTypes.asPtr(), value);
    if (targetType == zc::none || valueType == zc::none) {
      return rejectInvariant(signature::CheckerInvariantKind::InvalidFact, module,
                             site.key.schemaPreorder, zc::none, site.node,
                             site.key.sourceSpan.clone(), factPath(CheckedFactGroup::NodeType));
    }
    ZC_IF_SOME(target, targetType) {
      ZC_IF_SOME(valueEntry, valueType) {
        if (target.value != valueEntry.value) {
          return rejectInvariant(signature::CheckerInvariantKind::InvalidFact, module,
                                 site.key.schemaPreorder, zc::none, site.node,
                                 site.key.sourceSpan.clone(), factPath(CheckedFactGroup::NodeType));
        }
      }
    }
    if (site.production == BodyProductionKind::OwnerLocalFieldWrite) {
      auto targetPlace = factEntry(places.asPtr(), target);
      if (targetPlace == zc::none) {
        return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, module,
                               site.key.schemaPreorder, zc::none, site.node,
                               site.key.sourceSpan.clone(), factPath(CheckedFactGroup::Place));
      }
      ZC_IF_SOME(place, targetPlace) {
        if (!place.value.mutablePlace) {
          return rejectInvariant(signature::CheckerInvariantKind::InvalidFact, module,
                                 site.key.schemaPreorder, zc::none, site.node,
                                 site.key.sourceSpan.clone(), factPath(CheckedFactGroup::Place));
        }
      }
    }
  }

  const auto& tree = input.boundModule.tree();
  const auto definitionInventory = binder::DefinitionInventory::collect(tree);
  for (const auto& definition : input.boundModule.definitions().definitions()) {
    if (!hasDefinitionRequirement(input.requirements.definitionRequirements(),
                                  definition.definition)) {
      continue;
    }
    auto bindingSite = patternBindingSite(definitionInventory, definition);
    if (bindingSite == zc::none) {
      return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, module, 0,
                             definition.definition, definition.node, definition.source.clone(),
                             factPath(CheckedFactGroup::DefinitionType));
    }
    ZC_IF_SOME(site, bindingSite) {
      if (site.patternPath.size() != 0 || !tree.contains(site.introducer) ||
          tree.node(site.introducer).kind != ast::SyntaxKind::VariableDeclarator) {
        return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, module, 0,
                               definition.definition, definition.node, definition.source.clone(),
                               factPath(CheckedFactGroup::Pattern));
      }
      const auto& declarator = tree.node(site.introducer);
      const ast::NodeId pattern(declarator.payload.words[ast::kVariableDeclaratorPatternWord]);
      const ast::NodeId initializer(declarator.payload.words[ast::kVariableDeclaratorInitWord]);
      if (!tree.contains(pattern) ||
          tree.node(pattern).kind != ast::SyntaxKind::IdentifierPattern ||
          !tree.contains(initializer)) {
        return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, module, 0,
                               definition.definition, site.introducer, definition.source.clone(),
                               factPath(CheckedFactGroup::DefinitionType));
      }
      auto initializerType = factEntry(nodeTypes.asPtr(), initializer);
      auto initializerLiteral = factEntry(literals.asPtr(), initializer);
      auto initializerAggregate = factEntry(aggregates.asPtr(), initializer);
      auto bindingProduction =
          productionSite(input.requirements.impl->productionSiteValues.asPtr(), pattern);
      auto declaredType = valueType(input.signatureFacts, definition.definition);
      auto ownerPreorder = definitionPreorder(input.boundModule, definition.definition);
      if (initializerType == zc::none ||
          (initializerLiteral == zc::none && initializerAggregate == zc::none) ||
          bindingProduction == zc::none || declaredType == zc::none || ownerPreorder == zc::none) {
        return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, module, 0,
                               definition.definition, initializer, definition.source.clone(),
                               factPath(CheckedFactGroup::DefinitionType));
      }

      ZC_IF_SOME(typeEntry, initializerType) {
        ZC_IF_SOME(expectedType, declaredType) {
          ZC_IF_SOME(ownerOrdinal, ownerPreorder) {
            if (typeEntry.value != expectedType) {
              const auto site = productionSite(
                  input.requirements.impl->productionSiteValues.asPtr(), initializer);
              if (site == zc::none) {
                return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, module,
                                       0, definition.definition, initializer,
                                       definition.source.clone(),
                                       factPath(CheckedFactGroup::DefinitionType));
              }
              ZC_IF_SOME(value, site) {
                return attachRecoveryLedger(
                    rejectTypeMismatch(value, ownerOrdinal, expectedType, typeEntry.value), input,
                    factStoreBrands);
              }
            }
            definitionTypes.add(checked::DefinitionTypeMap::Entry{
                definition.definition, expectedType, zc::Array<uint8_t>()});
            ZC_IF_SOME(patternSite, bindingProduction) {
              auto pattern =
                  identifierPatternFact(patternSite, definition.definition, expectedType);
              if (pattern == zc::none) {
                return rejectInvariant(signature::CheckerInvariantKind::CanonicalCodecMismatch,
                                       module, 0, definition.definition, definition.node,
                                       definition.source.clone(),
                                       factPath(CheckedFactGroup::Pattern));
              }
              ZC_IF_SOME(value, pattern) { patterns.add(zc::mv(value)); }
            }
          }
        }
      }
      if (definition.record.kind() == identity::DefinitionKind::Constant) {
        if (initializerLiteral == zc::none) {
          return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, module, 0,
                                 definition.definition, initializer, definition.source.clone(),
                                 factPath(CheckedFactGroup::Constant));
        }
        ZC_IF_SOME(literalEntry, initializerLiteral) {
          constants.add(scalarConstantFact(definition.definition, initializer, literalEntry));
        }
      }
    }
  }

  for (const auto& requirement : input.requirements.nodeRequirements()) {
    if (requirement.group != CheckedFactGroup::NodeType &&
        requirement.group != CheckedFactGroup::Literal &&
        requirement.group != CheckedFactGroup::Aggregate &&
        requirement.group != CheckedFactGroup::Call &&
        requirement.group != CheckedFactGroup::Place &&
        requirement.group != CheckedFactGroup::Member &&
        requirement.group != CheckedFactGroup::Index &&
        requirement.group != CheckedFactGroup::MarkerObligation &&
        requirement.group != CheckedFactGroup::Pattern) {
      return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, module,
                             requirement.key.schemaPreorder, zc::none, requirement.node,
                             requirement.key.sourceSpan.clone(), factPath(requirement.group));
    }
  }
  if (nodeTypes.size() !=
          requirementCount(input.requirements.nodeRequirements(), CheckedFactGroup::NodeType) ||
      literals.size() !=
          requirementCount(input.requirements.nodeRequirements(), CheckedFactGroup::Literal) ||
      aggregates.size() !=
          requirementCount(input.requirements.nodeRequirements(), CheckedFactGroup::Aggregate) ||
      calls.size() !=
          requirementCount(input.requirements.nodeRequirements(), CheckedFactGroup::Call) ||
      places.size() !=
          requirementCount(input.requirements.nodeRequirements(), CheckedFactGroup::Place) ||
      members.size() !=
          requirementCount(input.requirements.nodeRequirements(), CheckedFactGroup::Member) ||
      indexes.size() !=
          requirementCount(input.requirements.nodeRequirements(), CheckedFactGroup::Index) ||
      markerObligations.size() != requirementCount(input.requirements.nodeRequirements(),
                                                   CheckedFactGroup::MarkerObligation) ||
      patterns.size() !=
          requirementCount(input.requirements.nodeRequirements(), CheckedFactGroup::Pattern) ||
      definitionTypes.size() !=
          definitionRequirementCount(input.requirements.definitionRequirements(),
                                     CheckedFactGroup::DefinitionType) ||
      constants.size() != definitionRequirementCount(input.requirements.definitionRequirements(),
                                                     CheckedFactGroup::Constant)) {
    return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, module, 0);
  }
  auto substitutionBrand = factStoreBrands.issue();
  auto witnessBrand = factStoreBrands.issue();
  if (substitutionBrand == zc::none || witnessBrand == zc::none) {
    return rejectInvariant(signature::CheckerInvariantKind::InferenceLifecycle, module, 0);
  }
  zc::Maybe<checked::FrozenSubstitutionStore> substitutions;
  zc::Maybe<checked::FrozenWitnessStore> witnesses;
  ZC_IF_SOME(brand, substitutionBrand) {
    zc::Vector<checked::FrozenSubstitutionStore::Record> records;
    substitutions = checked::FrozenSubstitutionStore::from(context, brand, zc::mv(records));
  }
  ZC_IF_SOME(brand, witnessBrand) {
    zc::Vector<checked::FrozenWitnessStore::Record> records;
    witnesses = checked::FrozenWitnessStore::from(context, brand, zc::mv(records));
  }
  if (substitutions == zc::none || witnesses == zc::none) {
    return rejectInvariant(signature::CheckerInvariantKind::InferenceLifecycle, module, 0);
  }

  ZC_IF_SOME(substitutionStore, substitutions) {
    ZC_IF_SOME(witnessStore, witnesses) {
      checked::CheckedFactsCandidate candidate{
          context,
          input.boundModule.semanticFingerprint().clone(),
          module,
          parsedModule.contentDigest(),
          parsedModule.receipt(),
          input.signatureFacts.revision(),
          input.importedSignatures.revision(),
          input.coherence.revision(),
          input.semanticOptions,
          zc::mv(substitutionStore),
          zc::mv(witnessStore),
          checked::NodeTypeMap::fromEntries(zc::mv(nodeTypes)),
          checked::DefinitionTypeMap::fromEntries(zc::mv(definitionTypes)),
          checked::LiteralFactMap::fromEntries(zc::mv(literals)),
          checked::ConstantFactMap::fromEntries(zc::mv(constants)),
          checked::AggregateFactMap::fromEntries(zc::mv(aggregates)),
          checked::PlaceFactMap::fromEntries(zc::mv(places)),
          emptyFactMap<checked::CoercionFactMap>(),
          emptyFactMap<checked::CastFactMap>(),
          checked::CallFactMap::fromEntries(zc::mv(calls)),
          emptyFactMap<checked::CompoundAssignmentFactMap>(),
          checked::MemberFactMap::fromEntries(zc::mv(members)),
          checked::IndexFactMap::fromEntries(zc::mv(indexes)),
          checked::PatternFactMap::fromEntries(zc::mv(patterns)),
          emptyFactMap<checked::ObservedOperationFactMap>(),
          emptyFactMap<checked::CaptureFactMap>(),
          checked::MarkerObligationFactMap::fromEntries(zc::mv(markerObligations)),
          emptyFactMap<checked::ExhaustivenessFactMap>(),
          emptyFactMap<checked::UnsafeOperationFactMap>(),
          emptyFactMap<checked::ProjectionFactMap>(),
          emptyFactMap<checked::ObligationFactMap>(),
          emptyFactMap<checked::ErrorUnionShapeFactMap>(),
          emptyFactMap<checked::ErrorOperatorFactMap>(),
          zc::Vector<checked::FrozenRecoveryLedger>(),
          zc::Vector<checked::CheckerFailureRef>(),
          zc::Vector<checked::CheckerAdvisoryRef>()};

      zc::Vector<identity::DefId> importedDefinitions;
      for (const auto& importedModule : input.importedSignatures.modules()) {
        for (const auto& definition : importedModule.lookupDefinitions()) {
          bool duplicate = false;
          for (const auto existing : importedDefinitions) {
            if (existing == definition.definition) {
              duplicate = true;
              break;
            }
          }
          if (!duplicate) importedDefinitions.add(definition.definition);
        }
      }
      zc::Vector<identity::ImplId> coherentImpls(input.coherence.implHeads().size());
      for (const auto& implementation : input.coherence.implHeads()) {
        coherentImpls.add(implementation.impl);
      }
      const checked::CheckedFactsVerificationInput codecInput{
          context,
          input.boundModule.semanticFingerprint(),
          module,
          parsedModule.source(),
          parsedModule.contentDigest(),
          parsedModule.receipt(),
          input.signatureFacts.revision(),
          input.importedSignatures.revision(),
          input.coherence.revision(),
          input.semanticOptions,
          input.requirements.nodeRequirements(),
          input.requirements.definitionRequirements(),
          input.requirements.captureRequirements(),
          importedDefinitions.asPtr(),
          coherentImpls.asPtr(),
          candidate.sourceFailures.asPtr(),
          input.boundModule.definitions().ownerLocalBindings(),
          input.boundModule.definitions().anonymousEntities(),
          input.identities,
          input.semanticTypes};
      if (!checked::CheckedFactsCanonicalCodec::writeCanonicalRecords(candidate, codecInput)) {
        return rejectInvariant(signature::CheckerInvariantKind::CanonicalCodecMismatch, module, 0);
      }
      return zc::mv(candidate);
    }
  }
  ZC_UNREACHABLE
}

}  // namespace zomlang::compiler::checker::body
