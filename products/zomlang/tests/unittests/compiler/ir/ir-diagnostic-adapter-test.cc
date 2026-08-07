// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/ir/ir-diagnostic-adapter.h"

#include "zc/ztest/test.h"
#include "zomlang/compiler/diagnostics/diagnostic-info.h"
#include "zomlang/compiler/source/manager.h"
#include "zomlang/tests/unittests/compiler/test-semantic-identities.h"

namespace zomlang::compiler::ir {
namespace {

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

identity::SourceSpan sourceSpan() {
  using namespace tests::test_identity_detail;
  auto sourceKey = tests::test_identity_detail::source();
  auto snapshot = identity::ImmutableSourceSnapshot::from(sourceKey.clone(),
                                                          zc::heapArray<uint8_t>(1, uint8_t{0}));
  ZC_REQUIRE(snapshot != zc::none);
  ZC_IF_SOME(value, snapshot) {
    auto span = value.span(0, 1);
    ZC_REQUIRE(span != zc::none);
    ZC_IF_SOME(admitted, span) {
      ZC_EXPECT(admitted.belongsTo(sourceKey));
      return zc::mv(admitted);
    }
  }
  ZC_FAIL_REQUIRE("source span fixture was rejected");
}

class UnusedIdentityResolver final : public IrFailureIdentityResolver {
public:
  ExpandedIrIdentityResult expand(identity::ModuleId) const override {
    ZC_FAIL_REQUIRE("session-owned diagnostic fixture unexpectedly expanded a module");
  }
  ExpandedIrIdentityResult expand(identity::DefId) const override {
    ZC_FAIL_REQUIRE("session-owned diagnostic fixture unexpectedly expanded a definition");
  }
  ExpandedIrIdentityResult expand(InstanceId) const override {
    ZC_FAIL_REQUIRE("session-owned diagnostic fixture unexpectedly expanded an instance");
  }
};

IrFailureFact targetSelectionFact(IrRejectedBranch branch, IrFailureKind kind,
                                  zc::Maybe<identity::SourceSpan>&& sourceSpan, uint32_t ordinal) {
  UnusedIdentityResolver identities;
  auto fallback = IrFailureFallbackContext::from(
      IrFailurePhase::TargetSelection, IrFailureOwner::session(emptyContextFingerprint()));
  ZC_REQUIRE(fallback != zc::none);
  zc::Maybe<IrFailureSite> noSite;
  zc::Vector<uint32_t> path;
  path.add(ordinal);
  auto descriptor = IrFailureDescriptor::decoded(branch, IrFailurePhase::TargetSelection, kind,
                                                 IrFailureOwner::session(emptyContextFingerprint()),
                                                 zc::mv(noSite), IrFailureDetail::none(),
                                                 zc::mv(sourceSpan), zc::mv(path), ordinal);
  ZC_IF_SOME(context, fallback) {
    auto result = IrFailureFactory::admit(zc::mv(descriptor), context, identities);
    ZC_REQUIRE(result.is<AcceptedIrFailureDescriptor>());
    return zc::mv(result.get<AcceptedIrFailureDescriptor>().fact);
  }
  ZC_UNREACHABLE
}

identity::IdentityInvariant identityFailure(uint32_t ordinal) {
  zc::Maybe<zc::Array<uint8_t>> noStructural;
  zc::Maybe<identity::UnbrandedSourceRange> noRange;
  auto result = identity::IdentityInvariant::from(
      identity::IdentityInvariantKind::InvalidHandle, identity::IdentityAllocationPhase::Definition,
      zc::mv(noStructural), zc::mv(noRange), identity::IdentityApiSite::HandleLookup, ordinal);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("identity diagnostic fixture was rejected");
}

diagnostics::DiagID expectedPhaseDiagnostic(IrFailurePhase phase) {
  using diagnostics::DiagID;
  switch (phase) {
    case IrFailurePhase::CheckedModuleAssembly:
      return DiagID::CheckedModuleInvariant;
    case IrFailurePhase::HirConstruction:
    case IrFailurePhase::HirVerification:
      return DiagID::HirInvariant;
    case IrFailurePhase::MirConstruction:
    case IrFailurePhase::BuiltMirVerification:
      return DiagID::BuiltMirInvariant;
    case IrFailurePhase::OwnershipProofValidation:
      return DiagID::OwnershipProofInvariant;
    case IrFailurePhase::CleanupElaboration:
    case IrFailurePhase::CoroutineElaboration:
    case IrFailurePhase::ExecutableMirVerification:
      return DiagID::ExecutableMirInvariant;
    case IrFailurePhase::Monomorphization:
    case IrFailurePhase::TargetSelection:
    case IrFailurePhase::LirLowering:
    case IrFailurePhase::LirVerification:
      return DiagID::LirInvariant;
    case IrFailurePhase::LlvmTranslation:
    case IrFailurePhase::ObjectEmission:
      return DiagID::BackendInvariant;
    case IrFailurePhase::FeatureBoundaryVerification:
      return DiagID::FeatureBoundaryInvariant;
  }
  ZC_UNREACHABLE
}

diagnostics::DiagID expectedDiagnostic(IrFailureKind kind, IrFailurePhase phase) {
  using diagnostics::DiagID;
  switch (kind) {
    case IrFailureKind::UnsupportedTargetCapability:
      return DiagID::TargetCapabilityUnavailable;
    case IrFailureKind::RecursiveInstantiation:
      return DiagID::RecursiveInstantiation;
    case IrFailureKind::InstantiationBudgetExceeded:
      return DiagID::InstantiationBudgetExceeded;
    case IrFailureKind::OutputCreationFailed:
      return DiagID::IrOutputCreationFailed;
    case IrFailureKind::CanonicalCodecMismatch:
      return DiagID::IrCanonicalCodecMismatch;
    case IrFailureKind::InputRevisionMismatch:
    case IrFailureKind::MissingRequiredFact:
    case IrFailureKind::AdditionalFact:
    case IrFailureKind::InvalidFact:
    case IrFailureKind::InvalidControlFlow:
    case IrFailureKind::InvalidPlace:
    case IrFailureKind::InvalidOwnershipProof:
    case IrFailureKind::InvalidCleanup:
    case IrFailureKind::InvalidCoroutineState:
    case IrFailureKind::InvalidSsa:
    case IrFailureKind::MissingTargetLayout:
    case IrFailureKind::InvalidAbi:
    case IrFailureKind::UnresolvedDispatch:
    case IrFailureKind::BackendTranslationRejected:
      return expectedPhaseDiagnostic(phase);
  }
  ZC_UNREACHABLE
}

void expectDiagnosticInfo(diagnostics::DiagID id, diagnostics::DiagSeverity severity,
                          zc::StringPtr headline, size_t arity) {
  const auto info = diagnostics::getDiagnosticInfo(id);
  ZC_EXPECT(info.severity == severity);
  ZC_EXPECT(info.message == headline);
  ZC_EXPECT(info.argCount == arity);
}

}  // namespace

ZC_TEST("IR diagnostic mapping is exhaustive across every closed phase and kind") {
  for (uint8_t phaseTag = 0x01; phaseTag <= 0x10; ++phaseTag) {
    const auto phase = static_cast<IrFailurePhase>(phaseTag);
    for (uint8_t kindTag = 0x01; kindTag <= 0x13; ++kindTag) {
      const auto kind = static_cast<IrFailureKind>(kindTag);
      ZC_EXPECT(irDiagnosticId(kind, phase) == expectedDiagnostic(kind, phase));
    }
    ZC_EXPECT(irDiagnosticId(IrFailureKind::CanonicalCodecMismatch, phase) ==
              diagnostics::DiagID::IrCanonicalCodecMismatch);
  }
}

ZC_TEST("IR diagnostic registrations have exact RFC 0010 severity headline and arity") {
  using diagnostics::DiagID;
  using diagnostics::DiagSeverity;
  expectDiagnosticInfo(DiagID::IrOutputCreationFailed, DiagSeverity::kError,
                       "IR emission could not create its output stream"_zcc, 0);
  expectDiagnosticInfo(DiagID::TargetCapabilityUnavailable, DiagSeverity::kError,
                       "The selected target does not support the required compiler operation"_zcc,
                       0);
  expectDiagnosticInfo(DiagID::RecursiveInstantiation, DiagSeverity::kError,
                       "Generic instantiation is recursively expanding"_zcc, 0);
  expectDiagnosticInfo(DiagID::InstantiationBudgetExceeded, DiagSeverity::kError,
                       "Generic instantiation exceeds the configured compiler limit"_zcc, 0);
  expectDiagnosticInfo(DiagID::CheckedModuleInvariant, DiagSeverity::kFatal,
                       "Internal checked-module invariant violated ({0} occurrence(s))"_zcc, 1);
  expectDiagnosticInfo(DiagID::HirInvariant, DiagSeverity::kFatal,
                       "Internal HIR invariant violated ({0} occurrence(s))"_zcc, 1);
  expectDiagnosticInfo(DiagID::BuiltMirInvariant, DiagSeverity::kFatal,
                       "Internal Built MIR invariant violated ({0} occurrence(s))"_zcc, 1);
  expectDiagnosticInfo(DiagID::OwnershipProofInvariant, DiagSeverity::kFatal,
                       "Internal ownership proof invariant violated ({0} occurrence(s))"_zcc, 1);
  expectDiagnosticInfo(DiagID::ExecutableMirInvariant, DiagSeverity::kFatal,
                       "Internal executable MIR invariant violated ({0} occurrence(s))"_zcc, 1);
  expectDiagnosticInfo(DiagID::LirInvariant, DiagSeverity::kFatal,
                       "Internal LIR invariant violated ({0} occurrence(s))"_zcc, 1);
  expectDiagnosticInfo(DiagID::BackendInvariant, DiagSeverity::kFatal,
                       "Internal backend invariant violated ({0} occurrence(s))"_zcc, 1);
  expectDiagnosticInfo(DiagID::IrCanonicalCodecMismatch, DiagSeverity::kFatal,
                       "Internal IR canonical encoding is invalid ({0} occurrence(s))"_zcc, 1);
  expectDiagnosticInfo(DiagID::FeatureBoundaryInvariant, DiagSeverity::kFatal,
                       "Internal feature-boundary invariant violated ({0} occurrence(s))"_zcc, 1);
}

ZC_TEST("IR invariant grouping uses adjacent mapped diagnostic and exact validated span") {
  zc::Maybe<identity::SourceSpan> noFirstSpan;
  zc::Maybe<identity::SourceSpan> noSecondSpan;
  zc::Maybe<identity::SourceSpan> noCodecSpan;
  zc::Maybe<identity::SourceSpan> distinctSpan = sourceSpan();
  zc::Vector<IrFailureFact> facts;
  facts.add(targetSelectionFact(IrRejectedBranch::IrInvariantRejected, IrFailureKind::InvalidFact,
                                zc::mv(noFirstSpan), 2));
  facts.add(targetSelectionFact(IrRejectedBranch::IrInvariantRejected,
                                IrFailureKind::MissingRequiredFact, zc::mv(noSecondSpan), 1));
  facts.add(targetSelectionFact(IrRejectedBranch::IrInvariantRejected, IrFailureKind::InvalidFact,
                                zc::mv(distinctSpan), 3));
  facts.add(targetSelectionFact(IrRejectedBranch::IrInvariantRejected,
                                IrFailureKind::CanonicalCodecMismatch, zc::mv(noCodecSpan), 4));
  auto sorted = SortedIrInvariantFailureFacts::from(zc::mv(facts));
  ZC_REQUIRE(sorted != zc::none);
  ZC_IF_SOME(values, sorted) {
    auto groups = groupIrInvariantFailures(values);
    ZC_REQUIRE(groups.size() == 3);
    ZC_EXPECT(groups[0].diagnosticId() == diagnostics::DiagID::LirInvariant);
    ZC_EXPECT(groups[0].diagnosticSpan() == zc::none);
    ZC_EXPECT(groups[0].occurrenceCount() == 2);
    ZC_EXPECT(groups[0].facts().size() == 2);
    ZC_EXPECT(groups[0].facts()[0].structuralFieldPath().size() == 1);
    ZC_EXPECT(groups[0].facts()[0].traversalOrdinal() == 1);
    ZC_EXPECT(groups[1].diagnosticId() == diagnostics::DiagID::LirInvariant);
    ZC_EXPECT(groups[1].diagnosticSpan() != zc::none);
    ZC_EXPECT(groups[1].occurrenceCount() == 1);
    ZC_EXPECT(groups[2].diagnosticId() == diagnostics::DiagID::IrCanonicalCodecMismatch);
    ZC_EXPECT(groups[2].occurrenceCount() == 1);

    source::SourceManager sourceManager;
    diagnostics::DiagnosticEngine engine(sourceManager);
    emitIrDiagnosticGroups(engine, groups.asPtr());
    ZC_EXPECT(engine.errorCount() == 2);
  }
}

ZC_TEST("IR capability grouping deduplicates one canonical root and retains every expansion") {
  zc::Maybe<identity::SourceSpan> noFirstSpan;
  zc::Maybe<identity::SourceSpan> noSecondSpan;
  zc::Vector<IrFailureFact> facts;
  facts.add(targetSelectionFact(IrRejectedBranch::CapabilityRejected,
                                IrFailureKind::UnsupportedTargetCapability, zc::mv(noFirstSpan),
                                2));
  facts.add(targetSelectionFact(IrRejectedBranch::CapabilityRejected,
                                IrFailureKind::UnsupportedTargetCapability, zc::mv(noSecondSpan),
                                1));
  auto sorted = SortedCapabilityFailureFacts::from(zc::mv(facts));
  ZC_REQUIRE(sorted != zc::none);
  ZC_IF_SOME(values, sorted) {
    auto groups = groupIrCapabilityFailures(values);
    ZC_REQUIRE(groups.size() == 1);
    ZC_EXPECT(!groups[0].isInvariant());
    ZC_EXPECT(groups[0].diagnosticId() == diagnostics::DiagID::TargetCapabilityUnavailable);
    ZC_EXPECT(groups[0].occurrenceCount() == 2);
    ZC_EXPECT(groups[0].facts().size() == 2);

    source::SourceManager sourceManager;
    diagnostics::DiagnosticEngine engine(sourceManager);
    emitIrDiagnosticGroups(engine, groups.asPtr());
    ZC_EXPECT(engine.errorCount() == 1);
  }
}

ZC_TEST("IR identity branch delegates to the canonical RFC 0011 diagnostic adapter") {
  zc::Vector<identity::IdentityInvariant> facts;
  facts.add(identityFailure(2));
  facts.add(identityFailure(1));
  auto sorted = SortedIdentityInvariantFacts::from(zc::mv(facts));
  ZC_REQUIRE(sorted != zc::none);
  ZC_IF_SOME(values, sorted) {
    source::SourceManager sourceManager;
    diagnostics::DiagnosticEngine engine(sourceManager);
    emitIrIdentityInvariantFailures(engine, values);
    ZC_EXPECT(engine.errorCount() == 1);
    ZC_EXPECT(values.facts().size() == 2);
  }
}

}  // namespace zomlang::compiler::ir
