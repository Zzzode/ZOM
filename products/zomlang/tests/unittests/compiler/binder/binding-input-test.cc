// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/binding-input.h"

#include "zc/core/encoding.h"
#include "zc/core/vector.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/binder/definition-inventory.h"
#include "zomlang/compiler/diagnostics/diagnostic-consumer.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/diagnostics/diagnostic-info.h"
#include "zomlang/compiler/diagnostics/diagnostic.h"
#include "zomlang/compiler/source/manager.h"
#include "zomlang/tests/unittests/compiler/test-ast-builder.h"

namespace zomlang::compiler::binder {
namespace {

template <typename Scalar>
Scalar requireScalar(zc::StringPtr text) {
  auto value = Scalar::fromCanonical(text);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid canonical scalar test input");
}

identity::PackageKey package() {
  zc::Vector<identity::CanonicalPathSegment> pathSegments;
  auto path = identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(pathSegments));
  zc::Vector<identity::FeatureName> features;
  auto featureSet = identity::SortedFeatureSet::from(zc::mv(features));
  auto version = identity::ResolvedVersion::fromCanonical("0.0.0"_zc);
  ZC_IF_SOME(featuresValue, featureSet) {
    ZC_IF_SOME(versionValue, version) {
      return identity::PackageKey::from(identity::CanonicalPackageSource::localPath(zc::mv(path)),
                                        requireScalar<identity::PackageName>("binder"_zc),
                                        zc::mv(versionValue), zc::mv(featuresValue));
    }
  }
  ZC_FAIL_REQUIRE("invalid package test input");
}

identity::CrateKey crate() {
  zc::Vector<identity::TargetFeatureName> features;
  auto featureSet = identity::SortedTargetFeatureSet::from(zc::mv(features));
  ZC_IF_SOME(featuresValue, featureSet) {
    auto target = identity::CanonicalTargetSpecificationKey::from(
        requireScalar<identity::TargetComponentName>("aarch64"_zc),
        requireScalar<identity::TargetComponentName>("apple"_zc),
        requireScalar<identity::TargetComponentName>("darwin"_zc),
        requireScalar<identity::TargetComponentName>("none"_zc),
        requireScalar<identity::TargetComponentName>("zom"_zc), 64, identity::Endianness::Little,
        zc::mv(featuresValue));
    ZC_IF_SOME(targetValue, target) {
      zc::Maybe<identity::BuildScriptOutputKey> noOutput;
      auto config = identity::CompilationConfigKey::from(
          identity::CompilationDomain::Target, zc::mv(targetValue),
          identity::SemanticCompilerOptionsKey::from(2026, true, false, false), zc::mv(noOutput));
      ZC_IF_SOME(configValue, config) {
        auto result = identity::CrateKey::from(package(), identity::CrateTargetKind::Library,
                                               requireScalar<identity::TargetName>("binder"_zc),
                                               zc::mv(configValue));
        ZC_IF_SOME(value, result) { return zc::mv(value); }
      }
    }
  }
  ZC_FAIL_REQUIRE("invalid crate test input");
}

identity::SourceFileKey source() {
  zc::Vector<identity::CanonicalPathSegment> segments;
  segments.add(requireScalar<identity::CanonicalPathSegment>("main.zom"_zc));
  auto path = identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(segments));
  return identity::SourceFileKey::from(crate(), identity::SourceOriginKey::localFile(zc::mv(path)));
}

identity::ImmutableSourceSnapshot snapshot() {
  uint8_t bytes[8];
  for (auto& byte : bytes) { byte = 0x41; }
  auto value =
      identity::ImmutableSourceSnapshot::from(source(), zc::heapArray(zc::arrayPtr(bytes)));
  ZC_IF_SOME(result, value) { return zc::mv(result); }
  ZC_FAIL_REQUIRE("invalid source snapshot test input");
}

identity::ModuleKey module() {
  zc::Vector<identity::ModulePathSegment> path;
  path.add(requireScalar<identity::ModulePathSegment>("root"_zc));
  zc::Maybe<identity::SourceSpan> noAnchor;
  auto value = identity::ModuleKey::from(crate(), zc::mv(path), source(), zc::mv(noAnchor));
  ZC_IF_SOME(result, value) { return zc::mv(result); }
  ZC_FAIL_REQUIRE("invalid module test input");
}

