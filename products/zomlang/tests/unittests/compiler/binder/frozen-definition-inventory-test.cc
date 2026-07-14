// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/frozen-definition-inventory.h"

#include "zc/core/vector.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/basic/string-pool.h"
#include "zomlang/compiler/basic/zomlang-opts.h"
#include "zomlang/compiler/binder/definition-inventory.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
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

identity::PackageKey packageKey() {
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

identity::CrateKey crateKey() {
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
        auto crate = identity::CrateKey::from(packageKey(), identity::CrateTargetKind::Library,
                                              requireScalar<identity::TargetName>("binder"_zc),
                                              zc::mv(configValue));
        ZC_IF_SOME(value, crate) { return zc::mv(value); }
      }
    }
  }
  ZC_FAIL_REQUIRE("invalid crate test input");
}

identity::SourceFileKey sourceKey() {
  zc::Vector<identity::CanonicalPathSegment> segments;
  segments.add(requireScalar<identity::CanonicalPathSegment>("dense.zom"_zc));
  auto path = identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(segments));
  return identity::SourceFileKey::from(crateKey(),
                                       identity::SourceOriginKey::localFile(zc::mv(path)));
}

identity::ModuleKey moduleKey() {
  zc::Vector<identity::ModulePathSegment> path;
  path.add(requireScalar<identity::ModulePathSegment>("root"_zc));
  zc::Maybe<identity::SourceSpan> noAnchor;
  auto module = identity::ModuleKey::from(crateKey(), zc::mv(path), sourceKey(), zc::mv(noAnchor));
  ZC_IF_SOME(value, module) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid module test input");
}

identity::SemanticContextBrand requireContext(identity::SemanticContextFactory& factory) {
  ZC_IF_SOME(context, factory.issue()) { return context; }
  ZC_FAIL_REQUIRE("semantic context test input exhausted");
}

template <typename Handle>
Handle requireHandle(zc::Maybe<Handle>&& value) {
  ZC_IF_SOME(handle, value) { return handle; }
  ZC_FAIL_REQUIRE("identity handle lookup failed");
}

parser::ParsedTokenSnapshot requireTokens(zc::Maybe<parser::ParsedTokenSnapshot>& tokens) {
  ZC_IF_SOME(value, tokens) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("parsed token snapshot is missing");
}

struct ParsedSource final {
  ParsedSource()
      : sources(zc::heap<source::SourceManager>()),
        diagnostics(zc::heap<diagnostics::DiagnosticEngine>(*sources)),
        buffer(sources->addMemBufferCopy(
            "module root;\nfun alpha();\nimpl Trait for Target {}\nfun omega();\n"_zc.asBytes(),
            "dense.zom")) {
    parser::Parser parser(*sources, *diagnostics, options, strings, buffer);
    ZC_IF_SOME(parsed, parser.parse()) { tree = zc::mv(parsed); }
    else { ZC_FAIL_REQUIRE("dense inventory source did not parse"); }
    ZC_REQUIRE(!diagnostics->hasErrors());
    tokens = parser.takeTokenSnapshot();
    ZC_REQUIRE(tokens != zc::none);
  }

  identity::ImmutableSourceSnapshot snapshot() const {
    auto value = identity::ImmutableSourceSnapshot::from(
        sourceKey(), zc::heapArray(sources->getEntireTextForBuffer(buffer)));
    ZC_IF_SOME(snapshot, value) { return zc::mv(snapshot); }
    ZC_FAIL_REQUIRE("source snapshot fixture failed");
  }

  zc::Own<source::SourceManager> sources;
  zc::Own<diagnostics::DiagnosticEngine> diagnostics;
  basic::LangOptions options;
  basic::StringPool strings;
  source::BufferId buffer;
  ast::Tree tree;
  zc::Maybe<parser::ParsedTokenSnapshot> tokens;
};

uint32_t siblingOrdinal(const DefinitionInventory& inventory, ast::NodeId node) {
  uint32_t ordinal = 0;
  for (const auto& candidate : inventory.definitions()) {
    ZC_REQUIRE(candidate.parentPath.empty());
    if (candidate.node.value < node.value) { ++ordinal; }
  }
  for (const auto& candidate : inventory.impls()) {
    ZC_REQUIRE(candidate.parentPath.empty());
    if (candidate.node.value < node.value) { ++ordinal; }
  }
  return ordinal;
}

