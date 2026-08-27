// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/driver/interface/borrow-evidence.h"

#include "compiler/driver/core/query.h"
#include "compiler/identity/canonical/canonical-encoder.h"

namespace zomlang::compiler::driver::borrow_evidence {
namespace {

bool lessBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) noexcept {
  const size_t shared = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < shared; ++index) {
    if (left[index] != right[index]) return left[index] < right[index];
  }
  return left.size() < right.size();
}

template <typename Value, typename Less>
void insertionSort(zc::Vector<Value>& values, Less&& comparator) {
  for (size_t index = 1; index < values.size(); ++index) {
    auto current = zc::mv(values[index]);
    size_t insertion = index;
    while (insertion != 0 && comparator(current, values[insertion - 1])) {
      values[insertion] = zc::mv(values[insertion - 1]);
      --insertion;
    }
    values[insertion] = zc::mv(current);
  }
}

BorrowEvidenceInvariantRejected reject(ir::IrFailureKind kind, uint32_t ordinal, uint32_t field) {
  zc::Vector<uint32_t> path;
  path.add(field);
  zc::Vector<BorrowEvidenceInvariantFact> failures;
  failures.add(BorrowEvidenceInvariantFact{kind, ordinal, zc::mv(path)});
  return BorrowEvidenceInvariantRejected{zc::mv(failures)};
}

BorrowEvidenceInvariantRejected rejectCount(ir::IrFailureKind kind, uint32_t ordinal,
                                            uint32_t field, size_t count) {
  zc::Vector<BorrowEvidenceInvariantFact> failures;
  for (size_t index = 0; index < count; ++index) {
    zc::Vector<uint32_t> path;
    path.add(field);
    path.add(static_cast<uint32_t>(index));
    failures.add(
        BorrowEvidenceInvariantFact{kind, ordinal + static_cast<uint32_t>(index), zc::mv(path)});
  }
  return BorrowEvidenceInvariantRejected{zc::mv(failures)};
}

bool isCallable(const checker::signature::SemanticSignature& signature) {
  return signature.payload.variant().is<checker::signature::CallableSignature>() &&
         !signature.scope.variant().is<checker::signature::EnclosedSignatureScope>();
}

struct EncodedSummary final {
  checker::borrow::BorrowSignatureSummary summary;
  zc::Array<uint8_t> expandedKey;
  zc::Array<uint8_t> record;
};

zc::Maybe<EncodedSummary> encodeSummary(const checker::borrow::BorrowSignatureSummary& summary,
                                        const checker::CheckerIdentityAuthority& identities) {
  auto definition = identities.definition(summary.callable);
  if (definition == zc::none) return zc::none;
  zc::Array<uint8_t> expanded;
  ZC_IF_SOME(value, definition) { expanded = value.key().encode(); }
  auto encoded =
      checker::borrow::BorrowSignatureCanonicalCodec::encodeFramed(summary, expanded.asPtr());
  if (encoded == zc::none) return zc::none;
  ZC_IF_SOME(record, encoded) {
    return EncodedSummary{summary.clone(), zc::mv(expanded), zc::mv(record)};
  }
  ZC_UNREACHABLE
}

zc::Maybe<const checker::borrow::BorrowSignatureSummary&> findSummary(
    identity::DefId callable, const checker::borrow::VerifiedBorrowInterfaceSurface& surface) {
  for (const auto& summary : surface.summaries()) {
    if (summary.callable == callable) return summary;
  }
  return zc::none;
}

struct EncodedImportedSurface final {
  ImportedBorrowSurface value;
  zc::Array<uint8_t> expandedKey;
};

struct ForeignSummaryProof final {
  identity::DefId callable;
  zc::Array<uint8_t> record;
};

struct ExpectedInventory final {
  zc::Vector<EncodedSummary> local;
  zc::Vector<EncodedImportedSurface> imported;
};

using ExpectedInventoryResult = zc::OneOf<ExpectedInventory, BorrowEvidenceInvariantRejected>;

bool sameFingerprint(const identity::ContextFingerprint& left,
                     const identity::ContextFingerprint& right) noexcept {
  return left.digest() == right.digest();
}

