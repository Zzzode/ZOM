// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/frozen-definition-inventory.h"

#include "parsed-module-query-test-fixture.h"
#include "zc/core/vector.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/ast/generated/node-traverse.h"
#include "zomlang/compiler/basic/string-pool.h"
#include "zomlang/compiler/basic/zomlang-opts.h"
#include "zomlang/compiler/binder/definition-inventory.h"
#include "zomlang/compiler/binder/stable-identity-candidate-producer.h"
#include "zomlang/compiler/diagnostics/diagnostic-fact-buffer.h"
#include "zomlang/compiler/parser/parser.h"
#include "zomlang/compiler/source/manager.h"

namespace zomlang::compiler::binder {
namespace {

template <typename Scalar>
Scalar scalar(zc::StringPtr text) {
  auto value = Scalar::fromCanonical(text);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid frozen-inventory test scalar");
}

identity::ResolvedVersion version() {
  auto value = identity::ResolvedVersion::fromCanonical("0.0.0"_zc);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid test version");
}

identity::SortedFeatureSet features() {
  zc::Vector<identity::FeatureName> values;
  auto value = identity::SortedFeatureSet::from(zc::mv(values));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid test feature set");
}

identity::SortedTargetFeatureSet targetFeatures() {
  zc::Vector<identity::TargetFeatureName> values;
  auto value = identity::SortedTargetFeatureSet::from(zc::mv(values));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid test target feature set");
}

identity::PackageKey packageKey() {
  zc::Vector<identity::CanonicalPathSegment> path;
  return identity::PackageKey::from(
      identity::CanonicalPackageSource::localPath(
          identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(path))),
      scalar<identity::PackageName>("frozen_inventory"_zc), version(), features());
}

identity::CanonicalTargetSpecificationKey target() {
  auto value = identity::CanonicalTargetSpecificationKey::from(
      scalar<identity::TargetComponentName>("aarch64"_zc),
      scalar<identity::TargetComponentName>("apple"_zc),
      scalar<identity::TargetComponentName>("darwin"_zc),
      scalar<identity::TargetComponentName>("none"_zc),
      scalar<identity::TargetComponentName>("zom"_zc), 64, identity::Endianness::Little,
      targetFeatures());
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid test target");
}

identity::CompilationConfigKey compilation() {
  zc::Maybe<identity::BuildScriptProducerKey> noBuildScript;
  auto value = identity::CompilationConfigKey::from(
      identity::CompilationDomain::Target, target(),
      identity::SemanticCompilerOptionsKey::from(2026, true, false, false), zc::mv(noBuildScript));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid test compilation");
}

identity::CrateKey crateKey() {
  auto value =
      identity::CrateKey::from(packageKey(), identity::CrateTargetKind::Library,
                               scalar<identity::TargetName>("frozen_inventory"_zc), compilation());
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid test crate");
}

identity::SourceFileKey sourceKey() {
  zc::Vector<identity::CanonicalPathSegment> path;
  path.add(scalar<identity::CanonicalPathSegment>("frozen-inventory.zom"_zc));
  return identity::SourceFileKey::from(
      crateKey(), identity::SourceOriginKey::localFile(
                      identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(path))));
}

identity::ModuleKey moduleKey(zc::StringPtr name = "root"_zc) {
  zc::Vector<identity::ModulePathSegment> path;
  path.add(scalar<identity::ModulePathSegment>(name));
  auto value = identity::ModuleKey::from(crateKey(), zc::mv(path));
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid test module");
}

identity::SemanticContextBrand requireContext(identity::SemanticContextFactory& factory) {
  ZC_IF_SOME(context, factory.issue()) { return context; }
  ZC_FAIL_REQUIRE("semantic context space exhausted");
}

parser::ParsedTokenSnapshot requireTokens(zc::Maybe<parser::ParsedTokenSnapshot>& value) {
  ZC_IF_SOME(tokens, value) { return zc::mv(tokens); }
  ZC_FAIL_REQUIRE("missing parser token snapshot");
}

enum class FixtureMode : uint8_t { Normal, Swapped, Duplicate, DuplicateGeneric, ForeignAuthority };