identity::SourceSpan sourceSpan(const ParsedSource& parsed,
                                const identity::ImmutableSourceSnapshot& snapshot,
                                source::SourceRange range) {
  const auto start = parsed.sources->getLocOffsetInBuffer(range.getStart(), parsed.buffer);
  const auto end = parsed.sources->getLocOffsetInBuffer(range.getEnd(), parsed.buffer);
  ZC_IF_SOME(span, snapshot.span(start, end)) { return zc::mv(span); }
  ZC_FAIL_REQUIRE("source span fixture failed");
}

identity::DefinitionKey definitionKey(const ParsedSource& parsed,
                                      const identity::ImmutableSourceSnapshot& snapshot,
                                      const DefinitionInventory& inventory,
                                      const DefinitionInventoryEntry& entry) {
  ZC_REQUIRE(entry.parentPath.empty());
  ZC_REQUIRE(entry.nameKind == InventoryDefinitionNameKind::Declared);
  auto declared =
      identity::DeclaredDefinitionName::fromSource(parsed.tree.ident(entry.declaredName));
  ZC_REQUIRE(declared != zc::none);
  ZC_IF_SOME(name, declared) {
    auto segment = identity::DefinitionPathSegment::from(
        entry.kind, identity::DefinitionNameKey::declared(zc::mv(name)),
        sourceSpan(parsed, snapshot, entry.source), siblingOrdinal(inventory, entry.node));
    ZC_IF_SOME(segmentValue, segment) {
      zc::Vector<identity::DefinitionPathComponent> path;
      path.add(identity::DefinitionPathComponent::definition(zc::mv(segmentValue)));
      auto key = identity::DefinitionKey::from(moduleKey(), zc::mv(path));
      ZC_IF_SOME(value, key) { return zc::mv(value); }
    }
  }
  ZC_FAIL_REQUIRE("definition key fixture failed");
}

identity::ImplKey implKey(const ParsedSource& parsed,
                          const identity::ImmutableSourceSnapshot& snapshot,
                          const DefinitionInventory& inventory, const ImplInventoryEntry& entry) {
  ZC_REQUIRE(entry.parentPath.empty());
  zc::Vector<identity::DefinitionPathSegment> parentPath;
  auto key = identity::ImplKey::from(moduleKey(), zc::mv(parentPath),
                                     sourceSpan(parsed, snapshot, entry.source),
                                     siblingOrdinal(inventory, entry.node));
  ZC_IF_SOME(value, key) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("implementation key fixture failed");
}

struct DenseInventoryFixture final {
  explicit DenseInventoryFixture(bool omitLastDefinition = false)
      : context(requireContext(factory)), registries(createRegistries()) {
    auto snapshot = parsed.snapshot();
    const auto inventory = DefinitionInventory::collect(parsed.tree);
    nodeCount = parsed.tree.nodeCount();
    rootNode = parsed.tree.root();
    ZC_REQUIRE(inventory.modules().size() == 1);
    moduleNode = inventory.modules()[0].node;
    ZC_REQUIRE(inventory.definitions().size() == 2);
    ZC_REQUIRE(inventory.impls().size() == 1);

    ZC_REQUIRE(registries.collectPackage(packageKey()) == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezePackages() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.collectCrate(crateKey()) == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezeCrates() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.collectSourceFile(snapshot.clone()) ==
               identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezeSourceFiles() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.collectModule(moduleKey()) == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezeModules() == identity::FrozenRegistryFailure::None);

    for (size_t index = 0; index < inventory.definitions().size(); ++index) {
      auto key = definitionKey(parsed, snapshot, inventory, inventory.definitions()[index]);
      ZC_REQUIRE(registries.collectDefinition(zc::mv(key), index) ==
                 identity::FrozenRegistryFailure::None);
    }
    ZC_REQUIRE(registries.freezeDefinitions() == identity::FrozenRegistryFailure::None);
    for (size_t index = 0; index < inventory.definitions().size(); ++index) {
      const auto& entry = inventory.definitions()[index];
      auto key = definitionKey(parsed, snapshot, inventory, entry);
      const auto definition = requireHandle(registries.definitions().find(key));
      if (!omitLastDefinition || index + 1 != inventory.definitions().size()) {
        ZC_REQUIRE(definitions.insert(entry.node, definition));
      }
    }

    for (const auto& entry : inventory.impls()) {
      auto key = implKey(parsed, snapshot, inventory, entry);
      ZC_REQUIRE(registries.collectImpl(zc::mv(key)) == identity::FrozenRegistryFailure::None);
    }
    ZC_REQUIRE(registries.freezeImpls() == identity::FrozenRegistryFailure::None);

    const auto module = requireHandle(registries.modules().find(moduleKey()));
    ZC_REQUIRE(parsed.tokens != zc::none);
    auto retainedTokens = requireTokens(parsed.tokens);
    auto admission = ParsedModuleVerifier::admit(snapshot, *parsed.sources, parsed.buffer,
                                                 zc::mv(retainedTokens), zc::mv(parsed.tree));
    ZC_REQUIRE(admission.is<UnbrandedParsedModule>());
    auto promotion = ParsedModuleVerifier::promote(context, registries,
                                                   zc::mv(admission.get<UnbrandedParsedModule>()));
    ZC_REQUIRE(promotion.is<VerifiedParsedModule>());
    auto result = FrozenDefinitionInventoryVerifier::verifySingleModule(
        context, module, promotion.get<VerifiedParsedModule>(), registries, definitions);
    if (result.is<FrozenDefinitionInventoryView>()) {
      view = zc::mv(result.get<FrozenDefinitionInventoryView>());
    } else {
      failure = result.get<FrozenInventoryInvariantFact>().kind;
    }
  }