ExpectedInventoryResult deriveExpectedInventory(const BorrowEvidenceBuildInput& input) {
  const auto context = input.localSignatureFacts.semanticContext();
  const auto module = input.localSignatureFacts.module();
  const auto& ownSurface = input.ownInterface.borrowSurface();
  if (!context.isValid() || !module.belongsTo(context) ||
      input.identities.semanticContext() != context ||
      !sameFingerprint(input.identities.fingerprint(),
                       input.localSignatureFacts.contextFingerprint()) ||
      input.identities.module(module) == zc::none ||
      input.importedSignatures.semanticContext() != context ||
      input.importedSignatures.requester() != module ||
      !sameFingerprint(input.localSignatureFacts.contextFingerprint(),
                       input.importedSignatures.contextFingerprint()) ||
      input.ownInterface.semanticContext() != context || input.ownInterface.module() != module ||
      input.ownInterface.signatureFactsRevision().digest() !=
          input.localSignatureFacts.revision().digest() ||
      input.ownInterface.importedSignatureViewRevision().digest() !=
          input.importedSignatures.revision().digest() ||
      ownSurface.semanticContext() != context || ownSurface.module() != module ||
      !sameFingerprint(ownSurface.contextFingerprint(),
                       input.localSignatureFacts.contextFingerprint()) ||
      ownSurface.signatureFactsRevision().digest() !=
          input.localSignatureFacts.revision().digest() ||
      ownSurface.importedSignatureViewRevision().digest() !=
          input.importedSignatures.revision().digest()) {
    return reject(ir::IrFailureKind::InputRevisionMismatch, 0, 0);
  }

  ExpectedInventory expected;
  for (const auto& signature : input.localSignatureFacts.signatures()) {
    if (!isCallable(signature)) continue;
    for (const auto& existing : expected.local) {
      if (existing.summary.callable == signature.definition) {
        return reject(ir::IrFailureKind::AdditionalFact, 1, 0);
      }
    }
    auto summary = findSummary(signature.definition, ownSurface);
    if (summary == zc::none) { return reject(ir::IrFailureKind::MissingRequiredFact, 2, 0); }
    ZC_IF_SOME(value, summary) {
      auto encoded = encodeSummary(value, input.identities);
      if (encoded == zc::none) { return reject(ir::IrFailureKind::CanonicalCodecMismatch, 3, 0); }
      ZC_IF_SOME(record, encoded) { expected.local.add(zc::mv(record)); }
    }
  }
  insertionSort(expected.local, [](const EncodedSummary& left, const EncodedSummary& right) {
    return lessBytes(left.expandedKey.asPtr(), right.expandedKey.asPtr());
  });

  zc::Vector<ForeignSummaryProof> foreignProofs;
  for (const auto& imported : input.importedSignatures.modules()) {
    if (!imported.sourceModule().belongsTo(context) || imported.sourceModule() == module) {
      return reject(ir::IrFailureKind::InvalidFact, 4, 1);
    }
    for (const auto& existing : expected.imported) {
      if (existing.value.module() == imported.sourceModule()) {
        return reject(ir::IrFailureKind::AdditionalFact, 5, 1);
      }
    }

    const bool coreSource = imported.interfaceRevision()
                                .variant()
                                .is<module_interface::ToolchainCoreImportedInterfaceRevision>();
    if (coreSource) {
      zc::Maybe<const core_library_query::VerifiedCoreModuleInterface&> selected;
      const auto& interfaceRevision = imported.interfaceRevision().variant();
      const auto& bindingSurfaceRevision = imported.bindingSurfaceRevision().variant();
      for (const auto& source : input.availableInterfaces) {
        if (!source.is<ToolchainCoreVerifiedInterfaceSource>()) continue;
        const auto& available = source.get<ToolchainCoreVerifiedInterfaceSource>().interface;
        if (available.module() != imported.sourceModule()) continue;
        if (selected != zc::none || available.context() != context ||
            available.fingerprint().digest() !=
                input.localSignatureFacts.contextFingerprint().digest() ||
            !interfaceRevision.is<module_interface::ToolchainCoreImportedInterfaceRevision>() ||
            available.record().revision().digest() !=
                interfaceRevision.get<module_interface::ToolchainCoreImportedInterfaceRevision>()
                    .value.digest() ||
            !bindingSurfaceRevision
                 .is<module_interface::ToolchainCoreImportedBindingSurfaceRevision>() ||
            available.record().bindingSurfaceRevision().digest() !=
                bindingSurfaceRevision
                    .get<module_interface::ToolchainCoreImportedBindingSurfaceRevision>()
                    .value.digest()) {
          return reject(ir::IrFailureKind::InputRevisionMismatch, 7, 1);
        }
        selected = available;
      }
      if (selected == zc::none) { return reject(ir::IrFailureKind::MissingRequiredFact, 8, 1); }
      for (const auto& signature : imported.lookupDefinitions()) {
        if (isCallable(signature)) { return reject(ir::IrFailureKind::InvalidFact, 9, 1); }
      }
      for (const auto& signature : imported.supportDefinitions()) {
        if (isCallable(signature)) { return reject(ir::IrFailureKind::InvalidFact, 10, 1); }
      }
      continue;
    }

    zc::Maybe<const VerifiedModuleInterface&> selected;
    for (const auto& source : input.availableInterfaces) {
      if (!source.is<UserVerifiedInterfaceSource>()) continue;
      const auto& available = source.get<UserVerifiedInterfaceSource>().interface;
      if (available.module() != imported.sourceModule()) continue;
      if (selected != zc::none) { return reject(ir::IrFailureKind::AdditionalFact, 6, 1); }
      const auto& interfaceRevision = imported.interfaceRevision().variant();
      const auto& bindingSurfaceRevision = imported.bindingSurfaceRevision().variant();
      if (available.semanticContext() != context ||
          !interfaceRevision.is<module_interface::UserImportedInterfaceRevision>() ||
          interfaceRevision.get<module_interface::UserImportedInterfaceRevision>().value.digest() !=
              available.revision().digest() ||
          !bindingSurfaceRevision.is<module_interface::UserImportedBindingSurfaceRevision>() ||
          available.bindingSurface().revision().digest() !=
              bindingSurfaceRevision.get<module_interface::UserImportedBindingSurfaceRevision>()
                  .value.digest()) {
        return reject(ir::IrFailureKind::InputRevisionMismatch, 7, 1);
      }
      selected = available;
    }
    if (selected == zc::none) { return reject(ir::IrFailureKind::MissingRequiredFact, 8, 1); }

    ZC_IF_SOME(selectedInterface, selected) {
      const auto& surface = selectedInterface.borrowSurface();
      if (surface.semanticContext() != context || surface.module() != imported.sourceModule() ||
          !sameFingerprint(surface.contextFingerprint(),
                           input.localSignatureFacts.contextFingerprint()) ||
          surface.signatureFactsRevision().digest() !=
              selectedInterface.signatureFactsRevision().digest() ||
          surface.importedSignatureViewRevision().digest() !=
              selectedInterface.importedSignatureViewRevision().digest()) {
        return reject(ir::IrFailureKind::InvalidFact, 9, 1);
      }

      const auto validateCallable = [&](const checker::signature::SemanticSignature& signature)
          -> zc::Maybe<BorrowEvidenceInvariantRejected> {
        if (!isCallable(signature)) return zc::none;
        auto summary = findSummary(signature.definition, surface);
        if (summary == zc::none) { return reject(ir::IrFailureKind::MissingRequiredFact, 10, 1); }
        ZC_IF_SOME(value, summary) {
          auto encoded = encodeSummary(value, input.identities);
          if (encoded == zc::none) {
            return reject(ir::IrFailureKind::CanonicalCodecMismatch, 11, 1);
          }
          ZC_IF_SOME(record, encoded) {
            for (const auto& proof : foreignProofs) {
              if (proof.callable != signature.definition) continue;
              if (proof.record.asPtr() != record.record.asPtr()) {
                return reject(ir::IrFailureKind::InvalidFact, 12, 1);
              }
              return zc::none;
            }
            foreignProofs.add(ForeignSummaryProof{signature.definition, zc::mv(record.record)});
          }
        }
        return zc::none;
      };
      for (const auto& signature : imported.lookupDefinitions()) {
        auto failure = validateCallable(signature);
        ZC_IF_SOME(value, failure) { return zc::mv(value); }
      }
      for (const auto& signature : imported.supportDefinitions()) {
        auto failure = validateCallable(signature);
        ZC_IF_SOME(value, failure) { return zc::mv(value); }
      }

      auto sourceModule = input.identities.module(imported.sourceModule());
      if (sourceModule == zc::none) { return reject(ir::IrFailureKind::InvalidFact, 13, 1); }
      ZC_IF_SOME(value, sourceModule) {
        expected.imported.add(EncodedImportedSurface{
            ImportedBorrowSurface(imported.sourceModule(), selectedInterface.revision(),
                                  surface.clone()),
            value.key().encode()});
      }
    }
  }
  insertionSort(expected.imported,
                [](const EncodedImportedSurface& left, const EncodedImportedSurface& right) {
                  return lessBytes(left.expandedKey.asPtr(), right.expandedKey.asPtr());
                });
  return expected;
}