struct FrozenInventoryFixture final {
  explicit FrozenInventoryFixture(FixtureMode mode)
      : mode(mode),
        sourceText(mode == FixtureMode::Duplicate
                       ? "module root;\nclass Alpha {}\nclass Alpha {}\n"_zc
                   : mode == FixtureMode::DuplicateGeneric
                       ? "module root;\nclass Box<T> {}\nclass Box<T> {}\n"_zc
                       : "module root;\nclass Alpha {}\nstruct Beta {}\n"_zc),
        sources(zc::heap<source::SourceManager>()),
        buffer(sources->addMemBufferCopy(sourceText.asBytes(), "frozen-inventory.zom")),
        context(requireContext(factory)),
        registries(createRegistries()) {
    diagnostics::DiagnosticFactBuffer diagnosticFacts(*sources, buffer);
    parser::Parser parser(*sources, diagnosticFacts, options, strings, buffer);
    ZC_IF_SOME(value, parser.parse()) {
      tree = zc::mv(value);
    } else {
      ZC_FAIL_REQUIRE("frozen inventory source did not parse");
    }
    ZC_REQUIRE(!diagnosticFacts.hasErrors());
    tokens = parser.takeTokenSnapshot();

    auto snapshotValue = identity::ImmutableSourceSnapshot::from(
        sourceKey(), zc::heapArray(sources->getEntireTextForBuffer(buffer)));
    ZC_REQUIRE(snapshotValue != zc::none);
    ZC_IF_SOME(value, snapshotValue) { snapshot = zc::mv(value); }
    const auto inventory = DefinitionInventory::collect(tree);
    ZC_REQUIRE(inventory.modules().size() == 1);
    ZC_REQUIRE(inventory.definitions().size() == 2);
    moduleNode = inventory.modules()[0].node;
    firstNode = inventory.definitions()[0].node;
    secondNode = inventory.definitions()[1].node;
    if (mode == FixtureMode::DuplicateGeneric) {
      ZC_REQUIRE(inventory.genericParameters().size() == 2);
      firstGenericNode = inventory.genericParameters()[0].node;
      secondGenericNode = inventory.genericParameters()[1].node;
    }

    ZC_REQUIRE(registries.collectPackage(packageKey()) == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezePackages() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.collectCrate(crateKey()) == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezeCrates() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.collectSourceFile(ZC_ASSERT_NONNULL(snapshot).clone()) ==
               identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezeSourceFiles() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.collectModule(moduleKey()) == identity::FrozenRegistryFailure::None);
    if (mode == FixtureMode::ForeignAuthority) {
      ZC_REQUIRE(registries.collectModule(moduleKey("foreign"_zc)) ==
                 identity::FrozenRegistryFailure::None);
    }
    ZC_REQUIRE(registries.freezeModules() == identity::FrozenRegistryFailure::None);

    zc::Vector<identity::DefinitionKey> producedKeys;
    for (const auto& syntax : inventory.definitions()) {
      auto name = identity::DeclaredDefinitionName::fromSource(tree.ident(syntax.declaredName));
      ZC_REQUIRE(name != zc::none);
      zc::Vector<identity::EnclosingStableOwnerKey> owners;
      zc::Maybe<identity::OverloadHeaderDigest> noOverload;
      ZC_IF_SOME(nameValue, name) {
        auto record = identity::DefinitionIdentityRecord::from(
            moduleKey(), zc::mv(owners), syntax.kind,
            ZC_ASSERT_NONNULL(identity::definitionNamespaceFor(syntax.kind)), zc::mv(nameValue),
            zc::mv(noOverload));
        ZC_REQUIRE(record != zc::none);
        ZC_IF_SOME(recordValue, record) {
          producedKeys.add(identity::DefinitionKey::compute(recordValue));
          zc::Maybe<identity::OverloadHeaderAuthority> noAuthority;
          ZC_REQUIRE(registries.collectDefinition(zc::mv(recordValue), zc::mv(noAuthority)) ==
                     identity::FrozenRegistryFailure::None);
        }
      }
    }
    if (mode == FixtureMode::ForeignAuthority) {
      zc::Vector<identity::EnclosingStableOwnerKey> owners;
      zc::Maybe<identity::OverloadHeaderDigest> noOverload;
      auto record = identity::DefinitionIdentityRecord::from(
          moduleKey("foreign"_zc), zc::mv(owners), identity::DefinitionKind::Class,
          identity::DefinitionNamespace::Type,
          scalar<identity::DeclaredDefinitionName>("Foreign"_zc), zc::mv(noOverload));
      ZC_REQUIRE(record != zc::none);
      ZC_IF_SOME(recordValue, record) {
        zc::Maybe<identity::OverloadHeaderAuthority> noAuthority;
        ZC_REQUIRE(registries.collectDefinition(zc::mv(recordValue), zc::mv(noAuthority)) ==
                   identity::FrozenRegistryFailure::None);
      }
    }
    ZC_REQUIRE(registries.freezeStableIdentities() == identity::FrozenRegistryFailure::None);
    zc::Maybe<identity::GenericParameterKey> genericKey;
    if (mode == FixtureMode::DuplicateGeneric) {
      auto record = identity::GenericParameterIdentityRecord::type(
          identity::StableGenericParameterOwnerKey::definition(producedKeys[0].clone()), 0);
      genericKey = identity::GenericParameterKey::compute(record);
      ZC_REQUIRE(registries.collectGenericParameter(zc::mv(record)) ==
                 identity::FrozenRegistryFailure::None);
    }
    ZC_REQUIRE(registries.freezeGenericParameters() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezeCallableParameters() == identity::FrozenRegistryFailure::None);

    FrozenDefinitionInventoryInput input;
    if (mode == FixtureMode::Swapped) {
      input.definitionCandidates.add(
          ProducedDefinitionIdentity{firstNode, producedKeys[1].clone()});
      input.definitionCandidates.add(
          ProducedDefinitionIdentity{secondNode, producedKeys[0].clone()});
    } else {
      input.definitionCandidates.add(
          ProducedDefinitionIdentity{firstNode, producedKeys[0].clone()});
      input.definitionCandidates.add(
          ProducedDefinitionIdentity{secondNode, producedKeys[1].clone()});
    }
    ZC_IF_SOME(keyValue, genericKey) {
      input.genericParameters.add(
          FrozenGenericParameterProjection{firstGenericNode, zc::mv(keyValue)});
    }

    const auto module = ZC_ASSERT_NONNULL(registries.modules().find(moduleKey()));
    auto parsedModule =
        test::requireVerifiedParsedSource(context, registries, ZC_ASSERT_NONNULL(snapshot),
                                          *sources, buffer, requireTokens(tokens), zc::mv(tree));
    auto result = FrozenDefinitionInventoryVerifier::verifySingleModule(
        context, module, parsedModule, registries, zc::mv(input));
    if (result.is<FrozenDefinitionInventoryView>()) {
      view = zc::mv(result.get<FrozenDefinitionInventoryView>());
    } else {
      failureKind = result.get<FrozenInventoryInvariantFact>().kind;
    }
  }

