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
#include "zomlang/compiler/binder/internal/binding-verifier.h"
#include "zomlang/compiler/binder/internal/scope-arena.h"
#include "zomlang/compiler/diagnostics/diagnostic-consumer.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/diagnostics/diagnostic-info.h"
#include "zomlang/compiler/diagnostics/diagnostic.h"
#include "zomlang/compiler/identity/sha256.h"
#include "zomlang/compiler/parser/parser.h"
#include "zomlang/compiler/source/manager.h"

namespace zomlang::compiler::binder {
namespace {

uint8_t hexNibble(char value) {
  if (value >= '0' && value <= '9') { return static_cast<uint8_t>(value - '0'); }
  if (value >= 'a' && value <= 'f') { return static_cast<uint8_t>(value - 'a' + 10); }
  ZC_FAIL_REQUIRE("invalid hexadecimal binder fixture");
}

zc::Array<uint8_t> decodeHexBytes(zc::StringPtr text) {
  ZC_REQUIRE(text.size() % 2 == 0);
  auto bytes = zc::heapArray<uint8_t>(text.size() / 2);
  for (size_t index = 0; index < bytes.size(); ++index) {
    bytes[index] =
        static_cast<uint8_t>((hexNibble(text[index * 2]) << 4) | hexNibble(text[index * 2 + 1]));
  }
  return bytes;
}

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

identity::SourceFileKey alternateSource() {
  zc::Vector<identity::CanonicalPathSegment> segments;
  segments.add(requireScalar<identity::CanonicalPathSegment>("other.zom"_zc));
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

bool sameParentPath(zc::ArrayPtr<const StructuralIdentityParent> left,
                    zc::ArrayPtr<const StructuralIdentityParent> right) {
  if (left.size() != right.size()) { return false; }
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index].kind != right[index].kind || left[index].node != right[index].node) {
      return false;
    }
  }
  return true;
}

uint32_t siblingOrdinal(const DefinitionInventory& inventory, ast::NodeId moduleNode,
                        zc::ArrayPtr<const StructuralIdentityParent> parents, ast::NodeId node) {
  uint32_t ordinal = 0;
  for (const auto& candidate : inventory.definitions()) {
    if (candidate.node.value < node.value && candidate.moduleNode == moduleNode &&
        sameParentPath(candidate.parentPath.asPtr(), parents)) {
      ++ordinal;
    }
  }
  for (const auto& candidate : inventory.impls()) {
    if (candidate.node.value < node.value && candidate.moduleNode == moduleNode &&
        sameParentPath(candidate.parentPath.asPtr(), parents)) {
      ++ordinal;
    }
  }
  return ordinal;
}

const DefinitionInventoryEntry& inventoryDefinition(const DefinitionInventory& inventory,
                                                    ast::NodeId node) {
  for (const auto& entry : inventory.definitions()) {
    if (entry.node == node) { return entry; }
  }
  ZC_FAIL_REQUIRE("definition parent fixture is missing");
}

const ImplInventoryEntry& inventoryImpl(const DefinitionInventory& inventory, ast::NodeId node) {
  for (const auto& entry : inventory.impls()) {
    if (entry.node == node) { return entry; }
  }
  ZC_FAIL_REQUIRE("implementation parent fixture is missing");
}

identity::DefinitionPathSegment definitionSegment(const ParsedSource& parsed,
                                                  const identity::ImmutableSourceSnapshot& snapshot,
                                                  const DefinitionInventory& inventory,
                                                  const DefinitionInventoryEntry& entry,
                                                  identity::DefinitionKind kind) {
  zc::Maybe<identity::DefinitionNameKey> name;
  if (entry.nameKind == InventoryDefinitionNameKind::Declared) {
    ZC_IF_SOME(value, identity::DeclaredDefinitionName::fromSource(
                          parsed.tree.ident(entry.declaredName))) {
      name = identity::DefinitionNameKey::declared(zc::mv(value));
    }
  } else {
    ZC_IF_SOME(role, entry.anonymousRole) { name = identity::DefinitionNameKey::anonymous(role); }
  }
  const auto start = parsed.sources->getLocOffsetInBuffer(entry.source.getStart(), parsed.buffer);
  const auto end = parsed.sources->getLocOffsetInBuffer(entry.source.getEnd(), parsed.buffer);
  auto span = snapshot.span(start, end);
  ZC_IF_SOME(nameValue, name) {
    ZC_IF_SOME(spanValue, span) {
      auto segment = identity::DefinitionPathSegment::from(
          kind, zc::mv(nameValue), zc::mv(spanValue),
          siblingOrdinal(inventory, entry.moduleNode, entry.parentPath.asPtr(), entry.node));
      ZC_IF_SOME(value, segment) { return zc::mv(value); }
    }
  }
  ZC_FAIL_REQUIRE("definition segment fixture failed");
}

