// Copyright (c) 2026 Zode.Z. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/mono/instance-set-builder.h"

#include "zc/core/debug.h"
#include "compiler/checker/facts/checked-facts-repository.h"
#include "compiler/identity/diagnostics/identity-invariant.h"
#include "compiler/ir/instance-identity.h"
#include "compiler/mono/instance-fixpoint.h"
#include "compiler/mono/instance-set.h"
#include "compiler/ownership/overlay/ownership-checked-mir.h"

namespace zomlang::compiler::mono {
namespace {

using checker::checked::CanonicalSubstitutionId;
using checker::checked::CheckedFactsRecordView;
using checker::checked::SubstitutionData;
using checker::checked::WitnessArgumentsId;
using identity::IdentityInvariant;
using identity::IdentityInvariantKind;
using identity::SourceSpan;
using ir::InstanceId;
using ir::IrFailureDescriptor;
using ir::IrFailureDetail;
using ir::IrFailureFactory;
using ir::IrFailureKind;
using ir::IrFailureOwner;
using ir::IrFailurePhase;
using ir::IrOperationResult;
using mir::MirFunction;
using type::SemanticTypeStore;

/// \brief Repository-backed evidence provider for one module's mono pass.
class SessionMonoEvidenceProvider final : public MonoEvidenceProvider {
public:
  SessionMonoEvidenceProvider(const ownership::CheckedMir& checkedMir,
                              const checker::signature::VerifiedSignatureFacts& signatures,
                              const CheckedFactsRecordView& records,
                              const checker::CheckerIdentityAuthority& identities)
      : checkedMir(checkedMir), signatures(signatures), records(records), identities(identities) {}

  zc::Maybe<const MirFunction&> templateFor(identity::DefId definition) const override {
    for (const auto& function : checkedMir.builtMir().functions()) {
      if (function.owner == definition) { return function; }
    }
    return zc::none;
  }

  zc::Maybe<zc::Vector<identity::GenericParameterKey>> genericParameterKeys(
      identity::DefId definition) const override {
    for (const auto& signature : signatures.signatures()) {
      if (signature.definition != definition) { continue; }
      const auto& payload = signature.payload.variant();
      if (!payload.is<checker::signature::CallableSignature>()) { return zc::none; }
      const auto& callable = payload.get<checker::signature::CallableSignature>();
      if (callable.genericParameters.empty()) { return zc::none; }
      zc::Vector<identity::GenericParameterKey> keys;
      for (const auto& parameter : callable.genericParameters) {
        keys.add(parameter.parameter.clone());
      }
      return zc::mv(keys);
    }
    return zc::none;
  }

  zc::Maybe<const SubstitutionData&> substitution(CanonicalSubstitutionId id) const override {
    return records.substitution(id);
  }
  zc::Maybe<zc::ArrayPtr<const uint8_t>> substitutionRecord(CanonicalSubstitutionId id)
      const override {
    return records.substitutionRecord(id);
  }
  zc::Maybe<zc::ArrayPtr<const uint8_t>> witnessRecord(WitnessArgumentsId id) const override {
    return records.witnessRecord(id);
  }
  zc::Maybe<zc::Array<uint8_t>> definitionKey(identity::DefId definition) const override {
    auto entry = identities.definition(definition);
    if (entry == zc::none) { return zc::none; }
    return ZC_ASSERT_NONNULL(entry).key().encode();
  }

private:
  const ownership::CheckedMir& checkedMir;
  const checker::signature::VerifiedSignatureFacts& signatures;
  const CheckedFactsRecordView& records;
  const checker::CheckerIdentityAuthority& identities;
};

/// \brief Expands instance identities from definition keys and frozen records.
class MonoIdentityResolver final : public ir::IrFailureIdentityResolver {
public:
  MonoIdentityResolver(const checker::CheckerIdentityAuthority& identities,
                       const CheckedFactsRecordView& records)
      : identities(identities), records(records) {}

