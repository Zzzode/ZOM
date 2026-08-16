// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and limitations under
// the License.

#include "zomlang/compiler/binder/import-binding.h"

#include "zc/ztest/test.h"
#include "zomlang/compiler/identity/key/package-key.h"
#include "zomlang/compiler/identity/sorted-feature-set.h"
#include "zomlang/compiler/identity/source-snapshot.h"

namespace zomlang::compiler::binder {
namespace {

template <typename Scalar>
Scalar requireScalar(zc::StringPtr text) {
  auto result = Scalar::fromCanonical(text);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid import-binding scalar fixture");
}

identity::PackageKey packageKey() {
  zc::Vector<identity::CanonicalPathSegment> pathSegments;
  auto path = identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(pathSegments));
  zc::Vector<identity::FeatureName> features;
  auto featureSet = identity::SortedFeatureSet::from(zc::mv(features));
  auto version = identity::ResolvedVersion::fromCanonical("0.0.0"_zc);
  ZC_IF_SOME(featureSetValue, featureSet) {
    ZC_IF_SOME(versionValue, version) {
      return identity::PackageKey::from(identity::CanonicalPackageSource::localPath(zc::mv(path)),
                                        requireScalar<identity::PackageName>("imports"_zc),
                                        zc::mv(versionValue), zc::mv(featureSetValue));
    }
  }
  ZC_FAIL_REQUIRE("invalid import-binding package fixture");
}

identity::CrateKey crateKey() {
  zc::Vector<identity::TargetFeatureName> features;
  auto featureSet = identity::SortedTargetFeatureSet::from(zc::mv(features));
  ZC_IF_SOME(featureSetValue, featureSet) {
    auto target = identity::CanonicalTargetSpecificationKey::from(
        requireScalar<identity::TargetComponentName>("aarch64"_zc),
        requireScalar<identity::TargetComponentName>("apple"_zc),
        requireScalar<identity::TargetComponentName>("darwin"_zc),
        requireScalar<identity::TargetComponentName>("none"_zc),
        requireScalar<identity::TargetComponentName>("zom"_zc), 64, identity::Endianness::Little,
        zc::mv(featureSetValue));
    ZC_IF_SOME(targetValue, target) {
      zc::Maybe<identity::BuildScriptProducerKey> noOutput;
      auto config = identity::CompilationConfigKey::from(
          identity::CompilationDomain::Target, zc::mv(targetValue),
          identity::SemanticCompilerOptionsKey::from(2026, true, false, false), zc::mv(noOutput));
      ZC_IF_SOME(configValue, config) {
        auto crate = identity::CrateKey::from(
            identity::CompilationUnitIdentity::userPackage(packageKey()),
            identity::CrateTargetKind::Library, requireScalar<identity::TargetName>("imports"_zc),
            zc::mv(configValue));
        ZC_IF_SOME(value, crate) { return zc::mv(value); }
      }
    }
  }
  ZC_FAIL_REQUIRE("invalid import-binding crate fixture");
}

identity::SourceFileKey sourceKey() {
  zc::Vector<identity::CanonicalPathSegment> segments;
  segments.add(requireScalar<identity::CanonicalPathSegment>("main.zom"_zc));
  auto path = identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(segments));
  return identity::SourceFileKey::from(crateKey(),
                                       identity::SourceOriginKey::localFile(zc::mv(path)));
}

identity::ModuleKey moduleKey() {
  zc::Vector<identity::ModulePathSegment> path;
  path.add(requireScalar<identity::ModulePathSegment>("main"_zc));
  auto result = identity::ModuleKey::from(crateKey(), zc::mv(path));
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid import-binding module fixture");
}

