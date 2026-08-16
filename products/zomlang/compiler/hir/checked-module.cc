// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/hir/checked-module.h"

#include "zomlang/compiler/driver/core/query.h"
#include "zomlang/compiler/identity/definition-key.h"
#include "zomlang/compiler/identity/source-key.h"
#include "zomlang/compiler/ownership/surface-admission.h"

namespace zomlang::compiler::hir {
namespace {

bool lessBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) noexcept {
  const size_t shared = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < shared; ++index) {
    if (left[index] != right[index]) return left[index] < right[index];
  }
  return left.size() < right.size();
}

identity::IdentityInvariant invalidIdentity(identity::IdentityAllocationPhase phase,
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

ir::IrOperationResult<VerifiedCheckedModule> rejectCheckedModule(
    ir::IrFailureKind kind, identity::ModuleId module,
    const checker::CheckerIdentityAuthority& identities, uint32_t ordinal,
    zc::Vector<uint32_t>&& fieldPath = zc::Vector<uint32_t>()) {
  AuthorityIdentityResolver resolver(identities);
  auto fallback = ir::IrFailureFallbackContext::from(ir::IrFailurePhase::CheckedModuleAssembly,
                                                     ir::IrFailureOwner::module(module));
  ZC_IREQUIRE(fallback != zc::none, "checked-module failure fallback must be legal");
  zc::Maybe<ir::IrFailureSite> noSite;
  zc::Maybe<identity::SourceSpan> noSpan;
  auto descriptor = ir::IrFailureDescriptor::decoded(
      ir::IrRejectedBranch::IrInvariantRejected, ir::IrFailurePhase::CheckedModuleAssembly, kind,
      ir::IrFailureOwner::module(module), zc::mv(noSite), ir::IrFailureDetail::none(),
      zc::mv(noSpan), zc::mv(fieldPath), ordinal);
  ZC_IF_SOME(fallbackValue, fallback) {
    auto admitted = ir::IrFailureFactory::admit(zc::mv(descriptor), fallbackValue, resolver);
    if (admitted.is<ir::IdentityRejectedIrFailureDescriptor>()) {
      zc::Vector<identity::IdentityInvariant> failures;
      failures.add(zc::mv(admitted).get<ir::IdentityRejectedIrFailureDescriptor>().failure);
      auto sorted = ir::SortedIdentityInvariantFacts::from(zc::mv(failures));
      ZC_IF_SOME(values, sorted) {
        return ir::IrOperationResult<VerifiedCheckedModule>::identityInvariantRejected(
            zc::mv(values));
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
      return ir::IrOperationResult<VerifiedCheckedModule>::irInvariantRejected(zc::mv(values));
    }
  }
  ZC_UNREACHABLE
}

ir::IrOperationResult<VerifiedCheckedModule> rejectBorrowEvidence(
    const driver::borrow_evidence::BorrowEvidenceInvariantRejected& rejected,
    identity::ModuleId module, const checker::CheckerIdentityAuthority& identities) {
  if (rejected.failures.empty()) {
    return rejectCheckedModule(ir::IrFailureKind::InvalidFact, module, identities, 0);
  }

  AuthorityIdentityResolver resolver(identities);
  auto fallback = ir::IrFailureFallbackContext::from(ir::IrFailurePhase::CheckedModuleAssembly,
                                                     ir::IrFailureOwner::module(module));
  ZC_IREQUIRE(fallback != zc::none, "borrow-evidence failure fallback must be legal");
  zc::Vector<ir::IrFailureFact> failures;
  ZC_IF_SOME(fallbackValue, fallback) {
    for (const auto& failure : rejected.failures) {
      zc::Maybe<ir::IrFailureSite> noSite;
      zc::Maybe<identity::SourceSpan> noSpan;
      zc::Vector<uint32_t> fieldPath;
      for (const auto field : failure.structuralFieldPath) { fieldPath.add(field); }
      auto descriptor = ir::IrFailureDescriptor::decoded(
          ir::IrRejectedBranch::IrInvariantRejected, ir::IrFailurePhase::CheckedModuleAssembly,
          failure.kind, ir::IrFailureOwner::module(module), zc::mv(noSite),
          ir::IrFailureDetail::none(), zc::mv(noSpan), zc::mv(fieldPath), failure.traversalOrdinal);
      auto admitted = ir::IrFailureFactory::admit(zc::mv(descriptor), fallbackValue, resolver);
      if (admitted.is<ir::IdentityRejectedIrFailureDescriptor>()) {
        zc::Vector<identity::IdentityInvariant> identityFailures;
        identityFailures.add(
            zc::mv(admitted).get<ir::IdentityRejectedIrFailureDescriptor>().failure);
        auto sorted = ir::SortedIdentityInvariantFacts::from(zc::mv(identityFailures));
        ZC_IF_SOME(values, sorted) {
          return ir::IrOperationResult<VerifiedCheckedModule>::identityInvariantRejected(
              zc::mv(values));
        }
        ZC_UNREACHABLE
      }
      if (admitted.is<ir::AcceptedIrFailureDescriptor>()) {
        failures.add(zc::mv(admitted).get<ir::AcceptedIrFailureDescriptor>().fact);
      } else {
        failures.add(zc::mv(admitted).get<ir::FallbackIrFailureDescriptor>().fact);
      }
    }
  }
  auto sorted = ir::SortedIrInvariantFailureFacts::from(zc::mv(failures));
  ZC_IF_SOME(values, sorted) {
    return ir::IrOperationResult<VerifiedCheckedModule>::irInvariantRejected(zc::mv(values));
  }
  ZC_UNREACHABLE
}

struct ExpectedInterface final {
  identity::ModuleId module;
  identity::Sha256Digest bindingRevision;
  bool matched;
};

bool appendExpectedInterface(zc::Vector<ExpectedInterface>& expected,
                             const binder::MaterializedDependencyExportSurface& surface) {
  for (const auto& entry : expected) {
    if (entry.module != surface.module) continue;
    return entry.bindingRevision == surface.surface.revision().digest();
  }
  expected.add(ExpectedInterface{surface.module, surface.surface.revision().digest(), false});
  return true;
}

bool validateImportedInterfaces(const CheckedModuleBuildInput& input,
                                zc::Vector<ModuleInterfaceLineage>& output) {
  zc::Vector<ExpectedInterface> expected;
  for (const auto& surface : input.boundModule.dependencySurfaces()) {
    if (!appendExpectedInterface(expected, surface)) return false;
  }
  ZC_IF_SOME(prelude, input.boundModule.preludeSurface()) {
    if (!appendExpectedInterface(expected, prelude)) return false;
  }
  if (expected.size() != input.importedSignatures.modules().size()) return false;

  zc::Array<uint8_t> previousKey;
  bool hasPrevious = false;
  for (const auto& imported : input.importedSignatures.modules()) {
    if (imported.sourceModule() == input.boundModule.module()) return false;
    auto key = input.identities.module(imported.sourceModule());
    if (key == zc::none) return false;
    zc::Array<uint8_t> currentKey;
    ZC_IF_SOME(value, key) { currentKey = value.key().encode(); }
    if (hasPrevious && !lessBytes(previousKey.asPtr(), currentKey.asPtr())) return false;
    previousKey = zc::mv(currentKey);
    hasPrevious = true;

    bool found = false;
    const bool coreSource = imported.interfaceRevision()
                                .variant()
                                .is<module_interface::ToolchainCoreImportedInterfaceRevision>();
    for (auto& entry : expected) {
      if (entry.module != imported.sourceModule()) continue;
      const auto& bindingRevision = imported.bindingSurfaceRevision().variant();
      if (entry.matched ||
          (coreSource &&
           !bindingRevision.is<module_interface::ToolchainCoreImportedBindingSurfaceRevision>()) ||
          (!coreSource &&
           (!bindingRevision.is<module_interface::UserImportedBindingSurfaceRevision>() ||
            entry.bindingRevision !=
                bindingRevision.get<module_interface::UserImportedBindingSurfaceRevision>()
                    .value.digest()))) {
        return false;
      }
      entry.matched = true;
      found = true;
      break;
    }
    if (!found) return false;

    zc::Maybe<const driver::VerifiedModuleInterface&> selectedUser;
    zc::Maybe<const driver::core_library_query::VerifiedCoreModuleInterface&> selectedCore;
    for (const auto& source : input.availableModuleInterfaces) {
      const auto& interfaceRevision = imported.interfaceRevision().variant();
      const auto& bindingSurfaceRevision = imported.bindingSurfaceRevision().variant();
      if (source.is<driver::UserVerifiedInterfaceSource>()) {
        const auto& interface = source.get<driver::UserVerifiedInterfaceSource>().interface;
        if (interface.module() != imported.sourceModule()) continue;
        if (coreSource || selectedUser != zc::none ||
            interface.semanticContext() != input.boundModule.semanticContext() ||
            interface.module() == input.boundModule.module() ||
            !interfaceRevision.is<module_interface::UserImportedInterfaceRevision>() ||
            interface.revision().digest() !=
                interfaceRevision.get<module_interface::UserImportedInterfaceRevision>()
                    .value.digest() ||
            !bindingSurfaceRevision.is<module_interface::UserImportedBindingSurfaceRevision>() ||
            interface.bindingSurface().revision().digest() !=
                bindingSurfaceRevision.get<module_interface::UserImportedBindingSurfaceRevision>()
                    .value.digest()) {
          return false;
        }
        selectedUser = interface;
        continue;
      }
      const auto& interface = source.get<driver::ToolchainCoreVerifiedInterfaceSource>().interface;
      if (interface.module() != imported.sourceModule()) continue;
      if (!coreSource || selectedCore != zc::none ||
          interface.context() != input.boundModule.semanticContext() ||
          interface.fingerprint().digest() != input.boundModule.semanticFingerprint().digest() ||
          !interfaceRevision.is<module_interface::ToolchainCoreImportedInterfaceRevision>() ||
          interface.record().revision().digest() !=
              interfaceRevision.get<module_interface::ToolchainCoreImportedInterfaceRevision>()
                  .value.digest() ||
          !bindingSurfaceRevision
               .is<module_interface::ToolchainCoreImportedBindingSurfaceRevision>() ||
          interface.record().bindingSurfaceRevision().digest() !=
              bindingSurfaceRevision
                  .get<module_interface::ToolchainCoreImportedBindingSurfaceRevision>()
                  .value.digest()) {
        return false;
      }
      for (const auto& signature : imported.lookupDefinitions()) {
        if (signature.payload.variant().is<checker::signature::CallableSignature>()) return false;
      }
      for (const auto& signature : imported.supportDefinitions()) {
        if (signature.payload.variant().is<checker::signature::CallableSignature>()) return false;
      }
      selectedCore = interface;
    }
    if (coreSource) {
      if (selectedCore == zc::none) return false;
      ZC_IF_SOME(interface, selectedCore) {
        output.add(ModuleInterfaceLineage{
            interface.module(), module_interface::ImportedInterfaceRevision(
                                    module_interface::ToolchainCoreImportedInterfaceRevision{
                                        interface.record().revision().clone()})});
      }
    } else {
      if (selectedUser == zc::none) return false;
      ZC_IF_SOME(interface, selectedUser) {
        output.add(ModuleInterfaceLineage{
            interface.module(),
            module_interface::ImportedInterfaceRevision(
                module_interface::UserImportedInterfaceRevision{interface.revision()})});
      }
    }
  }
  for (const auto& entry : expected) {
    if (!entry.matched) return false;
  }
  return true;
}

bool validateDispatchRevision(const CheckedModuleBuildInput& input,
                              const checker::checked::VerifiedCheckedFacts& checkedFacts) {
  auto moduleKey = input.identities.module(input.boundModule.module());
  if (moduleKey == zc::none) return false;
  zc::Vector<zc::ArrayPtr<const uint8_t>> records;
  for (const auto& fact : input.dispatchFacts.facts()) {
    records.add(fact.canonicalRecord.asPtr());
  }
  zc::Maybe<checker::dispatch::DispatchFactsRevision> expected;
  ZC_IF_SOME(key, moduleKey) {
    const auto encodedModule = key.key().encode();
    expected = checker::dispatch::DispatchFactsRevision::computeFramed(
        input.boundModule.semanticFingerprint().digest(), encodedModule.asPtr(),
        checkedFacts.revision(), records.asPtr());
  }
  if (expected == zc::none) return false;
  bool matches = false;
  ZC_IF_SOME(revision, expected) {
    matches = revision.digest() == input.dispatchFacts.revision().digest();
  }
  return matches;
}

}  // namespace

struct VerifiedCheckedModule::Impl final {
  Impl(ownership::OwnershipAdmittedBoundModule&& boundModule,
       const driver::VerifiedModuleInterface& ownModuleInterface,
       const checker::checked::CheckedFactsRepository& checkedRepository,
       const checker::checked::VerifiedCheckedFacts& checkedFacts,
       checker::checked::CheckedEvidenceLease&& checkedLease,
       const checker::dispatch::VerifiedDispatchFacts& dispatchFacts,
       const driver::borrow_evidence::BorrowEvidenceRevision& borrowEvidenceRevision,
       driver::borrow_evidence::VerifiedBorrowEvidenceLease&& borrowEvidenceLease,
       const driver::borrow_evidence::BorrowEvidenceRepository& borrowEvidenceRepository,
       checker::CheckerIdentityAuthority&& identities, const type::SemanticTypeStore& semanticTypes,
       ModuleInterfaceLineage&& ownInterface,
       zc::Vector<ModuleInterfaceLineage>&& visibleImportedInterfaces) noexcept
      : boundModuleValue(zc::mv(boundModule)),
        ownModuleInterfaceValue(ownModuleInterface),
        checkedRepositoryValue(checkedRepository),
        checkedFactsValue(checkedFacts),
        checkedLeaseValue(zc::mv(checkedLease)),
        dispatchFactsValue(dispatchFacts),
        borrowEvidenceRevisionValue(borrowEvidenceRevision),
        borrowEvidenceLeaseValue(zc::mv(borrowEvidenceLease)),
        borrowEvidenceRepositoryValue(borrowEvidenceRepository),
        identitiesValue(zc::mv(identities)),
        semanticTypesValue(semanticTypes),
        ownInterfaceValue(zc::mv(ownInterface)),
        visibleImportedInterfaceValues(zc::mv(visibleImportedInterfaces)) {}

  ownership::OwnershipAdmittedBoundModule boundModuleValue;
  const driver::VerifiedModuleInterface& ownModuleInterfaceValue;
  const checker::checked::CheckedFactsRepository& checkedRepositoryValue;
  const checker::checked::VerifiedCheckedFacts& checkedFactsValue;
  checker::checked::CheckedEvidenceLease checkedLeaseValue;
  const checker::dispatch::VerifiedDispatchFacts& dispatchFactsValue;
  driver::borrow_evidence::BorrowEvidenceRevision borrowEvidenceRevisionValue;
  driver::borrow_evidence::VerifiedBorrowEvidenceLease borrowEvidenceLeaseValue;
  const driver::borrow_evidence::BorrowEvidenceRepository& borrowEvidenceRepositoryValue;
  checker::CheckerIdentityAuthority identitiesValue;
  const type::SemanticTypeStore& semanticTypesValue;
  ModuleInterfaceLineage ownInterfaceValue;
  zc::Vector<ModuleInterfaceLineage> visibleImportedInterfaceValues;
};

VerifiedCheckedModule::VerifiedCheckedModule(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}
VerifiedCheckedModule::~VerifiedCheckedModule() noexcept(false) = default;
VerifiedCheckedModule::VerifiedCheckedModule(VerifiedCheckedModule&&) noexcept = default;
VerifiedCheckedModule& VerifiedCheckedModule::operator=(VerifiedCheckedModule&&) noexcept = default;

identity::SemanticContextBrand VerifiedCheckedModule::semanticContext() const noexcept {
  return impl->boundModuleValue.semanticContext();
}

const identity::SemanticContextFingerprint& VerifiedCheckedModule::contextFingerprint()
    const noexcept {
  return impl->boundModuleValue.semanticFingerprint();
}

identity::CompilationUnitId VerifiedCheckedModule::compilationUnit() const noexcept {
  return impl->boundModuleValue.compilationUnit();
}

identity::CrateId VerifiedCheckedModule::crate() const noexcept {
  return impl->boundModuleValue.crate();
}

identity::ModuleId VerifiedCheckedModule::module() const noexcept {
  return impl->boundModuleValue.module();
}

const identity::Sha256Digest& VerifiedCheckedModule::sourceContentDigest() const noexcept {
  return impl->boundModuleValue.parsedModule().contentDigest();
}

const binder::ParsedModuleReceipt& VerifiedCheckedModule::parsedModuleReceipt() const noexcept {
  return impl->boundModuleValue.parsedModule().receipt();
}

const checker::checked::CheckedFactsRevision& VerifiedCheckedModule::checkedFactsRevision()
    const noexcept {
  return impl->checkedFactsValue.revision();
}

const checker::dispatch::DispatchFactsRevision& VerifiedCheckedModule::dispatchFactsRevision()
    const noexcept {
  return impl->dispatchFactsValue.revision();
}

const driver::borrow_evidence::BorrowEvidenceRevision&
VerifiedCheckedModule::borrowEvidenceRevision() const noexcept {
  return impl->borrowEvidenceRevisionValue;
}

const ModuleInterfaceLineage& VerifiedCheckedModule::ownInterface() const noexcept {
  return impl->ownInterfaceValue;
}

zc::ArrayPtr<const ModuleInterfaceLineage> VerifiedCheckedModule::visibleImportedInterfaces()
    const noexcept {
  return impl->visibleImportedInterfaceValues.asPtr();
}

const checker::checked::CheckedEvidenceLease& VerifiedCheckedModule::checkedEvidenceLease()
    const noexcept {
  return impl->checkedLeaseValue;
}

const driver::borrow_evidence::VerifiedBorrowEvidenceLease&
VerifiedCheckedModule::borrowEvidenceLease() const noexcept {
  return impl->borrowEvidenceLeaseValue;
}

ownership::OwnershipAdmittedBoundModule VerifiedCheckedModule::retainAdmittedBoundModule() const {
  return impl->boundModuleValue.retain();
}

const checker::checked::CheckedFactsRepository& VerifiedCheckedModule::checkedRepository()
    const noexcept {
  return impl->checkedRepositoryValue;
}

const checker::checked::VerifiedCheckedFacts& VerifiedCheckedModule::checkedFacts() const noexcept {
  return impl->checkedFactsValue;
}

const checker::dispatch::VerifiedDispatchFacts& VerifiedCheckedModule::dispatchFacts()
    const noexcept {
  return impl->dispatchFactsValue;
}

driver::borrow_evidence::BorrowEvidenceRepositoryCapability
VerifiedCheckedModule::borrowEvidenceCapability() const noexcept {
  return impl->borrowEvidenceRepositoryValue.capability();
}

checker::CheckerIdentityAuthority VerifiedCheckedModule::retainIdentityAuthority() const {
  return impl->identitiesValue.clone();
}

const driver::VerifiedModuleInterface& VerifiedCheckedModule::ownModuleInterface() const noexcept {
  return impl->ownModuleInterfaceValue;
}

const type::SemanticTypeStore& VerifiedCheckedModule::semanticTypes() const noexcept {
  return impl->semanticTypesValue;
}

ir::IrOperationResult<VerifiedCheckedModule> CheckedModuleBuilder::build(
    CheckedModuleBuildInput&& input) {
  const auto context = input.boundModule.semanticContext();
  const auto module = input.boundModule.module();
  const auto& ownInterface = input.moduleInterface;
  const auto& parsedModule = input.boundModule.parsedModule();
  if (!context.isValid() || input.identities.semanticContext() != context ||
      input.identities.fingerprint().digest() != input.boundModule.semanticFingerprint().digest() ||
      input.identities.boundModule(module) == zc::none ||
      input.semanticTypes.context() != context ||
      input.checkedRepository.semanticContext() != context ||
      input.localSignatureFacts.semanticContext() != context ||
      input.localSignatureFacts.module() != module ||
      input.localSignatureFacts.contextFingerprint().digest() !=
          input.boundModule.semanticFingerprint().digest() ||
      ownInterface.semanticContext() != context ||
      input.importedSignatures.semanticContext() != context ||
      input.importedSignatures.contextFingerprint().digest() !=
          input.boundModule.semanticFingerprint().digest() ||
      input.importedSignatures.requester() != module ||
      ownInterface.importedSignatureViewRevision().digest() !=
          input.importedSignatures.revision().digest() ||
      ownInterface.signatureFactsRevision().digest() !=
          input.localSignatureFacts.revision().digest() ||
      ownInterface.compilationUnit() != input.boundModule.compilationUnit() ||
      ownInterface.crate() != input.boundModule.crate() || ownInterface.module() != module ||
      ownInterface.bindingSurface().sourceModule() != module ||
      ownInterface.bindingSurface().sourceCompilationUnit() !=
          input.boundModule.compilationUnit() ||
      ownInterface.bindingSurface().revision().digest() !=
          input.boundModule.bindingSurface().revision().digest() ||
      ownInterface.sourceContentDigest() != parsedModule.contentDigest() ||
      input.checkedLease.module() != module || input.dispatchFacts.semanticContext() != context ||
      input.dispatchFacts.module() != module ||
      input.dispatchFacts.checkedFactsRevision().digest() !=
          input.checkedLease.revision().digest()) {
    return rejectCheckedModule(ir::IrFailureKind::InputRevisionMismatch, module, input.identities,
                               0);
  }

  auto checkedFacts = input.checkedRepository.lookup(input.checkedLease);
  if (checkedFacts == zc::none) {
    return rejectCheckedModule(ir::IrFailureKind::InputRevisionMismatch, module, input.identities,
                               1);
  }

  ZC_IF_SOME(facts, checkedFacts) {
    if (facts.semanticContext() != context || facts.module() != module ||
        facts.revision().digest() != input.checkedLease.revision().digest() ||
        ownInterface.signatureFactsRevision().digest() != facts.signatureFactsRevision().digest() ||
        ownInterface.importedSignatureViewRevision().digest() !=
            facts.importedSignatureViewRevision().digest() ||
        !validateDispatchRevision(input, facts)) {
      return rejectCheckedModule(ir::IrFailureKind::InputRevisionMismatch, module, input.identities,
                                 2);
    }

    zc::Vector<ModuleInterfaceLineage> importedLineage;
    if (!validateImportedInterfaces(input, importedLineage)) {
      zc::Vector<uint32_t> path;
      path.add(1);
      return rejectCheckedModule(ir::IrFailureKind::InvalidFact, module, input.identities, 3,
                                 zc::mv(path));
    }

    auto retainedLease = input.checkedRepository.lease(module, facts.revision());
    if (retainedLease == zc::none) {
      return rejectCheckedModule(ir::IrFailureKind::InputRevisionMismatch, module, input.identities,
                                 4);
    }

    const driver::borrow_evidence::BorrowEvidenceBuildInput evidenceInput{
        input.localSignatureFacts, input.importedSignatures, input.moduleInterface,
        input.availableModuleInterfaces, input.identities};
    auto evidenceCandidate = driver::borrow_evidence::BorrowEvidenceBuilder::build(evidenceInput);
    if (evidenceCandidate.is<driver::borrow_evidence::BorrowEvidenceInvariantRejected>()) {
      return rejectBorrowEvidence(
          evidenceCandidate.get<driver::borrow_evidence::BorrowEvidenceInvariantRejected>(), module,
          input.identities);
    }
    auto evidenceVerification = driver::borrow_evidence::BorrowEvidenceVerifier::verify(
        zc::mv(evidenceCandidate).get<driver::borrow_evidence::BorrowEvidenceCandidate>(),
        evidenceInput);
    if (evidenceVerification.is<driver::borrow_evidence::BorrowEvidenceInvariantRejected>()) {
      return rejectBorrowEvidence(
          evidenceVerification.get<driver::borrow_evidence::BorrowEvidenceInvariantRejected>(),
          module, input.identities);
    }
    auto verifiedEvidence =
        zc::mv(evidenceVerification).get<driver::borrow_evidence::VerifiedBorrowEvidence>();
    const auto evidenceRevision = verifiedEvidence.revision();
    auto evidenceAdoption =
        input.borrowEvidenceRepository.adopt(zc::mv(verifiedEvidence), input.identities);
    if (evidenceAdoption.is<driver::borrow_evidence::BorrowEvidenceRepositoryRejected>()) {
      return rejectCheckedModule(
          evidenceAdoption.get<driver::borrow_evidence::BorrowEvidenceRepositoryRejected>().kind,
          module, input.identities, 5);
    }
    auto borrowLease =
        zc::mv(evidenceAdoption).get<driver::borrow_evidence::VerifiedBorrowEvidenceLease>();
    const auto borrowCapability = input.borrowEvidenceRepository.capability();
    const auto resolvedEvidence = borrowCapability.lookup(borrowLease);
    if (!resolvedEvidence.isResolved() ||
        resolvedEvidence.evidence().revision().digest() != evidenceRevision.digest() ||
        borrowLease.key().revision.digest() != evidenceRevision.digest()) {
      return rejectCheckedModule(ir::IrFailureKind::InputRevisionMismatch, module, input.identities,
                                 6);
    }

    ZC_IF_SOME(lease, retainedLease) {
      auto impl = zc::heap<VerifiedCheckedModule::Impl>(
          input.boundModule.retain(), input.moduleInterface, input.checkedRepository, facts,
          zc::mv(lease), input.dispatchFacts, evidenceRevision, zc::mv(borrowLease),
          input.borrowEvidenceRepository, input.identities.clone(), input.semanticTypes,
          ModuleInterfaceLineage{module, module_interface::ImportedInterfaceRevision(
                                             module_interface::UserImportedInterfaceRevision{
                                                 ownInterface.revision()})},
          zc::mv(importedLineage));
      return ir::IrOperationResult<VerifiedCheckedModule>::verified(
          VerifiedCheckedModule(zc::mv(impl)));
    }
    ZC_UNREACHABLE
  }
  ZC_UNREACHABLE
}

}  // namespace zomlang::compiler::hir