  ir::ExpandedIrIdentityResult expand(identity::ModuleId module) const override {
    auto entry = identities.module(module);
    if (entry == zc::none) { return rejectIdentity(); }
    return encodeExpanded(ZC_ASSERT_NONNULL(entry).key().encode());
  }

  ir::ExpandedIrIdentityResult expand(identity::DefId definition) const override {
    auto entry = identities.definition(definition);
    if (entry == zc::none) { return rejectIdentity(); }
    return encodeExpanded(ZC_ASSERT_NONNULL(entry).key().encode());
  }

  ir::ExpandedIrIdentityResult expand(InstanceId instance) const override {
    auto definition = identities.definition(instance.definition());
    auto substitution = records.substitutionRecord(instance.substitution());
    auto witness = records.witnessRecord(instance.witnesses());
    if (definition == zc::none || substitution == zc::none || witness == zc::none) {
      return rejectIdentity();
    }
    auto frame = ir::encodeInstanceIdentity(ZC_ASSERT_NONNULL(definition).key().encode().asPtr(),
                                            ZC_ASSERT_NONNULL(substitution),
                                            ZC_ASSERT_NONNULL(witness));
    if (frame == zc::none) { return rejectIdentity(); }
    return encodeExpanded(zc::mv(ZC_ASSERT_NONNULL(frame)));
  }

private:
  static ir::ExpandedIrIdentityResult encodeExpanded(zc::Array<uint8_t>&& bytes) {
    auto expanded = ir::ExpandedIrIdentity::from(zc::mv(bytes));
    if (expanded == zc::none) { return rejectIdentity(); }
    return ir::ExpandedIrIdentityValue{zc::mv(ZC_ASSERT_NONNULL(expanded))};
  }

  static ir::RejectedIrIdentityValue rejectIdentity() {
    zc::Maybe<zc::Array<uint8_t>> noKey;
    zc::Maybe<identity::UnbrandedSourceRange> noRange;
    auto invariant = IdentityInvariant::from(
        IdentityInvariantKind::InvalidHandle, identity::IdentityAllocationPhase::Registry,
        zc::mv(noKey), zc::mv(noRange), identity::IdentityApiSite::HandleLookup, 0);
    return ir::RejectedIrIdentityValue{zc::mv(ZC_ASSERT_NONNULL(invariant))};
  }

