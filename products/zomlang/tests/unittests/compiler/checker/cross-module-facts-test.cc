// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/checker/cross-module-facts.h"

#include "zc/core/encoding.h"
#include "zc/ztest/test.h"
#include "zomlang/tests/unittests/compiler/test-semantic-identities.h"

namespace zomlang::compiler::checker::cross_module {
namespace {

identity::Sha256Digest repeatedDigest(uint8_t byte) {
  uint8_t bytes[32];
  for (auto& value : bytes) { value = byte; }
  ZC_IF_SOME(digest, identity::Sha256Digest::fromBytes(zc::arrayPtr(bytes))) { return digest; }
  ZC_FAIL_REQUIRE("invalid cross-module digest fixture");
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

identity::SemanticImportBindingKey semanticBinding(zc::StringPtr localName) {
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
      identity::DefinitionNamespace::Type, scalar<identity::DeclaredDefinitionName>(localName));
  return zc::mv(ZC_REQUIRE_NONNULL(binding));
}

identity::ModuleId moduleIdentity() {
  using namespace tests::test_identity_detail;
  identity::SemanticContextFactory factory;
  auto context = factory.issue();
  ZC_REQUIRE(context != zc::none);
  ZC_IF_SOME(owner, context) {
    auto authorities = identity::CanonicalIdentityInternerSet::create(factory, owner);
    ZC_REQUIRE(authorities != zc::none);
    ZC_IF_SOME(interner, authorities) {
      auto result = interner.internModule(owner, module());
      ZC_REQUIRE(result.is<identity::ModuleId>());
      return result.get<identity::ModuleId>();
    }
  }
  ZC_UNREACHABLE;
}

}  // namespace

ZC_TEST("ImportedSignatureViewRevision.ReproducesRfc0005FramingOracle") {
  const uint8_t requester[] = {0xa1};
  const uint8_t module[] = {0xb2};
  const zc::ArrayPtr<const uint8_t> records[] = {module};
  auto revision =
      ImportedSignatureViewRevision::computeFramed(repeatedDigest(0x00), requester, records);
  ZC_REQUIRE(revision != zc::none);
  ZC_IF_SOME(value, revision) {
    ZC_EXPECT(zc::encodeHex(value.digest().bytes()) ==
              "16c9b731c156061752980de67bd85d410e3cc1aaed336ad592a0fc842bf1cb86"_zc);
  }
}

ZC_TEST("ImportedSignatureModuleCanonicalCodec.EncodesCompleteRfc0005Record") {
  const uint8_t source[] = {0xa1};
  const zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> emptyRecords;
  auto record = ImportedSignatureModuleCanonicalCodec::encodeFramed(
      SignatureViewOrigin::ExplicitImport, source, repeatedDigest(0x22), repeatedDigest(0x33),
      emptyRecords, emptyRecords, emptyRecords, emptyRecords);
  ZC_REQUIRE(record != zc::none);
  ZC_IF_SOME(bytes, record) {
    ZC_EXPECT(zc::encodeHex(bytes.asPtr()) ==
              "01a1"
              "2222222222222222222222222222222222222222222222222222222222222222"
              "3333333333333333333333333333333333333333333333333333333333333333"
              "0000000000000000000000000000000000000000000000000000000000000000"_zc);
  }
}

ZC_TEST("ImportedSignatureModuleCanonicalCodec.RejectsNonCanonicalRecords") {
  const uint8_t source[] = {0xa1};
  const uint8_t high[] = {0xb2};
  const uint8_t low[] = {0xa1};
  const zc::ArrayPtr<const uint8_t> reversed[] = {high, low};
  const zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> emptyRecords;
  ZC_EXPECT(ImportedSignatureModuleCanonicalCodec::encodeFramed(
                SignatureViewOrigin::NamespaceImport, source, repeatedDigest(0x22),
                repeatedDigest(0x33), reversed, emptyRecords, emptyRecords,
                emptyRecords) == zc::none);
}

ZC_TEST("CoherenceViewRevision.ReproducesRfc0015FramingOracle") {
  const uint8_t module[] = {0xc3};
  const uint8_t impl[] = {0xd4};
  const uint8_t marker[] = {0xe5};
  const zc::ArrayPtr<const uint8_t> modules[] = {module};
  const zc::ArrayPtr<const uint8_t> impls[] = {impl};
  const zc::ArrayPtr<const uint8_t> markers[] = {marker};
  auto revision = CoherenceViewRevision::computeFramed(repeatedDigest(0x00), repeatedDigest(0x77),
                                                       modules, impls, markers);
  ZC_REQUIRE(revision != zc::none);
  ZC_IF_SOME(value, revision) {
    ZC_EXPECT(zc::encodeHex(value.digest().bytes()) ==
              "3a10103a288cbe53af9bc6c02366309ed7c123805b9e7611affa17f8bd922c63"_zc);
  }
}

ZC_TEST("CoherenceViewRevision.ReproducesRfc0015ImplIntegrationOracle") {
  const uint8_t module[] = {0xc3};
  auto impl = decoded(
      "a1000000000000002e7a6f6d2e696d706c2d7061747465726e00a100000000000000"
      "01110000000008b20000000000000001110000000008b2000000000000000109c309b20000"
      "000000000001c30000000000000000010000000000000000d4"_zc);
  const zc::ArrayPtr<const uint8_t> modules[] = {module};
  const zc::ArrayPtr<const uint8_t> impls[] = {impl.asPtr()};
  const zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> emptyRecords;
  auto revision = CoherenceViewRevision::computeFramed(repeatedDigest(0x00), repeatedDigest(0x77),
                                                       modules, impls, emptyRecords);
  ZC_REQUIRE(revision != zc::none);
  ZC_IF_SOME(value, revision) {
    ZC_EXPECT(zc::encodeHex(value.digest().bytes()) ==
              "d348a593580f2309175fd0aefc773babc432559d00a25310cde2bccee1564283"_zc);
  }
}

ZC_TEST("CoherenceViewRevision.ReproducesEndToEndPolicyLineageOracle") {
  auto moduleRecord =
      decoded("a1701f41323c3e469b94012bfb98191c9b2b68bdd7be4f52697d2178227c37dd9f"_zc);
  const zc::ArrayPtr<const uint8_t> modules[] = {moduleRecord.asPtr()};
  const zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> emptyRecords;
  auto revision = CoherenceViewRevision::computeFramed(
      repeatedDigest(0x00),
      digest("15329853e2faae147a2f5ca73c85a58c4084c70faa3d1faef278c856fd75067b"_zc), modules,
      emptyRecords, emptyRecords);
  ZC_REQUIRE(revision != zc::none);
  ZC_IF_SOME(value, revision) {
    ZC_EXPECT(zc::encodeHex(value.digest().bytes()) ==
              "35177411cd1ae5d74ae1fc0ff0fc98c83e9e564809341460fbf960c3655b507b"_zc);
  }
}

ZC_TEST("CoherenceViewRevision.BindsEveryParentAndProjection") {
  const uint8_t module[] = {0xc3};
  const uint8_t impl[] = {0xd4};
  const uint8_t marker[] = {0xe5};
  const uint8_t mutation[] = {0xf6};
  const zc::ArrayPtr<const uint8_t> modules[] = {module};
  const zc::ArrayPtr<const uint8_t> impls[] = {impl};
  const zc::ArrayPtr<const uint8_t> markers[] = {marker};
  const zc::ArrayPtr<const uint8_t> mutated[] = {mutation};
  auto baseline = CoherenceViewRevision::computeFramed(repeatedDigest(0x00), repeatedDigest(0x77),
                                                       modules, impls, markers);
  ZC_REQUIRE(baseline != zc::none);
  ZC_IF_SOME(value, baseline) {
    const auto expectDifferent = [&](const identity::Sha256Digest& context,
                                     const identity::Sha256Digest& policy,
                                     zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> moduleValues,
                                     zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> implValues,
                                     zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> markerValues) {
      auto changed = CoherenceViewRevision::computeFramed(context, policy, moduleValues, implValues,
                                                          markerValues);
      ZC_REQUIRE(changed != zc::none);
      ZC_IF_SOME(changedValue, changed) { ZC_EXPECT(changedValue.digest() != value.digest()); }
    };
    expectDifferent(repeatedDigest(0x01), repeatedDigest(0x77), modules, impls, markers);
    expectDifferent(repeatedDigest(0x00), repeatedDigest(0x78), modules, impls, markers);
    expectDifferent(repeatedDigest(0x00), repeatedDigest(0x77), mutated, impls, markers);
    expectDifferent(repeatedDigest(0x00), repeatedDigest(0x77), modules, mutated, markers);
    expectDifferent(repeatedDigest(0x00), repeatedDigest(0x77), modules, impls, mutated);
  }
}

ZC_TEST("CrossModuleRevisions.RejectNonCanonicalRecordOrder") {
  const uint8_t requester[] = {0xa1};
  const uint8_t high[] = {0xb2};
  const uint8_t low[] = {0xa1};
  const zc::ArrayPtr<const uint8_t> reversed[] = {high, low};
  ZC_EXPECT(ImportedSignatureViewRevision::computeFramed(repeatedDigest(0x00), requester,
                                                         reversed) == zc::none);

  const zc::ArrayPtr<const uint8_t> empty;
  const zc::ArrayPtr<const uint8_t> emptyEntry[] = {empty};
  const zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> noRecords;
  ZC_EXPECT(CoherenceViewRevision::computeFramed(repeatedDigest(0x00), repeatedDigest(0x77),
                                                 reversed, noRecords, noRecords) == zc::none);
  ZC_EXPECT(CoherenceViewRevision::computeFramed(repeatedDigest(0x00), repeatedDigest(0x77),
                                                 emptyEntry, noRecords, noRecords) == zc::none);
}

ZC_TEST("SignatureRootBinding.AcceptsOnlyDefinitionAndSemanticImportIdentities") {
  auto definitions = tests::makeTestDefinitionIds(2);
  auto definition = binder::BindingTarget::definition(definitions[0]);
  auto sameDefinition = binder::BindingTarget::definition(definitions[0]);
  auto otherDefinition = binder::BindingTarget::definition(definitions[1]);
  auto semantic = binder::BindingTarget::semanticImport(semanticBinding("local"_zc));
  auto sameSemantic = semantic.clone();
  auto otherSemantic = binder::BindingTarget::semanticImport(semanticBinding("other"_zc));
  auto module = binder::BindingTarget::module(moduleIdentity());

  ZC_EXPECT(module_interface::isSignatureRootBinding(definition));
  ZC_EXPECT(module_interface::isSignatureRootBinding(semantic));
  ZC_EXPECT(!module_interface::isSignatureRootBinding(module));
  ZC_EXPECT(module_interface::sameSignatureRootBinding(definition, sameDefinition));
  ZC_EXPECT(!module_interface::sameSignatureRootBinding(definition, otherDefinition));
  ZC_EXPECT(module_interface::sameSignatureRootBinding(semantic, sameSemantic));
  ZC_EXPECT(!module_interface::sameSignatureRootBinding(semantic, otherSemantic));
  ZC_EXPECT(!module_interface::sameSignatureRootBinding(definition, semantic));

  ImportedDefinitionBindingSelection selection{semantic.clone(),
                                               binder::BindingTarget::definition(definitions[0]),
                                               SignatureViewOrigin::ExplicitImport};
  auto cloned = selection.clone();
  ZC_EXPECT(module_interface::sameSignatureRootBinding(selection.requesterBinding,
                                                       cloned.requesterBinding));
}

}  // namespace zomlang::compiler::checker::cross_module