  identity::SemanticIdentityRegistrySet createRegistries() {
    ZC_IF_SOME(value, identity::SemanticIdentityRegistrySet::create(factory, context)) {
      return zc::mv(value);
    }
    ZC_FAIL_REQUIRE("registry family was already claimed");
  }

  FixtureMode mode;
  zc::StringPtr sourceText;
  zc::Own<source::SourceManager> sources;
  basic::LangOptions options;
  basic::StringPool strings;
  source::BufferId buffer;
  ast::Tree tree;
  zc::Maybe<parser::ParsedTokenSnapshot> tokens;
  identity::SemanticContextFactory factory;
  identity::SemanticContextBrand context;
  identity::SemanticIdentityRegistrySet registries;
  zc::Maybe<identity::ImmutableSourceSnapshot> snapshot;
  ast::NodeId moduleNode;
  ast::NodeId firstNode;
  ast::NodeId secondNode;
  ast::NodeId firstGenericNode;
  ast::NodeId secondGenericNode;
  zc::Maybe<FrozenDefinitionInventoryView> view;
  zc::Maybe<FrozenInventoryInvariantKind> failureKind;
};

identity::CanonicalHeaderTypeSyntax predefined(identity::PredefinedTypeKind kind) {
  auto value = identity::CanonicalHeaderTypeSyntax::predefined(kind);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid predefined type fixture");
}

ZC_TEST("StableIdentityCandidateProducer retains every duplicate bound occurrence and site") {
  const auto sourceText =
      "module root;\nfun constrained<T: A + A + A>(value: T) -> T { return value; }\n"_zc;
  auto sources = zc::heap<source::SourceManager>();
  const auto buffer = sources->addMemBufferCopy(sourceText.asBytes(), "frozen-inventory.zom");
  basic::LangOptions options;
  basic::StringPool strings;
  diagnostics::DiagnosticFactBuffer diagnosticFacts(*sources, buffer);
  parser::Parser parser(*sources, diagnosticFacts, options, strings, buffer);
  auto parsedTree = parser.parse();
  ZC_REQUIRE(parsedTree != zc::none);
  ZC_REQUIRE(!diagnosticFacts.hasErrors());
  auto tokens = parser.takeTokenSnapshot();
  ZC_REQUIRE(tokens != zc::none);

  identity::SemanticContextFactory factory;
  const auto context = requireContext(factory);
  auto registriesValue = identity::SemanticIdentityRegistrySet::create(factory, context);
  ZC_REQUIRE(registriesValue != zc::none);
  auto registries = zc::mv(ZC_ASSERT_NONNULL(registriesValue));
  auto snapshot = identity::ImmutableSourceSnapshot::from(
      sourceKey(), zc::heapArray(sources->getEntireTextForBuffer(buffer)));
  ZC_REQUIRE(snapshot != zc::none);
  ZC_REQUIRE(registries.collectPackage(packageKey()) == identity::FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.freezePackages() == identity::FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.collectCrate(crateKey()) == identity::FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.freezeCrates() == identity::FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.collectSourceFile(ZC_ASSERT_NONNULL(snapshot).clone()) ==
             identity::FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.freezeSourceFiles() == identity::FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.collectModule(moduleKey()) == identity::FrozenRegistryFailure::None);
  ZC_REQUIRE(registries.freezeModules() == identity::FrozenRegistryFailure::None);

  const auto inventory = DefinitionInventory::collect(ZC_ASSERT_NONNULL(parsedTree));
  ZC_REQUIRE(inventory.modules().size() == 1);
  auto parsedModule = test::requireVerifiedParsedSource(
      context, registries, ZC_ASSERT_NONNULL(snapshot), *sources, buffer, requireTokens(tokens),
      zc::mv(ZC_ASSERT_NONNULL(parsedTree)));

  auto produced = StableIdentityCandidateProducer::produce(parsedModule.syntax(), moduleKey(),
                                                           inventory.modules()[0].node);
  ZC_REQUIRE(produced.is<StableIdentityCandidateInventory>());
  const auto& result = produced.get<StableIdentityCandidateInventory>();
  ZC_REQUIRE(result.candidates().size() == 1);
  ZC_REQUIRE(result.sites().size() == 4);
  const auto& candidate = result.candidates()[0];
  ZC_REQUIRE(candidate.duplicateBounds().size() == 2);
  ZC_REQUIRE(candidate.overloadHeader() != zc::none);
  ZC_IF_SOME(authority, candidate.overloadHeader()) {
    ZC_EXPECT(authority.header().obligations().size() == 1);
  }
  ZC_EXPECT(candidate.duplicateBounds()[0].first().sameAs(candidate.duplicateBounds()[1].first()));
  ZC_EXPECT(!candidate.duplicateBounds()[0].duplicate().sameAs(
      candidate.duplicateBounds()[1].duplicate()));
}