  const checker::CheckerIdentityAuthority& identities;
  const CheckedFactsRecordView& records;
};

IdentityInvariant invalidMonoIdentity() {
  zc::Maybe<zc::Array<uint8_t>> noKey;
  zc::Maybe<identity::UnbrandedSourceRange> noRange;
  auto invariant = IdentityInvariant::from(
      IdentityInvariantKind::InvalidHandle, identity::IdentityAllocationPhase::Registry,
      zc::mv(noKey), zc::mv(noRange), identity::IdentityApiSite::HandleLookup, 0);
  return zc::mv(ZC_ASSERT_NONNULL(invariant));
}

template <typename VerifiedValue>
IrOperationResult<VerifiedValue> rejectMonoIdentity(IdentityInvariant invariant) {
  zc::Vector<IdentityInvariant> failures;
  failures.add(zc::mv(invariant));
  auto sorted = ir::SortedIdentityInvariantFacts::from(zc::mv(failures));
  ZC_IF_SOME(values, sorted) {
    return IrOperationResult<VerifiedValue>::identityInvariantRejected(zc::mv(values));
  }
  ZC_UNREACHABLE
}

/// \brief Admits one legal mono capability descriptor into a sorted failure set.
///
/// Mono descriptors are constructed exclusively in legal 0x0a matrix rows; the
/// fallback context (needed by the admission API) is therefore never consumed.
template <typename VerifiedValue>
IrOperationResult<VerifiedValue> rejectMonoCapability(
    IrFailureKind kind, InstanceId owner, IrFailureDetail&& detail, zc::Maybe<SourceSpan> span,
    const ir::IrFailureIdentityResolver& resolver, uint32_t ordinal) {
  zc::Maybe<ir::IrFailureSite> site;
  auto descriptor = ir::IrFailureDescriptor::decoded(
      ir::IrRejectedBranch::CapabilityRejected, ir::IrFailurePhase::Monomorphization, kind,
      ir::IrFailureOwner::instance(owner), zc::mv(site), detail.clone(), zc::mv(span),
      zc::Vector<uint32_t>(), ordinal);
  ZC_IREQUIRE(ir::isLegalIrFailureShape(descriptor.shape()),
              "mono capability descriptor must be a legal RFC 0010 matrix row");
  auto fallback = ir::IrFailureFallbackContext::from(
      ir::IrFailurePhase::MirConstruction, ir::IrFailureOwner::definition(owner.definition()));
  ZC_IREQUIRE(fallback != zc::none, "mono fallback context must be a legal matrix row");
  auto admitted =
      IrFailureFactory::admit(zc::mv(descriptor), ZC_ASSERT_NONNULL(fallback), resolver);
  if (admitted.is<ir::IdentityRejectedIrFailureDescriptor>()) {
    return rejectMonoIdentity<VerifiedValue>(
        zc::mv(admitted).get<ir::IdentityRejectedIrFailureDescriptor>().failure);
  }
  zc::Vector<ir::IrFailureFact> facts;
  if (admitted.is<ir::AcceptedIrFailureDescriptor>()) {
    facts.add(zc::mv(admitted).get<ir::AcceptedIrFailureDescriptor>().fact);
  } else {
    facts.add(zc::mv(admitted).get<ir::FallbackIrFailureDescriptor>().fact);
  }
  auto sorted = ir::SortedCapabilityFailureFacts::from(zc::mv(facts));
  ZC_IF_SOME(values, sorted) {
    return IrOperationResult<VerifiedValue>::capabilityRejected(zc::mv(values));
  }
  ZC_UNREACHABLE
}

/// \brief Selects the non-generic source roots in deterministic MIR order.
zc::Vector<const MirFunction*> sourceRoots(const ownership::CheckedMir& checkedMir,
                                           const MonoEvidenceProvider& provider) {
  zc::Vector<const MirFunction*> roots;
  for (const auto& function : checkedMir.builtMir().functions()) {
    if (provider.genericParameterKeys(function.owner) == zc::none) {
      roots.add(&function);
    }
  }
  return roots;
}

zc::Vector<InstanceId> cloneChain(zc::ArrayPtr<const InstanceId> chain) {
  zc::Vector<InstanceId> cloned;
  for (const auto& instance : chain) { cloned.add(instance); }
  return cloned;
}

/// \brief Encodes one substituted body with the shared MIR function codec.
zc::Maybe<zc::Array<uint8_t>> encodeBody(const MirFunction& body,
                                         const ownership::CheckedMir& checkedMir,
                                         const checker::CheckerIdentityAuthority& identities,
                                         const CheckedFactsRecordView& records,
                                         const SemanticTypeStore& semanticTypes) {
  zc::Maybe<CheckedFactsRecordView> recordView;
  recordView.emplace(records);
  return mir::MirFunctionCanonicalCodec::encode(body, checkedMir.module(), identities,
                                                semanticTypes, zc::mv(recordView));
}

}  // namespace

IrOperationResult<MonoInstanceSetCandidate> MonoInstanceSetBuilder::build(
    const MonomorphizationInput& input) {
  SessionMonoEvidenceProvider provider(input.checkedMir, input.signatures, input.records,
                                       input.identities);
  MonoIdentityResolver resolver(input.identities, input.records);
  auto roots = sourceRoots(input.checkedMir, provider);
  auto result = collectInstances(roots.asPtr(), provider, input.semanticTypes, input.instanceBudget,
                                 input.substitutionNodeBudget);
  if (result.is<MonoCycleError>()) {
    const auto& cycle = result.get<MonoCycleError>();
    auto detail = ir::IrFailureDetail::instantiationCycle(cycle.root, cloneChain(cycle.chain));
    if (detail == zc::none) { return rejectMonoIdentity<MonoInstanceSetCandidate>(invalidMonoIdentity()); }
    return rejectMonoCapability<MonoInstanceSetCandidate>(
        IrFailureKind::RecursiveInstantiation, cycle.root, zc::mv(ZC_ASSERT_NONNULL(detail)),
        cycle.span.clone(), resolver, 0);
  }
  if (result.is<MonoBudgetError>()) {
    const auto& budget = result.get<MonoBudgetError>();
    auto detail = ir::IrFailureDetail::instantiationBudget(
        budget.root, cloneChain(budget.chain), budget.requestedInstanceCount,
        budget.requestedSubstitutionNodeCount, budget.instanceLimit,
        budget.substitutionNodeLimit);
    if (detail == zc::none) { return rejectMonoIdentity<MonoInstanceSetCandidate>(invalidMonoIdentity()); }
    return rejectMonoCapability<MonoInstanceSetCandidate>(
        IrFailureKind::InstantiationBudgetExceeded, budget.root,
        zc::mv(ZC_ASSERT_NONNULL(detail)), budget.span.clone(), resolver, 0);
  }
  if (result.is<MonoUnresolvedEdgeError>()) {
    return rejectMonoIdentity<MonoInstanceSetCandidate>(invalidMonoIdentity());
  }

  auto collected = zc::mv(result.get<zc::Vector<MonoRawInstance>>());
  sortInstancesByIdentity(collected);
  zc::Vector<MonoInstance> instances;
  for (auto& raw : collected) {
    auto encoded = encodeBody(raw.body, input.checkedMir, input.identities, input.records,
                              input.semanticTypes);
    if (encoded == zc::none) {
      return rejectMonoIdentity<MonoInstanceSetCandidate>(invalidMonoIdentity());
    }
    instances.add(MonoInstance{raw.identity, zc::mv(raw.body), zc::mv(raw.identityFrame),
                               zc::mv(ZC_ASSERT_NONNULL(encoded)), zc::mv(raw.callSiteSpan)});
  }
  zc::Vector<zc::Array<uint8_t>> canonicalBodies;
  for (const auto& instance : instances) {
    canonicalBodies.add(zc::heapArray(instance.canonicalBody.asPtr()));
  }
  zc::Vector<zc::Array<uint8_t>> identityFrames;
  for (const auto& instance : instances) {
    identityFrames.add(zc::heapArray(instance.identityFrame.asPtr()));
  }
  auto moduleEntry = input.identities.module(input.checkedMir.module());
  if (moduleEntry == zc::none) {
    return rejectMonoIdentity<MonoInstanceSetCandidate>(invalidMonoIdentity());
  }
  auto expandedModuleKey = ZC_ASSERT_NONNULL(moduleEntry).key().encode();
  auto revision = MonoInstanceSetRevisionCodec::computeFramed(
      input.checkedMir.contextFingerprint().digest(), expandedModuleKey.asPtr(),
      input.checkedMir.builtRevision().digest(),
      input.checkedMir.eventOverlayRevision().digest(),
      input.checkedMir.factsRevision().digest(), identityFrames.asPtr(), canonicalBodies.asPtr());
  if (revision == zc::none) {
    return rejectMonoIdentity<MonoInstanceSetCandidate>(invalidMonoIdentity());
  }
  return IrOperationResult<MonoInstanceSetCandidate>::verified(
      MonoInstanceSetCandidate(zc::mv(instances), ZC_ASSERT_NONNULL(revision)));
}

namespace {

bool sameByteSlice(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) {
  if (left.size() != right.size()) { return false; }
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index] != right[index]) { return false; }
  }
  return true;
}