identity::ModuleResolutionPolicyKey resolutionPolicy() {
  auto result = identity::ModuleResolutionPolicyKey::from(
      identity::UnicodeNormalizationPolicy::Nfc, identity::CaseComparisonPolicy::CaseSensitive,
      identity::SymlinkHandlingPolicy::ResolveThenConfine,
      identity::ModuleContainmentPolicy::DeclaredRootsOnly,
      identity::LocalModuleLookupPolicy::RequesterAncestryAndCrateRoot,
      identity::DependencyAliasLookupPolicy::ExactFirstSegment,
      identity::PreludeLookupPolicy::ConfiguredCratePrelude,
      identity::ModuleCandidateSelectionPolicy::AllDistinctMatchesNoPrecedence);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid import-binding resolution policy fixture");
}

identity::ModuleResolutionKey resolutionKey(ImportBindingKind kind) {
  zc::Vector<identity::ModulePathSegment> path;
  path.add(requireScalar<identity::ModulePathSegment>("dependency"_zc));
  zc::Maybe<zc::Vector<identity::ModulePathSegment>> retainedPath(zc::mv(path));
  zc::Maybe<identity::DependencyAlias> alias(
      requireScalar<identity::DependencyAlias>("dependency"_zc));
  const auto dependencyKind = kind == ImportBindingKind::Import
                                  ? identity::ModuleDependencyKind::Import
                                  : identity::ModuleDependencyKind::ForeignReexport;
  auto result = identity::ModuleResolutionKey::from(
      moduleKey(), dependencyKind, zc::mv(retainedPath), zc::mv(alias), resolutionPolicy());
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid import-binding resolution fixture");
}

identity::DefinitionNamespace definitionNamespace(Namespace nameSpace) {
  switch (nameSpace) {
    case Namespace::Value:
      return identity::DefinitionNamespace::Value;
    case Namespace::Type:
      return identity::DefinitionNamespace::Type;
    case Namespace::Module:
      return identity::DefinitionNamespace::Module;
    case Namespace::Label:
    case Namespace::Attribute:
      ZC_FAIL_REQUIRE("invalid semantic import namespace fixture");
  }
  ZC_UNREACHABLE;
}

identity::ImportBindingKey semanticBinding(ImportBindingKind kind, Namespace nameSpace,
                                                   zc::StringPtr sourceName,
                                                   zc::StringPtr localName) {
  const auto operation = kind == ImportBindingKind::Import
                             ? identity::SemanticImportOperation::Import
                             : identity::SemanticImportOperation::ForeignReexport;
  auto result = identity::ImportBindingKey::from(
      moduleKey(), resolutionKey(kind), operation, definitionNamespace(nameSpace),
      requireScalar<identity::DeclaredDefinitionName>(sourceName), definitionNamespace(nameSpace),
      requireScalar<identity::DeclaredDefinitionName>(localName));
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid semantic import binding fixture");
}

identity::SourceSpan span(uint64_t start, uint64_t end) {
  auto snapshot =
      identity::ImmutableSourceSnapshot::from(sourceKey(), zc::heapArray("0123456789abcdef"_zcb));
  ZC_IF_SOME(value, snapshot) {
    auto result = value.span(start, end);
    ZC_IF_SOME(spanValue, result) { return zc::mv(spanValue); }
  }
  ZC_FAIL_REQUIRE("invalid import-binding span fixture");
}

ExportSurfaceRevision revision() {
  const identity::Sha256Digest fingerprint;
  const uint8_t module[] = {0xa1};
  const uint8_t package[] = {0xb1};
  const uint8_t emptyMap[] = {0, 0, 0, 0, 0, 0, 0, 0};
  auto result =
      ExportSurfaceRevision::computeFramed(fingerprint, module, package, emptyMap, emptyMap);
  ZC_IF_SOME(value, result) { return value; }
  ZC_FAIL_REQUIRE("invalid import-binding revision fixture");
}

ModuleAliasExportNamesRevision moduleAliasExportNamesRevision() {
  return ModuleAliasExportNamesRevision::fromDigest(identity::Sha256Digest());
}

ImportBindingNameProjection name(Namespace nameSpace, zc::StringPtr text) {
  return ImportBindingNameProjection(nameSpace,
                                     requireScalar<identity::DeclaredDefinitionName>(text));
}