identity::DefinitionKey definitionKey(const ParsedSource& parsed,
                                      const identity::ImmutableSourceSnapshot& snapshot,
                                      const DefinitionInventory& inventory,
                                      const DefinitionInventoryEntry& entry,
                                      bool wrongKind = false) {
  zc::Vector<identity::DefinitionPathComponent> path;
  for (const auto& parent : entry.parentPath) {
    if (parent.kind == StructuralIdentityParentKind::Definition) {
      const auto& definition = inventoryDefinition(inventory, parent.node);
      path.add(identity::DefinitionPathComponent::definition(
          definitionSegment(parsed, snapshot, inventory, definition, definition.kind)));
    } else {
      const auto& implementation = inventoryImpl(inventory, parent.node);
      const auto start =
          parsed.sources->getLocOffsetInBuffer(implementation.source.getStart(), parsed.buffer);
      const auto end =
          parsed.sources->getLocOffsetInBuffer(implementation.source.getEnd(), parsed.buffer);
      ZC_IF_SOME(span, snapshot.span(start, end)) {
        path.add(identity::DefinitionPathComponent::impl(identity::ImplPathSegment::from(
            zc::mv(span), siblingOrdinal(inventory, implementation.moduleNode,
                                         implementation.parentPath.asPtr(), implementation.node))));
      }
    }
  }
  const auto kind = wrongKind ? identity::DefinitionKind::Class : entry.kind;
  path.add(identity::DefinitionPathComponent::definition(
      definitionSegment(parsed, snapshot, inventory, entry, kind)));
  ZC_IF_SOME(value, identity::DefinitionKey::from(module(), zc::mv(path))) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("definition key fixture failed");
}

