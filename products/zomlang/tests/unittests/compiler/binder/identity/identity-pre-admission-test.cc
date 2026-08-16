// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/identity/identity-pre-admission.h"

#include "zc/ztest/test.h"
#include "zomlang/compiler/identity/source-snapshot.h"

namespace zomlang::compiler::binder {
namespace {

template <typename Scalar>
Scalar requireScalar(zc::StringPtr text) {
  auto value = Scalar::fromCanonical(text);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid pre-admission test scalar");
}

identity::Sha256Digest repeatedDigest(uint8_t byte) {
  uint8_t bytes[32];
  for (auto& value : bytes) { value = byte; }
  auto value = identity::Sha256Digest::fromBytes(zc::arrayPtr(bytes));
  ZC_IF_SOME(admitted, value) { return admitted; }
  ZC_FAIL_REQUIRE("invalid digest fixture");
}

identity::SortedFeatureSet packageFeatures() {
  zc::Vector<identity::FeatureName> features;
  auto value = identity::SortedFeatureSet::from(zc::mv(features));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid package feature fixture");
}

identity::SortedTargetFeatureSet targetFeatures() {
  zc::Vector<identity::TargetFeatureName> features;
  auto value = identity::SortedTargetFeatureSet::from(zc::mv(features));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid target feature fixture");
}

identity::PackageKey package(zc::StringPtr name = "a"_zc) {
  zc::Vector<identity::CanonicalPathSegment> path;
  auto root = identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(path));
  auto version = identity::ResolvedVersion::fromCanonical("0.0.0"_zc);
  ZC_IF_SOME(admittedVersion, version) {
    return identity::PackageKey::from(identity::CanonicalPackageSource::localPath(zc::mv(root)),
                                      requireScalar<identity::PackageName>(name),
                                      zc::mv(admittedVersion), packageFeatures());
  }
  ZC_FAIL_REQUIRE("invalid package fixture");
}

identity::CanonicalTargetSpecificationKey target() {
  auto value = identity::CanonicalTargetSpecificationKey::from(
      requireScalar<identity::TargetComponentName>("x"_zc),
      requireScalar<identity::TargetComponentName>("v"_zc),
      requireScalar<identity::TargetComponentName>("o"_zc),
      requireScalar<identity::TargetComponentName>("e"_zc),
      requireScalar<identity::TargetComponentName>("a"_zc), 64, identity::Endianness::Little,
      targetFeatures());
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid target fixture");
}

identity::CompilationConfigKey compilation() {
  zc::Maybe<identity::BuildScriptProducerKey> producer =
      identity::BuildScriptProducerKey::from(repeatedDigest(0x11));
  auto value = identity::CompilationConfigKey::from(
      identity::CompilationDomain::Target, target(),
      identity::SemanticCompilerOptionsKey::from(2026, true, false, false), zc::mv(producer));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid compilation fixture");
}

identity::CrateKey crate(zc::StringPtr packageName = "a"_zc) {
  auto value =
      identity::CrateKey::from(identity::CompilationUnitIdentity::userPackage(package(packageName)),
                               identity::CrateTargetKind::Library,
                               requireScalar<identity::TargetName>("lib"_zc), compilation());
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid crate fixture");
}

identity::ModuleKey module(zc::StringPtr packageName = "a"_zc) {
  zc::Vector<identity::ModulePathSegment> path;
  path.add(requireScalar<identity::ModulePathSegment>("m"_zc));
  auto value = identity::ModuleKey::from(crate(packageName), zc::mv(path));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid module fixture");
}

identity::SourceFileKey source(zc::StringPtr packageName = "a"_zc,
                               zc::StringPtr fileName = "main.zom"_zc) {
  zc::Vector<identity::CanonicalPathSegment> segments;
  segments.add(requireScalar<identity::CanonicalPathSegment>(fileName));
  auto path = identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(segments));
  return identity::SourceFileKey::from(crate(packageName),
                                       identity::SourceOriginKey::localFile(zc::mv(path)));
}

