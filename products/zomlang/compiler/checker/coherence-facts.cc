// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/checker/coherence-facts.h"

#include "zomlang/compiler/identity/canonical/canonical-encoder.h"

namespace zomlang::compiler::checker::coherence {
namespace {

bool lessBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) noexcept {
  const size_t common = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < common; ++index) {
    if (left[index] != right[index]) return left[index] < right[index];
  }
  return left.size() < right.size();
}

bool sameBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) noexcept {
  return !lessBytes(left, right) && !lessBytes(right, left);
}

bool sameDigest(const identity::Sha256Digest& left, const identity::Sha256Digest& right) noexcept {
  return sameBytes(left.bytes(), right.bytes());
}

signature::CheckerVerificationFailure invariant(signature::CheckerInvariantKind kind,
                                                identity::ModuleId module, uint32_t ordinal) {
  zc::Maybe<identity::DefId> noOwner;
  zc::Maybe<ast::NodeId> noNode;
  zc::Maybe<identity::SourceSpan> noSpan;
  zc::Vector<uint32_t> noPath;
  zc::Maybe<identity::Sha256Digest> noExpected;
  zc::Maybe<identity::Sha256Digest> noActual;
  return signature::CheckerVerificationFailure(signature::CheckerInvariantFact{
      kind, signature::CheckerInvariantStage::Coherence, module, zc::mv(noOwner), zc::mv(noNode),
      zc::mv(noSpan), zc::mv(noPath), zc::mv(noExpected), zc::mv(noActual), ordinal});
}

CoherenceBuildResult reject(signature::CheckerVerificationFailure&& failure) {
  zc::Vector<signature::CheckerVerificationFailure> failures;
  failures.add(zc::mv(failure));
  return CoherenceInvariantRejected{zc::mv(failures)};
}

zc::Maybe<zc::Array<uint8_t>> moduleRevisionRecord(
    identity::ModuleId module, const module_interface::ModuleInterfaceRevision& revision,
    const CheckerIdentityAuthority& identities) {
  auto entry = identities.module(module);
  if (entry == zc::none) return zc::none;
  identity::CanonicalEncoder encoder;
  ZC_IF_SOME(value, entry) { value.key().encode(encoder); }
  encoder.encodeDigest(revision.digest());
  return encoder.finish();
}

bool definitionOwnedBy(identity::DefId definition, identity::ModuleId module,
                       const CheckerIdentityAuthority& identities) {
  auto definitionEntry = identities.definition(definition);
  auto moduleEntry = identities.module(module);
  if (definitionEntry == zc::none || moduleEntry == zc::none) return false;
  ZC_IF_SOME(definitionValue, definitionEntry) {
    ZC_IF_SOME(moduleValue, moduleEntry) {
      const auto left = definitionValue.record().module().encode();
      const auto right = moduleValue.key().encode();
      return sameBytes(left.asPtr(), right.asPtr());
    }
  }
  return false;
}

bool implIsLocal(const signature::ImplHead& head, identity::ModuleId module,
                 const CheckerIdentityAuthority& identities) {
  if (definitionOwnedBy(signature::SignatureFactsCanonicalCodec::implPatternInterface(head.pattern),
                        module, identities)) {
    return true;
  }
  const auto& value = head.head.variant();
  return value.is<signature::NominalTypeHead>() &&
         definitionOwnedBy(value.get<signature::NominalTypeHead>().definition, module, identities);
}

bool implementationOwnedBy(identity::ImplId implementation, identity::ModuleId module,
                           const CheckerIdentityAuthority& identities) {
  auto implementationEntry = identities.implementation(implementation);
  auto moduleEntry = identities.module(module);
  if (implementationEntry == zc::none || moduleEntry == zc::none) return false;
  ZC_IF_SOME(value, implementationEntry) {
    ZC_IF_SOME(owner, moduleEntry) {
      const auto left = value.record().module().encode();
      const auto right = owner.key().encode();
      return sameBytes(left.asPtr(), right.asPtr());
    }
  }
  return false;
}