identity::ImplKey implKey(const ParsedSource& parsed,
                          const identity::ImmutableSourceSnapshot& snapshot,
                          const DefinitionInventory& inventory, const ImplInventoryEntry& entry,
                          bool wrongOrdinal = false) {
  zc::Vector<identity::DefinitionPathSegment> parentPath;
  for (const auto& parent : entry.parentPath) {
    ZC_REQUIRE(parent.kind == StructuralIdentityParentKind::Definition);
    const auto& definition = inventoryDefinition(inventory, parent.node);
    parentPath.add(definitionSegment(parsed, snapshot, inventory, definition, definition.kind));
  }
  const auto start = parsed.sources->getLocOffsetInBuffer(entry.source.getStart(), parsed.buffer);
  const auto end = parsed.sources->getLocOffsetInBuffer(entry.source.getEnd(), parsed.buffer);
  ZC_IF_SOME(span, snapshot.span(start, end)) {
    auto value = identity::ImplKey::from(
        module(), zc::mv(parentPath), zc::mv(span),
        siblingOrdinal(inventory, entry.moduleNode, entry.parentPath.asPtr(), entry.node) +
            (wrongOrdinal ? 1 : 0));
    ZC_IF_SOME(result, value) { return zc::mv(result); }
  }
  ZC_FAIL_REQUIRE("implementation key fixture failed");
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

enum class ImplRegistration : uint8_t { None, Exact, WrongOrdinal };

struct FrozenFixture final {
  explicit FrozenFixture(ParsedSource& sourceFixture, bool includeDefinition = false,
                         bool wrongDefinitionKind = false,
                         ImplRegistration implRegistration = ImplRegistration::None)
      : context(requireContext(factory)), registries(createRegistries()) {
    auto snapshot = sourceFixture.snapshot();
    const auto inventory = DefinitionInventory::collect(sourceFixture.tree);
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
      for (size_t index = 0; index < inventory.definitions().size(); ++index) {
        auto key = definitionKey(sourceFixture, snapshot, inventory, inventory.definitions()[index],
                                 wrongDefinitionKind && index == 0);
        ZC_REQUIRE(registries.collectDefinition(zc::mv(key), index) ==
                   identity::FrozenRegistryFailure::None);
      }
      ZC_REQUIRE(registries.freezeDefinitions() == identity::FrozenRegistryFailure::None);
      for (size_t index = 0; index < inventory.definitions().size(); ++index) {
        const auto& entry = inventory.definitions()[index];
        auto key = definitionKey(sourceFixture, snapshot, inventory, entry,
                                 wrongDefinitionKind && index == 0);
        const auto identity = requireHandle(registries.definitions().find(key));
        if (index == 0) { definitionId = identity; }
        ZC_REQUIRE(rawDefinitions.insert(entry.node, identity));
      }
    } else {
      ZC_REQUIRE(registries.freezeDefinitions() == identity::FrozenRegistryFailure::None);
    }
    if (implRegistration != ImplRegistration::None) {
      ZC_REQUIRE(inventory.impls().size() == 1);
      const auto& implementation = inventory.impls()[0];
      auto key = implKey(sourceFixture, snapshot, inventory, implementation,
                         implRegistration == ImplRegistration::WrongOrdinal);
      auto retained = key.clone();
      ZC_REQUIRE(registries.collectImpl(zc::mv(key)) == identity::FrozenRegistryFailure::None);
      ZC_REQUIRE(registries.freezeImpls() == identity::FrozenRegistryFailure::None);
      implId = requireHandle(registries.impls().find(retained));
    } else {
      ZC_REQUIRE(registries.freezeImpls() == identity::FrozenRegistryFailure::None);
    }
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
  identity::ImplId implId;
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

const BinderInvariantFact& requireBinderInvariant(BindingVerificationResult& result) {
  ZC_REQUIRE(result.is<InvariantRejected>());
  auto& rejected = result.get<InvariantRejected>();
  ZC_REQUIRE(rejected.failures().size() == 1);
  ZC_REQUIRE(rejected.failures()[0].value.is<BinderInvariantFact>());
  return rejected.failures()[0].value.get<BinderInvariantFact>();
}

const identity::IdentityInvariant& requireIdentityInvariant(BindingVerificationResult& result) {
  ZC_REQUIRE(result.is<InvariantRejected>());
  auto& rejected = result.get<InvariantRejected>();
  ZC_REQUIRE(rejected.failures().size() == 1);
  ZC_REQUIRE(rejected.failures()[0].value.is<identity::IdentityInvariant>());
  return rejected.failures()[0].value.get<identity::IdentityInvariant>();
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
    ZC_REQUIRE(definitions.definitions()[0].site.value().is<DeclarationDefinitionSite>());
    ZC_EXPECT(definitions.definitions()[0].site.value().get<DeclarationDefinitionSite>().node ==
              definitions.definitions()[0].node);
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

ZC_TEST("ExportSurfaceRevision.MatchesNormativeEmptyMapOracle") {
  const identity::Sha256Digest zeroFingerprint;
  const uint8_t moduleBytes[] = {0xa1};
  const uint8_t packageBytes[] = {0xb2};
  const uint8_t emptyMap[] = {0, 0, 0, 0, 0, 0, 0, 0};
  auto revision = ExportSurfaceRevision::computeFramed(
      zeroFingerprint, zc::arrayPtr(moduleBytes), zc::arrayPtr(packageBytes),
      zc::arrayPtr(emptyMap), zc::arrayPtr(emptyMap));
  ZC_IF_SOME(value, revision) {
    ZC_EXPECT(zc::encodeHex(value.digest().bytes()) ==
              "54283a8bbfd0e89237271ac1162646118a16bbb59b776c011ce69c2bf30a5ed0"_zc);
    return;
  }
  ZC_EXPECT(false);
}

ZC_TEST("BindingAllocationDump.MatchesNormativeRFC0004Oracle") {
  const zc::StringPtr scopeHex[] = {
      "a1000000000001a101d0"_zc,         "a100000001010000000002b102d1"_zc,
      "a100000002010000000102b203d2"_zc, "a100000003010000000002b304d3"_zc,
      "a100000004010000000003c405d4"_zc, "a100000005010000000001a106d5"_zc,
      "a100000006010000000001a107d6"_zc, "a100000007010000000102b108d7"_zc,
      "a100000008010000000702b109d8"_zc, "a100000009010000000102b10ad9"_zc,
      "a10000000a010000000102b106da"_zc,
  };
  const zc::StringPtr labelHex[] = {
      "01a100000000e20200000006f1"_zc,
      "02b100000000e1010000000af0"_zc,
  };
  zc::Vector<zc::Array<uint8_t>> scopeStorage;
  zc::Vector<zc::Array<uint8_t>> labelStorage;
  for (const auto value : scopeHex) { scopeStorage.add(decodeHexBytes(value)); }
  for (const auto value : labelHex) { labelStorage.add(decodeHexBytes(value)); }
  zc::Vector<zc::ArrayPtr<const uint8_t>> scopes;
  zc::Vector<zc::ArrayPtr<const uint8_t>> labels;
  for (const auto& value : scopeStorage) { scopes.add(value.asPtr()); }
  for (const auto& value : labelStorage) { labels.add(value.asPtr()); }

  const auto dump = frameBindingAllocationDump(scopes.asPtr(), labels.asPtr());
  ZC_EXPECT(dump.size() == 327);
  ZC_IF_SOME(digest, identity::sha256(dump.asPtr())) {
    ZC_EXPECT(zc::encodeHex(digest.bytes()) ==
              "0212bdaf38dc3f7d85f4afc2d7413e27777c3dfe139be4d7c18896a839d4b7f8"_zc);
    return;
  }
  ZC_EXPECT(false);
}

ZC_TEST("BindingVerifier.PublishesCompletePrivateFunctionFactsAndSurface") {
  ParsedSource sourceFixture("module root;\nfun run();\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());
  auto verification =
      BindingVerifier::verify(input, zc::mv(candidate.get<BindingMetadataCandidate>()));
  ZC_REQUIRE(verification.is<VerifiedBindingOutput>());
  const auto& output = verification.get<VerifiedBindingOutput>();
  ZC_EXPECT(output.metadata.nodeScopes().size() == input.tree().nodeCount());
  ZC_REQUIRE(output.metadata.scopes().size() == 3);
  ZC_EXPECT(output.metadata.scopes()[0].id.index() == 0);
  ZC_EXPECT(output.metadata.scopes()[0].kind == ScopeKind::Module);
  ZC_EXPECT(output.metadata.scopes()[0].bindings.size() == 1);
  ZC_EXPECT(output.metadata.scopes()[1].id.index() == 1);
  ZC_EXPECT(output.metadata.scopes()[1].kind == ScopeKind::Function);
  ZC_EXPECT(output.metadata.scopes()[2].id.index() == 2);
  ZC_EXPECT(output.metadata.scopes()[2].kind == ScopeKind::Block);
  ZC_EXPECT(output.metadata.scopes()[1].source.byteStart() <=
            output.metadata.scopes()[2].source.byteStart());
  ZC_EXPECT(output.metadata.scopes()[2].source.byteEnd() <=
            output.metadata.scopes()[1].source.byteEnd());
  ZC_REQUIRE(output.metadata.definitions().size() == 1);
  ZC_EXPECT(output.metadata.definitions()[0].declaringScope.index() == 0);
  ZC_EXPECT(output.metadata.definitions()[0].activation == DefinitionActivation::ModuleSkeleton);
  ZC_EXPECT(output.metadata.nodeBindings().size() == 0);
  ZC_EXPECT(output.metadata.impls().size() == 0);
  ZC_EXPECT(output.metadata.moduleAliases().size() == 0);
  ZC_EXPECT(output.metadata.imports().size() == 0);
  ZC_EXPECT(output.metadata.localExports().size() == 0);
  ZC_EXPECT(output.metadata.deferredMembers().size() == 0);
  ZC_EXPECT(output.metadata.labels().size() == 0);
  ZC_EXPECT(output.metadata.controlTransfers().size() == 0);
  ZC_EXPECT(output.metadata.shadowTargets().size() == 0);
  ZC_EXPECT(output.metadata.closureFreeVariables().size() == 0);
  ZC_EXPECT(output.surface.visibleEntries().size() == 1);
  ZC_EXPECT(output.surface.exports().size() == 0);
  ZC_EXPECT(!output.surface.visibleEntries()[0].exported);
  ZC_EXPECT(output.surface.visibleEntries()[0].visibility.value().is<ModuleVisibility>());
  ZC_EXPECT(zc::encodeHex(output.surface.revision().digest().bytes()) ==
            "1764a287bf612ee8a648563f8f525b36ef5e7de5f8238a8c97194bd99796722b"_zc);
  ZC_IF_SOME(dump, encodeBindingAllocationDump(input, output.metadata.scopes())) {
    ZC_EXPECT(dump.size() == 3227);
    ZC_IF_SOME(digest, identity::sha256(dump.asPtr())) {
      ZC_EXPECT(zc::encodeHex(digest.bytes()) ==
                "2c5b3604e7bb003b11cff64d1b19af3405ab1940b4379846faba3a05754a9cb6"_zc);
    }
  }
}

ZC_TEST("BindingVerifier.RejectsMissingAndMalformedFunctionScopes") {
  ParsedSource missingSource("module root;\nfun run();\n"_zc);
  FrozenFixture missingFixture(missingSource, true);
  auto missingInputResult = verify(missingFixture);
  ZC_REQUIRE(missingInputResult.is<VerifiedBindingInput>());
  auto missingInput = zc::mv(missingInputResult.get<VerifiedBindingInput>());
  auto missingCandidate = BindingBuilder::build(missingInput, *missingSource.diagnostics);
  ZC_REQUIRE(missingCandidate.is<BindingMetadataCandidate>());
  missingCandidate.get<BindingMetadataCandidate>().scopes.removeLast();
  auto missing = BindingVerifier::verify(missingInput,
                                         zc::mv(missingCandidate.get<BindingMetadataCandidate>()));
  ZC_EXPECT(requireBinderInvariant(missing).kind == BinderInvariantKind::MissingRequiredResolution);

  ParsedSource malformedSource("module root;\nfun run();\n"_zc);
  FrozenFixture malformedFixture(malformedSource, true);
  auto malformedInputResult = verify(malformedFixture);
  ZC_REQUIRE(malformedInputResult.is<VerifiedBindingInput>());
  auto malformedInput = zc::mv(malformedInputResult.get<VerifiedBindingInput>());
  auto malformedCandidate = BindingBuilder::build(malformedInput, *malformedSource.diagnostics);
  ZC_REQUIRE(malformedCandidate.is<BindingMetadataCandidate>());
  malformedCandidate.get<BindingMetadataCandidate>().scopes[1].parent = zc::none;
  auto malformed = BindingVerifier::verify(
      malformedInput, zc::mv(malformedCandidate.get<BindingMetadataCandidate>()));
  ZC_EXPECT(requireBinderInvariant(malformed).kind == BinderInvariantKind::MalformedScopeGraph);
}

ZC_TEST("BindingVerifier.ClassifiesAdditionalFactsAndWrongScopeKinds") {
  ParsedSource additionalSource("module root;\nfun run();\n"_zc);
  FrozenFixture additionalFixture(additionalSource, true);
  auto additionalInputResult = verify(additionalFixture);
  ZC_REQUIRE(additionalInputResult.is<VerifiedBindingInput>());
  auto additionalInput = zc::mv(additionalInputResult.get<VerifiedBindingInput>());
  auto additionalCandidate = BindingBuilder::build(additionalInput, *additionalSource.diagnostics);
  ZC_REQUIRE(additionalCandidate.is<BindingMetadataCandidate>());
  auto& additionalValue = additionalCandidate.get<BindingMetadataCandidate>();
  additionalValue.nodeScopes.add(additionalValue.nodeScopes[0]);
  auto additional = BindingVerifier::verify(additionalInput, zc::mv(additionalValue));
  ZC_EXPECT(requireBinderInvariant(additional).kind == BinderInvariantKind::InvalidBindingFact);

  ParsedSource kindSource("module root;\nfun run();\n"_zc);
  FrozenFixture kindFixture(kindSource, true);
  auto kindInputResult = verify(kindFixture);
  ZC_REQUIRE(kindInputResult.is<VerifiedBindingInput>());
  auto kindInput = zc::mv(kindInputResult.get<VerifiedBindingInput>());
  auto kindCandidate = BindingBuilder::build(kindInput, *kindSource.diagnostics);
  ZC_REQUIRE(kindCandidate.is<BindingMetadataCandidate>());
  auto& kindValue = kindCandidate.get<BindingMetadataCandidate>();
  kindValue.scopes[2].kind = ScopeKind::Loop;
  auto wrongKind = BindingVerifier::verify(kindInput, zc::mv(kindValue));
  ZC_EXPECT(requireBinderInvariant(wrongKind).kind == BinderInvariantKind::MalformedScopeGraph);
}

ZC_TEST("BindingVerifier.RejectsWrongDeclaringScopeAndExternalPrivateSurface") {
  ParsedSource scopeSource("module root;\nfun run();\n"_zc);
  FrozenFixture scopeFixture(scopeSource, true);
  auto scopeInputResult = verify(scopeFixture);
  ZC_REQUIRE(scopeInputResult.is<VerifiedBindingInput>());
  auto scopeInput = zc::mv(scopeInputResult.get<VerifiedBindingInput>());
  auto scopeCandidate = BindingBuilder::build(scopeInput, *scopeSource.diagnostics);
  ZC_REQUIRE(scopeCandidate.is<BindingMetadataCandidate>());
  auto& scopeValue = scopeCandidate.get<BindingMetadataCandidate>();
  scopeValue.definitions[0].declaringScope = scopeValue.scopes[1].id;
  auto wrongScope = BindingVerifier::verify(scopeInput, zc::mv(scopeValue));
  ZC_EXPECT(requireBinderInvariant(wrongScope).kind == BinderInvariantKind::InvalidBindingFact);

  ParsedSource surfaceSource("module root;\nfun run();\n"_zc);
  FrozenFixture surfaceFixture(surfaceSource, true);
  auto surfaceInputResult = verify(surfaceFixture);
  ZC_REQUIRE(surfaceInputResult.is<VerifiedBindingInput>());
  auto surfaceInput = zc::mv(surfaceInputResult.get<VerifiedBindingInput>());
  auto surfaceCandidate = BindingBuilder::build(surfaceInput, *surfaceSource.diagnostics);
  ZC_REQUIRE(surfaceCandidate.is<BindingMetadataCandidate>());
  auto& surfaceValue = surfaceCandidate.get<BindingMetadataCandidate>();
  surfaceValue.currentSurface.visibleEntries[0].visibility = VisibilityEnvelope::external();
  surfaceValue.currentSurface.visibleEntries[0].exported = true;
  auto copiedSurface = surfaceValue.currentSurface.clone();
  surfaceValue.currentSurface.exports.add(zc::mv(copiedSurface.visibleEntries[0]));
  auto wrongSurface = BindingVerifier::verify(surfaceInput, zc::mv(surfaceValue));
  ZC_EXPECT(requireBinderInvariant(wrongSurface).kind == BinderInvariantKind::InvalidBindingFact);
}

ZC_TEST("BindingVerifier.RejectsStaleExportSurfaceRevision") {
  ParsedSource sourceFixture("module root;\nfun run();\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto candidate = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(candidate.is<BindingMetadataCandidate>());

  const identity::Sha256Digest zeroFingerprint;
  const uint8_t moduleBytes[] = {0xa1};
  const uint8_t packageBytes[] = {0xb2};
  const uint8_t emptyMap[] = {0, 0, 0, 0, 0, 0, 0, 0};
  auto stale = ExportSurfaceRevision::computeFramed(zeroFingerprint, zc::arrayPtr(moduleBytes),
                                                    zc::arrayPtr(packageBytes),
                                                    zc::arrayPtr(emptyMap), zc::arrayPtr(emptyMap));
  ZC_REQUIRE(stale != zc::none);
  ZC_IF_SOME(staleValue, stale) {
    candidate.get<BindingMetadataCandidate>().currentSurface.revision = staleValue;
  }
  auto result = BindingVerifier::verify(input, zc::mv(candidate.get<BindingMetadataCandidate>()));
  const auto& fact = requireBinderInvariant(result);
  ZC_EXPECT(fact.kind == BinderInvariantKind::InvalidBindingFact);
  ZC_EXPECT(fact.emitterSite == BinderEmitterSite::BindingVerifier);
}

ZC_TEST("BindingVerifier.RejectsForeignSurfaceAndScopeIdentities") {
  ParsedSource localSource("module root;\nfun run();\n"_zc);
  ParsedSource foreignSource("module root;\nfun run();\n"_zc);
  FrozenFixture localFixture(localSource, true);
  FrozenFixture foreignFixture(foreignSource, true);
  auto localInputResult = verify(localFixture);
  auto foreignInputResult = verify(foreignFixture);
  ZC_REQUIRE(localInputResult.is<VerifiedBindingInput>());
  ZC_REQUIRE(foreignInputResult.is<VerifiedBindingInput>());
  auto localInput = zc::mv(localInputResult.get<VerifiedBindingInput>());
  auto foreignInput = zc::mv(foreignInputResult.get<VerifiedBindingInput>());
  auto foreignCandidate = BindingBuilder::build(foreignInput, *foreignSource.diagnostics);
  ZC_REQUIRE(foreignCandidate.is<BindingMetadataCandidate>());

  auto surfaceCandidate = BindingBuilder::build(localInput, *localSource.diagnostics);
  ZC_REQUIRE(surfaceCandidate.is<BindingMetadataCandidate>());
  auto& surface = surfaceCandidate.get<BindingMetadataCandidate>().currentSurface;
  surface.sourceModule = foreignInput.module();
  surface.sourcePackage = foreignInput.package();
  auto surfaceResult =
      BindingVerifier::verify(localInput, zc::mv(surfaceCandidate.get<BindingMetadataCandidate>()));
  ZC_EXPECT(requireIdentityInvariant(surfaceResult).kind() ==
            identity::IdentityInvariantKind::ForeignContext);

  auto targetCandidate = BindingBuilder::build(localInput, *localSource.diagnostics);
  ZC_REQUIRE(targetCandidate.is<BindingMetadataCandidate>());
  targetCandidate.get<BindingMetadataCandidate>().currentSurface.visibleEntries[0].bindingIdentity =
      BindingTarget::definition(foreignFixture.definitionId);
  auto targetResult =
      BindingVerifier::verify(localInput, zc::mv(targetCandidate.get<BindingMetadataCandidate>()));
  ZC_EXPECT(requireIdentityInvariant(targetResult).kind() ==
            identity::IdentityInvariantKind::ForeignContext);

  auto rangeCandidate = BindingBuilder::build(localInput, *localSource.diagnostics);
  ZC_REQUIRE(rangeCandidate.is<BindingMetadataCandidate>());
  auto alternateSnapshot =
      identity::ImmutableSourceSnapshot::from(alternateSource(), zc::heapArray("x"_zcb));
  ZC_REQUIRE(alternateSnapshot != zc::none);
  ZC_IF_SOME(snapshot, alternateSnapshot) {
    auto alternateSpan = snapshot.span(0, 1);
    ZC_REQUIRE(alternateSpan != zc::none);
    ZC_IF_SOME(span, alternateSpan) {
      rangeCandidate.get<BindingMetadataCandidate>().definitions[0].source = zc::mv(span);
    }
  }
  auto rangeResult =
      BindingVerifier::verify(localInput, zc::mv(rangeCandidate.get<BindingMetadataCandidate>()));
  ZC_EXPECT(requireIdentityInvariant(rangeResult).kind() ==
            identity::IdentityInvariantKind::InvalidSourceRange);

  auto boundedCandidate = BindingBuilder::build(localInput, *localSource.diagnostics);
  ZC_REQUIRE(boundedCandidate.is<BindingMetadataCandidate>());
  auto oversizedSnapshot = identity::ImmutableSourceSnapshot::from(
      source(),
      zc::heapArray("module root;\nfun run();\nbytes beyond the verified source snapshot\n"_zcb));
  ZC_REQUIRE(oversizedSnapshot != zc::none);
  ZC_IF_SOME(snapshot, oversizedSnapshot) {
    auto oversizedSpan = snapshot.span(0, snapshot.bytes().size());
    ZC_REQUIRE(oversizedSpan != zc::none);
    ZC_IF_SOME(span, oversizedSpan) {
      boundedCandidate.get<BindingMetadataCandidate>().definitions[0].source = zc::mv(span);
    }
  }
  auto boundedResult =
      BindingVerifier::verify(localInput, zc::mv(boundedCandidate.get<BindingMetadataCandidate>()));
  ZC_EXPECT(requireIdentityInvariant(boundedResult).kind() ==
            identity::IdentityInvariantKind::InvalidSourceRange);

  auto scopeCandidate = BindingBuilder::build(localInput, *localSource.diagnostics);
  ZC_REQUIRE(scopeCandidate.is<BindingMetadataCandidate>());
  auto& scopes = scopeCandidate.get<BindingMetadataCandidate>();
  auto& foreignScopes = foreignCandidate.get<BindingMetadataCandidate>();
  ZC_REQUIRE(scopes.scopes.size() == foreignScopes.scopes.size());
  ZC_REQUIRE(scopes.nodeScopes.size() == foreignScopes.nodeScopes.size());
  for (size_t index = 0; index < scopes.scopes.size(); ++index) {
    scopes.scopes[index].id = foreignScopes.scopes[index].id;
    scopes.scopes[index].parent = foreignScopes.scopes[index].parent;
  }
  for (size_t index = 0; index < scopes.nodeScopes.size(); ++index) {
    scopes.nodeScopes[index].scope = foreignScopes.nodeScopes[index].scope;
  }
  scopes.definitions[0].declaringScope = foreignScopes.definitions[0].declaringScope;
  auto scopeResult = BindingVerifier::verify(localInput, zc::mv(scopes));
  ZC_EXPECT(requireIdentityInvariant(scopeResult).kind() ==
            identity::IdentityInvariantKind::ForeignContext);
}

ZC_TEST("BindingBuilder.DefersIdentifierResolutionBeforePublishingMetadata") {
  ParsedSource sourceFixture("module root;\nfun run() { missing; }\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto input = zc::mv(inputResult.get<VerifiedBindingInput>());
  auto result = BindingBuilder::build(input, *sourceFixture.diagnostics);
  ZC_REQUIRE(result.is<BinderInvariantFact>());
  ZC_EXPECT(result.get<BinderInvariantFact>().kind ==
            BinderInvariantKind::MissingRequiredResolution);
  ZC_EXPECT(sourceFixture.diagnostics->errorCount() == 0);
}

ZC_TEST("BinderInvariant.EmitsEveryRegisteredFatalDiagnostic") {
  class Capture final : public diagnostics::DiagnosticConsumer {
  public:
    zc::Vector<diagnostics::DiagID> ids;
    zc::Vector<zc::String> occurrences;

    void handleDiagnostic(const source::SourceManager&,
                          const diagnostics::Diagnostic& diagnostic) override {
      ids.add(diagnostic.getId());
      ZC_REQUIRE(diagnostic.getArgs().size() == 1);
      const auto& argument = diagnostic.getArgs()[0];
      if (argument.is<zc::String>()) {
        occurrences.add(zc::str(argument.get<zc::String>()));
      } else if (argument.is<zc::StringPtr>()) {
        occurrences.add(zc::str(argument.get<zc::StringPtr>()));
      } else {
        ZC_FAIL_REQUIRE("binder invariant occurrence argument is not a string");
      }
    }
  };

  ParsedSource sourceFixture("module root;\nfun run();\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  source::SourceManager sources;
  diagnostics::DiagnosticEngine diagnostics(sources);
  auto consumer = zc::heap<Capture>();
  const auto& captured = *consumer;
  diagnostics.addConsumer(zc::mv(consumer));
  const BinderInvariantKind kinds[] = {
      BinderInvariantKind::MalformedScopeGraph,
      BinderInvariantKind::MissingRequiredResolution,
      BinderInvariantKind::AliasCycle,
      BinderInvariantKind::InvalidBindingFact,
      BinderInvariantKind::InvalidEmitterOrdinal,
  };
  const diagnostics::DiagID expected[] = {
      diagnostics::DiagID::BinderMalformedScopeGraph,
      diagnostics::DiagID::BinderMissingRequiredResolution,
      diagnostics::DiagID::BinderAliasCycle,
      diagnostics::DiagID::BinderInvalidFact,
      diagnostics::DiagID::BinderInvalidEmitterOrdinal,
  };
  for (uint32_t index = 0; index < 5; ++index) {
    zc::Maybe<identity::UnbrandedSourceRange> noRange;
    emitBinderInvariant(diagnostics,
                        BinderInvariantFact{kinds[index], fixture.moduleId, zc::mv(noRange),
                                            BinderEmitterSite::BindingVerifier, index});
  }

  ZC_REQUIRE(captured.ids.size() == 5);
  ZC_REQUIRE(captured.occurrences.size() == 5);
  for (uint32_t index = 0; index < 5; ++index) {
    ZC_EXPECT(captured.ids[index] == expected[index]);
    ZC_EXPECT(diagnostics::getDiagnosticInfo(captured.ids[index]).severity ==
              diagnostics::DiagSeverity::kFatal);
    ZC_EXPECT(captured.occurrences[index] == "1"_zc);
  }
  ZC_EXPECT(diagnostics.hasErrors());

  zc::Vector<BinderInvariantFact> repeated;
  for (const uint32_t ordinal : {9u, 2u, 5u}) {
    zc::Maybe<identity::UnbrandedSourceRange> noRange;
    repeated.add(BinderInvariantFact{BinderInvariantKind::InvalidBindingFact, fixture.moduleId,
                                     zc::mv(noRange), BinderEmitterSite::BindingVerifier, ordinal});
  }
  auto groups = groupBinderInvariants(repeated.asPtr());
  ZC_REQUIRE(groups != zc::none);
  diagnostics::DiagnosticEngine groupedDiagnostics(sources);
  auto groupedConsumer = zc::heap<Capture>();
  const auto& groupedCapture = *groupedConsumer;
  groupedDiagnostics.addConsumer(zc::mv(groupedConsumer));
  ZC_IF_SOME(groupValues, groups) {
    ZC_REQUIRE(groupValues.size() == 1);
    ZC_EXPECT(groupValues[0].occurrenceCount() == 3);
    emitBinderInvariantGroups(groupedDiagnostics, groupValues.asPtr());
  }

  ZC_REQUIRE(groupedCapture.ids.size() == 1);
  ZC_REQUIRE(groupedCapture.occurrences.size() == 1);
  ZC_EXPECT(groupedCapture.ids[0] == diagnostics::DiagID::BinderInvalidFact);
  ZC_EXPECT(groupedCapture.occurrences[0] == "3"_zc);
  ZC_EXPECT(groupedDiagnostics.hasErrors());
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

ZC_TEST("FrozenInventory.PublishesStandaloneAndMarkerImplIdentities") {
  for (const auto sourceText : {"module root;\nimpl Trait for Target {}\n"_zc,
                                "module root;\nimpl !Shared for Target;\n"_zc}) {
    ParsedSource sourceFixture(sourceText);
    FrozenFixture fixture(sourceFixture, false, false, ImplRegistration::Exact);
    ZC_EXPECT(fixture.inventoryFailure == zc::none);
    ZC_IF_SOME(inventory, fixture.frozenDefinitions) {
      ZC_REQUIRE(inventory.impls().size() == 1);
      ZC_EXPECT(inventory.impls()[0].implementation == fixture.implId);
      ZC_EXPECT(inventory.implAt(inventory.impls()[0].node) == fixture.implId);
    }
    else { ZC_EXPECT(false); }
  }
}

ZC_TEST("FrozenInventory.RejectsMissingImplIdentity") {
  ParsedSource sourceFixture("module root;\nimpl Trait for Target {}\n"_zc);
  FrozenFixture fixture(sourceFixture);
  ZC_IF_SOME(kind, fixture.inventoryFailure) {
    ZC_EXPECT(kind == FrozenInventoryInvariantKind::IncompleteInventory);
  }
  else { ZC_EXPECT(false); }
}

ZC_TEST("FrozenInventory.RejectsWrongImplIdentity") {
  ParsedSource sourceFixture("module root;\nimpl Trait for Target {}\n"_zc);
  FrozenFixture fixture(sourceFixture, false, false, ImplRegistration::WrongOrdinal);
  ZC_IF_SOME(kind, fixture.inventoryFailure) {
    ZC_EXPECT(kind == FrozenInventoryInvariantKind::InvalidDefinitionIdentity);
  }
  else { ZC_EXPECT(false); }
}

ZC_TEST("ScopeArena.AllocatesStructuralScopesInSchemaPreorder") {
  ParsedSource sourceFixture(
      "module root;\n"
      "fun run() {\n"
      "  while (true) {}\n"
      "  for (;;) {}\n"
      "  do {} while (true);\n"
      "  match (true) { when true => {} }\n"
      "  unsafe {};\n"
      "}\n"_zc);
  FrozenFixture fixture(sourceFixture, true);
  auto inputResult = verify(fixture);
  ZC_REQUIRE(inputResult.is<VerifiedBindingInput>());
  auto arenaResult = ScopeArenaBuilder::build(inputResult.get<VerifiedBindingInput>());
  ZC_REQUIRE(arenaResult.is<ScopeArenaCandidate>());
  const auto& arena = arenaResult.get<ScopeArenaCandidate>();

  ZC_REQUIRE(arena.nodeScopes.size() == inputResult.get<VerifiedBindingInput>().tree().nodeCount());
  ZC_REQUIRE(arena.scopes.size() == 14);
  const ScopeKind expectedKinds[] = {
      ScopeKind::Module,      ScopeKind::Function, ScopeKind::Block,    ScopeKind::Loop,
      ScopeKind::Block,       ScopeKind::Loop,     ScopeKind::Block,    ScopeKind::Loop,
      ScopeKind::Block,       ScopeKind::Match,    ScopeKind::MatchArm, ScopeKind::Block,
      ScopeKind::UnsafeBlock, ScopeKind::Block,
  };
  const uint32_t expectedParents[] = {0, 0, 1, 2, 3, 2, 5, 2, 7, 2, 9, 10, 2, 12};
  for (size_t index = 0; index < arena.scopes.size(); ++index) {
    ZC_EXPECT(arena.scopes[index].id.index() == index);
    ZC_EXPECT(arena.scopes[index].kind == expectedKinds[index]);
    if (index == 0) {
      ZC_EXPECT(arena.scopes[index].parent == zc::none);
      ZC_EXPECT(arena.scopes[index].owner.value().is<ModuleScopeOwner>());
    } else {
      ZC_REQUIRE(arena.scopes[index].parent != zc::none);
      ZC_IF_SOME(parent, arena.scopes[index].parent) {
        ZC_EXPECT(parent.index() == expectedParents[index]);
        ZC_EXPECT(arena.scopes[parent.index()].source.byteStart() <=
                  arena.scopes[index].source.byteStart());
        ZC_EXPECT(arena.scopes[index].source.byteEnd() <=
                  arena.scopes[parent.index()].source.byteEnd());
      }
      ZC_EXPECT(arena.scopes[index].owner.value().is<DefinitionScopeOwner>());
    }
  }
  for (size_t index = 1; index < arena.nodeScopes.size(); ++index) {
    ZC_EXPECT(arena.nodeScopes[index - 1].node.value < arena.nodeScopes[index].node.value);
  }
}

ZC_TEST("ScopeArena.AssignsDefinitionAndImplOwners") {
  ParsedSource typeSource("module root;\nclass Box {}\n"_zc);
  FrozenFixture typeFixture(typeSource, true, true);
  auto typeInput = verify(typeFixture);
  ZC_REQUIRE(typeInput.is<VerifiedBindingInput>());
  auto typeArena = ScopeArenaBuilder::build(typeInput.get<VerifiedBindingInput>());
  ZC_REQUIRE(typeArena.is<ScopeArenaCandidate>());
  ZC_REQUIRE(typeArena.get<ScopeArenaCandidate>().scopes.size() == 2);
  ZC_EXPECT(typeArena.get<ScopeArenaCandidate>().scopes[1].kind == ScopeKind::TypeBody);
  ZC_EXPECT(
      typeArena.get<ScopeArenaCandidate>().scopes[1].owner.value().is<DefinitionScopeOwner>());

  ParsedSource implSource("module root;\nimpl Trait for Target {}\n"_zc);
  FrozenFixture implFixture(implSource, false, false, ImplRegistration::Exact);
  auto implInput = verify(implFixture);
  ZC_REQUIRE(implInput.is<VerifiedBindingInput>());
  auto implArena = ScopeArenaBuilder::build(implInput.get<VerifiedBindingInput>());
  ZC_REQUIRE(implArena.is<ScopeArenaCandidate>());
  ZC_REQUIRE(implArena.get<ScopeArenaCandidate>().scopes.size() == 2);
  ZC_EXPECT(implArena.get<ScopeArenaCandidate>().scopes[1].kind == ScopeKind::ImplBody);
  ZC_EXPECT(implArena.get<ScopeArenaCandidate>().scopes[1].owner.value().is<ImplScopeOwner>());
}

ZC_TEST("ScopeArena.RejectsScopeIndexOverflow") {
  ZC_EXPECT(checkedScopeIndex(UINT32_MAX) == UINT32_MAX);
  ZC_EXPECT(checkedScopeIndex(uint64_t(UINT32_MAX) + 1) == zc::none);
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
