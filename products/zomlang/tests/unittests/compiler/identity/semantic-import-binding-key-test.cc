// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/identity/semantic-import-binding-key.h"

#include "zc/core/encoding.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::identity {
namespace {

template <typename Scalar>
Scalar requireScalar(zc::StringPtr text) {
  auto value = Scalar::fromCanonical(text);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid canonical scalar test input");
}

ResolvedVersion requireVersion() {
  auto value = ResolvedVersion::fromCanonical("0.0.0"_zc);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid version test input");
}

SortedFeatureSet emptyPackageFeatures() {
  zc::Vector<FeatureName> features;
  auto value = SortedFeatureSet::from(zc::mv(features));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("empty package feature set was rejected");
}

SortedTargetFeatureSet emptyTargetFeatures() {
  zc::Vector<TargetFeatureName> features;
  auto value = SortedTargetFeatureSet::from(zc::mv(features));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("empty target feature set was rejected");
}

PackageKey localPackage() {
  zc::Vector<CanonicalPathSegment> segments;
  auto path = CanonicalWorkspaceRelativePath::from(0, zc::mv(segments));
  return PackageKey::from(CanonicalPackageSource::localPath(zc::mv(path)),
                          requireScalar<PackageName>("test"_zc), requireVersion(),
                          emptyPackageFeatures());
}

CanonicalTargetSpecificationKey targetSpec() {
  auto value = CanonicalTargetSpecificationKey::from(
      requireScalar<TargetComponentName>("x"_zc), requireScalar<TargetComponentName>("v"_zc),
      requireScalar<TargetComponentName>("o"_zc), requireScalar<TargetComponentName>("e"_zc),
      requireScalar<TargetComponentName>("a"_zc), 64, Endianness::Little, emptyTargetFeatures());
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid target specification test input");
}

CompilationConfigKey targetCompilation() {
  zc::Maybe<BuildScriptProducerKey> noOutput;
  auto value = CompilationConfigKey::from(
      CompilationDomain::Target, targetSpec(),
      SemanticCompilerOptionsKey::from(2026, true, false, false), zc::mv(noOutput));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid compilation configuration test input");
}

CrateKey crate() {
  auto value = CrateKey::from(localPackage(), CrateTargetKind::Library,
                              requireScalar<TargetName>("lib"_zc), targetCompilation());
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid crate test input");
}

ModuleKey module(zc::StringPtr name) {
  zc::Vector<ModulePathSegment> path;
  path.add(requireScalar<ModulePathSegment>(name));
  auto value = ModuleKey::from(crate(), zc::mv(path));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid module test input");
}

ModuleResolutionPolicyKey policy() {
  auto value = ModuleResolutionPolicyKey::from(
      UnicodeNormalizationPolicy::Nfc, CaseComparisonPolicy::CaseSensitive,
      SymlinkHandlingPolicy::ResolveThenConfine, ModuleContainmentPolicy::DeclaredRootsOnly,
      LocalModuleLookupPolicy::RequesterAncestryAndCrateRoot,
      DependencyAliasLookupPolicy::ExactFirstSegment, PreludeLookupPolicy::ConfiguredCratePrelude,
      ModuleCandidateSelectionPolicy::AllDistinctMatchesNoPrecedence);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("valid module-resolution policy was rejected");
}

ModuleResolutionKey resolution(zc::StringPtr requesterName, ModuleDependencyKind kind,
                               zc::StringPtr targetName) {
  zc::Vector<ModulePathSegment> path;
  path.add(requireScalar<ModulePathSegment>(targetName));
  zc::Maybe<zc::Vector<ModulePathSegment>> retainedPath(zc::mv(path));
  zc::Maybe<DependencyAlias> alias(requireScalar<DependencyAlias>(targetName));
  auto value = ModuleResolutionKey::from(module(requesterName), kind, zc::mv(retainedPath),
                                         zc::mv(alias), policy());
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("valid module-resolution key was rejected");
}

DeclaredDefinitionName name(zc::StringPtr text) {
  return requireScalar<DeclaredDefinitionName>(text);
}

SemanticImportBindingKey importKey(
    zc::StringPtr requesterName = "app"_zc, zc::StringPtr targetName = "dep"_zc,
    SemanticImportOperation operation = SemanticImportOperation::Import,
    DefinitionNamespace sourceNamespace = DefinitionNamespace::Value,
    zc::StringPtr sourceName = "source"_zc,
    DefinitionNamespace localNamespace = DefinitionNamespace::Value,
    zc::StringPtr localName = "local"_zc) {
  const auto dependencyKind = operation == SemanticImportOperation::ForeignReexport
                                  ? ModuleDependencyKind::ForeignReexport
                                  : ModuleDependencyKind::Import;
  auto value = SemanticImportBindingKey::from(
      module(requesterName), resolution(requesterName, dependencyKind, targetName), operation,
      sourceNamespace, name(sourceName), localNamespace, name(localName));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("valid semantic import binding key was rejected");
}

void expectDigest(zc::ArrayPtr<const uint8_t> bytes, zc::StringPtr expected) {
  auto digest = sha256(bytes);
  ZC_REQUIRE(digest != zc::none);
  ZC_IF_SOME(value, digest) { ZC_EXPECT(zc::encodeHex(value.bytes()) == expected); }
}

}  // namespace

