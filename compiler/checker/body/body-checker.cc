// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/checker/body/body-checker.h"

#include <cstdint>

#include "compiler/ast/generated/node-payload.h"
#include "compiler/ast/generated/node-traverse.h"
#include "compiler/binder/metadata/definition-inventory.h"
#include "compiler/checker/body/marker-proof.h"
#include "compiler/checker/facts/scalar-literal-facts.h"
#include "compiler/checker/inference/inference-recovery-context.h"
#include "compiler/checker/operator-kind.h"
#include "compiler/driver/core/marker-authority.h"
#include "compiler/type/semantic-type-data.h"

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
  ReceiverFieldWrite = 0x1a,
  PrimitiveUnaryOperation = 0x1b,
  IntegerCast = 0x1c,
  ConditionalExpression = 0x1d,
  PostfixIncrement = 0x1e,
  EnumVariantValue = 0x1f,
  EnumVariantConstruction = 0x20,
  MatchExpression = 0x21,
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

// A pattern inside a match arm is covered by the arm's exhaustiveness fact, not
// by a standalone pattern fact. The body-checker only produces pattern facts
// for identifier patterns in variable declarators, so match-arm patterns must
// not enter the requirement/production inventory at all.
bool isMatchArmPattern(const ast::Tree& tree, ast::NodeId node) {
  bool found = false;
  ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId, const ast::Node& syntax) {
    if ((syntax.kind == ast::SyntaxKind::MatchArmStmt &&
         ast::NodeId(syntax.payload.words[ast::kMatchArmStmtPatternWord]) == node) ||
        (syntax.kind == ast::SyntaxKind::MatchArmExpr &&
         ast::NodeId(syntax.payload.words[ast::kMatchArmExprPatternWord]) == node)) {
      found = true;
    }
  });
  return found;
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
      // An owner local may be declared inside a function or a method body. The
      // later HIR method gate drains every non-admitted method body per
      // definition (ZOM4099), so accepting the pattern here only removes a
      // spurious fact requirement; it does not admit the body to lowering.
      // Constructors and destructors stay out until their HIR gates exist.
      const bool isFunction = definition.record.kind() == identity::DefinitionKind::Function &&
                              tree.contains(definition.node) &&
                              tree.node(definition.node).kind == ast::SyntaxKind::FunctionDecl;
      const bool isMethod = definition.record.kind() == identity::DefinitionKind::Method &&
                            tree.contains(definition.node) &&
                            tree.node(definition.node).kind == ast::SyntaxKind::MethodDecl;
      if (!isFunction && !isMethod) continue;
      const auto bodyWord = isFunction ? ast::kFunctionDeclBodyWord : ast::kMethodDeclBodyWord;
      const ast::NodeId body(tree.node(definition.node).payload.words[bodyWord]);
      if (tree.contains(body) && subtreeContains(tree, body, node)) { return true; }
    }
  }
  return false;
}

/// \brief True for a definition kind that owns an executable body.
bool ownsExecutableBody(identity::DefinitionKind kind) noexcept {
  switch (kind) {
    case identity::DefinitionKind::Function:
    case identity::DefinitionKind::Method:
    case identity::DefinitionKind::Constructor:
    case identity::DefinitionKind::Destructor:
      return true;
    default:
      return false;
  }
}

