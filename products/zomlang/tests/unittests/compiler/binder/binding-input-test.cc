// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/binding-input.h"

#include "zc/core/encoding.h"
#include "zc/core/vector.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/basic/string-pool.h"
#include "zomlang/compiler/basic/zomlang-opts.h"
#include "zomlang/compiler/binder/definition-inventory.h"
#include "zomlang/compiler/diagnostics/diagnostic-consumer.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/diagnostics/diagnostic-info.h"
#include "zomlang/compiler/diagnostics/diagnostic.h"
#include "zomlang/compiler/parser/parser.h"
#include "zomlang/compiler/source/manager.h"

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

identity::ModuleKey module() {
  zc::Vector<identity::ModulePathSegment> path;
  path.add(requireScalar<identity::ModulePathSegment>("root"_zc));
  zc::Maybe<identity::SourceSpan> noAnchor;
  auto value = identity::ModuleKey::from(crate(), zc::mv(path), source(), zc::mv(noAnchor));
  ZC_IF_SOME(result, value) { return zc::mv(result); }
  ZC_FAIL_REQUIRE("invalid module test input");
}

identity::SemanticContextBrand requireContext(identity::SemanticContextFactory& factory) {
  ZC_IF_SOME(result, factory.issue()) { return result; }
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

struct ParsedSource final {
  explicit ParsedSource(zc::StringPtr text)
      : sources(zc::heap<source::SourceManager>()),
        diagnostics(zc::heap<diagnostics::DiagnosticEngine>(*sources)),
        buffer(sources->addMemBufferCopy(text.asBytes(), "main.zom")) {
    parser::Parser parser(*sources, *diagnostics, options, strings, buffer);
    ZC_IF_SOME(parsed, parser.parse()) { tree = zc::mv(parsed); }
    else { ZC_FAIL_REQUIRE("source fixture did not parse"); }
    ZC_REQUIRE(!diagnostics->hasErrors());
  }

  identity::ImmutableSourceSnapshot snapshot() const {
    auto value = identity::ImmutableSourceSnapshot::from(
        source(), zc::heapArray(sources->getEntireTextForBuffer(buffer)));
    ZC_IF_SOME(result, value) { return zc::mv(result); }
    ZC_FAIL_REQUIRE("source snapshot fixture failed");
  }

  zc::Own<source::SourceManager> sources;
  zc::Own<diagnostics::DiagnosticEngine> diagnostics;
  basic::LangOptions options;
  basic::StringPool strings;
  source::BufferId buffer;
  ast::Tree tree;
};

identity::DefinitionKey definitionKey(
    const ParsedSource& parsed, const identity::ImmutableSourceSnapshot& snapshot,
    identity::DefinitionKind kind = identity::DefinitionKind::Function) {
  const auto inventory = DefinitionInventory::collect(parsed.tree);
  ZC_REQUIRE(inventory.definitions().size() == 1);
  const auto& entry = inventory.definitions()[0];
  auto name = identity::DeclaredDefinitionName::fromSource(parsed.tree.ident(entry.declaredName));
  const auto start = parsed.sources->getLocOffsetInBuffer(entry.source.getStart(), parsed.buffer);
  const auto end = parsed.sources->getLocOffsetInBuffer(entry.source.getEnd(), parsed.buffer);
  auto span = snapshot.span(start, end);
  ZC_IF_SOME(nameValue, name) {
    ZC_IF_SOME(spanValue, span) {
      auto segment = identity::DefinitionPathSegment::from(
          kind, identity::DefinitionNameKey::declared(zc::mv(nameValue)), zc::mv(spanValue), 0);
      ZC_IF_SOME(segmentValue, segment) {
        zc::Vector<identity::DefinitionPathComponent> path;
        path.add(identity::DefinitionPathComponent::definition(zc::mv(segmentValue)));
        ZC_IF_SOME(value, identity::DefinitionKey::from(module(), zc::mv(path))) {
          return zc::mv(value);
        }
      }
    }
  }
  ZC_FAIL_REQUIRE("definition key fixture failed");
}

ast::Tree manualModuleTree(source::SourceRange range, zc::StringPtr moduleName) {
  ast::TreeBuilder builder;
  ast::NodePayload modulePayload;
  modulePayload.words[ast::kModuleDeclarationFormWord] =
      static_cast<uint32_t>(ast::ModuleDeclarationForm::RootDeclaration);
  modulePayload.words[ast::kModuleDeclarationDeclaredNameWord] =
      builder.internIdent(moduleName).value;
  const auto moduleNode =
      builder.makeNode(ast::SyntaxKind::ModuleDeclaration, range, modulePayload);
  ast::NodePayload rootPayload;
  rootPayload.words[ast::kSourceFileFileNameWord] = builder.internString("main.zom"_zc).value;
  rootPayload.words[ast::kSourceFileModuleWord] = moduleNode.value;
  const auto root = builder.makeNode(ast::SyntaxKind::SourceFile, range, rootPayload);
  builder.setRoot(root);
  return builder.finish();
}

struct FrozenFixture final {
  explicit FrozenFixture(ParsedSource& sourceFixture, bool includeDefinition = false,
                         bool wrongDefinitionKind = false)
      : context(requireContext(factory)), registries(createRegistries()) {
    auto snapshot = sourceFixture.snapshot();
    ZC_REQUIRE(registries.collectPackage(package()) == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezePackages() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.collectCrate(crate()) == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezeCrates() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.collectSourceFile(snapshot.clone()) ==
               identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezeSourceFiles() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.collectModule(module()) == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezeModules() == identity::FrozenRegistryFailure::None);
    if (includeDefinition) {
      auto key = definitionKey(sourceFixture, snapshot,
                               wrongDefinitionKind ? identity::DefinitionKind::Class
                                                   : identity::DefinitionKind::Function);
      auto retained = key.clone();
      ZC_REQUIRE(registries.collectDefinition(zc::mv(key)) ==
                 identity::FrozenRegistryFailure::None);
      ZC_REQUIRE(registries.freezeDefinitions() == identity::FrozenRegistryFailure::None);
      definitionId = requireHandle(registries.definitions().find(retained));
      const auto node = DefinitionInventory::collect(sourceFixture.tree).definitions()[0].node;
      ZC_REQUIRE(rawDefinitions.insert(node, definitionId));
    } else {
      ZC_REQUIRE(registries.freezeDefinitions() == identity::FrozenRegistryFailure::None);
    }
    ZC_REQUIRE(registries.freezeImpls() == identity::FrozenRegistryFailure::None);
    packageId = requireHandle(registries.packages().find(package()));
    crateId = requireHandle(registries.crates().find(crate()));
    moduleId = requireHandle(registries.modules().find(module()));

    auto admission = ParsedModuleVerifier::admit(snapshot, *sourceFixture.sources,
                                                 sourceFixture.buffer, zc::mv(sourceFixture.tree));
    ZC_REQUIRE(admission.is<UnbrandedParsedModule>());
    auto promotion = ParsedModuleVerifier::promote(context, registries,
                                                   zc::mv(admission.get<UnbrandedParsedModule>()));
    ZC_REQUIRE(promotion.is<VerifiedParsedModule>());
    parsed = zc::mv(promotion.get<VerifiedParsedModule>());
    ZC_IF_SOME(parsedValue, parsed) {
      auto graphResult = ModuleGraphVerifier::verifySingleModule(context, fingerprint(registries),
                                                                 registries, moduleId, parsedValue);
      if (graphResult.is<VerifiedModuleGraphView>()) {
        graph = zc::mv(graphResult.get<VerifiedModuleGraphView>());
      } else {
        graphFailure = graphResult.get<ModuleGraphInvariantFact>().kind;
      }
      auto inventoryResult = FrozenDefinitionInventoryVerifier::verifySingleModule(
          context, moduleId, parsedValue, registries, rawDefinitions);
      if (inventoryResult.is<FrozenDefinitionInventoryView>()) {
        frozenDefinitions = zc::mv(inventoryResult.get<FrozenDefinitionInventoryView>());
      } else {
        inventoryFailure = inventoryResult.get<FrozenInventoryInvariantFact>().kind;
      }
    }
  }

  template <typename Handle>
  Handle requireHandle(zc::Maybe<Handle>&& value) {
    ZC_IF_SOME(result, value) { return result; }
    ZC_FAIL_REQUIRE("identity handle lookup failed");
  }

  identity::SemanticIdentityRegistrySet createRegistries() {
    ZC_IF_SOME(result, identity::SemanticIdentityRegistrySet::create(factory, context)) {
      return zc::mv(result);
    }
    ZC_FAIL_REQUIRE("registry set test input was already claimed");
  }

  identity::SemanticContextFactory factory;
  identity::SemanticContextBrand context;
  identity::SemanticIdentityRegistrySet registries;
  identity::PackageId packageId;
  identity::CrateId crateId;
  identity::ModuleId moduleId;
  identity::DefId definitionId;
  DefinitionIdentityMap rawDefinitions;
  zc::Maybe<VerifiedParsedModule> parsed;
  zc::Maybe<VerifiedModuleGraphView> graph;
  zc::Maybe<FrozenDefinitionInventoryView> frozenDefinitions;
  zc::Maybe<ModuleGraphInvariantKind> graphFailure;
  zc::Maybe<FrozenInventoryInvariantKind> inventoryFailure;
};

BindingInputVerificationResult verify(FrozenFixture& fixture) {
  ZC_IF_SOME(parsed, fixture.parsed) {
    ZC_IF_SOME(graph, fixture.graph) {
      ZC_IF_SOME(definitions, fixture.frozenDefinitions) {
        return BindingInputVerifier::verify(BindingInputCandidate{
            fixture.context, fixture.packageId, fixture.crateId, fixture.moduleId,
            fixture.registries, graph, parsed, definitions});
      }
    }
  }
  ZC_FAIL_REQUIRE("binding verification requires complete verified inputs");
}

template <typename Success>
void expectGraphFailure(zc::OneOf<Success, ModuleGraphInvariantFact>& result,
                        ModuleGraphInvariantKind kind) {
  ZC_REQUIRE(result.template is<ModuleGraphInvariantFact>());
  ZC_EXPECT(result.template get<ModuleGraphInvariantFact>().kind == kind);
}

void expectInventoryFailure(FrozenDefinitionInventoryResult& result,
                            FrozenInventoryInvariantKind kind) {
  ZC_REQUIRE(result.is<FrozenInventoryInvariantFact>());
  ZC_EXPECT(result.get<FrozenInventoryInvariantFact>().kind == kind);
}

}  // namespace