identity::ImmutableSourceSnapshot snapshot(identity::SourceFileKey&& sourceKey) {
  auto value = identity::ImmutableSourceSnapshot::from(zc::mv(sourceKey),
                                                       zc::heapArray("0123456789abcdef"_zcb));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid source snapshot fixture");
}

identity::SourceSpan span(const identity::ImmutableSourceSnapshot& value, uint64_t start,
                          uint64_t end) {
  auto result = value.span(start, end);
  ZC_IF_SOME(admitted, result) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid source span fixture");
}

IdentitySyntaxSiteKey siteKey(zc::ArrayPtr<const uint32_t> path,
                              zc::StringPtr fileName = "main.zom"_zc) {
  zc::Vector<uint32_t> ownedPath(path.size());
  ownedPath.addAll(path);
  auto value = IdentitySyntaxSiteKey::from(module(), source("a"_zc, fileName), zc::mv(ownedPath));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid identity syntax site key fixture");
}

identity::SemanticIdentifier identifier(zc::StringPtr text) {
  return requireScalar<identity::SemanticIdentifier>(text);
}

identity::CanonicalNameReference name(zc::StringPtr text) {
  zc::Vector<identity::SemanticIdentifier> suffix;
  suffix.add(identifier(text));
  auto value = identity::CanonicalNameReference::from(identity::CanonicalNameRoot::relative(),
                                                      zc::mv(suffix));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid canonical name fixture");
}

identity::CanonicalHeaderTypeSyntax namedType(zc::StringPtr text) {
  zc::Vector<identity::CanonicalHeaderTypeSyntax> arguments;
  return identity::CanonicalHeaderTypeSyntax::named(
      identity::CanonicalNamedHeaderType::from(name(text), zc::mv(arguments)));
}

identity::CanonicalBoundObligation obligation(zc::StringPtr bound = "Eq"_zc) {
  return identity::CanonicalBoundObligation::from(namedType("T"_zc), namedType(bound));
}

identity::ImplHeader implHeader(bool withObligation = false) {
  zc::Vector<identity::CanonicalHeaderTypeSyntax> arguments;
  auto trait = identity::CanonicalTraitReference::from(name("Trait"_zc), zc::mv(arguments));
  ZC_REQUIRE(trait != zc::none);
  ZC_IF_SOME(admittedTrait, trait) {
    zc::Vector<identity::CanonicalGenericParameter> generics;
    zc::Vector<identity::CanonicalBoundObligation> obligations;
    if (withObligation) { obligations.add(obligation()); }
    auto value = identity::ImplHeader::from(
        zc::mv(generics), identity::ImplPolarity::Positive, identity::ImplSafety::Safe,
        zc::mv(admittedTrait), namedType("T"_zc), zc::mv(obligations));
    ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  }
  ZC_FAIL_REQUIRE("invalid implementation header fixture");
}

identity::ImplIdentityRecord implRecord(bool withObligation = false) {
  zc::Vector<identity::EnclosingStableOwnerKey> owners;
  return identity::ImplIdentityRecord::from(module(), zc::mv(owners), implHeader(withObligation));
}

identity::OverloadHeaderAuthority overloadAuthority(zc::StringPtr functionName = "f"_zc) {
  zc::Maybe<identity::ReceiverShape> receiver;
  zc::Vector<identity::CanonicalGenericParameter> generics;
  zc::Vector<identity::CanonicalBoundObligation> obligations;
  zc::Vector<identity::CanonicalCallableParameter> parameters;
  zc::Maybe<zc::Vector<identity::CanonicalHeaderTypeSyntax>> raises;
  zc::Maybe<identity::ExternalAbi> abi;
  auto header = identity::OverloadHeader::from(
      identity::CallableHeaderKind::Function,
      requireScalar<identity::DeclaredDefinitionName>(functionName), zc::mv(receiver),
      zc::mv(generics), zc::mv(obligations), zc::mv(parameters),
      identity::CanonicalCallableResult::unit(), zc::mv(raises), zc::mv(abi));
  ZC_IF_SOME(admitted, header) { return identity::OverloadHeaderAuthority::from(zc::mv(admitted)); }
  ZC_FAIL_REQUIRE("invalid overload authority fixture");
}