identity::CanonicalImplHeader implHeader(
    identity::PredefinedTypeKind selfKind = identity::PredefinedTypeKind::I32) {
  zc::Vector<identity::SemanticIdentifier> suffix;
  suffix.add(scalar<identity::SemanticIdentifier>("Trait"_zc));
  auto name = identity::CanonicalNameReference::from(identity::CanonicalNameRoot::relative(),
                                                     zc::mv(suffix));
  ZC_REQUIRE(name != zc::none);
  ZC_IF_SOME(nameValue, name) {
    zc::Vector<identity::CanonicalHeaderTypeSyntax> arguments;
    auto trait = identity::CanonicalTraitReference::from(zc::mv(nameValue), zc::mv(arguments));
    ZC_REQUIRE(trait != zc::none);
    ZC_IF_SOME(traitValue, trait) {
      zc::Vector<identity::CanonicalGenericParameter> generics;
      zc::Vector<identity::CanonicalBoundObligation> obligations;
      auto value = identity::CanonicalImplHeader::from(
          zc::mv(generics), identity::ImplPolarity::Positive, identity::ImplSafety::Safe,
          zc::mv(traitValue), predefined(selfKind), zc::mv(obligations));
      ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
    }
  }
  ZC_FAIL_REQUIRE("invalid implementation header fixture");
}

bool findPath(const ast::Tree& tree, ast::NodeId current, ast::NodeId target,
              zc::Vector<uint32_t>& path) {
  if (current == target) { return true; }
  uint32_t index = 0;
  bool found = false;
  ast::visitChildNodeIds(tree, tree.node(current), [&](ast::NodeId child) {
    const uint32_t childIndex = index++;
    if (found) { return; }
    path.add(childIndex);
    if (findPath(tree, child, target, path)) {
      found = true;
    } else {
      path.removeLast();
    }
  });
  return found;
}

struct ImplOccurrenceFixture final {
  explicit ImplOccurrenceFixture(bool mutateHeader = false)
      : sources(zc::heap<source::SourceManager>()),
        buffer(sources->addMemBufferCopy("module root;\nimpl Trait for i32 {}\n"_zc.asBytes(),
                                         "frozen-inventory.zom")),
        context(requireContext(factory)),
        registries(createRegistries()) {
    diagnostics::DiagnosticFactBuffer diagnosticFacts(*sources, buffer);
    parser::Parser parser(*sources, diagnosticFacts, options, strings, buffer);
    ZC_IF_SOME(value, parser.parse()) {
      tree = zc::mv(value);
    } else {
      ZC_FAIL_REQUIRE("implementation occurrence source did not parse");
    }
    ZC_REQUIRE(!diagnosticFacts.hasErrors());
    tokens = parser.takeTokenSnapshot();
    auto snapshotValue = identity::ImmutableSourceSnapshot::from(
        sourceKey(), zc::heapArray(sources->getEntireTextForBuffer(buffer)));
    ZC_REQUIRE(snapshotValue != zc::none);
    ZC_IF_SOME(value, snapshotValue) { snapshot = zc::mv(value); }
    const auto inventory = DefinitionInventory::collect(tree);
    ZC_REQUIRE(inventory.impls().size() == 1);
    implNode = inventory.impls()[0].node;

    ZC_REQUIRE(registries.collectPackage(packageKey()) == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezePackages() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.collectCrate(crateKey()) == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezeCrates() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.collectSourceFile(ZC_ASSERT_NONNULL(snapshot).clone()) ==
               identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezeSourceFiles() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.collectModule(moduleKey()) == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezeModules() == identity::FrozenRegistryFailure::None);

    zc::Vector<identity::EnclosingStableOwnerKey> owners;
    auto record = identity::ImplIdentityRecord::from(
        moduleKey(), zc::mv(owners),
        implHeader(mutateHeader ? identity::PredefinedTypeKind::Bool
                                : identity::PredefinedTypeKind::I32));
    auto key = identity::ImplKey::compute(record);
    ZC_REQUIRE(registries.collectImpl(zc::mv(record)) == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezeStableIdentities() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezeGenericParameters() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezeCallableParameters() == identity::FrozenRegistryFailure::None);

    const auto module = ZC_ASSERT_NONNULL(registries.modules().find(moduleKey()));
    auto allocator = ZC_ASSERT_NONNULL(ModuleLocalIdentityAllocator::create(context, module));
    occurrence = ZC_ASSERT_NONNULL(allocator.allocateImplOccurrence());
    zc::Vector<uint32_t> path;
    ZC_REQUIRE(findPath(tree, tree.root(), implNode, path));
    auto site = IdentitySyntaxSiteKey::from(moduleKey(), sourceKey(), zc::mv(path));
    ZC_REQUIRE(site != zc::none);
    FrozenDefinitionInventoryInput input;
    ZC_IF_SOME(siteValue, site) {
      input.implOccurrences.add(FrozenImplOccurrenceProjection{
          implNode, occurrence, ImplSourceOccurrenceKey::from(key.clone(), zc::mv(siteValue))});
    }

    auto parsedModule =
        test::requireVerifiedParsedSource(context, registries, ZC_ASSERT_NONNULL(snapshot),
                                          *sources, buffer, requireTokens(tokens), zc::mv(tree));
    auto result = FrozenDefinitionInventoryVerifier::verifySingleModule(
        context, module, parsedModule, registries, zc::mv(input));
    if (result.is<FrozenDefinitionInventoryView>()) {
      view = zc::mv(result.get<FrozenDefinitionInventoryView>());
    } else {
      failureKind = result.get<FrozenInventoryInvariantFact>().kind;
    }
  }