identity::DefinitionKey definition() {
  auto snapshotValue = snapshot();
  auto span = snapshotValue.span(0, 1);
  auto name = identity::DefinitionNameKey::declared(
      requireScalar<identity::DeclaredDefinitionName>("run"_zc));
  ZC_IF_SOME(spanValue, span) {
    auto segment = identity::DefinitionPathSegment::from(identity::DefinitionKind::Function,
                                                         zc::mv(name), zc::mv(spanValue), 0);
    ZC_IF_SOME(segmentValue, segment) {
      zc::Vector<identity::DefinitionPathComponent> path;
      path.add(identity::DefinitionPathComponent::definition(zc::mv(segmentValue)));
      auto value = identity::DefinitionKey::from(module(), zc::mv(path));
      ZC_IF_SOME(result, value) { return zc::mv(result); }
    }
  }
  ZC_FAIL_REQUIRE("invalid definition test input");
}

identity::SemanticContextBrand requireContext(identity::SemanticContextFactory& factory) {
  auto value = factory.issue();
  ZC_IF_SOME(result, value) { return result; }
  ZC_FAIL_REQUIRE("semantic context test input exhausted");
}

identity::SemanticContextFingerprint fingerprint(
    const identity::SemanticIdentityRegistrySet& registries) {
  auto value = identity::SemanticContextFingerprint::compute(
      registries, zc::ArrayPtr<const identity::PackageDependencyEdgeKey>(),
      zc::ArrayPtr<const identity::CrateDependencyEdgeKey>());
  ZC_IF_SOME(result, value) { return zc::mv(result); }
  ZC_FAIL_REQUIRE("semantic context fingerprint test input failed");
}

identity::SemanticContextFingerprint emptyFingerprint() {
  zc::Vector<identity::PackageKey> packages;
  zc::Vector<identity::PackageDependencyEdgeKey> packageEdges;
  zc::Vector<identity::CrateKey> crates;
  zc::Vector<identity::CrateDependencyEdgeKey> crateEdges;
  zc::Vector<identity::SourceContentIdentity> sources;
  zc::Vector<identity::ModuleKey> modules;
  auto value = identity::SemanticContextFingerprint::compute(packages.asPtr(), packageEdges.asPtr(),
                                                             crates.asPtr(), crateEdges.asPtr(),
                                                             sources.asPtr(), modules.asPtr());
  ZC_IF_SOME(result, value) { return zc::mv(result); }
  ZC_FAIL_REQUIRE("empty semantic context fingerprint failed");
}

struct FrozenFixture final {
  explicit FrozenFixture(const ast::Tree& tree, ast::NodeId definitionNode = {})
      : context(requireContext(factory)), registries(createRegistries()) {
    ZC_REQUIRE(registries.collectPackage(package()) == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezePackages() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.collectCrate(crate()) == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezeCrates() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.collectSourceFile(snapshot()) == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezeSourceFiles() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.collectModule(module()) == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezeModules() == identity::FrozenRegistryFailure::None);
    if (definitionNode) {
      auto key = definition();
      auto retained = key.clone();
      ZC_REQUIRE(registries.collectDefinition(zc::mv(key)) ==
                 identity::FrozenRegistryFailure::None);
      ZC_REQUIRE(registries.freezeDefinitions() == identity::FrozenRegistryFailure::None);
      definitionId = requireHandle(registries.definitions().find(retained));
      ZC_REQUIRE(definitions.insert(definitionNode, definitionId));
    } else {
      ZC_REQUIRE(registries.freezeDefinitions() == identity::FrozenRegistryFailure::None);
    }
    ZC_REQUIRE(registries.freezeImpls() == identity::FrozenRegistryFailure::None);
    packageId = requireHandle(registries.packages().find(package()));
    crateId = requireHandle(registries.crates().find(crate()));
    sourceId = requireHandle(registries.sourceFiles().find(source()));
    moduleId = requireHandle(registries.modules().find(module()));
    auto result = ModuleGraphVerifier::verifySingleModule(context, fingerprint(registries),
                                                          registries, moduleId, tree);
    if (result.is<VerifiedModuleGraphView>()) {
      graph = zc::mv(result.get<VerifiedModuleGraphView>());
    } else {
      graphFailure = result.get<ModuleGraphInvariantFact>().kind;
    }
  }