zc::Maybe<identity::ImplId> explicitMarkerImpl(const signature::MarkerFact& fact) {
  const auto& evidence = fact.evidence.variant();
  if (evidence.is<signature::ExplicitMarkerEvidence>()) {
    return evidence.get<signature::ExplicitMarkerEvidence>().impl;
  }
  return zc::none;
}

zc::Array<uint8_t> cloneBytes(zc::ArrayPtr<const uint8_t> bytes) {
  zc::Vector<uint8_t> result(bytes.size());
  result.addAll(bytes);
  return result.releaseAsArray();
}

struct EncodedModule final {
  ModuleInterfaceRevisionEntry entry;
  zc::Array<uint8_t> record;
};

struct EncodedImpl final {
  identity::ModuleId module;
  signature::ImplHead fact;
  zc::Array<uint8_t> orderingKey;
  zc::Array<uint8_t> record;
};

struct EncodedMarker final {
  identity::ModuleId module;
  signature::MarkerFact fact;
  zc::Array<uint8_t> record;
};

struct EncodedFailure final {
  CoherenceFailureRef failure;
  zc::Array<uint8_t> orderingKey;
};

template <typename Value, typename Key>
void insertSorted(zc::Vector<Value>& values, Value&& value, Key key) {
  size_t insertion = values.size();
  values.add(zc::mv(value));
  while (insertion > 0 && lessBytes(key(values[insertion]), key(values[insertion - 1]))) {
    auto current = zc::mv(values[insertion]);
    values[insertion] = zc::mv(values[insertion - 1]);
    values[insertion - 1] = zc::mv(current);
    --insertion;
  }
}

zc::Vector<checked::CheckerDisplayArgument> failureArguments(identity::DefId interface,
                                                             identity::SemanticTypeId selfType) {
  zc::Vector<checked::CheckerDisplayArgument> arguments;
  arguments.add(checked::CheckerDisplayArgument(checked::DefinitionDisplayArg{interface}));
  zc::Maybe<identity::SemanticIdentifier> noAlias;
  arguments.add(
      checked::CheckerDisplayArgument(checked::TypeDisplayArg{selfType, zc::mv(noAlias)}));
  return arguments;
}

zc::Vector<checked::CheckerNoteRef> conflictNotes(const identity::SourceSpan& previousSpan) {
  zc::Vector<checked::CheckerNoteRef> notes;
  zc::Vector<checked::CheckerDisplayArgument> noArguments;
  zc::Maybe<identity::DefId> noCause;
  notes.add(checked::CheckerNoteRef{checked::CheckerNoteId::PreviousImplHere(),
                                    previousSpan.clone(), zc::mv(noArguments), zc::mv(noCause)});
  return notes;
}

zc::Maybe<zc::Array<uint8_t>> failureOrderingKey(const CoherenceFailureRef& failure,
                                                 const CheckerIdentityAuthority& identities) {
  auto implementation = identities.implementation(failure.primaryImpl);
  if (implementation == zc::none) return zc::none;
  identity::CanonicalEncoder encoder;
  ZC_IF_SOME(record, implementation) { record.record().module().encode(encoder); }
  failure.primarySpan.encode(encoder);
  encoder.encodeUint32(static_cast<uint32_t>(failure.diagnostic.diagnosticId()));
  ZC_IF_SOME(key, implementation) { key.key().encode(encoder); }
  ZC_IF_SOME(related, failure.relatedImpl) {
    auto relatedKey = identities.implementation(related);
    if (relatedKey == zc::none) return zc::none;
    encoder.encodeSome();
    ZC_IF_SOME(key, relatedKey) { key.key().encode(encoder); }
  } else {
    encoder.encodeNone();
  }
  return encoder.finish();
}

