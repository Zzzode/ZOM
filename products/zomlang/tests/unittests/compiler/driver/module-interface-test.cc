// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/driver/module-interface.h"

#include "zc/core/encoding.h"
#include "zc/ztest/test.h"
#include "zomlang/tests/unittests/compiler/test-semantic-identities.h"

namespace zomlang::compiler::driver {
namespace {

identity::Sha256Digest repeatedDigest(uint8_t byte) {
  uint8_t bytes[32];
  for (auto& value : bytes) { value = byte; }
  ZC_IF_SOME(digest, identity::Sha256Digest::fromBytes(zc::arrayPtr(bytes))) { return digest; }
  ZC_FAIL_REQUIRE("invalid module-interface digest fixture");
}

zc::Array<uint8_t> decoded(zc::StringPtr hex) {
  auto bytes = zc::decodeHex(hex);
  ZC_REQUIRE(bytes != zc::none);
  return zc::mv(ZC_REQUIRE_NONNULL(bytes));
}

identity::Sha256Digest digest(zc::StringPtr hex) {
  auto bytes = decoded(hex);
  auto value = identity::Sha256Digest::fromBytes(bytes.asPtr());
  ZC_REQUIRE(value != zc::none);
  return ZC_REQUIRE_NONNULL(value);
}

identity::ModuleResolutionPolicyKey resolutionPolicy() {
  auto value = identity::ModuleResolutionPolicyKey::from(
      identity::UnicodeNormalizationPolicy::Nfc, identity::CaseComparisonPolicy::CaseSensitive,
      identity::SymlinkHandlingPolicy::ResolveThenConfine,
      identity::ModuleContainmentPolicy::DeclaredRootsOnly,
      identity::LocalModuleLookupPolicy::RequesterAncestryAndCrateRoot,
      identity::DependencyAliasLookupPolicy::ExactFirstSegment,
      identity::PreludeLookupPolicy::ConfiguredCratePrelude,
      identity::ModuleCandidateSelectionPolicy::AllDistinctMatchesNoPrecedence);
  return zc::mv(ZC_REQUIRE_NONNULL(value));
}

identity::SemanticImportBindingKey semanticBinding() {
  using namespace tests::test_identity_detail;
  zc::Vector<identity::ModulePathSegment> path;
  path.add(scalar<identity::ModulePathSegment>("dependency"_zc));
  zc::Maybe<zc::Vector<identity::ModulePathSegment>> retainedPath(zc::mv(path));
  zc::Maybe<identity::DependencyAlias> alias(scalar<identity::DependencyAlias>("dependency"_zc));
  auto resolution =
      identity::ModuleResolutionKey::from(module(), identity::ModuleDependencyKind::Import,
                                          zc::mv(retainedPath), zc::mv(alias), resolutionPolicy());
  ZC_REQUIRE(resolution != zc::none);
  auto binding = identity::SemanticImportBindingKey::from(
      module(), zc::mv(ZC_REQUIRE_NONNULL(resolution)), identity::SemanticImportOperation::Import,
      identity::DefinitionNamespace::Type, scalar<identity::DeclaredDefinitionName>("source"_zc),
      identity::DefinitionNamespace::Type, scalar<identity::DeclaredDefinitionName>("local"_zc));
  return zc::mv(ZC_REQUIRE_NONNULL(binding));
}

identity::ModuleId moduleIdentity() {
  using namespace tests::test_identity_detail;
  identity::SemanticContextFactory factory;
  auto context = factory.issue();
  auto registries =
      identity::SemanticIdentityRegistrySet::create(factory, ZC_REQUIRE_NONNULL(context));
  ZC_IF_SOME(values, registries) {
    ZC_REQUIRE(values.collectCompilationUnit(identity::CompilationUnitIdentity::userPackage(
                   package())) == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(values.freezeCompilationUnits() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(values.collectCrate(crate()) == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(values.freezeCrates() == identity::FrozenRegistryFailure::None);
    auto snapshot = identity::ImmutableSourceSnapshot::from(tests::test_identity_detail::source(),
                                                            zc::heapArray<uint8_t>(1, uint8_t{0}));
    ZC_REQUIRE(snapshot != zc::none);
    ZC_REQUIRE(values.collectSourceFile(zc::mv(ZC_REQUIRE_NONNULL(snapshot))) ==
               identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(values.freezeSourceFiles() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(values.collectModule(module()) == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(values.freezeModules() == identity::FrozenRegistryFailure::None);
    return ZC_REQUIRE_NONNULL(values.modules().find(module()));
  }
  ZC_UNREACHABLE;
}

binder::ExportSurfaceRevision surfaceRevision() {
  const uint8_t framed[] = {0x01};
  auto revision = binder::ExportSurfaceRevision::computeFramed(repeatedDigest(0x10), framed, framed,
                                                               framed, framed);
  return ZC_REQUIRE_NONNULL(revision);
}

}  // namespace

ZC_TEST("ModuleInterfaceRevision.ReproducesRfc0015FramingOracle") {
  const uint8_t moduleBytes[] = {0xa1};
  const zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> emptyRecords;
  auto revision = module_interface::ModuleInterfaceRevision::computeFramed(
      repeatedDigest(0x00), moduleBytes, repeatedDigest(0x22), repeatedDigest(0x33),
      repeatedDigest(0x44), repeatedDigest(0x77), repeatedDigest(0x55), repeatedDigest(0x66),
      emptyRecords, emptyRecords, emptyRecords, emptyRecords, emptyRecords, emptyRecords,
      emptyRecords);

  ZC_REQUIRE(revision != zc::none);
  ZC_IF_SOME(value, revision) {
    ZC_EXPECT(zc::encodeHex(value.digest().bytes()) ==
              "3fc2e4ca1feccee881fb66622afcba7a0ba725c1aded5e4355e90fd56afb44b3"_zc);
  }
}

ZC_TEST("ModuleInterfaceRevision.RejectsEmptyOwnerAndNonCanonicalRecords") {
  const zc::ArrayPtr<const uint8_t> noModule;
  const zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> emptyRecords;
  auto missingOwner = module_interface::ModuleInterfaceRevision::computeFramed(
      repeatedDigest(0x00), noModule, repeatedDigest(0x22), repeatedDigest(0x33),
      repeatedDigest(0x44), repeatedDigest(0x77), repeatedDigest(0x55), repeatedDigest(0x66),
      emptyRecords, emptyRecords, emptyRecords, emptyRecords, emptyRecords, emptyRecords,
      emptyRecords);
  ZC_EXPECT(missingOwner == zc::none);

  const uint8_t moduleBytes[] = {0xa1};
  const uint8_t high[] = {0xb2};
  const uint8_t low[] = {0xa1};
  const zc::ArrayPtr<const uint8_t> reversed[] = {high, low};
  auto nonCanonical = module_interface::ModuleInterfaceRevision::computeFramed(
      repeatedDigest(0x00), moduleBytes, repeatedDigest(0x22), repeatedDigest(0x33),
      repeatedDigest(0x44), repeatedDigest(0x77), repeatedDigest(0x55), repeatedDigest(0x66),
      reversed, emptyRecords, emptyRecords, emptyRecords, emptyRecords, emptyRecords, emptyRecords);
  ZC_EXPECT(nonCanonical == zc::none);

  const zc::ArrayPtr<const uint8_t> empty;
  const zc::ArrayPtr<const uint8_t> emptyEntry[] = {empty};
  auto emptyProjection = module_interface::ModuleInterfaceRevision::computeFramed(
      repeatedDigest(0x00), moduleBytes, repeatedDigest(0x22), repeatedDigest(0x33),
      repeatedDigest(0x44), repeatedDigest(0x77), repeatedDigest(0x55), repeatedDigest(0x66),
      emptyEntry, emptyRecords, emptyRecords, emptyRecords, emptyRecords, emptyRecords,
      emptyRecords);
  ZC_EXPECT(emptyProjection == zc::none);
}

ZC_TEST("ModuleInterfaceRevision.ReproducesRfc0015ImplIntegrationOracle") {
  const uint8_t moduleBytes[] = {0xa1};
  auto implementation = decoded(
      "a1000000000000002e7a6f6d2e696d706c2d7061747465726e00a100000000000000"
      "01110000000008b20000000000000001110000000008b2000000000000000109c309b20000"
      "000000000001c30000000000000000010000000000000000d4"_zc);
  const zc::ArrayPtr<const uint8_t> implementations[] = {implementation.asPtr()};
  const zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> emptyRecords;
  auto revision = module_interface::ModuleInterfaceRevision::computeFramed(
      repeatedDigest(0x00), moduleBytes, repeatedDigest(0x22), repeatedDigest(0x33),
      repeatedDigest(0x44), repeatedDigest(0x77), repeatedDigest(0x55), repeatedDigest(0x66),
      emptyRecords, emptyRecords, emptyRecords, emptyRecords, emptyRecords, implementations,
      emptyRecords);
  ZC_REQUIRE(revision != zc::none);
  ZC_IF_SOME(value, revision) {
    ZC_EXPECT(zc::encodeHex(value.digest().bytes()) ==
              "74f1e27a55111c32ee12fb7507e8fb04774ce4bf594d16d1f00202f643c20fed"_zc);
  }
}

ZC_TEST("ModuleInterfaceRevision.ReproducesRfc0015InterfaceSelfOracle") {
  const uint8_t moduleBytes[] = {0xa1};
  auto definition = decoded("7a6f6d2e73656d616e7469632d747970652d6b65790010a1"_zc);
  const zc::ArrayPtr<const uint8_t> definitions[] = {definition.asPtr()};
  const zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> emptyRecords;
  auto revision = module_interface::ModuleInterfaceRevision::computeFramed(
      repeatedDigest(0x00), moduleBytes, repeatedDigest(0x22), repeatedDigest(0x33),
      repeatedDigest(0x44), repeatedDigest(0x77), repeatedDigest(0x55), repeatedDigest(0x66),
      emptyRecords, definitions, emptyRecords, emptyRecords, emptyRecords, emptyRecords,
      emptyRecords);
  ZC_REQUIRE(revision != zc::none);
  ZC_IF_SOME(value, revision) {
    ZC_EXPECT(zc::encodeHex(value.digest().bytes()) ==
              "85e77363ec5b543e8a2bb5eb2d86028ab5a31fd53a2829a522568f02bcdc2395"_zc);
  }
}

ZC_TEST("ModuleInterfaceRevision.ReproducesEndToEndPolicyLineageOracle") {
  const uint8_t moduleBytes[] = {0xa1};
  const zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> emptyRecords;
  auto revision = module_interface::ModuleInterfaceRevision::computeFramed(
      repeatedDigest(0x00), moduleBytes, repeatedDigest(0x22), repeatedDigest(0x33),
      digest("c665ba5bf2be40edc425a063842bee3ce7a6774efbbe64acb8d3824ecf4ec85b"_zc),
      digest("15329853e2faae147a2f5ca73c85a58c4084c70faa3d1faef278c856fd75067b"_zc),
      repeatedDigest(0x55), repeatedDigest(0x66), emptyRecords, emptyRecords, emptyRecords,
      emptyRecords, emptyRecords, emptyRecords, emptyRecords);
  ZC_REQUIRE(revision != zc::none);
  ZC_IF_SOME(value, revision) {
    ZC_EXPECT(zc::encodeHex(value.digest().bytes()) ==
              "55ae783a09d8cd6de1c317071a61e3263972440d35614c8f283295fc6b6f83ce"_zc);
  }
}

ZC_TEST("ModuleInterfaceRevision.CoversEveryProjectionSequence") {
  const uint8_t moduleBytes[] = {0xa1};
  const uint8_t root[] = {0xb1};
  const uint8_t definition[] = {0xb2};
  const uint8_t support[] = {0xb3};
  const uint8_t visible[] = {0xb4};
  const uint8_t exported[] = {0xb5};
  const uint8_t implementation[] = {0xb6};
  const uint8_t marker[] = {0xb7};
  const zc::ArrayPtr<const uint8_t> roots[] = {root};
  const zc::ArrayPtr<const uint8_t> definitions[] = {definition};
  const zc::ArrayPtr<const uint8_t> supports[] = {support};
  const zc::ArrayPtr<const uint8_t> visibles[] = {visible};
  const zc::ArrayPtr<const uint8_t> exports[] = {exported};
  const zc::ArrayPtr<const uint8_t> implementations[] = {implementation};
  const zc::ArrayPtr<const uint8_t> markers[] = {marker};
  auto revision = module_interface::ModuleInterfaceRevision::computeFramed(
      repeatedDigest(0x00), moduleBytes, repeatedDigest(0x22), repeatedDigest(0x33),
      repeatedDigest(0x44), repeatedDigest(0x77), repeatedDigest(0x55), repeatedDigest(0x66), roots,
      definitions, supports, visibles, exports, implementations, markers);
  ZC_REQUIRE(revision != zc::none);
  ZC_IF_SOME(value, revision) {
    ZC_EXPECT(zc::encodeHex(value.digest().bytes()) ==
              "a3f7cea452648db7086c42a42c72d6172c3f42fd9ae3538d884b350305800f39"_zc);
    auto recomputed = module_interface::ModuleInterfaceRevision::computeFramed(
        repeatedDigest(0x00), moduleBytes, repeatedDigest(0x22), repeatedDigest(0x33),
        repeatedDigest(0x44), repeatedDigest(0x77), repeatedDigest(0x55), repeatedDigest(0x66),
        roots, definitions, supports, visibles, exports, implementations, markers);
    ZC_REQUIRE(recomputed != zc::none);
    ZC_IF_SOME(second, recomputed) { ZC_EXPECT(second.digest() == value.digest()); }

    const uint8_t mutation[] = {0xc1};
    const zc::ArrayPtr<const uint8_t> mutated[] = {mutation};
    auto expectDifferent = [&](zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> rootValues,
                               zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> definitionValues,
                               zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> supportValues,
                               zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> visibleValues,
                               zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> exportValues,
                               zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> implValues,
                               zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> markerValues) {
      auto changed = module_interface::ModuleInterfaceRevision::computeFramed(
          repeatedDigest(0x00), moduleBytes, repeatedDigest(0x22), repeatedDigest(0x33),
          repeatedDigest(0x44), repeatedDigest(0x77), repeatedDigest(0x55), repeatedDigest(0x66),
          rootValues, definitionValues, supportValues, visibleValues, exportValues, implValues,
          markerValues);
      ZC_REQUIRE(changed != zc::none);
      ZC_IF_SOME(changedValue, changed) { ZC_EXPECT(changedValue.digest() != value.digest()); }
    };
    expectDifferent(mutated, definitions, supports, visibles, exports, implementations, markers);
    expectDifferent(roots, mutated, supports, visibles, exports, implementations, markers);
    expectDifferent(roots, definitions, mutated, visibles, exports, implementations, markers);
    expectDifferent(roots, definitions, supports, mutated, exports, implementations, markers);
    expectDifferent(roots, definitions, supports, visibles, mutated, implementations, markers);
    expectDifferent(roots, definitions, supports, visibles, exports, mutated, markers);
    expectDifferent(roots, definitions, supports, visibles, exports, implementations, mutated);

    const auto expectParentDifferent =
        [&](const identity::Sha256Digest& context, zc::ArrayPtr<const uint8_t> owner,
            const identity::Sha256Digest& source, const identity::Sha256Digest& surface,
            const identity::Sha256Digest& signature, const identity::Sha256Digest& policy,
            const identity::Sha256Digest& imported, const identity::Sha256Digest& borrow) {
          auto changed = module_interface::ModuleInterfaceRevision::computeFramed(
              context, owner, source, surface, signature, policy, imported, borrow, roots,
              definitions, supports, visibles, exports, implementations, markers);
          ZC_REQUIRE(changed != zc::none);
          ZC_IF_SOME(changedValue, changed) { ZC_EXPECT(changedValue.digest() != value.digest()); }
        };
    const uint8_t changedOwner[] = {0xa2};
    expectParentDifferent(repeatedDigest(0x01), moduleBytes, repeatedDigest(0x22),
                          repeatedDigest(0x33), repeatedDigest(0x44), repeatedDigest(0x77),
                          repeatedDigest(0x55), repeatedDigest(0x66));
    expectParentDifferent(repeatedDigest(0x00), changedOwner, repeatedDigest(0x22),
                          repeatedDigest(0x33), repeatedDigest(0x44), repeatedDigest(0x77),
                          repeatedDigest(0x55), repeatedDigest(0x66));
    expectParentDifferent(repeatedDigest(0x00), moduleBytes, repeatedDigest(0x23),
                          repeatedDigest(0x33), repeatedDigest(0x44), repeatedDigest(0x77),
                          repeatedDigest(0x55), repeatedDigest(0x66));
    expectParentDifferent(repeatedDigest(0x00), moduleBytes, repeatedDigest(0x22),
                          repeatedDigest(0x34), repeatedDigest(0x44), repeatedDigest(0x77),
                          repeatedDigest(0x55), repeatedDigest(0x66));
    expectParentDifferent(repeatedDigest(0x00), moduleBytes, repeatedDigest(0x22),
                          repeatedDigest(0x33), repeatedDigest(0x45), repeatedDigest(0x77),
                          repeatedDigest(0x55), repeatedDigest(0x66));
    expectParentDifferent(repeatedDigest(0x00), moduleBytes, repeatedDigest(0x22),
                          repeatedDigest(0x33), repeatedDigest(0x44), repeatedDigest(0x78),
                          repeatedDigest(0x55), repeatedDigest(0x66));
    expectParentDifferent(repeatedDigest(0x00), moduleBytes, repeatedDigest(0x22),
                          repeatedDigest(0x33), repeatedDigest(0x44), repeatedDigest(0x77),
                          repeatedDigest(0x56), repeatedDigest(0x66));
    expectParentDifferent(repeatedDigest(0x00), moduleBytes, repeatedDigest(0x22),
                          repeatedDigest(0x33), repeatedDigest(0x44), repeatedDigest(0x77),
                          repeatedDigest(0x55), repeatedDigest(0x67));
  }
}

ZC_TEST("ModuleInterfaceVerifiedTypes.HaveNoPublicEmptyConstructionPath") {
  static_assert(!__is_constructible(VerifiedModuleInterface));
  static_assert(!__is_constructible(checker::cross_module::ImportedSignatureView));
  static_assert(!__is_constructible(checker::coherence::CoherenceModuleInput));
}

ZC_TEST("SignatureRootAuthorization.CloneRetainsSemanticImportBinding") {
  auto binding = binder::BindingTarget::semanticImport(semanticBinding());
  module_interface::SignatureRootAuthorization root{
      binding.clone(),
      tests::testDefinition(0),
      binder::VisibilityEnvelope::external(),
      moduleIdentity(),
      surfaceRevision(),
      module_interface::SignatureAuthorizationOrigin(
          module_interface::LocalSignatureAuthorization{})};
  auto cloned = root.clone();
  ZC_EXPECT(module_interface::sameSignatureRootBinding(root.binding, cloned.binding));
  ZC_EXPECT(root.canonicalDefinition == cloned.canonicalDefinition);

  auto moduleBinding = binder::BindingTarget::module(moduleIdentity());
  ZC_EXPECT(!module_interface::isSignatureRootBinding(moduleBinding));
}

}  // namespace zomlang::compiler::driver