identity::DefinitionIdentityRecord functionRecord(const identity::OverloadHeaderDigest& digest,
                                                  zc::StringPtr name = "f"_zc) {
  zc::Vector<identity::EnclosingStableOwnerKey> owners;
  zc::Maybe<identity::OverloadHeaderDigest> overload = digest.clone();
  auto value = identity::DefinitionIdentityRecord::from(
      module(), zc::mv(owners), identity::DefinitionKind::Function,
      identity::DefinitionNamespace::Value, requireScalar<identity::DeclaredDefinitionName>(name),
      zc::mv(overload));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid function identity record fixture");
}

identity::IdentityInternerSet admittedImplAuthorities(
    const identity::ImplIdentityRecord& record) {
  identity::SemanticContextFactory factory;
  auto context = factory.issue();
  ZC_REQUIRE(context != zc::none);
  ZC_IF_SOME(admittedContext, context) {
    auto authorities = identity::IdentityInternerSet::create(factory, admittedContext);
    ZC_REQUIRE(authorities != zc::none);
    ZC_IF_SOME(value, authorities) {
      const auto key = identity::ImplKey::compute(record);
      auto interned = value.internImplementation(admittedContext, key, record);
      ZC_REQUIRE(interned.is<identity::ImplId>());
      return zc::mv(value);
    }
  }
  ZC_FAIL_REQUIRE("failed to build implementation authority fixture");
}

}  // namespace

ZC_TEST("Identity syntax sites retain source-qualified structural paths and ranges") {
  const uint32_t path[] = {2, 7};
  auto key = siteKey(path);
  auto sourceSnapshot = snapshot(key.source().clone());
  auto site = IdentitySyntaxSite::from(key.clone(), span(sourceSnapshot, 3, 8));
  ZC_REQUIRE(site != zc::none);
  ZC_IF_SOME(value, site) {
    ZC_EXPECT(value.key().sameAs(key));
    ZC_EXPECT(value.range().byteStart() == 3);
    ZC_EXPECT(value.range().byteEnd() == 8);
    ZC_EXPECT(value.clone().encode().asPtr() == value.encode().asPtr());
  }

  auto foreignSnapshot = snapshot(source("a"_zc, "other.zom"_zc));
  ZC_EXPECT(IdentitySyntaxSite::from(key.clone(), span(foreignSnapshot, 0, 1)) == zc::none);
  zc::Vector<uint32_t> foreignPath;
  ZC_EXPECT(IdentitySyntaxSiteKey::from(module("a"_zc), source("b"_zc), zc::mv(foreignPath)) ==
            zc::none);
}

ZC_TEST("Duplicate bounds require ordered distinct sites in one module and source") {
  const uint32_t firstPath[] = {1, 0};
  const uint32_t duplicatePath[] = {1, 1};
  auto value =
      DuplicateBoundOccurrence::from(obligation(), siteKey(firstPath), siteKey(duplicatePath));
  ZC_REQUIRE(value != zc::none);
  ZC_IF_SOME(admitted, value) {
    ZC_EXPECT(admitted.clone().encode().asPtr() == admitted.encode().asPtr());
  }
  ZC_EXPECT(DuplicateBoundOccurrence::from(obligation(), siteKey(duplicatePath),
                                           siteKey(firstPath)) == zc::none);
  ZC_EXPECT(DuplicateBoundOccurrence::from(obligation(), siteKey(firstPath),
                                           siteKey(firstPath, "other.zom"_zc)) == zc::none);
}