zc::Maybe<zc::Array<uint8_t>> encodeInventory(
    const identity::ContextFingerprint& fingerprint, identity::ModuleId module,
    const checker::signature::SignatureFactsRevision& signatureRevision,
    zc::ArrayPtr<const EncodedSummary> summaries,
    const module_interface::ModuleInterfaceRevision& ownInterface,
    const checker::borrow::BorrowInterfaceRevision& ownBorrow,
    zc::ArrayPtr<const EncodedImportedSurface> imported,
    const checker::CheckerIdentityAuthority& identities) {
  auto moduleEntry = identities.module(module);
  if (moduleEntry == zc::none) return zc::none;
  zc::Vector<LocalBorrowSummaryRevisionFrame> summaryFrames;
  for (const auto& summary : summaries) {
    summaryFrames.add(
        LocalBorrowSummaryRevisionFrame{summary.expandedKey.asPtr(), summary.record.asPtr()});
  }
  zc::Vector<ImportedBorrowRevisionFrame> importedFrames;
  for (const auto& surface : imported) {
    importedFrames.add(ImportedBorrowRevisionFrame{surface.expandedKey.asPtr(),
                                                   surface.value.interfaceRevision().digest(),
                                                   surface.value.surface().revision().digest()});
  }
  ZC_IF_SOME(value, moduleEntry) {
    const auto expanded = value.key().encode();
    return BorrowEvidenceCanonicalCodec::encodeFramed(
        fingerprint.digest(), expanded.asPtr(), signatureRevision.digest(), summaryFrames.asPtr(),
        ownInterface.digest(), ownBorrow.digest(), importedFrames.asPtr());
  }
  ZC_UNREACHABLE
}

}  // namespace

BorrowEvidenceRevision::BorrowEvidenceRevision(const identity::Sha256Digest& value) noexcept
    : value(value) {}

const identity::Sha256Digest& BorrowEvidenceRevision::digest() const noexcept { return value; }

zc::Maybe<zc::Array<uint8_t>> BorrowEvidenceCanonicalCodec::encodeFramed(
    const identity::Sha256Digest& contextFingerprint, zc::ArrayPtr<const uint8_t> expandedModuleKey,
    const identity::Sha256Digest& signatureFactsRevision,
    zc::ArrayPtr<const LocalBorrowSummaryRevisionFrame> localSummaries,
    const identity::Sha256Digest& ownInterfaceRevision,
    const identity::Sha256Digest& ownBorrowRevision,
    zc::ArrayPtr<const ImportedBorrowRevisionFrame> importedSurfaces) {
  if (expandedModuleKey.size() == 0) return zc::none;
  for (const auto& summary : localSummaries) {
    if (summary.expandedCallableKey.size() == 0 || summary.encodedSummary.size() == 0) {
      return zc::none;
    }
  }
  for (size_t index = 1; index < localSummaries.size(); ++index) {
    if (!lessBytes(localSummaries[index - 1].expandedCallableKey,
                   localSummaries[index].expandedCallableKey)) {
      return zc::none;
    }
  }
  for (const auto& surface : importedSurfaces) {
    if (surface.expandedModuleKey.size() == 0) return zc::none;
  }
  for (size_t index = 1; index < importedSurfaces.size(); ++index) {
    if (!lessBytes(importedSurfaces[index - 1].expandedModuleKey,
                   importedSurfaces[index].expandedModuleKey)) {
      return zc::none;
    }
  }
  identity::CanonicalEncoder encoder;
  static constexpr uint8_t domain[] = {'z', 'o', 'm', '.', 'b', 'o', 'r', 'r', 'o', 'w',
                                       '-', 'e', 'v', 'i', 'd', 'e', 'n', 'c', 'e'};
  for (const auto byte : domain) encoder.encodeUint8(byte);
  encoder.encodeUint8(0);
  encoder.encodeDigest(contextFingerprint);
  encoder.encodeByteString(expandedModuleKey);
  encoder.encodeDigest(signatureFactsRevision);
  encoder.encodeSequenceSize(localSummaries.size());
  for (const auto& summary : localSummaries) encoder.encodeByteString(summary.encodedSummary);
  encoder.encodeDigest(ownInterfaceRevision);
  encoder.encodeDigest(ownBorrowRevision);
  encoder.encodeSequenceSize(importedSurfaces.size());
  for (const auto& surface : importedSurfaces) {
    encoder.encodeByteString(surface.expandedModuleKey);
    encoder.encodeDigest(surface.interfaceRevision);
    encoder.encodeDigest(surface.borrowRevision);
  }
  return encoder.finish();
}