zc::Maybe<EncodedFailure> ordinaryFailure(checked::CheckerErrorId diagnostic,
                                          const signature::ImplHead& primary,
                                          zc::Maybe<identity::ImplId>&& related,
                                          zc::Vector<checked::CheckerNoteRef>&& notes,
                                          CoherenceFailureProducer producer,
                                          const CheckerIdentityAuthority& identities) {
  auto interface = signature::SignatureFactsCanonicalCodec::implPatternInterface(primary.pattern);
  CoherenceFailureRef failure{diagnostic,
                              primary.impl,
                              zc::mv(related),
                              primary.declarationSpan.clone(),
                              failureArguments(interface, primary.selfType),
                              zc::mv(notes),
                              producer};
  auto orderingKey = failureOrderingKey(failure, identities);
  ZC_IF_SOME(key, orderingKey) { return EncodedFailure{zc::mv(failure), zc::mv(key)}; }
  return zc::none;
}

zc::Maybe<EncodedFailure> markerConflictFailure(const signature::MarkerFact& primary,
                                                identity::ImplId primaryImpl,
                                                const signature::MarkerFact& related,
                                                identity::ImplId relatedImpl,
                                                const CheckerIdentityAuthority& identities) {
  if (primary.declarationSpan == zc::none || related.declarationSpan == zc::none) return zc::none;
  ZC_IF_SOME(primarySpan, primary.declarationSpan) {
    ZC_IF_SOME(relatedSpan, related.declarationSpan) {
      zc::Maybe<identity::ImplId> relatedValue = relatedImpl;
      CoherenceFailureRef failure{checked::CheckerErrorId::ConflictingImpl(),
                                  primaryImpl,
                                  zc::mv(relatedValue),
                                  primarySpan.clone(),
                                  failureArguments(primary.key.marker, primary.key.subject),
                                  conflictNotes(relatedSpan),
                                  CoherenceFailureProducer::Coherence};
      auto orderingKey = failureOrderingKey(failure, identities);
      ZC_IF_SOME(key, orderingKey) { return EncodedFailure{zc::mv(failure), zc::mv(key)}; }
    }
  }
  return zc::none;
}

}  // namespace

struct CoherenceModuleInput::Impl final {
  Impl(identity::ModuleId module, module_interface::ModuleInterfaceRevision interfaceRevision,
       signature::MarkerPolicyRegistryRevision markerPolicyRegistryRevision,
       zc::Vector<signature::ImplHead>&& implHeads,
       zc::Vector<zc::Array<uint8_t>>&& implHeadRecords,
       zc::Vector<signature::MarkerFact>&& markerFacts,
       zc::Vector<zc::Array<uint8_t>>&& markerFactRecords)
      : module(module),
        interfaceRevision(interfaceRevision),
        markerPolicyRegistryRevision(markerPolicyRegistryRevision),
        implHeads(zc::mv(implHeads)),
        implHeadRecords(zc::mv(implHeadRecords)),
        markerFacts(zc::mv(markerFacts)),
        markerFactRecords(zc::mv(markerFactRecords)) {}

  identity::ModuleId module;
  module_interface::ModuleInterfaceRevision interfaceRevision;
  signature::MarkerPolicyRegistryRevision markerPolicyRegistryRevision;
  zc::Vector<signature::ImplHead> implHeads;
  zc::Vector<zc::Array<uint8_t>> implHeadRecords;
  zc::Vector<signature::MarkerFact> markerFacts;
  zc::Vector<zc::Array<uint8_t>> markerFactRecords;
};

CoherenceModuleInput::CoherenceModuleInput(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}
CoherenceModuleInput::~CoherenceModuleInput() noexcept(false) = default;
CoherenceModuleInput::CoherenceModuleInput(CoherenceModuleInput&&) noexcept = default;
CoherenceModuleInput& CoherenceModuleInput::operator=(CoherenceModuleInput&&) noexcept = default;