  template <typename Handle>
  Handle requireHandle(zc::Maybe<Handle>&& value) {
    ZC_IF_SOME(result, value) { return result; }
    ZC_FAIL_REQUIRE("identity handle lookup failed");
  }

  identity::SemanticIdentityRegistrySet createRegistries() {
    auto value = identity::SemanticIdentityRegistrySet::create(factory, context);
    ZC_IF_SOME(result, value) { return zc::mv(result); }
    ZC_FAIL_REQUIRE("registry set test input was already claimed");
  }

  identity::SemanticContextFactory factory;
  identity::SemanticContextBrand context;
  identity::SemanticIdentityRegistrySet registries;
  identity::PackageId packageId;
  identity::CrateId crateId;
  identity::SourceFileId sourceId;
  identity::ModuleId moduleId;
  identity::DefId definitionId;
  DefinitionIdentityMap definitions;
  zc::Maybe<VerifiedModuleGraphView> graph;
  zc::Maybe<ModuleGraphInvariantKind> graphFailure;
};

ast::Tree dependencyFreeTree(bool includeDefinition = false, bool includeImpl = false) {
  tests::TestFixture fix;
  zc::Vector<ast::NodeId> declarations;
  if (includeDefinition) {
    declarations.add(fix.makeFunctionDecl("run"_zc, fix.makeBlockStmt(ast::NodeList())));
  }
  if (includeImpl) {
    declarations.add(fix.makeStandaloneImplDecl(fix.makeNamedTypeExpr("Target"_zc), ast::NodeId(),
                                                fix.makeClassMemberList(ast::NodeList())));
  }
  zc::Vector<ast::NodeId> items;
  for (const auto declaration : declarations) { items.add(fix.makeStatementListItem(declaration)); }
  fix.makeSourceFile(fix.makeModuleDecl("root"_zc), fix.makeNodeList(items));
  return fix.finishTree();
}

BindingInputVerificationResult verify(FrozenFixture& fixture, const ast::Tree& tree,
                                      const DefinitionIdentityMap& definitions) {
  ZC_IF_SOME(graph, fixture.graph) {
    return BindingInputVerifier::verify(
        BindingInputCandidate{fixture.context, fixture.packageId, fixture.crateId, fixture.sourceId,
                              fixture.moduleId, tree, fixture.registries, graph, definitions});
  }
  ZC_FAIL_REQUIRE("binding verification requires a verified graph fixture");
}

template <typename Success>
void expectFailure(zc::OneOf<Success, ModuleGraphInvariantFact>& result,
                   ModuleGraphInvariantKind kind) {
  ZC_REQUIRE(result.template is<ModuleGraphInvariantFact>());
  ZC_EXPECT(static_cast<uint32_t>(result.template get<ModuleGraphInvariantFact>().kind) ==
            static_cast<uint32_t>(kind));
}

}  // namespace

ZC_TEST("BindingInput.AcceptsCompleteDependencyFreeInventory") {
  const auto tree = dependencyFreeTree(true);
  const auto definitionNode = DefinitionInventory::collect(tree).definitions()[0].node;
  FrozenFixture fixture(tree, definitionNode);
  auto result = verify(fixture, tree, fixture.definitions);
  ZC_REQUIRE(result.is<VerifiedBindingInput>());
}

ZC_TEST("BindingInput.ClassifiesIncompleteAndForeignDefinitionInventories") {
  const auto tree = dependencyFreeTree(true);
  const auto definitionNode = DefinitionInventory::collect(tree).definitions()[0].node;
  FrozenFixture fixture(tree, definitionNode);

  DefinitionIdentityMap missing;
  auto missingResult = verify(fixture, tree, missing);
  expectFailure(missingResult, ModuleGraphInvariantKind::IncompleteResolution);

  DefinitionIdentityMap additional;
  ZC_REQUIRE(additional.insert(definitionNode, fixture.definitionId));
  ZC_REQUIRE(additional.insert(tree.root(), fixture.definitionId));
  auto additionalResult = verify(fixture, tree, additional);
  expectFailure(additionalResult, ModuleGraphInvariantKind::IncompleteResolution);

  FrozenFixture foreign(tree, definitionNode);
  auto foreignResult = verify(fixture, tree, foreign.definitions);
  expectFailure(foreignResult, ModuleGraphInvariantKind::InputMismatch);
}