  identity::SemanticIdentityRegistrySet createRegistries() {
    ZC_IF_SOME(value, identity::SemanticIdentityRegistrySet::create(factory, context)) {
      return zc::mv(value);
    }
    ZC_FAIL_REQUIRE("registry family was already claimed");
  }

  zc::Own<source::SourceManager> sources;
  basic::LangOptions options;
  basic::StringPool strings;
  source::BufferId buffer;
  ast::Tree tree;
  zc::Maybe<parser::ParsedTokenSnapshot> tokens;
  identity::SemanticContextFactory factory;
  identity::SemanticContextBrand context;
  identity::SemanticIdentityRegistrySet registries;
  zc::Maybe<identity::ImmutableSourceSnapshot> snapshot;
  ast::NodeId implNode;
  ImplOccurrenceId occurrence;
  zc::Maybe<FrozenDefinitionInventoryView> view;
  zc::Maybe<FrozenInventoryInvariantKind> failureKind;
};

identity::OverloadHeaderAuthority callableHeader(bool hasDefault) {
  zc::Maybe<identity::ReceiverShape> receiver;
  zc::Vector<identity::CanonicalGenericParameter> generics;
  zc::Vector<identity::CanonicalBoundObligation> obligations;
  zc::Vector<identity::CanonicalCallableParameter> parameters;
  parameters.add(identity::CanonicalCallableParameter::from(
      scalar<identity::SemanticIdentifier>("value"_zc),
      predefined(identity::PredefinedTypeKind::I32), hasDefault));
  zc::Maybe<zc::Vector<identity::CanonicalHeaderTypeSyntax>> raises;
  zc::Maybe<identity::ExternalAbi> externalAbi;
  auto header = identity::CanonicalOverloadHeader::from(
      identity::CallableHeaderKind::Function, scalar<identity::DeclaredDefinitionName>("target"_zc),
      zc::mv(receiver), zc::mv(generics), zc::mv(obligations), zc::mv(parameters),
      identity::CanonicalCallableResult::type(predefined(identity::PredefinedTypeKind::I32)),
      zc::mv(raises), zc::mv(externalAbi));
  ZC_IF_SOME(value, header) { return identity::OverloadHeaderAuthority::from(zc::mv(value)); }
  ZC_FAIL_REQUIRE("invalid callable header fixture");
}

