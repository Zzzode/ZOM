// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include <cstdint>

#include "compiler/ast/generated/node-payload.h"
#include "compiler/ast/generated/node-traverse.h"
#include "compiler/ast/tree.h"
#include "compiler/binder/metadata/definition-inventory.h"
#include "compiler/binder/metadata/definition-site.h"
#include "compiler/binder/metadata/immutable-binding-metadata.h"
#include "compiler/binder/metadata/immutable-definition-inventory.h"
#include "compiler/checker/checker-identity-authority.h"
#include "compiler/checker/facts/signature-facts.h"
#include "compiler/checker/inference/checked-facts.h"
#include "compiler/hir/hir-module.h"
#include "compiler/identity/brand.h"
#include "compiler/ir/diagnostics/ir-failure.h"
#include "compiler/type/semantic-type-store.h"

namespace zomlang::compiler::hir {
namespace detail {

inline identity::IdentityInvariant invalidIdentity(identity::IdentityAllocationPhase phase,
                                                   uint32_t ordinal) {
  zc::Maybe<zc::Array<uint8_t>> noKey;
  zc::Maybe<identity::UnbrandedSourceRange> noRange;
  auto invariant = identity::IdentityInvariant::from(
      identity::IdentityInvariantKind::InvalidHandle, phase, zc::mv(noKey), zc::mv(noRange),
      identity::IdentityApiSite::HandleLookup, ordinal);
  ZC_IF_SOME(value, invariant) { return zc::mv(value); }
  ZC_UNREACHABLE
}

class AuthorityIdentityResolver final : public ir::IrFailureIdentityResolver {
public:
  explicit AuthorityIdentityResolver(const checker::CheckerIdentityAuthority& identities) noexcept
      : identities(identities) {}

  ir::ExpandedIrIdentityResult expand(identity::ModuleId module) const override {
    auto key = identities.module(module);
    if (key == zc::none) {
      return ir::RejectedIrIdentityValue{
          invalidIdentity(identity::IdentityAllocationPhase::Module, 0)};
    }
    ZC_IF_SOME(value, key) {
      auto expanded = ir::ExpandedIrIdentity::from(value.key().encode());
      ZC_IF_SOME(bytes, expanded) { return ir::ExpandedIrIdentityValue{zc::mv(bytes)}; }
    }
    return ir::RejectedIrIdentityValue{
        invalidIdentity(identity::IdentityAllocationPhase::Encoding, 0)};
  }

  ir::ExpandedIrIdentityResult expand(identity::DefId definition) const override {
    auto key = identities.definition(definition);
    if (key == zc::none) {
      return ir::RejectedIrIdentityValue{
          invalidIdentity(identity::IdentityAllocationPhase::Definition, 0)};
    }
    ZC_IF_SOME(value, key) {
      auto expanded = ir::ExpandedIrIdentity::from(value.key().encode());
      ZC_IF_SOME(bytes, expanded) { return ir::ExpandedIrIdentityValue{zc::mv(bytes)}; }
    }
    return ir::RejectedIrIdentityValue{
        invalidIdentity(identity::IdentityAllocationPhase::Encoding, 0)};
  }