ZC_TEST("ParsedModuleReceipt.MatchesNormativeRFC0004Oracle") {
  uint8_t sourceBytes[] = {0xa1};
  uint8_t contentBytes[32];
  uint8_t schemaBytes[32];
  for (auto& byte : contentBytes) { byte = 0x22; }
  for (auto& byte : schemaBytes) { byte = 0x33; }
  auto content = identity::Sha256Digest::fromBytes(zc::arrayPtr(contentBytes));
  auto schema = identity::Sha256Digest::fromBytes(zc::arrayPtr(schemaBytes));
  const uint8_t dump[] = {'x', 'y', 'z'};
  ZC_IF_SOME(contentValue, content) {
    ZC_IF_SOME(schemaValue, schema) {
      auto receipt = ParsedModuleReceipt::compute(zc::arrayPtr(sourceBytes), contentValue, 3,
                                                  schemaValue, zc::arrayPtr(dump));
      ZC_IF_SOME(value, receipt) {
        ZC_EXPECT(zc::encodeHex(value.digest().bytes()) ==
                  "7a4ab18a31387244311bd2a1b1472350536140c89532ce64240d7670d5a20b8e"_zc);
        return;
      }
    }
  }
  ZC_EXPECT(false);
}

ZC_TEST("ParsedModule.PromotesExactSourceTreeAndRootSpan") {
  ParsedSource sourceFixture("module root;\nfun run() {}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  ZC_REQUIRE(fixture.parsed != zc::none);
  ZC_IF_SOME(parsed, fixture.parsed) {
    ZC_EXPECT(parsed.byteLength() == 26);
    ZC_EXPECT(parsed.tree().node(parsed.tree().root()).kind == ast::SyntaxKind::SourceFile);
    ZC_EXPECT(parsed.rootSpan().belongsTo(source()));
  }
  ZC_REQUIRE(fixture.frozenDefinitions != zc::none);
  ZC_IF_SOME(definitions, fixture.frozenDefinitions) {
    ZC_EXPECT(definitions.definitions().size() == 1);
  }
}

ZC_TEST("ParsedModule.RejectsCrossSourceAndInvalidRanges") {
  source::SourceManager sources;
  const auto buffer = sources.addMemBufferCopy("module root;"_zcb, "main.zom");
  auto snapshot = identity::ImmutableSourceSnapshot::from(
      source(), zc::heapArray(sources.getEntireTextForBuffer(buffer)));
  const uint8_t foreign[] = {0, 1};
  const auto badRange = source::SourceRange(foreign, foreign + 1);
  ZC_IF_SOME(snapshotValue, snapshot) {
    auto result = ParsedModuleVerifier::admit(snapshotValue, sources, buffer,
                                              manualModuleTree(badRange, "root"_zc));
    ZC_REQUIRE(result.is<ParsedModuleInvariantFact>());
    ZC_EXPECT(result.get<ParsedModuleInvariantFact>().kind ==
              ParsedModuleInvariantKind::InvalidSourceRange);
  }
}

ZC_TEST("ParsedModule.ReceiptBindsExactTreeAndPromotionRejectsStaleSource") {
  source::SourceManager sources;
  const auto buffer = sources.addMemBufferCopy("module root;"_zcb, "main.zom");
  auto snapshot = identity::ImmutableSourceSnapshot::from(
      source(), zc::heapArray(sources.getEntireTextForBuffer(buffer)));
  ZC_IF_SOME(snapshotValue, snapshot) {
    const auto bufferRange = sources.getRangeForBuffer(buffer).getAsRange();
    auto first = ParsedModuleVerifier::admit(snapshotValue, sources, buffer,
                                             manualModuleTree(bufferRange, "root"_zc));
    auto second = ParsedModuleVerifier::admit(snapshotValue, sources, buffer,
                                              manualModuleTree(bufferRange, "other"_zc));
    ZC_REQUIRE(first.is<UnbrandedParsedModule>());
    ZC_REQUIRE(second.is<UnbrandedParsedModule>());
    ZC_EXPECT(first.get<UnbrandedParsedModule>().receipt().digest() !=
              second.get<UnbrandedParsedModule>().receipt().digest());

    identity::SemanticContextFactory factory;
    const auto context = requireContext(factory);
    auto registriesValue = identity::SemanticIdentityRegistrySet::create(factory, context);
    ZC_REQUIRE(registriesValue != zc::none);
    ZC_IF_SOME(registries, registriesValue) {
      ZC_REQUIRE(registries.collectPackage(package()) == identity::FrozenRegistryFailure::None);
      ZC_REQUIRE(registries.freezePackages() == identity::FrozenRegistryFailure::None);
      ZC_REQUIRE(registries.collectCrate(crate()) == identity::FrozenRegistryFailure::None);
      ZC_REQUIRE(registries.freezeCrates() == identity::FrozenRegistryFailure::None);
      auto stale =
          identity::ImmutableSourceSnapshot::from(source(), zc::heapArray("module root; "_zcb));
      ZC_IF_SOME(staleValue, stale) {
        ZC_REQUIRE(registries.collectSourceFile(zc::mv(staleValue)) ==
                   identity::FrozenRegistryFailure::None);
      }
      ZC_REQUIRE(registries.freezeSourceFiles() == identity::FrozenRegistryFailure::None);
      auto promoted = ParsedModuleVerifier::promote(context, registries,
                                                    zc::mv(first.get<UnbrandedParsedModule>()));
      ZC_REQUIRE(promoted.is<ParsedModuleInvariantFact>());
      ZC_EXPECT(promoted.get<ParsedModuleInvariantFact>().kind ==
                ParsedModuleInvariantKind::SourceMismatch);
    }
  }
}

ZC_TEST("BindingInput.AcceptsVerifiedDependencyFreeInventory") {
  ParsedSource sourceFixture("module root;\nfun run() {}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto result = verify(fixture);
  ZC_REQUIRE(result.is<VerifiedBindingInput>());
  ZC_EXPECT(result.get<VerifiedBindingInput>().definitions().definitions().size() == 1);
}

ZC_TEST("FrozenInventory.RejectsMissingAdditionalWrongKindAndForeignDefinitions") {
  ParsedSource missingSource("module root;\nfun run() {}\n"_zc);
  FrozenFixture missingFixture(missingSource, true);
  DefinitionIdentityMap missing;
  ZC_IF_SOME(parsed, missingFixture.parsed) {
    auto result = FrozenDefinitionInventoryVerifier::verifySingleModule(
        missingFixture.context, missingFixture.moduleId, parsed, missingFixture.registries,
        missing);
    expectInventoryFailure(result, FrozenInventoryInvariantKind::IncompleteInventory);
  }

  ParsedSource additionalSource("module root;\nfun run() {}\n"_zc);
  FrozenFixture additionalFixture(additionalSource, true);
  DefinitionIdentityMap additional;
  ZC_IF_SOME(parsed, additionalFixture.parsed) {
    ZC_REQUIRE(additional.insert(parsed.tree().root(), additionalFixture.definitionId));
    const auto definitionNode = DefinitionInventory::collect(parsed.tree()).definitions()[0].node;
    ZC_REQUIRE(additional.insert(definitionNode, additionalFixture.definitionId));
    auto result = FrozenDefinitionInventoryVerifier::verifySingleModule(
        additionalFixture.context, additionalFixture.moduleId, parsed, additionalFixture.registries,
        additional);
    expectInventoryFailure(result, FrozenInventoryInvariantKind::IncompleteInventory);
  }

  ParsedSource wrongSource("module root;\nfun run() {}\n"_zc);
  FrozenFixture wrongFixture(wrongSource, true, true);
  ZC_IF_SOME(kind, wrongFixture.inventoryFailure) {
    ZC_EXPECT(kind == FrozenInventoryInvariantKind::InvalidDefinitionIdentity);
  }
  else { ZC_EXPECT(false); }

  ParsedSource localSource("module root;\nfun run() {}\n"_zc);
  ParsedSource foreignSource("module root;\nfun run() {}\n"_zc);
  FrozenFixture local(localSource, true);
  FrozenFixture foreign(foreignSource, true);
  DefinitionIdentityMap foreignMap;
  ZC_IF_SOME(parsed, local.parsed) {
    const auto definitionNode = DefinitionInventory::collect(parsed.tree()).definitions()[0].node;
    ZC_REQUIRE(foreignMap.insert(definitionNode, foreign.definitionId));
    auto result = FrozenDefinitionInventoryVerifier::verifySingleModule(
        local.context, local.moduleId, parsed, local.registries, foreignMap);
    expectInventoryFailure(result, FrozenInventoryInvariantKind::InvalidDefinitionIdentity);
  }
}

ZC_TEST("BindingInput.RejectsImplProducerUntilImplInventoryPublicationExists") {
  ParsedSource sourceFixture("module root;\nimpl Trait for Target {}\n"_zc);
  FrozenFixture fixture(sourceFixture);
  ZC_IF_SOME(kind, fixture.inventoryFailure) {
    ZC_EXPECT(kind == FrozenInventoryInvariantKind::UnsupportedImplInventory);
  }
  else { ZC_EXPECT(false); }
}

ZC_TEST("ModuleGraph.ClassifiesUnresolvedSyntaxRequesterAndRevisionFailures") {
  ParsedSource importSource("module root;\nimport math::geometry;\n"_zc);
  FrozenFixture unresolved(importSource);
  ZC_IF_SOME(kind, unresolved.graphFailure) {
    ZC_EXPECT(kind == ModuleGraphInvariantKind::IncompleteResolution);
  }
  else { ZC_EXPECT(false); }

  ParsedSource sourceFixture("module root;\n"_zc);
  FrozenFixture fixture(sourceFixture);
  ZC_IF_SOME(parsed, fixture.parsed) {
    auto requesterResult =
        ModuleGraphVerifier::verifySingleModule(fixture.context, fingerprint(fixture.registries),
                                                fixture.registries, identity::ModuleId(), parsed);
    expectGraphFailure(requesterResult, ModuleGraphInvariantKind::InputMismatch);

    auto revisionResult = ModuleGraphVerifier::verifySingleModule(
        fixture.context, emptyFingerprint(), fixture.registries, fixture.moduleId, parsed);
    expectGraphFailure(revisionResult, ModuleGraphInvariantKind::RevisionMismatch);
  }
}

ZC_TEST("ModuleGraphRevision.IsDeterministicForEquivalentSingleModuleGraphs") {
  ParsedSource firstSource("module root;\n"_zc);
  ParsedSource secondSource("module root;\n"_zc);
  FrozenFixture first(firstSource);
  FrozenFixture second(secondSource);
  ZC_IF_SOME(firstGraph, first.graph) {
    ZC_IF_SOME(secondGraph, second.graph) {
      ZC_EXPECT(firstGraph.revision().digest() == secondGraph.revision().digest());
      ZC_EXPECT(zc::encodeHex(firstGraph.revision().digest().bytes()) ==
                "2d40cd7d8c0fa23e5857fa4785dfcd2db7bff312a635ce0d692ddea433a6f81d"_zc);
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