zc::Maybe<BorrowEvidenceRevision> BorrowEvidenceRevision::computeFramed(
    const identity::Sha256Digest& contextFingerprint, zc::ArrayPtr<const uint8_t> expandedModuleKey,
    const identity::Sha256Digest& signatureFactsRevision,
    zc::ArrayPtr<const LocalBorrowSummaryRevisionFrame> localSummaries,
    const identity::Sha256Digest& ownInterfaceRevision,
    const identity::Sha256Digest& ownBorrowRevision,
    zc::ArrayPtr<const ImportedBorrowRevisionFrame> importedSurfaces) {
  auto encoded = BorrowEvidenceCanonicalCodec::encodeFramed(
      contextFingerprint, expandedModuleKey, signatureFactsRevision, localSummaries,
      ownInterfaceRevision, ownBorrowRevision, importedSurfaces);
  if (encoded == zc::none) return zc::none;
  ZC_IF_SOME(bytes, encoded) {
    auto digest = identity::sha256(bytes.asPtr());
    ZC_IF_SOME(value, digest) { return BorrowEvidenceRevision(value); }
  }
  return zc::none;
}

struct ImportedBorrowSurface::Impl final {
  Impl(identity::ModuleId module, module_interface::ModuleInterfaceRevision interfaceRevision,
       checker::borrow::VerifiedBorrowInterfaceSurface&& surface)
      : module(module), interfaceRevision(interfaceRevision), surface(zc::mv(surface)) {}

  identity::ModuleId module;
  module_interface::ModuleInterfaceRevision interfaceRevision;
  checker::borrow::VerifiedBorrowInterfaceSurface surface;
};

ImportedBorrowSurface::ImportedBorrowSurface(
    identity::ModuleId module, module_interface::ModuleInterfaceRevision interfaceRevision,
    checker::borrow::VerifiedBorrowInterfaceSurface&& surface)
    : impl(zc::heap<Impl>(module, interfaceRevision, zc::mv(surface))) {}
ImportedBorrowSurface::ImportedBorrowSurface(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}
ImportedBorrowSurface::~ImportedBorrowSurface() noexcept(false) = default;
ImportedBorrowSurface::ImportedBorrowSurface(ImportedBorrowSurface&&) noexcept = default;
ImportedBorrowSurface& ImportedBorrowSurface::operator=(ImportedBorrowSurface&&) noexcept = default;
identity::ModuleId ImportedBorrowSurface::module() const noexcept { return impl->module; }
const module_interface::ModuleInterfaceRevision& ImportedBorrowSurface::interfaceRevision()
    const noexcept {
  return impl->interfaceRevision;
}
const checker::borrow::VerifiedBorrowInterfaceSurface& ImportedBorrowSurface::surface()
    const noexcept {
  return impl->surface;
}
ImportedBorrowSurface ImportedBorrowSurface::clone() const {
  return ImportedBorrowSurface(
      zc::heap<Impl>(impl->module, impl->interfaceRevision, impl->surface.clone()));
}

BorrowEvidenceCandidateResult BorrowEvidenceBuilder::build(const BorrowEvidenceBuildInput& input) {
  auto derived = deriveExpectedInventory(input);
  if (derived.is<BorrowEvidenceInvariantRejected>()) {
    return zc::mv(derived).get<BorrowEvidenceInvariantRejected>();
  }
  auto expected = zc::mv(derived).get<ExpectedInventory>();
  auto canonical = encodeInventory(
      input.localSignatureFacts.contextFingerprint(), input.localSignatureFacts.module(),
      input.localSignatureFacts.revision(), expected.local.asPtr(), input.ownInterface.revision(),
      input.ownInterface.borrowSurface().revision(), expected.imported.asPtr(), input.identities);
  if (canonical == zc::none) { return reject(ir::IrFailureKind::CanonicalCodecMismatch, 14, 2); }
  ZC_IF_SOME(record, canonical) {
    auto digest = identity::sha256(record.asPtr());
    if (digest == zc::none) { return reject(ir::IrFailureKind::CanonicalCodecMismatch, 15, 2); }
    zc::Vector<checker::borrow::BorrowSignatureSummary> summaries;
    for (auto& summary : expected.local) { summaries.add(zc::mv(summary.summary)); }
    zc::Vector<ImportedBorrowSurfaceCandidate> imported;
    for (auto& surface : expected.imported) {
      const auto module = surface.value.module();
      imported.add(ImportedBorrowSurfaceCandidate{module, zc::mv(surface.value)});
    }
    ZC_IF_SOME(revision, digest) {
      return BorrowEvidenceCandidate{input.localSignatureFacts.semanticContext(),
                                     input.localSignatureFacts.contextFingerprint().clone(),
                                     input.localSignatureFacts.module(),
                                     input.localSignatureFacts.revision(),
                                     zc::mv(summaries),
                                     input.ownInterface.revision(),
                                     input.ownInterface.borrowSurface().revision(),
                                     zc::mv(imported),
                                     BorrowEvidenceRevision(revision),
                                     zc::mv(record)};
    }
  }
  ZC_UNREACHABLE
}

