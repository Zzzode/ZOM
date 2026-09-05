// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/ir/diagnostics/ir-diagnostic-adapter.h"

#include "compiler/diagnostics/core/diagnostic-info.h"
#include "compiler/identity/crypto/sha256.h"
#include "compiler/identity/diagnostics/identity-diagnostic-projector.h"
#include "compiler/ir/diagnostics/ir-diagnostic-projector.h"
#include "compiler/ir/link/link-plan-codec.h"
#include "compiler/source/manager.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::ir {
namespace {

identity::ContextFingerprint emptyContextFingerprint() {
  zc::ArrayPtr<const identity::CompilationUnitIdentity> compilationUnits;
  zc::ArrayPtr<const identity::ToolchainSemanticContextInput> toolchainInputs;
  zc::ArrayPtr<const identity::PackageDependencyEdgeKey> packageEdges;
  zc::ArrayPtr<const identity::CrateKey> crates;
  zc::ArrayPtr<const identity::CrateDependencyEdgeKey> crateEdges;
  zc::ArrayPtr<const identity::SourceContentIdentity> sources;
  zc::ArrayPtr<const identity::ModuleKey> modules;
  return ZC_REQUIRE_NONNULL(identity::ContextFingerprint::compute(
      compilationUnits, toolchainInputs, packageEdges, crates, crateEdges, sources, modules));
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

IrFailureFact targetSelectionFact(IrRejectedBranch branch, IrFailureKind kind, uint32_t ordinal) {
  UnusedIdentityResolver identities;
  auto fallback = IrFailureFallbackContext::from(
      IrFailurePhase::TargetSelection, IrFailureOwner::session(emptyContextFingerprint()));
  ZC_REQUIRE(fallback != zc::none);
  zc::Maybe<IrFailureSite> noSite;
  zc::Maybe<identity::SourceSpan> noSpan;
  zc::Vector<uint32_t> path;
  path.add(ordinal);
  auto descriptor = IrFailureDescriptor::decoded(branch, IrFailurePhase::TargetSelection, kind,
                                                 IrFailureOwner::session(emptyContextFingerprint()),
                                                 zc::mv(noSite), IrFailureDetail::none(),
                                                 zc::mv(noSpan), zc::mv(path), ordinal);
  auto result =
      IrFailureFactory::admit(zc::mv(descriptor), ZC_REQUIRE_NONNULL(fallback), identities);
  ZC_REQUIRE(result.is<AcceptedIrFailureDescriptor>());
  return zc::mv(result.get<AcceptedIrFailureDescriptor>().fact);
}

identity::IdentityInvariant identityFailure(uint32_t ordinal) {
  zc::Maybe<zc::Array<uint8_t>> noStructural;
  zc::Maybe<identity::UnbrandedSourceRange> noRange;
  return ZC_REQUIRE_NONNULL(identity::IdentityInvariant::from(
      identity::IdentityInvariantKind::InvalidHandle, identity::IdentityAllocationPhase::Definition,
      zc::mv(noStructural), zc::mv(noRange), identity::IdentityApiSite::HandleLookup, ordinal));
}

void expectDiagnosticInfo(diagnostics::DiagID id, zc::StringPtr headline) {
  const auto info = diagnostics::getDiagnosticInfo(id);
  ZC_EXPECT(info.severity == diagnostics::DiagSeverity::kError);
  ZC_EXPECT(info.message == headline);
  ZC_EXPECT(info.argCount == 0);
}

identity::Sha256Digest linkDigestOf(zc::StringPtr seed) {
  return ZC_REQUIRE_NONNULL(identity::sha256(seed.asBytes()));
}

LinkInputRecord linkInput(zc::StringPtr path, LinkInputRole role, zc::StringPtr digestSeed,
                          uint64_t byteCount) {
  return ZC_REQUIRE_NONNULL(LinkInputRecord::make(path, role, linkDigestOf(digestSeed), byteCount));
}

zc::Array<LinkInputRecord> oneLinkInput(LinkInputRecord&& record) {
  auto builder = zc::heapArrayBuilder<LinkInputRecord>(1);
  builder.add(zc::mv(record));
  return builder.finish();
}

ExecutableInspectionProfile linkInspectionProfile() {
  auto symbols = zc::heapArrayBuilder<zc::String>(1);
  symbols.add(zc::str("__zom_runtime"));
  return ZC_REQUIRE_NONNULL(ExecutableInspectionProfile::make(
      ObjectFormat::Elf, ExecutableMachine::X86_64, 64, symbols.finish(), zc::str("__zom_")));
}

ToolchainClosureRecord linkMinimalClosure() {
  const uint8_t targetIdentity[] = {0x74, 0x67, 0x74};
  auto crtObjects =
      oneLinkInput(linkInput("/sysroot/lib/crt1.o", LinkInputRole::CrtObject, "crt1", 1024));
  auto defaultLibraries =
      oneLinkInput(linkInput("/sysroot/lib/libc.so", LinkInputRole::DefaultLibrary, "libc", 2048));
  return ZC_REQUIRE_NONNULL(ToolchainClosureRecord::make(
      zc::arrayPtr(targetIdentity, 3), "/sysroot"_zc, LinkerDriverKind::ElfDriver,
      "/sysroot/bin/cc"_zc, linkDigestOf("cc"), 4096, zc::mv(crtObjects),
      zc::mv(defaultLibraries)));
}

template <typename VerifiedValue>
void routeRejection(diagnostics::DiagnosticEngine& engine, basic::BoundedIncidentSet& incidents,
                    const IrOperationResult<VerifiedValue>& result) {
  if (result.isCapabilityRejected()) {
    auto groups = groupIrCapabilityFailures(result.capabilityFailures());
    emitIrCapabilityDiagnosticGroups(engine, groups.asPtr());
  } else if (result.isIrInvariantRejected()) {
    auto projected = IrDiagnosticProjector::projectAll(result.invariantFailures());
    ZC_REQUIRE(projected != zc::none && incidents.merge(ZC_ASSERT_NONNULL(projected)),
               "IR incident projection must fit");
  } else if (result.isIdentityInvariantRejected()) {
    auto projected =
        identity::IdentityDiagnosticProjector::projectAll(result.identityFailures().facts());
    ZC_REQUIRE(projected != zc::none && incidents.merge(ZC_ASSERT_NONNULL(projected)),
               "identity incident projection must fit");
  }
}

}  // namespace

ZC_TEST("IR capability diagnostic registrations remain user actionable") {
  using diagnostics::DiagID;
  expectDiagnosticInfo(DiagID::IrOutputCreationFailed,
                       "IR emission could not create its output stream"_zcc);
  expectDiagnosticInfo(DiagID::TargetCapabilityUnavailable,
                       "The selected target does not support the required compiler operation"_zcc);
  expectDiagnosticInfo(DiagID::RecursiveInstantiation,
                       "Generic instantiation is recursively expanding"_zcc);
  expectDiagnosticInfo(DiagID::InstantiationBudgetExceeded,
                       "Generic instantiation exceeds the configured compiler limit"_zcc);
}

ZC_TEST("IR capability grouping deduplicates one canonical root") {
  zc::Vector<IrFailureFact> facts;
  facts.add(targetSelectionFact(IrRejectedBranch::CapabilityRejected,
                                IrFailureKind::UnsupportedTargetCapability, 2));
  facts.add(targetSelectionFact(IrRejectedBranch::CapabilityRejected,
                                IrFailureKind::UnsupportedTargetCapability, 1));
  auto sorted = SortedCapabilityFailureFacts::from(zc::mv(facts));
  ZC_REQUIRE(sorted != zc::none);
  auto groups = groupIrCapabilityFailures(ZC_ASSERT_NONNULL(sorted));
  ZC_REQUIRE(groups.size() == 1);
  ZC_EXPECT(groups[0].diagnosticId() == diagnostics::DiagID::TargetCapabilityUnavailable);
  ZC_EXPECT(groups[0].occurrenceCount() == 2);

  source::SourceManager sourceManager;
  diagnostics::DiagnosticEngine engine(sourceManager);
  emitIrCapabilityDiagnosticGroups(engine, groups.asPtr());
  ZC_EXPECT(engine.errorCount() == 1);
}

ZC_TEST("IR invariant projection aggregates equal shapes without source evidence") {
  zc::Vector<IrFailureFact> facts;
  facts.add(
      targetSelectionFact(IrRejectedBranch::IrInvariantRejected, IrFailureKind::InvalidFact, 2));
  facts.add(
      targetSelectionFact(IrRejectedBranch::IrInvariantRejected, IrFailureKind::InvalidFact, 1));
  facts.add(targetSelectionFact(IrRejectedBranch::IrInvariantRejected,
                                IrFailureKind::CanonicalCodecMismatch, 4));
  auto sorted = SortedIrInvariantFailureFacts::from(zc::mv(facts));
  ZC_REQUIRE(sorted != zc::none);
  auto incidents = ZC_REQUIRE_NONNULL(IrDiagnosticProjector::projectAll(ZC_ASSERT_NONNULL(sorted)));
  ZC_REQUIRE(incidents.size() == 2);
  ZC_EXPECT(incidents.descriptors()[0].domain() == basic::CompilerIncidentDomain::Ir);
  ZC_EXPECT(incidents.descriptors()[0].phase().tag() ==
            static_cast<uint32_t>(IrFailurePhase::TargetSelection));
  ZC_EXPECT(incidents.descriptors()[0].kind().tag() ==
            static_cast<uint32_t>(IrFailureKind::InvalidFact));
  ZC_EXPECT(incidents.descriptors()[0].occurrences() == 2);
}

ZC_TEST("IR identity branch projects to internal incidents") {
  zc::Vector<identity::IdentityInvariant> facts;
  facts.add(identityFailure(2));
  facts.add(identityFailure(1));
  auto sorted = SortedIdentityInvariantFacts::from(zc::mv(facts));
  ZC_REQUIRE(sorted != zc::none);
  auto incidents = ZC_REQUIRE_NONNULL(
      identity::IdentityDiagnosticProjector::projectAll(ZC_ASSERT_NONNULL(sorted).facts()));
  ZC_REQUIRE(incidents.size() == 1);
  ZC_EXPECT(incidents.descriptors()[0].domain() == basic::CompilerIncidentDomain::Identity);
  ZC_EXPECT(incidents.descriptors()[0].occurrences() == 2);
}

ZC_TEST("Target selection contract failures project to IR incidents") {
  const auto descriptor =
      IrDiagnosticProjector::project(TargetSelectionVerificationIssue::RegistryRevisionMismatch);
  ZC_EXPECT(descriptor.domain() == basic::CompilerIncidentDomain::Ir);
  ZC_EXPECT(descriptor.phase().tag() == static_cast<uint32_t>(IrFailurePhase::TargetSelection));
  ZC_EXPECT(descriptor.kind().tag() == static_cast<uint32_t>(IrFailureKind::InputRevisionMismatch));
}

ZC_TEST("A rejected link plan materializes its invariant as an incident") {
  auto result = LinkPlanVerifier::verify(ExecutableLinkRequest{
      linkMinimalClosure(), linkInspectionProfile(), zc::Array<uint8_t>(),
      oneLinkInput(linkInput("/out/app.o", LinkInputRole::ObjectArtifact, "obj", 512)),
      oneLinkInput(linkInput("/sysroot/lib/zomrt.o", LinkInputRole::RuntimeObject, "rt", 256)),
      zc::str("/out"), zc::str("/out/app")});
  ZC_REQUIRE(result.isIrInvariantRejected());

  source::SourceManager sourceManager;
  diagnostics::DiagnosticEngine engine(sourceManager);
  basic::BoundedIncidentSet incidents;
  routeRejection(engine, incidents, result);
  ZC_EXPECT(engine.errorCount() == 0);
  ZC_REQUIRE(incidents.size() == 1);
  ZC_EXPECT(incidents.descriptors()[0].domain() == basic::CompilerIncidentDomain::Backend);
  ZC_EXPECT(incidents.descriptors()[0].phase().tag() ==
            static_cast<uint32_t>(IrFailurePhase::LinkPlanConstruction));
  ZC_EXPECT(incidents.descriptors()[0].kind().tag() ==
            static_cast<uint32_t>(IrFailureKind::MissingRequiredFact));
  ZC_EXPECT(incidents.descriptors()[0].producer().tag() ==
            static_cast<uint32_t>(IrIncidentProducer::PhaseBoundary));
}

ZC_TEST("A verified link plan routes no diagnostics or incidents") {
  auto result = LinkPlanVerifier::verify(ExecutableLinkRequest{
      linkMinimalClosure(), linkInspectionProfile(), zc::heapArray<uint8_t>({0x7a, 0x6f, 0x6d}),
      oneLinkInput(linkInput("/out/app.o", LinkInputRole::ObjectArtifact, "obj", 512)),
      oneLinkInput(linkInput("/sysroot/lib/zomrt.o", LinkInputRole::RuntimeObject, "rt", 256)),
      zc::str("/out"), zc::str("/out/app")});
  ZC_REQUIRE(result.isVerified());

  source::SourceManager sourceManager;
  diagnostics::DiagnosticEngine engine(sourceManager);
  basic::BoundedIncidentSet incidents;
  routeRejection(engine, incidents, result);
  ZC_EXPECT(engine.errorCount() == 0);
  ZC_EXPECT(incidents.empty());
}

}  // namespace zomlang::compiler::ir