CoherenceModuleInput CoherenceModuleInput::publish(
    identity::ModuleId module, module_interface::ModuleInterfaceRevision interfaceRevision,
    signature::MarkerPolicyRegistryRevision markerPolicyRegistryRevision,
    zc::Vector<signature::ImplHead>&& implHeads, zc::Vector<zc::Array<uint8_t>>&& implHeadRecords,
    zc::Vector<signature::MarkerFact>&& markerFacts,
    zc::Vector<zc::Array<uint8_t>>&& markerFactRecords) {
  return CoherenceModuleInput(
      zc::heap<Impl>(module, interfaceRevision, markerPolicyRegistryRevision, zc::mv(implHeads),
                     zc::mv(implHeadRecords), zc::mv(markerFacts), zc::mv(markerFactRecords)));
}

identity::ModuleId CoherenceModuleInput::module() const noexcept { return impl->module; }
const module_interface::ModuleInterfaceRevision& CoherenceModuleInput::interfaceRevision()
    const noexcept {
  return impl->interfaceRevision;
}
const signature::MarkerPolicyRegistryRevision& CoherenceModuleInput::markerPolicyRegistryRevision()
    const noexcept {
  return impl->markerPolicyRegistryRevision;
}
zc::ArrayPtr<const signature::ImplHead> CoherenceModuleInput::implHeads() const noexcept {
  return impl->implHeads.asPtr();
}
zc::ArrayPtr<const signature::MarkerFact> CoherenceModuleInput::markerFacts() const noexcept {
  return impl->markerFacts.asPtr();
}
zc::ArrayPtr<const zc::Array<uint8_t>> CoherenceModuleInput::implHeadRecords() const noexcept {
  return impl->implHeadRecords.asPtr();
}
zc::ArrayPtr<const zc::Array<uint8_t>> CoherenceModuleInput::markerFactRecords() const noexcept {
  return impl->markerFactRecords.asPtr();
}

struct FrozenCoherenceView::Impl final {
  Impl(identity::SemanticContextBrand context,
       const identity::ContextFingerprint& fingerprint,
       signature::MarkerPolicyRegistryRevision markerPolicyRegistryRevision,
       cross_module::CoherenceViewRevision revision,
       zc::Vector<ModuleInterfaceRevisionEntry>&& modules, zc::Vector<signature::ImplHead>&& impls,
       zc::Vector<signature::MarkerFact>&& markers)
      : context(context),
        fingerprint(fingerprint),
        markerPolicyRegistryRevision(markerPolicyRegistryRevision),
        revision(zc::mv(revision)),
        modules(zc::mv(modules)),
        impls(zc::mv(impls)),
        markers(zc::mv(markers)) {}

  identity::SemanticContextBrand context;
  identity::ContextFingerprint fingerprint;
  signature::MarkerPolicyRegistryRevision markerPolicyRegistryRevision;
  cross_module::CoherenceViewRevision revision;
  zc::Vector<ModuleInterfaceRevisionEntry> modules;
  zc::Vector<signature::ImplHead> impls;
  zc::Vector<signature::MarkerFact> markers;
};

FrozenCoherenceView::FrozenCoherenceView(zc::Own<Impl>&& value) noexcept : impl(zc::mv(value)) {}
FrozenCoherenceView::~FrozenCoherenceView() noexcept(false) = default;
FrozenCoherenceView::FrozenCoherenceView(FrozenCoherenceView&&) noexcept = default;
FrozenCoherenceView& FrozenCoherenceView::operator=(FrozenCoherenceView&&) noexcept = default;