struct VerifiedBorrowEvidence::Impl final {
  explicit Impl(BorrowEvidenceCandidate&& candidate)
      : semanticContext(candidate.semanticContext),
        contextFingerprint(zc::mv(candidate.contextFingerprint)),
        module(candidate.module),
        signatureRevision(candidate.localSignatureFactsRevision),
        localSummaries(zc::mv(candidate.localSummaries)),
        ownInterfaceRevision(candidate.ownInterfaceRevision),
        ownBorrowRevision(candidate.ownBorrowRevision),
        revision(candidate.revision) {
    for (auto& imported : candidate.importedSurfaces) {
      importedSurfaces.add(zc::mv(imported.surface));
    }
  }

  Impl(identity::SemanticContextBrand semanticContext,
       identity::ContextFingerprint&& contextFingerprint, identity::ModuleId module,
       checker::signature::SignatureFactsRevision signatureRevision,
       zc::Vector<checker::borrow::BorrowSignatureSummary>&& localSummaries,
       module_interface::ModuleInterfaceRevision ownInterfaceRevision,
       checker::borrow::BorrowInterfaceRevision ownBorrowRevision,
       zc::Vector<ImportedBorrowSurface>&& importedSurfaces, BorrowEvidenceRevision revision)
      : semanticContext(semanticContext),
        contextFingerprint(zc::mv(contextFingerprint)),
        module(module),
        signatureRevision(signatureRevision),
        localSummaries(zc::mv(localSummaries)),
        ownInterfaceRevision(ownInterfaceRevision),
        ownBorrowRevision(ownBorrowRevision),
        importedSurfaces(zc::mv(importedSurfaces)),
        revision(revision) {}

  identity::SemanticContextBrand semanticContext;
  identity::ContextFingerprint contextFingerprint;
  identity::ModuleId module;
  checker::signature::SignatureFactsRevision signatureRevision;
  zc::Vector<checker::borrow::BorrowSignatureSummary> localSummaries;
  module_interface::ModuleInterfaceRevision ownInterfaceRevision;
  checker::borrow::BorrowInterfaceRevision ownBorrowRevision;
  zc::Vector<ImportedBorrowSurface> importedSurfaces;
  BorrowEvidenceRevision revision;
};

VerifiedBorrowEvidence::VerifiedBorrowEvidence(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}
VerifiedBorrowEvidence::~VerifiedBorrowEvidence() noexcept(false) = default;
VerifiedBorrowEvidence::VerifiedBorrowEvidence(VerifiedBorrowEvidence&&) noexcept = default;
VerifiedBorrowEvidence& VerifiedBorrowEvidence::operator=(VerifiedBorrowEvidence&&) noexcept =
    default;
VerifiedBorrowEvidence VerifiedBorrowEvidence::clone() const {
  zc::Vector<checker::borrow::BorrowSignatureSummary> localSummaries(impl->localSummaries.size());
  for (const auto& summary : impl->localSummaries) { localSummaries.add(summary.clone()); }
  zc::Vector<ImportedBorrowSurface> importedSurfaces(impl->importedSurfaces.size());
  for (const auto& surface : impl->importedSurfaces) { importedSurfaces.add(surface.clone()); }
  return VerifiedBorrowEvidence(
      zc::heap<Impl>(impl->semanticContext, impl->contextFingerprint.clone(), impl->module,
                     impl->signatureRevision, zc::mv(localSummaries), impl->ownInterfaceRevision,
                     impl->ownBorrowRevision, zc::mv(importedSurfaces), impl->revision));
}
identity::SemanticContextBrand VerifiedBorrowEvidence::semanticContext() const noexcept {
  return impl->semanticContext;
}
const identity::ContextFingerprint& VerifiedBorrowEvidence::contextFingerprint()
    const noexcept {
  return impl->contextFingerprint;
}
identity::ModuleId VerifiedBorrowEvidence::module() const noexcept { return impl->module; }
const checker::signature::SignatureFactsRevision&
VerifiedBorrowEvidence::localSignatureFactsRevision() const noexcept {
  return impl->signatureRevision;
}
zc::ArrayPtr<const checker::borrow::BorrowSignatureSummary> VerifiedBorrowEvidence::localSummaries()
    const noexcept {
  return impl->localSummaries;
}
const module_interface::ModuleInterfaceRevision& VerifiedBorrowEvidence::ownInterfaceRevision()
    const noexcept {
  return impl->ownInterfaceRevision;
}
const checker::borrow::BorrowInterfaceRevision& VerifiedBorrowEvidence::ownBorrowRevision()
    const noexcept {
  return impl->ownBorrowRevision;
}
zc::ArrayPtr<const ImportedBorrowSurface> VerifiedBorrowEvidence::importedSurfaces()
    const noexcept {
  return impl->importedSurfaces;
}
const BorrowEvidenceRevision& VerifiedBorrowEvidence::revision() const noexcept {
  return impl->revision;
}

