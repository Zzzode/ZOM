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
// same-typed scalars. Unlike a comparison, the result is the operand type, not
// bool; the logical short-circuit operators (`&&` / `||`) are excluded.
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
      return true;
    default:
      return false;
  }
}

// Returns true when the syntactic binary operator is a relational comparison or
// an arithmetic/bitwise operator, i.e. a primitive binary operation lowerable in
// return position. Strict identity and the logical short-circuit operators are
// excluded.

bool noUnsupportedFacts(const checker::checked::VerifiedCheckedFacts& facts) {
  return facts.coercions().size() == 0 && facts.casts().size() == 0 &&
         facts.compoundAssignments().size() == 0 && facts.observedOperations().size() == 0 &&
         facts.captures().size() == 0 && facts.exhaustiveness().size() == 0 &&
         facts.unsafeOperations().size() == 0 && facts.projections().size() == 0 &&
         facts.obligations().size() == 0 && facts.errorUnionShapes().size() == 0 &&
         facts.errorOperators().size() == 0;
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

}  // namespace detail
}  // namespace zomlang::compiler::hir