  identity::SemanticIdentityRegistrySet createRegistries() {
    ZC_IF_SOME(value, identity::SemanticIdentityRegistrySet::create(factory, context)) {
      return zc::mv(value);
    }
    ZC_FAIL_REQUIRE("registry set test input was already claimed");
  }

  ParsedSource parsed;
  identity::SemanticContextFactory factory;
  identity::SemanticContextBrand context;
  identity::SemanticIdentityRegistrySet registries;
  DefinitionIdentityMap definitions;
  size_t nodeCount = 0;
  ast::NodeId rootNode;
  ast::NodeId moduleNode;
  zc::Maybe<FrozenDefinitionInventoryView> view;
  zc::Maybe<FrozenInventoryInvariantKind> failure;
};

}  // namespace

ZC_TEST("FrozenDefinitionInventory.DenseLookupSeparatesDefinitionsAndImplementations") {
  DenseInventoryFixture fixture;
  ZC_REQUIRE(fixture.failure == zc::none);
  ZC_REQUIRE(fixture.view != zc::none);
  ZC_IF_SOME(view, fixture.view) {
    ZC_REQUIRE(view.definitions().size() == 2);
    ZC_REQUIRE(view.impls().size() == 1);
    for (const auto& entry : view.definitions()) {
      auto definition = view.definitionAt(entry.node);
      ZC_REQUIRE(definition != zc::none);
      ZC_IF_SOME(value, definition) { ZC_EXPECT(value == entry.definition); }
      ZC_EXPECT(view.implAt(entry.node) == zc::none);
    }
    for (const auto& entry : view.impls()) {
      auto implementation = view.implAt(entry.node);
      ZC_REQUIRE(implementation != zc::none);
      ZC_IF_SOME(value, implementation) { ZC_EXPECT(value == entry.implementation); }
      ZC_EXPECT(view.definitionAt(entry.node) == zc::none);
    }
  }
}

ZC_TEST("FrozenDefinitionInventory.DenseLookupRejectsHolesAndInvalidNodeIds") {
  DenseInventoryFixture fixture;
  ZC_REQUIRE(fixture.view != zc::none);
  ZC_IF_SOME(view, fixture.view) {
    zc::Vector<ast::NodeId> missingNodes;
    missingNodes.add(ast::NodeId());
    missingNodes.add(fixture.moduleNode);
    missingNodes.add(fixture.rootNode);
    missingNodes.add(ast::NodeId(static_cast<uint32_t>(fixture.nodeCount + 1)));
    missingNodes.add(ast::NodeId(UINT32_MAX));
    for (const auto node : missingNodes) {
      ZC_EXPECT(view.definitionAt(node) == zc::none);
      ZC_EXPECT(view.implAt(node) == zc::none);
    }
  }
}

ZC_TEST("FrozenDefinitionInventory.RejectsIncompleteDefinitionCensus") {
  DenseInventoryFixture fixture(true);
  ZC_REQUIRE(fixture.view == zc::none);
  ZC_REQUIRE(fixture.failure != zc::none);
  ZC_IF_SOME(kind, fixture.failure) {
    ZC_EXPECT(kind == FrozenInventoryInvariantKind::IncompleteInventory);
  }
}

}  // namespace zomlang::compiler::binder