ZC_TEST("BindingInput.RejectsImplProducerUntilImplInventoryPublicationExists") {
  const auto tree = dependencyFreeTree(false, true);
  FrozenFixture fixture(tree);
  auto result = verify(fixture, tree, fixture.definitions);
  expectFailure(result, ModuleGraphInvariantKind::IncompleteResolution);
}

ZC_TEST("ModuleGraph.ClassifiesUnresolvedSyntaxRequesterAndRevisionFailures") {
  tests::TestFixture fix;
  zc::Vector<ast::NodeId> declarations;
  declarations.add(fix.makeImportDecl("dependency"_zc));
  const auto importTree = fix.buildSourceFile("root"_zc, declarations);
  FrozenFixture unresolved(importTree);
  ZC_IF_SOME(kind, unresolved.graphFailure) {
    ZC_EXPECT(kind == ModuleGraphInvariantKind::IncompleteResolution);
  }
  else { ZC_EXPECT(false); }

  const auto tree = dependencyFreeTree();
  FrozenFixture fixture(tree);
  auto requesterResult =
      ModuleGraphVerifier::verifySingleModule(fixture.context, fingerprint(fixture.registries),
                                              fixture.registries, identity::ModuleId(), tree);
  expectFailure(requesterResult, ModuleGraphInvariantKind::InputMismatch);

  auto revisionResult = ModuleGraphVerifier::verifySingleModule(
      fixture.context, emptyFingerprint(), fixture.registries, fixture.moduleId, tree);
  expectFailure(revisionResult, ModuleGraphInvariantKind::RevisionMismatch);
}

ZC_TEST("ModuleGraphRevision.IsDeterministicForEquivalentSingleModuleGraphs") {
  const auto firstTree = dependencyFreeTree();
  const auto secondTree = dependencyFreeTree();
  FrozenFixture first(firstTree);
  FrozenFixture second(secondTree);
  ZC_IF_SOME(firstGraph, first.graph) {
    ZC_IF_SOME(secondGraph, second.graph) {
      ZC_EXPECT(firstGraph.revision().digest() == secondGraph.revision().digest());
      ZC_EXPECT(zc::encodeHex(firstGraph.revision().digest().bytes()) ==
                "bc59817632beabe08f937aa1ff8f2986ad6d1b0f9916a7e8417bf2f2525f8802"_zc);
      return;
    }
  }
  ZC_EXPECT(false);
}

ZC_TEST("ModuleGraphInvariant.EmitsFatalZOM9956WithOccurrenceCount") {
  class Capture final : public diagnostics::DiagnosticConsumer {
  public:
    diagnostics::DiagID id = diagnostics::DiagID::UndefinedIdentifier;
    zc::String occurrence;

    void handleDiagnostic(const source::SourceManager&,
                          const diagnostics::Diagnostic& diagnostic) override {
      id = diagnostic.getId();
      if (diagnostic.getArgs().size() == 1) {
        const auto& argument = diagnostic.getArgs()[0];
        if (argument.is<zc::String>()) {
          occurrence = zc::str(argument.get<zc::String>());
        } else if (argument.is<zc::StringPtr>()) {
          occurrence = zc::str(argument.get<zc::StringPtr>());
        }
      }
    }
  };

  source::SourceManager sources;
  diagnostics::DiagnosticEngine diagnostics(sources);
  auto consumer = zc::heap<Capture>();
  const auto& captured = *consumer;
  diagnostics.addConsumer(zc::mv(consumer));
  emitModuleGraphInvariant(diagnostics,
                           ModuleGraphInvariantFact{ModuleGraphInvariantKind::InvalidEdge, zc::none,
                                                    zc::Vector<uint32_t>(), 7});

  ZC_EXPECT(captured.id == diagnostics::DiagID::ModuleGraphInvariant);
  ZC_EXPECT(diagnostics::getDiagnosticInfo(captured.id).severity ==
            diagnostics::DiagSeverity::kFatal);
  ZC_EXPECT(captured.occurrence == "7"_zc);
  ZC_EXPECT(diagnostics.hasErrors());
}

}  // namespace zomlang::compiler::binder