zc::Maybe<identity::SourceSpan> noSpan() {
  zc::Maybe<identity::SourceSpan> result;
  return result;
}

zc::Maybe<identity::SourceSpan> someSpan(uint64_t start, uint64_t end) { return span(start, end); }

ModuleScopeBindingProjection existingBinding(ast::NodeId node, Namespace nameSpace,
                                             zc::StringPtr text, uint64_t start) {
  identity::DefId definition;
  zc::Vector<ReexportProvenanceStep> noChain;
  return ModuleScopeBindingProjection(
      node, name(nameSpace, text), BindingTarget::definition(definition),
      BindingTarget::definition(definition), BindingOrigin::LocalDeclaration,
      span(start, start + 1), noSpan(), span(start, start + 1), zc::mv(noChain));
}

ImportBindingProjectionInput emptyInput() {
  return ImportBindingProjectionInput{
      identity::ModuleId(), zc::Vector<ModuleScopeBindingProjection>(),
      zc::Vector<ResolvedModuleAliasProjection>(), zc::Vector<ResolvedImportBindingProjection>(),
      zc::Vector<LocalExportBindingProjection>()};
}

}  // namespace

ZC_TEST("ImportBindingProjector.ProjectsModuleAliasesAndOrdinaryImports") {
  auto input = emptyInput();
  identity::DefId moduleAlias;
  identity::ModuleId targetModule;
  identity::ModuleId sourceModule;
  input.moduleAliases.add(ResolvedModuleAliasProjection(
      ast::NodeId(10), 1, moduleAlias, name(Namespace::Module, "geometry"_zc), targetModule,
      moduleAliasExportNamesRevision(), span(1, 3), span(4, 6), false));
  zc::Vector<ReexportProvenanceStep> noChain;
  input.imports.add(ResolvedImportBindingProjection(
      ast::NodeId(20), 2,
      semanticBinding(ImportBindingKind::Import, Namespace::Type, "Point"_zc, "Point"_zc),
      name(Namespace::Type, "Point"_zc), BindingTarget::definition(identity::DefId()), sourceModule,
      revision(), ImportBindingKind::Import, span(6, 8), noSpan(), span(8, 9), noSpan(),
      zc::mv(noChain)));

  auto result = ImportBindingProjector::project(zc::mv(input));

  ZC_REQUIRE(result.is<ImportBindingProjectionCandidate>());
  const auto& candidate = result.get<ImportBindingProjectionCandidate>();
  ZC_REQUIRE(candidate.moduleAliases.size() == 1);
  ZC_REQUIRE(candidate.imports.size() == 1);
  ZC_EXPECT(candidate.localExports.empty());
  ZC_REQUIRE(candidate.moduleScopeBindings.size() == 2);
  ZC_EXPECT(candidate.moduleScopeBindings[0].name.name().text() == "geometry"_zc);
  ZC_EXPECT(candidate.moduleScopeBindings[0].binding.origin == BindingOrigin::LocalDeclaration);
  ZC_EXPECT(candidate.moduleScopeBindings[1].name.name().text() == "Point"_zc);
  ZC_EXPECT(candidate.moduleScopeBindings[1].binding.origin == BindingOrigin::ImportAlias);
  ZC_EXPECT(candidate.moduleScopeBindings[1]
                .binding.bindingIdentity.value()
                .is<SemanticImportBindingTarget>());
  ZC_REQUIRE(candidate.surfaceSeeds.size() == 1);
  ZC_EXPECT(candidate.surfaceSeeds[0].name.name().text() == "geometry"_zc);
  ZC_EXPECT(!candidate.surfaceSeeds[0].exported);
  ZC_EXPECT(candidate.surfaceSeeds[0].visibility.value().is<ModuleVisibility>());
  ZC_EXPECT(candidate.sourceFailures.empty());
}