struct CallableHeaderMutationFixture final {
  CallableHeaderMutationFixture()
      : sources(zc::heap<source::SourceManager>()),
        buffer(sources->addMemBufferCopy(
            "module root;\nfun target(value: i32) -> i32 { return value; }\n"_zc.asBytes(),
            "frozen-inventory.zom")),
        context(requireContext(factory)),
        registries(createRegistries()) {
    diagnostics::DiagnosticFactBuffer diagnosticFacts(*sources, buffer);
    parser::Parser parser(*sources, diagnosticFacts, options, strings, buffer);
    auto parsed = parser.parse();
    ZC_REQUIRE(parsed != zc::none);
    ZC_REQUIRE(!diagnosticFacts.hasErrors());
    auto tokens = parser.takeTokenSnapshot();
    ZC_REQUIRE(tokens != zc::none);
    auto snapshot = identity::ImmutableSourceSnapshot::from(
        sourceKey(), zc::heapArray(sources->getEntireTextForBuffer(buffer)));
    ZC_REQUIRE(snapshot != zc::none);
    const auto inventory = DefinitionInventory::collect(ZC_ASSERT_NONNULL(parsed));
    ZC_REQUIRE(inventory.definitions().size() == 1);
    const auto function = inventory.definitions()[0].node;

    ZC_REQUIRE(registries.collectPackage(packageKey()) == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezePackages() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.collectCrate(crateKey()) == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezeCrates() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.collectSourceFile(ZC_ASSERT_NONNULL(snapshot).clone()) ==
               identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezeSourceFiles() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.collectModule(moduleKey()) == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezeModules() == identity::FrozenRegistryFailure::None);

    auto overload = callableHeader(true);
    zc::Vector<identity::EnclosingStableOwnerKey> owners;
    zc::Maybe<identity::OverloadHeaderDigest> digest = overload.digest().clone();
    auto record = identity::DefinitionIdentityRecord::from(
        moduleKey(), zc::mv(owners), identity::DefinitionKind::Function,
        identity::DefinitionNamespace::Value, scalar<identity::DeclaredDefinitionName>("target"_zc),
        zc::mv(digest));
    ZC_REQUIRE(record != zc::none);
    ZC_IF_SOME(recordValue, record) {
      key = identity::DefinitionKey::compute(recordValue);
      zc::Maybe<identity::OverloadHeaderAuthority> retained = zc::mv(overload);
      ZC_REQUIRE(registries.collectDefinition(zc::mv(recordValue), zc::mv(retained)) ==
                 identity::FrozenRegistryFailure::None);
    }
    ZC_REQUIRE(registries.freezeStableIdentities() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezeGenericParameters() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezeCallableParameters() == identity::FrozenRegistryFailure::None);

    auto parsedModule = test::requireVerifiedParsedSource(
        context, registries, ZC_ASSERT_NONNULL(snapshot), *sources, buffer, requireTokens(tokens),
        zc::mv(ZC_ASSERT_NONNULL(parsed)));
    FrozenDefinitionInventoryInput input;
    input.definitionCandidates.add(
        ProducedDefinitionIdentity{function, ZC_ASSERT_NONNULL(key).clone()});
    const auto module = ZC_ASSERT_NONNULL(registries.modules().find(moduleKey()));
    auto result = FrozenDefinitionInventoryVerifier::verifySingleModule(
        context, module, parsedModule, registries, zc::mv(input));
    ZC_REQUIRE(result.is<FrozenInventoryInvariantFact>());
    failureKind = result.get<FrozenInventoryInvariantFact>().kind;
  }

  identity::SemanticIdentityRegistrySet createRegistries() {
    ZC_IF_SOME(value, identity::SemanticIdentityRegistrySet::create(factory, context)) {
      return zc::mv(value);
    }
    ZC_FAIL_REQUIRE("registry family was already claimed");
  }

  zc::Own<source::SourceManager> sources;
  basic::LangOptions options;
  basic::StringPool strings;
  source::BufferId buffer;
  identity::SemanticContextFactory factory;
  identity::SemanticContextBrand context;
  identity::SemanticIdentityRegistrySet registries;
  zc::Maybe<identity::DefinitionKey> key;
  FrozenInventoryInvariantKind failureKind;
};

enum class DuplicateBoundMode : uint8_t { Exact, Missing, Swapped };

