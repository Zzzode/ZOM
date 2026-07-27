// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/ir/ir-failure.h"

#include "zc/ztest/test.h"

namespace zomlang::compiler::ir {
namespace {

identity::IdentityInvariant invalidIdentity(uint32_t ordinal = 0) {
  zc::Maybe<zc::Array<uint8_t>> noStructural;
  zc::Maybe<identity::UnbrandedSourceRange> noRange;
  auto result = identity::IdentityInvariant::from(
      identity::IdentityInvariantKind::InvalidHandle, identity::IdentityAllocationPhase::Definition,
      zc::mv(noStructural), zc::mv(noRange), identity::IdentityApiSite::HandleLookup, ordinal);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("valid identity invariant fixture was rejected");
}

class RejectingIdentityResolver final : public IrFailureIdentityResolver {
public:
  ExpandedIrIdentityResult expand(identity::ModuleId) const override {
    return RejectedIrIdentityValue{invalidIdentity()};
  }
  ExpandedIrIdentityResult expand(identity::DefId) const override {
    return RejectedIrIdentityValue{invalidIdentity()};
  }
  ExpandedIrIdentityResult expand(InstanceId) const override {
    return RejectedIrIdentityValue{invalidIdentity()};
  }
};

struct GateSourceFailure final {
  uint32_t ordinal;
};

struct GateSourceFailureOrdering final {
  static bool less(const GateSourceFailure& left, const GateSourceFailure& right) noexcept {
    return left.ordinal < right.ordinal;
  }
};

identity::SemanticContextFingerprint emptyContextFingerprint() {
  zc::ArrayPtr<const identity::CompilationUnitIdentity> compilationUnits;
  zc::ArrayPtr<const identity::ToolchainSemanticContextInput> toolchainInputs;
  zc::ArrayPtr<const identity::PackageDependencyEdgeKey> packageEdges;
  zc::ArrayPtr<const identity::CrateKey> crates;
  zc::ArrayPtr<const identity::CrateDependencyEdgeKey> crateEdges;
  zc::ArrayPtr<const identity::SourceContentIdentity> sources;
  zc::ArrayPtr<const identity::ModuleKey> modules;
  auto result = identity::SemanticContextFingerprint::compute(
      compilationUnits, toolchainInputs, packageEdges, crates, crateEdges, sources, modules);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("empty semantic context fingerprint fixture was rejected");
}

bool expectedKind(IrRejectedBranch branch, IrFailurePhase phase, IrFailureKind kind) {
  if (branch == IrRejectedBranch::CapabilityRejected) {
    if (phase == IrFailurePhase::Monomorphization) {
      return kind == IrFailureKind::RecursiveInstantiation ||
             kind == IrFailureKind::InstantiationBudgetExceeded;
    }
    if (phase == IrFailurePhase::TargetSelection) {
      return kind == IrFailureKind::UnsupportedTargetCapability;
    }
    return phase == IrFailurePhase::ObjectEmission && kind == IrFailureKind::OutputCreationFailed;
  }
  if (branch != IrRejectedBranch::IrInvariantRejected) { return false; }
  const bool common = kind == IrFailureKind::InputRevisionMismatch ||
                      kind == IrFailureKind::MissingRequiredFact ||
                      kind == IrFailureKind::AdditionalFact || kind == IrFailureKind::InvalidFact ||
                      kind == IrFailureKind::CanonicalCodecMismatch;
  if (phase == IrFailurePhase::TargetSelection) {
    return kind == IrFailureKind::InputRevisionMismatch ||
           kind == IrFailureKind::MissingRequiredFact || kind == IrFailureKind::InvalidFact ||
           kind == IrFailureKind::CanonicalCodecMismatch;
  }
  if (common) { return true; }
  switch (phase) {
    case IrFailurePhase::CheckedModuleAssembly:
    case IrFailurePhase::TargetSelection:
    case IrFailurePhase::FeatureBoundaryVerification:
      return false;
    case IrFailurePhase::HirConstruction:
    case IrFailurePhase::HirVerification:
      return kind == IrFailureKind::InvalidControlFlow || kind == IrFailureKind::UnresolvedDispatch;
    case IrFailurePhase::MirConstruction:
    case IrFailurePhase::BuiltMirVerification:
      return kind == IrFailureKind::InvalidControlFlow || kind == IrFailureKind::InvalidPlace ||
             kind == IrFailureKind::UnresolvedDispatch;
    case IrFailurePhase::OwnershipProofValidation:
      return kind == IrFailureKind::InvalidPlace || kind == IrFailureKind::InvalidOwnershipProof;
    case IrFailurePhase::CleanupElaboration:
      return kind == IrFailureKind::InvalidControlFlow || kind == IrFailureKind::InvalidPlace ||
             kind == IrFailureKind::InvalidOwnershipProof || kind == IrFailureKind::InvalidCleanup;
    case IrFailurePhase::CoroutineElaboration:
    case IrFailurePhase::ExecutableMirVerification:
      return kind == IrFailureKind::InvalidControlFlow || kind == IrFailureKind::InvalidPlace ||
             kind == IrFailureKind::InvalidOwnershipProof ||
             kind == IrFailureKind::InvalidCleanup || kind == IrFailureKind::InvalidCoroutineState;
    case IrFailurePhase::Monomorphization:
      return kind == IrFailureKind::UnresolvedDispatch;
    case IrFailurePhase::LirLowering:
      return kind == IrFailureKind::InvalidSsa || kind == IrFailureKind::MissingTargetLayout ||
             kind == IrFailureKind::InvalidAbi || kind == IrFailureKind::UnresolvedDispatch;
    case IrFailurePhase::LirVerification:
      return kind == IrFailureKind::InvalidSsa || kind == IrFailureKind::MissingTargetLayout ||
             kind == IrFailureKind::InvalidAbi;
    case IrFailurePhase::LlvmTranslation:
      return kind == IrFailureKind::InvalidSsa || kind == IrFailureKind::MissingTargetLayout ||
             kind == IrFailureKind::InvalidAbi || kind == IrFailureKind::BackendTranslationRejected;
    case IrFailurePhase::ObjectEmission:
      return kind == IrFailureKind::InvalidAbi || kind == IrFailureKind::BackendTranslationRejected;
  }
  return false;
}

bool expectedSite(zc::Maybe<IrFailureSiteKind> site, IrFailureSiteKind kind) {
  ZC_IF_SOME(value, site) { return value == kind; }
  return false;
}

bool expectedNone(zc::Maybe<IrFailureSiteKind> site) { return site == zc::none; }

bool expectedOwnerSite(IrFailurePhase phase, IrFailureOwnerKind owner,
                       zc::Maybe<IrFailureSiteKind> site) {
  switch (phase) {
    case IrFailurePhase::CheckedModuleAssembly:
      return owner == IrFailureOwnerKind::Module &&
             (expectedNone(site) || expectedSite(site, IrFailureSiteKind::FrontendHandoff));
    case IrFailurePhase::HirConstruction:
      return (owner == IrFailureOwnerKind::Module &&
              (expectedNone(site) || expectedSite(site, IrFailureSiteKind::FrontendHandoff))) ||
             (owner == IrFailureOwnerKind::Definition &&
              (expectedNone(site) || expectedSite(site, IrFailureSiteKind::FrontendHandoff) ||
               expectedSite(site, IrFailureSiteKind::Hir)));
    case IrFailurePhase::HirVerification:
      return (owner == IrFailureOwnerKind::Module && expectedNone(site)) ||
             (owner == IrFailureOwnerKind::Definition &&
              (expectedNone(site) || expectedSite(site, IrFailureSiteKind::Hir)));
    case IrFailurePhase::MirConstruction:
      return owner == IrFailureOwnerKind::Definition &&
             (expectedNone(site) || expectedSite(site, IrFailureSiteKind::Hir) ||
              expectedSite(site, IrFailureSiteKind::Mir));
    case IrFailurePhase::BuiltMirVerification:
    case IrFailurePhase::OwnershipProofValidation:
    case IrFailurePhase::CleanupElaboration:
    case IrFailurePhase::CoroutineElaboration:
    case IrFailurePhase::ExecutableMirVerification:
      return owner == IrFailureOwnerKind::Definition &&
             (expectedNone(site) || expectedSite(site, IrFailureSiteKind::Mir));
    case IrFailurePhase::Monomorphization:
      return owner == IrFailureOwnerKind::Instance &&
             (expectedNone(site) || expectedSite(site, IrFailureSiteKind::Hir) ||
              expectedSite(site, IrFailureSiteKind::Mir));
    case IrFailurePhase::TargetSelection:
      return owner == IrFailureOwnerKind::Session && expectedNone(site);
    case IrFailurePhase::FeatureBoundaryVerification:
      return (owner == IrFailureOwnerKind::Module || owner == IrFailureOwnerKind::Definition) &&
             (expectedNone(site) || expectedSite(site, IrFailureSiteKind::FrontendHandoff));
    case IrFailurePhase::LirLowering:
      return owner == IrFailureOwnerKind::Instance &&
             (expectedNone(site) || expectedSite(site, IrFailureSiteKind::Mir) ||
              expectedSite(site, IrFailureSiteKind::Lir));
    case IrFailurePhase::LirVerification:
      return owner == IrFailureOwnerKind::Instance &&
             (expectedNone(site) || expectedSite(site, IrFailureSiteKind::Lir));
    case IrFailurePhase::LlvmTranslation:
      return owner == IrFailureOwnerKind::Instance &&
             (expectedNone(site) || expectedSite(site, IrFailureSiteKind::Lir) ||
              expectedSite(site, IrFailureSiteKind::Backend));
    case IrFailurePhase::ObjectEmission:
      return (owner == IrFailureOwnerKind::Session || owner == IrFailureOwnerKind::Instance) &&
             (expectedNone(site) || expectedSite(site, IrFailureSiteKind::Backend));
  }
  return false;
}

bool expectedDetail(IrRejectedBranch branch, IrFailureKind kind, IrFailureDetailKind detail) {
  if (branch == IrRejectedBranch::IrInvariantRejected) {
    return detail == IrFailureDetailKind::None;
  }
  if (kind == IrFailureKind::RecursiveInstantiation) {
    return detail == IrFailureDetailKind::InstantiationCycle;
  }
  if (kind == IrFailureKind::InstantiationBudgetExceeded) {
    return detail == IrFailureDetailKind::InstantiationBudget;
  }
  return detail == IrFailureDetailKind::None;
}

bool expectedLegal(const IrFailureDescriptorShape& shape) {
  return expectedKind(shape.branch, shape.phase, shape.kind) &&
         expectedOwnerSite(shape.phase, shape.owner, shape.site) &&
         expectedDetail(shape.branch, shape.kind, shape.detail);
}

IrFailureFact sessionFact(IrRejectedBranch branch, IrFailureKind kind, uint32_t ordinal) {
  RejectingIdentityResolver identities;
  auto owner = IrFailureOwner::session(emptyContextFingerprint());
  auto fallbackOwner = IrFailureOwner::session(emptyContextFingerprint());
  auto fallback =
      IrFailureFallbackContext::from(IrFailurePhase::TargetSelection, zc::mv(fallbackOwner));
  ZC_REQUIRE(fallback != zc::none);
  zc::Maybe<IrFailureSite> noSite;
  zc::Maybe<identity::SourceSpan> noSpan;
  zc::Vector<uint32_t> path;
  path.add(ordinal);
  auto descriptor = IrFailureDescriptor::decoded(
      branch, IrFailurePhase::TargetSelection, kind, zc::mv(owner), zc::mv(noSite),
      IrFailureDetail::none(), zc::mv(noSpan), zc::mv(path), ordinal);
  ZC_IF_SOME(context, fallback) {
    auto result = IrFailureFactory::admit(zc::mv(descriptor), context, identities);
    ZC_REQUIRE(result.is<AcceptedIrFailureDescriptor>());
    return zc::mv(result.get<AcceptedIrFailureDescriptor>().fact);
  }
  ZC_UNREACHABLE
}

}  // namespace

ZC_TEST("IR failure closed tags match RFC 0010") {
  ZC_EXPECT(static_cast<uint8_t>(IrFailureOwnerKind::Instance) == 0x04);
  ZC_EXPECT(static_cast<uint8_t>(IrFailureSiteKind::Backend) == 0x05);
  ZC_EXPECT(static_cast<uint8_t>(IrFailurePhase::FeatureBoundaryVerification) == 0x10);
  ZC_EXPECT(static_cast<uint8_t>(IrFailureKind::CanonicalCodecMismatch) == 0x13);
  ZC_EXPECT(static_cast<uint8_t>(IrFailureDetailKind::InstantiationBudget) == 0x03);
  for (uint8_t tag = 0x01; tag <= 0x0a; ++tag) {
    const auto operation = static_cast<BackendOperation>(tag);
    ZC_EXPECT(operation >= BackendOperation::TranslateType);
    ZC_EXPECT(operation <= BackendOperation::EmitObject);
  }
}

ZC_TEST("IR failure matrix accepts every legal coordinate and rejects every illegal coordinate") {
  uint32_t legalCount = 0;
  for (uint8_t branchTag = 0x01; branchTag <= 0x02; ++branchTag) {
    for (uint8_t phaseTag = 0x01; phaseTag <= 0x10; ++phaseTag) {
      for (uint8_t kindTag = 0x01; kindTag <= 0x13; ++kindTag) {
        for (uint8_t ownerTag = 0x01; ownerTag <= 0x04; ++ownerTag) {
          for (uint8_t siteTag = 0x00; siteTag <= 0x05; ++siteTag) {
            for (uint8_t detailTag = 0x01; detailTag <= 0x03; ++detailTag) {
              zc::Maybe<IrFailureSiteKind> site;
              if (siteTag != 0) { site = static_cast<IrFailureSiteKind>(siteTag); }
              const IrFailureDescriptorShape shape{static_cast<IrRejectedBranch>(branchTag),
                                                   static_cast<IrFailurePhase>(phaseTag),
                                                   static_cast<IrFailureKind>(kindTag),
                                                   static_cast<IrFailureOwnerKind>(ownerTag),
                                                   site,
                                                   static_cast<IrFailureDetailKind>(detailTag)};
              const bool expected = expectedLegal(shape);
              ZC_EXPECT(isLegalIrFailureShape(shape) == expected);
              if (expected) { ++legalCount; }
            }
          }
        }
      }
    }
  }
  ZC_EXPECT(legalCount != 0);

  const IrFailureDescriptorShape invalidPhase{IrRejectedBranch::IrInvariantRejected,
                                              static_cast<IrFailurePhase>(0xff),
                                              IrFailureKind::InvalidFact,
                                              IrFailureOwnerKind::Session,
                                              zc::none,
                                              IrFailureDetailKind::None};
  ZC_EXPECT(!isLegalIrFailureShape(invalidPhase));
}

ZC_TEST("Invalid descriptor takes one legal no-location fallback without recursive admission") {
  RejectingIdentityResolver identities;
  auto fallback = IrFailureFallbackContext::from(
      IrFailurePhase::TargetSelection, IrFailureOwner::session(emptyContextFingerprint()));
  ZC_REQUIRE(fallback != zc::none);
  zc::Maybe<IrFailureSite> noSite;
  zc::Maybe<identity::SourceSpan> noSpan;
  zc::Vector<uint32_t> path;
  path.add(7);
  auto invalid = IrFailureDescriptor::decoded(
      IrRejectedBranch::IrInvariantRejected, IrFailurePhase::TargetSelection,
      IrFailureKind::InvalidControlFlow, IrFailureOwner::session(emptyContextFingerprint()),
      zc::mv(noSite), IrFailureDetail::none(), zc::mv(noSpan), zc::mv(path), 9);
  ZC_IF_SOME(context, fallback) {
    auto result = IrFailureFactory::admit(zc::mv(invalid), context, identities);
    ZC_REQUIRE(result.is<FallbackIrFailureDescriptor>());
    const auto& fallbackResult = result.get<FallbackIrFailureDescriptor>();
    ZC_EXPECT(fallbackResult.fact.kind() == IrFailureKind::InvalidFact);
    ZC_EXPECT(fallbackResult.fact.phase() == IrFailurePhase::TargetSelection);
    ZC_EXPECT(fallbackResult.fact.site() == zc::none);
    ZC_EXPECT(fallbackResult.fact.sourceSpan() == zc::none);
    ZC_EXPECT(fallbackResult.fact.detail().kind() == IrFailureDetailKind::None);
    ZC_REQUIRE(fallbackResult.fact.structuralFieldPath().size() == 1);
    ZC_EXPECT(fallbackResult.fact.structuralFieldPath()[0] == 7);
  }
}

ZC_TEST("Valid failure shape with an invalid semantic handle selects identity rejection") {
  RejectingIdentityResolver identities;
  identity::ModuleId invalidModule;
  auto fallback = IrFailureFallbackContext::from(IrFailurePhase::CheckedModuleAssembly,
                                                 IrFailureOwner::module(invalidModule));
  ZC_REQUIRE(fallback != zc::none);
  zc::Maybe<IrFailureSite> noSite;
  zc::Maybe<identity::SourceSpan> noSpan;
  zc::Vector<uint32_t> noPath;
  auto descriptor = IrFailureDescriptor::decoded(
      IrRejectedBranch::IrInvariantRejected, IrFailurePhase::CheckedModuleAssembly,
      IrFailureKind::InvalidFact, IrFailureOwner::module(invalidModule), zc::mv(noSite),
      IrFailureDetail::none(), zc::mv(noSpan), zc::mv(noPath), 0);
  ZC_IF_SOME(context, fallback) {
    auto result = IrFailureFactory::admit(zc::mv(descriptor), context, identities);
    ZC_REQUIRE(result.is<IdentityRejectedIrFailureDescriptor>());
    ZC_EXPECT(result.get<IdentityRejectedIrFailureDescriptor>().failure.kind() ==
              identity::IdentityInvariantKind::InvalidHandle);
  }
}

ZC_TEST("IR operation result branches are mutually exclusive and failures sort canonically") {
  zc::Vector<IrFailureFact> invariantFacts;
  invariantFacts.add(
      sessionFact(IrRejectedBranch::IrInvariantRejected, IrFailureKind::InvalidFact, 2));
  invariantFacts.add(
      sessionFact(IrRejectedBranch::IrInvariantRejected, IrFailureKind::InvalidFact, 1));
  auto sortedInvariants = SortedIrInvariantFailureFacts::from(zc::mv(invariantFacts));
  ZC_REQUIRE(sortedInvariants != zc::none);
  ZC_IF_SOME(failures, sortedInvariants) {
    auto result = IrOperationResult<uint32_t>::irInvariantRejected(zc::mv(failures));
    ZC_EXPECT(result.isIrInvariantRejected());
    ZC_EXPECT(!result.isVerified());
    ZC_REQUIRE(result.invariantFailures().facts().size() == 2);
    ZC_EXPECT(result.invariantFailures().facts()[0].traversalOrdinal() == 1);
  }

  auto verified = IrOperationResult<uint32_t>::verified(42);
  ZC_EXPECT(verified.isVerified());
  ZC_EXPECT(verified.verifiedValue() == 42);

  zc::Vector<IrFailureFact> capabilityFacts;
  capabilityFacts.add(sessionFact(IrRejectedBranch::CapabilityRejected,
                                  IrFailureKind::UnsupportedTargetCapability, 1));
  auto sortedCapabilities = SortedCapabilityFailureFacts::from(zc::mv(capabilityFacts));
  ZC_REQUIRE(sortedCapabilities != zc::none);
  ZC_IF_SOME(failures, sortedCapabilities) {
    auto result = IrOperationResult<uint32_t>::capabilityRejected(zc::mv(failures));
    ZC_EXPECT(result.isCapabilityRejected());
    ZC_EXPECT(!result.isIrInvariantRejected());
  }

  zc::Vector<identity::IdentityInvariant> identityFacts;
  identityFacts.add(invalidIdentity(2));
  identityFacts.add(invalidIdentity(1));
  auto sortedIdentities = SortedIdentityInvariantFacts::from(zc::mv(identityFacts));
  ZC_REQUIRE(sortedIdentities != zc::none);
  ZC_IF_SOME(failures, sortedIdentities) {
    auto result = IrOperationResult<uint32_t>::identityInvariantRejected(zc::mv(failures));
    ZC_EXPECT(result.isIdentityInvariantRejected());
    ZC_EXPECT(result.identityFailures().facts()[0].inputTraversalOrdinal() == 1);
  }
}

ZC_TEST("Unified verification failures sort identity facts before IR invariant facts") {
  zc::Vector<IrVerificationFailure> failures;
  failures.add(IrVerificationFailure::ir(
      sessionFact(IrRejectedBranch::IrInvariantRejected, IrFailureKind::InvalidFact, 1)));
  failures.add(IrVerificationFailure::identity(invalidIdentity()));
  auto sorted = SortedIrVerificationFailures::from(zc::mv(failures));
  ZC_REQUIRE(sorted != zc::none);
  ZC_IF_SOME(values, sorted) {
    ZC_REQUIRE(values.facts().size() == 2);
    ZC_EXPECT(values.facts()[0].isIdentity());
    ZC_EXPECT(!values.facts()[1].isIdentity());
  }
}

ZC_TEST("Feature boundary result is the sole typed source-rejecting extension") {
  zc::Vector<GateSourceFailure> failures;
  failures.add(GateSourceFailure{2});
  failures.add(GateSourceFailure{1});
  using Result =
      FeatureBoundaryVerificationResult<uint32_t, GateSourceFailure, GateSourceFailureOrdering>;
  auto sorted = Result::SourceFailures::from(zc::mv(failures));
  ZC_REQUIRE(sorted != zc::none);
  ZC_IF_SOME(values, sorted) {
    auto result = Result::sourceRejected(zc::mv(values));
    ZC_EXPECT(result.isSourceRejected());
    ZC_EXPECT(!result.isVerified());
    auto consumed = zc::mv(result).takeSourceFailures();
    ZC_EXPECT(consumed.facts()[0].ordinal == 1);
  }
}

}  // namespace zomlang::compiler::ir