ZC_TEST("ImportBindingProjector.ProjectsForeignAndLocalReexports") {
  auto input = emptyInput();
  input.existingModuleBindings.add(
      existingBinding(ast::NodeId(1), Namespace::Value, "local"_zc, 1));

  zc::Vector<ReexportProvenanceStep> sourceChain;
  sourceChain.add(ReexportProvenanceStep{identity::ModuleId(),
                                         BindingTarget::definition(identity::DefId()),
                                         BindingTarget::definition(identity::DefId()), span(2, 3)});
  input.imports.add(ResolvedImportBindingProjection(
      ast::NodeId(30), 3,
      semanticBinding(ImportBindingKind::ForeignReexport, Namespace::Type, "Point"_zc, "Point"_zc),
      name(Namespace::Type, "Point"_zc), BindingTarget::definition(identity::DefId()),
      identity::ModuleId(), revision(), ImportBindingKind::ForeignReexport, span(3, 5),
      someSpan(4, 5), span(5, 6), someSpan(3, 5), zc::mv(sourceChain)));
  input.localExports.add(LocalExportBindingProjection(
      ast::NodeId(40), 4, requireScalar<identity::SemanticIdentifier>("local"_zc),
      requireScalar<identity::SemanticIdentifier>("publicLocal"_zc), span(6, 7), span(6, 8),
      someSpan(7, 8), span(6, 8)));

  auto result = ImportBindingProjector::project(zc::mv(input));

  ZC_REQUIRE(result.is<ImportBindingProjectionCandidate>());
  const auto& candidate = result.get<ImportBindingProjectionCandidate>();
  ZC_REQUIRE(candidate.imports.size() == 1);
  ZC_REQUIRE(candidate.imports[0].reexportChain.size() == 2);
  ZC_REQUIRE(candidate.localExports.size() == 1);
  ZC_REQUIRE(candidate.localExports[0].reexportChain.size() == 1);
  ZC_EXPECT(candidate.localExports[0].sourceBinding.value().is<DefinitionBindingTarget>());
  ZC_REQUIRE(candidate.moduleScopeBindings.size() == 2);
  ZC_EXPECT(candidate.moduleScopeBindings[0].binding.origin == BindingOrigin::ReexportAlias);
  ZC_EXPECT(candidate.moduleScopeBindings[1].binding.origin == BindingOrigin::ReexportAlias);
  ZC_REQUIRE(candidate.surfaceSeeds.size() == 2);
  for (const auto& seed : candidate.surfaceSeeds) {
    ZC_EXPECT(seed.exported);
    ZC_EXPECT(seed.visibility.value().is<ExternalVisibility>());
  }
  ZC_EXPECT(candidate.surfaceSeeds[0].name.name().text() == "Point"_zc);
  ZC_EXPECT(candidate.surfaceSeeds[1].name.name().text() == "publicLocal"_zc);
  ZC_EXPECT(candidate.sourceFailures.empty());
}

ZC_TEST("ImportBindingProjector.DerivesLocalExportNamespaceFromAuthoritativeBindings") {
  auto input = emptyInput();
  input.existingModuleBindings.add(
      existingBinding(ast::NodeId(1), Namespace::Type, "LocalType"_zc, 1));
  input.localExports.add(LocalExportBindingProjection(
      ast::NodeId(2), 2, requireScalar<identity::SemanticIdentifier>("LocalType"_zc),
      requireScalar<identity::SemanticIdentifier>("PublicType"_zc), span(2, 3), span(2, 4),
      noSpan(), span(2, 4)));

  auto result = ImportBindingProjector::project(zc::mv(input));

  ZC_REQUIRE(result.is<ImportBindingProjectionCandidate>());
  const auto& candidate = result.get<ImportBindingProjectionCandidate>();
  ZC_REQUIRE(candidate.moduleScopeBindings.size() == 1);
  ZC_EXPECT(candidate.moduleScopeBindings[0].name.nameSpace() == Namespace::Type);
  ZC_REQUIRE(candidate.surfaceSeeds.size() == 1);
  ZC_EXPECT(candidate.surfaceSeeds[0].name.nameSpace() == Namespace::Type);
}