/// \brief Returns the innermost body-owning definition whose subtree contains
/// `node`.
///
/// Depth is the definition's owner-chain length, so the deepest match wins. Two
/// distinct definitions at the same depth would make the owner ambiguous, which
/// fails closed with none rather than guessing.
///
/// This admits every kind that owns a body, not just `Function`. A source
/// diagnostic needs an owner to build its recovery root, so restricting this to
/// `Function` silently disabled every owner-scoped diagnostic inside a method,
/// constructor, or destructor -- the refusal fell through to the invariant rail
/// with a fully diagnosed user error already in hand.
zc::Maybe<identity::DefId> enclosingBodyOwner(
    const driver::module_graph_query::CheckerBoundModuleView& boundModule, ast::NodeId node) {
  const auto& tree = boundModule.tree();
  zc::Maybe<identity::DefId> result;
  size_t bestDepth = 0;
  for (const auto& definition : boundModule.definitions().definitions()) {
    if (!ownsExecutableBody(definition.record.kind()) ||
        !subtreeContains(tree, definition.node, node)) {
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

/// \brief True when the binder attached an executable owner body to the
/// definition. A bodyless interface requirement method has none.
///
/// Mirrors hir::detail::hasExecutableBody; the checker cannot depend on the HIR
/// internal header, so the owner-body walk is duplicated here.
bool hasExecutableOwnerBody(const driver::module_graph_query::CheckerBoundModuleView& boundModule,
                            const binder::MaterializedDefinitionInventoryEntry& definition) {
  for (const auto& body : boundModule.definitions().ownerBodies()) {
    const auto& owner = body.owner().owner();
    if (owner.kind() == binder::StableBodyOwnerKind::Definition) {
      ZC_IF_SOME(key, owner.definitionKey()) {
        if (key == definition.key) { return true; }
      }
      continue;
    }
    if (definition.record.owners().size() == 0) { return true; }
  }
  return false;
}

/// \brief True when a method definition is a concrete method with a body
/// supplied by a standalone `impl Interface for Type` block (an Implementation
/// stable owner). Interface requirement declarations are not impl-owned;
/// inherent struct/class methods are owned by their nominal, not by an impl.
bool providedByImplWithBody(const driver::module_graph_query::CheckerBoundModuleView& boundModule,
                            const binder::MaterializedDefinitionInventoryEntry& definition) {
  if (definition.record.kind() != identity::DefinitionKind::Method ||
      !hasExecutableOwnerBody(boundModule, definition)) {
    return false;
  }
  for (const auto& owner : definition.record.owners()) {
    if (owner.kind() == identity::EnclosingStableOwnerKind::Implementation) { return true; }
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

  return enclosingBodyOwner(boundModule, value);
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

zc::Maybe<identity::SemanticTypeId> ownerLocalInitializerDeclaredType(
    const driver::module_graph_query::CheckerBoundModuleView& boundModule,
    const CheckerIdentityAuthority& identities, type::SemanticTypeStore& semanticTypes,
    ast::NodeId initializer);

/// \brief Structurally derives the result type of a primitive-binary
/// initializer for an unannotated local, without reading any node-type fact.
///
/// A reference operand contributes its parameter annotation or earlier-local
/// type (which recursively follows another unannotated initializer chain); a
/// comparison yields bool and an arithmetic or bitwise operation yields the
/// operand type. Defined after the operator and primitive-kind helpers below.
zc::Maybe<identity::SemanticTypeId> unannotatedBinaryInitializerType(
    const BodyCheckingInput& input, ast::NodeId binary,
    zc::ArrayPtr<const checked::NodeTypeMap::Entry> nodeTypes, unsigned depth);

zc::Maybe<identity::SemanticTypeId> ownerLocalReferenceType(
    const BodyCheckingInput& input, ast::NodeId node,
    zc::ArrayPtr<const checked::NodeTypeMap::Entry> nodeTypes) {
  const auto binding = resolvedOwnerLocal(input.boundModule.bindings(), node);
  if (binding == zc::none) return zc::none;
  ZC_IF_SOME(value, binding) {
    const auto initializer = ownerLocalInitializer(input.boundModule, value);
    const auto& tree = input.boundModule.tree();
    ZC_IF_SOME(initializerNode, initializer) {
      // An annotated owner local binds to its declared annotation type. The
      // definition-type production independently verifies the initializer
      // meets that annotation (a concrete-to-dyn erasure coerces at the
      // initializer site), so every later reference must see the bound
      // annotation rather than the initializer's raw type. Reading the
      // initializer node type here would let a reference to an erased
      // `let a: dyn I = c` flow as the concrete type, fabricating a second
      // erasure or accepting an unsound dyn-to-concrete move.
      auto declared = ownerLocalInitializerDeclaredType(input.boundModule, input.identities,
                                                        input.semanticTypes, initializerNode);
      if (declared != zc::none) { return declared; }
      for (const auto& entry : nodeTypes) {
        if (entry.key == initializerNode) return entry.value;
      }
      // The initializer's node type is not yet produced. A primitive-binary
      // initializer is typed one stage after a bare identifier reference. An
      // annotated binding was resolved from its annotation above; for an
      // unannotated binding derive the binary's result structurally from its
      // operand leaves, so an earlier reference resolves before the binary's own
      // node-type fact exists. The binary production independently recomputes
      // and verifies the same result type, so the two agree. A non-binary
      // initializer is always typed before its uses in schema preorder, so this
      // fallback is reached only for the binary case.
      if (tree.contains(initializerNode) &&
          tree.node(initializerNode).kind == ast::SyntaxKind::BinaryExpr) {
        return unannotatedBinaryInitializerType(input, initializerNode, nodeTypes, 0);
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

// Returns true when `node` is an IdentifierReference whose binding's
// initializer is a conditional or match expression.  These initializers
// produce their NodeType fact in stage 1, so the referencing site must be
// deferred to stage 2 alongside structured and call initializers.
bool dependsOnTernaryLocalInitializer(const BodyCheckingInput& input, ast::NodeId node) {
  const auto binding = resolvedOwnerLocal(input.boundModule.bindings(), node);
  if (binding == zc::none) return false;
  ZC_IF_SOME(value, binding) {
    auto initializer = ownerLocalInitializer(input.boundModule, value);
    ZC_IF_SOME(initializerNode, initializer) {
      const auto& tree = input.boundModule.tree();
      if (!tree.contains(initializerNode)) return false;
      const auto kind = tree.node(initializerNode).kind;
      return kind == ast::SyntaxKind::ConditionalExpr || kind == ast::SyntaxKind::MatchExpr;
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
    const driver::module_graph_query::CheckerBoundModuleView& boundModule,
    const CheckerIdentityAuthority& identities, type::SemanticTypeStore& semanticTypes,
    ast::NodeId initializer) {
  const auto& tree = boundModule.tree();
  for (const auto& local : boundModule.definitions().ownerLocalBindings()) {
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
    return signature::resolveClosedSourceType(boundModule, identities, semanticTypes, annotation);
  }
  return zc::none;
}

// A selected concrete-to-dyn erasure at one annotated initializer site. The
// slice admits only a non-generic nominal concrete type erased to a bare
// object-safe principal interface with matching associated bindings and no
// marker or additional-interface requirements.
struct DynErasePlan final {
  identity::DefId interface;
  identity::ImplId impl;
  zc::Vector<checked::AssociatedTypeBindingData> bindings;
};

// Reads the existential payload of `type` when it is in the erasable slice
// shape, or none for every other type form.
zc::Maybe<const type::semantic::ExistentialTypeData&> erasableExistentialType(
    type::SemanticTypeStore& semanticTypes, identity::SemanticTypeId type) {
  auto lookup = semanticTypes.get(type);
  if (!lookup.is<type::SemanticTypeLookup>()) { return zc::none; }
  const auto& data = lookup.get<type::SemanticTypeLookup>().data();
  if (!data.is<type::semantic::ExistentialTypeData>()) { return zc::none; }
  const auto& existential = data.get<type::semantic::ExistentialTypeData>();
  if (!existential.principal.arguments.empty() || existential.additionalInterfaces.size() != 0 ||
      existential.markers.size() != 0) {
    return zc::none;
  }
  return existential;
}

// Resolves the return type annotation of a function definition, or none when
// the definition is not a function declaration with a return annotation.
zc::Maybe<identity::SemanticTypeId> functionReturnType(
    const driver::module_graph_query::CheckerBoundModuleView& boundModule,
    const CheckerIdentityAuthority& identities, type::SemanticTypeStore& semanticTypes,
    identity::DefId function) {
  const auto& tree = boundModule.tree();
  zc::Maybe<ast::NodeId> functionNode;
  for (const auto& definition : boundModule.definitions().definitions()) {
    if (definition.definition == function) {
      functionNode = definition.node;
      break;
    }
  }
  if (functionNode == zc::none || !tree.contains(ZC_ASSERT_NONNULL(functionNode)) ||
      tree.node(ZC_ASSERT_NONNULL(functionNode)).kind != ast::SyntaxKind::FunctionDecl) {
    return zc::none;
  }
  const ast::NodeId returnAnnotation(
      tree.node(ZC_ASSERT_NONNULL(functionNode)).payload.words[ast::kFunctionDeclRetTyWord]);
  if (!tree.contains(returnAnnotation)) { return zc::none; }
  return signature::resolveClosedSourceType(boundModule, identities, semanticTypes,
                                            returnAnnotation);
}

// Resolves the return type of a direct call's callee, or none when the callee
// is not a resolvable function declaration.
zc::Maybe<identity::SemanticTypeId> directCallReturnType(
    const driver::module_graph_query::CheckerBoundModuleView& boundModule,
    const CheckerIdentityAuthority& identities, type::SemanticTypeStore& semanticTypes,
    ast::NodeId callNode) {
  const auto& tree = boundModule.tree();
  if (!tree.contains(callNode) || tree.node(callNode).kind != ast::SyntaxKind::CallExpression) {
    return zc::none;
  }
  const ast::NodeId callee(tree.node(callNode).payload.words[ast::kCallExpressionCalleeWord]);
  if (!tree.contains(callee) || tree.node(callee).kind != ast::SyntaxKind::IdentExpr) {
    return zc::none;
  }
  auto callable = resolvedDefinition(boundModule.bindings(), callee);
  if (callable == zc::none) { return zc::none; }
  return functionReturnType(boundModule, identities, semanticTypes, ZC_ASSERT_NONNULL(callable));
}

// Statically decides whether an identifier initializer already carries the
// declared erasable existential, making the assignment an identity copy that
// needs no DynErase. It mirrors the producer's identifier type resolution
// without depending on facts produced later, reading every terminal type from
// its closed source annotation (the same resolver that produces `declared`,
// so equal annotations intern to equal type ids):
//   - a callable parameter contributes its parameter annotation;
//   - an annotated owner local contributes its annotation;
//   - an unannotated local transparently follows its initializer chain (a bare
//     reference or a direct call returning the declared type).
// Returns true only on a proven same-existential source; every unprovable
// shape is treated as a potential erasure so the requirement never under-counts
// a coercion the producer actually records.
bool isIdentityExistentialInitializer(
    const driver::module_graph_query::CheckerBoundModuleView& boundModule,
    const CheckerIdentityAuthority& identities, type::SemanticTypeStore& semanticTypes,
    ast::NodeId node, identity::SemanticTypeId declared, unsigned depth) {
  constexpr unsigned kMaxChainDepth = 64;
  if (depth > kMaxChainDepth) { return false; }
  const auto& tree = boundModule.tree();
  if (!tree.contains(node) || tree.node(node).kind != ast::SyntaxKind::IdentExpr) { return false; }

  // Callable parameter terminal: read the parameter declaration annotation.
  auto parameter = resolvedCallableParameter(boundModule.bindings(), node);
  if (parameter != zc::none) {
    ZC_IF_SOME(handle, parameter) {
      for (const auto& entry : boundModule.definitions().callableParameters()) {
        if (entry.parameter != handle) { continue; }
        if (!tree.contains(entry.node) ||
            tree.node(entry.node).kind != ast::SyntaxKind::FunctionParameterDecl) {
          return false;
        }
        const ast::NodeId annotation(
            tree.node(entry.node).payload.words[ast::kFunctionParameterDeclTyWord]);
        if (!tree.contains(annotation)) { return false; }
        auto parameterType =
            signature::resolveClosedSourceType(boundModule, identities, semanticTypes, annotation);
        return parameterType != zc::none && ZC_ASSERT_NONNULL(parameterType) == declared;
      }
    }
    return false;
  }

  // Owner-local terminal: an annotated local contributes its annotation; an
  // unannotated one is transparent and the chain continues through its
  // initializer.
  auto binding = resolvedOwnerLocal(boundModule.bindings(), node);
  if (binding == zc::none) { return false; }
  ZC_IF_SOME(ownerLocal, binding) {
    const auto initializer = ownerLocalInitializer(boundModule, ownerLocal);
    if (initializer == zc::none) { return false; }
    const ast::NodeId initializerNode = ZC_ASSERT_NONNULL(initializer);
    auto annotated =
        ownerLocalInitializerDeclaredType(boundModule, identities, semanticTypes, initializerNode);
    if (annotated != zc::none) { return ZC_ASSERT_NONNULL(annotated) == declared; }
    if (!tree.contains(initializerNode)) { return false; }
    const auto& initializerSyntax = tree.node(initializerNode);
    if (initializerSyntax.kind == ast::SyntaxKind::IdentExpr) {
      return isIdentityExistentialInitializer(boundModule, identities, semanticTypes,
                                              initializerNode, declared, depth + 1);
    }
    if (initializerSyntax.kind == ast::SyntaxKind::CallExpression) {
      auto returnType =
          directCallReturnType(boundModule, identities, semanticTypes, initializerNode);
      return returnType != zc::none && ZC_ASSERT_NONNULL(returnType) == declared;
    }
  }
  return false;
}

// Statically decides whether a return expression already carries the enclosing
// function's erasable existential return type, making the return an identity
// copy that needs no DynErase. Reuses the identifier-chain identity test for
// identifier returns and extends it to direct call results.
bool isIdentityExistentialReturn(
    const driver::module_graph_query::CheckerBoundModuleView& boundModule,
    const CheckerIdentityAuthority& identities, type::SemanticTypeStore& semanticTypes,
    ast::NodeId node, identity::SemanticTypeId declared, unsigned depth) {
  constexpr unsigned kMaxChainDepth = 64;
  if (depth > kMaxChainDepth) { return false; }
  const auto& tree = boundModule.tree();
  if (!tree.contains(node)) { return false; }
  const auto& syntax = tree.node(node);
  if (syntax.kind == ast::SyntaxKind::IdentExpr) {
    return isIdentityExistentialInitializer(boundModule, identities, semanticTypes, node, declared,
                                            depth);
  }
  if (syntax.kind == ast::SyntaxKind::CallExpression) {
    auto returnType = directCallReturnType(boundModule, identities, semanticTypes, node);
    return returnType != zc::none && ZC_ASSERT_NONNULL(returnType) == declared;
  }
  return false;
}

// Selects the unique non-generic impl that erases `concrete` to the
// existential's principal interface. Returns none when no impl applies; the
// frozen coherent view guarantees at most one impl for a concrete self type.
zc::Maybe<identity::ImplId> selectDynEraseImpl(
    const BodyCheckingInput& input, identity::SemanticTypeId concrete,
    const type::semantic::ExistentialTypeData& existential) {
  auto concreteLookup = input.semanticTypes.get(concrete);
  if (!concreteLookup.is<type::SemanticTypeLookup>()) { return zc::none; }
  const auto& concreteData = concreteLookup.get<type::SemanticTypeLookup>().data();
  if (!concreteData.is<type::semantic::NominalTypeData>()) { return zc::none; }
  const auto& nominal = concreteData.get<type::semantic::NominalTypeData>();
  if (!nominal.arguments.empty()) { return zc::none; }
  zc::Maybe<identity::ImplId> selected;
  for (const auto& head : input.coherence.implHeads()) {
    if (head.selfType != concrete) { continue; }
    if (head.genericParameters.size() != 0) { continue; }
    if (signature::SignatureFactsCanonicalCodec::implPatternInterface(head.pattern) !=
        existential.principal.definition) {
      continue;
    }
    bool bindingsMatch = head.associatedBindings.size() == existential.associatedBindings.size();
    if (bindingsMatch) {
      for (const auto& required : existential.associatedBindings) {
        bool found = false;
        for (const auto& provided : head.associatedBindings) {
          if (provided.associated == required.associated && provided.type == required.type) {
            found = true;
            break;
          }
        }
        if (!found) {
          bindingsMatch = false;
          break;
        }
      }
    }
    if (!bindingsMatch) { continue; }
    if (selected != zc::none) { return zc::none; }
    selected = head.impl;
  }
  return selected;
}

// True when coherence already has an impl of the existential's principal
// interface for the concrete nominal's definition, regardless of whether the
// concrete type or impl is generic. Used to tell a genuine missing obligation
// (no impl exists -> ZOM4018) apart from an impl that the non-generic erasure
// slice cannot lower yet (-> a semantics-unavailable diagnostic).
bool hasImplForErasureOutsideSlice(const BodyCheckingInput& input,
                                   identity::SemanticTypeId concrete,
                                   const type::semantic::ExistentialTypeData& existential) {
  auto concreteLookup = input.semanticTypes.get(concrete);
  if (!concreteLookup.is<type::SemanticTypeLookup>()) { return false; }
  const auto& concreteData = concreteLookup.get<type::SemanticTypeLookup>().data();
  if (!concreteData.is<type::semantic::NominalTypeData>()) { return false; }
  const auto concreteDefinition = concreteData.get<type::semantic::NominalTypeData>().definition;
  for (const auto& head : input.coherence.implHeads()) {
    if (signature::SignatureFactsCanonicalCodec::implPatternInterface(head.pattern) !=
        existential.principal.definition) {
      continue;
    }
    auto selfLookup = input.semanticTypes.get(head.selfType);
    if (!selfLookup.is<type::SemanticTypeLookup>()) { continue; }
    const auto& selfData = selfLookup.get<type::SemanticTypeLookup>().data();
    if (!selfData.is<type::semantic::NominalTypeData>()) { continue; }
    if (selfData.get<type::semantic::NominalTypeData>().definition == concreteDefinition) {
      return true;
    }
  }
  return false;
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
    // A for-loop init local is implicitly mutable because the update clause
    // writes to it. Check whether the binding's introducer is inside a
    // ForStmt init slot before falling back to the LetStmt mutability.
    bool forLoopInit = false;
    ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId, const ast::Node& syntax) {
      if (syntax.kind != ast::SyntaxKind::ForStmt) return;
      const ast::NodeId init(syntax.payload.words[ast::kForStmtInitWord]);
      if (!tree.contains(init) || tree.node(init).kind != ast::SyntaxKind::LetStmt) return;
      const ast::NodeId declarations(tree.node(init).payload.words[ast::kLetStmtDeclarationsWord]);
      if (tree.contains(declarations) && subtreeContains(tree, declarations, site.introducer)) {
        forLoopInit = true;
      }
    });
    if (forLoopInit) return true;
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

bool isScalarLiteral(ast::SyntaxKind kind) noexcept;
zc::Maybe<PrimitiveOperation> scalarComparisonOperation(ast::BinaryOperatorKind syntax);
zc::Maybe<PrimitiveOperation> scalarArithmeticOperation(ast::BinaryOperatorKind syntax);
template <typename Entry, typename Key>
zc::Maybe<const Entry&> factEntry(zc::ArrayPtr<Entry> entries, const Key& key);

// A compound assignment operator that desugars to a primitive binary write
// (`x += 1` -> `x = x + 1`). The ten arithmetic, remainder, bitwise, and
// shift compound operators are admitted; power, unsigned-shift, logical, and
// null-coalescing compound operators stay unsupported in this slice.
bool isAdmittedCompoundAssignment(ast::AssignmentOperatorKind op) noexcept {
  switch (op) {
    case ast::AssignmentOperatorKind::AddAssign:
    case ast::AssignmentOperatorKind::SubAssign:
    case ast::AssignmentOperatorKind::MulAssign:
    case ast::AssignmentOperatorKind::DivAssign:
    case ast::AssignmentOperatorKind::ModAssign:
    case ast::AssignmentOperatorKind::BitAndAssign:
    case ast::AssignmentOperatorKind::BitOrAssign:
    case ast::AssignmentOperatorKind::BitXorAssign:
    case ast::AssignmentOperatorKind::ShlAssign:
    case ast::AssignmentOperatorKind::ShrAssign:
      return true;
    default:
      return false;
  }
}

bool isSimpleLocalWrite(const driver::module_graph_query::CheckerBoundModuleView& boundModule,
                        ast::NodeId assignment) {
  const auto& tree = boundModule.tree();
  if (!tree.contains(assignment)) return false;
  const auto& syntax = tree.node(assignment);
  if (syntax.kind != ast::SyntaxKind::AssignmentExpr) return false;
  const auto writeOp =
      static_cast<ast::AssignmentOperatorKind>(syntax.payload.words[ast::kAssignmentExprOpWord]);
  // A compound assignment (`x += 1`) desugars to a binary write
  // (`x = x + 1`); the target and value structural checks below are the
  // same as a plain write.
  const bool compoundWrite = isAdmittedCompoundAssignment(writeOp);
  if (writeOp != ast::AssignmentOperatorKind::Assign && !compoundWrite) { return false; }
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
  // A write value is a scalar literal, an identifier reference (a parameter or
  // an earlier local, resolved downstream and lowered to a copy/move place-use),
  // or a primitive binary operation whose operands each follow the same rule
  // (each a scalar literal or an identifier reference, with at least one
  // reference). Every form keeps the write's target-type fact intact. Operator
  // support is decided at the RHS binary's own production site; this structural
  // check only admits the write shape.
  const bool reference = valueKind == ast::SyntaxKind::IdentExpr;
  bool binary = false;
  // A compound assignment's RHS is the binary's second operand, not a nested
  // binary, so binary values stay plain-assignment-only.
  if (!compoundWrite && valueKind == ast::SyntaxKind::BinaryExpr) {
    const auto operation = static_cast<ast::BinaryOperatorKind>(
        tree.node(value).payload.words[ast::kBinaryExprOpWord]);
    if (scalarComparisonOperation(operation) != zc::none ||
        scalarArithmeticOperation(operation) != zc::none) {
      const ast::NodeId binaryLeft(tree.node(value).payload.words[ast::kBinaryExprLhsWord]);
      const ast::NodeId binaryRight(tree.node(value).payload.words[ast::kBinaryExprRhsWord]);
      if (tree.contains(binaryLeft) && tree.contains(binaryRight)) {
        const bool leftIdent = tree.node(binaryLeft).kind == ast::SyntaxKind::IdentExpr;
        const bool rightIdent = tree.node(binaryRight).kind == ast::SyntaxKind::IdentExpr;
        const bool leftOk = leftIdent || isScalarLiteral(tree.node(binaryLeft).kind);
        const bool rightOk = rightIdent || isScalarLiteral(tree.node(binaryRight).kind);
        binary = leftOk && rightOk && (leftIdent || rightIdent);
      }
    }
  }
  return (scalar || reference || binary) && isMutableOwnerLocal(boundModule, target);
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

/// \brief Classifies `this.<field> = <scalar literal>` inside a method body.
/// Structural only: whether the enclosing method's receiver is mutable is
/// authorized against the callable signature at fact production, so a write
/// through a shared receiver drains there instead of being admitted here.
bool isSimpleReceiverFieldWrite(
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
  if (static_cast<ast::MemberAccessKind>(member.payload.words[ast::kMemberExpressionAccessWord]) !=
      ast::MemberAccessKind::Dot) {
    return false;
  }
  const ast::NodeId object(member.payload.words[ast::kMemberExpressionObjectWord]);
  if (!tree.contains(object) || tree.node(object).kind != ast::SyntaxKind::ThisExpr) {
    return false;
  }
  // The written value is a scalar literal or an identifier reference (an
  // ordinary parameter or owner local, resolved and type-checked downstream at
  // the production site). Structure only; the checker decides whether the
  // reference is in scope and matches the field type.
  return isScalarLiteral(tree.node(value).kind) ||
         tree.node(value).kind == ast::SyntaxKind::IdentExpr;
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

/// \brief Shape of a qualified enum variant access such as `Color::Red`. The
/// enum type is the nominal type of the enum definition; the variant is the
/// resolved enum variant definition; the discriminant is the variant's
/// explicit integer discriminant when the variant declares one (`Red = 10`),
/// otherwise its zero-based index within the enum's variant list.
struct EnumVariantValueShape final {
  identity::SemanticTypeId enumType;
  identity::DefId variant;
  uint64_t discriminant;
};

/// \brief Resolves a qualified enum variant access `Enum::Variant`. The base
/// identifier must resolve to an enum definition and the member must name a
/// unit variant (empty payload) of that enum. The enum type is interned as a
/// closed nominal type.
zc::Maybe<EnumVariantValueShape> enumVariantValueShape(const BodyCheckingInput& input,
                                                       ast::NodeId node) {
  const auto& tree = input.boundModule.tree();
  if (!tree.contains(node) || tree.node(node).kind != ast::SyntaxKind::MemberExpression) {
    return zc::none;
  }
  const auto& member = tree.node(node);
  if (static_cast<ast::MemberAccessKind>(member.payload.words[ast::kMemberExpressionAccessWord]) !=
      ast::MemberAccessKind::Qualified) {
    return zc::none;
  }
  const ast::NodeId object(member.payload.words[ast::kMemberExpressionObjectWord]);
  if (!tree.contains(object) || tree.node(object).kind != ast::SyntaxKind::IdentExpr) {
    return zc::none;
  }
  const auto baseDefinition = resolvedDefinition(input.boundModule.bindings(), object);
  if (baseDefinition == zc::none) return zc::none;
  const auto enumDefId = ZC_ASSERT_NONNULL(baseDefinition);
  bool baseIsEnum = false;
  for (const auto& definition : input.boundModule.definitions().definitions()) {
    if (definition.definition == enumDefId) {
      baseIsEnum = definition.record.kind() == identity::DefinitionKind::Enum;
      break;
    }
  }
  if (!baseIsEnum) return zc::none;
  const auto propertyName =
      tree.ident(ast::IdentId(member.payload.words[ast::kMemberExpressionPropertyWord]));
  zc::Maybe<identity::DefId> variant;
  uint64_t discriminant = 0;
  for (const auto& signature : input.signatureFacts.signatures()) {
    if (signature.definition != enumDefId ||
        !signature.payload.variant().is<signature::NominalSignature>()) {
      continue;
    }
    const auto& nominal = signature.payload.variant().get<signature::NominalSignature>();
    for (size_t variantIndex = 0; variantIndex < nominal.variants.size(); ++variantIndex) {
      const auto candidate = nominal.variants[variantIndex];
      for (const auto& definition : input.boundModule.definitions().definitions()) {
        if (definition.definition != candidate || definition.record.name() != propertyName) {
          continue;
        }
        if (definition.record.kind() != identity::DefinitionKind::EnumVariant) return zc::none;
        if (variant != zc::none) return zc::none;
        variant = candidate;
        discriminant = static_cast<uint64_t>(variantIndex);
      }
    }
  }
  if (variant == zc::none) return zc::none;
  // A unit variant (empty payload) is the only admitted shape in this slice.
  for (const auto& signature : input.signatureFacts.signatures()) {
    if (signature.definition != ZC_ASSERT_NONNULL(variant) ||
        !signature.payload.variant().is<signature::EnumVariantSignature>()) {
      continue;
    }
    const auto& variantSignature =
        signature.payload.variant().get<signature::EnumVariantSignature>();
    if (variantSignature.payload.size() != 0) return zc::none;
    ZC_IF_SOME(explicitDiscriminant, variantSignature.discriminant) {
      // An explicit discriminant (`Red = 10`) overrides the zero-based index.
      // The magnitude is big-endian and bounded to u64 by the literal parser.
      if (explicitDiscriminant.magnitude.size() > 8) return zc::none;
      uint64_t explicitValue = 0;
      for (const uint8_t byte : explicitDiscriminant.magnitude.asPtr()) {
        explicitValue = (explicitValue << 8) | byte;
      }
      discriminant = explicitValue;
    }
  }
  auto admitted = input.semanticTypes.canonicalizeClosed(
      type::semantic::TypeData(type::semantic::NominalTypeData{enumDefId, {}}));
  if (!admitted.is<type::semantic::CanonicalTypeData>()) return zc::none;
  auto interned =
      input.semanticTypes.intern(zc::mv(admitted).get<type::semantic::CanonicalTypeData>());
  if (!interned.is<type::SemanticTypeInterned>()) return zc::none;
  return EnumVariantValueShape{interned.get<type::SemanticTypeInterned>().id,
                               ZC_ASSERT_NONNULL(variant), discriminant};
}

/// \brief Shape of an enum tuple-variant construction such as `Result::Ok(41)`.
/// The enum type is the nominal type of the enum definition; the variant is
/// the resolved tuple-variant definition; the payload carries the variant's
/// declared field types.
struct EnumVariantConstructionShape final {
  identity::SemanticTypeId enumType;
  identity::DefId variant;
  zc::Vector<identity::SemanticTypeId> payload;
};

/// \brief Resolves an enum tuple-variant construction `Enum::Variant(args)`.
/// The node must be a CallExpression whose callee is a qualified member access
/// naming a tuple variant (non-empty payload) of an enum definition. The enum
/// type is interned as a closed nominal type.
zc::Maybe<EnumVariantConstructionShape> enumVariantConstructionShape(const BodyCheckingInput& input,
                                                                     ast::NodeId node) {
  const auto& tree = input.boundModule.tree();
  if (!tree.contains(node) || tree.node(node).kind != ast::SyntaxKind::CallExpression) {
    return zc::none;
  }
  const auto& call = tree.node(node);
  const ast::NodeId callee(call.payload.words[ast::kCallExpressionCalleeWord]);
  if (!tree.contains(callee) || tree.node(callee).kind != ast::SyntaxKind::MemberExpression) {
    return zc::none;
  }
  const auto& member = tree.node(callee);
  if (static_cast<ast::MemberAccessKind>(member.payload.words[ast::kMemberExpressionAccessWord]) !=
      ast::MemberAccessKind::Qualified) {
    return zc::none;
  }
  const ast::NodeId object(member.payload.words[ast::kMemberExpressionObjectWord]);
  if (!tree.contains(object) || tree.node(object).kind != ast::SyntaxKind::IdentExpr) {
    return zc::none;
  }
  const auto baseDefinition = resolvedDefinition(input.boundModule.bindings(), object);
  if (baseDefinition == zc::none) return zc::none;
  const auto enumDefId = ZC_ASSERT_NONNULL(baseDefinition);
  bool baseIsEnum = false;
  for (const auto& definition : input.boundModule.definitions().definitions()) {
    if (definition.definition == enumDefId) {
      baseIsEnum = definition.record.kind() == identity::DefinitionKind::Enum;
      break;
    }
  }
  if (!baseIsEnum) return zc::none;
  const auto propertyName =
      tree.ident(ast::IdentId(member.payload.words[ast::kMemberExpressionPropertyWord]));
  zc::Maybe<identity::DefId> variant;
  for (const auto& signature : input.signatureFacts.signatures()) {
    if (signature.definition != enumDefId ||
        !signature.payload.variant().is<signature::NominalSignature>()) {
      continue;
    }
    const auto& nominal = signature.payload.variant().get<signature::NominalSignature>();
    for (const auto candidate : nominal.variants) {
      for (const auto& definition : input.boundModule.definitions().definitions()) {
        if (definition.definition != candidate || definition.record.name() != propertyName) {
          continue;
        }
        if (definition.record.kind() != identity::DefinitionKind::EnumVariant) return zc::none;
        if (variant != zc::none) return zc::none;
        variant = candidate;
      }
    }
  }
  if (variant == zc::none) return zc::none;
  // A tuple variant (non-empty payload) is the admitted shape; a unit variant
  // has no constructor call and stays on the EnumVariantValue rail.
  zc::Vector<identity::SemanticTypeId> payload;
  for (const auto& signature : input.signatureFacts.signatures()) {
    if (signature.definition != ZC_ASSERT_NONNULL(variant) ||
        !signature.payload.variant().is<signature::EnumVariantSignature>()) {
      continue;
    }
    const auto& variantSignature =
        signature.payload.variant().get<signature::EnumVariantSignature>();
    if (variantSignature.payload.size() == 0) return zc::none;
    for (const auto field : variantSignature.payload.asPtr()) { payload.add(field); }
  }
  if (payload.size() == 0) return zc::none;
  auto admitted = input.semanticTypes.canonicalizeClosed(
      type::semantic::TypeData(type::semantic::NominalTypeData{enumDefId, {}}));
  if (!admitted.is<type::semantic::CanonicalTypeData>()) return zc::none;
  auto interned =
      input.semanticTypes.intern(zc::mv(admitted).get<type::semantic::CanonicalTypeData>());
  if (!interned.is<type::SemanticTypeInterned>()) return zc::none;
  return EnumVariantConstructionShape{interned.get<type::SemanticTypeInterned>().id,
                                      ZC_ASSERT_NONNULL(variant), zc::mv(payload)};
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

/// \brief Shape of `parameter.<field>` where `parameter` is an ordinary by-value
/// callable parameter of closed nominal struct type. The place root is the
/// callable parameter with one field projection; unlike the receiver shape it
/// carries no dereference.
struct ParameterFieldShape final {
  identity::CallableParameterId parameter;
  identity::SemanticTypeId receiverType;
  identity::DefId field;
  identity::SemanticTypeId fieldType;
};

zc::Maybe<identity::SemanticTypeId> callableParameterReferenceType(const BodyCheckingInput& input,
                                                                   ast::NodeId node);

zc::Maybe<ParameterFieldShape> parameterFieldShape(
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
  const auto parameter = resolvedCallableParameter(input.boundModule.bindings(), object);
  if (parameter == zc::none) return zc::none;
  auto receiverType = callableParameterReferenceType(input, object);
  if (receiverType == zc::none) return zc::none;

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
  (void)nodeTypes;
  ZC_IF_SOME(param, parameter) {
    ZC_IF_SOME(type, receiverType) {
      ZC_IF_SOME(memberField, field) {
        return ParameterFieldShape{param, type, memberField.definition, memberField.type};
      }
    }
  }
  ZC_UNREACHABLE
}

/// \brief The implicit receiver of the inherent method enclosing a `this`
/// expression: the receiver parameter, the method and owner definitions, the
/// owner nominal type, the receiver reference type, and the receiver mode.
struct EnclosingMethodReceiver final {
  identity::CallableParameterId receiverParameter;
  identity::DefId methodDefinition;
  identity::DefId ownerDefinition;
  identity::SemanticTypeId receiverType;
  identity::SemanticTypeId receiverReferenceType;
  bool receiverMutable;
};

/// \brief Resolves the implicit receiver of the inherent method enclosing a
/// `this` expression. Both a shared const receiver and a mutating mutable
/// receiver are accepted; the caller applies the slice-specific mode gate.
zc::Maybe<EnclosingMethodReceiver> enclosingMethodReceiver(const BodyCheckingInput& input,
                                                           ast::NodeId object) {
  const auto& tree = input.boundModule.tree();
  zc::Maybe<identity::CallableParameterId> receiverParameter;
  for (const auto& binding : input.boundModule.bindings().thisBindings()) {
    if (binding.expression != object) continue;
    if (receiverParameter != zc::none) { return zc::none; }
    receiverParameter = binding.binding.receiverParameter;
  }
  if (receiverParameter == zc::none) { return zc::none; }

  // The implicit receiver is declared with `Self` and is not part of the
  // callable parameter list. Resolve the enclosing method and its receiver from
  // the member-signature scope: the receiver key must name this parameter and
  // the referent is the scope's owner nominal.
  const auto& spans = input.boundModule.parsedModule();
  zc::Maybe<identity::SourceSpan> nodeSpan = spans.spanFor(tree.node(object).range);
  if (nodeSpan == zc::none) { return zc::none; }
  zc::Maybe<identity::DefId> methodDefinition;
  for (const auto& definition : input.boundModule.bindings().definitions()) {
    if (definition.kind != identity::DefinitionKind::Method ||
        !definition.site.value().is<binder::DeclarationDefinitionSite>()) {
      continue;
    }
    const ast::NodeId bodyNode =
        definition.site.value().get<binder::DeclarationDefinitionSite>().node;
    if (!tree.contains(bodyNode)) { continue; }
    auto bodySpan = spans.spanFor(tree.node(bodyNode).range);
    if (bodySpan == zc::none || nodeSpan == zc::none) { continue; }
    if (ZC_ASSERT_NONNULL(bodySpan).byteStart() <= ZC_ASSERT_NONNULL(nodeSpan).byteStart() &&
        ZC_ASSERT_NONNULL(nodeSpan).byteEnd() <= ZC_ASSERT_NONNULL(bodySpan).byteEnd()) {
      if (methodDefinition != zc::none) { return zc::none; }
      methodDefinition = definition.identity;
    }
  }
  if (methodDefinition == zc::none) { return zc::none; }

  auto receiverAuthority = input.identities.callableParameter(ZC_ASSERT_NONNULL(receiverParameter));
  if (receiverAuthority == zc::none) { return zc::none; }
  zc::Maybe<identity::DefId> ownerDefinition;
  zc::Maybe<identity::SemanticTypeId> receiverReferenceType;
  zc::Maybe<identity::SemanticTypeId> receiverType;
  bool receiverMutable = false;
  for (const auto& signature : input.signatureFacts.signatures()) {
    if (signature.definition != ZC_ASSERT_NONNULL(methodDefinition)) { continue; }
    if (!signature.payload.variant().is<signature::CallableSignature>()) { continue; }
    if (!signature.scope.variant().is<signature::MemberSignatureScope>()) { continue; }
    const auto& callable = signature.payload.variant().get<signature::CallableSignature>();
    const auto& scope = signature.scope.variant().get<signature::MemberSignatureScope>();
    if (callable.receiver == zc::none || ownerDefinition != zc::none) { return zc::none; }
    const auto& receiverSignature = ZC_ASSERT_NONNULL(callable.receiver);
    if ((receiverSignature.mode != signature::ReceiverMode::Shared &&
         receiverSignature.mode != signature::ReceiverMode::Mutable) ||
        receiverSignature.parameter != ZC_ASSERT_NONNULL(receiverAuthority).key()) {
      return zc::none;
    }
    receiverMutable = receiverSignature.mode == signature::ReceiverMode::Mutable;
    auto admitted = input.semanticTypes.canonicalizeClosed(
        type::semantic::TypeData(type::semantic::NominalTypeData{scope.owner, {}}));
    if (!admitted.is<type::semantic::CanonicalTypeData>()) { return zc::none; }
    auto interned =
        input.semanticTypes.intern(zc::mv(admitted).get<type::semantic::CanonicalTypeData>());
    if (!interned.is<type::SemanticTypeInterned>()) { return zc::none; }
    const auto ownerType = interned.get<type::SemanticTypeInterned>().id;
    auto referenceAdmitted = input.semanticTypes.canonicalizeClosed(type::semantic::TypeData(
        type::semantic::ReferenceTypeData{receiverMutable ? type::semantic::Mutability::Mutable
                                                          : type::semantic::Mutability::Const,
                                          ownerType}));
    if (!referenceAdmitted.is<type::semantic::CanonicalTypeData>()) { return zc::none; }
    auto referenceInterned = input.semanticTypes.intern(
        zc::mv(referenceAdmitted).get<type::semantic::CanonicalTypeData>());
    if (!referenceInterned.is<type::SemanticTypeInterned>()) { return zc::none; }
    receiverReferenceType = referenceInterned.get<type::SemanticTypeInterned>().id;
    receiverType = ownerType;
    ownerDefinition = scope.owner;
  }
  if (receiverType == zc::none || receiverReferenceType == zc::none ||
      ownerDefinition == zc::none) {
    return zc::none;
  }
  auto referenceLookup = input.semanticTypes.get(ZC_ASSERT_NONNULL(receiverReferenceType));
  if (!referenceLookup.is<type::SemanticTypeLookup>()) { return zc::none; }
  const auto& referenceData = referenceLookup.get<type::SemanticTypeLookup>().data();
  if (!referenceData.is<type::semantic::ReferenceTypeData>() ||
      (referenceData.get<type::semantic::ReferenceTypeData>().mutability ==
       type::semantic::Mutability::Mutable) != receiverMutable) {
    return zc::none;
  }
  return EnclosingMethodReceiver{
      ZC_ASSERT_NONNULL(receiverParameter),     ZC_ASSERT_NONNULL(methodDefinition),
      ZC_ASSERT_NONNULL(ownerDefinition),       ZC_ASSERT_NONNULL(receiverType),
      ZC_ASSERT_NONNULL(receiverReferenceType), receiverMutable};
}

/// \brief Resolved field access through the implicit `this` receiver of an
/// inherent method: the receiver parameter root, the owner nominal type, and
/// the projected field. `receiverMutable` records the callable receiver mode;
/// a place derived from a shared receiver is never a mutable place.
struct ThisReceiverFieldShape final {
  identity::CallableParameterId receiverParameter;
  identity::SemanticTypeId receiverType;
  identity::SemanticTypeId receiverReferenceType;
  identity::DefId field;
  identity::SemanticTypeId fieldType;
  bool receiverMutable;
};

/// \brief Resolves `this.field` inside an inherent method body to the
/// receiver parameter and field definition. Both a shared const receiver and a
/// mutating mutable receiver are accepted; the receiver source type is read from
/// the typed `this` expression and its mutability is returned to the caller.
zc::Maybe<ThisReceiverFieldShape> thisReceiverFieldShape(const BodyCheckingInput& input,
                                                         ast::NodeId node) {
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
  if (!tree.contains(object) || tree.node(object).kind != ast::SyntaxKind::ThisExpr) {
    return zc::none;
  }
  auto receiver = enclosingMethodReceiver(input, object);
  if (receiver == zc::none) { return zc::none; }

  auto field = nominalFieldShape(
      input, ZC_ASSERT_NONNULL(receiver).ownerDefinition,
      tree.ident(ast::IdentId(member.payload.words[ast::kMemberExpressionPropertyWord])));
  if (field == zc::none) { return zc::none; }
  return ThisReceiverFieldShape{ZC_ASSERT_NONNULL(receiver).receiverParameter,
                                ZC_ASSERT_NONNULL(receiver).receiverType,
                                ZC_ASSERT_NONNULL(receiver).receiverReferenceType,
                                ZC_ASSERT_NONNULL(field).definition,
                                ZC_ASSERT_NONNULL(field).type,
                                ZC_ASSERT_NONNULL(receiver).receiverMutable};
}

struct StructLiteralShape final {
  identity::DefId definition;
  identity::SemanticTypeId type;
  zc::Vector<checked::AggregateElementFact> elements;
};

/// \brief A user-level error in a struct literal, classified so the production
/// site can drain it with a registered ZOM code instead of collapsing every
/// rejected literal into a MissingRequiredFact invariant.
struct StructLiteralRejection final {
  enum class Kind : uint8_t {
    UnknownField,
    MissingField,
    DuplicateField,
    UnsupportedIntegerField,
    TypeMismatch
  };
  Kind kind;
  zc::Maybe<identity::SemanticIdentifier> name;
  zc::Maybe<identity::DefId> field;
  zc::Maybe<identity::SemanticTypeId> actualType;
  zc::Maybe<identity::SemanticTypeId> expectedType;
};

using StructLiteralBuildResult = zc::Maybe<zc::OneOf<StructLiteralShape, StructLiteralRejection>>;

/// \brief The integer primitive kind of `type`, or none for non-integers.
zc::Maybe<type::semantic::PrimitiveKind> integerPrimitiveKind(
    const type::SemanticTypeStore& semanticTypes, identity::SemanticTypeId type) {
  auto lookup = semanticTypes.get(type);
  if (!lookup.is<type::SemanticTypeLookup>()) return zc::none;
  const auto& data = lookup.get<type::SemanticTypeLookup>().data();
  if (!data.is<type::semantic::PrimitiveTypeData>()) return zc::none;
  const auto kind = data.get<type::semantic::PrimitiveTypeData>().kind;
  if (kind < type::semantic::PrimitiveKind::I8 || kind > type::semantic::PrimitiveKind::Usize) {
    return zc::none;
  }
  return kind;
}

StructLiteralBuildResult buildStructLiteral(
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

  // The complete declared-field set of the nominal; the inventory is the sole
  // authority for "this name is not a field", distinct from a missing fact.
  zc::Vector<identity::DefId> declaredFields;
  for (const auto& nominalSignature : input.signatureFacts.signatures()) {
    if (nominalSignature.definition != nominal.definition ||
        !nominalSignature.payload.variant().is<signature::NominalSignature>()) {
      continue;
    }
    for (const auto field :
         nominalSignature.payload.variant().get<signature::NominalSignature>().fields) {
      declaredFields.add(field);
    }
  }
  auto fieldNamed = [&](zc::StringPtr propertyName) -> zc::Maybe<identity::DefId> {
    zc::Maybe<identity::DefId> result;
    for (const auto field : declaredFields) {
      bool named = false;
      for (const auto& definition : input.boundModule.definitions().definitions()) {
        if (definition.definition == field && definition.record.name() == propertyName) {
          named = true;
        }
      }
      if (!named) continue;
      if (result != zc::none) return zc::none;
      result = field;
    }
    return result;
  };

  zc::Vector<checked::AggregateElementFact> elements(properties.size);
  zc::Vector<identity::DefId> initializedFields;
  for (uint32_t index = 0; index < properties.size; ++index) {
    const auto property = tree.list(properties)[index];
    if (!tree.contains(property) || tree.node(property).kind != ast::SyntaxKind::ObjectProperty) {
      return zc::none;
    }
    const auto& syntax = tree.node(property);
    if (syntax.payload.words[ast::kObjectPropertyShortFormWord] != 0) return zc::none;
    const ast::NodeId value(syntax.payload.words[ast::kObjectPropertyValueWord]);
    if (!tree.contains(value)) return zc::none;
    const auto propertyName =
        tree.ident(ast::IdentId(syntax.payload.words[ast::kObjectPropertyNameWord]));
    auto sourceName = identity::SemanticIdentifier::fromSource(propertyName);
    if (sourceName == zc::none) return zc::none;
    if (fieldNamed(propertyName) == zc::none) {
      return StructLiteralBuildResult(StructLiteralRejection{
          StructLiteralRejection::Kind::UnknownField, zc::mv(ZC_ASSERT_NONNULL(sourceName)),
          zc::none, zc::none, zc::none});
    }
    const auto field = nominalFieldShape(input, nominal.definition, propertyName);
    if (field == zc::none) return zc::none;
    zc::Maybe<const checked::NodeTypeMap::Entry&> valueType;
    for (const auto& entry : nodeTypes) {
      if (entry.key != value) continue;
      if (valueType != zc::none) return zc::none;
      valueType = entry;
    }
    if (valueType == zc::none) return zc::none;
    ZC_IF_SOME(member, field) {
      ZC_IF_SOME(sourceType, valueType) {
        for (const auto initialized : initializedFields) {
          if (initialized == member.definition) {
            return StructLiteralBuildResult(StructLiteralRejection{
                StructLiteralRejection::Kind::DuplicateField, zc::mv(ZC_ASSERT_NONNULL(sourceName)),
                zc::none, zc::none, zc::none});
          }
        }
        if (sourceType.value != member.type) {
          // An integer literal assigned to a non-i32 integer field is a valid
          // source relation the numeric-literal unification slice does not
          // implement yet; drain it as ZOM4099 like the scalar positions. Every
          // other unequal pair is a genuine type error (ZOM4009).
          const bool integerLiteralWidening =
              tree.node(value).kind == ast::SyntaxKind::IntLiteral &&
              integerPrimitiveKind(input.semanticTypes, member.type) != zc::none &&
              integerPrimitiveKind(input.semanticTypes, sourceType.value) != zc::none;
          if (integerLiteralWidening) {
            return StructLiteralBuildResult(
                StructLiteralRejection{StructLiteralRejection::Kind::UnsupportedIntegerField,
                                       zc::none, zc::none, sourceType.value, member.type});
          }
          return StructLiteralBuildResult(
              StructLiteralRejection{StructLiteralRejection::Kind::TypeMismatch, zc::none, zc::none,
                                     sourceType.value, member.type});
        }
        initializedFields.add(member.definition);
        zc::Maybe<identity::DefId> fieldDefinition = member.definition;
        zc::Maybe<checked::CoercionAdjustment> noAdjustment;
        elements.add(checked::AggregateElementFact{value, zc::mv(fieldDefinition), index,
                                                   sourceType.value, member.type,
                                                   zc::mv(noAdjustment)});
      }
    }
  }
  if (elements.size() != properties.size) return zc::none;
  for (const auto field : declaredFields) {
    bool fieldInitialized = false;
    for (const auto& initialized : initializedFields) {
      if (initialized == field) fieldInitialized = true;
    }
    if (!fieldInitialized) {
      return StructLiteralBuildResult(StructLiteralRejection{
          StructLiteralRejection::Kind::MissingField, zc::none, field, zc::none, zc::none});
    }
  }
  ZC_IF_SOME(value, type) {
    return StructLiteralBuildResult(
        StructLiteralShape{nominal.definition, value, zc::mv(elements)});
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

/// \brief One admitted primitive unary operation (`+` `-` `~` `!`) on a scalar
/// operand. The operand is a value reference (parameter or owner local) or a
/// scalar literal; at least one must be a reference. The HIR builder desugars
/// each to an equivalent binary operation, reusing the comparison-return path.
struct PrimitiveUnaryOperationShape final {
  ast::NodeId operandNode;
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
/// of the twelve arithmetic or bitwise operators of same-typed scalars, or one
/// of the two logical short-circuit operators (`&&` / `||`) on bool operands.
///
/// Reuses `OperatorKind::fromBinary` so the operator mapping lives in exactly
/// one place. The six relational comparisons (handled by
/// `scalarComparisonOperation`) and strict identity return none so their
/// existing handling stands. Unlike a comparison, the result type of the
/// arithmetic and bitwise operators is the operand type, not bool; the logical
/// operators also produce bool, but their operand type is bool too, so the
/// same contract holds.
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
      case PrimitiveOperation::LogicalAnd:
      case PrimitiveOperation::LogicalOr:
        return variant.get<PrimitiveOperation>();
      default:
        return zc::none;
    }
  }
  return zc::none;
}

/// \brief True when the node is the condition of an enclosing `if`, `while`,
/// or `for` statement. An arithmetic result is not bool, so it is not lowerable
/// as a condition and stays unsupported there; a comparison result is bool and
/// is lowerable in both positions.
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
    if (syntax.kind == ast::SyntaxKind::ForStmt &&
        ast::NodeId(syntax.payload.words[ast::kForStmtCondWord]) == node) {
      isCondition = true;
    }
  });
  return isCondition;
}

/// \brief Resolves a semantic type id to its primitive kind, or none for a
/// non-primitive (nominal, structural, or unknown) type.
zc::Maybe<type::semantic::PrimitiveKind> primitiveKindOf(
    const type::SemanticTypeStore& semanticTypes, identity::SemanticTypeId id) {
  auto lookup = semanticTypes.get(id);
  if (!lookup.is<type::SemanticTypeLookup>()) return zc::none;
  const auto& data = lookup.get<type::SemanticTypeLookup>().data();
  if (!data.is<type::semantic::PrimitiveTypeData>()) return zc::none;
  return data.get<type::semantic::PrimitiveTypeData>().kind;
}

/// \brief Whether a dot member read targets a resolved owner local or by-value
/// parameter whose receiver carries no nominal field the body slice can read.
///
/// This is true for a primitive receiver (str, char, a numeric type, bool,
/// unit, ...), which never has a nominal field, and for a closed nominal
/// receiver whose named member does not resolve (every known field shape is
/// admitted by `ownerLocalFieldShape` / `parameterFieldShape` before this
/// guard runs). It keeps the site inside the expression capability family
/// instead of letting the missing fact reach the invariant rail. `this`
/// member reads and non-identifier receivers are not classified here.
bool isUnsupportedResolvedMemberRead(const BodyCheckingInput& input,
                                     zc::ArrayPtr<const checked::NodeTypeMap::Entry> nodeTypes,
                                     ast::NodeId node) {
  const auto& tree = input.boundModule.tree();
  if (!tree.contains(node) || tree.node(node).kind != ast::SyntaxKind::MemberExpression) {
    return false;
  }
  const auto& member = tree.node(node);
  if (static_cast<ast::MemberAccessKind>(member.payload.words[ast::kMemberExpressionAccessWord]) !=
      ast::MemberAccessKind::Dot) {
    return false;
  }
  const ast::NodeId object(member.payload.words[ast::kMemberExpressionObjectWord]);
  if (!tree.contains(object) || tree.node(object).kind != ast::SyntaxKind::IdentExpr) {
    return false;
  }
  zc::Maybe<identity::SemanticTypeId> receiverType;
  if (resolvedOwnerLocal(input.boundModule.bindings(), object) != zc::none) {
    receiverType = ownerLocalReferenceType(input, object, nodeTypes);
  } else if (resolvedCallableParameter(input.boundModule.bindings(), object) != zc::none) {
    receiverType = callableParameterReferenceType(input, object);
  }
  ZC_IF_SOME(type, receiverType) {
    if (primitiveKindOf(input.semanticTypes, type) != zc::none) { return true; }
    auto lookup = input.semanticTypes.get(type);
    if (lookup.is<type::SemanticTypeLookup>()) {
      const auto& typeData = lookup.get<type::SemanticTypeLookup>().data();
      if (typeData.is<type::semantic::NominalTypeData>()) {
        // A known field on the zero-argument nominal matches one of the field
        // shapes and never reaches this guard; an unknown member name on such a
        // nominal is an unsupported member read in this slice.
        return typeData.get<type::semantic::NominalTypeData>().arguments.size() == 0;
      }
    }
  }
  return false;
}

namespace {
// Bound on how deep an unannotated local initializer chain may recurse while
// resolving a result type, mirroring the existential-initializer chain bound.
constexpr unsigned kUnannotatedBinaryMaxDepth = 64;

zc::Maybe<identity::SemanticTypeId> internPrimitiveKind(const BodyCheckingInput& input,
                                                        type::semantic::PrimitiveKind kind) {
  auto canonical = input.semanticTypes.canonicalizeClosed(
      type::semantic::TypeData(type::semantic::PrimitiveTypeData{kind}));
  if (!canonical.is<type::semantic::CanonicalTypeData>()) return zc::none;
  auto interned =
      input.semanticTypes.intern(zc::mv(canonical).get<type::semantic::CanonicalTypeData>());
  if (!interned.is<type::SemanticTypeInterned>()) return zc::none;
  return interned.get<type::SemanticTypeInterned>().id;
}
}  // namespace

zc::Maybe<identity::SemanticTypeId> unannotatedBinaryInitializerType(
    const BodyCheckingInput& input, ast::NodeId node,
    zc::ArrayPtr<const checked::NodeTypeMap::Entry> nodeTypes, unsigned depth) {
  if (depth > kUnannotatedBinaryMaxDepth) return zc::none;
  const auto& tree = input.boundModule.tree();
  if (!tree.contains(node) || tree.node(node).kind != ast::SyntaxKind::BinaryExpr) return zc::none;
  const auto& syntax = tree.node(node);
  const auto binaryOperator =
      static_cast<ast::BinaryOperatorKind>(syntax.payload.words[ast::kBinaryExprOpWord]);
  auto comparison = scalarComparisonOperation(binaryOperator);
  auto arithmetic = scalarArithmeticOperation(binaryOperator);
  if (comparison == zc::none && arithmetic == zc::none) return zc::none;
  const bool isArithmetic = comparison == zc::none;
  const ast::NodeId left(syntax.payload.words[ast::kBinaryExprLhsWord]);
  const ast::NodeId right(syntax.payload.words[ast::kBinaryExprRhsWord]);
  if (!tree.contains(left) || !tree.contains(right)) return zc::none;
  // A typed operand anchors the result: an identifier resolving to a parameter
  // or an earlier local, or a one-level nested binary. A scalar literal carries
  // no type, so a literal-literal binary has no anchor and yields none.
  auto referenceOperandType = [&](ast::NodeId operand) -> zc::Maybe<identity::SemanticTypeId> {
    if (!tree.contains(operand) || tree.node(operand).kind != ast::SyntaxKind::IdentExpr) {
      return zc::none;
    }
    auto parameter = callableParameterReferenceType(input, operand);
    if (parameter != zc::none) return parameter;
    return ownerLocalReferenceType(input, operand, nodeTypes);
  };
  const ast::NodeId operands[2] = {left, right};
  zc::Maybe<identity::SemanticTypeId> operandType;
  for (const ast::NodeId operand : operands) {
    operandType = tree.node(operand).kind == ast::SyntaxKind::BinaryExpr
                      ? unannotatedBinaryInitializerType(input, operand, nodeTypes, depth + 1)
                      : referenceOperandType(operand);
    if (operandType != zc::none) break;
  }
  if (operandType == zc::none) return zc::none;
  const auto operation = ZC_ASSERT_NONNULL(comparison != zc::none ? comparison : arithmetic);
  auto primitive = primitiveKindOf(input.semanticTypes, ZC_ASSERT_NONNULL(operandType));
  if (primitive == zc::none ||
      !primitiveBinaryOperationAdmits(operation, ZC_ASSERT_NONNULL(primitive))) {
    return zc::none;
  }
  if (!isArithmetic) { return internPrimitiveKind(input, type::semantic::PrimitiveKind::Bool); }
  return operandType;
}

/// \brief Operand types of a binary operation the shape validator refused.
struct InvalidBinaryOperandTypes final {
  identity::SemanticTypeId leftType;
  identity::SemanticTypeId rightType;
  PrimitiveOperation operation;
  bool isComparison;
};

/// \brief Classifies a refused binary operation as ill-typed, not unsupported.
///
/// `primitiveBinaryOperationShape` returns none for two very different reasons:
/// a form this slice does not lower yet (nested operands, literal-vs-literal,
/// short-circuit operators), and operands whose types the operator is simply not
/// defined for. The first is a compiler-capability boundary and stays on the
/// invariant rail; the second is a user error and must reach the source rail as
/// `ZOM4029` for a comparison or `ZOM4028` for an arithmetic or bitwise
/// operation. Both operands must resolve to a type for the answer to be
/// trustworthy, so this returns none when either type is unknown, leaving the
/// existing fail-closed rejection in place.
///
/// An operation is ill-typed when the operand types differ, or when they agree
/// on a type the operator is not defined for (any non-scalar, including `Null`
/// and every nominal type).
zc::Maybe<InvalidBinaryOperandTypes> invalidBinaryOperandTypes(
    const BodyCheckingInput& input, ast::NodeId node,
    zc::ArrayPtr<const checked::NodeTypeMap::Entry> nodeTypes) {
  const auto& tree = input.boundModule.tree();
  if (!tree.contains(node) || tree.node(node).kind != ast::SyntaxKind::BinaryExpr) return zc::none;
  const auto& syntax = tree.node(node);
  const auto binaryOperator =
      static_cast<ast::BinaryOperatorKind>(syntax.payload.words[ast::kBinaryExprOpWord]);
  auto comparison = scalarComparisonOperation(binaryOperator);
  auto arithmetic = scalarArithmeticOperation(binaryOperator);
  auto operation = comparison != zc::none ? comparison : arithmetic;
  if (operation == zc::none) return zc::none;
  const ast::NodeId left(syntax.payload.words[ast::kBinaryExprLhsWord]);
  const ast::NodeId right(syntax.payload.words[ast::kBinaryExprRhsWord]);
  if (!tree.contains(left) || !tree.contains(right)) return zc::none;
  // Resolve each operand's type from the node-type facts already produced for
  // this body. A nested binary has no node-type fact yet at this point in schema
  // preorder, so an unresolved operand yields none and the caller keeps its
  // existing rejection.
  auto leftType = factEntry(nodeTypes, left);
  auto rightType = factEntry(nodeTypes, right);
  if (leftType == zc::none || rightType == zc::none) return zc::none;
  identity::SemanticTypeId leftValue;
  identity::SemanticTypeId rightValue;
  ZC_IF_SOME(entry, leftType) { leftValue = entry.value; }
  ZC_IF_SOME(entry, rightType) { rightValue = entry.value; }
  const PrimitiveOperation op = ZC_ASSERT_NONNULL(operation);
  // Operands of different types are ill-typed (ZOM has no numeric widening).
  // Same-typed operands are well-typed only when the operator is defined for
  // that primitive type; `bool + bool` or `bool < bool` are type errors even
  // though both operands agree, because arithmetic and ordering are not defined
  // for bool. A non-primitive (nominal/structural) operand is likewise ill-typed.
  if (leftValue == rightValue) {
    auto kind = primitiveKindOf(input.semanticTypes, leftValue);
    ZC_IF_SOME(value, kind) {
      if (primitiveBinaryOperationAdmits(op, value)) return zc::none;
    }
  }
  return InvalidBinaryOperandTypes{leftValue, rightValue, op, comparison != zc::none};
}

/// \brief Declared and produced types of a binary local initializer that disagree.
struct BinaryInitializerTypeMismatch final {
  identity::SemanticTypeId declaredType;
  identity::SemanticTypeId producedType;
};

/// \brief Classifies a refused binary as an initializer annotation mismatch.
///
/// `primitiveBinaryOperationShape` refuses a binary whose result type differs
/// from the annotation of the owner local it initializes (`let x: bool = a + b`)
/// by returning none, which the caller reports as a compiler invariant. That is
/// a user error and belongs on the source rail as `ZOM4009`, exactly as the
/// bare-identifier form `let x: bool = a` does.
///
/// This only answers for a binary whose operands are otherwise well-typed: same
/// primitive scalar type on both sides, so `invalidBinaryOperandTypes` has
/// already declined it. The result type is then the operand type for an
/// arithmetic operation and bool for a comparison, mirroring the shape
/// validator. Returns none when there is no annotation, when it agrees, or when
/// either operand type is unresolved, leaving the existing rejection in place.
zc::Maybe<BinaryInitializerTypeMismatch> binaryInitializerTypeMismatch(
    const BodyCheckingInput& input, ast::NodeId node,
    zc::ArrayPtr<const checked::NodeTypeMap::Entry> nodeTypes) {
  const auto& tree = input.boundModule.tree();
  if (!tree.contains(node) || tree.node(node).kind != ast::SyntaxKind::BinaryExpr) return zc::none;
  const auto& syntax = tree.node(node);
  const auto binaryOperator =
      static_cast<ast::BinaryOperatorKind>(syntax.payload.words[ast::kBinaryExprOpWord]);
  auto comparison = scalarComparisonOperation(binaryOperator);
  auto arithmetic = scalarArithmeticOperation(binaryOperator);
  if (comparison == zc::none && arithmetic == zc::none) return zc::none;
  auto declaredType = ownerLocalInitializerDeclaredType(input.boundModule, input.identities,
                                                        input.semanticTypes, node);
  if (declaredType == zc::none) return zc::none;
  const ast::NodeId left(syntax.payload.words[ast::kBinaryExprLhsWord]);
  const ast::NodeId right(syntax.payload.words[ast::kBinaryExprRhsWord]);
  if (!tree.contains(left) || !tree.contains(right)) return zc::none;
  auto leftType = factEntry(nodeTypes, left);
  auto rightType = factEntry(nodeTypes, right);
  if (leftType == zc::none || rightType == zc::none) return zc::none;
  identity::SemanticTypeId leftValue;
  identity::SemanticTypeId rightValue;
  ZC_IF_SOME(entry, leftType) { leftValue = entry.value; }
  ZC_IF_SOME(entry, rightType) { rightValue = entry.value; }
  // Ill-typed operands (different types, a type the operator is not defined
  // for, or a non-primitive) are the other diagnostic's business; only a
  // well-typed operand pair can have a meaningful result type to compare.
  if (leftValue != rightValue) { return zc::none; }
  {
    auto operandKind = primitiveKindOf(input.semanticTypes, leftValue);
    if (operandKind == zc::none) { return zc::none; }
    auto operationKind = comparison != zc::none ? comparison : arithmetic;
    if (!primitiveBinaryOperationAdmits(ZC_ASSERT_NONNULL(operationKind),
                                        ZC_ASSERT_NONNULL(operandKind))) {
      return zc::none;
    }
  }
  identity::SemanticTypeId resultType = leftValue;
  if (comparison != zc::none) {
    auto canonical = input.semanticTypes.canonicalizeClosed(type::semantic::TypeData(
        type::semantic::PrimitiveTypeData{type::semantic::PrimitiveKind::Bool}));
    if (!canonical.is<type::semantic::CanonicalTypeData>()) return zc::none;
    auto interned =
        input.semanticTypes.intern(zc::mv(canonical).get<type::semantic::CanonicalTypeData>());
    if (!interned.is<type::SemanticTypeInterned>()) return zc::none;
    resultType = interned.get<type::SemanticTypeInterned>().id;
  }
  identity::SemanticTypeId declared;
  ZC_IF_SOME(value, declaredType) { declared = value; }
  if (declared == resultType) return zc::none;
  return BinaryInitializerTypeMismatch{declared, resultType};
}

/// \brief Returns the enclosing method's bare name when a node lies inside the
/// body of an inherent struct/class method.
///
/// Inherent method bodies are checked as ordinary function bodies with an
/// implicit `this` receiver, but the body checker cannot lower them yet. A
/// `this` expression inside a Method body is therefore well-formed receiver
/// syntax the later phases cannot accept; this predicate supplies the method
/// name so the rejection can be reported as a capability instead of an
/// invariant. Returns none when the node is outside every Method body (for
/// example a `this` expression in a module function, which the parser rejects
/// independently with ZOM2095).
zc::Maybe<identity::DeclaredDefinitionName> enclosingMethodName(const BodyCheckingInput& input,
                                                                ast::NodeId node) {
  const auto& boundModule = input.boundModule;
  const auto& tree = boundModule.tree();
  const auto& parsedModule = boundModule.parsedModule();
  if (!tree.contains(node)) { return zc::none; }
  const source::SourceRange nodeRange = tree.node(node).range;
  zc::Maybe<identity::SourceSpan> nodeSpan = parsedModule.spanFor(nodeRange);
  for (const auto& definition : boundModule.bindings().definitions()) {
    if (definition.kind != identity::DefinitionKind::Method) { continue; }
    if (!definition.site.value().is<binder::DeclarationDefinitionSite>()) { continue; }
    const ast::NodeId bodyNode =
        definition.site.value().get<binder::DeclarationDefinitionSite>().node;
    if (!tree.contains(bodyNode)) { continue; }
    // A method body is parsed as a MethodDecl but bound as DefinitionKind::Method.
    const auto bodyKind = tree.node(bodyNode).kind;
    if (bodyKind != ast::SyntaxKind::FunctionDecl && bodyKind != ast::SyntaxKind::MethodDecl) {
      continue;
    }
    zc::Maybe<identity::SourceSpan> bodySpan = parsedModule.spanFor(tree.node(bodyNode).range);
    if (bodySpan == zc::none || nodeSpan == zc::none) { continue; }
    const auto& body = ZC_ASSERT_NONNULL(bodySpan);
    const auto& contained = ZC_ASSERT_NONNULL(nodeSpan);
    if (body.byteStart() <= contained.byteStart() && contained.byteEnd() <= body.byteEnd()) {
      return definition.name.clone();
    }
  }
  return zc::none;
}

/// \brief Returns the enclosing method's bare name when a `this` expression is
/// the implicit receiver of an inherent struct/class method the later phases
/// cannot lower.
zc::Maybe<identity::DeclaredDefinitionName> unsupportedInherentMethodThis(
    const BodyCheckingInput& input, ast::NodeId node) {
  const auto& tree = input.boundModule.tree();
  if (!tree.contains(node) || tree.node(node).kind != ast::SyntaxKind::ThisExpr) {
    return zc::none;
  }
  return enclosingMethodName(input, node);
}

/// \brief Returns the enclosing method's name when a member projection has an
/// implicit `this` receiver (`this.field`) the later phases cannot lower yet.
///
/// A `this.field` read is well-formed inside an inherent method body but the
/// HIR/MIR/LIR receiver-field rail does not admit it; it must reach ZOM4125
/// rather than the owner-local field invariant.
zc::Maybe<identity::DeclaredDefinitionName> unsupportedThisFieldAccess(
    const BodyCheckingInput& input, ast::NodeId memberNode) {
  const auto& tree = input.boundModule.tree();
  if (!tree.contains(memberNode) ||
      tree.node(memberNode).kind != ast::SyntaxKind::MemberExpression) {
    return zc::none;
  }
  const auto& member = tree.node(memberNode);
  if (static_cast<ast::MemberAccessKind>(member.payload.words[ast::kMemberExpressionAccessWord]) !=
      ast::MemberAccessKind::Dot) {
    return zc::none;
  }
  const ast::NodeId object(member.payload.words[ast::kMemberExpressionObjectWord]);
  if (!tree.contains(object) || tree.node(object).kind != ast::SyntaxKind::ThisExpr) {
    return zc::none;
  }
  return enclosingMethodName(input, object);
}

/// \brief Returns the resolved method when a well-formed shared-receiver
/// inherent method call on an immutable owner local cannot be lowered yet.
///
/// This mirrors concreteMethodCallShape's gates exactly, with the two receiver
/// choices inverted: the receiver is an immutable (`let`) owner local rather
/// than a `mut` one, and the resolved method's receiver mode is Shared rather
/// than Mutable. The later HIR/MIR/LIR phases do not admit this shape, so it
/// must reach a capability rejection (ZOM4125) instead of an invariant. Returns
/// none for every shape the mutable rail accepts or for malformed input, so
/// those keep their existing diagnostics.
zc::Maybe<identity::DeclaredDefinitionName> unsupportedSharedReceiverInherentMethodCall(
    const BodyCheckingInput& input, zc::ArrayPtr<const checked::NodeTypeMap::Entry> nodeTypes,
    ast::NodeId callNode) {
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
      resolvedOwnerLocal(input.boundModule.bindings(), receiverNode) == zc::none ||
      isMutableOwnerLocal(input.boundModule, receiverNode)) {
    return zc::none;
  }
  const auto receiverSourceType = ownerLocalReferenceType(input, receiverNode, nodeTypes);
  if (receiverSourceType == zc::none) { return zc::none; }
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

  const auto memberName =
      tree.ident(ast::IdentId(member.payload.words[ast::kMemberExpressionPropertyWord]));
  zc::Maybe<identity::DefId> selected;
  zc::Maybe<identity::DeclaredDefinitionName> selectedName;
  uint32_t parameterCount = 0;
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
      if (namedMethod) {
        for (const auto& binding : input.boundModule.bindings().definitions()) {
          if (binding.identity == candidate) { selectedName = binding.name.clone(); }
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
          if (receiver.mode != signature::ReceiverMode::Shared) return zc::none;
        }
        for (const auto& parameter : callable.parameters) {
          if (parameter.hasDefault || parameter.mode != signature::ParameterMode::Value) {
            return zc::none;
          }
          ++parameterCount;
        }
        selected = candidate;
      }
    }
  }
  if (selected == zc::none || parameterCount != arguments.size) { return zc::none; }
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
  return selectedName;
}

/// \brief Name and declared parameter count of a dot-method call on an owner
/// local, resolved without the lowering shape's receiver-mutability and arity
/// gates.
///
/// The lowering shape (`concreteMethodCallShape`) returns one undifferentiated
/// none for every unlowered call, so ordinary user errors -- wrong argument
/// count, an identifier argument, an unknown method -- would otherwise collapse
/// into a MissingRequiredFact invariant. This resolver returns none only for
/// sites that are not dot-method calls on a resolved owner-local receiver of a
/// non-generic nominal type. A uniquely resolved inherent method reports its
/// declared binding name and parameter count; an unknown or ambiguous member
/// name keeps the source spelling with `methodResolved == false`.
struct ReceiverMethodCallName final {
  identity::DeclaredDefinitionName name;
  bool methodResolved;
  uint32_t parameterCount;
};

zc::Maybe<ReceiverMethodCallName> receiverMethodCallName(
    const BodyCheckingInput& input, zc::ArrayPtr<const checked::NodeTypeMap::Entry> nodeTypes,
    ast::NodeId callNode) {
  const auto& tree = input.boundModule.tree();
  if (!tree.contains(callNode) || tree.node(callNode).kind != ast::SyntaxKind::CallExpression) {
    return zc::none;
  }
  const auto& call = tree.node(callNode);
  const ast::NodeId callee(call.payload.words[ast::kCallExpressionCalleeWord]);
  const ast::NodeList typeArguments{call.payload.words[ast::kCallExpressionTypeArgsFirstWord],
                                    call.payload.words[ast::kCallExpressionTypeArgsSizeWord]};
  if (!tree.contains(callee) || tree.node(callee).kind != ast::SyntaxKind::MemberExpression ||
      !tree.contains(typeArguments) || !typeArguments.empty()) {
    return zc::none;
  }
  const auto& member = tree.node(callee);
  if (static_cast<ast::MemberAccessKind>(member.payload.words[ast::kMemberExpressionAccessWord]) !=
      ast::MemberAccessKind::Dot) {
    return zc::none;
  }
  const ast::NodeId receiverNode(member.payload.words[ast::kMemberExpressionObjectWord]);
  if (!tree.contains(receiverNode) || tree.node(receiverNode).kind != ast::SyntaxKind::IdentExpr) {
    return zc::none;
  }
  const bool ownerLocalReceiver =
      resolvedOwnerLocal(input.boundModule.bindings(), receiverNode) != zc::none;
  const bool parameterReceiver =
      !ownerLocalReceiver &&
      resolvedCallableParameter(input.boundModule.bindings(), receiverNode) != zc::none;
  if (!ownerLocalReceiver && !parameterReceiver) { return zc::none; }
  zc::Maybe<identity::SemanticTypeId> receiverSourceType;
  if (ownerLocalReceiver) {
    receiverSourceType = ownerLocalReferenceType(input, receiverNode, nodeTypes);
  } else {
    receiverSourceType = callableParameterReferenceType(input, receiverNode);
  }
  if (receiverSourceType == zc::none) return zc::none;

  const auto memberName =
      tree.ident(ast::IdentId(member.payload.words[ast::kMemberExpressionPropertyWord]));
  auto sourceName = identity::DeclaredDefinitionName::fromSource(memberName);
  if (sourceName == zc::none) return zc::none;

  auto receiverLookup = input.semanticTypes.get(ZC_ASSERT_NONNULL(receiverSourceType));
  if (!receiverLookup.is<type::SemanticTypeLookup>() ||
      !receiverLookup.get<type::SemanticTypeLookup>()
           .data()
           .is<type::semantic::NominalTypeData>()) {
    // A receiver whose type is not a closed nominal (a primitive such as str,
    // a `dyn` interface, ...) still names a dot-method call that has no
    // lowering yet, whether the receiver is an owner local or a by-value
    // parameter; drain it with the source spelling instead of an invariant.
    return ReceiverMethodCallName{zc::mv(ZC_ASSERT_NONNULL(sourceName)), false, 0};
  }
  const auto& nominal =
      receiverLookup.get<type::SemanticTypeLookup>().data().get<type::semantic::NominalTypeData>();
  if (nominal.arguments.size() != 0) return zc::none;

  zc::Maybe<identity::DefId> selected;
  zc::Maybe<identity::DeclaredDefinitionName> selectedName;
  uint32_t parameterCount = 0;
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
      if (namedMethod) {
        for (const auto& binding : input.boundModule.bindings().definitions()) {
          if (binding.identity == candidate) { selectedName = binding.name.clone(); }
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
        for (const auto& parameter : callable.parameters) {
          if (parameter.hasDefault || parameter.mode != signature::ParameterMode::Value) {
            return zc::none;
          }
          ++parameterCount;
        }
        selected = candidate;
      }
    }
  }
  if (selected == zc::none) {
    return ReceiverMethodCallName{zc::mv(ZC_ASSERT_NONNULL(sourceName)), false, 0};
  }
  if (selectedName == zc::none) return zc::none;
  return ReceiverMethodCallName{zc::mv(ZC_ASSERT_NONNULL(selectedName)), true, parameterCount};
}

/// \brief Returns the binary operator the checker does not yet implement.
///
/// Surface admission is deliberately operator-agnostic: it admits every
/// relational, arithmetic, bitwise, and logical short-circuit `BinaryExpr` of
/// the supported shape and leaves operator support to the checker, keeping that
/// contract in one place. The checker supports the six relational, the twelve
/// arithmetic and bitwise, and the two logical short-circuit operations.
/// `===` and `!==` are specified language syntax that it does not implement
/// yet, and reporting a compiler invariant for spec'd syntax is wrong --
/// `ZOM4103` says so honestly, mirroring the `ZOM4095`-`ZOM4099` family that
/// covers unadmitted body syntax.
///
/// Returns none for a supported operator, and for a syntax that maps to no
/// primitive operation at all, so those keep their existing rejection.
zc::Maybe<PrimitiveOperation> unsupportedBinaryOperator(const BodyCheckingInput& input,
                                                        ast::NodeId node) {
  const auto& tree = input.boundModule.tree();
  if (!tree.contains(node) || tree.node(node).kind != ast::SyntaxKind::BinaryExpr) return zc::none;
  const auto binaryOperator =
      static_cast<ast::BinaryOperatorKind>(tree.node(node).payload.words[ast::kBinaryExprOpWord]);
  if (scalarComparisonOperation(binaryOperator) != zc::none) return zc::none;
  auto arithmetic = scalarArithmeticOperation(binaryOperator);
  if (arithmetic != zc::none) {
    // Logical operators are classified as arithmetic for fact production, but
    // the shape validator only admits them in condition positions. A logical
    // operator outside a condition is an unsupported operator (ZOM4103).
    if ((arithmetic == PrimitiveOperation::LogicalAnd ||
         arithmetic == PrimitiveOperation::LogicalOr) &&
        !isConditionPosition(input.boundModule, node)) {
      return arithmetic;
    }
    return zc::none;
  }
  ZC_IF_SOME(kind, OperatorKind::fromBinary(binaryOperator)) {
    const auto& variant = kind.variant();
    if (!variant.is<PrimitiveOperation>()) return zc::none;
    return variant.get<PrimitiveOperation>();
  }
  return zc::none;
}

/// \brief True for a supported primitive comparison/arithmetic binary that has
/// no operand the local-binary slice can derive a value type from.
///
/// The shape validator resolves an operand from a callable parameter, an earlier
/// owner local, or a one-level nested binary; a scalar literal alone carries no
/// type. An identifier that resolves to neither a parameter nor a local (a
/// module-level function used as a value, `g + 1`) therefore leaves the binary
/// un-producible. Surface admission structurally lets the binary through, so the
/// checker must drain it as ZOM4099 here rather than falling to an invariant.
/// Operands outside the local-binary universe (receiver-field member reads in a
/// method) are deliberately left to their own production path.
bool primitiveBinaryLacksReferenceOperand(
    const BodyCheckingInput& input, ast::NodeId node,
    zc::ArrayPtr<const checked::NodeTypeMap::Entry> nodeTypes) {
  const auto& tree = input.boundModule.tree();
  if (!tree.contains(node) || tree.node(node).kind != ast::SyntaxKind::BinaryExpr) return false;
  const auto binaryOperator =
      static_cast<ast::BinaryOperatorKind>(tree.node(node).payload.words[ast::kBinaryExprOpWord]);
  if (scalarComparisonOperation(binaryOperator) == zc::none &&
      scalarArithmeticOperation(binaryOperator) == zc::none) {
    return false;
  }
  const ast::NodeId left(tree.node(node).payload.words[ast::kBinaryExprLhsWord]);
  const ast::NodeId right(tree.node(node).payload.words[ast::kBinaryExprRhsWord]);
  bool hasReference = false;
  for (const ast::NodeId operand : {left, right}) {
    if (!tree.contains(operand)) return false;
    const auto kind = tree.node(operand).kind;
    if (kind == ast::SyntaxKind::IdentExpr) {
      if (callableParameterReferenceType(input, operand) != zc::none ||
          ownerLocalReferenceType(input, operand, nodeTypes) != zc::none) {
        hasReference = true;
      }
    } else if (kind == ast::SyntaxKind::BinaryExpr) {
      if (unannotatedBinaryInitializerType(input, operand, nodeTypes, 0) != zc::none) {
        hasReference = true;
      }
    } else if (isScalarLiteral(kind)) {
      // A literal contributes no value type; it cannot be the anchor by itself.
    } else {
      // Receiver-field reads and other non-local operands are handled elsewhere.
      return false;
    }
  }
  return !hasReference;
}

// Structural test for a `this.<field>` read operand: a dot member expression
// whose object is the implicit receiver. This is usable from the production
// classification traversal, which only has the bound module and tree (no
// `BodyCheckingInput`); the full semantic resolution (enclosing method, field
// type, receiver mutability) is left to `thisReceiverFieldShape` in the shape
// validator. A `this` base is only bound inside a method, so the structural
// shape is sufficient to schedule production.
bool isReceiverFieldRead(const ast::Tree& tree, ast::NodeId node) {
  if (!tree.contains(node) || tree.node(node).kind != ast::SyntaxKind::MemberExpression) {
    return false;
  }
  const auto& member = tree.node(node);
  if (static_cast<ast::MemberAccessKind>(member.payload.words[ast::kMemberExpressionAccessWord]) !=
      ast::MemberAccessKind::Dot) {
    return false;
  }
  const ast::NodeId object(member.payload.words[ast::kMemberExpressionObjectWord]);
  return tree.contains(object) && tree.node(object).kind == ast::SyntaxKind::ThisExpr;
}

// An owner-local field read (`cell.value`) is a dot member expression whose
// object is an identifier resolving to an owner local. The binding and field
// type are resolved structurally by `ownerLocalFieldShape` in the shape
// validator, mirroring the receiver-field path.
bool isOwnerLocalFieldRead(const ast::Tree& tree, ast::NodeId node) {
  if (!tree.contains(node) || tree.node(node).kind != ast::SyntaxKind::MemberExpression) {
    return false;
  }
  const auto& member = tree.node(node);
  if (static_cast<ast::MemberAccessKind>(member.payload.words[ast::kMemberExpressionAccessWord]) !=
      ast::MemberAccessKind::Dot) {
    return false;
  }
  const ast::NodeId object(member.payload.words[ast::kMemberExpressionObjectWord]);
  return tree.contains(object) && tree.node(object).kind == ast::SyntaxKind::IdentExpr;
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
  // `if` / `while` discriminant. The logical short-circuit operators (`&&` /
  // `||`) produce bool like a comparison, but the HIR builder only lowers them
  // as a conditional discriminant, so they are admitted in condition positions
  // only and rejected with ZOM4103 elsewhere.
  const auto binaryOperator =
      static_cast<ast::BinaryOperatorKind>(syntax.payload.words[ast::kBinaryExprOpWord]);
  auto comparison = scalarComparisonOperation(binaryOperator);
  auto arithmetic = scalarArithmeticOperation(binaryOperator);
  const bool isLogical =
      arithmetic == PrimitiveOperation::LogicalAnd || arithmetic == PrimitiveOperation::LogicalOr;
  const bool isArithmetic = comparison == zc::none && arithmetic != zc::none && !isLogical;
  auto operation = comparison != zc::none ? comparison : arithmetic;
  if (operation == zc::none) return zc::none;
  if (isArithmetic && isConditionPosition(input.boundModule, node)) return zc::none;
  if (isLogical && !isConditionPosition(input.boundModule, node)) return zc::none;
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
  // A field operand is a `this.<field>` projection read inside an inherent
  // method or an owner-local field read (`cell.value`). Its operand type is
  // the field type. Like a nested operand, its node-type fact is produced at
  // a later production stage than the binary, so it is validated
  // structurally here rather than through a node-type fact lookup.
  auto receiverFieldType = [&](ast::NodeId operandNode) -> zc::Maybe<identity::SemanticTypeId> {
    ZC_IF_SOME(shape, thisReceiverFieldShape(input, operandNode)) { return shape.fieldType; }
    ZC_IF_SOME(shape, ownerLocalFieldShape(input, operandNode, nodeTypes)) {
      return shape.fieldType;
    }
    return zc::none;
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
    auto innerOp = innerComparison != zc::none ? innerComparison : innerArithmetic;
    {
      auto kind = primitiveKindOf(input.semanticTypes, innerOperand);
      if (kind == zc::none ||
          !primitiveBinaryOperationAdmits(ZC_ASSERT_NONNULL(innerOp), ZC_ASSERT_NONNULL(kind))) {
        return zc::none;
      }
    }
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
  const bool leftIsReceiverField =
      (isReceiverFieldRead(tree, left) || isOwnerLocalFieldRead(tree, left)) &&
      receiverFieldType(left) != zc::none;
  const bool rightIsReceiverField =
      (isReceiverFieldRead(tree, right) || isOwnerLocalFieldRead(tree, right)) &&
      receiverFieldType(right) != zc::none;
  auto leftNestedType = leftIsNested ? nestedBinaryResultType(left) : zc::none;
  auto rightNestedType = rightIsNested ? nestedBinaryResultType(right) : zc::none;
  if (leftIsNested && leftNestedType == zc::none) return zc::none;
  if (rightIsNested && rightNestedType == zc::none) return zc::none;
  if ((!leftIsReference && !leftIsLiteral && !leftIsNested && !leftIsReceiverField) ||
      (!rightIsReference && !rightIsLiteral && !rightIsNested && !rightIsReceiverField) ||
      (!leftIsReference && !rightIsReference && !leftIsNested && !rightIsNested &&
       !leftIsReceiverField && !rightIsReceiverField)) {
    return zc::none;
  }
  // Derive the shared operand type from a reference operand, then from a
  // receiver-field operand, or from a nested operand's result type when no
  // operand is a plain reference; both operands must agree on this primitive
  // scalar type.
  zc::Maybe<identity::SemanticTypeId> operandType;
  if (leftIsReference) {
    operandType = referenceType(left);
  } else if (rightIsReference) {
    operandType = referenceType(right);
  } else if (leftIsReceiverField) {
    operandType = receiverFieldType(left);
  } else if (rightIsReceiverField) {
    operandType = receiverFieldType(right);
  } else if (leftIsNested) {
    operandType = leftNestedType;
  } else if (rightIsNested) {
    operandType = rightNestedType;
  }
  if (operandType == zc::none) return zc::none;
  identity::SemanticTypeId operand;
  ZC_IF_SOME(value, operandType) { operand = value; }
  {
    auto operandKind = primitiveKindOf(input.semanticTypes, operand);
    if (operandKind == zc::none ||
        !primitiveBinaryOperationAdmits(ZC_ASSERT_NONNULL(operation),
                                        ZC_ASSERT_NONNULL(operandKind))) {
      return zc::none;
    }
  }
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
                            bool isNested, bool isReceiverField) -> bool {
    if (isNested) return true;
    // A receiver-field read is validated structurally: the projected field type
    // must equal the shared operand type. Its node-type fact is produced at a
    // later production stage than the binary, so a node-type lookup here would
    // always miss.
    if (isReceiverField) {
      auto fieldType = receiverFieldType(operandNode);
      bool fieldOk = false;
      ZC_IF_SOME(value, fieldType) { fieldOk = value == operand; }
      return fieldOk;
    }
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
  if (!operandMatches(left, leftIsReference, leftIsLiteral, leftIsNested, leftIsReceiverField) ||
      !operandMatches(right, rightIsReference, rightIsLiteral, rightIsNested,
                      rightIsReceiverField)) {
    return zc::none;
  }
  // A comparison or a logical short-circuit operation produces bool; an
  // arithmetic or bitwise operation produces the shared operand type.
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
  auto declaredType = ownerLocalInitializerDeclaredType(input.boundModule, input.identities,
                                                        input.semanticTypes, node);
  ZC_IF_SOME(declared, declaredType) {
    if (declared != resultType) return zc::none;
  }
  return PrimitiveBinaryOperationShape{left, right, operand, resultType,
                                       ZC_ASSERT_NONNULL(operation)};
}

zc::Maybe<PrimitiveUnaryOperationShape> primitiveUnaryOperationShape(
    const BodyCheckingInput& input, ast::NodeId node,
    zc::ArrayPtr<const checked::NodeTypeMap::Entry> nodeTypes) {
  const auto& tree = input.boundModule.tree();
  if (!tree.contains(node) || tree.node(node).kind != ast::SyntaxKind::UnaryExpression) {
    return zc::none;
  }
  const auto& syntax = tree.node(node);
  const auto unaryOperator =
      static_cast<ast::UnaryOperatorKind>(syntax.payload.words[ast::kUnaryExpressionOpWord]);
  auto kind = OperatorKind::fromUnary(unaryOperator);
  if (kind == zc::none) return zc::none;
  const auto& variant = ZC_ASSERT_NONNULL(kind).variant();
  if (!variant.is<PrimitiveOperation>()) return zc::none;
  const auto operation = variant.get<PrimitiveOperation>();
  const ast::NodeId operand(syntax.payload.words[ast::kUnaryExpressionOperandWord]);
  if (!tree.contains(operand)) return zc::none;
  // The operand is a value reference (parameter or owner local) or a scalar
  // literal. At least one must be a reference; a literal-only unary has no
  // place to lower and is left unsupported.
  auto referenceType = [&](ast::NodeId operandNode) -> zc::Maybe<identity::SemanticTypeId> {
    if (!tree.contains(operandNode) || tree.node(operandNode).kind != ast::SyntaxKind::IdentExpr) {
      return zc::none;
    }
    auto parameter = callableParameterReferenceType(input, operandNode);
    if (parameter != zc::none) return parameter;
    return ownerLocalReferenceType(input, operandNode, nodeTypes);
  };
  zc::Maybe<identity::SemanticTypeId> operandType;
  if (tree.node(operand).kind == ast::SyntaxKind::IdentExpr) {
    operandType = referenceType(operand);
  } else if (isScalarLiteral(tree.node(operand).kind)) {
    ZC_IF_SOME(entry, factEntry(nodeTypes, operand)) { operandType = entry.value; }
  }
  if (operandType == zc::none) return zc::none;
  identity::SemanticTypeId operandTypeValue;
  ZC_IF_SOME(value, operandType) { operandTypeValue = value; }
  auto operandKind = primitiveKindOf(input.semanticTypes, operandTypeValue);
  if (operandKind == zc::none ||
      !primitiveUnaryOperationAdmits(operation, ZC_ASSERT_NONNULL(operandKind))) {
    return zc::none;
  }
  // LogicalNot produces bool; the arithmetic unary operators produce the
  // operand type.
  if (operation == PrimitiveOperation::LogicalNot) {
    auto boolType = internPrimitiveKind(input, type::semantic::PrimitiveKind::Bool);
    if (boolType == zc::none) return zc::none;
    return PrimitiveUnaryOperationShape{operand, operandTypeValue, ZC_ASSERT_NONNULL(boolType),
                                        operation};
  }
  return PrimitiveUnaryOperationShape{operand, operandTypeValue, operandTypeValue, operation};
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
  signature::ReceiverMode receiverMode;
  zc::Vector<identity::SemanticTypeId> parameters;
};

zc::Maybe<ConcreteMethodCallShape> concreteMethodCallShape(
    const BodyCheckingInput& input, ast::NodeId callNode,
    zc::ArrayPtr<const checked::NodeTypeMap::Entry> nodeTypes,
    zc::Maybe<ast::NodeId>* immutableReceiver = nullptr) {
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
      resolvedOwnerLocal(input.boundModule.bindings(), receiverNode) == zc::none) {
    return zc::none;
  }
  const bool receiverLocalIsMutable = isMutableOwnerLocal(input.boundModule, receiverNode);
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

  const auto memberName =
      tree.ident(ast::IdentId(member.payload.words[ast::kMemberExpressionPropertyWord]));
  zc::Maybe<identity::DefId> selected;
  zc::Maybe<identity::SemanticTypeId> success;
  zc::Maybe<signature::ReceiverMode> receiverMode;
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
        // The owner local's mutability must grant the method's receiver mode.
        // A shared receiver accepts either an immutable or mutable local (a
        // mutable place is always shareable); a mutable receiver requires a
        // `mut` local. The one genuine error -- mutable receiver on an
        // immutable `let` -- is reported as ZOM4024 by the caller once the rest
        // of the call resolves. Move and by-value receivers stay outside this
        // slice.
        ZC_IF_SOME(receiver, callable.receiver) {
          if (receiver.mode == signature::ReceiverMode::Mutable) {
            if (!receiverLocalIsMutable) {
              // The receiver is an immutable owner local. Record the receiver
              // identifier node so the caller emits ZOM4024 naming the binding
              // that cannot be mutably borrowed; owner locals have no DefId, so
              // the diagnostic carries the source identifier instead.
              if (immutableReceiver != nullptr) *immutableReceiver = receiverNode;
              return zc::none;
            }
          } else if (receiver.mode != signature::ReceiverMode::Shared) {
            return zc::none;
          }
          receiverMode = receiver.mode;
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
  if (selected == zc::none || success == zc::none || receiverMode == zc::none ||
      arguments.size != parameters.size()) {
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

  const auto receiverMutability =
      ZC_ASSERT_NONNULL(receiverMode) == signature::ReceiverMode::Mutable
          ? type::semantic::Mutability::Mutable
          : type::semantic::Mutability::Const;
  auto canonicalReceiver = input.semanticTypes.canonicalizeClosed(
      type::semantic::TypeData(type::semantic::ReferenceTypeData{
          receiverMutability, ZC_ASSERT_NONNULL(receiverSourceType)}));
  if (!canonicalReceiver.is<type::semantic::CanonicalTypeData>()) return zc::none;
  auto receiverParameter = input.semanticTypes.intern(
      zc::mv(canonicalReceiver).get<type::semantic::CanonicalTypeData>());
  if (!receiverParameter.is<type::SemanticTypeInterned>()) return zc::none;

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
                                   ZC_ASSERT_NONNULL(receiverMode),
                                   zc::mv(parameters)};
  }
  ZC_UNREACHABLE
}

/// \brief Structurally derives the closed expected type of a numeric literal at
/// its coercion site, without reading the literal's own not-yet-produced node
/// type.
///
/// Only the annotation-directed sites supply a hint, in priority order:
/// (a) the initializer of an annotated owner local, (b) a return value,
/// (c) a typed operand of an admitted primitive binary, (d) a direct or
/// concrete-method call argument, and (e) an admitted assignment RHS. An
/// unannotated or otherwise unconstrained literal yields none and keeps the
/// i32/f64 default. The hint only types the literal; it never widens a
/// non-literal value, so the ZOM4009 rails are unchanged.
zc::Maybe<identity::SemanticTypeId> expectedLiteralType(
    const BodyCheckingInput& input, ast::NodeId literal,
    zc::ArrayPtr<const checked::NodeTypeMap::Entry> nodeTypes) {
  const auto& tree = input.boundModule.tree();
  if (!tree.contains(literal)) return zc::none;

  // (a) Annotated owner-local initializer.
  auto annotated = ownerLocalInitializerDeclaredType(input.boundModule, input.identities,
                                                     input.semanticTypes, literal);
  if (annotated != zc::none) return annotated;

  // (b) Return value of the enclosing callable.
  auto returnOwner = returnValueOwner(input.boundModule, literal);
  ZC_IF_SOME(owner, returnOwner) {
    auto success = callableSuccess(input.signatureFacts, owner);
    if (success != zc::none) return success;
  }

  // A typed value anchoring a binary operand: a parameter or owner-local
  // reference, or a one-level nested binary with the same anchoring rule.
  auto referenceOperandType = [&](ast::NodeId operand) -> zc::Maybe<identity::SemanticTypeId> {
    if (!tree.contains(operand)) return zc::none;
    if (tree.node(operand).kind == ast::SyntaxKind::BinaryExpr) {
      return unannotatedBinaryInitializerType(input, operand, nodeTypes, 0);
    }
    if (tree.node(operand).kind != ast::SyntaxKind::IdentExpr) return zc::none;
    auto parameter = callableParameterReferenceType(input, operand);
    if (parameter != zc::none) return parameter;
    return ownerLocalReferenceType(input, operand, nodeTypes);
  };

  zc::Maybe<identity::SemanticTypeId> binaryHint;
  zc::Maybe<identity::SemanticTypeId> argumentHint;
  zc::Maybe<identity::SemanticTypeId> assignmentHint;
  zc::Maybe<identity::SemanticTypeId> castHint;
  ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId node, const ast::Node& syntax) {
    // (f) Inner expression of an `as` cast: the cast's target type.
    if (castHint == zc::none && syntax.kind == ast::SyntaxKind::CastExpression) {
      const ast::NodeId castExpr(syntax.payload.words[ast::kCastExpressionExprWord]);
      if (castExpr == literal) {
        const ast::NodeId castTy(syntax.payload.words[ast::kCastExpressionTyWord]);
        if (tree.contains(castTy)) {
          castHint = signature::resolveClosedSourceType(input.boundModule, input.identities,
                                                        input.semanticTypes, castTy);
        }
      }
    }
    // (c) Operand of an admitted primitive arithmetic/comparison binary; the
    // literal adopts the other, typed operand's type.
    if (binaryHint == zc::none && syntax.kind == ast::SyntaxKind::BinaryExpr) {
      const ast::NodeId left(syntax.payload.words[ast::kBinaryExprLhsWord]);
      const ast::NodeId right(syntax.payload.words[ast::kBinaryExprRhsWord]);
      zc::Maybe<ast::NodeId> other;
      if (left == literal) {
        other = right;
      } else if (right == literal) {
        other = left;
      }
      ZC_IF_SOME(otherNode, other) {
        const auto binaryOperator =
            static_cast<ast::BinaryOperatorKind>(syntax.payload.words[ast::kBinaryExprOpWord]);
        auto comparison = scalarComparisonOperation(binaryOperator);
        auto arithmetic = scalarArithmeticOperation(binaryOperator);
        if (comparison != zc::none || arithmetic != zc::none) {
          auto otherType = referenceOperandType(otherNode);
          ZC_IF_SOME(type, otherType) {
            auto kind = primitiveKindOf(input.semanticTypes, type);
            ZC_IF_SOME(kindValue, kind) {
              const auto operation =
                  ZC_ASSERT_NONNULL(comparison != zc::none ? comparison : arithmetic);
              if (primitiveBinaryOperationAdmits(operation, kindValue)) { binaryHint = type; }
            }
          }
        }
      }
    }

    // (d) Direct or concrete-method call argument: the corresponding parameter
    // type. Both shape resolvers are purely structural and already carry the
    // callee parameter vector.
    if (argumentHint == zc::none && syntax.kind == ast::SyntaxKind::CallExpression) {
      const ast::NodeList arguments{syntax.payload.words[ast::kCallExpressionArgsFirstWord],
                                    syntax.payload.words[ast::kCallExpressionArgsSizeWord]};
      if (tree.contains(arguments)) {
        const auto argumentNodes = tree.list(arguments);
        for (size_t index = 0; index < argumentNodes.size(); ++index) {
          if (argumentNodes[index] != literal) continue;
          auto direct = directCallShape(input, node);
          if (direct != zc::none) {
            const auto& parameters = ZC_ASSERT_NONNULL(direct).parameters;
            if (index < parameters.size()) argumentHint = parameters[index];
          } else {
            auto method = concreteMethodCallShape(input, node, nodeTypes);
            if (method != zc::none) {
              const auto& parameters = ZC_ASSERT_NONNULL(method).parameters;
              if (index < parameters.size()) argumentHint = parameters[index];
            } else {
              // An enum tuple-variant construction argument adopts the
              // corresponding payload field type.
              auto construction = enumVariantConstructionShape(input, node);
              if (construction != zc::none) {
                const auto& payload = ZC_ASSERT_NONNULL(construction).payload;
                if (index < payload.size()) argumentHint = payload[index];
              }
            }
          }
          break;
        }
      }
    }

    // (e) RHS of an admitted local or field write: the LHS storage type. A
    // compound assignment (`x += 1`) desugars to a binary write, so its RHS
    // literal receives the same target-type hint as a plain write.
    if (assignmentHint == zc::none && syntax.kind == ast::SyntaxKind::AssignmentExpr &&
        ast::NodeId(syntax.payload.words[ast::kAssignmentExprRhsWord]) == literal) {
      const auto assignmentOp = static_cast<ast::AssignmentOperatorKind>(
          syntax.payload.words[ast::kAssignmentExprOpWord]);
      if (assignmentOp == ast::AssignmentOperatorKind::Assign ||
          isAdmittedCompoundAssignment(assignmentOp)) {
        const ast::NodeId target(syntax.payload.words[ast::kAssignmentExprLhsWord]);
        if (tree.contains(target)) {
          if (isSimpleLocalWrite(input.boundModule, node)) {
            assignmentHint = ownerLocalReferenceType(input, target, nodeTypes);
          } else if (isSimpleOwnerLocalFieldWrite(input.boundModule, node)) {
            auto shape = ownerLocalFieldShape(input, target, nodeTypes);
            ZC_IF_SOME(field, shape) { assignmentHint = field.fieldType; }
          } else if (isSimpleReceiverFieldWrite(input.boundModule, node)) {
            auto shape = thisReceiverFieldShape(input, target);
            ZC_IF_SOME(field, shape) { assignmentHint = field.fieldType; }
          }
        }
      }
    }
  });

  if (binaryHint != zc::none) return binaryHint;
  if (argumentHint != zc::none) return argumentHint;
  if (assignmentHint != zc::none) return assignmentHint;
  if (castHint != zc::none) return castHint;
  return zc::none;
}

/// \brief Resolves a zero-argument inherent method self-call
/// (`this.method()`) inside an inherent method body.
///
/// The implicit `this` receiver is already a reference parameter, so both the
/// receiver source and the callable receiver parameter are the enclosing
/// method's receiver reference type; lowering forwards the parameter directly
/// instead of borrowing an owner local. Only a shared enclosing receiver
/// calling a shared, non-generic, zero-parameter method is admitted; every
/// other self-call shape returns none and keeps its capability rejection.
zc::Maybe<ConcreteMethodCallShape> thisReceiverMethodCallShape(const BodyCheckingInput& input,
                                                               ast::NodeId callNode) {
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
      !tree.contains(typeArguments) || !typeArguments.empty() || !tree.contains(arguments) ||
      !arguments.empty()) {
    return zc::none;
  }
  const auto& member = tree.node(callee);
  if (static_cast<ast::MemberAccessKind>(member.payload.words[ast::kMemberExpressionAccessWord]) !=
      ast::MemberAccessKind::Dot) {
    return zc::none;
  }
  const ast::NodeId receiverNode(member.payload.words[ast::kMemberExpressionObjectWord]);
  if (!tree.contains(receiverNode) || tree.node(receiverNode).kind != ast::SyntaxKind::ThisExpr) {
    return zc::none;
  }
  auto receiver = enclosingMethodReceiver(input, receiverNode);
  if (receiver == zc::none || ZC_ASSERT_NONNULL(receiver).receiverMutable) { return zc::none; }

  const auto memberName =
      tree.ident(ast::IdentId(member.payload.words[ast::kMemberExpressionPropertyWord]));
  zc::Maybe<identity::DefId> selected;
  zc::Maybe<identity::SemanticTypeId> success;
  for (const auto& nominalSignature : input.signatureFacts.signatures()) {
    if (nominalSignature.definition != ZC_ASSERT_NONNULL(receiver).ownerDefinition ||
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
        if (scope.owner != ZC_ASSERT_NONNULL(receiver).ownerDefinition ||
            callable.genericParameters.size() != 0 || callable.receiver == zc::none ||
            callable.raises != zc::none || callable.abi != zc::none) {
          return zc::none;
        }
        ZC_IF_SOME(targetReceiver, callable.receiver) {
          if (targetReceiver.mode != signature::ReceiverMode::Shared) { return zc::none; }
        }
        if (callable.parameters.size() != 0) { return zc::none; }
        selected = candidate;
        success = callable.success;
      }
    }
  }
  if (selected == zc::none || success == zc::none) { return zc::none; }
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

  zc::Vector<identity::SemanticTypeId> noParameters;
  zc::Maybe<identity::SemanticTypeId> noRaises;
  auto canonical = input.semanticTypes.canonicalizeClosed(
      type::semantic::TypeData(type::semantic::FunctionTypeData{
          zc::mv(noParameters), ZC_ASSERT_NONNULL(success), zc::mv(noRaises)}));
  if (!canonical.is<type::semantic::CanonicalTypeData>()) return zc::none;
  auto interned =
      input.semanticTypes.intern(zc::mv(canonical).get<type::semantic::CanonicalTypeData>());
  if (!interned.is<type::SemanticTypeInterned>()) return zc::none;
  ZC_IF_SOME(method, selected) {
    const auto receiverReferenceType = ZC_ASSERT_NONNULL(receiver).receiverReferenceType;
    return ConcreteMethodCallShape{receiverNode,
                                   method,
                                   receiverReferenceType,
                                   receiverReferenceType,
                                   interned.get<type::SemanticTypeInterned>().id,
                                   ZC_ASSERT_NONNULL(success),
                                   signature::ReceiverMode::Shared,
                                   zc::Vector<identity::SemanticTypeId>{}};
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

// ZOM4036: a call supplies a different number of arguments than the resolved
// callee declares. Both display arguments are plain counts, matching the
// projector contract (Count, Count).
checked::CheckedFactsSourceRejected rejectCallArgumentCount(const BodyProductionSite& site,
                                                            uint32_t ownerPreorder,
                                                            uint64_t expectedCount,
                                                            uint64_t actualCount) {
  zc::Vector<checked::CheckerDisplayArgument> arguments;
  arguments.add(checked::CheckerDisplayArgument(checked::CountDisplayArg{expectedCount}));
  arguments.add(checked::CheckerDisplayArgument(checked::CountDisplayArg{actualCount}));
  zc::Vector<checked::CheckerNoteRef> notes;
  zc::Maybe<checked::TypeErrorId> noRecovery;
  zc::Vector<checked::CheckerFailureRef> failures;
  failures.add(checked::CheckerFailureRef{
      checked::CheckerErrorId::CallArgumentCountMismatch(), checked::CheckerDiagnosticStage::Body,
      site.node, site.key.sourceSpan.clone(), zc::mv(arguments), zc::mv(notes),
      checked::CheckerDiagnosticProducer::Call,
      checked::CheckerRecoveryPolicy(
          checked::CreateRootRecoveryPolicy{checked::CheckerRecoveryClass::TypeMismatch, true}),
      checked::CheckerEmitterOrdinal{static_cast<uint8_t>(checked::CheckerDiagnosticStage::Body),
                                     ownerPreorder, site.key.schemaPreorder, 0},
      zc::mv(noRecovery)});
  return checked::CheckedFactsSourceRejected{zc::mv(failures),
                                             zc::Vector<checked::CheckerAdvisoryRef>(),
                                             zc::Vector<checked::FrozenRecoveryLedger>()};
}

// ZOM4018: a concrete value assigned to an annotated dyn existential has no
// impl of the principal interface. Argument kinds are (Type, Definition),
// matching the checked-facts projector contract.
checked::CheckedFactsSourceRejected rejectTraitNotImplemented(const BodyProductionSite& site,
                                                              uint32_t ownerPreorder,
                                                              identity::SemanticTypeId concreteType,
                                                              identity::DefId interfaceDefinition) {
  zc::Maybe<identity::SemanticIdentifier> noConcreteAlias;
  zc::Vector<checked::CheckerDisplayArgument> arguments;
  arguments.add(checked::CheckerDisplayArgument(
      checked::TypeDisplayArg{concreteType, zc::mv(noConcreteAlias)}));
  arguments.add(
      checked::CheckerDisplayArgument(checked::DefinitionDisplayArg{interfaceDefinition}));
  zc::Vector<checked::CheckerNoteRef> notes;
  zc::Maybe<checked::TypeErrorId> noRecovery;
  zc::Vector<checked::CheckerFailureRef> failures;
  failures.add(checked::CheckerFailureRef{
      checked::CheckerErrorId::CheckerTraitNotImplemented(), checked::CheckerDiagnosticStage::Body,
      site.node, site.key.sourceSpan.clone(), zc::mv(arguments), zc::mv(notes),
      checked::CheckerDiagnosticProducer::Obligation,
      checked::CheckerRecoveryPolicy(
          checked::CreateRootRecoveryPolicy{checked::CheckerRecoveryClass::FailedObligation, true}),
      checked::CheckerEmitterOrdinal{static_cast<uint8_t>(checked::CheckerDiagnosticStage::Body),
                                     ownerPreorder, site.key.schemaPreorder, 0},
      zc::mv(noRecovery)});
  return checked::CheckedFactsSourceRejected{zc::mv(failures),
                                             zc::Vector<checked::CheckerAdvisoryRef>(),
                                             zc::Vector<checked::FrozenRecoveryLedger>()};
}

// ZOM4124: coherence has an impl of the principal interface for the concrete
// nominal, but the concrete type is generic (or the selected impl is), which
// the non-generic concrete-to-dyn erasure slice cannot lower. This is an
// unsupported-in-slice operation, not a missing trait obligation.
checked::CheckedFactsSourceRejected rejectGenericErasureUnsupported(
    const BodyProductionSite& site, uint32_t ownerPreorder, identity::SemanticTypeId concreteType,
    identity::DefId interfaceDefinition) {
  zc::Maybe<identity::SemanticIdentifier> noConcreteAlias;
  zc::Vector<checked::CheckerDisplayArgument> arguments;
  arguments.add(checked::CheckerDisplayArgument(
      checked::TypeDisplayArg{concreteType, zc::mv(noConcreteAlias)}));
  arguments.add(
      checked::CheckerDisplayArgument(checked::DefinitionDisplayArg{interfaceDefinition}));
  zc::Vector<checked::CheckerNoteRef> notes;
  zc::Maybe<checked::TypeErrorId> noRecovery;
  zc::Vector<checked::CheckerFailureRef> failures;
  failures.add(checked::CheckerFailureRef{
      checked::CheckerErrorId::GenericConcreteDynErasureUnsupported(),
      checked::CheckerDiagnosticStage::Body, site.node, site.key.sourceSpan.clone(),
      zc::mv(arguments), zc::mv(notes), checked::CheckerDiagnosticProducer::Obligation,
      checked::CheckerRecoveryPolicy(
          checked::CreateRootRecoveryPolicy{checked::CheckerRecoveryClass::InvalidOperation, true}),
      checked::CheckerEmitterOrdinal{static_cast<uint8_t>(checked::CheckerDiagnosticStage::Body),
                                     ownerPreorder, site.key.schemaPreorder, 0},
      zc::mv(noRecovery)});
  return checked::CheckedFactsSourceRejected{zc::mv(failures),
                                             zc::Vector<checked::CheckerAdvisoryRef>(),
                                             zc::Vector<checked::FrozenRecoveryLedger>()};
}

checked::CheckedFactsSourceRejected rejectInvalidBinaryOperands(
    const BodyProductionSite& site, uint32_t ownerPreorder,
    const InvalidBinaryOperandTypes& operands) {
  zc::Maybe<identity::SemanticIdentifier> noLeftAlias;
  zc::Maybe<identity::SemanticIdentifier> noRightAlias;
  zc::Vector<checked::CheckerDisplayArgument> arguments;
  arguments.add(checked::CheckerDisplayArgument(
      checked::OperatorDisplayArg{OperatorKind(operands.operation)}));
  arguments.add(checked::CheckerDisplayArgument(
      checked::TypeDisplayArg{operands.leftType, zc::mv(noLeftAlias)}));
  arguments.add(checked::CheckerDisplayArgument(
      checked::TypeDisplayArg{operands.rightType, zc::mv(noRightAlias)}));
  zc::Vector<checked::CheckerNoteRef> notes;
  zc::Maybe<checked::TypeErrorId> noRecovery;
  zc::Vector<checked::CheckerFailureRef> failures;
  const auto diagnostic = operands.isComparison
                              ? checked::CheckerErrorId::InvalidComparisonOperands()
                              : checked::CheckerErrorId::InvalidBinaryOperands();
  failures.add(checked::CheckerFailureRef{
      diagnostic, checked::CheckerDiagnosticStage::Body, site.node, site.key.sourceSpan.clone(),
      zc::mv(arguments), zc::mv(notes), checked::CheckerDiagnosticProducer::Operator,
      checked::CheckerRecoveryPolicy(
          checked::CreateRootRecoveryPolicy{checked::CheckerRecoveryClass::InvalidOperation, true}),
      checked::CheckerEmitterOrdinal{static_cast<uint8_t>(checked::CheckerDiagnosticStage::Body),
                                     ownerPreorder, site.key.schemaPreorder, 0},
      zc::mv(noRecovery)});
  return checked::CheckedFactsSourceRejected{zc::mv(failures),
                                             zc::Vector<checked::CheckerAdvisoryRef>(),
                                             zc::Vector<checked::FrozenRecoveryLedger>()};
}

checked::CheckedFactsSourceRejected rejectUnsupportedBinaryOperator(const BodyProductionSite& site,
                                                                    uint32_t ownerPreorder,
                                                                    PrimitiveOperation operation) {
  zc::Vector<checked::CheckerDisplayArgument> arguments;
  arguments.add(
      checked::CheckerDisplayArgument(checked::OperatorDisplayArg{OperatorKind(operation)}));
  zc::Vector<checked::CheckerNoteRef> notes;
  zc::Maybe<checked::TypeErrorId> noRecovery;
  zc::Vector<checked::CheckerFailureRef> failures;
  failures.add(checked::CheckerFailureRef{
      checked::CheckerErrorId::BinaryOperatorSemanticsUnavailable(),
      checked::CheckerDiagnosticStage::Body, site.node, site.key.sourceSpan.clone(),
      zc::mv(arguments), zc::mv(notes), checked::CheckerDiagnosticProducer::Operator,
      checked::CheckerRecoveryPolicy(
          checked::CreateRootRecoveryPolicy{checked::CheckerRecoveryClass::InvalidOperation, true}),
      checked::CheckerEmitterOrdinal{static_cast<uint8_t>(checked::CheckerDiagnosticStage::Body),
                                     ownerPreorder, site.key.schemaPreorder, 0},
      zc::mv(noRecovery)});
  return checked::CheckedFactsSourceRejected{zc::mv(failures),
                                             zc::Vector<checked::CheckerAdvisoryRef>(),
                                             zc::Vector<checked::FrozenRecoveryLedger>()};
}

checked::CheckedFactsSourceRejected rejectUnsupportedInherentMethodCall(
    const BodyProductionSite& site, uint32_t ownerPreorder,
    identity::DeclaredDefinitionName methodName) {
  zc::Vector<checked::CheckerDisplayArgument> arguments;
  arguments.add(checked::CheckerDisplayArgument(
      checked::DeclaredDefinitionNameDisplayArg{zc::mv(methodName)}));
  zc::Vector<checked::CheckerNoteRef> notes;
  zc::Maybe<checked::TypeErrorId> noRecovery;
  zc::Vector<checked::CheckerFailureRef> failures;
  failures.add(checked::CheckerFailureRef{
      checked::CheckerErrorId::MethodCallSemanticsUnavailable(),
      checked::CheckerDiagnosticStage::Body, site.node, site.key.sourceSpan.clone(),
      zc::mv(arguments), zc::mv(notes), checked::CheckerDiagnosticProducer::Call,
      checked::CheckerRecoveryPolicy(
          checked::CreateRootRecoveryPolicy{checked::CheckerRecoveryClass::InvalidOperation, true}),
      checked::CheckerEmitterOrdinal{static_cast<uint8_t>(checked::CheckerDiagnosticStage::Body),
                                     ownerPreorder, site.key.schemaPreorder, 0},
      zc::mv(noRecovery)});
  return checked::CheckedFactsSourceRejected{zc::mv(failures),
                                             zc::Vector<checked::CheckerAdvisoryRef>(),
                                             zc::Vector<checked::FrozenRecoveryLedger>()};
}

/// \brief Rejects a well-typed value reference that the lowered body surface
/// cannot materialize (a module-level/imported symbol read) with ZOM4099. The
/// construct is valid source; only code generation for it is unimplemented.
checked::CheckedFactsSourceRejected rejectUnsupportedFunctionBodyConstruct(
    const BodyProductionSite& site, uint32_t ownerPreorder) {
  zc::Vector<checked::CheckerDisplayArgument> arguments;
  zc::Vector<checked::CheckerNoteRef> notes;
  zc::Maybe<checked::TypeErrorId> noRecovery;
  zc::Vector<checked::CheckerFailureRef> failures;
  failures.add(checked::CheckerFailureRef{
      checked::CheckerErrorId::FunctionBodySemanticsUnavailable(),
      checked::CheckerDiagnosticStage::Body, site.node, site.key.sourceSpan.clone(),
      zc::mv(arguments), zc::mv(notes), checked::CheckerDiagnosticProducer::Inference,
      checked::CheckerRecoveryPolicy(
          checked::CreateRootRecoveryPolicy{checked::CheckerRecoveryClass::InvalidOperation, true}),
      checked::CheckerEmitterOrdinal{static_cast<uint8_t>(checked::CheckerDiagnosticStage::Body),
                                     ownerPreorder, site.key.schemaPreorder, 0},
      zc::mv(noRecovery)});
  return checked::CheckedFactsSourceRejected{zc::mv(failures),
                                             zc::Vector<checked::CheckerAdvisoryRef>(),
                                             zc::Vector<checked::FrozenRecoveryLedger>()};
}

/// \brief Builds an aggregate-literal source rejection (ZOM4048 unknown field,
/// ZOM4049 missing field, or ZOM4131 duplicate field). The display argument is
/// the source property name for the identifier-arg codes and the field
/// definition for ZOM4049.
checked::CheckedFactsSourceRejected rejectAggregateLiteralField(
    const BodyProductionSite& site, uint32_t ownerPreorder, checked::CheckerErrorId diagnostic,
    zc::Vector<checked::CheckerDisplayArgument>&& arguments) {
  zc::Vector<checked::CheckerNoteRef> notes;
  zc::Maybe<checked::TypeErrorId> noRecovery;
  zc::Vector<checked::CheckerFailureRef> failures;
  failures.add(checked::CheckerFailureRef{
      diagnostic, checked::CheckerDiagnosticStage::Body, site.node, site.key.sourceSpan.clone(),
      zc::mv(arguments), zc::mv(notes), checked::CheckerDiagnosticProducer::Aggregate,
      checked::CheckerRecoveryPolicy(
          checked::CreateRootRecoveryPolicy{checked::CheckerRecoveryClass::InvalidOperation, true}),
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
  // A failing node is owned by an initializer, or by a callable body. The two
  // recognized body positions are a return value and a condition expression;
  // both recover against the enclosing callable, so fall back to the enclosing
  // function when the node is neither an initializer nor a return value. An
  // ambiguous or absent owner still fails closed below.
  auto callable = returnValueOwner(input.boundModule, failure.primaryNode);
  if (initializer == zc::none && callable == zc::none) {
    callable = enclosingBodyOwner(input.boundModule, failure.primaryNode);
  }
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

/// \brief Reject a direct free-function call whose identifier argument resolves
/// to a value outside the parameter-argument lowering slice (an owner local or
/// a module/imported symbol) as ZOM4125. Returns none when the enclosing owner
/// cannot be resolved so the caller keeps its existing invariant handling.
zc::Maybe<zc::OneOf<checked::CheckedFactsSourceRejected, checked::CheckedFactsInvariantRejected>>
rejectUnsupportedDirectCallArgument(const BodyProductionSite& site, const BodyCheckingInput& input,
                                    const identity::RegistryBrandIssuer& factStoreBrands,
                                    ast::NodeId calleeNode) {
  const auto& tree = input.boundModule.tree();
  zc::Maybe<identity::DeclaredDefinitionName> name;
  if (tree.contains(calleeNode) && tree.node(calleeNode).kind == ast::SyntaxKind::IdentExpr) {
    name = identity::DeclaredDefinitionName::fromSource(
        tree.ident(ast::IdentId(tree.node(calleeNode).payload.words[ast::kIdentExprNameWord])));
  }
  ZC_IF_SOME(owner, enclosingBodyOwner(input.boundModule, site.node)) {
    ZC_IF_SOME(ownerOrdinal, definitionPreorder(input.boundModule, owner)) {
      ZC_IF_SOME(callName, name) {
        return attachRecoveryLedger(
            rejectUnsupportedInherentMethodCall(site, ownerOrdinal, zc::mv(callName)), input,
            factStoreBrands);
      }
    }
  }
  return zc::none;
}

/// \brief True when a direct-call argument is an owner local of closed nominal
/// struct type, the admitted by-value struct argument carrier. The local's
/// initializer shape and field set are checked by the HIR capability gate; here
/// only the semantic argument kind is decided so the call fact records the
/// aggregate argument instead of draining the call as an unlowered local read.
bool isByValueStructLocalArgument(const BodyCheckingInput& input, ast::NodeId argument,
                                  identity::SemanticTypeId argumentType) {
  const auto& tree = input.boundModule.tree();
  if (!tree.contains(argument) || tree.node(argument).kind != ast::SyntaxKind::IdentExpr) {
    return false;
  }
  if (resolvedOwnerLocal(input.boundModule.bindings(), argument) == zc::none) { return false; }
  auto lookup = input.semanticTypes.get(argumentType);
  if (!lookup.is<type::SemanticTypeLookup>()) return false;
  const auto& data = lookup.get<type::SemanticTypeLookup>().data();
  if (!data.is<type::semantic::NominalTypeData>()) { return false; }
  const auto& nominal = data.get<type::semantic::NominalTypeData>();
  if (nominal.arguments.size() != 0) return false;
  for (const auto& definition : input.boundModule.definitions().definitions()) {
    if (definition.definition != nominal.definition) continue;
    return definition.record.kind() == identity::DefinitionKind::Struct;
  }
  return false;
}

/// \brief True when a direct-call argument is an owner local of scalar i32 type,
/// the admitted by-value scalar-local argument carrier. Only the semantic
/// argument kind is decided here; the local's literal-only initializer shape is
/// checked by the HIR capability gate. Resolving through an owner local excludes
/// globals and imported symbols, which keep the ZOM4125 drain.
bool isScalarOwnerLocalArgument(const BodyCheckingInput& input, ast::NodeId argument,
                                identity::SemanticTypeId argumentType) {
  const auto& tree = input.boundModule.tree();
  if (!tree.contains(argument) || tree.node(argument).kind != ast::SyntaxKind::IdentExpr) {
    return false;
  }
  if (resolvedOwnerLocal(input.boundModule.bindings(), argument) == zc::none) { return false; }
  auto lookup = input.semanticTypes.get(argumentType);
  if (!lookup.is<type::SemanticTypeLookup>()) return false;
  const auto& data = lookup.get<type::SemanticTypeLookup>().data();
  if (!data.is<type::semantic::PrimitiveTypeData>()) return false;
  const auto kind = data.get<type::semantic::PrimitiveTypeData>().kind;
  return kind == type::semantic::PrimitiveKind::I32 || kind == type::semantic::PrimitiveKind::Bool;
}

/// \brief Attaches a ZOM4125 recovery ledger for a method-call site, resolving
/// its enclosing callable owner exactly like the binary-operator rail.
///
/// The emitter ordinal must name the real enclosing body owner; hardcoding
/// ordinal zero makes the recovery issuer reject the root and collapse the
/// honest capability diagnostic back into an invariant. A site with no
/// resolvable body owner keeps its existing invariant rejection.
zc::OneOf<checked::CheckedFactsSourceRejected, checked::CheckedFactsInvariantRejected>
rejectMethodCallCapability(const BodyProductionSite& site, const BodyCheckingInput& input,
                           const identity::RegistryBrandIssuer& factStoreBrands,
                           zc::Maybe<identity::DeclaredDefinitionName>&& methodName) {
  ZC_IF_SOME(name, methodName) {
    ZC_IF_SOME(owner, enclosingBodyOwner(input.boundModule, site.node)) {
      ZC_IF_SOME(ownerOrdinal, definitionPreorder(input.boundModule, owner)) {
        return attachRecoveryLedger(
            rejectUnsupportedInherentMethodCall(site, ownerOrdinal, zc::mv(name)), input,
            factStoreBrands);
      }
    }
  }
  zc::Vector<uint32_t> structuralPath;
  structuralPath.add(static_cast<uint32_t>(site.primaryGroup));
  return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact,
                         input.boundModule.module(), site.key.schemaPreorder, zc::none, site.node,
                         site.key.sourceSpan.clone(), zc::mv(structuralPath));
}

checked::CheckedFactsSourceRejected rejectCannotMutateImmutableVariable(
    const BodyProductionSite& site, uint32_t ownerPreorder, const ast::Tree& tree,
    ast::NodeId receiverNode) {
  // Owner locals carry no DefId; the ZOM4024 argument is the receiver's source
  // identifier, validated by the same Identifier display-argument schema.
  auto receiverName = identity::SemanticIdentifier::fromSource(
      tree.ident(ast::IdentId(tree.node(receiverNode).payload.words[ast::kIdentExprNameWord])));
  zc::Vector<checked::CheckerDisplayArgument> arguments;
  if (receiverName != zc::none) {
    arguments.add(checked::CheckerDisplayArgument(
        checked::IdentifierDisplayArg{zc::mv(ZC_ASSERT_NONNULL(receiverName))}));
  }
  zc::Vector<checked::CheckerNoteRef> notes;
  zc::Maybe<checked::TypeErrorId> noRecovery;
  zc::Vector<checked::CheckerFailureRef> failures;
  failures.add(checked::CheckerFailureRef{
      checked::CheckerErrorId::CannotMutateImmutableVariable(),
      checked::CheckerDiagnosticStage::Body, site.node, site.key.sourceSpan.clone(),
      zc::mv(arguments), zc::mv(notes), checked::CheckerDiagnosticProducer::Mutation,
      checked::CheckerRecoveryPolicy(
          checked::CreateRootRecoveryPolicy{checked::CheckerRecoveryClass::InvalidOperation, true}),
      checked::CheckerEmitterOrdinal{static_cast<uint8_t>(checked::CheckerDiagnosticStage::Body),
                                     ownerPreorder, site.key.schemaPreorder, 0},
      zc::mv(noRecovery)});
  return checked::CheckedFactsSourceRejected{zc::mv(failures),
                                             zc::Vector<checked::CheckerAdvisoryRef>(),
                                             zc::Vector<checked::FrozenRecoveryLedger>()};
}

/// \brief Rejects a field write through a shared (immutable) method receiver
/// with ZOM4126. The offending place is the implicit receiver keyword, so the
/// diagnostic carries no display arguments.
checked::CheckedFactsSourceRejected rejectCannotMutateSharedReceiver(const BodyProductionSite& site,
                                                                     uint32_t ownerPreorder) {
  zc::Vector<checked::CheckerDisplayArgument> arguments;
  zc::Vector<checked::CheckerNoteRef> notes;
  zc::Maybe<checked::TypeErrorId> noRecovery;
  zc::Vector<checked::CheckerFailureRef> failures;
  failures.add(checked::CheckerFailureRef{
      checked::CheckerErrorId::CannotMutateSharedReceiver(), checked::CheckerDiagnosticStage::Body,
      site.node, site.key.sourceSpan.clone(), zc::mv(arguments), zc::mv(notes),
      checked::CheckerDiagnosticProducer::Mutation,
      checked::CheckerRecoveryPolicy(
          checked::CreateRootRecoveryPolicy{checked::CheckerRecoveryClass::InvalidOperation, true}),
      checked::CheckerEmitterOrdinal{static_cast<uint8_t>(checked::CheckerDiagnosticStage::Body),
                                     ownerPreorder, site.key.schemaPreorder, 0},
      zc::mv(noRecovery)});
  return checked::CheckedFactsSourceRejected{zc::mv(failures),
                                             zc::Vector<checked::CheckerAdvisoryRef>(),
                                             zc::Vector<checked::FrozenRecoveryLedger>()};
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
    case ast::SyntaxKind::MatchExpr:
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
    // A compound assignment (`x += 1`) desugars to a binary write
    // (`x = x + 1`) in the HIR builder. The CompoundAssignmentFactMap is
    // intentionally empty, so no checker requirement is added here; the
    // admitted compound operators are gated by isSimpleLocalWrite and the
    // surface admission boundary.
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
    const BodyFactRequirementInventoryBuildInput& buildInput) {
  const auto& boundModule = buildInput.boundModule;
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
  // Pre-compute return value nodes whose enclosing function returns an erasable
  // dyn existential. The visitor below adds a Coercion requirement for each
  // such node (except identity returns), matching the coercion fact the body
  // checker records at the same site.
  struct ReturnValueInfo {
    ast::NodeId node;
    identity::SemanticTypeId expectedType;
  };
  zc::Vector<ReturnValueInfo> erasableReturnValues;
  ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId, const ast::Node& syntax) {
    if (syntax.kind != ast::SyntaxKind::ReturnStmt) return;
    const ast::NodeId value(syntax.payload.words[ast::kReturnStmtValueWord]);
    if (!tree.contains(value)) return;
    auto owner = enclosingBodyOwner(boundModule, value);
    if (owner == zc::none) return;
    auto returnType = functionReturnType(boundModule, buildInput.identities,
                                         buildInput.semanticTypes, ZC_ASSERT_NONNULL(owner));
    if (returnType == zc::none) return;
    if (erasableExistentialType(buildInput.semanticTypes, ZC_ASSERT_NONNULL(returnType)) ==
        zc::none) {
      return;
    }
    erasableReturnValues.add(ReturnValueInfo{value, ZC_ASSERT_NONNULL(returnType)});
  });
  // Collect IdentExpr nodes that are the base of a qualified member access
  // (`Color` in `Color::Red`). These are type-namespace references resolved by
  // the binder, not value references; the body checker must not classify them
  // as IdentifierReference production sites or require NodeType facts for them.
  zc::Vector<ast::NodeId> qualifiedMemberBases;
  ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId node, const ast::Node& syntax) {
    if (syntax.kind != ast::SyntaxKind::MemberExpression) return;
    if (static_cast<ast::MemberAccessKind>(
            syntax.payload.words[ast::kMemberExpressionAccessWord]) !=
        ast::MemberAccessKind::Qualified) {
      return;
    }
    const ast::NodeId object(syntax.payload.words[ast::kMemberExpressionObjectWord]);
    if (tree.contains(object) && tree.node(object).kind == ast::SyntaxKind::IdentExpr) {
      qualifiedMemberBases.add(object);
    }
  });
  // Collect discriminant expression nodes of enum variants with explicit
  // discriminants (`Red = 10`). These are part of the variant signature, not
  // the function body; the signature facts builder encodes them in the variant
  // signature. The body checker must not produce node-type or literal facts
  // for them, since the HIR count equations only cover function bodies and
  // value-declaration initializers.
  zc::Vector<ast::NodeId> enumDiscriminantNodes;
  ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId node, const ast::Node& syntax) {
    if (syntax.kind == ast::SyntaxKind::UnitVariant) {
      const ast::NodeId discriminant(syntax.payload.words[ast::kUnitVariantDiscriminantWord]);
      if (tree.contains(discriminant)) { enumDiscriminantNodes.add(discriminant); }
    } else if (syntax.kind == ast::SyntaxKind::TupleVariant) {
      const ast::NodeId discriminant(syntax.payload.words[ast::kTupleVariantDiscriminantWord]);
      if (tree.contains(discriminant)) { enumDiscriminantNodes.add(discriminant); }
    }
  });
  // Collect callee MemberExpression nodes of enum tuple-variant construction
  // calls (`Result::Ok(41)`). The constructor callee is a type-namespace
  // reference resolved by the binder, not a value reference; the body checker
  // must not classify it as an EnumVariantValue production site or require
  // Literal/Member/Place facts for it. The CallExpression itself is classified
  // as EnumVariantConstruction and carries only a NodeType requirement. A unit
  // variant is a value, not a callable, so only TupleVariant definitions admit
  // the construction shape and a call on a unit variant keeps its existing
  // ConcreteMethodCall rejection.
  zc::Vector<ast::NodeId> constructionCallees;
  ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId node, const ast::Node& syntax) {
    if (syntax.kind != ast::SyntaxKind::CallExpression) return;
    const ast::NodeId callee(syntax.payload.words[ast::kCallExpressionCalleeWord]);
    if (!tree.contains(callee) || tree.node(callee).kind != ast::SyntaxKind::MemberExpression) {
      return;
    }
    const auto& member = tree.node(callee);
    if (static_cast<ast::MemberAccessKind>(
            member.payload.words[ast::kMemberExpressionAccessWord]) !=
        ast::MemberAccessKind::Qualified) {
      return;
    }
    const ast::NodeId object(member.payload.words[ast::kMemberExpressionObjectWord]);
    if (!tree.contains(object) || tree.node(object).kind != ast::SyntaxKind::IdentExpr) { return; }
    const auto baseDefinition = resolvedDefinition(boundModule.bindings(), object);
    if (baseDefinition == zc::none) return;
    const auto enumDefId = ZC_ASSERT_NONNULL(baseDefinition);
    bool baseIsEnum = false;
    for (const auto& definition : boundModule.definitions().definitions()) {
      if (definition.definition == enumDefId) {
        baseIsEnum = definition.record.kind() == identity::DefinitionKind::Enum;
        break;
      }
    }
    if (!baseIsEnum) return;
    const auto propertyName =
        tree.ident(ast::IdentId(member.payload.words[ast::kMemberExpressionPropertyWord]));
    for (const auto& definition : boundModule.definitions().definitions()) {
      if (definition.record.kind() != identity::DefinitionKind::EnumVariant ||
          definition.record.name() != propertyName) {
        continue;
      }
      if (tree.contains(definition.node) &&
          tree.node(definition.node).kind == ast::SyntaxKind::TupleVariant) {
        constructionCallees.add(callee);
      }
      break;
    }
  });
  ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId node, const ast::Node& syntax) {
    if (failure != zc::none) return;
    const uint32_t ordinal = schemaPreorder++;
    if (syntax.kind == ast::SyntaxKind::Unknown) {
      failure = rejectInvariant(signature::CheckerInvariantKind::InvalidFact, boundModule.module(),
                                ordinal, zc::none, node);
      return;
    }
    const bool bodyNode = (isExpression(syntax.kind) || isPattern(syntax.kind) ||
                           syntax.kind == ast::SyntaxKind::MatchStmt ||
                           syntax.kind == ast::SyntaxKind::SuspendStatement) &&
                          !(isPattern(syntax.kind) && isMatchArmPattern(tree, node));
    if (!bodyNode) return;
    // Skip enum variant discriminant expressions; they belong to the variant
    // signature, not the body.
    for (const auto discriminant : enumDiscriminantNodes) {
      if (discriminant == node) return;
    }
    // Skip the base identifier of a qualified member access; it is a
    // type-namespace reference, not a value production site, and needs no
    // NodeType fact or production site.
    if (syntax.kind == ast::SyntaxKind::IdentExpr) {
      for (const auto base : qualifiedMemberBases) {
        if (base == node) return;
      }
    }
    // Skip the constructor-callee MemberExpression of an enum tuple-variant
    // construction call; it is a type-namespace reference, not a value
    // production site, and needs no NodeType/Literal/Member/Place fact.
    for (const auto callee : constructionCallees) {
      if (callee == node) return;
    }
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
      // A bare identifier initializer whose annotation is an erasable dyn
      // existential drives a concrete-to-dyn coercion, except when the source
      // already has that exact existential type: a dyn-to-dyn identity copy
      // needs no DynErase. The identity test statically mirrors the producer's
      // identifier type resolution so this requirement and the produced
      // coercion fact always agree (a mismatch either way fails the count gate).
      if (syntax.kind == ast::SyntaxKind::IdentExpr) {
        auto declaredType = ownerLocalInitializerDeclaredType(boundModule, buildInput.identities,
                                                              buildInput.semanticTypes, node);
        ZC_IF_SOME(declared, declaredType) {
          if (erasableExistentialType(buildInput.semanticTypes, declared) != zc::none &&
              !isIdentityExistentialInitializer(boundModule, buildInput.identities,
                                                buildInput.semanticTypes, node, declared, 0)) {
            addNodeRequirement(nodeRequirements, CheckedFactGroup::Coercion, node, key);
          }
        }
      }
      // A return expression whose enclosing function returns an erasable dyn
      // existential drives a concrete-to-dyn coercion, except when the return
      // expression already carries that exact existential type: a dyn-to-dyn
      // identity return needs no DynErase. The identity test statically
      // mirrors the producer's return type resolution so this requirement and
      // the produced coercion fact always agree.
      for (const auto& info : erasableReturnValues) {
        if (info.node != node) continue;
        if (!isIdentityExistentialReturn(boundModule, buildInput.identities,
                                         buildInput.semanticTypes, node, info.expectedType, 0)) {
          addNodeRequirement(nodeRequirements, CheckedFactGroup::Coercion, node, key);
        }
        break;
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
        // An enum tuple-variant construction (`Result::Ok(41)`) produces only
        // a NodeType fact; the constructor call has no Call fact because no
        // callable is invoked.
        bool isConstruction = false;
        if (syntax.kind == ast::SyntaxKind::CallExpression) {
          const ast::NodeId callCallee(syntax.payload.words[ast::kCallExpressionCalleeWord]);
          for (const auto callee : constructionCallees) {
            if (callee == callCallee) {
              isConstruction = true;
              break;
            }
          }
        }
        if (!isConstruction) {
          addNodeRequirement(nodeRequirements, CheckedFactGroup::Call, node, key);
        }
      } else if (syntax.kind == ast::SyntaxKind::MemberExpression) {
        // A qualified enum variant access (`Color::Red`) lowers to an integer
        // constant; it needs a Literal requirement, not Member/Place.
        const auto accessKind = static_cast<ast::MemberAccessKind>(
            syntax.payload.words[ast::kMemberExpressionAccessWord]);
        bool isEnumVariant = false;
        if (accessKind == ast::MemberAccessKind::Qualified) {
          const ast::NodeId object(syntax.payload.words[ast::kMemberExpressionObjectWord]);
          if (tree.contains(object) && tree.node(object).kind == ast::SyntaxKind::IdentExpr) {
            const auto baseDefinition = resolvedDefinition(boundModule.bindings(), object);
            if (baseDefinition != zc::none) {
              for (const auto& definition : boundModule.definitions().definitions()) {
                if (definition.definition == ZC_ASSERT_NONNULL(baseDefinition) &&
                    definition.record.kind() == identity::DefinitionKind::Enum) {
                  isEnumVariant = true;
                  break;
                }
              }
            }
          }
        }
        if (isEnumVariant) {
          addNodeRequirement(nodeRequirements, CheckedFactGroup::Literal, node, key);
        } else {
          addNodeRequirement(nodeRequirements, CheckedFactGroup::Member, node, key);
          if (!isMethodCallCallee(tree, node)) {
            addNodeRequirement(nodeRequirements, CheckedFactGroup::Place, node, key);
          }
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
        case ast::SyntaxKind::CallExpression: {
          const ast::NodeId callCallee(syntax.payload.words[ast::kCallExpressionCalleeWord]);
          bool isConstruction = false;
          for (const auto callee : constructionCallees) {
            if (callee == callCallee) {
              isConstruction = true;
              break;
            }
          }
          if (isConstruction) {
            production = BodyProductionKind::EnumVariantConstruction;
          } else {
            production = tree.node(callCallee).kind == ast::SyntaxKind::MemberExpression
                             ? BodyProductionKind::ConcreteMethodCall
                             : BodyProductionKind::DirectCall;
          }
          break;
        }
        case ast::SyntaxKind::IdentExpr:
          production = BodyProductionKind::IdentifierReference;
          break;
        case ast::SyntaxKind::AssignmentExpr:
          if (isSimpleLocalWrite(boundModule, node)) {
            production = BodyProductionKind::LocalWrite;
          } else if (isSimpleOwnerLocalFieldWrite(boundModule, node)) {
            production = BodyProductionKind::OwnerLocalFieldWrite;
          } else if (isSimpleReceiverFieldWrite(boundModule, node)) {
            production = BodyProductionKind::ReceiverFieldWrite;
          }
          break;
        case ast::SyntaxKind::MemberExpression: {
          // A qualified access whose base resolves to an enum definition is an
          // enum variant value. The full variant resolution and type production
          // happen in the check loop via `enumVariantValueShape`.
          const auto& memberSyntax = tree.node(node);
          const auto accessKind = static_cast<ast::MemberAccessKind>(
              memberSyntax.payload.words[ast::kMemberExpressionAccessWord]);
          if (accessKind == ast::MemberAccessKind::Qualified) {
            const ast::NodeId object(memberSyntax.payload.words[ast::kMemberExpressionObjectWord]);
            if (tree.contains(object) && tree.node(object).kind == ast::SyntaxKind::IdentExpr) {
              const auto baseDefinition = resolvedDefinition(boundModule.bindings(), object);
              if (baseDefinition != zc::none) {
                for (const auto& definition : boundModule.definitions().definitions()) {
                  if (definition.definition == ZC_ASSERT_NONNULL(baseDefinition) &&
                      definition.record.kind() == identity::DefinitionKind::Enum) {
                    production = BodyProductionKind::EnumVariantValue;
                    break;
                  }
                }
              }
            }
          }
          if (production == BodyProductionKind::Unsupported) {
            production = isMethodCallCallee(tree, node)
                             ? BodyProductionKind::OwnerLocalMethodReference
                             : BodyProductionKind::OwnerLocalFieldReference;
          }
          break;
        }
        case ast::SyntaxKind::IndexExpression:
          production = BodyProductionKind::ReadIndex;
          break;
        case ast::SyntaxKind::StructLiteralExpr:
          production = BodyProductionKind::StructLiteral;
          break;
        case ast::SyntaxKind::BinaryExpr: {
          // Admit the six relational comparisons (result bool, any position),
          // the twelve arithmetic/bitwise operators (result operand type, only
          // outside a condition), and the two logical short-circuit operators
          // (result bool, any position) where each operand is a scalar value
          // reference (a parameter or an owner local) or a scalar literal and
          // at least one operand is a reference; every other binary shape stays
          // unsupported so its existing rejection stands. A literal-vs-literal
          // operation has no place to lower and is left unsupported.
          const auto operation =
              static_cast<ast::BinaryOperatorKind>(syntax.payload.words[ast::kBinaryExprOpWord]);
          const bool isComparison = scalarComparisonOperation(operation) != zc::none;
          const auto arithmeticOp = scalarArithmeticOperation(operation);
          const bool isLogical = arithmeticOp == PrimitiveOperation::LogicalAnd ||
                                 arithmeticOp == PrimitiveOperation::LogicalOr;
          const bool isArithmetic = !isComparison && arithmeticOp != zc::none && !isLogical;
          if (!isComparison && arithmeticOp == zc::none) break;
          // An arithmetic result is not bool, so it cannot drive an `if` /
          // `while` condition; it stays unsupported there and the existing
          // rejection stands. Logical operators produce bool and are admitted
          // in any position.
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
            // An operand may be a `this.<field>` projection read inside an
            // inherent method or an owner-local field read (`cell.value`).
            // This is structural; the shape validator resolves the enclosing
            // method, field type, and receiver mutability.
            const bool leftIsReceiverField =
                isReceiverFieldRead(tree, left) || isOwnerLocalFieldRead(tree, left);
            const bool rightIsReceiverField =
                isReceiverFieldRead(tree, right) || isOwnerLocalFieldRead(tree, right);
            const bool leftOk = leftIsReference || isScalarLiteral(tree.node(left).kind) ||
                                leftIsNested || leftIsReceiverField;
            const bool rightOk = rightIsReference || isScalarLiteral(tree.node(right).kind) ||
                                 rightIsNested || rightIsReceiverField;
            if (leftOk && rightOk &&
                (leftIsReference || rightIsReference || leftIsNested || rightIsNested ||
                 leftIsReceiverField || rightIsReceiverField)) {
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
          } else if (operation == ast::UnaryOperatorKind::Plus ||
                     operation == ast::UnaryOperatorKind::Minus ||
                     operation == ast::UnaryOperatorKind::LogicalNot ||
                     operation == ast::UnaryOperatorKind::BitNot) {
            // Admit the four primitive unary operators (`+` `-` `~` `!`) where
            // the operand is a scalar value reference (a parameter or an owner
            // local) or a scalar literal and at least the operand is a
            // reference; every other unary shape stays unsupported so its
            // existing rejection stands. A literal-only unary has no place to
            // lower and is left unsupported.
            const ast::NodeId operand(syntax.payload.words[ast::kUnaryExpressionOperandWord]);
            if (tree.contains(operand)) {
              const bool operandIsReference =
                  tree.node(operand).kind == ast::SyntaxKind::IdentExpr &&
                  (resolvedCallableParameter(boundModule.bindings(), operand) != zc::none ||
                   resolvedOwnerLocal(boundModule.bindings(), operand) != zc::none);
              const bool operandIsLiteral = isScalarLiteral(tree.node(operand).kind);
              if (operandIsReference || operandIsLiteral) {
                production = BodyProductionKind::PrimitiveUnaryOperation;
              }
            }
          }
          break;
        }
        case ast::SyntaxKind::PostfixExpression: {
          const auto operation = static_cast<ast::PostfixOperatorKind>(
              syntax.payload.words[ast::kPostfixExpressionOpWord]);
          if (operation == ast::PostfixOperatorKind::ErrorPropagate ||
              operation == ast::PostfixOperatorKind::ErrorUnwrap) {
            production = BodyProductionKind::ErrorOperator;
          } else if (operation == ast::PostfixOperatorKind::Increment ||
                     operation == ast::PostfixOperatorKind::Decrement) {
            // Admit postfix increment/decrement on a mutable owner local. The
            // HIR builder desugars the write to `x = x + 1` (or `x = x - 1`),
            // reusing the binary-write path. The operand type is verified as an
            // integer primitive at consumption time.
            const ast::NodeId operand(syntax.payload.words[ast::kPostfixExpressionOperandWord]);
            if (tree.contains(operand) && tree.node(operand).kind == ast::SyntaxKind::IdentExpr &&
                isMutableOwnerLocal(boundModule, operand)) {
              production = BodyProductionKind::PostfixIncrement;
            }
          }
          break;
        }
        case ast::SyntaxKind::UnsafeBlockExpr:
          production = BodyProductionKind::UnsafeBlock;
          break;
        case ast::SyntaxKind::CastExpression: {
          // Admit an integer `as` cast whose inner expression is a scalar
          // literal and whose target type resolves to a closed integer
          // primitive. Every other cast shape stays unsupported so its
          // existing rejection stands.
          const ast::NodeId castExpr(syntax.payload.words[ast::kCastExpressionExprWord]);
          const ast::NodeId castTy(syntax.payload.words[ast::kCastExpressionTyWord]);
          if (tree.contains(castExpr) && isScalarLiteral(tree.node(castExpr).kind) &&
              tree.contains(castTy)) {
            auto targetType = signature::resolveClosedSourceType(boundModule, buildInput.identities,
                                                                 buildInput.semanticTypes, castTy);
            if (targetType != zc::none &&
                integerPrimitiveKind(buildInput.semanticTypes, ZC_ASSERT_NONNULL(targetType)) !=
                    zc::none) {
              production = BodyProductionKind::IntegerCast;
            }
          }
          break;
        }
        case ast::SyntaxKind::ConditionalExpr: {
          // Admit a ternary conditional expression whose condition is a scalar
          // bool literal or reference (parameter or owner local) and whose
          // branches are scalar literals of the same type. Every other ternary
          // shape stays unsupported so its existing rejection stands.
          const ast::NodeId condNode(syntax.payload.words[ast::kConditionalExprCondWord]);
          const ast::NodeId thenNode(syntax.payload.words[ast::kConditionalExprThenExprWord]);
          const ast::NodeId elseNode(syntax.payload.words[ast::kConditionalExprElseExprWord]);
          if (!tree.contains(condNode) || !tree.contains(thenNode) || !tree.contains(elseNode)) {
            break;
          }
          const auto& condSyntax = tree.node(condNode);
          const auto& thenSyntax = tree.node(thenNode);
          const auto& elseSyntax = tree.node(elseNode);
          if (condSyntax.kind != ast::SyntaxKind::IdentExpr &&
              condSyntax.kind != ast::SyntaxKind::BoolLiteral) {
            break;
          }
          if (!isScalarLiteral(thenSyntax.kind) || !isScalarLiteral(elseSyntax.kind)) { break; }
          production = BodyProductionKind::ConditionalExpression;
          break;
        }
        case ast::SyntaxKind::MatchExpr: {
          // Admit a two-arm boolean match expression whose scrutinee is a bool
          // literal or reference and whose arms each carry a bool literal
          // pattern (true/false) and a scalar-literal body. The HIR shape layer
          // normalizes this to the ternary conditional-select path. Every
          // other match-expr shape stays unsupported so its existing rejection
          // stands.
          const ast::NodeId scrutinee(syntax.payload.words[ast::kMatchExprScrutineeWord]);
          const ast::NodeList arms{syntax.payload.words[ast::kMatchExprArmsFirstWord],
                                   syntax.payload.words[ast::kMatchExprArmsSizeWord]};
          if (!tree.contains(scrutinee) || !tree.contains(arms) || arms.size != 2) { break; }
          const auto& scrutineeSyntax = tree.node(scrutinee);
          if (scrutineeSyntax.kind != ast::SyntaxKind::IdentExpr &&
              scrutineeSyntax.kind != ast::SyntaxKind::BoolLiteral) {
            break;
          }
          bool sawTrue = false;
          bool sawFalse = false;
          bool armsAdmitted = true;
          for (size_t index = 0; index < arms.size; ++index) {
            const ast::NodeId armId = tree.list(arms)[index];
            if (!tree.contains(armId) || tree.node(armId).kind != ast::SyntaxKind::MatchArmExpr) {
              armsAdmitted = false;
              break;
            }
            const auto& arm = tree.node(armId);
            const ast::NodeId guard(arm.payload.words[ast::kMatchArmExprGuardWord]);
            if (tree.contains(guard)) {
              armsAdmitted = false;
              break;
            }
            const ast::NodeId pattern(arm.payload.words[ast::kMatchArmExprPatternWord]);
            if (!tree.contains(pattern) ||
                tree.node(pattern).kind != ast::SyntaxKind::LiteralPattern) {
              armsAdmitted = false;
              break;
            }
            const ast::NodeId literal(
                tree.node(pattern).payload.words[ast::kLiteralPatternLiteralWord]);
            if (!tree.contains(literal) ||
                tree.node(literal).kind != ast::SyntaxKind::BoolLiteral) {
              armsAdmitted = false;
              break;
            }
            if (tree.node(literal).payload.words[ast::kBoolLiteralValueWord] != 0) {
              sawTrue = true;
            } else {
              sawFalse = true;
            }
            const ast::NodeId body(arm.payload.words[ast::kMatchArmExprBodyWord]);
            if (!tree.contains(body) || !isScalarLiteral(tree.node(body).kind)) {
              armsAdmitted = false;
              break;
            }
          }
          if (armsAdmitted && sawTrue && sawFalse) {
            production = BodyProductionKind::MatchExpression;
          }
          break;
        }
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

  // Capability gate for concrete methods supplied by a standalone `impl`
  // block. Their signatures are published upstream so coherence observes the
  // complete impl, but neither receiverless nor `this`-bearing impl method
  // bodies lower yet. Reject the whole definition once as ZOM4099 before any
  // production site is analyzed, so an unsupported `this` receiver/field can
  // never reach a per-expression capability code or an invariant. Inherent
  // struct/class methods are owned by their nominal and stay unaffected.
  for (const auto& definition : input.boundModule.definitions().definitions()) {
    if (!providedByImplWithBody(input.boundModule, definition)) { continue; }
    const auto& tree = input.boundModule.tree();
    auto span = parsedModule.spanFor(tree.node(definition.node).range);
    auto ordinal = definitionPreorder(input.boundModule, definition.definition);
    ZC_IF_SOME(ownerOrdinal, ordinal) {
      ZC_IF_SOME(sourceSpan, span) {
        const auto& syntax = tree.node(definition.node);
        BodyProductionSite site{definition.node,
                                checked::CheckedNodeKey{static_cast<uint32_t>(syntax.kind),
                                                        ownerOrdinal, sourceSpan.clone()},
                                CheckedFactGroup::NodeType, BodyProductionKind::Unsupported};
        return attachRecoveryLedger(rejectUnsupportedFunctionBodyConstruct(site, ownerOrdinal),
                                    input, factStoreBrands);
      }
    }
    return rejectInvariant(signature::CheckerInvariantKind::InvalidFact, module, 0,
                           zc::Maybe<identity::DefId>(definition.definition));
  }

  zc::Vector<checked::NodeTypeMap::Entry> nodeTypes;
  zc::Vector<checked::DefinitionTypeMap::Entry> definitionTypes;
  zc::Vector<checked::LiteralFactMap::Entry> literals;
  zc::Vector<checked::ConstantFactMap::Entry> constants;
  zc::Vector<checked::PatternFactMap::Entry> patterns;
  zc::Vector<checked::CallFactMap::Entry> calls;
  zc::Vector<checked::CastFactMap::Entry> casts;
  zc::Vector<checked::AggregateFactMap::Entry> aggregates;
  zc::Vector<checked::PlaceFactMap::Entry> places;
  zc::Vector<checked::MemberFactMap::Entry> members;
  zc::Vector<checked::IndexFactMap::Entry> indexes;
  zc::Vector<checked::MarkerObligationFactMap::Entry> markerObligations;
  zc::Vector<checked::ExhaustivenessFactMap::Entry> exhaustiveness;
  // Concrete-to-dyn erasures selected at annotated initializer sites, one per
  // initializer expression node. Consumed into the coercion fact map and the
  // witness store after every production site has been checked.
  struct SelectedDynErase final {
    ast::NodeId node;
    identity::SemanticTypeId concrete;
    identity::SemanticTypeId existential;
    identity::DefId interface;
    identity::ImplId impl;
    zc::Vector<checked::AssociatedTypeBindingData> bindings;
    checked::CoercionSite site;
  };
  zc::Vector<SelectedDynErase> dynErases;
  for (uint8_t stage = 0; stage != 5; ++stage) {
    for (const auto& site : input.requirements.impl->productionSiteValues) {
      bool deferredLocalReference = false;
      if (site.production == BodyProductionKind::IdentifierReference) {
        deferredLocalReference = dependsOnStructuredLocalInitializer(input, site.node) ||
                                 dependsOnDirectCallLocalInitializer(input, site.node) ||
                                 dependsOnTernaryLocalInitializer(input, site.node);
      }
      const bool structured = site.production == BodyProductionKind::StructLiteral;
      const bool projected = site.production == BodyProductionKind::OwnerLocalFieldReference ||
                             site.production == BodyProductionKind::OwnerLocalMethodReference ||
                             deferredLocalReference;
      const bool methodReference = site.production == BodyProductionKind::OwnerLocalMethodReference;
      const bool fieldWrite = site.production == BodyProductionKind::OwnerLocalFieldWrite;
      const bool receiverFieldWrite = site.production == BodyProductionKind::ReceiverFieldWrite;
      const bool directCall = site.production == BodyProductionKind::DirectCall;
      const bool enumConstruction = site.production == BodyProductionKind::EnumVariantConstruction;
      const bool concreteMethodCall = site.production == BodyProductionKind::ConcreteMethodCall;
      const bool errorOperator = site.production == BodyProductionKind::ErrorOperator;
      const bool indexed = site.production == BodyProductionKind::ReadIndex;
      const bool unsafeBlock = site.production == BodyProductionKind::UnsafeBlock;
      const bool primitiveBinary = site.production == BodyProductionKind::PrimitiveBinaryOperation;
      const bool integerCast = site.production == BodyProductionKind::IntegerCast;
      const bool conditionalExpr = site.production == BodyProductionKind::ConditionalExpression;
      const bool matchExpr = site.production == BodyProductionKind::MatchExpression;
      if ((stage == 0 &&
           (structured || projected || fieldWrite || receiverFieldWrite || directCall ||
            enumConstruction || concreteMethodCall || errorOperator || indexed || unsafeBlock ||
            primitiveBinary || integerCast || conditionalExpr || matchExpr)) ||
          (stage == 1 &&
           (((!structured && !directCall && !enumConstruction) || errorOperator || indexed) &&
            !unsafeBlock && !primitiveBinary && !integerCast && !conditionalExpr && !matchExpr)) ||
          (stage == 2 && ((!projected && !indexed) || methodReference)) ||
          (stage == 3 &&
           (!fieldWrite && !receiverFieldWrite && !concreteMethodCall && !methodReference)) ||
          (stage == 4 && !errorOperator)) {
        continue;
      }
      if (site.production == BodyProductionKind::Unsupported) {
        if (input.boundModule.tree().node(site.node).kind == ast::SyntaxKind::IdentifierPattern) {
          continue;
        }
        // An admitted two-arm boolean match produces its exhaustiveness fact in
        // a dedicated pass after the main production loop; skip it here so the
        // Unsupported fallthrough does not reject it.
        if (input.boundModule.tree().node(site.node).kind == ast::SyntaxKind::MatchStmt) {
          continue;
        }
        // A binary operator the checker does not implement yet reaches here as
        // Unsupported, because body admission classifies only the six relational,
        // twelve arithmetic, and two logical short-circuit operators. `===` and
        // `!==` are specified language syntax, so report ZOM4103 rather than a
        // compiler invariant. Every other unsupported node keeps its existing
        // rejection.
        ZC_IF_SOME(operation, unsupportedBinaryOperator(input, site.node)) {
          ZC_IF_SOME(owner, enclosingBodyOwner(input.boundModule, site.node)) {
            ZC_IF_SOME(ownerOrdinal, definitionPreorder(input.boundModule, owner)) {
              return attachRecoveryLedger(
                  rejectUnsupportedBinaryOperator(site, ownerOrdinal, operation), input,
                  factStoreBrands);
            }
          }
        }
        // A `this` expression inside the body of an inherent struct/class
        // method is well-formed receiver syntax the later HIR/MIR/LIR phases do
        // not admit yet. When it is enclosed by a Method definition, route it
        // to a method-call capability diagnostic. A `this` outside every
        // method body keeps its existing rejection (the parser independently
        // rejects `this` in module functions with ZOM2095).
        // A `this` expression that is the receiver of an admitted `this.field`
        // read produces only its receiver node-type fact; the enclosing member
        // expression's place fact covers the projection.
        if (input.boundModule.tree().node(site.node).kind == ast::SyntaxKind::ThisExpr) {
          zc::Maybe<ThisReceiverFieldShape> admittedThisField;
          const auto& tree = input.boundModule.tree();
          for (size_t index = 0; index < tree.nodeCount(); ++index) {
            const ast::NodeId candidate(static_cast<uint32_t>(index));
            if (!tree.contains(candidate) ||
                tree.node(candidate).kind != ast::SyntaxKind::MemberExpression) {
              continue;
            }
            const auto& member = tree.node(candidate);
            if (static_cast<ast::MemberAccessKind>(
                    member.payload.words[ast::kMemberExpressionAccessWord]) !=
                ast::MemberAccessKind::Dot) {
              continue;
            }
            const ast::NodeId object(member.payload.words[ast::kMemberExpressionObjectWord]);
            if (object != site.node) { continue; }
            auto shape = thisReceiverFieldShape(input, candidate);
            if (shape != zc::none) {
              if (admittedThisField != zc::none) {
                admittedThisField = zc::none;
                break;
              }
              admittedThisField = shape;
            }
          }
          if (admittedThisField != zc::none) {
            nodeTypes.add(checked::NodeTypeMap::Entry{
                site.node, ZC_ASSERT_NONNULL(admittedThisField).receiverReferenceType,
                zc::Array<uint8_t>()});
            continue;
          }
          // A `this` expression that is the receiver of an admitted zero-arg
          // self-call (`this.method()`) is typed as the enclosing receiver
          // reference; the call site supplies the typed call fact.
          zc::Maybe<identity::SemanticTypeId> admittedThisCallReceiver;
          for (size_t index = 0; index < tree.nodeCount(); ++index) {
            const ast::NodeId candidate(static_cast<uint32_t>(index));
            if (!tree.contains(candidate) ||
                tree.node(candidate).kind != ast::SyntaxKind::CallExpression) {
              continue;
            }
            const ast::NodeId callCallee(
                tree.node(candidate).payload.words[ast::kCallExpressionCalleeWord]);
            if (!tree.contains(callCallee) ||
                tree.node(callCallee).kind != ast::SyntaxKind::MemberExpression) {
              continue;
            }
            const ast::NodeId callObject(
                tree.node(callCallee).payload.words[ast::kMemberExpressionObjectWord]);
            if (callObject != site.node) { continue; }
            if (thisReceiverMethodCallShape(input, candidate) == zc::none) { continue; }
            if (admittedThisCallReceiver != zc::none) {
              admittedThisCallReceiver = zc::none;
              break;
            }
            admittedThisCallReceiver =
                ZC_ASSERT_NONNULL(enclosingMethodReceiver(input, site.node)).receiverReferenceType;
          }
          if (admittedThisCallReceiver != zc::none) {
            nodeTypes.add(checked::NodeTypeMap::Entry{
                site.node, ZC_ASSERT_NONNULL(admittedThisCallReceiver), zc::Array<uint8_t>()});
            continue;
          }
        }
        ZC_IF_SOME(method, unsupportedInherentMethodThis(input, site.node)) {
          return rejectMethodCallCapability(site, input, factStoreBrands, zc::mv(method));
        }
        // A supported primitive comparison/arithmetic binary whose operands the
        // local slice cannot resolve (a function or module value used as an
        // operand, `g + 1`, or a literal-only operation) is a capability gap in
        // a free function body, not an invariant. Surface admission stops most
        // shapes, but a nested-operand form can still classify here; drain it as
        // ZOM4099.
        if (input.boundModule.tree().node(site.node).kind == ast::SyntaxKind::BinaryExpr &&
            primitiveBinaryLacksReferenceOperand(input, site.node, nodeTypes.asPtr())) {
          ZC_IF_SOME(owner, enclosingBodyOwner(input.boundModule, site.node)) {
            ZC_IF_SOME(ownerOrdinal, definitionPreorder(input.boundModule, owner)) {
              return attachRecoveryLedger(
                  rejectUnsupportedFunctionBodyConstruct(site, ownerOrdinal), input,
                  factStoreBrands);
            }
          }
        }
        // Every other unproduced site inside an inherent method body is a
        // well-formed construct the current HIR/MIR/LIR slice does not admit
        // (binary results over receiver fields, control flow, nested method
        // calls, and so on). The HIR gate drains the method definition as a
        // capability rejection; emit the checker-side method capability code
        // ZOM4125 here so the unsupported construct never falls through to a
        // MissingRequiredFact invariant. Sites outside a method keep the
        // invariant rail.
        ZC_IF_SOME(method, enclosingMethodName(input, site.node)) {
          return rejectMethodCallCapability(site, input, factStoreBrands, zc::mv(method));
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
        // When this reference is itself an owner local's initializer, the local's
        // declared annotation is authoritative and the initializer must match it.
        // `ownerLocalReferenceType` reports a local's type as its initializer's
        // type, so without this check `let x: bool = a` with an i32 `a` binds a
        // bool local to an i32 and the mismatch is never reported. The
        // primitive-binary path already performs the same comparison for
        // `let x: bool = a + b`; this extends it to a bare reference.
        ZC_IF_SOME(produced, producedType) {
          ZC_IF_SOME(declared,
                     ownerLocalInitializerDeclaredType(input.boundModule, input.identities,
                                                       input.semanticTypes, site.node)) {
            if (declared != produced) {
              // A concrete initializer assigned to an annotated dyn
              // existential is a concrete-to-dyn coercion, not a mismatch,
              // when a unique applicable impl exists. The initializer node
              // keeps its concrete type; the local binds to the declared
              // existential at the definition-type decision point below.
              auto existentialRef = erasableExistentialType(input.semanticTypes, declared);
              bool erased = false;
              ZC_IF_SOME(existential, existentialRef) {
                ZC_IF_SOME(impl, selectDynEraseImpl(input, produced, existential)) {
                  zc::Vector<checked::AssociatedTypeBindingData> bindings;
                  for (const auto& binding : existential.associatedBindings) {
                    bindings.add(
                        checked::AssociatedTypeBindingData{binding.associated, binding.type});
                  }
                  dynErases.add(SelectedDynErase{
                      site.node, produced, declared, existential.principal.definition, impl,
                      zc::mv(bindings), checked::CoercionSite::AnnotatedInitializer});
                  erased = true;
                }
              }
              if (!erased) {
                ZC_IF_SOME(owner, enclosingBodyOwner(input.boundModule, site.node)) {
                  ZC_IF_SOME(ownerOrdinal, definitionPreorder(input.boundModule, owner)) {
                    // An erasable existential with no selected impl is either a
                    // genuine missing obligation (no impl exists -> ZOM4018) or
                    // an impl that the non-generic erasure slice cannot lower
                    // yet (generic concrete or generic impl -> ZOM4124). Every
                    // other declared/produced disagreement is ZOM4009.
                    if (existentialRef != zc::none) {
                      const auto& existentialValue = ZC_ASSERT_NONNULL(existentialRef);
                      if (hasImplForErasureOutsideSlice(input, produced, existentialValue)) {
                        return attachRecoveryLedger(
                            rejectGenericErasureUnsupported(site, ownerOrdinal, produced,
                                                            existentialValue.principal.definition),
                            input, factStoreBrands);
                      }
                      return attachRecoveryLedger(
                          rejectTraitNotImplemented(site, ownerOrdinal, produced,
                                                    existentialValue.principal.definition),
                          input, factStoreBrands);
                    }
                    return attachRecoveryLedger(
                        rejectTypeMismatch(site, ownerOrdinal, declared, produced), input,
                        factStoreBrands);
                  }
                }
              }
            }
          }
        }
        if (producedType == zc::none) {
          // An unannotated binary-result local referenced inside an inherent
          // method cannot be typed at this stage; surface admission does not
          // gate method bodies, so drain the method as ZOM4125 here instead of
          // reaching the invariant rail. The annotated binding form is the
          // admitted method lowering shape.
          ZC_IF_SOME(method, enclosingMethodName(input, site.node)) {
            return rejectMethodCallCapability(site, input, factStoreBrands, zc::mv(method));
          }
          // Every remaining bare reference with no produced type is legal source
          // the body slice cannot lower yet: a module-level constant/static, an
          // imported symbol, or a function used as a first-class value (including
          // a binary operand such as `g + 1`). An unbound name is rejected by the
          // binder before this stage. Drain it as ZOM4099 rather than a
          // missing-fact invariant.
          ZC_IF_SOME(owner, enclosingBodyOwner(input.boundModule, site.node)) {
            ZC_IF_SOME(ownerOrdinal, definitionPreorder(input.boundModule, owner)) {
              return attachRecoveryLedger(
                  rejectUnsupportedFunctionBodyConstruct(site, ownerOrdinal), input,
                  factStoreBrands);
            }
          }
          return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, module,
                                 site.key.schemaPreorder, zc::none, site.node,
                                 site.key.sourceSpan.clone(), factPath(site.primaryGroup));
        }
      } else if (site.production == BodyProductionKind::ConcreteMethodCall) {
        zc::Maybe<ast::NodeId> immutableReceiver;
        auto shape =
            concreteMethodCallShape(input, site.node, nodeTypes.asPtr(), &immutableReceiver);
        if (shape == zc::none) {
          // A zero-argument self-call through the implicit `this` receiver is
          // the same typed call shape with the enclosing receiver reference
          // forwarded directly.
          shape = thisReceiverMethodCallShape(input, site.node);
        }
        if (shape == zc::none) {
          // A mutable-receiver method invoked on an immutable `let` local is a
          // genuine mutability error (ZOM4024), distinct from a method the
          // lowering does not implement yet (ZOM4125).
          if (immutableReceiver != zc::none) {
            ZC_IF_SOME(owner, enclosingBodyOwner(input.boundModule, site.node)) {
              ZC_IF_SOME(ownerOrdinal, definitionPreorder(input.boundModule, owner)) {
                return attachRecoveryLedger(rejectCannotMutateImmutableVariable(
                                                site, ownerOrdinal, input.boundModule.tree(),
                                                ZC_ASSERT_NONNULL(immutableReceiver)),
                                            input, factStoreBrands);
              }
            }
          }
          // Resolve the callee name without the lowering shape's mutability and
          // arity gates so ordinary user errors do not collapse into an
          // invariant: a wrong argument count is ZOM4036, while an unknown
          // method or any other unlowered receiver call drains ZOM4125.
          const auto& fallbackTree = input.boundModule.tree();
          const ast::NodeList fallbackArguments{
              fallbackTree.node(site.node).payload.words[ast::kCallExpressionArgsFirstWord],
              fallbackTree.node(site.node).payload.words[ast::kCallExpressionArgsSizeWord]};
          auto methodName = receiverMethodCallName(input, nodeTypes.asPtr(), site.node);
          ZC_IF_SOME(resolved, methodName) {
            if (resolved.methodResolved && fallbackTree.contains(fallbackArguments) &&
                fallbackArguments.size != resolved.parameterCount) {
              ZC_IF_SOME(owner, enclosingBodyOwner(input.boundModule, site.node)) {
                ZC_IF_SOME(ownerOrdinal, definitionPreorder(input.boundModule, owner)) {
                  return attachRecoveryLedger(
                      rejectCallArgumentCount(site, ownerOrdinal, resolved.parameterCount,
                                              fallbackArguments.size),
                      input, factStoreBrands);
                }
              }
            }
            return rejectMethodCallCapability(site, input, factStoreBrands,
                                              zc::mv(ZC_ASSERT_NONNULL(methodName).name));
          }
          return rejectMethodCallCapability(
              site, input, factStoreBrands,
              unsupportedSharedReceiverInherentMethodCall(input, nodeTypes.asPtr(), site.node));
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
            // A field-projection argument on the receiver local
            // (`cell.echo(cell.value)`) is admitted structurally: its node-type
            // fact is produced at a later production stage, so the field type
            // is resolved from the binding rather than looked up. The two
            // `cell` identifiers are distinct AST nodes, so the match is by
            // owner-local binding, not by NodeId.
            const auto& argumentTree = input.boundModule.tree();
            const bool isReceiverFieldArgument =
                argumentTree.contains(argument) &&
                argumentTree.node(argument).kind == ast::SyntaxKind::MemberExpression &&
                resolvedOwnerLocal(
                    input.boundModule.bindings(),
                    ast::NodeId(argumentTree.node(argument)
                                    .payload.words[ast::kMemberExpressionObjectWord])) ==
                    resolvedOwnerLocal(input.boundModule.bindings(), value.receiverNode);
            if (isReceiverFieldArgument) {
              auto fieldShape = ownerLocalFieldShape(input, argument, nodeTypes.asPtr());
              if (fieldShape == zc::none) {
                return rejectInvariant(signature::CheckerInvariantKind::InvalidFact, module,
                                       site.key.schemaPreorder, zc::none, site.node,
                                       site.key.sourceSpan.clone(), factPath(site.primaryGroup));
              }
              if (ZC_ASSERT_NONNULL(fieldShape).fieldType != value.parameters[index]) {
                ZC_IF_SOME(owner, enclosingBodyOwner(input.boundModule, site.node)) {
                  ZC_IF_SOME(ownerOrdinal, definitionPreorder(input.boundModule, owner)) {
                    return attachRecoveryLedger(
                        rejectTypeMismatch(site, ownerOrdinal, value.parameters[index],
                                           ZC_ASSERT_NONNULL(fieldShape).fieldType),
                        input, factStoreBrands);
                  }
                }
                return rejectInvariant(signature::CheckerInvariantKind::InvalidFact, module,
                                       site.key.schemaPreorder, zc::none, site.node,
                                       site.key.sourceSpan.clone(), factPath(site.primaryGroup));
              }
              zc::Maybe<checked::CoercionAdjustment> noAdjustment;
              checkedArguments.add(
                  checked::CheckedArgumentFact{argument, ZC_ASSERT_NONNULL(fieldShape).fieldType,
                                               value.parameters[index], zc::mv(noAdjustment)});
              continue;
            }
            // A primitive binary argument (`cell.compare(cell.value > 0)`) is
            // admitted structurally: its node-type fact (produced at the
            // primitive-binary production stage) gives the result type, which
            // must match the callee parameter type. The binary operation and
            // operand support are checked at the binary's own production site.
            if (input.boundModule.tree().contains(argument) &&
                input.boundModule.tree().node(argument).kind == ast::SyntaxKind::BinaryExpr) {
              if (argumentType == zc::none) {
                return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, module,
                                       site.key.schemaPreorder, zc::none, site.node,
                                       site.key.sourceSpan.clone(), factPath(site.primaryGroup));
              }
              if (ZC_ASSERT_NONNULL(argumentType).value != value.parameters[index]) {
                ZC_IF_SOME(owner, enclosingBodyOwner(input.boundModule, site.node)) {
                  ZC_IF_SOME(ownerOrdinal, definitionPreorder(input.boundModule, owner)) {
                    return attachRecoveryLedger(
                        rejectTypeMismatch(site, ownerOrdinal, value.parameters[index],
                                           ZC_ASSERT_NONNULL(argumentType).value),
                        input, factStoreBrands);
                  }
                }
                return rejectInvariant(signature::CheckerInvariantKind::InvalidFact, module,
                                       site.key.schemaPreorder, zc::none, site.node,
                                       site.key.sourceSpan.clone(), factPath(site.primaryGroup));
              }
              zc::Maybe<checked::CoercionAdjustment> noAdjustment;
              checkedArguments.add(
                  checked::CheckedArgumentFact{argument, ZC_ASSERT_NONNULL(argumentType).value,
                                               value.parameters[index], zc::mv(noAdjustment)});
              continue;
            }
            if (!input.boundModule.tree().contains(argument) ||
                !isScalarLiteral(input.boundModule.tree().node(argument).kind) ||
                argumentType == zc::none || literal == zc::none) {
              // A non-literal (for example an identifier) argument names a
              // well-typed call the receiver-call lowering does not admit yet;
              // report the method capability code rather than an invariant.
              if (input.boundModule.tree().contains(argument) &&
                  !isScalarLiteral(input.boundModule.tree().node(argument).kind)) {
                auto calleeName = receiverMethodCallName(input, nodeTypes.asPtr(), site.node);
                if (calleeName != zc::none) {
                  return rejectMethodCallCapability(site, input, factStoreBrands,
                                                    zc::mv(ZC_ASSERT_NONNULL(calleeName).name));
                }
              }
              return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, module,
                                     site.key.schemaPreorder, zc::none, site.node,
                                     site.key.sourceSpan.clone(), factPath(site.primaryGroup));
            }
            ZC_IF_SOME(type, argumentType) {
              ZC_IF_SOME(literalFact, literal) {
                if (type.value != value.parameters[index]) {
                  ZC_IF_SOME(owner, enclosingBodyOwner(input.boundModule, site.node)) {
                    ZC_IF_SOME(ownerOrdinal, definitionPreorder(input.boundModule, owner)) {
                      return attachRecoveryLedger(
                          rejectTypeMismatch(site, ownerOrdinal, value.parameters[index],
                                             type.value),
                          input, factStoreBrands);
                    }
                  }
                  return rejectInvariant(signature::CheckerInvariantKind::InvalidFact, module,
                                         site.key.schemaPreorder, zc::none, site.node,
                                         site.key.sourceSpan.clone(), factPath(site.primaryGroup));
                }
                if (literalFact.value.node != argument || literalFact.value.type != type.value) {
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
          zc::Maybe<signature::ReceiverMode> receiverMode = value.receiverMode;
          const bool receiverIsThis =
              input.boundModule.tree().contains(value.receiverNode) &&
              input.boundModule.tree().node(value.receiverNode).kind == ast::SyntaxKind::ThisExpr;
          zc::Vector<checked::ReceiverAdjustmentStep> adjustmentSteps;
          if (receiverIsThis) {
            // `this` is already a shared receiver parameter; the self-call
            // forwards it directly rather than borrowing an owner local.
            adjustmentSteps.add(checked::ReceiverAdjustmentStep::ReborrowShared);
          } else {
            adjustmentSteps.add(value.receiverMode == signature::ReceiverMode::Mutable
                                    ? checked::ReceiverAdjustmentStep::BorrowMutable
                                    : checked::ReceiverAdjustmentStep::BorrowShared);
          }
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
          // A resolved callee supplied with the wrong number of arguments is a
          // user error (ZOM4036); every other unresolved direct call keeps its
          // invariant rail.
          const auto& tree = input.boundModule.tree();
          if (tree.contains(site.node) &&
              tree.node(site.node).kind == ast::SyntaxKind::CallExpression) {
            const auto& failedCall = tree.node(site.node);
            const ast::NodeId callee(failedCall.payload.words[ast::kCallExpressionCalleeWord]);
            const ast::NodeList failedTypeArguments{
                failedCall.payload.words[ast::kCallExpressionTypeArgsFirstWord],
                failedCall.payload.words[ast::kCallExpressionTypeArgsSizeWord]};
            const ast::NodeList failedArguments{
                failedCall.payload.words[ast::kCallExpressionArgsFirstWord],
                failedCall.payload.words[ast::kCallExpressionArgsSizeWord]};
            if (tree.contains(callee) && tree.node(callee).kind == ast::SyntaxKind::IdentExpr &&
                tree.contains(failedTypeArguments) && failedTypeArguments.empty() &&
                tree.contains(failedArguments)) {
              auto resolved = directCallableShape(input, callee);
              ZC_IF_SOME(callable, resolved) {
                if (failedArguments.size != callable.parameters.size()) {
                  ZC_IF_SOME(owner, enclosingBodyOwner(input.boundModule, site.node)) {
                    ZC_IF_SOME(ownerOrdinal, definitionPreorder(input.boundModule, owner)) {
                      return attachRecoveryLedger(
                          rejectCallArgumentCount(site, ownerOrdinal, callable.parameters.size(),
                                                  failedArguments.size),
                          input, factStoreBrands);
                    }
                  }
                }
              }
            }
          }
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
            const bool isIdentifierArgument =
                input.boundModule.tree().contains(argument) &&
                input.boundModule.tree().node(argument).kind == ast::SyntaxKind::IdentExpr;
            const bool isLocalOrGlobalArgument =
                isIdentifierArgument && !isLiteralArgument && !isParameterArgument;
            // A reference to an aggregate- or scalar-initialized owner local is
            // scheduled after this direct-call site (a deferred local reference
            // at stage 2), so its node-type fact does not exist yet. Resolve its
            // type from the binding (the declared annotation or the already-
            // typed initializer) for the by-value aggregate and scalar-local
            // argument carriers.
            zc::Maybe<identity::SemanticTypeId> deferredLocalType;
            if (isLocalOrGlobalArgument && argumentType == zc::none) {
              deferredLocalType = ownerLocalReferenceType(input, argument, nodeTypes.asPtr());
              if (deferredLocalType != zc::none &&
                  (isByValueStructLocalArgument(input, argument,
                                                ZC_ASSERT_NONNULL(deferredLocalType)) ||
                   isScalarOwnerLocalArgument(input, argument,
                                              ZC_ASSERT_NONNULL(deferredLocalType)))) {
                argumentType = zc::Maybe<const checked::NodeTypeMap::Entry&>{};
              } else {
                deferredLocalType = zc::none;
              }
            }
            const bool hasArgumentType = argumentType != zc::none || deferredLocalType != zc::none;
            const auto argumentTypeId =
                argumentType != zc::none
                    ? ZC_ASSERT_NONNULL(argumentType).value
                    : (deferredLocalType != zc::none ? ZC_ASSERT_NONNULL(deferredLocalType)
                                                     : identity::SemanticTypeId{});
            if (isLocalOrGlobalArgument && hasArgumentType &&
                argumentTypeId == value.parameters[index] &&
                !isByValueStructLocalArgument(input, argument, argumentTypeId) &&
                !isScalarOwnerLocalArgument(input, argument, argumentTypeId)) {
              // A typed owner-local or module/imported identifier argument is
              // valid source the direct-call lowering slice does not admit yet
              // (only parameter arguments lower). Its type already matches the
              // parameter, so drain the unlowered call shape (ZOM4125) instead
              // of a missing-fact invariant. A type-disagreeing identifier
              // argument is left for the ordinary type-error block below.
              const ast::NodeId calleeNode(input.boundModule.tree()
                                               .node(site.node)
                                               .payload.words[ast::kCallExpressionCalleeWord]);
              auto capability =
                  rejectUnsupportedDirectCallArgument(site, input, factStoreBrands, calleeNode);
              if (capability != zc::none) { return zc::mv(ZC_ASSERT_NONNULL(capability)); }
            }
            if ((!isLiteralArgument && !isParameterArgument &&
                 !(isLocalOrGlobalArgument && hasArgumentType)) ||
                !hasArgumentType || (isLiteralArgument && literal == zc::none)) {
              return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, module,
                                     site.key.schemaPreorder, zc::none, site.node,
                                     site.key.sourceSpan.clone(), factPath(site.primaryGroup));
            }
            if (deferredLocalType != zc::none) {
              // The by-value aggregate argument's node-type fact is produced by
              // a later stage; its type was resolved from the binding above. A
              // parameter type disagreement is an ordinary type error.
              if (argumentTypeId != value.parameters[index]) {
                ZC_IF_SOME(callOwner, enclosingBodyOwner(input.boundModule, site.node)) {
                  ZC_IF_SOME(callOwnerOrdinal, definitionPreorder(input.boundModule, callOwner)) {
                    return attachRecoveryLedger(
                        rejectTypeMismatch(site, callOwnerOrdinal, value.parameters[index],
                                           argumentTypeId),
                        input, factStoreBrands);
                  }
                }
                return rejectInvariant(signature::CheckerInvariantKind::InvalidFact, module,
                                       site.key.schemaPreorder, zc::none, site.node,
                                       site.key.sourceSpan.clone(), factPath(site.primaryGroup));
              }
              zc::Maybe<checked::CoercionAdjustment> noAdjustment;
              checkedArguments.add(checked::CheckedArgumentFact{
                  argument, argumentTypeId, value.parameters[index], zc::mv(noAdjustment)});
              continue;
            }
            ZC_IF_SOME(type, argumentType) {
              if (type.value != value.parameters[index]) {
                // A concrete argument coerces to an erasable dyn parameter via
                // a concrete-to-dyn DynErase, mirroring the annotated
                // initializer decision. The adjustment is attached after the
                // witness store is frozen; here we only select the plan.
                auto parameterExistential =
                    erasableExistentialType(input.semanticTypes, value.parameters[index]);
                bool argumentErased = false;
                ZC_IF_SOME(existential, parameterExistential) {
                  ZC_IF_SOME(impl, selectDynEraseImpl(input, type.value, existential)) {
                    zc::Vector<checked::AssociatedTypeBindingData> bindings;
                    for (const auto& binding : existential.associatedBindings) {
                      bindings.add(
                          checked::AssociatedTypeBindingData{binding.associated, binding.type});
                    }
                    dynErases.add(SelectedDynErase{argument, type.value, value.parameters[index],
                                                   existential.principal.definition, impl,
                                                   zc::mv(bindings),
                                                   checked::CoercionSite::Argument});
                    argumentErased = true;
                  }
                }
                if (!argumentErased) {
                  const auto argumentSite = productionSite(
                      input.requirements.impl->productionSiteValues.asPtr(), argument);
                  ZC_IF_SOME(owner, enclosingBodyOwner(input.boundModule, argument)) {
                    ZC_IF_SOME(ownerOrdinal, definitionPreorder(input.boundModule, owner)) {
                      if (parameterExistential != zc::none) {
                        const auto& existentialValue = ZC_ASSERT_NONNULL(parameterExistential);
                        if (argumentSite != zc::none &&
                            hasImplForErasureOutsideSlice(input, type.value, existentialValue)) {
                          return attachRecoveryLedger(
                              rejectGenericErasureUnsupported(
                                  ZC_ASSERT_NONNULL(argumentSite), ownerOrdinal, type.value,
                                  existentialValue.principal.definition),
                              input, factStoreBrands);
                        }
                        if (argumentSite != zc::none) {
                          return attachRecoveryLedger(
                              rejectTraitNotImplemented(ZC_ASSERT_NONNULL(argumentSite),
                                                        ownerOrdinal, type.value,
                                                        existentialValue.principal.definition),
                              input, factStoreBrands);
                        }
                      }
                    }
                  }
                  // A concrete argument that does not unify with the declared
                  // parameter is an ordinary type error (ZOM4009), not a
                  // checker invariant.
                  ZC_IF_SOME(callOwner, enclosingBodyOwner(input.boundModule, site.node)) {
                    ZC_IF_SOME(callOwnerOrdinal, definitionPreorder(input.boundModule, callOwner)) {
                      return attachRecoveryLedger(
                          rejectTypeMismatch(site, callOwnerOrdinal, value.parameters[index],
                                             type.value),
                          input, factStoreBrands);
                    }
                  }
                  return rejectInvariant(signature::CheckerInvariantKind::InvalidFact, module,
                                         site.key.schemaPreorder, zc::none, site.node,
                                         site.key.sourceSpan.clone(), factPath(site.primaryGroup));
                }
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
          // The shape validator refuses both forms this slice cannot lower yet
          // and operations the operator is not defined for. Only the second is
          // a user error; report it as ZOM4029 for a comparison or ZOM4028 for
          // an arithmetic operation instead of a compiler invariant. Everything
          // else keeps the existing fail-closed rejection.
          auto invalidOperands = invalidBinaryOperandTypes(input, site.node, nodeTypes.asPtr());
          ZC_IF_SOME(operands, invalidOperands) {
            ZC_IF_SOME(owner, enclosingBodyOwner(input.boundModule, site.node)) {
              ZC_IF_SOME(ownerOrdinal, definitionPreorder(input.boundModule, owner)) {
                return attachRecoveryLedger(
                    rejectInvalidBinaryOperands(site, ownerOrdinal, operands), input,
                    factStoreBrands);
              }
            }
          }
          // A binary whose operands are well-typed but whose result disagrees
          // with the annotation of the local it initializes (`let x: bool = a + b`)
          // is likewise a user error, and the same mismatch as the
          // bare-identifier form, so it reports ZOM4009.
          auto initializerMismatch =
              binaryInitializerTypeMismatch(input, site.node, nodeTypes.asPtr());
          ZC_IF_SOME(mismatch, initializerMismatch) {
            ZC_IF_SOME(owner, enclosingBodyOwner(input.boundModule, site.node)) {
              ZC_IF_SOME(ownerOrdinal, definitionPreorder(input.boundModule, owner)) {
                return attachRecoveryLedger(
                    rejectTypeMismatch(site, ownerOrdinal, mismatch.declaredType,
                                       mismatch.producedType),
                    input, factStoreBrands);
              }
            }
          }
          // An operator the checker does not implement yet is specified language
          // syntax, not a compiler invariant, so it reports ZOM4103.
          ZC_IF_SOME(operation, unsupportedBinaryOperator(input, site.node)) {
            ZC_IF_SOME(owner, enclosingBodyOwner(input.boundModule, site.node)) {
              ZC_IF_SOME(ownerOrdinal, definitionPreorder(input.boundModule, owner)) {
                return attachRecoveryLedger(
                    rejectUnsupportedBinaryOperator(site, ownerOrdinal, operation), input,
                    factStoreBrands);
              }
            }
          }
          // A supported operator with no operand the local-binary slice can
          // resolve (e.g. a module-level function used as a value, `g + 1`) is a
          // capability gap, not an invariant: drain it as ZOM4099.
          if (primitiveBinaryLacksReferenceOperand(input, site.node, nodeTypes.asPtr())) {
            ZC_IF_SOME(owner, enclosingBodyOwner(input.boundModule, site.node)) {
              ZC_IF_SOME(ownerOrdinal, definitionPreorder(input.boundModule, owner)) {
                return attachRecoveryLedger(
                    rejectUnsupportedFunctionBodyConstruct(site, ownerOrdinal), input,
                    factStoreBrands);
              }
            }
          }
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
      } else if (site.production == BodyProductionKind::PrimitiveUnaryOperation) {
        auto shape = primitiveUnaryOperationShape(input, site.node, nodeTypes.asPtr());
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
          zc::Maybe<checked::CoercionAdjustment> noAdjustment;
          arguments.add(checked::CheckedArgumentFact{value.operandNode, value.operandType,
                                                     value.operandType, zc::mv(noAdjustment)});
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
      } else if (site.production == BodyProductionKind::PostfixIncrement) {
        // A postfix increment/decrement on a mutable owner local with an
        // integer primitive type. The HIR builder desugars the write to
        // `x = x + 1` (or `x = x - 1`), reusing the binary-write path. The
        // call fact records the primitive operation for downstream
        // verification; the result type is the operand type.
        const auto& postfix = input.boundModule.tree().node(site.node);
        const auto postfixOperator = static_cast<ast::PostfixOperatorKind>(
            postfix.payload.words[ast::kPostfixExpressionOpWord]);
        const ast::NodeId operand(postfix.payload.words[ast::kPostfixExpressionOperandWord]);
        auto operandType = ownerLocalReferenceType(input, operand, nodeTypes.asPtr());
        if (operandType == zc::none ||
            integerPrimitiveKind(input.semanticTypes, ZC_ASSERT_NONNULL(operandType)) == zc::none) {
          return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, module,
                                 site.key.schemaPreorder, zc::none, site.node,
                                 site.key.sourceSpan.clone(), factPath(site.primaryGroup));
        }
        ZC_IF_SOME(value, operandType) {
          producedType = value;
          const auto primitiveOperation = postfixOperator == ast::PostfixOperatorKind::Increment
                                              ? PrimitiveOperation::PostIncrement
                                              : PrimitiveOperation::PostDecrement;
          zc::Maybe<checked::CheckedArgumentFact> noReceiver;
          zc::Maybe<signature::ReceiverMode> noReceiverMode;
          zc::Maybe<checked::ReceiverAdjustment> noReceiverAdjustment;
          zc::Vector<checked::CheckedArgumentFact> arguments;
          zc::Maybe<checked::CoercionAdjustment> noAdjustment;
          arguments.add(checked::CheckedArgumentFact{operand, value, value, zc::mv(noAdjustment)});
          zc::Maybe<checked::CanonicalSubstitutionId> noSubstitutions;
          zc::Maybe<checked::WitnessArgumentsId> noWitnesses;
          zc::Maybe<identity::SemanticTypeId> noRaises;
          calls.add(checked::CallFactMap::Entry{
              site.node,
              checked::TypedCallFact{
                  site.node,
                  checked::CheckedCallEnvelope{
                      checked::SelectedCallable(checked::PrimitiveCallable{primitiveOperation}),
                      value, zc::mv(noReceiver), zc::mv(noReceiverMode),
                      zc::mv(noReceiverAdjustment), zc::mv(arguments), value, value,
                      zc::mv(noSubstitutions), zc::mv(noWitnesses), zc::mv(noRaises)},
                  site.key.sourceSpan.clone()},
              zc::Array<uint8_t>()});
        }
      } else if (site.production == BodyProductionKind::IntegerCast) {
        // An integer `as` cast over a scalar literal inner expression. The
        // literal adopts the cast target type, so the cast is a widening or
        // identity conversion that lowers to a no-op. The cast fact records
        // the source/target/result types for downstream verification.
        const auto& castNode = input.boundModule.tree().node(site.node);
        const ast::NodeId castExpr(castNode.payload.words[ast::kCastExpressionExprWord]);
        const ast::NodeId castTy(castNode.payload.words[ast::kCastExpressionTyWord]);
        auto targetType = signature::resolveClosedSourceType(input.boundModule, input.identities,
                                                             input.semanticTypes, castTy);
        auto sourceType = factEntry(nodeTypes.asPtr(), castExpr);
        if (targetType == zc::none || sourceType == zc::none) {
          return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, module,
                                 site.key.schemaPreorder, zc::none, site.node,
                                 site.key.sourceSpan.clone(), factPath(site.primaryGroup));
        }
        const auto source = ZC_ASSERT_NONNULL(sourceType).value;
        const auto target = ZC_ASSERT_NONNULL(targetType);
        auto sourceKind = integerPrimitiveKind(input.semanticTypes, source);
        auto targetKind = integerPrimitiveKind(input.semanticTypes, target);
        if (sourceKind == zc::none || targetKind == zc::none) {
          return rejectInvariant(signature::CheckerInvariantKind::InvalidFact, module,
                                 site.key.schemaPreorder, zc::none, site.node,
                                 site.key.sourceSpan.clone(), factPath(site.primaryGroup));
        }
        const auto castKind = ZC_ASSERT_NONNULL(sourceKind) <= ZC_ASSERT_NONNULL(targetKind)
                                  ? checked::CastKind::IntegerWiden
                                  : checked::CastKind::IntegerNarrowChecked;
        producedType = target;
        zc::Maybe<identity::ImplId> noImpl;
        zc::Maybe<checked::WitnessArgumentsId> noWitnesses;
        zc::Vector<identity::DefId> noDynPath;
        casts.add(checked::CastFactMap::Entry{
            site.node,
            checked::CheckedCastFact{site.node, checked::CastMode::Guaranteed, castKind, source,
                                     target, target, zc::mv(noImpl), zc::mv(noWitnesses),
                                     zc::mv(noDynPath), checked::UnsafeRequirement::None,
                                     site.key.sourceSpan.clone()},
            zc::Array<uint8_t>()});
      } else if (site.production == BodyProductionKind::ConditionalExpression) {
        // A ternary conditional expression `cond ? then : else`. The condition
        // is a bool literal or reference; both branches are scalar literals of
        // the same type. The result type is the branch type.
        const auto& condNode = input.boundModule.tree().node(site.node);
        const ast::NodeId condExpr(condNode.payload.words[ast::kConditionalExprCondWord]);
        const ast::NodeId thenExpr(condNode.payload.words[ast::kConditionalExprThenExprWord]);
        const ast::NodeId elseExpr(condNode.payload.words[ast::kConditionalExprElseExprWord]);
        auto condType = factEntry(nodeTypes.asPtr(), condExpr);
        auto thenType = factEntry(nodeTypes.asPtr(), thenExpr);
        auto elseType = factEntry(nodeTypes.asPtr(), elseExpr);
        if (condType == zc::none || thenType == zc::none || elseType == zc::none) {
          return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, module,
                                 site.key.schemaPreorder, zc::none, site.node,
                                 site.key.sourceSpan.clone(), factPath(site.primaryGroup));
        }
        const auto cond = ZC_ASSERT_NONNULL(condType).value;
        const auto then = ZC_ASSERT_NONNULL(thenType).value;
        const auto elseTy = ZC_ASSERT_NONNULL(elseType).value;
        auto condKind = primitiveKindOf(input.semanticTypes, cond);
        if (condKind == zc::none ||
            ZC_ASSERT_NONNULL(condKind) != type::semantic::PrimitiveKind::Bool) {
          return rejectInvariant(signature::CheckerInvariantKind::InvalidFact, module,
                                 site.key.schemaPreorder, zc::none, site.node,
                                 site.key.sourceSpan.clone(), factPath(site.primaryGroup));
        }
        if (then != elseTy) {
          return rejectInvariant(signature::CheckerInvariantKind::InvalidFact, module,
                                 site.key.schemaPreorder, zc::none, site.node,
                                 site.key.sourceSpan.clone(), factPath(site.primaryGroup));
        }
        producedType = then;
      } else if (site.production == BodyProductionKind::MatchExpression) {
        // A two-arm boolean match expression. The scrutinee is bool; both arm
        // bodies are scalar literals of the same type. The result type is the
        // arm body type.
        const auto& matchNode = input.boundModule.tree().node(site.node);
        const ast::NodeId scrutinee(matchNode.payload.words[ast::kMatchExprScrutineeWord]);
        const ast::NodeList arms{matchNode.payload.words[ast::kMatchExprArmsFirstWord],
                                 matchNode.payload.words[ast::kMatchExprArmsSizeWord]};
        auto scrutineeType = factEntry(nodeTypes.asPtr(), scrutinee);
        if (scrutineeType == zc::none) {
          return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, module,
                                 site.key.schemaPreorder, zc::none, site.node,
                                 site.key.sourceSpan.clone(), factPath(site.primaryGroup));
        }
        auto scrutineeKind =
            primitiveKindOf(input.semanticTypes, ZC_ASSERT_NONNULL(scrutineeType).value);
        if (scrutineeKind == zc::none ||
            ZC_ASSERT_NONNULL(scrutineeKind) != type::semantic::PrimitiveKind::Bool) {
          return rejectInvariant(signature::CheckerInvariantKind::InvalidFact, module,
                                 site.key.schemaPreorder, zc::none, site.node,
                                 site.key.sourceSpan.clone(), factPath(site.primaryGroup));
        }
        zc::Maybe<identity::SemanticTypeId> firstArmType;
        for (size_t index = 0; index < arms.size; ++index) {
          const ast::NodeId armId = input.boundModule.tree().list(arms)[index];
          const auto& arm = input.boundModule.tree().node(armId);
          const ast::NodeId body(arm.payload.words[ast::kMatchArmExprBodyWord]);
          auto bodyType = factEntry(nodeTypes.asPtr(), body);
          if (bodyType == zc::none) {
            return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, module,
                                   site.key.schemaPreorder, zc::none, site.node,
                                   site.key.sourceSpan.clone(), factPath(site.primaryGroup));
          }
          if (firstArmType == zc::none) {
            firstArmType = ZC_ASSERT_NONNULL(bodyType).value;
          } else if (ZC_ASSERT_NONNULL(firstArmType) != ZC_ASSERT_NONNULL(bodyType).value) {
            return rejectInvariant(signature::CheckerInvariantKind::InvalidFact, module,
                                   site.key.schemaPreorder, zc::none, site.node,
                                   site.key.sourceSpan.clone(), factPath(site.primaryGroup));
          }
        }
        producedType = ZC_ASSERT_NONNULL(firstArmType);
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
      } else if (site.production == BodyProductionKind::ReceiverFieldWrite) {
        const auto& assignment = input.boundModule.tree().node(site.node);
        const ast::NodeId target(assignment.payload.words[ast::kAssignmentExprLhsWord]);
        auto targetShape = thisReceiverFieldShape(input, target);
        if (targetShape == zc::none) {
          return rejectMethodCallCapability(site, input, factStoreBrands,
                                            unsupportedThisFieldAccess(input, target));
        }
        if (!ZC_ASSERT_NONNULL(targetShape).receiverMutable) {
          // A field write through a shared receiver is the same mutation error
          // as writing an immutable variable, naming the implicit receiver.
          zc::Maybe<uint32_t> ownerOrdinal;
          ZC_IF_SOME(owner, enclosingBodyOwner(input.boundModule, site.node)) {
            ownerOrdinal = definitionPreorder(input.boundModule, owner);
          }
          if (ownerOrdinal == zc::none) {
            return rejectMethodCallCapability(site, input, factStoreBrands,
                                              unsupportedThisFieldAccess(input, target));
          }
          return attachRecoveryLedger(
              rejectCannotMutateSharedReceiver(site, ZC_ASSERT_NONNULL(ownerOrdinal)), input,
              factStoreBrands);
        }
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
        auto thisShape = thisReceiverFieldShape(input, site.node);
        auto shape = thisShape != zc::none
                         ? zc::none
                         : ownerLocalFieldShape(input, site.node, nodeTypes.asPtr());
        auto parameterShape = (thisShape != zc::none || shape != zc::none)
                                  ? zc::none
                                  : parameterFieldShape(input, site.node, nodeTypes.asPtr());
        if (thisShape == zc::none && shape == zc::none && parameterShape == zc::none) {
          auto unsupportedThisField = unsupportedThisFieldAccess(input, site.node);
          if (unsupportedThisField != zc::none) {
            return rejectMethodCallCapability(site, input, factStoreBrands,
                                              zc::mv(unsupportedThisField));
          }
          // A member read on a primitive receiver (such as `str.length`) or an
          // unknown member of a closed nominal receiver is legal source the
          // body slice cannot lower. Inside an inherent method it drains with
          // the method capability code exactly like an unresolvable identifier
          // reference; in a free function body it is ZOM4099.
          if (isUnsupportedResolvedMemberRead(input, nodeTypes.asPtr(), site.node)) {
            ZC_IF_SOME(method, enclosingMethodName(input, site.node)) {
              return rejectMethodCallCapability(site, input, factStoreBrands, zc::mv(method));
            }
            ZC_IF_SOME(owner, enclosingBodyOwner(input.boundModule, site.node)) {
              ZC_IF_SOME(ownerOrdinal, definitionPreorder(input.boundModule, owner)) {
                return attachRecoveryLedger(
                    rejectUnsupportedFunctionBodyConstruct(site, ownerOrdinal), input,
                    factStoreBrands);
              }
            }
          }
          return rejectMethodCallCapability(site, input, factStoreBrands,
                                            unsupportedThisFieldAccess(input, site.node));
        }
        ZC_IF_SOME(value, thisShape) {
          producedType = value.fieldType;
          zc::Maybe<checked::CoercionAdjustment> noAdjustment;
          members.add(checked::MemberFactMap::Entry{
              site.node,
              checked::CheckedMemberFact{site.node, value.receiverType, value.field,
                                         value.fieldType, zc::mv(noAdjustment)},
              zc::Array<uint8_t>()});
          zc::Vector<checked::PlaceProjection> projections;
          projections.add(checked::PlaceProjection(checked::FieldProjection{value.field}));
          // A place reached through a mutable receiver is itself mutable; the
          // receiver field-write arm requires that bit on its target.
          places.add(checked::PlaceFactMap::Entry{
              site.node,
              checked::CheckedPlaceFact{
                  site.node,
                  checked::PlaceRoot(checked::CallableParameterPlaceRoot{value.receiverParameter}),
                  zc::mv(projections), value.fieldType, value.receiverMutable, true},
              zc::Array<uint8_t>()});
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
        ZC_IF_SOME(value, parameterShape) {
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
                  site.node,
                  checked::PlaceRoot(checked::CallableParameterPlaceRoot{value.parameter}),
                  zc::mv(projections), value.fieldType, false, true},
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
        zc::Maybe<ast::NodeId> immutableReceiver;
        auto shape =
            concreteMethodCallShape(input, callNode, nodeTypes.asPtr(), &immutableReceiver);
        if (shape == zc::none) { shape = thisReceiverMethodCallShape(input, callNode); }
        if (shape == zc::none) {
          if (immutableReceiver != zc::none) {
            ZC_IF_SOME(owner, enclosingBodyOwner(input.boundModule, callNode)) {
              ZC_IF_SOME(ownerOrdinal, definitionPreorder(input.boundModule, owner)) {
                return attachRecoveryLedger(rejectCannotMutateImmutableVariable(
                                                site, ownerOrdinal, input.boundModule.tree(),
                                                ZC_ASSERT_NONNULL(immutableReceiver)),
                                            input, factStoreBrands);
              }
            }
          }
          // Resolve the callee name without the lowering shape's gates so a
          // wrong argument count is ZOM4036 and an unknown or unlowered method
          // drains ZOM4125 instead of an invariant.
          const auto& referenceTree = input.boundModule.tree();
          const ast::NodeList referenceArguments{
              referenceTree.node(callNode).payload.words[ast::kCallExpressionArgsFirstWord],
              referenceTree.node(callNode).payload.words[ast::kCallExpressionArgsSizeWord]};
          auto referenceName = receiverMethodCallName(input, nodeTypes.asPtr(), callNode);
          ZC_IF_SOME(resolved, referenceName) {
            if (resolved.methodResolved && referenceTree.contains(referenceArguments) &&
                referenceArguments.size != resolved.parameterCount) {
              ZC_IF_SOME(owner, enclosingBodyOwner(input.boundModule, callNode)) {
                ZC_IF_SOME(ownerOrdinal, definitionPreorder(input.boundModule, owner)) {
                  return attachRecoveryLedger(
                      rejectCallArgumentCount(site, ownerOrdinal, resolved.parameterCount,
                                              referenceArguments.size),
                      input, factStoreBrands);
                }
              }
            }
            return rejectMethodCallCapability(site, input, factStoreBrands,
                                              zc::mv(ZC_ASSERT_NONNULL(referenceName).name));
          }
          return rejectMethodCallCapability(
              site, input, factStoreBrands,
              unsupportedSharedReceiverInherentMethodCall(input, nodeTypes.asPtr(), callNode));
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
        auto built = buildStructLiteral(input, site.node, nodeTypes.asPtr());
        if (built == zc::none) {
          return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, module,
                                 site.key.schemaPreorder, zc::none, site.node,
                                 site.key.sourceSpan.clone(),
                                 factPath(CheckedFactGroup::Aggregate));
        }
        ZC_IF_SOME(result, built) {
          if (result.is<StructLiteralRejection>()) {
            const auto& rejection = result.get<StructLiteralRejection>();
            ZC_IF_SOME(owner, enclosingBodyOwner(input.boundModule, site.node)) {
              ZC_IF_SOME(ownerOrdinal, definitionPreorder(input.boundModule, owner)) {
                switch (rejection.kind) {
                  case StructLiteralRejection::Kind::UnknownField:
                  case StructLiteralRejection::Kind::DuplicateField: {
                    ZC_IF_SOME(name, rejection.name) {
                      zc::Vector<checked::CheckerDisplayArgument> arguments;
                      arguments.add(checked::CheckerDisplayArgument(
                          checked::IdentifierDisplayArg{name.clone()}));
                      const auto diagnostic =
                          rejection.kind == StructLiteralRejection::Kind::UnknownField
                              ? checked::CheckerErrorId::UnknownStructField()
                              : checked::CheckerErrorId::DuplicateStructField();
                      return attachRecoveryLedger(
                          rejectAggregateLiteralField(site, ownerOrdinal, diagnostic,
                                                      zc::mv(arguments)),
                          input, factStoreBrands);
                    }
                    break;
                  }
                  case StructLiteralRejection::Kind::MissingField: {
                    ZC_IF_SOME(field, rejection.field) {
                      zc::Vector<checked::CheckerDisplayArgument> arguments;
                      arguments.add(
                          checked::CheckerDisplayArgument(checked::DefinitionDisplayArg{field}));
                      return attachRecoveryLedger(
                          rejectAggregateLiteralField(site, ownerOrdinal,
                                                      checked::CheckerErrorId::MissingStructField(),
                                                      zc::mv(arguments)),
                          input, factStoreBrands);
                    }
                    break;
                  }
                  case StructLiteralRejection::Kind::UnsupportedIntegerField:
                    return attachRecoveryLedger(
                        rejectUnsupportedFunctionBodyConstruct(site, ownerOrdinal), input,
                        factStoreBrands);
                  case StructLiteralRejection::Kind::TypeMismatch:
                    ZC_IF_SOME(expected, rejection.expectedType) {
                      ZC_IF_SOME(actual, rejection.actualType) {
                        return attachRecoveryLedger(
                            rejectTypeMismatch(site, ownerOrdinal, expected, actual), input,
                            factStoreBrands);
                      }
                    }
                    break;
                }
              }
            }
            return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, module,
                                   site.key.schemaPreorder, zc::none, site.node,
                                   site.key.sourceSpan.clone(),
                                   factPath(CheckedFactGroup::Aggregate));
          }
          auto& value = result.get<StructLiteralShape>();
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
      } else if (site.production == BodyProductionKind::EnumVariantValue) {
        // A qualified enum variant access `Enum::Variant`. The variant must be
        // a unit variant (empty payload); the enum type is the produced type.
        // The variant discriminant is emitted as an integer literal fact so the
        // HIR builder can lower it to a scalar constant.
        auto shape = enumVariantValueShape(input, site.node);
        if (shape == zc::none) {
          return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, module,
                                 site.key.schemaPreorder, zc::none, site.node,
                                 site.key.sourceSpan.clone(), factPath(site.primaryGroup));
        }
        ZC_IF_SOME(value, shape) {
          producedType = value.enumType;
          // Build the big-endian magnitude of the discriminant, stripping
          // leading zero bytes so zero is an empty magnitude.
          zc::Vector<uint8_t> bytes;
          uint64_t remaining = value.discriminant;
          while (remaining != 0) {
            bytes.add(static_cast<uint8_t>(remaining & 0xff));
            remaining >>= 8;
          }
          auto magnitude = zc::heapArray<uint8_t>(bytes.size());
          for (size_t index = 0; index < bytes.size(); ++index) {
            magnitude[index] = bytes[bytes.size() - index - 1];
          }
          literals.add(checked::LiteralFactMap::Entry{
              site.node,
              checked::CheckedLiteralFact{
                  site.node,
                  signature::CanonicalConstValue::integer(signature::CanonicalInteger{
                      signature::IntegerSign::NonNegative, zc::mv(magnitude)}),
                  value.enumType, site.key.sourceSpan.clone()},
              zc::Array<uint8_t>()});
        }
      } else if (site.production == BodyProductionKind::EnumVariantConstruction) {
        // An enum tuple-variant construction `Enum::Variant(args)`. The variant
        // must carry a non-empty payload; each argument's type must match the
        // corresponding payload field. The enum type is the produced type. No
        // Call fact is produced because no callable is invoked.
        auto shape = enumVariantConstructionShape(input, site.node);
        if (shape == zc::none) {
          return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, module,
                                 site.key.schemaPreorder, zc::none, site.node,
                                 site.key.sourceSpan.clone(), factPath(site.primaryGroup));
        }
        ZC_IF_SOME(value, shape) {
          const auto& callSyntax = input.boundModule.tree().node(site.node);
          const ast::NodeList arguments{callSyntax.payload.words[ast::kCallExpressionArgsFirstWord],
                                        callSyntax.payload.words[ast::kCallExpressionArgsSizeWord]};
          const auto argumentNodes = input.boundModule.tree().list(arguments);
          if (argumentNodes.size() != value.payload.size()) {
            auto owner = enclosingBodyOwner(input.boundModule, site.node);
            if (owner == zc::none) {
              return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, module,
                                     site.key.schemaPreorder, zc::none, site.node,
                                     site.key.sourceSpan.clone(), factPath(site.primaryGroup));
            }
            auto ownerPreorder = definitionPreorder(input.boundModule, ZC_ASSERT_NONNULL(owner));
            if (ownerPreorder == zc::none) {
              return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, module,
                                     site.key.schemaPreorder, zc::none, site.node,
                                     site.key.sourceSpan.clone(), factPath(site.primaryGroup));
            }
            return attachRecoveryLedger(
                rejectCallArgumentCount(site, ZC_ASSERT_NONNULL(ownerPreorder),
                                        value.payload.size(), argumentNodes.size()),
                input, factStoreBrands);
          }
          for (size_t index = 0; index < argumentNodes.size(); ++index) {
            auto argumentType = factEntry(nodeTypes.asPtr(), argumentNodes[index]);
            if (argumentType == zc::none) {
              return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, module,
                                     site.key.schemaPreorder, zc::none, argumentNodes[index],
                                     site.key.sourceSpan.clone(),
                                     factPath(CheckedFactGroup::NodeType));
            }
            if (ZC_ASSERT_NONNULL(argumentType).value != value.payload[index]) {
              auto owner = enclosingBodyOwner(input.boundModule, site.node);
              if (owner == zc::none) {
                return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, module,
                                       site.key.schemaPreorder, zc::none, site.node,
                                       site.key.sourceSpan.clone(), factPath(site.primaryGroup));
              }
              auto ownerPreorder = definitionPreorder(input.boundModule, ZC_ASSERT_NONNULL(owner));
              if (ownerPreorder == zc::none) {
                return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, module,
                                       site.key.schemaPreorder, zc::none, site.node,
                                       site.key.sourceSpan.clone(), factPath(site.primaryGroup));
              }
              return attachRecoveryLedger(
                  rejectTypeMismatch(site, ZC_ASSERT_NONNULL(ownerPreorder), value.payload[index],
                                     ZC_ASSERT_NONNULL(argumentType).value),
                  input, factStoreBrands);
            }
          }
          producedType = value.enumType;
        }
      } else {
        auto emitted = scalar_literal::FactEmitter::emit(scalar_literal::FactEmissionInput{
            context, module, input.boundModule.tree(), site.node, site.key,
            input.boundModule.parsedModule().source(), input.identities, input.semanticTypes,
            expectedLiteralType(input, site.node, nodeTypes.asPtr())});
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
              // A concrete return value coerces to an erasable dyn return type
              // via a concrete-to-dyn DynErase, mirroring the annotated
              // initializer decision. The adjustment is attached after the
              // witness store is frozen; here we only select the plan.
              bool returnErased = false;
              if (producedType != zc::none) {
                auto returnExistential = erasableExistentialType(input.semanticTypes, expectedType);
                ZC_IF_SOME(existential, returnExistential) {
                  ZC_IF_SOME(impl, selectDynEraseImpl(input, ZC_ASSERT_NONNULL(producedType),
                                                      existential)) {
                    zc::Vector<checked::AssociatedTypeBindingData> bindings;
                    for (const auto& binding : existential.associatedBindings) {
                      bindings.add(
                          checked::AssociatedTypeBindingData{binding.associated, binding.type});
                    }
                    dynErases.add(SelectedDynErase{site.node, ZC_ASSERT_NONNULL(producedType),
                                                   expectedType, existential.principal.definition,
                                                   impl, zc::mv(bindings),
                                                   checked::CoercionSite::Return});
                    returnErased = true;
                  }
                }
                if (!returnErased && returnExistential != zc::none) {
                  // An erasable existential with no selected impl is either a
                  // genuine missing obligation (no impl exists -> ZOM4018) or
                  // an impl that the non-generic erasure slice cannot lower
                  // yet (generic concrete or generic impl -> ZOM4124).
                  const auto& existentialValue = ZC_ASSERT_NONNULL(returnExistential);
                  if (hasImplForErasureOutsideSlice(input, ZC_ASSERT_NONNULL(producedType),
                                                    existentialValue)) {
                    return attachRecoveryLedger(
                        rejectGenericErasureUnsupported(site, ownerOrdinal,
                                                        ZC_ASSERT_NONNULL(producedType),
                                                        existentialValue.principal.definition),
                        input, factStoreBrands);
                  }
                  return attachRecoveryLedger(
                      rejectTraitNotImplemented(site, ownerOrdinal, ZC_ASSERT_NONNULL(producedType),
                                                existentialValue.principal.definition),
                      input, factStoreBrands);
                }
              }
              if (!returnErased) {
                return attachRecoveryLedger(rejectTypeMismatch(site, ownerOrdinal, expectedType,
                                                               ZC_ASSERT_NONNULL(producedType)),
                                            input, factStoreBrands);
              }
            }
          }
        }
      }
      ZC_IF_SOME(value, producedType) {
        nodeTypes.add(checked::NodeTypeMap::Entry{site.node, value, zc::Array<uint8_t>()});
      }
      if (site.production == BodyProductionKind::EnumVariantValue) {}
    }
  }

  for (const auto& site : input.requirements.impl->productionSiteValues) {
    if (site.production != BodyProductionKind::LocalWrite &&
        site.production != BodyProductionKind::OwnerLocalFieldWrite &&
        site.production != BodyProductionKind::ReceiverFieldWrite) {
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
    ZC_IF_SOME(targetTypeEntry, targetType) {
      ZC_IF_SOME(valueTypeEntry, valueType) {
        if (targetTypeEntry.value != valueTypeEntry.value) {
          // A write value whose type differs from the target place is an
          // ordinary user type error (ZOM4009), covering local writes,
          // owner-local field writes, and receiver field writes.
          ZC_IF_SOME(owner, enclosingBodyOwner(input.boundModule, site.node)) {
            ZC_IF_SOME(ownerOrdinal, definitionPreorder(input.boundModule, owner)) {
              return attachRecoveryLedger(
                  rejectTypeMismatch(site, ownerOrdinal, targetTypeEntry.value,
                                     valueTypeEntry.value),
                  input, factStoreBrands);
            }
          }
          return rejectInvariant(signature::CheckerInvariantKind::InvalidFact, module,
                                 site.key.schemaPreorder, zc::none, site.node,
                                 site.key.sourceSpan.clone(), factPath(CheckedFactGroup::NodeType));
        }
      }
    }
    if (site.production == BodyProductionKind::OwnerLocalFieldWrite ||
        site.production == BodyProductionKind::ReceiverFieldWrite) {
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

  // An annotated owner local (`let x: T = init`) binds to its annotation; the
  // initializer must meet that annotation. Owner locals are bindings rather
  // than materialized definitions, so the definition-type loop above never
  // visits them. A mismatch is an ordinary type error (ZOM4009), matching the
  // materialized-initializer rail; a selected concrete-to-dyn erasure at the
  // initializer site coerces and is accepted.
  for (const auto& local : input.boundModule.definitions().ownerLocalBindings()) {
    if (!local.site.value().is<binder::PatternBindingSite>()) continue;
    const auto& localSite = local.site.value().get<binder::PatternBindingSite>();
    const auto& localTree = input.boundModule.tree();
    if (!localTree.contains(localSite.introducer) ||
        localTree.node(localSite.introducer).kind != ast::SyntaxKind::VariableDeclarator) {
      continue;
    }
    const auto& declarator = localTree.node(localSite.introducer);
    const ast::NodeId annotation(declarator.payload.words[ast::kVariableDeclaratorTyWord]);
    const ast::NodeId initializer(declarator.payload.words[ast::kVariableDeclaratorInitWord]);
    if (!localTree.contains(annotation) || !localTree.contains(initializer)) continue;
    auto declaredType = ownerLocalInitializerDeclaredType(input.boundModule, input.identities,
                                                          input.semanticTypes, initializer);
    auto initializerType = factEntry(nodeTypes.asPtr(), initializer);
    if (declaredType == zc::none || initializerType == zc::none) continue;
    if (ZC_ASSERT_NONNULL(declaredType) == ZC_ASSERT_NONNULL(initializerType).value) continue;
    bool erasedHere = false;
    for (const auto& erase : dynErases) {
      if (erase.node == initializer && erase.existential == ZC_ASSERT_NONNULL(declaredType) &&
          erase.concrete == ZC_ASSERT_NONNULL(initializerType).value) {
        erasedHere = true;
        break;
      }
    }
    if (erasedHere) continue;
    const auto initializerProductionSite =
        productionSite(input.requirements.impl->productionSiteValues.asPtr(), initializer);
    ZC_IF_SOME(owner, enclosingBodyOwner(input.boundModule, initializer)) {
      ZC_IF_SOME(ownerOrdinal, definitionPreorder(input.boundModule, owner)) {
        ZC_IF_SOME(initializerSite, initializerProductionSite) {
          return attachRecoveryLedger(
              rejectTypeMismatch(initializerSite, ownerOrdinal, ZC_ASSERT_NONNULL(declaredType),
                                 ZC_ASSERT_NONNULL(initializerType).value),
              input, factStoreBrands);
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
      auto initializerCast = factEntry(casts.asPtr(), initializer);
      auto bindingProduction =
          productionSite(input.requirements.impl->productionSiteValues.asPtr(), pattern);
      auto declaredType = valueType(input.signatureFacts, definition.definition);
      auto ownerPreorder = definitionPreorder(input.boundModule, definition.definition);
      if (initializerType == zc::none ||
          (initializerLiteral == zc::none && initializerAggregate == zc::none &&
           initializerCast == zc::none) ||
          bindingProduction == zc::none || declaredType == zc::none || ownerPreorder == zc::none) {
        return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, module, 0,
                               definition.definition, initializer, definition.source.clone(),
                               factPath(CheckedFactGroup::DefinitionType));
      }

      ZC_IF_SOME(typeEntry, initializerType) {
        ZC_IF_SOME(expectedType, declaredType) {
          ZC_IF_SOME(ownerOrdinal, ownerPreorder) {
            if (typeEntry.value != expectedType) {
              bool erasedHere = false;
              for (const auto& erase : dynErases) {
                if (erase.node == initializer && erase.existential == expectedType &&
                    erase.concrete == typeEntry.value) {
                  erasedHere = true;
                  break;
                }
              }
              if (!erasedHere) {
                const auto site = productionSite(
                    input.requirements.impl->productionSiteValues.asPtr(), initializer);
                if (site == zc::none) {
                  return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact,
                                         module, 0, definition.definition, initializer,
                                         definition.source.clone(),
                                         factPath(CheckedFactGroup::DefinitionType));
                }
                ZC_IF_SOME(value, site) {
                  return attachRecoveryLedger(
                      rejectTypeMismatch(value, ownerOrdinal, expectedType, typeEntry.value), input,
                      factStoreBrands);
                }
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

  // Produce exhaustiveness facts for admitted two-arm matches. The scrutinee
  // type is already in nodeTypes from the main production loop. A bool
  // scrutinee produces a Closed domain with the two bool literal constructors;
  // an integer scrutinee produces an OpenRequiresCatchAll domain with the
  // single integer literal constructor read from the literal arm's pattern.
  for (const auto& site : input.requirements.impl->productionSiteValues) {
    if (site.primaryGroup != CheckedFactGroup::Exhaustiveness) continue;
    const auto& matchNode = input.boundModule.tree().node(site.node);
    const ast::NodeId scrutinee(matchNode.payload.words[ast::kMatchStmtScrutineeWord]);
    auto scrutineeType = factEntry(nodeTypes.asPtr(), scrutinee);
    if (scrutineeType == zc::none) {
      return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, module,
                             site.key.schemaPreorder, zc::none, site.node,
                             site.key.sourceSpan.clone(), factPath(CheckedFactGroup::NodeType));
    }
    identity::SemanticTypeId scrutineeTypeId;
    ZC_IF_SOME(entry, scrutineeType) { scrutineeTypeId = entry.value; }
    auto scrutineeKind = primitiveKindOf(input.semanticTypes, scrutineeTypeId);
    if (scrutineeKind == zc::none) {
      return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, module,
                             site.key.schemaPreorder, zc::none, site.node,
                             site.key.sourceSpan.clone(), factPath(CheckedFactGroup::NodeType));
    }
    bool isBool = false;
    ZC_IF_SOME(kind, scrutineeKind) { isBool = kind == type::semantic::PrimitiveKind::Bool; }
    if (isBool) {
      zc::Vector<checked::PatternConstructor> covered;
      covered.add(checked::PatternConstructor(
          checked::LiteralPattern{checked::CanonicalLiteral::boolean(false)}));
      covered.add(checked::PatternConstructor(
          checked::LiteralPattern{checked::CanonicalLiteral::boolean(true)}));
      exhaustiveness.add(checked::ExhaustivenessFactMap::Entry{
          site.node,
          checked::ExhaustivenessFact{
              site.node, scrutineeTypeId, checked::ExhaustivenessDomain::Closed, zc::mv(covered),
              zc::Vector<checked::PatternConstructor>(), zc::Vector<ast::NodeId>()},
          zc::Array<uint8_t>()});
    } else {
      // Integer scrutinee: find the literal arm's pattern and read its checked
      // literal fact. The open integer domain requires the default arm.
      const ast::NodeList arms{matchNode.payload.words[ast::kMatchStmtArmsFirstWord],
                               matchNode.payload.words[ast::kMatchStmtArmsSizeWord]};
      if (!input.boundModule.tree().contains(arms) || arms.size != 2) {
        return rejectInvariant(
            signature::CheckerInvariantKind::InvalidFact, module, site.key.schemaPreorder, zc::none,
            site.node, site.key.sourceSpan.clone(), factPath(CheckedFactGroup::Exhaustiveness));
      }
      zc::Maybe<checked::CanonicalLiteral> integerLiteral;
      for (size_t index = 0; index < arms.size; ++index) {
        const ast::NodeId armId = input.boundModule.tree().list(arms)[index];
        if (!input.boundModule.tree().contains(armId)) continue;
        const auto& arm = input.boundModule.tree().node(armId);
        if (arm.kind != ast::SyntaxKind::MatchArmStmt) continue;
        const ast::NodeId pattern(arm.payload.words[ast::kMatchArmStmtPatternWord]);
        if (!input.boundModule.tree().contains(pattern) ||
            input.boundModule.tree().node(pattern).kind != ast::SyntaxKind::LiteralPattern) {
          continue;
        }
        const ast::NodeId literal(
            input.boundModule.tree().node(pattern).payload.words[ast::kLiteralPatternLiteralWord]);
        if (!input.boundModule.tree().contains(literal) ||
            input.boundModule.tree().node(literal).kind != ast::SyntaxKind::IntLiteral) {
          continue;
        }
        auto literalFact = factEntry(literals.asPtr(), literal);
        if (literalFact == zc::none) {
          return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, module,
                                 site.key.schemaPreorder, zc::none, literal,
                                 site.key.sourceSpan.clone(), factPath(CheckedFactGroup::Literal));
        }
        ZC_IF_SOME(fact, literalFact) { integerLiteral = fact.value.literal.clone(); }
      }
      if (integerLiteral == zc::none) {
        return rejectInvariant(
            signature::CheckerInvariantKind::InvalidFact, module, site.key.schemaPreorder, zc::none,
            site.node, site.key.sourceSpan.clone(), factPath(CheckedFactGroup::Exhaustiveness));
      }
      zc::Vector<checked::PatternConstructor> covered;
      ZC_IF_SOME(value, integerLiteral) {
        covered.add(checked::PatternConstructor(checked::LiteralPattern{zc::mv(value)}));
      }
      exhaustiveness.add(checked::ExhaustivenessFactMap::Entry{
          site.node,
          checked::ExhaustivenessFact{site.node, scrutineeTypeId,
                                      checked::ExhaustivenessDomain::OpenRequiresCatchAll,
                                      zc::mv(covered), zc::Vector<checked::PatternConstructor>(),
                                      zc::Vector<ast::NodeId>()},
          zc::Array<uint8_t>()});
    }
  }

  for (const auto& requirement : input.requirements.nodeRequirements()) {
    if (requirement.group != CheckedFactGroup::NodeType &&
        requirement.group != CheckedFactGroup::Literal &&
        requirement.group != CheckedFactGroup::Aggregate &&
        requirement.group != CheckedFactGroup::Call &&
        requirement.group != CheckedFactGroup::Place &&
        requirement.group != CheckedFactGroup::Coercion &&
        requirement.group != CheckedFactGroup::Cast &&
        requirement.group != CheckedFactGroup::Member &&
        requirement.group != CheckedFactGroup::Index &&
        requirement.group != CheckedFactGroup::MarkerObligation &&
        requirement.group != CheckedFactGroup::Pattern &&
        requirement.group != CheckedFactGroup::Exhaustiveness) {
      return rejectInvariant(signature::CheckerInvariantKind::MissingRequiredFact, module,
                             requirement.key.schemaPreorder, zc::none, requirement.node,
                             requirement.key.sourceSpan.clone(), factPath(requirement.group));
    }
  }
  // Only annotated-initializer erasures occupy the top-level Coercion fact
  // Top-level coercions (annotated initializer and return site) each occupy a
  // node requirement in the Coercion group; argument-position erasures ride
  // inside their call's argument adjustments and are validated through the
  // call fact instead.
  size_t topLevelEraseCount = 0;
  for (const auto& erase : dynErases) {
    if (erase.site == checked::CoercionSite::AnnotatedInitializer ||
        erase.site == checked::CoercionSite::Return) {
      ++topLevelEraseCount;
    }
  }
  if (nodeTypes.size() !=
          requirementCount(input.requirements.nodeRequirements(), CheckedFactGroup::NodeType) ||
      literals.size() !=
          requirementCount(input.requirements.nodeRequirements(), CheckedFactGroup::Literal) ||
      aggregates.size() !=
          requirementCount(input.requirements.nodeRequirements(), CheckedFactGroup::Aggregate) ||
      casts.size() !=
          requirementCount(input.requirements.nodeRequirements(), CheckedFactGroup::Cast) ||
      calls.size() !=
          requirementCount(input.requirements.nodeRequirements(), CheckedFactGroup::Call) ||
      places.size() !=
          requirementCount(input.requirements.nodeRequirements(), CheckedFactGroup::Place) ||
      topLevelEraseCount !=
          requirementCount(input.requirements.nodeRequirements(), CheckedFactGroup::Coercion) ||
      members.size() !=
          requirementCount(input.requirements.nodeRequirements(), CheckedFactGroup::Member) ||
      indexes.size() !=
          requirementCount(input.requirements.nodeRequirements(), CheckedFactGroup::Index) ||
      markerObligations.size() != requirementCount(input.requirements.nodeRequirements(),
                                                   CheckedFactGroup::MarkerObligation) ||
      patterns.size() !=
          requirementCount(input.requirements.nodeRequirements(), CheckedFactGroup::Pattern) ||
      exhaustiveness.size() != requirementCount(input.requirements.nodeRequirements(),
                                                CheckedFactGroup::Exhaustiveness) ||
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
    if (dynErases.size() != 0) {
      // One witness argument set per distinct (subject, interface, impl)
      // triple, in site order. Every in-slice erasure shares one witness set;
      // dedupe repeated erasures through the same impl.
      // One witness argument set per distinct (subject, interface, impl)
      // triple, in site order. The interface participates in the key: although
      // one impl currently satisfies exactly one principal, two erasures of
      // the same subject through different interface instantiations must not
      // collapse into one witness set.
      zc::Vector<checked::WitnessEntry> entries;
      for (const auto& erase : dynErases) {
        bool duplicate = false;
        for (const auto& existing : entries) {
          if (existing.subject == erase.concrete &&
              existing.interface.interface == erase.interface && existing.impl == erase.impl) {
            duplicate = true;
            break;
          }
        }
        if (duplicate) { continue; }
        zc::Vector<checked::AssociatedTypeBindingData> bindings;
        for (const auto& binding : erase.bindings) {
          bindings.add(checked::AssociatedTypeBindingData{binding.associated, binding.type});
        }
        entries.add(checked::WitnessEntry{
            erase.concrete,
            signature::InterfaceInstantiation{erase.interface,
                                              zc::Vector<identity::SemanticTypeId>()},
            erase.impl, zc::mv(bindings), zc::Vector<checked::WitnessArgumentsId>()});
      }
      checked::WitnessArgumentsData data;
      for (auto& entry : entries) { data.entries.add(zc::mv(entry)); }
      // The store canonical-order gate requires a non-empty record; with a
      // single record the byte content is not compared here. Field validity is
      // independently checked when a coercion references this witness set.
      records.add(checked::FrozenWitnessStore::Record{zc::mv(data),
                                                      zc::heapArray<uint8_t>(1, uint8_t{0xa1})});
    }
    witnesses = checked::FrozenWitnessStore::from(context, brand, zc::mv(records));
  }
  if (substitutions == zc::none || witnesses == zc::none) {
    return rejectInvariant(signature::CheckerInvariantKind::InferenceLifecycle, module, 0);
  }

  // Build the concrete-to-dyn coercion facts once the witness store is frozen,
  // since each DynErase step references the frozen witness set id.
  zc::Maybe<checked::WitnessArgumentsId> eraseWitnessId;
  if (dynErases.size() != 0) {
    ZC_IF_SOME(witnessView, witnesses) { eraseWitnessId = witnessView.idAt(0); }
    if (eraseWitnessId == zc::none) {
      return rejectInvariant(signature::CheckerInvariantKind::CanonicalCodecMismatch, module, 0);
    }
  }
  zc::Vector<checked::CoercionFactMap::Entry> coercionEntries;
  if (dynErases.size() != 0) {
    ZC_IF_SOME(witnessId, eraseWitnessId) {
      for (const auto& erase : dynErases) {
        // Argument-position erasures ride inside the call's CheckedArgumentFact
        // adjustment rather than the top-level coercion map.
        if (erase.site != checked::CoercionSite::AnnotatedInitializer &&
            erase.site != checked::CoercionSite::Return) {
          continue;
        }
        zc::Vector<checked::CoercionStep> steps;
        steps.add(checked::CoercionStep(
            checked::DynEraseStep{signature::InterfaceInstantiation{
                                      erase.interface, zc::Vector<identity::SemanticTypeId>()},
                                  erase.impl, witnessId}));
        auto eraseSpan = input.boundModule.parsedModule().spanFor(
            input.boundModule.tree().node(erase.node).range);
        coercionEntries.add(checked::CoercionFactMap::Entry{
            erase.node,
            checked::CoercionAdjustment{erase.site, erase.concrete, erase.existential,
                                        zc::mv(steps), zc::mv(ZC_ASSERT_NONNULL(eraseSpan))},
            zc::Array<uint8_t>()});
      }
      // Attach the argument-position DynErase adjustments to their enclosing
      // call facts. Each adjustment source/destination must equal the argument
      // fact's source/parameter types, which the verifier re-checks.
      if (eraseWitnessId != zc::none) {
        for (auto& callEntry : calls) {
          zc::Vector<checked::CheckedArgumentFact> adjustedArguments;
          for (auto& argument : callEntry.value.invocation.arguments) {
            zc::Maybe<checked::CoercionAdjustment> adjustment = zc::mv(argument.adjustment);
            if (adjustment == zc::none) {
              for (const auto& erase : dynErases) {
                if (erase.site != checked::CoercionSite::Argument ||
                    erase.node != argument.sourceNode || erase.concrete != argument.sourceType ||
                    erase.existential != argument.parameterType) {
                  continue;
                }
                zc::Vector<checked::CoercionStep> steps;
                steps.add(checked::CoercionStep(checked::DynEraseStep{
                    signature::InterfaceInstantiation{erase.interface,
                                                      zc::Vector<identity::SemanticTypeId>()},
                    erase.impl, ZC_ASSERT_NONNULL(eraseWitnessId)}));
                auto span = input.boundModule.parsedModule().spanFor(
                    input.boundModule.tree().node(erase.node).range);
                adjustment = checked::CoercionAdjustment{
                    checked::CoercionSite::Argument, erase.concrete, erase.existential,
                    zc::mv(steps), zc::mv(ZC_ASSERT_NONNULL(span))};
                break;
              }
            }
            adjustedArguments.add(
                checked::CheckedArgumentFact{argument.sourceNode, argument.sourceType,
                                             argument.parameterType, zc::mv(adjustment)});
          }
          callEntry.value.invocation.arguments = zc::mv(adjustedArguments);
        }
      }
    }
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
          checked::CoercionFactMap::fromEntries(zc::mv(coercionEntries)),
          checked::CastFactMap::fromEntries(zc::mv(casts)),
          checked::CallFactMap::fromEntries(zc::mv(calls)),
          emptyFactMap<checked::CompoundAssignmentFactMap>(),
          checked::MemberFactMap::fromEntries(zc::mv(members)),
          checked::IndexFactMap::fromEntries(zc::mv(indexes)),
          checked::PatternFactMap::fromEntries(zc::mv(patterns)),
          emptyFactMap<checked::ObservedOperationFactMap>(),
          emptyFactMap<checked::CaptureFactMap>(),
          checked::MarkerObligationFactMap::fromEntries(zc::mv(markerObligations)),
          checked::ExhaustivenessFactMap::fromEntries(zc::mv(exhaustiveness)),
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