BorrowEvidenceVerificationResult BorrowEvidenceVerifier::verify(
    BorrowEvidenceCandidate&& candidate, const BorrowEvidenceBuildInput& input) {
  auto derived = deriveExpectedInventory(input);
  if (derived.is<BorrowEvidenceInvariantRejected>()) {
    return zc::mv(derived).get<BorrowEvidenceInvariantRejected>();
  }
  auto expected = zc::mv(derived).get<ExpectedInventory>();

  if (candidate.semanticContext != input.localSignatureFacts.semanticContext() ||
      candidate.module != input.localSignatureFacts.module() ||
      !sameFingerprint(candidate.contextFingerprint,
                       input.localSignatureFacts.contextFingerprint()) ||
      candidate.localSignatureFactsRevision.digest() !=
          input.localSignatureFacts.revision().digest() ||
      candidate.ownInterfaceRevision.digest() != input.ownInterface.revision().digest() ||
      candidate.ownBorrowRevision.digest() !=
          input.ownInterface.borrowSurface().revision().digest()) {
    return reject(ir::IrFailureKind::InputRevisionMismatch, 16, 3);
  }

  size_t duplicateCount = 0;
  for (size_t index = 0; index < candidate.localSummaries.size(); ++index) {
    for (size_t prior = 0; prior < index; ++prior) {
      if (candidate.localSummaries[prior].callable == candidate.localSummaries[index].callable) {
        ++duplicateCount;
        break;
      }
    }
  }
  for (size_t index = 0; index < candidate.importedSurfaces.size(); ++index) {
    for (size_t prior = 0; prior < index; ++prior) {
      if (candidate.importedSurfaces[prior].module == candidate.importedSurfaces[index].module) {
        ++duplicateCount;
        break;
      }
    }
  }
  if (duplicateCount != 0) {
    return rejectCount(ir::IrFailureKind::AdditionalFact, 17, 3, duplicateCount);
  }
  size_t missingCount = 0;
  for (const auto& wanted : expected.local) {
    bool found = false;
    for (const auto& actual : candidate.localSummaries) {
      if (actual.callable == wanted.summary.callable) {
        found = true;
        break;
      }
    }
    if (!found) ++missingCount;
  }
  for (const auto& wanted : expected.imported) {
    bool found = false;
    for (const auto& actual : candidate.importedSurfaces) {
      if (actual.module == wanted.value.module()) {
        found = true;
        break;
      }
    }
    if (!found) ++missingCount;
  }
  if (missingCount != 0) {
    return rejectCount(ir::IrFailureKind::MissingRequiredFact, 19, 3, missingCount);
  }

  size_t additionalCount = 0;
  for (const auto& actual : candidate.localSummaries) {
    bool found = false;
    for (const auto& wanted : expected.local) {
      if (actual.callable == wanted.summary.callable) {
        found = true;
        break;
      }
    }
    if (!found) ++additionalCount;
  }
  for (const auto& actual : candidate.importedSurfaces) {
    bool found = false;
    for (const auto& wanted : expected.imported) {
      if (actual.module == wanted.value.module()) {
        found = true;
        break;
      }
    }
    if (!found) ++additionalCount;
  }
  if (additionalCount != 0) {
    return rejectCount(ir::IrFailureKind::AdditionalFact, 20, 3, additionalCount);
  }

  zc::Vector<EncodedSummary> candidateSummaries;
  for (const auto& summary : candidate.localSummaries) {
    auto encoded = encodeSummary(summary, input.identities);
    if (encoded == zc::none) { return reject(ir::IrFailureKind::CanonicalCodecMismatch, 21, 3); }
    ZC_IF_SOME(value, encoded) { candidateSummaries.add(zc::mv(value)); }
  }
  for (size_t index = 0; index < expected.local.size(); ++index) {
    if (candidateSummaries[index].summary.callable != expected.local[index].summary.callable) {
      bool keyExists = false;
      for (const auto& summary : expected.local) {
        keyExists =
            keyExists || summary.summary.callable == candidateSummaries[index].summary.callable;
      }
      return reject(keyExists ? ir::IrFailureKind::CanonicalCodecMismatch
                              : ir::IrFailureKind::MissingRequiredFact,
                    22, 3);
    }
    if (candidateSummaries[index].record.asPtr() != expected.local[index].record.asPtr()) {
      return reject(ir::IrFailureKind::InvalidFact, 23, 3);
    }
  }

  zc::Vector<EncodedImportedSurface> candidateImported;
  for (const auto& imported : candidate.importedSurfaces) {
    if (imported.module != imported.surface.module()) {
      return reject(ir::IrFailureKind::InvalidFact, 24, 3);
    }
    auto module = input.identities.module(imported.module);
    if (module == zc::none) { return reject(ir::IrFailureKind::InvalidFact, 24, 3); }
    ZC_IF_SOME(value, module) {
      candidateImported.add(EncodedImportedSurface{imported.surface.clone(), value.key().encode()});
    }
  }
  for (size_t index = 0; index < expected.imported.size(); ++index) {
    const auto& actual = candidateImported[index].value;
    const auto& wanted = expected.imported[index].value;
    if (actual.module() != wanted.module()) {
      bool keyExists = false;
      for (const auto& imported : expected.imported) {
        keyExists = keyExists || imported.value.module() == actual.module();
      }
      return reject(keyExists ? ir::IrFailureKind::CanonicalCodecMismatch
                              : ir::IrFailureKind::MissingRequiredFact,
                    25, 3);
    }
    if (actual.interfaceRevision().digest() != wanted.interfaceRevision().digest()) {
      return reject(ir::IrFailureKind::InputRevisionMismatch, 26, 3);
    }
    const auto& actualSurface = actual.surface();
    const auto& wantedSurface = wanted.surface();
    if (actualSurface.semanticContext() != candidate.semanticContext ||
        actualSurface.module() != actual.module() ||
        !sameFingerprint(actualSurface.contextFingerprint(), candidate.contextFingerprint) ||
        actualSurface.revision().digest() != wantedSurface.revision().digest() ||
        actualSurface.signatureFactsRevision().digest() !=
            wantedSurface.signatureFactsRevision().digest() ||
        actualSurface.importedSignatureViewRevision().digest() !=
            wantedSurface.importedSignatureViewRevision().digest() ||
        actualSurface.summaries().size() != wantedSurface.summaries().size()) {
      return reject(ir::IrFailureKind::InvalidFact, 27, 3);
    }
    for (size_t summaryIndex = 0; summaryIndex < actualSurface.summaries().size(); ++summaryIndex) {
      auto actualRecord = encodeSummary(actualSurface.summaries()[summaryIndex], input.identities);
      auto wantedRecord = encodeSummary(wantedSurface.summaries()[summaryIndex], input.identities);
      if (actualRecord == zc::none || wantedRecord == zc::none) {
        return reject(ir::IrFailureKind::CanonicalCodecMismatch, 28, 3);
      }
      ZC_IF_SOME(actualValue, actualRecord) {
        ZC_IF_SOME(wantedValue, wantedRecord) {
          if (actualValue.record.asPtr() != wantedValue.record.asPtr()) {
            return reject(ir::IrFailureKind::InvalidFact, 29, 3);
          }
        }
      }
    }
  }

  auto canonical = encodeInventory(
      candidate.contextFingerprint, candidate.module, candidate.localSignatureFactsRevision,
      candidateSummaries.asPtr(), candidate.ownInterfaceRevision, candidate.ownBorrowRevision,
      candidateImported.asPtr(), input.identities);
  if (canonical == zc::none) { return reject(ir::IrFailureKind::CanonicalCodecMismatch, 30, 3); }
  ZC_IF_SOME(record, canonical) {
    if (record.asPtr() != candidate.canonicalRecord.asPtr()) {
      return reject(ir::IrFailureKind::CanonicalCodecMismatch, 31, 3);
    }
    auto digest = identity::sha256(record.asPtr());
    if (digest == zc::none) { return reject(ir::IrFailureKind::CanonicalCodecMismatch, 32, 3); }
    ZC_IF_SOME(value, digest) {
      if (value != candidate.revision.digest()) {
        return reject(ir::IrFailureKind::CanonicalCodecMismatch, 33, 3);
      }
    }
  }
  return VerifiedBorrowEvidence(zc::heap<VerifiedBorrowEvidence::Impl>(zc::mv(candidate)));
}