identity::SemanticContextBrand FrozenCoherenceView::semanticContext() const noexcept {
  return impl->context;
}
const identity::ContextFingerprint& FrozenCoherenceView::contextFingerprint()
    const noexcept {
  return impl->fingerprint;
}
const signature::MarkerPolicyRegistryRevision& FrozenCoherenceView::markerPolicyRegistryRevision()
    const noexcept {
  return impl->markerPolicyRegistryRevision;
}
const cross_module::CoherenceViewRevision& FrozenCoherenceView::revision() const noexcept {
  return impl->revision;
}
zc::ArrayPtr<const ModuleInterfaceRevisionEntry> FrozenCoherenceView::moduleInterfaceRevisions()
    const noexcept {
  return impl->modules.asPtr();
}
zc::ArrayPtr<const signature::ImplHead> FrozenCoherenceView::implHeads() const noexcept {
  return impl->impls.asPtr();
}
zc::ArrayPtr<const signature::MarkerFact> FrozenCoherenceView::markerFacts() const noexcept {
  return impl->markers.asPtr();
}
zc::Maybe<const signature::ImplHead&> FrozenCoherenceView::implementation(
    identity::ImplId implementation) const noexcept {
  for (const auto& fact : impl->impls) {
    if (fact.impl == implementation) return fact;
  }
  return zc::none;
}
zc::Maybe<const signature::MarkerFact&> FrozenCoherenceView::marker(
    const signature::MarkerFactKey& key) const noexcept {
  for (const auto& fact : impl->markers) {
    if (fact.key.marker == key.marker && fact.key.subject == key.subject) return fact;
  }
  return zc::none;
}