ZC_TEST("SemanticImportBindingKey passes the fixed canonical codec vector") {
  auto key = importKey();
  const auto encoded = key.encode();
  constexpr auto domain = "zom.semantic-import-binding.v0"_zc;
  ZC_REQUIRE(encoded.size() > domain.size());
  ZC_EXPECT(encoded.slice(0, domain.size()).asChars() == domain);
  ZC_EXPECT(encoded[domain.size()] == 0x00);
  expectDigest(encoded.asPtr(),
               "0d9e88fef6bcaebaf76f8bb5ab9e9df856b2d994fd9e220229fa5b26241158bf"_zc);
  ZC_EXPECT(key.clone().encode().asPtr() == encoded.asPtr());
  ZC_EXPECT(key.requester().encode().asPtr() == module("app"_zc).encode().asPtr());
  ZC_EXPECT(key.resolution().dependencyKind() == ModuleDependencyKind::Import);
  ZC_EXPECT(key.operation() == SemanticImportOperation::Import);
  ZC_EXPECT(key.sourceNamespace() == DefinitionNamespace::Value);
  ZC_EXPECT(key.sourceName().text() == "source"_zc);
  ZC_EXPECT(key.localNamespace() == DefinitionNamespace::Value);
  ZC_EXPECT(key.localName().text() == "local"_zc);
}

ZC_TEST("SemanticImportBindingKey distinguishes every semantic field") {
  const auto baseline = importKey();
  ZC_EXPECT(baseline != importKey("other"_zc));
  ZC_EXPECT(baseline != importKey("app"_zc, "other"_zc));
  ZC_EXPECT(baseline != importKey("app"_zc, "dep"_zc, SemanticImportOperation::ForeignReexport));
  ZC_EXPECT(baseline != importKey("app"_zc, "dep"_zc, SemanticImportOperation::Import,
                                  DefinitionNamespace::Type));
  ZC_EXPECT(baseline != importKey("app"_zc, "dep"_zc, SemanticImportOperation::Import,
                                  DefinitionNamespace::Value, "other"_zc));
  ZC_EXPECT(baseline != importKey("app"_zc, "dep"_zc, SemanticImportOperation::Import,
                                  DefinitionNamespace::Value, "source"_zc,
                                  DefinitionNamespace::Module));
  ZC_EXPECT(baseline != importKey("app"_zc, "dep"_zc, SemanticImportOperation::Import,
                                  DefinitionNamespace::Value, "source"_zc,
                                  DefinitionNamespace::Value, "other"_zc));
}

ZC_TEST("SemanticImportBindingKey rejects requester operation and namespace mismatches") {
  ZC_EXPECT(SemanticImportBindingKey::from(
                module("other"_zc), resolution("app"_zc, ModuleDependencyKind::Import, "dep"_zc),
                SemanticImportOperation::Import, DefinitionNamespace::Value, name("source"_zc),
                DefinitionNamespace::Value, name("local"_zc)) == zc::none);
  ZC_EXPECT(SemanticImportBindingKey::from(
                module("app"_zc),
                resolution("app"_zc, ModuleDependencyKind::ForeignReexport, "dep"_zc),
                SemanticImportOperation::Import, DefinitionNamespace::Value, name("source"_zc),
                DefinitionNamespace::Value, name("local"_zc)) == zc::none);
  ZC_EXPECT(SemanticImportBindingKey::from(
                module("app"_zc), resolution("app"_zc, ModuleDependencyKind::Import, "dep"_zc),
                static_cast<SemanticImportOperation>(0xff), DefinitionNamespace::Value,
                name("source"_zc), DefinitionNamespace::Value, name("local"_zc)) == zc::none);
  ZC_EXPECT(SemanticImportBindingKey::from(
                module("app"_zc), resolution("app"_zc, ModuleDependencyKind::Import, "dep"_zc),
                SemanticImportOperation::Import, static_cast<DefinitionNamespace>(0xff),
                name("source"_zc), DefinitionNamespace::Value, name("local"_zc)) == zc::none);
  ZC_EXPECT(SemanticImportBindingKey::from(
                module("app"_zc), resolution("app"_zc, ModuleDependencyKind::Import, "dep"_zc),
                SemanticImportOperation::Import, DefinitionNamespace::Value, name("source"_zc),
                static_cast<DefinitionNamespace>(0xff), name("local"_zc)) == zc::none);
}

ZC_TEST("SemanticImportBindingKey canonicalizes source and local names to NFC") {
  auto decomposed = DeclaredDefinitionName::fromSource("e\xcc\x81"_zc);
  auto composed = DeclaredDefinitionName::fromCanonical("\xc3\xa9"_zc);
  ZC_REQUIRE(decomposed != zc::none);
  ZC_REQUIRE(composed != zc::none);
  ZC_IF_SOME(decomposedValue, decomposed) {
    ZC_IF_SOME(composedValue, composed) {
      auto first = SemanticImportBindingKey::from(
          module("app"_zc), resolution("app"_zc, ModuleDependencyKind::Import, "dep"_zc),
          SemanticImportOperation::Import, DefinitionNamespace::Value, decomposedValue.clone(),
          DefinitionNamespace::Type, decomposedValue.clone());
      auto second = SemanticImportBindingKey::from(
          module("app"_zc), resolution("app"_zc, ModuleDependencyKind::Import, "dep"_zc),
          SemanticImportOperation::Import, DefinitionNamespace::Value, composedValue.clone(),
          DefinitionNamespace::Type, composedValue.clone());
      ZC_REQUIRE(first != zc::none);
      ZC_REQUIRE(second != zc::none);
      ZC_IF_SOME(firstValue, first) {
        ZC_IF_SOME(secondValue, second) { ZC_EXPECT(firstValue == secondValue); }
      }
    }
  }
}

}  // namespace zomlang::compiler::identity