VerifiedBorrowEvidenceLease::VerifiedBorrowEvidenceLease(
    identity::SemanticContextBrand semanticContext, identity::RegistryBrand repository,
    BorrowEvidenceKey key) noexcept
    : context(semanticContext), repository(repository), evidenceKey(key) {}
identity::SemanticContextBrand VerifiedBorrowEvidenceLease::semanticContext() const noexcept {
  return context;
}
const BorrowEvidenceKey& VerifiedBorrowEvidenceLease::key() const noexcept { return evidenceKey; }

VerifiedBorrowEvidenceLease VerifiedBorrowEvidenceLease::clone() const {
  return VerifiedBorrowEvidenceLease(context, repository,
                                     BorrowEvidenceKey{evidenceKey.module, evidenceKey.revision});
}
bool VerifiedBorrowEvidenceLease::matches(const VerifiedBorrowEvidenceLease& other) const noexcept {
  return context == other.context && repository == other.repository &&
         evidenceKey.module == other.evidenceKey.module &&
         evidenceKey.revision.digest() == other.evidenceKey.revision.digest();
}

BorrowEvidenceLookupResult::BorrowEvidenceLookupResult(
    const VerifiedBorrowEvidence& evidence) noexcept
    : resolved(evidence) {}
BorrowEvidenceLookupResult::BorrowEvidenceLookupResult(ir::IrFailureKind rejection) noexcept
    : rejection(rejection) {}
bool BorrowEvidenceLookupResult::isResolved() const noexcept { return resolved != zc::none; }
const VerifiedBorrowEvidence& BorrowEvidenceLookupResult::evidence() const {
  ZC_IF_SOME(value, resolved) { return value; }
  ZC_UNREACHABLE
}
ir::IrFailureKind BorrowEvidenceLookupResult::rejectionKind() const noexcept { return rejection; }

BorrowEvidenceRepositoryCapability::BorrowEvidenceRepositoryCapability(
    identity::SemanticContextBrand semanticContext, identity::RegistryBrand repository,
    zc::Arc<detail::BorrowEvidenceRepositoryState>&& state) noexcept
    : context(semanticContext), repository(repository), state(zc::mv(state)) {}