CoherenceBuildResult CoherenceVerifier::verify(
    CoherenceCandidate&& candidate, const signature::VerifiedMarkerPolicyRegistry& markerPolicies,
    const CheckerIdentityAuthority& identities) {
  if (candidate.modules.size() == 0 || candidate.modules.size() != identities.modules().size() ||
      identities.semanticContext() != candidate.semanticContext ||
      markerPolicies.semanticContext() != candidate.semanticContext ||
      markerPolicies.contextFingerprint().digest() != candidate.contextFingerprint.digest() ||
      markerPolicies.revision().digest() != candidate.markerPolicyRegistryRevision.digest()) {
    return reject(
        invariant(signature::CheckerInvariantKind::InputReceiptMismatch, identity::ModuleId(), 0));
  }

  zc::Vector<EncodedModule> modules(candidate.modules.size());
  zc::Vector<EncodedImpl> impls;
  zc::Vector<EncodedMarker> markers;
  uint32_t ordinal = 0;
  for (const auto& module : candidate.modules) {
    if (identities.module(module.module()) == zc::none ||
        !sameDigest(module.markerPolicyRegistryRevision().digest(),
                    candidate.markerPolicyRegistryRevision.digest()) ||
        module.implHeads().size() != module.implHeadRecords().size() ||
        module.markerFacts().size() != module.markerFactRecords().size()) {
      return reject(invariant(signature::CheckerInvariantKind::InputReceiptMismatch,
                              module.module(), ordinal));
    }
    auto moduleRecord =
        moduleRevisionRecord(module.module(), module.interfaceRevision(), identities);
    if (moduleRecord == zc::none) {
      return reject(invariant(signature::CheckerInvariantKind::CanonicalCodecMismatch,
                              module.module(), ordinal));
    }
    ZC_IF_SOME(record, moduleRecord) {
      insertSorted(
          modules,
          EncodedModule{ModuleInterfaceRevisionEntry{module.module(), module.interfaceRevision()},
                        zc::mv(record)},
          [](const EncodedModule& value) { return value.record.asPtr(); });
    }
    for (size_t index = 0; index < module.implHeads().size(); ++index) {
      const auto& head = module.implHeads()[index];
      auto key = identities.implementation(head.impl);
      const auto record = module.implHeadRecords()[index].asPtr();
      if (key == zc::none || record.size() == 0 ||
          !implementationOwnedBy(head.impl, module.module(), identities)) {
        return reject(invariant(signature::CheckerInvariantKind::CanonicalCodecMismatch,
                                module.module(), ordinal));
      }
      ZC_IF_SOME(keyValue, key) {
        insertSorted(
            impls,
            EncodedImpl{module.module(), head.clone(), keyValue.key().encode(), cloneBytes(record)},
            [](const EncodedImpl& value) { return value.orderingKey.asPtr(); });
      }
    }
    for (size_t index = 0; index < module.markerFacts().size(); ++index) {
      const auto& fact = module.markerFacts()[index];
      const auto record = module.markerFactRecords()[index].asPtr();
      auto implementation = explicitMarkerImpl(fact);
      if (record.size() == 0 || implementation == zc::none || fact.declarationSpan == zc::none) {
        return reject(
            invariant(signature::CheckerInvariantKind::InvalidFact, module.module(), ordinal));
      }
      ZC_IF_SOME(value, implementation) {
        if (!implementationOwnedBy(value, module.module(), identities)) {
          return reject(
              invariant(signature::CheckerInvariantKind::InvalidFact, module.module(), ordinal));
        }
      }
      insertSorted(markers, EncodedMarker{module.module(), fact.clone(), cloneBytes(record)},
                   [](const EncodedMarker& item) { return item.record.asPtr(); });
    }
    ++ordinal;
  }

  for (size_t index = 1; index < modules.size(); ++index) {
    if (modules[index - 1].entry.module == modules[index].entry.module ||
        !lessBytes(modules[index - 1].record.asPtr(), modules[index].record.asPtr())) {
      return reject(invariant(signature::CheckerInvariantKind::AdditionalFact,
                              modules[index].entry.module, static_cast<uint32_t>(index)));
    }
  }

  zc::Vector<EncodedFailure> sourceFailures;
  for (size_t index = 0; index < impls.size(); ++index) {
    const auto& current = impls[index];
    if (!implIsLocal(current.fact, current.module, identities)) {
      zc::Maybe<identity::ImplId> noRelated;
      zc::Vector<checked::CheckerNoteRef> noNotes;
      auto failure =
          ordinaryFailure(checked::CheckerErrorId::OrphanImpl(), current.fact, zc::mv(noRelated),
                          zc::mv(noNotes), CoherenceFailureProducer::Orphan, identities);
      if (failure == zc::none) {
        return reject(invariant(signature::CheckerInvariantKind::InvalidFact, current.module,
                                static_cast<uint32_t>(index)));
      }
      ZC_IF_SOME(value, failure) {
        insertSorted(sourceFailures, zc::mv(value),
                     [](const EncodedFailure& item) { return item.orderingKey.asPtr(); });
      }
    }
    for (size_t previous = 0; previous < index; ++previous) {
      const auto& earlier = impls[previous];
      if (current.fact.impl == earlier.fact.impl) {
        return reject(invariant(signature::CheckerInvariantKind::AdditionalFact, current.module,
                                static_cast<uint32_t>(index)));
      }
      if (signature::SignatureFactsCanonicalCodec::implPatternInterface(current.fact.pattern) !=
          signature::SignatureFactsCanonicalCodec::implPatternInterface(earlier.fact.pattern)) {
        continue;
      }
      auto overlap = signature::SignatureFactsCanonicalCodec::implPatternsOverlap(
          earlier.fact.pattern, current.fact.pattern, identities);
      if (overlap == zc::none) {
        return reject(invariant(signature::CheckerInvariantKind::CanonicalCodecMismatch,
                                current.module, static_cast<uint32_t>(index)));
      }
      ZC_IF_SOME(value, overlap) {
        if (!value) continue;
        zc::Maybe<identity::ImplId> previousImplementation = earlier.fact.impl;
        auto failure = ordinaryFailure(checked::CheckerErrorId::ConflictingImpl(), current.fact,
                                       zc::mv(previousImplementation),
                                       conflictNotes(earlier.fact.declarationSpan),
                                       CoherenceFailureProducer::Coherence, identities);
        if (failure == zc::none) {
          return reject(invariant(signature::CheckerInvariantKind::InvalidFact, current.module,
                                  static_cast<uint32_t>(index)));
        }
        ZC_IF_SOME(item, failure) {
          insertSorted(sourceFailures, zc::mv(item),
                       [](const EncodedFailure& entry) { return entry.orderingKey.asPtr(); });
        }
      }
    }
  }

  for (size_t index = 0; index < markers.size(); ++index) {
    const auto& current = markers[index];
    const auto implementation = explicitMarkerImpl(current.fact);
    if (implementation == zc::none) {
      return reject(invariant(signature::CheckerInvariantKind::InvalidFact, current.module,
                              static_cast<uint32_t>(index)));
    }
    for (size_t previous = 0; previous < index; ++previous) {
      const auto& earlier = markers[previous];
      if (earlier.fact.key.marker != current.fact.key.marker ||
          earlier.fact.key.subject != current.fact.key.subject) {
        continue;
      }
      auto previousImplementation = explicitMarkerImpl(earlier.fact);
      if (previousImplementation == zc::none) {
        return reject(invariant(signature::CheckerInvariantKind::InvalidFact, current.module,
                                static_cast<uint32_t>(index)));
      }
      zc::Maybe<EncodedFailure> failure;
      ZC_IF_SOME(primary, implementation) {
        ZC_IF_SOME(related, previousImplementation) {
          failure = markerConflictFailure(current.fact, primary, earlier.fact, related, identities);
        }
      }
      if (failure == zc::none) {
        return reject(invariant(signature::CheckerInvariantKind::InvalidFact, current.module,
                                static_cast<uint32_t>(index)));
      }
      ZC_IF_SOME(item, failure) {
        insertSorted(sourceFailures, zc::mv(item),
                     [](const EncodedFailure& entry) { return entry.orderingKey.asPtr(); });
      }
    }
  }

  if (!sourceFailures.empty()) {
    zc::Vector<CoherenceFailureRef> failures(sourceFailures.size());
    for (auto& failure : sourceFailures) { failures.add(zc::mv(failure.failure)); }
    return CoherenceSourceRejected{zc::mv(failures), zc::Vector<signature::SignatureAdvisoryRef>()};
  }

  zc::Vector<zc::ArrayPtr<const uint8_t>> moduleRecords(modules.size());
  zc::Vector<zc::ArrayPtr<const uint8_t>> implRecords(impls.size());
  zc::Vector<zc::ArrayPtr<const uint8_t>> markerRecords(markers.size());
  for (const auto& module : modules) { moduleRecords.add(module.record.asPtr()); }
  for (const auto& implementation : impls) { implRecords.add(implementation.record.asPtr()); }
  for (const auto& marker : markers) { markerRecords.add(marker.record.asPtr()); }
  auto revision = cross_module::CoherenceViewRevision::computeFramed(
      candidate.contextFingerprint.digest(), candidate.markerPolicyRegistryRevision.digest(),
      moduleRecords.asPtr(), implRecords.asPtr(), markerRecords.asPtr());
  if (revision == zc::none) {
    return reject(invariant(signature::CheckerInvariantKind::CanonicalCodecMismatch,
                            modules[0].entry.module, 0));
  }

  zc::Vector<ModuleInterfaceRevisionEntry> publishedModules(modules.size());
  zc::Vector<signature::ImplHead> publishedImpls(impls.size());
  zc::Vector<signature::MarkerFact> publishedMarkers(markers.size());
  for (auto& module : modules) { publishedModules.add(zc::mv(module.entry)); }
  for (auto& implementation : impls) { publishedImpls.add(zc::mv(implementation.fact)); }
  for (auto& marker : markers) { publishedMarkers.add(zc::mv(marker.fact)); }
  ZC_IF_SOME(value, revision) {
    FrozenCoherenceView view(zc::heap<FrozenCoherenceView::Impl>(
        candidate.semanticContext, candidate.contextFingerprint,
        candidate.markerPolicyRegistryRevision, zc::mv(value), zc::mv(publishedModules),
        zc::mv(publishedImpls), zc::mv(publishedMarkers)));
    return CoherenceFrozen{zc::mv(view), zc::Vector<signature::SignatureAdvisoryRef>()};
  }
  ZC_UNREACHABLE
}

}  // namespace zomlang::compiler::checker::coherence