bool instanceIsMember(zc::ArrayPtr<const MonoRawInstance> instances, const InstanceId& instance) {
  for (const auto& member : instances) {
    if (member.identity == instance) { return true; }
  }
  return false;
}

}  // namespace

IrOperationResult<VerifiedMonoInstanceSet> MonoInstanceSetVerifier::verify(
    MonoInstanceSetCandidate&& candidate, const MonomorphizationInput& input) {
  SessionMonoEvidenceProvider provider(input.checkedMir, input.signatures, input.records,
                                       input.identities);
  MonoIdentityResolver resolver(input.identities, input.records);
  auto roots = sourceRoots(input.checkedMir, provider);
  auto result = collectInstances(roots.asPtr(), provider, input.semanticTypes, input.instanceBudget,
                                 input.substitutionNodeBudget);
  if (result.is<MonoCycleError>()) {
    const auto& cycle = result.get<MonoCycleError>();
    auto detail = ir::IrFailureDetail::instantiationCycle(cycle.root, cloneChain(cycle.chain));
    if (detail == zc::none) { return rejectMonoIdentity<VerifiedMonoInstanceSet>(invalidMonoIdentity()); }
    return rejectMonoCapability<VerifiedMonoInstanceSet>(
        IrFailureKind::RecursiveInstantiation, cycle.root, zc::mv(ZC_ASSERT_NONNULL(detail)),
        cycle.span.clone(), resolver, 0);
  }
  if (result.is<MonoBudgetError>()) {
    const auto& budget = result.get<MonoBudgetError>();
    auto detail = ir::IrFailureDetail::instantiationBudget(
        budget.root, cloneChain(budget.chain), budget.requestedInstanceCount,
        budget.requestedSubstitutionNodeCount, budget.instanceLimit,
        budget.substitutionNodeLimit);
    if (detail == zc::none) { return rejectMonoIdentity<VerifiedMonoInstanceSet>(invalidMonoIdentity()); }
    return rejectMonoCapability<VerifiedMonoInstanceSet>(
        IrFailureKind::InstantiationBudgetExceeded, budget.root,
        zc::mv(ZC_ASSERT_NONNULL(detail)), budget.span.clone(), resolver, 0);
  }
  if (result.is<MonoUnresolvedEdgeError>()) {
    return rejectMonoIdentity<VerifiedMonoInstanceSet>(invalidMonoIdentity());
  }

  auto expected = zc::mv(result.get<zc::Vector<MonoRawInstance>>());
  sortInstancesByIdentity(expected);
  if (!instancesAreFrameSorted(expected.asPtr()) || expected.size() != candidate.instances.size()) {
    return rejectMonoIdentity<VerifiedMonoInstanceSet>(invalidMonoIdentity());
  }
  uint64_t substitutedNodes = 0;
  for (size_t index = 0; index < expected.size(); ++index) {
    const auto& derived = expected[index];
    const auto& published = candidate.instances[index];
    substitutedNodes += derived.substitutedNodeCount;
    if (derived.identity != published.identity ||
        !sameByteSlice(derived.identityFrame.asPtr(), published.identityFrame.asPtr())) {
      return rejectMonoIdentity<VerifiedMonoInstanceSet>(invalidMonoIdentity());
    }
    // Every frame must be a valid domain-tagged instance expansion whose
    // components belong to this semantic context.
    auto decoded = ir::decodeInstanceIdentity(published.identityFrame.asPtr());
    if (decoded == zc::none) {
      return rejectMonoIdentity<VerifiedMonoInstanceSet>(invalidMonoIdentity());
    }
    auto contextual = ir::InstanceId::from(
        input.checkedMir.semanticContext(), published.identity.definition(),
        published.identity.substitution(), published.identity.witnesses());
    if (contextual == zc::none ||
        ZC_ASSERT_NONNULL(contextual) != published.identity) {
      return rejectMonoIdentity<VerifiedMonoInstanceSet>(invalidMonoIdentity());
    }
    auto encoded = encodeBody(derived.body, input.checkedMir, input.identities, input.records,
                              input.semanticTypes);
    if (encoded == zc::none ||
        !sameByteSlice(ZC_ASSERT_NONNULL(encoded).asPtr(), published.canonicalBody.asPtr())) {
      return rejectMonoIdentity<VerifiedMonoInstanceSet>(invalidMonoIdentity());
    }
    auto keys = provider.genericParameterKeys(derived.identity.definition());
    if (keys == zc::none) {
      return rejectMonoIdentity<VerifiedMonoInstanceSet>(invalidMonoIdentity());
    }
    auto bodyRejection =
        validateInstanceBody(derived.body, ZC_ASSERT_NONNULL(keys).asPtr(), input.semanticTypes);
    if (bodyRejection != zc::none) {
      zc::Maybe<SourceSpan> span = published.callSiteSpan.clone();
      return rejectMonoCapability<VerifiedMonoInstanceSet>(
          IrFailureKind::UnsupportedSourceConstruct, derived.identity, IrFailureDetail::none(),
          zc::mv(span), resolver, ZC_ASSERT_NONNULL(bodyRejection).blockOrdinal);
    }
    auto edgeRejection = validateCallEdges(derived.body, provider, [&](InstanceId instance) {
      return instanceIsMember(expected.asPtr(), instance);
    });
    if (edgeRejection != zc::none) {
      zc::Maybe<SourceSpan> span = published.callSiteSpan.clone();
      return rejectMonoCapability<VerifiedMonoInstanceSet>(
          IrFailureKind::UnsupportedSourceConstruct, derived.identity, IrFailureDetail::none(),
          zc::mv(span), resolver, ZC_ASSERT_NONNULL(edgeRejection).blockOrdinal);
    }
  }
  if (expected.size() > input.instanceBudget ||
      substitutedNodes > input.substitutionNodeBudget) {
    return rejectMonoIdentity<VerifiedMonoInstanceSet>(invalidMonoIdentity());
  }
  // Root Instance edges must close over the published member set; a root edge
  // that fails key or arity validation is malformed checked evidence, not a
  // user capability failure.
  for (const auto* root : roots) {
    auto edgeRejection = validateCallEdges(*root, provider, [&](InstanceId instance) {
      return instanceIsMember(expected.asPtr(), instance);
    });
    if (edgeRejection != zc::none) {
      return rejectMonoIdentity<VerifiedMonoInstanceSet>(invalidMonoIdentity());
    }
  }

  zc::Vector<zc::Array<uint8_t>> canonicalBodies;
  zc::Vector<zc::Array<uint8_t>> identityFrames;
  for (const auto& instance : candidate.instances) {
    canonicalBodies.add(zc::heapArray(instance.canonicalBody.asPtr()));
    identityFrames.add(zc::heapArray(instance.identityFrame.asPtr()));
  }
  auto moduleEntry = input.identities.module(input.checkedMir.module());
  if (moduleEntry == zc::none) {
    return rejectMonoIdentity<VerifiedMonoInstanceSet>(invalidMonoIdentity());
  }
  auto expandedModuleKey = ZC_ASSERT_NONNULL(moduleEntry).key().encode();
  auto revision = MonoInstanceSetRevisionCodec::computeFramed(
      input.checkedMir.contextFingerprint().digest(), expandedModuleKey.asPtr(),
      input.checkedMir.builtRevision().digest(),
      input.checkedMir.eventOverlayRevision().digest(),
      input.checkedMir.factsRevision().digest(), identityFrames.asPtr(), canonicalBodies.asPtr());
  if (revision == zc::none ||
      ZC_ASSERT_NONNULL(revision).digest() != candidate.revision.digest()) {
    return rejectMonoIdentity<VerifiedMonoInstanceSet>(invalidMonoIdentity());
  }

  auto overlayRevision =
      ownership::EventOverlayRevision::fromDigest(input.checkedMir.eventOverlayRevision().digest());
  auto factsRevision =
      ownership::facts::FactsRevision::fromDigest(input.checkedMir.factsRevision().digest());
  return IrOperationResult<VerifiedMonoInstanceSet>::verified(publishMonoInstanceSet(
      input.checkedMir.semanticContext(), input.checkedMir.module(),
      input.checkedMir.builtRevision(), zc::mv(overlayRevision), zc::mv(factsRevision),
      ZC_ASSERT_NONNULL(revision), zc::mv(candidate.instances)));
}

}  // namespace zomlang::compiler::mono
