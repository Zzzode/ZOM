// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/checker/facts/checked-facts-repository.h"

namespace zomlang::compiler::checker::checked {
namespace {

bool sameRevision(const CheckedFactsRevision& left, const CheckedFactsRevision& right) noexcept {
  const auto leftBytes = left.digest().bytes();
  const auto rightBytes = right.digest().bytes();
  if (leftBytes.size() != rightBytes.size()) return false;
  for (size_t index = 0; index < leftBytes.size(); ++index) {
    if (leftBytes[index] != rightBytes[index]) return false;
  }
  return true;
}

}  // namespace

struct CheckedFactsRepository::Impl final {
  explicit Impl(identity::SemanticContextBrand context) : context(context) {}

  identity::SemanticContextBrand context;
  zc::Vector<VerifiedCheckedFacts> entries;
};

CheckedFactsRepository::CheckedFactsRepository(identity::SemanticContextBrand semanticContext)
    : impl(zc::heap<Impl>(semanticContext)) {}
CheckedFactsRepository::~CheckedFactsRepository() noexcept(false) = default;
CheckedFactsRepository::CheckedFactsRepository(CheckedFactsRepository&&) noexcept = default;
CheckedFactsRepository& CheckedFactsRepository::operator=(CheckedFactsRepository&&) noexcept =
    default;

identity::SemanticContextBrand CheckedFactsRepository::semanticContext() const noexcept {
  return impl->context;
}
size_t CheckedFactsRepository::size() const noexcept { return impl->entries.size(); }

CheckedFactsAdoptionResult CheckedFactsRepository::adopt(VerifiedCheckedFacts&& facts) {
  if (facts.semanticContext() != impl->context ||
      facts.substitutionStore().semanticContext() != impl->context ||
      facts.witnessStore().semanticContext() != impl->context) {
    return CheckedFactsRepositoryIssue::ForeignContext;
  }
  for (const auto& existing : impl->entries) {
    if (existing.module() == facts.module() &&
        sameRevision(existing.revision(), facts.revision())) {
      return CheckedFactsRepositoryIssue::DuplicateEvidence;
    }
  }
  impl->entries.add(zc::mv(facts));
  const auto& stored = impl->entries.back();
  return CheckedEvidenceLease(CheckedEvidenceKey{stored.module(), stored.revision()}, impl->context,
                              stored.substitutionStore().issuer(), stored.witnessStore().issuer());
}

zc::Maybe<CheckedEvidenceLease> CheckedFactsRepository::lease(
    identity::ModuleId module, const CheckedFactsRevision& revision) const {
  for (const auto& facts : impl->entries) {
    if (facts.module() != module || !sameRevision(facts.revision(), revision)) continue;
    return CheckedEvidenceLease(CheckedEvidenceKey{module, facts.revision()}, impl->context,
                                facts.substitutionStore().issuer(), facts.witnessStore().issuer());
  }
  return zc::none;
}

zc::Maybe<const VerifiedCheckedFacts&> CheckedFactsRepository::lookup(
    const CheckedEvidenceLease& lease) const noexcept {
  if (lease.session != impl->context) return zc::none;
  for (const auto& facts : impl->entries) {
    if (facts.module() != lease.key.module || !sameRevision(facts.revision(), lease.key.revision) ||
        facts.semanticContext() != lease.session ||
        facts.substitutionStore().issuer() != lease.substitutionIssuer ||
        facts.witnessStore().issuer() != lease.witnessIssuer) {
      continue;
    }
    return facts;
  }
  return zc::none;
}

zc::Maybe<const SubstitutionData&> CheckedFactsRepository::substitution(
    const CheckedEvidenceLease& lease, CanonicalSubstitutionId id) const noexcept {
  ZC_IF_SOME(facts, lookup(lease)) {
    const auto& store = facts.substitutionStore();
    if (!store.contains(id)) return zc::none;
    for (uint32_t index = 0; index < store.records().size(); ++index) {
      auto candidate = store.idAt(index);
      if (candidate == zc::none) return zc::none;
      ZC_IF_SOME(value, candidate) {
        if (value == id) return store.records()[index].value;
      }
    }
  }
  return zc::none;
}

zc::Maybe<const WitnessArgumentsData&> CheckedFactsRepository::witnesses(
    const CheckedEvidenceLease& lease, WitnessArgumentsId id) const noexcept {
  ZC_IF_SOME(facts, lookup(lease)) {
    const auto& store = facts.witnessStore();
    if (!store.contains(id)) return zc::none;
    for (uint32_t index = 0; index < store.records().size(); ++index) {
      auto candidate = store.idAt(index);
      if (candidate == zc::none) return zc::none;
      ZC_IF_SOME(value, candidate) {
        if (value == id) return store.records()[index].value;
      }
    }
  }
  return zc::none;
}

}  // namespace zomlang::compiler::checker::checked