ZC_TEST("Implementation candidates retain matching duplicate bounds and reject overload headers") {
  const uint32_t declarationPath[] = {1};
  const uint32_t firstPath[] = {1, 0};
  const uint32_t duplicatePath[] = {1, 1};
  zc::Vector<DuplicateBoundOccurrence> duplicates;
  auto duplicate =
      DuplicateBoundOccurrence::from(obligation(), siteKey(firstPath), siteKey(duplicatePath));
  ZC_IF_SOME(admitted, duplicate) { duplicates.add(zc::mv(admitted)); }
  zc::Maybe<identity::OverloadHeaderAuthority> noOverload;
  auto candidate = PreAdmissionIdentityCandidate::implementation(
      implRecord(true), zc::mv(noOverload), siteKey(declarationPath), zc::mv(duplicates));
  ZC_REQUIRE(candidate != zc::none);
  ZC_IF_SOME(admitted, candidate) {
    ZC_EXPECT(admitted.kind() == PreAdmissionIdentityKind::Implementation);
    ZC_EXPECT(admitted.implRecord() != zc::none);
    ZC_EXPECT(admitted.definitionRecord() == zc::none);
    ZC_EXPECT(admitted.duplicateBounds().size() == 1);
    ZC_EXPECT(admitted.clone().encode().asPtr() == admitted.encode().asPtr());
  }

  zc::Vector<DuplicateBoundOccurrence> noDuplicates;
  zc::Maybe<identity::OverloadHeaderAuthority> invalidOverload = overloadAuthority();
  ZC_EXPECT(PreAdmissionIdentityCandidate::implementation(implRecord(), zc::mv(invalidOverload),
                                                          siteKey(declarationPath),
                                                          zc::mv(noDuplicates)) == zc::none);
}

ZC_TEST("Definition candidates require the exact complete overload authority") {
  const uint32_t declarationPath[] = {1};
  auto authority = overloadAuthority();
  auto record = functionRecord(authority.digest());
  zc::Maybe<identity::OverloadHeaderAuthority> retained = authority.clone();
  zc::Vector<DuplicateBoundOccurrence> noDuplicates;
  auto candidate = PreAdmissionIdentityCandidate::definition(
      record.clone(), zc::mv(retained), siteKey(declarationPath), zc::mv(noDuplicates));
  ZC_REQUIRE(candidate != zc::none);
  ZC_IF_SOME(admitted, candidate) {
    ZC_EXPECT(admitted.kind() == PreAdmissionIdentityKind::Definition);
    ZC_EXPECT(admitted.definitionRecord() != zc::none);
    ZC_EXPECT(admitted.implRecord() == zc::none);
    ZC_EXPECT(admitted.overloadHeader() != zc::none);
    ZC_EXPECT(admitted.clone().encode().asPtr() == admitted.encode().asPtr());
  }

  zc::Maybe<identity::OverloadHeaderAuthority> absent;
  zc::Vector<DuplicateBoundOccurrence> absentDuplicates;
  ZC_EXPECT(PreAdmissionIdentityCandidate::definition(record.clone(), zc::mv(absent),
                                                      siteKey(declarationPath),
                                                      zc::mv(absentDuplicates)) == zc::none);

  auto wrongAuthority = overloadAuthority("g"_zc);
  zc::Maybe<identity::OverloadHeaderAuthority> mismatched = zc::mv(wrongAuthority);
  zc::Vector<DuplicateBoundOccurrence> mismatchedDuplicates;
  ZC_EXPECT(PreAdmissionIdentityCandidate::definition(zc::mv(record), zc::mv(mismatched),
                                                      siteKey(declarationPath),
                                                      zc::mv(mismatchedDuplicates)) == zc::none);
}