  ir::ExpandedIrIdentityResult expand(ir::InstanceId) const override {
    return ir::RejectedIrIdentityValue{
        invalidIdentity(identity::IdentityAllocationPhase::Definition, 0)};
  }

private:
  const checker::CheckerIdentityAuthority& identities;
};

template <typename VerifiedValue>
ir::IrOperationResult<VerifiedValue> rejectHir(
    ir::IrFailurePhase phase, ir::IrFailureKind kind, identity::ModuleId module,
    const checker::CheckerIdentityAuthority& identities, uint32_t ordinal,
    zc::Vector<uint32_t>&& fieldPath = zc::Vector<uint32_t>()) {
  AuthorityIdentityResolver resolver(identities);
  auto fallback = ir::IrFailureFallbackContext::from(phase, ir::IrFailureOwner::module(module));
  ZC_IREQUIRE(fallback != zc::none, "HIR failure fallback must be legal");
  zc::Maybe<ir::IrFailureSite> noSite;
  zc::Maybe<identity::SourceSpan> noSpan;
  auto descriptor = ir::IrFailureDescriptor::decoded(
      ir::IrRejectedBranch::IrInvariantRejected, phase, kind, ir::IrFailureOwner::module(module),
      zc::mv(noSite), ir::IrFailureDetail::none(), zc::mv(noSpan), zc::mv(fieldPath), ordinal);
  ZC_IF_SOME(fallbackValue, fallback) {
    auto admitted = ir::IrFailureFactory::admit(zc::mv(descriptor), fallbackValue, resolver);
    if (admitted.is<ir::IdentityRejectedIrFailureDescriptor>()) {
      zc::Vector<identity::IdentityInvariant> failures;
      failures.add(zc::mv(admitted).get<ir::IdentityRejectedIrFailureDescriptor>().failure);
      auto sorted = ir::SortedIdentityInvariantFacts::from(zc::mv(failures));
      ZC_IF_SOME(values, sorted) {
        return ir::IrOperationResult<VerifiedValue>::identityInvariantRejected(zc::mv(values));
      }
      ZC_UNREACHABLE
    }
    zc::Vector<ir::IrFailureFact> failures;
    if (admitted.is<ir::AcceptedIrFailureDescriptor>()) {
      failures.add(zc::mv(admitted).get<ir::AcceptedIrFailureDescriptor>().fact);
    } else {
      failures.add(zc::mv(admitted).get<ir::FallbackIrFailureDescriptor>().fact);
    }
    auto sorted = ir::SortedIrInvariantFailureFacts::from(zc::mv(failures));
    ZC_IF_SOME(values, sorted) {
      return ir::IrOperationResult<VerifiedValue>::irInvariantRejected(zc::mv(values));
    }
  }
  ZC_UNREACHABLE
}

template <typename Map, typename Key>
zc::Maybe<size_t> factIndex(const Map& map, const Key& key) {
  const auto entries = map.entries();
  for (size_t index = 0; index < entries.size(); ++index) {
    if (entries[index].key == key) return index;
  }
  return zc::none;
}

bool sameSpan(const identity::SourceSpan& left, const identity::SourceSpan& right);

bool isScalarLiteral(ast::SyntaxKind kind) noexcept;

zc::Maybe<size_t> definitionIndex(const binder::ImmutableDefinitionInventory& definitions,
                                  identity::DefId definition);

zc::Maybe<const binder::PatternBindingSite&> patternBindingSite(
    const binder::DefinitionInventory& inventory,
    const binder::MaterializedDefinitionInventoryEntry& definition);

bool hasExecutableBody(const binder::MaterializedDefinitionInventoryEntry& definition,
                       const binder::ImmutableDefinitionInventory& definitions);

size_t executableDefinitionCount(const binder::ImmutableDefinitionInventory& definitions);

bool sameConstant(const checker::checked::CanonicalConstValue& left,
                  const checker::checked::CanonicalConstValue& right, identity::ModuleId module,
                  const checker::CheckerIdentityAuthority& identities,
                  const type::SemanticTypeStore& semanticTypes);

zc::Maybe<HirLinkage> linkage(const checker::signature::ValueSignature& signature);

zc::Maybe<HirLinkage> linkage(const checker::signature::CallableSignature& signature);

zc::Maybe<HirVisibility> visibility(const binder::VisibilityEnvelope& source);

bool sameVisibility(const HirVisibility& left, const HirVisibility& right);

HirNodeId hirId(uint32_t ordinal);

HirLocalId hirLocalId(uint32_t ordinal);

// Returns true for the six relational comparison operators of same-typed
// scalars that the conditional-condition lowering supports.
bool isScalarComparisonOperation(checker::PrimitiveOperation operation);

// Returns true for the twelve arithmetic and bitwise binary operators of
// same-typed scalars. Unlike a comparison, the result is the operand type, not
// bool; the logical short-circuit operators (`&&` / `||`) are excluded.
bool isScalarArithmeticOperation(checker::PrimitiveOperation operation);

bool noUnsupportedFacts(const checker::checked::VerifiedCheckedFacts& facts);

zc::Maybe<identity::DefId> resolvedDefinition(const binder::ImmutableBindingMetadata& bindings,
                                              ast::NodeId node);

zc::Maybe<binder::OwnerLocalBindingId> resolvedOwnerLocal(
    const binder::ImmutableBindingMetadata& bindings, ast::NodeId node);

zc::Maybe<identity::CallableParameterId> resolvedCallableParameter(
    const binder::ImmutableBindingMetadata& bindings, ast::NodeId node);

bool ownerLocalMatches(const binder::ImmutableDefinitionInventory& definitions,
                       binder::OwnerLocalBindingId binding, ast::NodeId pattern,
                       const ast::Tree& tree);

zc::Maybe<binder::OwnerLocalBindingId> ownerLocalBindingForPattern(
    const binder::ImmutableDefinitionInventory& definitions, ast::NodeId pattern,
    const ast::Tree& tree);

zc::Maybe<checker::checked::CheckedNodeKey> checkedNodeKey(
    const ast::Tree& tree, const binder::CanonicalParsedModule& parsedModule, ast::NodeId target);

bool sameNodeKey(const checker::checked::CheckedNodeKey& left,
                 const checker::checked::CheckedNodeKey& right);

zc::Maybe<size_t> dispatchFactIndex(
    zc::ArrayPtr<const checker::dispatch::VerifiedDispatchFact> facts,
    const checker::checked::CheckedNodeKey& node);

zc::Maybe<size_t> signatureIndex(
    zc::ArrayPtr<const checker::signature::SemanticSignature> signatures,
    identity::DefId definition);

zc::Maybe<size_t> signatureRootIndex(
    zc::ArrayPtr<const module_interface::SignatureRootAuthorization> roots,
    identity::DefId definition);

bool definitionBelongsToModule(const binder::MaterializedDefinitionInventoryEntry& definition,
                               const binder::ImmutableDefinitionInventory& definitions);

bool typeExists(identity::SemanticTypeId semanticType,
                const type::SemanticTypeStore& semanticTypes);

}  // namespace detail
}  // namespace zomlang::compiler::hir