struct DuplicateBoundFixture final {
  explicit DuplicateBoundFixture(DuplicateBoundMode mode)
      : sources(zc::heap<source::SourceManager>()),
        buffer(sources->addMemBufferCopy(
            "module root;\nfun constrained<T: A + A + A>(value: T) -> T { return value; }\n"_zc
                .asBytes(),
            "frozen-inventory.zom")),
        context(requireContext(factory)),
        registries(createRegistries()) {
    diagnostics::DiagnosticFactBuffer diagnosticFacts(*sources, buffer);
    parser::Parser parser(*sources, diagnosticFacts, options, strings, buffer);
    auto parsed = parser.parse();
    ZC_REQUIRE(parsed != zc::none);
    ZC_REQUIRE(!diagnosticFacts.hasErrors());
    auto tokens = parser.takeTokenSnapshot();
    ZC_REQUIRE(tokens != zc::none);
    auto snapshot = identity::ImmutableSourceSnapshot::from(
        sourceKey(), zc::heapArray(sources->getEntireTextForBuffer(buffer)));
    ZC_REQUIRE(snapshot != zc::none);
    const auto inventory = DefinitionInventory::collect(ZC_ASSERT_NONNULL(parsed));
    ZC_REQUIRE(inventory.modules().size() == 1);
    ZC_REQUIRE(inventory.definitions().size() == 1);
    ZC_REQUIRE(inventory.genericParameters().size() == 1);
    ZC_REQUIRE(inventory.callableParameters().size() == 1);

    ZC_REQUIRE(registries.collectPackage(packageKey()) == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezePackages() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.collectCrate(crateKey()) == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezeCrates() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.collectSourceFile(ZC_ASSERT_NONNULL(snapshot).clone()) ==
               identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezeSourceFiles() == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.collectModule(moduleKey()) == identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezeModules() == identity::FrozenRegistryFailure::None);

    auto parsedModule = test::requireVerifiedParsedSource(
        context, registries, ZC_ASSERT_NONNULL(snapshot), *sources, buffer, requireTokens(tokens),
        zc::mv(ZC_ASSERT_NONNULL(parsed)));
    auto produced = StableIdentityCandidateProducer::produce(parsedModule.syntax(), moduleKey(),
                                                             inventory.modules()[0].node);
    ZC_REQUIRE(produced.is<StableIdentityCandidateInventory>());
    auto& identities = produced.get<StableIdentityCandidateInventory>();
    ZC_REQUIRE(identities.definitions().size() == 1);
    ZC_REQUIRE(identities.candidates().size() == 1);
    const auto& definition = identities.definitions()[0];
    const auto& candidate = identities.candidates()[0];
    ZC_REQUIRE(candidate.duplicateBounds().size() == 2);
    ZC_REQUIRE(candidate.definitionRecord() != zc::none);
    ZC_REQUIRE(candidate.overloadHeader() != zc::none);
    ZC_IF_SOME(record, candidate.definitionRecord()) {
      ZC_IF_SOME(overload, candidate.overloadHeader()) {
        zc::Maybe<identity::OverloadHeaderAuthority> retained = overload.clone();
        ZC_REQUIRE(registries.collectDefinition(record.clone(), zc::mv(retained)) ==
                   identity::FrozenRegistryFailure::None);
      }
    }
    ZC_REQUIRE(registries.freezeStableIdentities() == identity::FrozenRegistryFailure::None);

    auto genericRecord = identity::GenericParameterIdentityRecord::type(
        identity::StableGenericParameterOwnerKey::definition(definition.key.clone()), 0);
    auto genericKey = identity::GenericParameterKey::compute(genericRecord);
    ZC_REQUIRE(registries.collectGenericParameter(zc::mv(genericRecord)) ==
               identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezeGenericParameters() == identity::FrozenRegistryFailure::None);
    auto callableRecord = identity::CallableParameterIdentityRecord::from(
        definition.key.clone(), identity::CallableParameterPosition::ordinary(0));
    auto callableKey = identity::CallableParameterKey::compute(callableRecord);
    ZC_REQUIRE(registries.collectCallableParameter(zc::mv(callableRecord)) ==
               identity::FrozenRegistryFailure::None);
    ZC_REQUIRE(registries.freezeCallableParameters() == identity::FrozenRegistryFailure::None);

    FrozenDefinitionInventoryInput input;
    input.definitionCandidates.add(
        ProducedDefinitionIdentity{definition.node, definition.key.clone()});
    input.genericParameters.add(FrozenGenericParameterProjection{
        inventory.genericParameters()[0].node, zc::mv(genericKey)});
    input.callableParameters.add(FrozenCallableParameterProjection{
        inventory.callableParameters()[0].node, zc::mv(callableKey)});
    if (mode == DuplicateBoundMode::Exact) {
      for (const auto& duplicate : candidate.duplicateBounds()) {
        input.duplicateBounds.add(
            FrozenDuplicateBoundProjection{definition.node, duplicate.clone()});
      }
    } else if (mode == DuplicateBoundMode::Swapped) {
      input.duplicateBounds.add(
          FrozenDuplicateBoundProjection{definition.node, candidate.duplicateBounds()[1].clone()});
      input.duplicateBounds.add(
          FrozenDuplicateBoundProjection{definition.node, candidate.duplicateBounds()[0].clone()});
    }

    const auto module = ZC_ASSERT_NONNULL(registries.modules().find(moduleKey()));
    auto result = FrozenDefinitionInventoryVerifier::verifySingleModule(
        context, module, parsedModule, registries, zc::mv(input));
    if (result.is<FrozenDefinitionInventoryView>()) {
      view = zc::mv(result.get<FrozenDefinitionInventoryView>());
    } else {
      failureKind = result.get<FrozenInventoryInvariantFact>().kind;
    }
  }

  identity::SemanticIdentityRegistrySet createRegistries() {
    ZC_IF_SOME(value, identity::SemanticIdentityRegistrySet::create(factory, context)) {
      return zc::mv(value);
    }
    ZC_FAIL_REQUIRE("registry family was already claimed");
  }

  zc::Own<source::SourceManager> sources;
  basic::LangOptions options;
  basic::StringPool strings;
  source::BufferId buffer;
  identity::SemanticContextFactory factory;
  identity::SemanticContextBrand context;
  identity::SemanticIdentityRegistrySet registries;
  zc::Maybe<FrozenDefinitionInventoryView> view;
  zc::Maybe<FrozenInventoryInvariantKind> failureKind;
};

}  // namespace

ZC_TEST("Frozen inventory publishes registry records and dense node projections") {
  FrozenInventoryFixture fixture(FixtureMode::Normal);
  ZC_REQUIRE(fixture.view != zc::none);
  ZC_IF_SOME(view, fixture.view) {
    ZC_REQUIRE(view.definitions().size() == 2);
    ZC_EXPECT(view.genericParameters().size() == 0);
    ZC_EXPECT(view.callableParameters().size() == 0);
    ZC_EXPECT(view.ownerLocalBindings().size() == 0);
    ZC_EXPECT(view.anonymousEntities().size() == 0);
    ZC_EXPECT(view.implAuthorities().size() == 0);
    ZC_EXPECT(view.impls().size() == 0);
    for (const auto& entry : view.definitions()) {
      ZC_EXPECT(entry.record.kind() == identity::DefinitionKind::Class ||
                entry.record.kind() == identity::DefinitionKind::Struct);
      ZC_EXPECT(view.definitionAt(entry.node) == entry.definition);
      ZC_REQUIRE(view.definitionKey(entry.definition) != zc::none);
      ZC_REQUIRE(view.definitionRecord(entry.definition) != zc::none);
    }
    ZC_EXPECT(view.definitionAt(fixture.moduleNode) == zc::none);
  }
}