ZC_TEST("Implementation occurrence groups require exact authorities and canonical source order") {
  auto record = implRecord();
  auto authorityRecord = identity::ImplIdentityAuthority::from(record.clone());
  auto authorities = admittedImplAuthorities(record);
  auto authorityValue = authorities.implementation(authorityRecord.key());
  ZC_REQUIRE(authorityValue != zc::none);
  const uint32_t firstPath[] = {2};
  const uint32_t secondPath[] = {3};
  auto sourceSnapshot = snapshot(source());
  auto firstSite = IdentitySyntaxSite::from(siteKey(firstPath), span(sourceSnapshot, 1, 2));
  auto secondSite = IdentitySyntaxSite::from(siteKey(secondPath), span(sourceSnapshot, 4, 5));
  ZC_REQUIRE(firstSite != zc::none);
  ZC_REQUIRE(secondSite != zc::none);
  ZC_IF_SOME(first, firstSite) {
    ZC_IF_SOME(second, secondSite) {
      zc::Vector<IdentitySyntaxSite> sites;
      sites.add(first.clone());
      sites.add(second.clone());
      zc::Vector<ImplSourceOccurrenceKey> occurrences;
      occurrences.add(
          ImplSourceOccurrenceKey::from(authorityRecord.key().clone(), first.key().clone()));
      occurrences.add(
          ImplSourceOccurrenceKey::from(authorityRecord.key().clone(), second.key().clone()));
      ZC_IF_SOME(authorityEntry, authorityValue) {
        const auto authority = authorityEntry.handle();
        auto group = ImplIdentityOccurrenceGroup::from(authorities, authority, zc::mv(occurrences),
                                                       sites.asPtr());
        ZC_REQUIRE(group != zc::none);
        ZC_IF_SOME(admitted, group) {
          ZC_EXPECT(admitted.implementation() == authorityRecord.key());
          ZC_EXPECT(admitted.authority() == authority);
          ZC_EXPECT(admitted.occurrences().size() == 2);
          ZC_EXPECT(admitted.clone().occurrences()[1].site().sameAs(second.key()));
        }

        zc::Vector<ImplSourceOccurrenceKey> reversed;
        reversed.add(
            ImplSourceOccurrenceKey::from(authorityRecord.key().clone(), second.key().clone()));
        reversed.add(
            ImplSourceOccurrenceKey::from(authorityRecord.key().clone(), first.key().clone()));
        ZC_EXPECT(ImplIdentityOccurrenceGroup::from(authorities, authority, zc::mv(reversed),
                                                    sites.asPtr()) == zc::none);
      }
    }
  }
}

ZC_TEST("Identity site inventory and stable admission retain exact parse lineage") {
  auto selected = snapshot(source());
  const uint32_t firstPath[] = {1};
  const uint32_t secondPath[] = {2};
  auto first = IdentitySyntaxSite::from(siteKey(firstPath), span(selected, 1, 2));
  auto second = IdentitySyntaxSite::from(siteKey(secondPath), span(selected, 3, 4));
  ZC_REQUIRE(first != zc::none);
  ZC_REQUIRE(second != zc::none);

  zc::Vector<IdentitySyntaxSiteInventoryEntry> entries;
  entries.add(IdentitySyntaxSiteInventoryEntry{1, ZC_REQUIRE_NONNULL(first).clone()});
  entries.add(IdentitySyntaxSiteInventoryEntry{2, ZC_REQUIRE_NONNULL(second).clone()});
  auto inventory = IdentitySyntaxSiteInventory::fromVerified(
      module(), source(), selected.contentDigest(), 3, zc::mv(entries));
  ZC_REQUIRE(inventory != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(inventory).entries().size() == 2);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(inventory).clone() == ZC_REQUIRE_NONNULL(inventory));

  zc::Vector<IdentitySyntaxSiteInventoryEntry> reversed;
  reversed.add(IdentitySyntaxSiteInventoryEntry{2, ZC_REQUIRE_NONNULL(second).clone()});
  reversed.add(IdentitySyntaxSiteInventoryEntry{1, ZC_REQUIRE_NONNULL(first).clone()});
  ZC_EXPECT(IdentitySyntaxSiteInventory::fromVerified(module(), source(), selected.contentDigest(),
                                                      3, zc::mv(reversed)) == zc::none);

  zc::Vector<StableIdentityAdmissionDefinition> definitions;
  zc::Vector<StableIdentityAdmissionImplementation> implementations;
  auto admission = StableIdentityAdmission::fromVerified(
      module(), source(), selected.contentDigest(), zc::mv(definitions), zc::mv(implementations));
  ZC_REQUIRE(admission != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(admission).sourceDigest() == selected.contentDigest());
  ZC_EXPECT(ZC_REQUIRE_NONNULL(admission).clone() == ZC_REQUIRE_NONNULL(admission));
}

}  // namespace zomlang::compiler::binder
