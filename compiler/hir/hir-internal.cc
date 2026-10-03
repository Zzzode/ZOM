// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/hir/hir-internal.h"

#include "compiler/identity/key/definition-key.h"

namespace zomlang::compiler::hir {
namespace detail {

bool sameSpan(const identity::SourceSpan& left, const identity::SourceSpan& right) {
  return left.source().sameAs(right.source()) && left.byteStart() == right.byteStart() &&
         left.byteEnd() == right.byteEnd();
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

bool isCompoundAssignment(ast::AssignmentOperatorKind op) noexcept {
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

zc::Maybe<checker::PrimitiveOperation> compoundAssignmentBinaryOperation(
    ast::AssignmentOperatorKind op) noexcept {
  switch (op) {
    case ast::AssignmentOperatorKind::AddAssign:
      return checker::PrimitiveOperation::Add;
    case ast::AssignmentOperatorKind::SubAssign:
      return checker::PrimitiveOperation::Sub;
    case ast::AssignmentOperatorKind::MulAssign:
      return checker::PrimitiveOperation::Mul;
    case ast::AssignmentOperatorKind::DivAssign:
      return checker::PrimitiveOperation::Div;
    case ast::AssignmentOperatorKind::ModAssign:
      return checker::PrimitiveOperation::Rem;
    case ast::AssignmentOperatorKind::BitAndAssign:
      return checker::PrimitiveOperation::BitAnd;
    case ast::AssignmentOperatorKind::BitOrAssign:
      return checker::PrimitiveOperation::BitOr;
    case ast::AssignmentOperatorKind::BitXorAssign:
      return checker::PrimitiveOperation::BitXor;
    case ast::AssignmentOperatorKind::ShlAssign:
      return checker::PrimitiveOperation::Shl;
    case ast::AssignmentOperatorKind::ShrAssign:
      return checker::PrimitiveOperation::Shr;
    default:
      return zc::none;
  }
}

zc::Maybe<size_t> definitionIndex(const binder::ImmutableDefinitionInventory& definitions,
                                  identity::DefId definition) {
  const auto entries = definitions.definitions();
  for (size_t index = 0; index < entries.size(); ++index) {
    if (entries[index].definition == definition) return index;
  }
  return zc::none;
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

bool hasExecutableBody(const binder::MaterializedDefinitionInventoryEntry& definition,
                       const binder::ImmutableDefinitionInventory& definitions) {
  for (const auto& body : definitions.ownerBodies()) {
    const auto& owner = body.owner().owner();
    if (owner.kind() == binder::StableBodyOwnerKind::Definition) {
      auto ownerDefinition = owner.definitionKey();
      ZC_IF_SOME(key, ownerDefinition) {
        if (key == definition.key) { return true; }
      }
      continue;
    }
    if (definition.record.owners().size() == 0) { return true; }
  }
  return false;
}

size_t executableDefinitionCount(const binder::ImmutableDefinitionInventory& definitions) {
  size_t count = 0;
  for (const auto& definition : definitions.definitions()) {
    if (hasExecutableBody(definition, definitions) &&
        (definition.record.kind() == identity::DefinitionKind::Function ||
         definition.record.kind() == identity::DefinitionKind::Method ||
         definition.record.kind() == identity::DefinitionKind::Static ||
         definition.record.kind() == identity::DefinitionKind::Constant)) {
      ++count;
    }
  }
  return count;
}

bool sameConstant(const checker::checked::CanonicalConstValue& left,
                  const checker::checked::CanonicalConstValue& right, identity::ModuleId module,
                  const checker::CheckerIdentityAuthority& identities,
                  const type::SemanticTypeStore& semanticTypes) {
  auto leftBytes =
      checker::signature::SignatureFactsCanonicalCodec::encodeCanonicalConstValueFromAuthority(
          left, module, identities, semanticTypes);
  auto rightBytes =
      checker::signature::SignatureFactsCanonicalCodec::encodeCanonicalConstValueFromAuthority(
          right, module, identities, semanticTypes);
  if (leftBytes == zc::none || rightBytes == zc::none) return false;
  bool equal = false;
  ZC_IF_SOME(leftValue, leftBytes) {
    ZC_IF_SOME(rightValue, rightBytes) { equal = leftValue.asPtr() == rightValue.asPtr(); }
  }
  return equal;
}

zc::Maybe<HirLinkage> linkage(const checker::signature::ValueSignature& signature) {
  if (signature.abi == zc::none) return HirLinkage::Internal;
  ZC_IF_SOME(abi, signature.abi) {
    switch (abi) {
      case checker::signature::ExternAbi::Cdecl:
        return HirLinkage::ExternalCdecl;
      case checker::signature::ExternAbi::Stdcall:
        return HirLinkage::ExternalStdcall;
      case checker::signature::ExternAbi::ZomNative:
        return HirLinkage::ExternalZomNative;
    }
  }
  return zc::none;
}

zc::Maybe<HirLinkage> linkage(const checker::signature::CallableSignature& signature) {
  if (signature.abi == zc::none) return HirLinkage::Internal;
  ZC_IF_SOME(abi, signature.abi) {
    switch (abi) {
      case checker::signature::ExternAbi::Cdecl:
        return HirLinkage::ExternalCdecl;
      case checker::signature::ExternAbi::Stdcall:
        return HirLinkage::ExternalStdcall;
      case checker::signature::ExternAbi::ZomNative:
        return HirLinkage::ExternalZomNative;
    }
  }
  return zc::none;
}

zc::Maybe<HirVisibility> visibility(const binder::VisibilityEnvelope& source) {
  if (source.value().is<binder::ModuleVisibility>()) {
    return HirVisibility::module(source.value().get<binder::ModuleVisibility>().module);
  }
  if (source.value().is<binder::ExternalVisibility>()) return HirVisibility::external();
  return zc::none;
}

HirVisibility memberVisibility(checker::signature::MemberVisibility source,
                               identity::ModuleId module) {
  switch (source) {
    case checker::signature::MemberVisibility::Public:
      return HirVisibility::external();
    case checker::signature::MemberVisibility::Protected:
    case checker::signature::MemberVisibility::Private:
      return HirVisibility::module(module);
  }
  ZC_UNREACHABLE
}

bool sameVisibility(const HirVisibility& left, const HirVisibility& right) {
  if (left.kind() != right.kind()) return false;
  if (left.kind() == HirVisibilityKind::External) return true;
  return left.visibleModule() == right.visibleModule();
}

HirNodeId hirId(uint32_t ordinal) {
  auto value = HirNodeId::fromOrdinal(ordinal);
  ZC_IF_SOME(id, value) { return id; }
  ZC_UNREACHABLE
}

HirLocalId hirLocalId(uint32_t ordinal) {
  auto value = HirLocalId::fromOrdinal(ordinal);
  ZC_IF_SOME(id, value) { return id; }
  ZC_UNREACHABLE
}

// Returns true for the six relational comparison operators of same-typed
// scalars that the conditional-condition lowering supports.
bool isScalarComparisonOperation(checker::PrimitiveOperation operation) {
  switch (operation) {
    case checker::PrimitiveOperation::Eq:
    case checker::PrimitiveOperation::Ne:
    case checker::PrimitiveOperation::Lt:
    case checker::PrimitiveOperation::Le:
    case checker::PrimitiveOperation::Gt:
    case checker::PrimitiveOperation::Ge:
      return true;
    default:
      return false;
  }
}

// Returns true for the twelve arithmetic and bitwise binary operators of
// same-typed scalars, plus the two logical short-circuit operators. The
// short-circuit operators are admitted because the HIR slice only accepts
// operands with no side effects (literals, parameters, earlier locals), so
// lowering to bitwise And/Or is semantically equivalent; the MIR builder
// maps LogicalAnd to BitAnd and LogicalOr to BitOr.
bool isScalarArithmeticOperation(checker::PrimitiveOperation operation) {
  switch (operation) {
    case checker::PrimitiveOperation::Add:
    case checker::PrimitiveOperation::Sub:
    case checker::PrimitiveOperation::Mul:
    case checker::PrimitiveOperation::Div:
    case checker::PrimitiveOperation::Rem:
    case checker::PrimitiveOperation::Pow:
    case checker::PrimitiveOperation::Shl:
    case checker::PrimitiveOperation::Shr:
    case checker::PrimitiveOperation::UShr:
    case checker::PrimitiveOperation::BitAnd:
    case checker::PrimitiveOperation::BitOr:
    case checker::PrimitiveOperation::BitXor:
    case checker::PrimitiveOperation::LogicalAnd:
    case checker::PrimitiveOperation::LogicalOr:
      return true;
    default:
      return false;
  }
}

// Returns true for the four primitive unary operators that the unary-return
// lowering supports. Each is desugared to an equivalent binary operation in the
// HIR builder, so no new MIR/LIR carrier is needed.
bool isScalarUnaryOperation(checker::PrimitiveOperation operation) {
  switch (operation) {
    case checker::PrimitiveOperation::Neg:
    case checker::PrimitiveOperation::UnaryPlus:
    case checker::PrimitiveOperation::BitNot:
    case checker::PrimitiveOperation::LogicalNot:
      return true;
    default:
      return false;
  }
}

// Returns true when the syntactic binary operator is a relational comparison or
// an arithmetic/bitwise operator, i.e. a primitive binary operation lowerable in
// return position. Strict identity and the logical short-circuit operators are
// excluded.

// True when every unsupported family other than coercions is empty. Coercions
// are handled separately so a single concrete-to-dyn erasure can fail closed as
// a per-definition capability rejection instead of a module invariant.
bool unsupportedNonErasureFacts(const checker::checked::VerifiedCheckedFacts& facts) {
  return facts.compoundAssignments().size() == 0 && facts.observedOperations().size() == 0 &&
         facts.captures().size() == 0 && facts.unsafeOperations().size() == 0 &&
         facts.projections().size() == 0 && facts.obligations().size() == 0 &&
         facts.errorUnionShapes().size() == 0 && facts.errorOperators().size() == 0;
}

// A concrete-to-dyn erasure the lowering carrier does not implement yet:
// exactly one DynErase step and nothing else.
bool isSingleDynEraseAdjustment(const checker::checked::CoercionAdjustment& adjustment) {
  return adjustment.steps.size() == 1 &&
         adjustment.steps[0].variant().is<checker::checked::DynEraseStep>();
}

bool isDeadErasedInitializer(const ast::Tree& tree,
                             const binder::ImmutableDefinitionInventory& definitions,
                             ast::NodeId initializerNode) {
  if (!tree.contains(initializerNode)) return false;
  // Find the VariableDeclarator whose init word is the initializer, then read
  // its identifier pattern name.
  zc::Maybe<ast::NodeId> pattern;
  for (const auto& local : definitions.ownerLocalBindings()) {
    if (!local.site.value().is<binder::PatternBindingSite>()) continue;
    const auto& site = local.site.value().get<binder::PatternBindingSite>();
    if (!tree.contains(site.introducer) ||
        tree.node(site.introducer).kind != ast::SyntaxKind::VariableDeclarator) {
      continue;
    }
    const auto& declarator = tree.node(site.introducer);
    if (ast::NodeId(declarator.payload.words[ast::kVariableDeclaratorInitWord]) !=
        initializerNode) {
      continue;
    }
    pattern = ast::NodeId(declarator.payload.words[ast::kVariableDeclaratorPatternWord]);
    break;
  }
  if (pattern == zc::none) return false;
  ast::NodeId patternNode;
  ZC_IF_SOME(value, pattern) { patternNode = value; }
  if (!tree.contains(patternNode) ||
      tree.node(patternNode).kind != ast::SyntaxKind::IdentifierPattern) {
    return false;
  }
  const auto name = tree.node(patternNode).payload.words[ast::kIdentifierPatternNameWord];
  // Locate the enclosing function body so the scan covers every read site.
  const auto owner = enclosingExecutableDefinition(tree, definitions, initializerNode);
  if (owner == zc::none) return false;
  for (const auto& definition : definitions.definitions()) {
    if (definition.definition != ZC_ASSERT_NONNULL(owner)) continue;
    if (!tree.contains(definition.node) ||
        tree.node(definition.node).kind != ast::SyntaxKind::FunctionDecl) {
      continue;
    }
    const ast::NodeId body(tree.node(definition.node).payload.words[ast::kFunctionDeclBodyWord]);
    if (!tree.contains(body)) return false;
    // Scan the body for an IdentExpr whose name matches the binding. The
    // pattern declaration itself is an IdentifierPattern, not an IdentExpr, so
    // the declaration is never counted as a read. The initializer expression
    // is the RHS of the let; any IdentExpr inside it names a different binding
    // (the concrete source), never the erased local itself.
    bool read = false;
    ast::visitTreePreOrder(tree, body, [&](ast::NodeId node, const ast::Node& syntax) {
      if (read) return;
      if (syntax.kind == ast::SyntaxKind::IdentExpr &&
          syntax.payload.words[ast::kIdentExprNameWord] == name) {
        read = true;
      }
    });
    return !read;
  }
  return false;
}

zc::Maybe<identity::DefId> implMethodOwner(
    const binder::MaterializedDefinitionInventoryEntry& definition,
    const checker::CheckerIdentityAuthority& identities,
    zc::ArrayPtr<const checker::signature::ImplHead> implHeads) {
  for (const auto& owner : definition.record.owners()) {
    if (owner.kind() != identity::EnclosingStableOwnerKind::Implementation) continue;
    ZC_IF_SOME(key, owner.implKey()) {
      ZC_IF_SOME(entry, identities.implementation(key)) {
        const auto implId = entry.handle();
        for (const auto& head : implHeads) {
          if (head.impl != implId) continue;
          if (!head.head.variant().is<checker::signature::NominalTypeHead>()) return zc::none;
          return head.head.variant().get<checker::signature::NominalTypeHead>().definition;
        }
      }
    }
    return zc::none;
  }
  return zc::none;
}

bool isDevirtualizedEraseInitializer(const ast::Tree& tree,
                                     const binder::ImmutableDefinitionInventory& definitions,
                                     const checker::checked::VerifiedCheckedFacts& facts,
                                     ast::NodeId initializerNode) {
  if (!tree.contains(initializerNode)) return false;
  // Find the VariableDeclarator whose init word is the initializer, then read
  // its identifier pattern name. Mirrors isDeadErasedInitializer.
  zc::Maybe<ast::NodeId> pattern;
  for (const auto& local : definitions.ownerLocalBindings()) {
    if (!local.site.value().is<binder::PatternBindingSite>()) continue;
    const auto& site = local.site.value().get<binder::PatternBindingSite>();
    if (!tree.contains(site.introducer) ||
        tree.node(site.introducer).kind != ast::SyntaxKind::VariableDeclarator) {
      continue;
    }
    const auto& declarator = tree.node(site.introducer);
    if (ast::NodeId(declarator.payload.words[ast::kVariableDeclaratorInitWord]) !=
        initializerNode) {
      continue;
    }
    pattern = ast::NodeId(declarator.payload.words[ast::kVariableDeclaratorPatternWord]);
    break;
  }
  if (pattern == zc::none) return false;
  ast::NodeId patternNode;
  ZC_IF_SOME(value, pattern) { patternNode = value; }
  if (!tree.contains(patternNode) ||
      tree.node(patternNode).kind != ast::SyntaxKind::IdentifierPattern) {
    return false;
  }
  const auto name = tree.node(patternNode).payload.words[ast::kIdentifierPatternNameWord];
  const auto owner = enclosingExecutableDefinition(tree, definitions, initializerNode);
  if (owner == zc::none) return false;
  for (const auto& definition : definitions.definitions()) {
    if (definition.definition != ZC_ASSERT_NONNULL(owner)) continue;
    if (!tree.contains(definition.node) ||
        tree.node(definition.node).kind != ast::SyntaxKind::FunctionDecl) {
      continue;
    }
    const ast::NodeId body(tree.node(definition.node).payload.words[ast::kFunctionDeclBodyWord]);
    if (!tree.contains(body)) return false;
    // Collect the IdentExpr nodes that are receivers of devirtualized calls.
    // A receiver is the object of a dot-access MemberExpression that is the
    // callee of a CallExpression whose call fact selects an ImplMethodCallable.
    zc::Vector<ast::NodeId> devirtualizedReceivers;
    ast::visitTreePreOrder(tree, body, [&](ast::NodeId node, const ast::Node& syntax) {
      if (syntax.kind != ast::SyntaxKind::CallExpression) return;
      const ast::NodeId callee(syntax.payload.words[ast::kCallExpressionCalleeWord]);
      if (!tree.contains(callee) || tree.node(callee).kind != ast::SyntaxKind::MemberExpression) {
        return;
      }
      const auto& member = tree.node(callee);
      if (static_cast<ast::MemberAccessKind>(
              member.payload.words[ast::kMemberExpressionAccessWord]) !=
          ast::MemberAccessKind::Dot) {
        return;
      }
      const ast::NodeId object(member.payload.words[ast::kMemberExpressionObjectWord]);
      if (!tree.contains(object) || tree.node(object).kind != ast::SyntaxKind::IdentExpr ||
          tree.node(object).payload.words[ast::kIdentExprNameWord] != name) {
        return;
      }
      for (const auto& entry : facts.calls().entries()) {
        if (entry.key != node) continue;
        if (entry.value.invocation.selected.variant().is<checker::checked::ImplMethodCallable>()) {
          devirtualizedReceivers.add(object);
        }
        break;
      }
    });
    // Every IdentExpr in the body that matches the binding name must be a
    // receiver of a devirtualized call. A read in any other position (return,
    // argument, field access, standalone) rejects the erasure.
    bool allDevirtualized = true;
    ast::visitTreePreOrder(tree, body, [&](ast::NodeId node, const ast::Node& syntax) {
      if (!allDevirtualized) return;
      if (syntax.kind != ast::SyntaxKind::IdentExpr ||
          syntax.payload.words[ast::kIdentExprNameWord] != name) {
        return;
      }
      bool found = false;
      for (const auto& receiver : devirtualizedReceivers) {
        if (receiver == node) {
          found = true;
          break;
        }
      }
      if (!found) { allDevirtualized = false; }
    });
    return allDevirtualized;
  }
  return false;
}

zc::Vector<ast::NodeId> deadEraseInitializerNodes(
    const ast::Tree& tree, const binder::ImmutableDefinitionInventory& definitions,
    const checker::checked::VerifiedCheckedFacts& facts) {
  zc::Vector<ast::NodeId> nodes;
  for (const auto& entry : facts.coercions().entries()) {
    if (entry.value.site == checker::checked::CoercionSite::AnnotatedInitializer &&
        isSingleDynEraseAdjustment(entry.value) &&
        isDeadErasedInitializer(tree, definitions, entry.key)) {
      nodes.add(entry.key);
    }
  }
  return nodes;
}

zc::Vector<ast::NodeId> enumConstructionInitializerNodes(const ast::Tree& tree) {
  zc::Vector<ast::NodeId> nodes;
  ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId node, const ast::Node& syntax) {
    if (syntax.kind != ast::SyntaxKind::CallExpression) return;
    const ast::NodeId callee(syntax.payload.words[ast::kCallExpressionCalleeWord]);
    if (!tree.contains(callee) || tree.node(callee).kind != ast::SyntaxKind::MemberExpression) {
      return;
    }
    if (static_cast<ast::MemberAccessKind>(
            tree.node(callee).payload.words[ast::kMemberExpressionAccessWord]) !=
        ast::MemberAccessKind::Qualified) {
      return;
    }
    const ast::NodeId object(tree.node(callee).payload.words[ast::kMemberExpressionObjectWord]);
    if (!tree.contains(object) || tree.node(object).kind != ast::SyntaxKind::IdentExpr) { return; }
    nodes.add(node);
  });
  return nodes;
}

// Returns the innermost executable definition whose AST subtree contains
// `node`, or none. Innermost is the definition with the deepest owner chain
// (nested function declarations nest their ranges), mirroring the body
// checker's enclosingBodyOwner; an equal-depth tie is ambiguous and yields
// none instead of attributing the failure to the outermost function.
zc::Maybe<identity::DefId> enclosingExecutableDefinition(
    const ast::Tree& tree, const binder::ImmutableDefinitionInventory& definitions,
    ast::NodeId node) {
  if (!tree.contains(node)) { return zc::none; }
  const auto nodeRange = tree.node(node).range;
  if (nodeRange.isInvalid()) { return zc::none; }
  zc::Maybe<identity::DefId> result;
  size_t bestDepth = 0;
  for (const auto& definition : definitions.definitions()) {
    if (!hasExecutableBody(definition, definitions)) { continue; }
    if (!tree.contains(definition.node)) { continue; }
    const auto range = tree.node(definition.node).range;
    if (!range.isValid() || range.getStart() > nodeRange.getStart() ||
        nodeRange.getEnd() > range.getEnd()) {
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
    if (resolution.node != node || !resolution.value.is<binder::BoundNameResolution>()) continue;
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

bool ownerLocalMatches(const binder::ImmutableDefinitionInventory& definitions,
                       binder::OwnerLocalBindingId binding, ast::NodeId pattern,
                       const ast::Tree& tree) {
  for (const auto& local : definitions.ownerLocalBindings()) {
    if (local.binding != binding) continue;
    if (!local.site.value().is<binder::PatternBindingSite>()) return false;
    const auto& site = local.site.value().get<binder::PatternBindingSite>();
    if (!tree.contains(site.introducer) ||
        tree.node(site.introducer).kind != ast::SyntaxKind::VariableDeclarator) {
      return false;
    }
    return ast::NodeId(
               tree.node(site.introducer).payload.words[ast::kVariableDeclaratorPatternWord]) ==
           pattern;
  }
  return false;
}

zc::Maybe<binder::OwnerLocalBindingId> ownerLocalBindingForPattern(
    const binder::ImmutableDefinitionInventory& definitions, ast::NodeId pattern,
    const ast::Tree& tree) {
  zc::Maybe<binder::OwnerLocalBindingId> result;
  for (const auto& local : definitions.ownerLocalBindings()) {
    if (!local.site.value().is<binder::PatternBindingSite>()) continue;
    const auto& site = local.site.value().get<binder::PatternBindingSite>();
    if (!tree.contains(site.introducer) ||
        tree.node(site.introducer).kind != ast::SyntaxKind::VariableDeclarator) {
      continue;
    }
    if (ast::NodeId(
            tree.node(site.introducer).payload.words[ast::kVariableDeclaratorPatternWord]) !=
        pattern) {
      continue;
    }
    if (result != zc::none) return zc::none;
    result = local.binding;
  }
  return result;
}

zc::Maybe<checker::checked::CheckedNodeKey> checkedNodeKey(
    const ast::Tree& tree, const binder::CanonicalParsedModule& parsedModule, ast::NodeId target) {
  zc::Maybe<checker::checked::CheckedNodeKey> result;
  uint32_t preorder = 0;
  ast::visitTreePreOrder(tree, tree.root(), [&](ast::NodeId node, const ast::Node& syntax) {
    const uint32_t ordinal = preorder++;
    if (node != target || result != zc::none) return;
    auto sourceSpan = parsedModule.spanFor(syntax.range);
    ZC_IF_SOME(span, sourceSpan) {
      result = checker::checked::CheckedNodeKey{static_cast<uint32_t>(syntax.kind), ordinal,
                                                span.clone()};
    }
  });
  return result;
}

bool sameNodeKey(const checker::checked::CheckedNodeKey& left,
                 const checker::checked::CheckedNodeKey& right) {
  return left.syntaxKind == right.syntaxKind && left.schemaPreorder == right.schemaPreorder &&
         sameSpan(left.sourceSpan, right.sourceSpan);
}

zc::Maybe<size_t> dispatchFactIndex(
    zc::ArrayPtr<const checker::dispatch::VerifiedDispatchFact> facts,
    const checker::checked::CheckedNodeKey& node) {
  zc::Maybe<size_t> result;
  for (size_t index = 0; index < facts.size(); ++index) {
    if (!sameNodeKey(facts[index].checkedNode, node)) continue;
    if (result != zc::none) return zc::none;
    result = index;
  }
  return result;
}

zc::Maybe<size_t> signatureIndex(
    zc::ArrayPtr<const checker::signature::SemanticSignature> signatures,
    identity::DefId definition) {
  zc::Maybe<size_t> result;
  for (size_t index = 0; index < signatures.size(); ++index) {
    if (signatures[index].definition != definition) continue;
    if (result != zc::none) return zc::none;
    result = index;
  }
  return result;
}

zc::Maybe<size_t> signatureRootIndex(
    zc::ArrayPtr<const module_interface::SignatureRootAuthorization> roots,
    identity::DefId definition) {
  zc::Maybe<size_t> result;
  for (size_t index = 0; index < roots.size(); ++index) {
    if (roots[index].canonicalDefinition != definition) continue;
    if (result != zc::none) return zc::none;
    result = index;
  }
  return result;
}

bool definitionBelongsToModule(const binder::MaterializedDefinitionInventoryEntry& definition,
                               const binder::ImmutableDefinitionInventory& definitions) {
  return definition.record.module().encode().asPtr() ==
         definitions.identities().stableWitness().module().encode().asPtr();
}

bool typeExists(identity::SemanticTypeId semanticType,
                const type::SemanticTypeStore& semanticTypes) {
  return semanticTypes.get(semanticType).is<type::SemanticTypeLookup>();
}

bool isBoolSemanticType(const type::SemanticTypeStore& store, identity::SemanticTypeId id) {
  auto lookup = store.get(id);
  return lookup.is<type::SemanticTypeLookup>() &&
         lookup.get<type::SemanticTypeLookup>().data().is<type::semantic::PrimitiveTypeData>() &&
         lookup.get<type::SemanticTypeLookup>()
                 .data()
                 .get<type::semantic::PrimitiveTypeData>()
                 .kind == type::semantic::PrimitiveKind::Bool;
}

}  // namespace detail
}  // namespace zomlang::compiler::hir