ZC_TEST("ImportBindingProjector.RejectsAmbiguousNamespaceNeutralLocalExports") {
  auto input = emptyInput();
  input.existingModuleBindings.add(
      existingBinding(ast::NodeId(1), Namespace::Value, "ambiguous"_zc, 1));
  input.existingModuleBindings.add(
      existingBinding(ast::NodeId(2), Namespace::Type, "ambiguous"_zc, 2));
  input.localExports.add(LocalExportBindingProjection(
      ast::NodeId(3), 3, requireScalar<identity::SemanticIdentifier>("ambiguous"_zc),
      requireScalar<identity::SemanticIdentifier>("publicAmbiguous"_zc), span(3, 4), span(3, 5),
      noSpan(), span(3, 5)));

  auto result = ImportBindingProjector::project(zc::mv(input));

  ZC_REQUIRE(result.is<BinderInvariantFact>());
  ZC_EXPECT(result.get<BinderInvariantFact>().kind == BinderInvariantKind::InvalidBindingFact);
  ZC_EXPECT(result.get<BinderInvariantFact>().emitterSite == BinderEmitterSite::ImportBinding);
}

ZC_TEST("ImportBindingProjector.RetainsFactsAndEmitsClosedDuplicateAndUndefinedFailures") {
  auto input = emptyInput();
  input.existingModuleBindings.add(
      existingBinding(ast::NodeId(1), Namespace::Value, "taken"_zc, 1));
  zc::Vector<ReexportProvenanceStep> noChain;
  input.imports.add(ResolvedImportBindingProjection(
      ast::NodeId(2), 2,
      semanticBinding(ImportBindingKind::Import, Namespace::Value, "taken"_zc, "taken"_zc),
      name(Namespace::Value, "taken"_zc), BindingTarget::definition(identity::DefId()),
      identity::ModuleId(), revision(), ImportBindingKind::Import, span(3, 4), noSpan(), span(4, 5),
      noSpan(), zc::mv(noChain)));
  input.localExports.add(LocalExportBindingProjection(
      ast::NodeId(3), 3, requireScalar<identity::SemanticIdentifier>("missing"_zc),
      requireScalar<identity::SemanticIdentifier>("publicMissing"_zc), span(5, 6), span(5, 7),
      noSpan(), span(5, 7)));

  auto result = ImportBindingProjector::project(zc::mv(input));

  ZC_REQUIRE(result.is<ImportBindingProjectionCandidate>());
  const auto& candidate = result.get<ImportBindingProjectionCandidate>();
  ZC_REQUIRE(candidate.imports.size() == 1);
  ZC_EXPECT(candidate.moduleScopeBindings.empty());
  ZC_EXPECT(candidate.surfaceSeeds.empty());
  ZC_EXPECT(candidate.localExports.empty());
  ZC_REQUIRE(candidate.sourceFailures.size() == 2);
  ZC_EXPECT(candidate.sourceFailures[0].failure.diagnostic ==
            BinderDiagnosticCode::DuplicateIdentifier);
  ZC_REQUIRE(candidate.sourceFailures[0].failure.notes.size() == 1);
  ZC_EXPECT(candidate.sourceFailures[0].failure.notes[0].diagnostic ==
            BinderDiagnosticCode::PreviousDeclarationHere);
  ZC_EXPECT(candidate.sourceFailures[1].failure.diagnostic ==
            BinderDiagnosticCode::UndefinedIdentifier);
  ZC_EXPECT(candidate.sourceFailures[1].failure.notes.empty());
  ZC_EXPECT(candidate.sourceFailures[0].failure.emitterOrdinal <
            candidate.sourceFailures[1].failure.emitterOrdinal);
}