ZC_TEST("Frozen inventory rejects a producer node-to-key swap") {
  FrozenInventoryFixture fixture(FixtureMode::Swapped);
  ZC_EXPECT(fixture.view == zc::none);
  ZC_REQUIRE(fixture.failureKind != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(fixture.failureKind) ==
            FrozenInventoryInvariantKind::CanonicalHeaderMismatch);
}

ZC_TEST("Frozen inventory coalesces duplicate definition candidates under one authority") {
  FrozenInventoryFixture fixture(FixtureMode::Duplicate);
  ZC_REQUIRE(fixture.view != zc::none);
  ZC_IF_SOME(view, fixture.view) {
    ZC_REQUIRE(view.definitions().size() == 1);
    ZC_EXPECT(view.definitionAt(fixture.firstNode) != zc::none);
    ZC_EXPECT(view.definitionAt(fixture.secondNode) == zc::none);
  }
}

ZC_TEST("Frozen inventory materializes subordinates only below the selected authority") {
  FrozenInventoryFixture fixture(FixtureMode::DuplicateGeneric);
  ZC_REQUIRE(fixture.view != zc::none);
  ZC_IF_SOME(view, fixture.view) {
    ZC_REQUIRE(view.definitions().size() == 1);
    ZC_REQUIRE(view.genericParameters().size() == 1);
    ZC_EXPECT(view.genericParameterAt(fixture.firstGenericNode) != zc::none);
    ZC_EXPECT(view.genericParameterAt(fixture.secondGenericNode) == zc::none);
  }
}

ZC_TEST("Frozen inventory accepts a context-global authority registry") {
  FrozenInventoryFixture fixture(FixtureMode::ForeignAuthority);
  ZC_EXPECT(fixture.failureKind == zc::none);
  ZC_REQUIRE(fixture.view != zc::none);
  ZC_IF_SOME(view, fixture.view) { ZC_EXPECT(view.definitions().size() == 2); }
}

ZC_TEST("Frozen inventory maps each implementation syntax node to an occurrence") {
  ImplOccurrenceFixture fixture;
  ZC_REQUIRE(fixture.view != zc::none);
  ZC_IF_SOME(view, fixture.view) {
    ZC_REQUIRE(view.implAuthorities().size() == 1);
    ZC_REQUIRE(view.impls().size() == 1);
    ZC_EXPECT(view.implAt(fixture.implNode) == fixture.occurrence);
    ZC_EXPECT(view.implAuthority(fixture.occurrence) == view.impls()[0].authority);
    ZC_REQUIRE(view.implKey(view.impls()[0].authority) != zc::none);
    ZC_REQUIRE(view.implRecord(view.impls()[0].authority) != zc::none);
  }
}

ZC_TEST("Frozen inventory rejects a retained callable parameter-default mismatch") {
  CallableHeaderMutationFixture fixture;
  ZC_EXPECT(fixture.failureKind == FrozenInventoryInvariantKind::CanonicalHeaderMismatch);
}

ZC_TEST("Frozen inventory rejects a retained implementation header mismatch") {
  ImplOccurrenceFixture fixture(true);
  ZC_EXPECT(fixture.view == zc::none);
  ZC_REQUIRE(fixture.failureKind != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(fixture.failureKind) ==
            FrozenInventoryInvariantKind::CanonicalHeaderMismatch);
}

ZC_TEST("Frozen inventory accepts the exact duplicate-bound occurrence inventory") {
  DuplicateBoundFixture fixture(DuplicateBoundMode::Exact);
  ZC_EXPECT(fixture.failureKind == zc::none);
  ZC_EXPECT(fixture.view != zc::none);
}

ZC_TEST("Frozen inventory rejects a missing duplicate-bound occurrence") {
  DuplicateBoundFixture fixture(DuplicateBoundMode::Missing);
  ZC_EXPECT(fixture.view == zc::none);
  ZC_REQUIRE(fixture.failureKind != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(fixture.failureKind) ==
            FrozenInventoryInvariantKind::DuplicateBoundMismatch);
}

ZC_TEST("Frozen inventory rejects swapped duplicate-bound occurrence order") {
  DuplicateBoundFixture fixture(DuplicateBoundMode::Swapped);
  ZC_EXPECT(fixture.view == zc::none);
  ZC_REQUIRE(fixture.failureKind != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(fixture.failureKind) ==
            FrozenInventoryInvariantKind::DuplicateBoundMismatch);
}

}  // namespace zomlang::compiler::binder