identity::SemanticContextBrand BorrowEvidenceRepositoryCapability::semanticContext()
    const noexcept {
  return context;
}
BorrowEvidenceRepositoryCapability BorrowEvidenceRepositoryCapability::clone() const noexcept {
  return BorrowEvidenceRepositoryCapability(context, repository, state.addRef());
}
bool BorrowEvidenceRepositoryCapability::matches(
    const BorrowEvidenceRepositoryCapability& other) const noexcept {
  return context == other.context && repository == other.repository && state == other.state;
}
BorrowEvidenceLookupResult BorrowEvidenceRepositoryCapability::lookup(
    const VerifiedBorrowEvidenceLease& lease) const noexcept {
  if (!state->isLive()) {
    return BorrowEvidenceLookupResult(ir::IrFailureKind::InputRevisionMismatch);
  }
  const auto& capability = *this;
  if (state->context != capability.context || state->repository != capability.repository ||
      lease.context != capability.context || lease.repository != capability.repository) {
    return BorrowEvidenceLookupResult(ir::IrFailureKind::InvalidFact);
  }
  if (!lease.evidenceKey.module.belongsTo(state->context)) {
    return BorrowEvidenceLookupResult(ir::IrFailureKind::InputRevisionMismatch);
  }
  for (const auto index : state->sortedIndices) {
    const auto& entry = state->entries[index];
    if (entry.evidence.module() == lease.evidenceKey.module &&
        entry.evidence.revision().digest() == lease.evidenceKey.revision.digest()) {
      return BorrowEvidenceLookupResult(entry.evidence);
    }
  }
  return BorrowEvidenceLookupResult(ir::IrFailureKind::InputRevisionMismatch);
}

BorrowEvidenceRepository::BorrowEvidenceRepository(
    zc::Arc<detail::BorrowEvidenceRepositoryState>&& state) noexcept
    : state(zc::mv(state)) {}
BorrowEvidenceRepository::~BorrowEvidenceRepository() noexcept(false) {
  if (state != nullptr) state->invalidate();
}
BorrowEvidenceRepository::BorrowEvidenceRepository(BorrowEvidenceRepository&&) noexcept = default;
BorrowEvidenceRepository& BorrowEvidenceRepository::operator=(
    BorrowEvidenceRepository&& other) noexcept {
  if (this != &other) {
    if (state != nullptr) state->invalidate();
    state = zc::mv(other.state);
  }
  return *this;
}

zc::Maybe<BorrowEvidenceRepository> BorrowEvidenceRepository::create(
    identity::SemanticContextBrand context, identity::RegistryBrand repositoryBrand,
    uint32_t expectedEntryCount) {
  if (!context.isValid() || !repositoryBrand.belongsTo(context)) return zc::none;
  return BorrowEvidenceRepository(
      zc::arc<detail::BorrowEvidenceRepositoryState>(context, repositoryBrand, expectedEntryCount));
}

BorrowEvidenceAdoptionResult BorrowEvidenceRepository::adopt(
    VerifiedBorrowEvidence&& evidence, const checker::CheckerIdentityAuthority& identities) {
  if (identities.semanticContext() != state->context ||
      evidence.semanticContext() != state->context ||
      !evidence.module().belongsTo(state->context)) {
    return BorrowEvidenceRepositoryRejected{ir::IrFailureKind::InputRevisionMismatch};
  }
  auto module = identities.module(evidence.module());
  if (module == zc::none) {
    return BorrowEvidenceRepositoryRejected{ir::IrFailureKind::InvalidFact};
  }
  for (const auto& entry : state->entries) {
    if (entry.evidence.module() == evidence.module() &&
        entry.evidence.revision().digest() == evidence.revision().digest()) {
      return BorrowEvidenceRepositoryRejected{ir::IrFailureKind::AdditionalFact};
    }
  }
  if (state->entries.size() == state->expectedEntryCount) {
    return BorrowEvidenceRepositoryRejected{ir::IrFailureKind::AdditionalFact};
  }
  zc::Array<uint8_t> expanded;
  ZC_IF_SOME(value, module) { expanded = value.key().encode(); }
  BorrowEvidenceKey leaseKey{evidence.module(), evidence.revision()};
  const auto entryIndex = static_cast<uint32_t>(state->entries.size());
  state->entries.add(
      detail::BorrowEvidenceRepositoryState::Entry{zc::mv(expanded), zc::mv(evidence)});
  const auto& entry = state->entries[entryIndex];
  size_t insertion = 0;
  while (insertion < state->sortedIndices.size()) {
    const auto& existing = state->entries[state->sortedIndices[insertion]];
    if (lessBytes(entry.expandedModuleKey.asPtr(), existing.expandedModuleKey.asPtr()) ||
        (entry.expandedModuleKey.asPtr() == existing.expandedModuleKey.asPtr() &&
         lessBytes(entry.evidence.revision().digest().bytes(),
                   existing.evidence.revision().digest().bytes()))) {
      break;
    }
    ++insertion;
  }
  state->sortedIndices.add(entryIndex);
  for (size_t index = state->sortedIndices.size() - 1; index > insertion; --index) {
    state->sortedIndices[index] = state->sortedIndices[index - 1];
  }
  state->sortedIndices[insertion] = entryIndex;
  return VerifiedBorrowEvidenceLease(state->context, state->repository, leaseKey);
}

BorrowEvidenceRepositoryCapability BorrowEvidenceRepository::capability() const noexcept {
  return BorrowEvidenceRepositoryCapability(state->context, state->repository, state.addRef());
}

zc::Maybe<VerifiedBorrowEvidenceLease> BorrowEvidenceRepository::lease(
    identity::ModuleId module, const BorrowEvidenceRevision& revision) const noexcept {
  if (!module.belongsTo(state->context)) return zc::none;
  for (const auto index : state->sortedIndices) {
    const auto& entry = state->entries[index];
    if (entry.evidence.module() == module &&
        entry.evidence.revision().digest() == revision.digest()) {
      return VerifiedBorrowEvidenceLease(state->context, state->repository,
                                         BorrowEvidenceKey{module, revision});
    }
  }
  return zc::none;
}

}  // namespace zomlang::compiler::driver::borrow_evidence