ZC_TEST("ImportBindingProjector.AllowsOneImportedNameInMultipleNamespaces") {
  auto input = emptyInput();
  zc::Vector<ReexportProvenanceStep> valueChain;
  input.imports.add(ResolvedImportBindingProjection(
      ast::NodeId(2), 2,
      semanticBinding(ImportBindingKind::Import, Namespace::Value, "Item"_zc, "Item"_zc),
      name(Namespace::Value, "Item"_zc), BindingTarget::definition(identity::DefId()),
      identity::ModuleId(), revision(), ImportBindingKind::Import, span(3, 4), noSpan(), span(4, 5),
      noSpan(), zc::mv(valueChain)));
  zc::Vector<ReexportProvenanceStep> typeChain;
  input.imports.add(ResolvedImportBindingProjection(
      ast::NodeId(2), 2,
      semanticBinding(ImportBindingKind::Import, Namespace::Type, "Item"_zc, "Item"_zc),
      name(Namespace::Type, "Item"_zc), BindingTarget::definition(identity::DefId()),
      identity::ModuleId(), revision(), ImportBindingKind::Import, span(3, 4), noSpan(), span(4, 5),
      noSpan(), zc::mv(typeChain)));

  auto result = ImportBindingProjector::project(zc::mv(input));

  ZC_REQUIRE(result.is<ImportBindingProjectionCandidate>());
  const auto& candidate = result.get<ImportBindingProjectionCandidate>();
  ZC_REQUIRE(candidate.imports.size() == 2);
  ZC_REQUIRE(candidate.moduleScopeBindings.size() == 2);
  ZC_EXPECT(candidate.moduleScopeBindings[0].name.nameSpace() == Namespace::Value);
  ZC_EXPECT(candidate.moduleScopeBindings[1].name.nameSpace() == Namespace::Type);
}

ZC_TEST("ImportBindingProjector.RejectsInexactSitesAndForeignExportShape") {
  auto repeated = emptyInput();
  repeated.moduleAliases.add(ResolvedModuleAliasProjection(
      ast::NodeId(1), 7, identity::DefId(), name(Namespace::Module, "first"_zc),
      identity::ModuleId(), moduleAliasExportNamesRevision(), span(1, 2), span(2, 3), false));
  zc::Vector<ReexportProvenanceStep> noChain;
  repeated.imports.add(ResolvedImportBindingProjection(
      ast::NodeId(2), 7,
      semanticBinding(ImportBindingKind::Import, Namespace::Value, "second"_zc, "second"_zc),
      name(Namespace::Value, "second"_zc), BindingTarget::definition(identity::DefId()),
      identity::ModuleId(), revision(), ImportBindingKind::Import, span(3, 4), noSpan(), span(4, 5),
      noSpan(), zc::mv(noChain)));
  auto repeatedResult = ImportBindingProjector::project(zc::mv(repeated));
  ZC_REQUIRE(repeatedResult.is<BinderInvariantFact>());
  ZC_EXPECT(repeatedResult.get<BinderInvariantFact>().emitterSite ==
            BinderEmitterSite::ImportBinding);

  auto malformed = emptyInput();
  zc::Vector<ReexportProvenanceStep> emptyChain;
  malformed.imports.add(ResolvedImportBindingProjection(
      ast::NodeId(3), 8,
      semanticBinding(ImportBindingKind::ForeignReexport, Namespace::Value, "third"_zc, "third"_zc),
      name(Namespace::Value, "third"_zc), BindingTarget::definition(identity::DefId()),
      identity::ModuleId(), revision(), ImportBindingKind::ForeignReexport, span(5, 6), noSpan(),
      span(6, 7), noSpan(), zc::mv(emptyChain)));
  auto malformedResult = ImportBindingProjector::project(zc::mv(malformed));
  ZC_REQUIRE(malformedResult.is<BinderInvariantFact>());
  ZC_EXPECT(malformedResult.get<BinderInvariantFact>().kind ==
            BinderInvariantKind::InvalidBindingFact);
}

}  // namespace zomlang::compiler::binder
